// Steam's public search catalog, not featuredcategories.coming_soon (a tiny promotion strip).
// Dates and artwork arrive with the rows, avoiding a request/deadline per title.
#include "glance.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/system.hpp"
#include "json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <unordered_set>

namespace glance {
namespace {
constexpr size_t max_page = 2*1024*1024;
constexpr int page_size = 100;
std::wstring trim(std::wstring_view text) { return std::wstring(ascii_text::trim(text)); }
size_t tagEnd(std::wstring_view html,size_t at) {
    wchar_t quote=0;
    for (;at<html.size();++at) {
        const auto c=html[at];
        if (quote) { if (c==quote) quote=0; }
        else if (c==L'\'' || c==L'"') quote=c;
        else if (c==L'>') return at;
    }
    return html.npos;
}
std::wstring attr(std::wstring_view tag,std::wstring_view key) {
    size_t at=0;
    while (at<tag.size()) {
        while (at<tag.size() && !iswalnum(tag[at]) && tag[at]!=L'-') ++at;
        const size_t name=at;
        while (at<tag.size() && (iswalnum(tag[at]) || tag[at]==L'-' || tag[at]==L'_' || tag[at]==L':')) ++at;
        const auto attribute=tag.substr(name,at-name);
        while (at<tag.size() && iswspace(tag[at])) ++at;
        if (at==tag.size() || tag[at]!=L'=') continue;
        ++at; while (at<tag.size() && iswspace(tag[at])) ++at;
        if (at==tag.size()) break;
        const wchar_t quote=tag[at];
        const bool quoted=quote==L'\'' || quote==L'"';
        if (quoted) ++at;
        const size_t start=at;
        while (at<tag.size() && (quoted ? tag[at]!=quote : !iswspace(tag[at]) && tag[at]!=L'>')) ++at;
        const auto value=tag.substr(start,at-start);
        if (quoted && at<tag.size()) ++at;
        if (attribute==key) return std::wstring(value);
    }
    return {};
}
bool hasClass(std::wstring_view tag,std::wstring_view wanted) {
    const auto classes=attr(tag,L"class"); size_t at=0;
    while (at<classes.size()) {
        while (at<classes.size() && iswspace(classes[at])) ++at;
        const auto start=at; while (at<classes.size() && !iswspace(classes[at])) ++at;
        if (std::wstring_view(classes).substr(start,at-start)==wanted) return true;
    }
    return false;
}
std::wstring entities(std::wstring_view text) {
    std::wstring result;
    for (size_t at=0;at<text.size();) {
        if (text[at]!=L'&') { result+=text[at++]; continue; }
        const auto end=text.find(L';',at+1);
        if (end==text.npos || end-at>14) { result+=text[at++]; continue; }
        const auto name=text.substr(at+1,end-at-1); uint32_t point=0;
        if (name==L"amp") point=L'&'; else if (name==L"quot") point=L'"'; else if (name==L"apos" || name==L"#39") point=L'\'';
        else if (name==L"lt") point=L'<'; else if (name==L"gt") point=L'>'; else if (name==L"nbsp") point=L' ';
        else if (name==L"ndash") point=0x2013; else if (name==L"mdash") point=0x2014; else if (name==L"hellip") point=0x2026;
        else if (name.starts_with(L"#")) {
            const bool hex=name.size()>2 && (name[1]==L'x' || name[1]==L'X');
            const auto digits=name.substr(hex ? 2 : 1); const uint32_t base=hex ? 16 : 10;
            for (const auto c : digits) {
                const int value=c>=L'0' && c<=L'9' ? c-L'0' : c>=L'a' && c<=L'f' ? c-L'a'+10 : c>=L'A' && c<=L'F' ? c-L'A'+10 : -1;
                if (value<0 || static_cast<uint32_t>(value)>=base || point>0x10ffff/base) { point=0; break; }
                point=point*base+value;
            }
        }
        if (point<32 || point>0x10ffff || (point>=0xd800 && point<=0xdfff)) { result+=text[at++]; continue; }
        if (point<=0xffff || sizeof(wchar_t)==4) result+=static_cast<wchar_t>(point); // UTF-32 on Linux.
        else { point-=0x10000; result+=static_cast<wchar_t>(0xd800+(point>>10)); result+=static_cast<wchar_t>(0xdc00+(point&1023)); }
        at=end+1;
    }
    return result;
}
std::wstring plain(std::wstring_view html) {
    std::wstring text;
    for (size_t at=0;at<html.size();) {
        if (html[at]==L'<') { const auto end=tagEnd(html,at+1); if (end==html.npos) break; at=end+1; }
        else { const auto c=html[at++]; text+=iswspace(c) ? L' ' : c; }
    }
    return trim(entities(text));
}
std::optional<std::wstring> classText(std::wstring_view row,std::wstring_view key) {
    for (size_t at=0;(at=row.find(L'<',at))!=row.npos;) {
        const auto end=tagEnd(row,at+1); if (end==row.npos) return std::nullopt;
        size_t name_end=at+1; while (name_end<end && iswalnum(row[name_end])) ++name_end;
        const auto name=row.substr(at+1,name_end-at-1);
        if (!name.empty() && hasClass(row.substr(name_end,end-name_end),key)) {
            const auto close=row.find(L"</"+std::wstring(name),end+1);
            if (close==row.npos) return std::nullopt;
            return plain(row.substr(end+1,close-end-1));
        }
        at=end+1;
    }
    return std::nullopt;
}
std::wstring firstImage(std::wstring_view row) {
    const auto at=row.find(L"<img"); if (at==row.npos) return {};
    const auto end=tagEnd(row,at+4); if (end==row.npos) return {};
    auto image=entities(attr(row.substr(at+4,end-at-4),L"src"));
    return imageUrl(image) ? image : L"";
}
// data-ds-tagids="[1662,21,1695]": the row's store tags, most voted first.
std::vector<uint32_t> tagIds(std::wstring_view text) {
    std::vector<uint32_t> ids; uint64_t value=0; bool digits=false;
    const auto flush=[&] { if (digits && value && value<=UINT32_MAX) ids.push_back(static_cast<uint32_t>(value)); value=0; digits=false; };
    for (const auto c : text) {
        if (c>=L'0' && c<=L'9') { if (value<=UINT32_MAX) value=value*10+static_cast<uint64_t>(c-L'0'); digits=true; }
        else { flush(); if (ids.size()>=40) break; }
    }
    flush();
    return ids;
}
// The review summary's tooltip: "Very Positive<br>94% of the 1,234 user reviews for this game are positive."
void reviewSummary(std::wstring_view row,Item& item) {
    for (size_t at=0;(at=row.find(L'<',at))!=row.npos;) {
        const auto end=tagEnd(row,at+1); if (end==row.npos) return;
        const auto tag=row.substr(at+1,end-at-1);
        if (!hasClass(tag,L"search_review_summary")) { at=end+1; continue; }
        const auto summary=entities(attr(tag,L"data-tooltip-html"));
        const auto percent=summary.find(L'%');
        if (percent==summary.npos) return;
        size_t start=percent; while (start && iswdigit(summary[start-1])) --start;
        if (start==percent || percent-start>3) return;
        const int share=std::stoi(summary.substr(start,percent-start));
        if (share>100) return;
        item.review_percent=share;
        const auto of=summary.find(L" of the ",percent);
        if (of==summary.npos) return;
        uint64_t count=0; bool any=false;
        for (size_t i=of+8;i<summary.size() && count<=UINT32_MAX;++i) {
            const auto c=summary[i];
            if (c>=L'0' && c<=L'9') { count=count*10+static_cast<uint64_t>(c-L'0'); any=true; }
            else if (c!=L',' && c!=L'.') break;
        }
        if (any && count<=UINT32_MAX) item.review_count=static_cast<uint32_t>(count);
        return;
    }
}
std::string readPage(const std::filesystem::path& file) {
    std::error_code error; const auto size=std::filesystem::file_size(file,error);
    if (error || size>max_page) return {};
    std::string bytes(static_cast<size_t>(size),'\0'); std::ifstream input(file,std::ios::binary);
    if (!input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
bool freshPage(const std::filesystem::path& file) {
    std::error_code error; const auto stamp=std::filesystem::last_write_time(file,error);
    if (error) return false;
    const auto age=std::filesystem::file_time_type::clock::now()-stamp;
    return age>=decltype(age)::zero() && age<std::chrono::minutes(30);
}
std::optional<UpcomingPage> page(const std::filesystem::path& file,const std::wstring& url,int start,bool refresh,const Get& get,bool& stale) {
    auto saved=parseUpcomingSearch(readPage(file));
    if (saved && saved->start!=start) saved.reset();
    if (saved && !refresh && freshPage(file)) return saved;
    HttpBudget budget(3500); const auto response=get(url,L"",max_page);
    const std::string bytes(response.body.begin(),response.body.end());
    auto result=response.status==200 ? parseUpcomingSearch(bytes) : std::nullopt;
    if (result && result->start==start) {
        if (!httpCancelled()) atomic_file::write(file,bytes);
        return result;
    }
    stale=bool(saved); return saved;
}
int dateKey(const ReleaseDate& date) { return date.year*10000+date.month*100+date.day; }
int key(std::chrono::sys_days day) {
    const std::chrono::year_month_day d{day};
    return int(d.year())*10000+unsigned(d.month())*100+unsigned(d.day());
}
std::chrono::sys_days day(int ymd) {
    return std::chrono::year_month_day{std::chrono::year(ymd/10000),std::chrono::month(ymd/100%100),std::chrono::day(ymd%100)};
}
}

std::optional<UpcomingPage> parseUpcomingSearch(std::string_view bytes) {
    const auto root=json::parse(bytes,max_page);
    const auto* success=root ? root->get("success") : nullptr;
    const auto* source=root ? root->get("results_html") : nullptr;
    const auto* total=root ? root->get("total_count") : nullptr;
    const auto* start=root ? root->get("start") : nullptr;
    if (!success || !(success->flag || success->intOr()==1) || !source || source->type!=json::Value::Type::string ||
        !total || total->intOr(-1)<0 || !start || start->intOr(-1)<0) return std::nullopt;
    UpcomingPage result; result.total=total->intOr(); result.start=start->intOr();
    const auto html=text_encoding::wide(source->text);
    if (html.empty() && !source->text.empty()) return std::nullopt;
    std::unordered_set<uint32_t> seen;
    for (size_t at=0;(at=html.find(L"<a",at))!=html.npos;) {
        if (at+2>=html.size() || !iswspace(html[at+2])) { at+=2; continue; }
        const auto end=tagEnd(html,at+2); if (end==html.npos) return std::nullopt;
        const auto tag=std::wstring_view(html).substr(at+2,end-at-2);
        if (!hasClass(tag,L"search_result_row")) { at=end+1; continue; }
        const auto close=html.find(L"</a>",end+1); if (close==html.npos) return std::nullopt;
        const auto content=std::wstring_view(html).substr(end+1,close-end-1); at=close+4;
        const auto id=attr(tag,L"data-ds-appid");
        if (id.empty() || id.size()>10 || id.find_first_not_of(L"0123456789")!=id.npos) continue;
        const auto numeric=std::stoull(id); if (!numeric || numeric>INT32_MAX || !seen.insert(static_cast<uint32_t>(numeric)).second) continue;
        const auto title=classText(content,L"title"), date=classText(content,L"search_released");
        if (!title || title->empty() || !date) return std::nullopt; // Markup drift must not become an empty successful cache.
        Item item; item.app_id=static_cast<uint32_t>(numeric); item.title=title->substr(0,240);
        item.release=parseReleaseDate(*date); item.source=L"Steam";
        item.url=L"https://store.steampowered.com/app/"+id+L"/"; item.image=firstImage(content);
        item.tag_ids=tagIds(attr(tag,L"data-ds-tagids"));
        reviewSummary(content,item);
        result.items.push_back(std::move(item));
        if (result.items.size()>200) return std::nullopt;
    }
    if (result.items.empty() && result.start<result.total) return std::nullopt;
    return result;
}

std::vector<Item> selectUpcoming(std::vector<Item> items,int today) {
    const auto current=day(today); const int end=key(current+std::chrono::days(30)), first=key(current-std::chrono::days(6));
    std::map<int,std::vector<Item>> future, recent;
    std::unordered_set<uint32_t> seen;
    // The released list wins an overlap with an outdated coming-soon row for the same game.
    for (bool released : {true,false}) for (auto& item : items) {
        if (item.release.recent!=released || !item.app_id || item.release.precision!=ReleasePrecision::day) continue;
        const int date=dateKey(item.release);
        if (released ? date<first || date>today : date<today || date>end) continue;
        if (!seen.insert(item.app_id).second) continue;
        (released ? recent : future)[date].push_back(std::move(item));
    }
    std::vector<Item> result;
    const auto collect=[&](auto& buckets,size_t limit,bool descending) {
        std::vector<int> dates; for (const auto& [date,values] : buckets) { (void)values; dates.push_back(date); }
        if (descending) std::reverse(dates.begin(),dates.end());
        std::vector<Item> selected;
        for (size_t round=0;round<2 && selected.size()<limit;++round) for (int date : dates) {
            auto& values=buckets[date];
            if (round<values.size()) selected.push_back(std::move(values[round]));
            if (selected.size()==limit) break;
        }
        std::stable_sort(selected.begin(),selected.end(),[&](const auto& a,const auto& b) { return descending ? dateKey(a.release)>dateKey(b.release) : dateKey(a.release)<dateKey(b.release); });
        for (auto& item : selected) result.push_back(std::move(item));
    };
    collect(future,upcoming_limit,false); collect(recent,recent_release_limit,true);
    return result;
}

Feed loadUpcoming(bool english,const std::filesystem::path& cache,bool refresh,const Get& get) {
    (void)english; // Stable English source dates; the renderer localizes their structured values.
    const auto now=system_info::localTime(); const int today=now.year*10000+now.month*100+now.day;
    const int week_start=key(day(today)-std::chrono::days(6));
    struct Source { const wchar_t* key; const wchar_t* query; int pages; bool recent; };
    const Source sources[]={{L"popular",L"filter=popularcomingsoon",2,false},
        {L"soon",L"filter=comingsoon&sort_by=Released_ASC",4,false},
        {L"recent",L"filter=recentlyreleased&sort_by=Released_DESC",6,true}};
    Feed result; std::vector<Item> candidates; int available=0;
    HttpBudget budget(30000);
    for (const auto& source : sources) for (int index=0;index<source.pages;++index) {
        if (httpCancelled()) return {};
        const int start=index*page_size; bool stale=false;
        const auto file=cache/(L"calendar-v1-"+std::wstring(source.key)+L"-"+std::to_wstring(start)+L".json");
        const auto url=L"https://store.steampowered.com/search/results/?query&start="+std::to_wstring(start)+L"&count=100&dynamic_data=&"+
            source.query+L"&category1=998&infinite=1&cc=pl&l=english";
        auto data=page(file,url,start,refresh,get,stale);
        if (!data) { result.dates_partial=true; break; }
        ++available; result.stale=result.stale || stale;
        if (stale) result.dates_partial=true;
        bool passed_week=false;
        for (auto& item : data->items) {
            item.release.recent=source.recent;
            passed_week=source.recent && item.release.precision==ReleasePrecision::day && dateKey(item.release)<week_start;
            candidates.push_back(std::move(item));
        }
        if (passed_week || start+page_size>=data->total) break;
    }
    result.items=selectUpcoming(std::move(candidates),today); result.unavailable=available==0;
    return result;
}
}
