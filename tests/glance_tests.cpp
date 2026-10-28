#include "../src/net/glance.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/system.hpp"
#include <iostream>
#ifdef _WIN32
#include <objbase.h>
#endif
#include "image_fixture.hpp"
#include <chrono>
#include <algorithm>
#include <map>

namespace {
constexpr auto steam = R"({"new_releases":{"items":[{"id":10,"name":"Nowa gra","large_capsule_image":"https://shared.akamai.steamstatic.com/a.jpg"},{"id":10,"name":"Duplicate"},{"id":-1,"name":"Invalid"}]},"coming_soon":{"items":[]}})";
constexpr auto rss = R"(<?xml version="1.0" encoding="UTF-8"?><rss version="2.0" xmlns:media="http://search.yahoo.com/mrss/"><channel>
<item><title>Zażółć &amp; gra</title><link>https://www.eurogamer.pl/news</link><pubDate>Tue, 13 Oct 2026 08:59:39 +0000</pubDate><media:content url="https://assetsio.gnwcdn.com/a.jpg?format=jpg&amp;auto=webp"/><description><![CDATA[<script>alert(1)</script>]]></description></item>
<item><title>Duplicate</title><link>https://www.eurogamer.pl/news</link></item><item><title>Unsafe</title><link>file:///C:/bad.exe</link></item></channel></rss>)";
HttpResponse response(std::string_view text) { return {200,{text.begin(),text.end()}}; }
std::string announcement(uint32_t app, uint64_t date, std::string gid = {}) {
    if (gid.empty()) gid = std::to_string(app)+"001";
    return "{\"appnews\":{\"appid\":"+std::to_string(app)+",\"newsitems\":[{\"appid\":"+std::to_string(app)+
        ",\"gid\":\""+gid+"\",\"title\":\"Aktualizacja gry\",\"url\":\"https://steamstore-a.akamaihd.net/news/externalpost/steam_community_announcements/"+gid+
        "\",\"feedname\":\"steam_community_announcements\",\"date\":"+std::to_string(date)+"}]}}";
}
std::string escapeJson(std::string_view text) {
    std::string result;
    for (const char c : text) {
        if (c=='"' || c=='\\') { result+='\\'; result+=c; }
        else if (c=='\n') result+="\\n"; else if (c=='\r') result+="\\r"; else if (c=='\t') result+="\\t"; else result+=c;
    }
    return result;
}
std::string searchRow(int id,std::string title,std::string date) {
    return "<a class=\"search_result_row ds_collapse_flag\" data-ds-appid=\""+std::to_string(id)+
        "\"><div class=\"search_capsule\"><img src=\"https://shared.fastly.steamstatic.com/a.jpg?t=1&amp;x=2\"></div>"
        "<span class=\"title\">"+title+"</span><div class=\"search_released responsive_secondrow\">\n "+date+" </div></a>";
}
std::string searchPage(std::string html,int total,int start=0) {
    return "{\"success\":1,\"start\":"+std::to_string(start)+",\"total_count\":"+std::to_string(total)+",\"results_html\":\""+escapeJson(html)+"\"}";
}
std::string dateText(std::chrono::sys_days date) {
    static constexpr const char* months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    const std::chrono::year_month_day d{date};
    return std::to_string(unsigned(d.day()))+" "+months[unsigned(d.month())-1]+", "+std::to_string(int(d.year()));
}
}
int main(int argc, char** argv) {
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-glance-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path,ec); CoUninitialize(); } } cleanup{root};
    // Explicit opt-in smoke, never registered with CTest; no accounts or API keys.
    if (argc == 2 && std::string_view(argv[1]) == "--live") {
        for (int i = 0; i < glance::section_count; ++i) {
            const auto feed = i == static_cast<int>(glance::Section::owned) ?
                glance::loadOwned({{570,L"Dota 2",100,true},{730,L"Counter-Strike 2",90,true}},false,root,false) :
                i==static_cast<int>(glance::Section::upcoming) ? glance::loadUpcoming(false,root,false) : glance::load(static_cast<glance::Section>(i),false,root,false);
            std::cout << "section=" << i << " items=" << feed.items.size() << " unavailable=" << feed.unavailable << '\n';
            if (feed.unavailable || feed.items.empty()) return 90+i;
            if (i==static_cast<int>(glance::Section::upcoming)) {
                std::map<int,int> future,recent;
                for (const auto& item : feed.items) {
                    const auto& d=item.release; ++(d.recent ? recent : future)[d.year*10000+d.month*100+d.day];
                }
                std::cout << "future_days=" << future.size() << " recent_days=" << recent.size() << " partial=" << feed.dates_partial << '\n';
                for (const auto& [date,count] : future) std::cout << "  upcoming " << date << " games=" << count << '\n';
                for (const auto& [date,count] : recent) std::cout << "  released " << date << " games=" << count << '\n';
                if (future.size()<2) return 99;
            }
            const auto image = glance::thumbnail(feed.items[0],root / (std::to_wstring(i)+L".img"));
            std::cout << "thumbnail_bytes=" << image.size() << '\n';
            if (image.empty()) return 94+i;
        }
        return 0;
    }
    const auto games = glance::parseSteam(steam,glance::Section::steam);
    if (!games || games->size() != 1 || (*games)[0].url != L"https://store.steampowered.com/app/10/") return 1;
    if (glance::parseSteam("{}",glance::Section::steam) || glance::parseSteam("garbage",glance::Section::upcoming)) return 2;
    const auto empty = glance::parseSteam(steam,glance::Section::upcoming);
    if (!empty || !empty->empty()) return 3;
    const auto news = glance::parseRss(rss);
    if (!news || news->size()!=1 || (*news)[0].title != L"Zażółć & gra" || (*news)[0].date != L"13 Oct 2026" ||
        (*news)[0].image != L"https://assetsio.gnwcdn.com/a.jpg?format=jpg") {
        std::cerr << "RSS parsed=" << bool(news) << " count=" << (news ? news->size() : 0) << " allowed=" << glance::articleUrl(L"https://www.eurogamer.pl/news") << '\n';
        if (news && !news->empty()) std::wcerr << news->front().title << L" | " << news->front().date << L" | " << news->front().image << L'\n';
        return 4;
    }
    if (glance::parseRss("<html/>") || glance::parseRss("<rss><channel><item>") ||
        glance::parseRss("<!DOCTYPE rss [<!ENTITY x SYSTEM 'file:///C:/secret'>]><rss><channel><item><title>&x;</title></item></channel></rss>")) return 5;
    for (const auto* url : {L"file:///C:/x", L"https://www.eurogamer.pl.evil.test/a", L"https://www.eurogamer.pl@evil.test/a", L"https://user:pass@www.eurogamer.pl/a", L"https://www.eurogamer.pl:444/a", L"javascript:alert(1)"})
        if (glance::articleUrl(url)) return 6;
    int calls = 0;
    const glance::Get get = [&](const auto&, const auto&, size_t) { ++calls; return response(steam); };
    const auto fresh = glance::load(glance::Section::steam,false,root,false,get);
    if (calls!=1 || fresh.unavailable || fresh.items.size()!=1) return 7;
    const auto cached = glance::load(glance::Section::steam,false,root,false,get);
    if (calls!=1 || cached.unavailable || cached.items.size()!=1) return 8;
    const glance::Get offline = [&](const auto&, const auto&, size_t) { ++calls; return HttpResponse{503}; };
    const auto stale = glance::load(glance::Section::steam,false,root,true,offline);
    if (calls!=2 || !stale.stale || !stale.unavailable || stale.items.size()!=1) return 9;
    const auto missing = glance::load(glance::Section::news,false,root,false,offline);
    if (!missing.unavailable || missing.stale || !missing.items.empty()) return 10;
    // Malformed refreshes must not destroy the last good cache.
    const auto invalid = glance::load(glance::Section::steam,false,root,true,[](const auto&,const auto&,size_t) { return response("{bad"); });
    if (!invalid.stale || invalid.items.size()!=1) return 11;
    std::filesystem::last_write_time(root / L"pl-0.feed",std::filesystem::file_time_type::clock::now()-std::chrono::hours(1));
    glance::load(glance::Section::steam,false,root,false,get);
    if (calls!=4) return 12;
    // Languages have distinct cache identities, and empty valid feeds are not failures.
    const auto english = glance::load(glance::Section::upcoming,true,root,false,get);
    if (calls!=5 || english.unavailable || !english.items.empty()) return 13;
    // Official developer news, exact app ownership, source URL and date validation.
    const auto personal = glance::parseNews(announcement(570,1791332561),{570,L"Dota 2"});
    if (!personal || personal->size()!=1 || personal->front().app_id!=570 || personal->front().source!=L"Dota 2" ||
        personal->front().published!=1791332561 || personal->front().date!=L"07.10.2026") return 20;
    if (glance::parseNews(announcement(730,1791332561),{570,L"Dota 2"}) || glance::parseNews("{}",{570,L"Dota 2"})) return 21;
    auto publisher = announcement(570,1791332561);
    const auto tag = publisher.rfind("steam_community_announcements"); publisher.replace(tag,29,"third_party");
    const auto third_party = glance::parseNews(publisher,{570,L"Dota 2"});
    if (!third_party || !third_party->empty()) return 22;
    std::vector<uint32_t> requested;
    const glance::Get news_get = [&](const std::wstring& url,const auto&,size_t) {
        const auto at = url.find(L"appid=");
        if (at == url.npos || url.find(L"feeds=steam_community_announcements") == url.npos) return HttpResponse{400};
        const auto app = static_cast<uint32_t>(std::stoul(url.substr(at+6))); requested.push_back(app);
        return response(announcement(app,1700000000+app));
    };
    auto owned = glance::loadOwned({{570,L"Dota 2",200,true},{730,L"CS2",100,false},{570,L"Duplicate",0,false}},false,root,false,news_get);
    if (requested != std::vector<uint32_t>{570,730} || owned.games_total!=2 || owned.games_checked!=2 || owned.items.size()!=2 ||
        owned.items[0].app_id!=730 || owned.items[1].app_id!=570) return 23; // Sort by publication, not play history.
    requested.clear();
    owned = glance::loadOwned({{570,L"Renamed",0,true}},false,root,false,news_get);
    if (!requested.empty() || owned.items.size()!=1 || owned.items[0].app_id!=570 || owned.items[0].source!=L"Renamed") return 24;
    owned = glance::loadOwned({{570,L"Dota 2",0,true}},false,root,true,offline);
    if (!owned.stale || owned.unavailable || owned.items.size()!=1) return 25;
    owned = glance::loadOwned({},false,root,true,news_get);
    if (!requested.empty() || !owned.items.empty() || owned.games_total) return 26;
    std::vector<glance::OwnedGame> many;
    for (uint32_t i = 0; i < 30; ++i) many.push_back({1000+i,L"Game",i,true});
    owned = glance::loadOwned(many,false,root,false,news_get);
    if (requested.size()!=glance::owned_limit || requested.front()!=1029 || requested.back()!=1018 ||
        owned.games_total!=30 || owned.games_selected!=glance::owned_limit || owned.games_checked!=glance::owned_limit || owned.items.size()!=glance::item_limit) return 27;
    requested.clear();
    std::atomic_bool cancelled{true};
    { HttpCancellation scope(cancelled); owned = glance::loadOwned(many,false,root,true,news_get); }
    if (!requested.empty() || !owned.items.empty()) return 28;
    // Both Steam image syntaxes, including the localized clan placeholder.
    auto with_image = announcement(570,1791332561);
    const auto add_contents = [&](std::string value) {
        auto text = with_image;
        const auto at = text.find("\"date\":"); text.insert(at,"\"contents\":\""+value+"\",");
        return glance::parseNews(text,{570,L"Dota 2"});
    };
    const std::string hash(40,'a');
    const auto bb = add_contents("[img]{STEAM_CLAN_IMAGE}/3703047/"+hash+".png[/img]");
    const auto loc = add_contents("[img src=\\\"{STEAM_CLAN_LOC_IMAGE}/3703047/"+hash+".png\\\"][/img]");
    const auto expected_image = L"https://clan.akamai.steamstatic.com/images/3703047/"+std::wstring(40,L'a')+L".png";
    if (!bb || !loc || bb->size()!=1 || loc->size()!=1 || bb->front().image!=expected_image || loc->front().image!=expected_image) return 30;
    if (glance::imageUrl(L"https://clan.akamai.steamstatic.com.evil.test/a.png")) return 31;
    const std::string events = "{\"events\":[{\"appid\":570,\"announcement_body\":{\"headline\":\"Aktualizacja gry\",\"posttime\":1791332561,\"clanid\":\"3703047\"},"
        "\"jsondata\":\"{\\\"localized_capsule_image\\\":[\\\""+hash+".png\\\"]}\"}]}";
    if (glance::steamEventImage(events,personal->front(),false)!=expected_image) return 32;
    auto mismatch = personal->front(); ++mismatch.published;
    if (!glance::steamEventImage(events,mismatch,false).empty()) return 33;
    const auto png = pngFixture(120,48,32);
    glance::Item thumbnail; thumbnail.image = L"https://shared.akamai.steamstatic.com/broken.png";
    thumbnail.fallback_images = {L"https://shared.fastly.steamstatic.com/good.png"};
    int bad_calls=0, good_calls=0;
    const glance::Get images = [&](const std::wstring& url,const auto&,size_t) {
        if (url == thumbnail.image) { ++bad_calls; return response("<html>not an image</html>"); }
        ++good_calls; return HttpResponse{200,png};
    };
    const auto image_file = root/L"thumbnail.img";
    if (glance::thumbnail(thumbnail,image_file,images)!=png || bad_calls!=1 || good_calls!=1) return 34;
    if (glance::thumbnail(thumbnail,image_file,images)!=png || good_calls!=1) return 35; // Reuse the good fallback cache.
    atomic_file::write(image_file,std::string_view("broken cached image"));
    if (glance::thumbnail(thumbnail,image_file,images)!=png || good_calls!=2) return 36;
    // An article banner can be present only in the event metadata, not its body.
    glance::ImageSession image_session;
    int metadata_calls=0;
    const glance::Get event_get = [&](const std::wstring& url,const auto&,size_t) {
        if (url.find(L"/events/")!=url.npos) { ++metadata_calls; return response(events); }
        if (url == expected_image) return HttpResponse{200,png};
        return HttpResponse{404};
    };
    if (glance::thumbnail(personal->front(),root/L"event-1.img",event_get,false,&image_session)!=png ||
        glance::thumbnail(personal->front(),root/L"event-2.img",event_get,false,&image_session)!=png || metadata_calls!=1) return 37;
    // Content-addressed store headers replace the legacy path after an actual download failure.
    glance::Item moved; moved.app_id = 10; moved.image = L"https://cdn.akamai.steamstatic.com/steam/apps/10/header.jpg";
    const std::wstring hashed = L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/10/hash/header.jpg";
    const glance::Get moved_get = [&](const std::wstring& url,const auto&,size_t) {
        if (url.find(L"/api/appdetails")!=url.npos) return response(R"({"10":{"success":true,"data":{"header_image":"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/10/hash/header.jpg"}}})");
        if (url==hashed) return HttpResponse{200,png};
        return HttpResponse{404};
    };
    if (glance::thumbnail(moved,root/L"moved.img",moved_get)!=png) return 38;
    atomic_file::write(root/L"legacy"/L"steam-news"/L"pl-570.json",announcement(570,1791332561));
    const auto migrated = glance::loadOwned({{570,L"Dota 2",0,true}},false,root/L"legacy",false,offline);
    if (migrated.items.size()!=1 || !migrated.stale || migrated.unavailable) return 39;
    using Precision=glance::ReleasePrecision;
    const auto leap=glance::parseReleaseDate(L"29 Feb, 2028");
    if (leap.precision!=Precision::day || leap.day!=29 || leap.month!=2 || leap.year!=2028) return 40;
    for (const auto* text : {L"29 Feb, 2027",L"31 Apr, 2027",L"9999 Jan, 2027",L"14 Marmalade, 2027",L"Coming soon",L"To be announced"})
        if (glance::parseReleaseDate(text).precision!=Precision::unknown) return 41;
    if (glance::parseReleaseDate(L"October 2027").precision!=Precision::month ||
        glance::parseReleaseDate(L"Q4 2027").precision!=Precision::quarter || glance::parseReleaseDate(L"2028").precision!=Precision::year) return 42;
    const auto catalog=glance::parseUpcomingSearch(searchPage(searchRow(31,"Tidy &amp; Bright &#x1F680;", "20 Oct, 2027"),1));
    if (!catalog || catalog->items.size()!=1 || catalog->items[0].title!=L"Tidy & Bright \U0001F680" ||
        catalog->items[0].image!=L"https://shared.fastly.steamstatic.com/a.jpg?t=1&x=2" || catalog->items[0].release.day!=20) return 43;
    if (glance::parseUpcomingSearch(searchPage("<html>Sign in</html>",100)) || glance::parseUpcomingSearch("{}")) return 44;
    const auto empty_search=glance::parseUpcomingSearch(searchPage("",0));
    if (!empty_search || !empty_search->items.empty()) return 45;
    const auto now=system_info::localTime();
    const auto today=std::chrono::sys_days(std::chrono::year(now.year)/now.month/now.day);
    const auto future_html=searchRow(31,"Later",dateText(today+std::chrono::days(10)))+searchRow(32,"First",dateText(today+std::chrono::days(2)))+
        searchRow(33,"Quarter","Q4 2099")+searchRow(34,"Unknown","Coming soon")+searchRow(35,"Past",dateText(today-std::chrono::days(1)))+
        searchRow(36,"Too late",dateText(today+std::chrono::days(40)));
    const auto recent_html=searchRow(40,"Released today",dateText(today))+searchRow(41,"Released yesterday",dateText(today-std::chrono::days(1)))+
        searchRow(42,"Too old",dateText(today-std::chrono::days(7)));
    int release_calls=0;
    const glance::Get release_get=[&](const std::wstring& url,const auto&,size_t) {
        ++release_calls;
        if (url.find(L"/search/results/")==url.npos) return HttpResponse{400};
        const bool recent=url.find(L"recentlyreleased")!=url.npos;
        return response(searchPage(recent ? recent_html : future_html,recent ? 3 : 6));
    };
    const auto timeline=glance::loadUpcoming(false,root/L"timeline",false,release_get);
    if (timeline.items.size()!=4 || timeline.dates_partial || release_calls!=3 || timeline.items[0].app_id!=32 || timeline.items[1].app_id!=31 ||
        !timeline.items[2].release.recent || timeline.items[2].app_id!=40 || timeline.items[3].app_id!=41) return 46;
    release_calls=0;
    glance::loadUpcoming(false,root/L"timeline",false,release_get);
    if (release_calls) return 47; // Dates and images arrive in the catalog: no per-app requests.
    const auto old_calendar=glance::loadUpcoming(false,root/L"timeline",true,offline);
    if (!old_calendar.stale || old_calendar.unavailable || old_calendar.items.size()!=4) return 48;
    const auto no_calendar=glance::loadUpcoming(false,root/L"no-calendar",false,offline);
    if (!no_calendar.unavailable || !no_calendar.items.empty()) return 49;
    // A hundred releases today cannot crowd tomorrow or the end of the 30-day horizon out.
    std::vector<glance::Item> dense;
    const auto append=[&](int id,std::wstring date,bool recent=false) {
        glance::Item item; item.app_id=id; item.title=L"Game"; item.release=glance::parseReleaseDate(date); item.release.recent=recent; dense.push_back(item);
    };
    const auto epoch=std::chrono::sys_days(std::chrono::year(2026)/10/14);
    for (int i=0;i<100;++i) append(1000+i,L"14 Oct, 2026");
    for (int offset=1;offset<=31;++offset) for (int n=0;n<4;++n) {
        const auto date=dateText(epoch+std::chrono::days(offset)); append(2000+offset*10+n,std::wstring(date.begin(),date.end()));
    }
    append(1000,L"14 Oct, 2026",true); // Exact AppID overlap: released takes precedence.
    for (int offset=0;offset<8;++offset) for (int n=0;n<5;++n) {
        const auto date=dateText(epoch-std::chrono::days(offset)); append(3000+offset*10+n,std::wstring(date.begin(),date.end()),true);
    }
    const auto diverse=glance::selectUpcoming(std::move(dense),20261014);
    std::map<int,int> forward_days,back_days; int duplicated=0;
    for (const auto& item : diverse) {
        const auto& d=item.release; const int day=d.year*10000+d.month*100+d.day;
        ++(d.recent ? back_days : forward_days)[day];
        if (item.app_id==1000) { ++duplicated; if (!d.recent) return 50; }
    }
    if (forward_days.size()!=31 || !forward_days.contains(20261113) || forward_days.contains(20261114) || back_days.size()!=7 ||
        !back_days.contains(20261008) || back_days.contains(20261007) || diverse.size()!=glance::calendar_limit || duplicated!=1) return 51;
    for (const auto& [date,count] : forward_days) { (void)date; if (count>2) return 52; }
    if (std::count_if(diverse.begin(),diverse.end(),[](const auto& item) { return item.release.recent; })!=glance::recent_release_limit) return 53;
    std::string crowded;
    for (int i=0;i<100;++i) crowded+=searchRow(1000+i,"Today",dateText(today));
    int paged_calls=0;
    const glance::Get paged=[&](const std::wstring& url,const auto&,size_t) {
        ++paged_calls;
        if (url.find(L"recentlyreleased")!=url.npos) return response(searchPage("",0));
        const int start=std::stoi(url.substr(url.find(L"start=")+6));
        return response(searchPage(start==0 ? crowded : searchRow(9000,"Next week",dateText(today+std::chrono::days(7))),101,start));
    };
    const auto later_page=glance::loadUpcoming(false,root/L"pagination",false,paged);
    if (paged_calls!=5 || later_page.items.size()!=3 || later_page.items.back().app_id!=9000 || later_page.dates_partial) return 54;
    std::cout << "Glance parsers, source URLs, UTF-8, cache TTL, stale fallback and refresh passed\n";
}
