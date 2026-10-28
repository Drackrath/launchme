#include "glance.hpp"
#include "json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../artwork/image_codec.hpp"
#include "../artwork/image_compact.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#include <objbase.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <wrl/client.h>
#else
#include <curl/curl.h>
#include <expat.h>
#endif
#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_set>
#include <sstream>
#include <chrono>

namespace glance {
namespace {
constexpr size_t max_feed = 1024 * 1024, max_image = 8 * 1024 * 1024;
std::wstring host(std::wstring_view url) {
    if (!url.starts_with(L"https://") || url.size() > 4096 || url.find_first_of(L"\r\n\t \\") != url.npos) return {};
#ifdef _WIN32
    const std::wstring copy(url);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = 1;
    if (!WinHttpCrackUrl(copy.c_str(), static_cast<DWORD>(copy.size()), 0, &parts) || parts.nPort != 443 ||
        parts.dwUserNameLength || parts.dwPasswordLength) return {};
    std::wstring result(parts.lpszHostName, parts.dwHostNameLength);
#else
    // libcurl's parser, with WinHttpCrackUrl's rules: port 443, no user name or password.
    struct Url { CURLU* handle = curl_url(); ~Url() { curl_url_cleanup(handle); } } parts;
    const auto part = [&](CURLUPart which, unsigned flags) -> std::optional<std::string> {
        char* value = nullptr;
        if (curl_url_get(parts.handle, which, &value, flags) != CURLUE_OK || !value) return std::nullopt;
        std::string text(value); curl_free(value); return text;
    };
    if (!parts.handle || curl_url_set(parts.handle, CURLUPART_URL, text_encoding::utf8(url).c_str(), 0) != CURLUE_OK ||
        part(CURLUPART_PORT, CURLU_DEFAULT_PORT) != "443" || part(CURLUPART_USER, 0) || part(CURLUPART_PASSWORD, 0)) return {};
    std::wstring result = text_encoding::wideLenient(part(CURLUPART_HOST, 0).value_or(""));
#endif
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return result;
}
std::string read(const std::filesystem::path& file, size_t limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || size > limit) return {};
    std::string bytes(static_cast<size_t>(size), '\0');
    std::ifstream input(file, std::ios::binary);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
std::wstring clean(std::wstring value, size_t limit = 240) {
    for (auto& c : value) if (c < 32) c = L' ';
    const auto start = value.find_first_not_of(L" \t\r\n");
    if (start == value.npos) return {};
    value = value.substr(start, value.find_last_not_of(L" \t\r\n") - start + 1);
    if (value.size() > limit) value.resize(limit);
    return value;
}
std::wstring field(const json::Value& value, const char* key) {
    const auto* found = value.get(key);
    return found ? text_encoding::wide(found->stringOr()) : L"";
}
bool fresh(const std::filesystem::path& file) {
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(file, error);
    if (error) return false;
    const auto age = std::filesystem::file_time_type::clock::now() - modified;
    return age >= decltype(age)::zero() && age < std::chrono::minutes(30);
}
bool steamNewsUrl(std::wstring_view url) {
    const auto h = host(url);
    return h == L"store.steampowered.com" || h == L"steamcommunity.com" ||
        (h == L"steamstore-a.akamaihd.net" && url.starts_with(L"https://steamstore-a.akamaihd.net/news/externalpost/"));
}
std::wstring newsDate(uint64_t timestamp) {
#ifdef _WIN32
    const uint64_t ticks = (timestamp + 11644473600ULL) * 10000000ULL;
    FILETIME time{static_cast<DWORD>(ticks),static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME date{};
    if (!FileTimeToSystemTime(&time,&date)) return {};
    wchar_t result[16]{};
    swprintf_s(result,L"%02u.%02u.%04u",date.wDay,date.wMonth,date.wYear);
    return result;
#else
    const std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm date{};
    if (timestamp > 253402300799ULL || gmtime_s(&date,&time)) return {};
    wchar_t result[16]{};
    swprintf_s(result,L"%02d.%02d.%04d",date.tm_mday,date.tm_mon+1,date.tm_year+1900);
    return result;
#endif
}
std::wstring attribute(std::wstring_view tag, std::wstring_view key) {
    size_t at = 0;
    while ((at = tag.find(key,at)) != tag.npos) {
        const size_t begin = at; at += key.size();
        if (begin && !iswspace(tag[begin-1])) continue;
        size_t eq = tag.find_first_not_of(L" \t\r\n",at);
        if (eq == tag.npos || tag[eq] != L'=') continue;
        size_t start = tag.find_first_not_of(L" \t\r\n",eq+1);
        if (start == tag.npos) return {};
        const auto quote = tag[start];
        if (quote != L'\'' && quote != L'"') continue;
        const auto end = tag.find(quote,start+1);
        if (end != tag.npos) return std::wstring(tag.substr(start+1,end-start-1));
    }
    return {};
}
std::wstring articleImage(std::wstring contents) {
    // Steam exports both [img]URL[/img] and [img src="..."][/img], plus HTML in older posts.
    for (size_t at = 0; at < contents.size();) {
        const auto bb = contents.find(L"[img",at), html = contents.find(L"<img",at);
        const auto begin = std::min(bb,html);
        if (begin == contents.npos) break;
        const bool bracket = begin == bb;
        const auto end = contents.find(bracket ? L']' : L'>',begin+4);
        if (end == contents.npos) break;
        std::wstring image;
        if (end-begin < 4096) image = attribute(std::wstring_view(contents).substr(begin+4,end-begin-4),L"src");
        if (bracket && image.empty()) {
            const auto close = contents.find(L"[/img]",end+1);
            if (close != contents.npos && close-end < 4096) image = clean(contents.substr(end+1,close-end-1),4096);
        }
        for (const auto* token : {L"{STEAM_CLAN_IMAGE}",L"{STEAM_CLAN_LOC_IMAGE}"})
            if (image.starts_with(token)) image.replace(0,wcslen(token),L"https://clan.akamai.steamstatic.com/images");
        for (size_t p = 0; (p = image.find(L"&amp;",p)) != image.npos;) image.replace(p,5,L"&");
        if (imageUrl(image)) return image;
        at = end+1;
    }
    return {};
}
bool validImage(std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > max_image) return false;
    return image_codec::decode(bytes,[](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        if (info.width>16384 || info.height>16384 || uint64_t(info.width)*info.height>16*1024*1024) return {};
        const double scale = std::min(1.0,320.0/std::max(info.width,info.height));
        return image_codec::Size{std::max<uint32_t>(1,static_cast<uint32_t>(info.width*scale)),std::max<uint32_t>(1,static_cast<uint32_t>(info.height*scale))};
    }).has_value();
}
std::string metadata(const std::filesystem::path& file, const std::wstring& url, const Get& get,
                     ImageSession* session, const std::function<bool(const json::Value&)>& valid, bool refresh = false, bool* stale = nullptr) {
    if (session) if (const auto found = session->metadata.find(url); found != session->metadata.end()) return found->second;
    auto saved = read(file,max_feed);
    const auto accepted = [&](std::string_view bytes) { const auto root = json::parse(bytes,max_feed); return root && valid(*root); };
    if (!accepted(saved)) saved.clear();
    if (refresh || saved.empty() || !fresh(file)) {
        HttpBudget request(2500);
        const auto response = get(url,L"",max_feed);
        const std::string bytes(response.body.begin(),response.body.end());
        if (response.status == 200 && accepted(bytes)) {
            saved = bytes;
            if (!httpCancelled()) atomic_file::write(file,saved);
        } else if (stale) *stale=true;
    }
    if (session) session->metadata[url] = saved; // Failed optional metadata is tried once per pass/app.
    return saved;
}
}
bool articleUrl(std::wstring_view url) {
    const auto h = host(url);
    return steamNewsUrl(url) || h == L"www.eurogamer.pl" || h == L"www.eurogamer.net";
}
bool imageUrl(std::wstring_view url) {
    const auto h = host(url);
    return h == L"assetsio.gnwcdn.com" || h.ends_with(L".steamstatic.com") ||
        h == L"steamcdn-a.akamaihd.net" || h == L"steamuserimages-a.akamaihd.net";
}
std::optional<std::vector<Item>> parseSteam(std::string_view bytes, Section section) {
    if (section != Section::steam && section != Section::upcoming) return std::nullopt;
    const auto root = json::parse(bytes, max_feed);
    const auto* group = root ? root->get(section == Section::steam ? "new_releases" : "coming_soon") : nullptr;
    const auto* items = group ? group->get("items") : nullptr;
    if (!items || items->type != json::Value::Type::array) return std::nullopt;
    std::vector<Item> result;
    std::unordered_set<int> ids;
    for (const auto& value : items->elements) {
        const auto* id_value = value.get("id");
        const int id = id_value ? id_value->intOr() : 0;
        auto title = clean(field(value, "name"));
        if (id <= 0 || title.empty() || !ids.insert(id).second) continue;
        auto image = field(value, "large_capsule_image");
        if (!imageUrl(image)) image = field(value, "header_image");
        if (!imageUrl(image)) image.clear();
        result.push_back({std::move(title), L"https://store.steampowered.com/app/" + std::to_wstring(id) + L"/", std::move(image), L"Steam", {}});
        auto& item = result.back();
        const auto* type = value.get("type");
        if (!type || type->intOr(-1) == 0) item.app_id = static_cast<uint32_t>(id);
        for (const auto* key : {"header_image","small_capsule_image"})
            if (auto alternate = field(value,key); imageUrl(alternate) && alternate != item.image) item.fallback_images.push_back(std::move(alternate));
        if (result.size() == item_limit) break;
    }
    return result;
}
namespace {
// A finished <item>: Eurogamer articles only, each link once.
void addRssItem(Item item, std::vector<Item>& result, std::unordered_set<std::wstring>& urls) {
    item.title = clean(std::move(item.title)); item.url = clean(std::move(item.url), 4096);
    const auto h = host(item.url);
    if (item.title.empty() || (h != L"www.eurogamer.pl" && h != L"www.eurogamer.net") || !urls.insert(item.url).second) return;
    item.source = h == L"www.eurogamer.pl" ? L"Eurogamer.pl" : L"Eurogamer";
    item.date = clean(std::move(item.date), 40);
    if (item.date.size() >= 16 && item.date[3] == L',') item.date = item.date.substr(5, 11);
    if (!imageUrl(item.image)) item.image.clear();
    if (const auto at = item.image.find(L"&auto=webp"); at != item.image.npos) item.image.erase(at, 10); // WIC: request JPEG.
    if (result.size() < item_limit) result.push_back(std::move(item));
}
}
#ifdef _WIN32
std::optional<std::vector<Item>> parseRss(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > max_feed) return std::nullopt;
    using Microsoft::WRL::ComPtr;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
    ComPtr<IXmlReader> reader;
    if (!stream || FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr)) ||
        FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(reader->SetProperty(XmlReaderProperty_MaxElementDepth, 32)) || FAILED(reader->SetInput(stream.Get()))) return std::nullopt;
    std::vector<Item> result;
    std::unordered_set<std::wstring> urls;
    bool rss = false, channel = false, in_item = false;
    UINT item_depth = 0;
    Item item;
    std::wstring field_name;
    XmlNodeType type{};
    HRESULT status;
    while ((status = reader->Read(&type)) == S_OK) {
        const wchar_t* name = nullptr; UINT depth = 0;
        reader->GetLocalName(&name, nullptr); reader->GetDepth(&depth);
        const std::wstring_view tag = name ? name : L"";
        if (type == XmlNodeType_Element) {
            if (depth == 0 && tag == L"rss") rss = true;
            if (rss && depth == 1 && tag == L"channel") channel = true;
            if (channel && depth == 2 && tag == L"item") { in_item = !reader->IsEmptyElement(); item_depth = depth; item = {}; }
            if (in_item && depth == item_depth + 1) {
                field_name = tag;
                const wchar_t* ns = nullptr; reader->GetNamespaceUri(&ns, nullptr);
                if (tag == L"content" && ns && std::wstring_view(ns) == L"http://search.yahoo.com/mrss/" &&
                    SUCCEEDED(reader->MoveToAttributeByName(L"url", nullptr))) {
                    const wchar_t* value = nullptr; reader->GetValue(&value, nullptr);
                    if (value) item.image = value;
                    reader->MoveToElement();
                }
            }
        } else if (in_item && (type == XmlNodeType_Text || type == XmlNodeType_CDATA) && depth == item_depth + 2) {
            const wchar_t* value = nullptr; UINT length = 0; reader->GetValue(&value, &length);
            if (!value) continue;
            auto* target = field_name == L"title" ? &item.title : field_name == L"link" ? &item.url : field_name == L"pubDate" ? &item.date : nullptr;
            if (target && target->size() + length <= 4096) target->append(value, length);
        } else if (type == XmlNodeType_EndElement && in_item && depth == item_depth + 1 && tag == L"item") {
            // XmlLite reports end elements at the depth inside the element being closed.
            in_item = false;
            addRssItem(std::move(item), result, urls);
        }
    }
    if (FAILED(status) || !rss || !channel || in_item) return std::nullopt;
    return result;
}
#else
// Expat in place of XmlLite, with its settings: no DTD at all, at most 32 levels of elements.
// Elements are matched by local name (namespace URI and name are split by '\x01'); depth
// counts the open elements, so an <item> sits at 2 inside <rss><channel>.
std::optional<std::vector<Item>> parseRss(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > max_feed) return std::nullopt;
    struct State {
        XML_Parser parser = XML_ParserCreateNS(nullptr, '\x01');
        std::vector<Item> result;
        std::unordered_set<std::wstring> urls;
        bool rss = false, channel = false, in_item = false;
        unsigned depth = 0, item_depth = 0;
        Item item;
        std::wstring field_name;
        ~State() { if (parser) XML_ParserFree(parser); }
        static std::string_view local(const XML_Char* name, std::string_view* ns = nullptr) {
            const std::string_view full(name);
            const auto split = full.find('\x01');
            if (ns) *ns = split == full.npos ? std::string_view{} : full.substr(0, split);
            return split == full.npos ? full : full.substr(split + 1);
        }
    } state;
    if (!state.parser) return std::nullopt;
    XML_SetUserData(state.parser, &state);
    XML_SetStartDoctypeDeclHandler(state.parser, [](void* data, const XML_Char*, const XML_Char*, const XML_Char*, int) {
        XML_StopParser(static_cast<State*>(data)->parser, XML_FALSE);
    });
    XML_SetElementHandler(state.parser, [](void* data, const XML_Char* name, const XML_Char** attributes) {
        auto& s = *static_cast<State*>(data);
        std::string_view ns;
        const auto tag = State::local(name, &ns);
        const unsigned depth = s.depth++;
        if (s.depth > 32) { XML_StopParser(s.parser, XML_FALSE); return; }
        if (depth == 0 && tag == "rss") s.rss = true;
        if (s.rss && depth == 1 && tag == "channel") s.channel = true;
        if (s.channel && depth == 2 && tag == "item") { s.in_item = true; s.item_depth = depth; s.item = {}; }
        if (s.in_item && depth == s.item_depth + 1) {
            s.field_name = text_encoding::wideLenient(tag);
            if (tag == "content" && ns == "http://search.yahoo.com/mrss/")
                for (auto* at = attributes; at[0] && at[1]; at += 2)
                    if (State::local(at[0]) == "url") s.item.image = text_encoding::wideLenient(at[1]);
        }
    }, [](void* data, const XML_Char* name) {
        auto& s = *static_cast<State*>(data);
        if (s.in_item && s.depth == s.item_depth + 1 && State::local(name) == "item") {
            s.in_item = false;
            addRssItem(std::move(s.item), s.result, s.urls);
        }
        --s.depth;
    });
    XML_SetCharacterDataHandler(state.parser, [](void* data, const XML_Char* text, int length) {
        auto& s = *static_cast<State*>(data);
        if (!s.in_item || s.depth != s.item_depth + 2 || length <= 0) return;
        auto* target = s.field_name == L"title" ? &s.item.title : s.field_name == L"link" ? &s.item.url : s.field_name == L"pubDate" ? &s.item.date : nullptr;
        if (!target) return;
        const auto value = text_encoding::wideLenient(std::string_view(text, static_cast<size_t>(length)));
        if (target->size() + value.size() <= 4096) target->append(value);
    });
    if (XML_Parse(state.parser, bytes.data(), static_cast<int>(bytes.size()), XML_TRUE) != XML_STATUS_OK ||
        !state.rss || !state.channel || state.in_item) return std::nullopt;
    return std::move(state.result);
}
#endif
Feed load(Section section, bool english, const std::filesystem::path& cache, bool refresh, const Get& get) {
    if (section == Section::owned) return {}; // Requires an explicit current-library snapshot.
    const bool steam = section == Section::steam || section == Section::upcoming;
    const auto file = cache / ((english ? L"en-" : L"pl-") + std::to_wstring(static_cast<int>(section)) + L".feed");
    const auto parse = [&](std::string_view bytes) { return steam ? parseSteam(bytes, section) : parseRss(bytes); };
    auto saved = parse(read(file, max_feed));
    if (saved && !refresh && fresh(file)) return {std::move(*saved)};
    const std::wstring url = steam ? L"https://store.steampowered.com/api/featuredcategories/?cc=pl&l=" + std::wstring(english ? L"english" : L"polish") :
        std::wstring(english ? L"https://www.eurogamer.net/feed/" : L"https://www.eurogamer.pl/feed/") + (section == Section::news ? L"news" : L"features");
    HttpBudget budget(12000);
    const auto response = get(url, L"", max_feed);
    if (response.status == 200) {
        const std::string text(response.body.begin(), response.body.end());
        if (auto items = parse(text)) {
            if (!httpCancelled()) atomic_file::write(file, text);
            return {std::move(*items)};
        }
    }
    return saved ? Feed{std::move(*saved), true, true} : Feed{{}, true, false};
}

ReleaseDate parseReleaseDate(std::wstring date) {
    ReleaseDate result; result.raw=clean(date,80); result.available=true;
    for (auto& c : date) { if (c==L',') c=L' '; c=static_cast<wchar_t>(towlower(c)); }
    std::wistringstream stream(date); std::vector<std::wstring> words; std::wstring word;
    while (stream>>word) words.push_back(word);
    const auto number=[](const std::wstring& text) {
        if (text.empty() || text.size()>4 || text.find_first_not_of(L"0123456789")!=text.npos) return 0;
        return std::stoi(text);
    };
    const auto month=[](const std::wstring& text) {
        static constexpr const wchar_t* names[]={L"january",L"february",L"march",L"april",L"may",L"june",L"july",L"august",L"september",L"october",L"november",L"december"};
        for (int i=0;i<12;++i) if (text==names[i] || text==std::wstring_view(names[i]).substr(0,3)) return i+1;
        return 0;
    };
    if (words.size()==3) {
        result.year=number(words[2]); result.month=month(words[1]); result.day=number(words[0]);
        if (!result.month) { result.month=month(words[0]); result.day=number(words[1]); }
        if (result.year>=1970 && result.year<=2200 && result.month>=1 && result.month<=12 && result.day>=1 && result.day<=31 &&
            std::chrono::year_month_day{std::chrono::year(result.year),std::chrono::month(result.month),std::chrono::day(result.day)}.ok())
            result.precision=ReleasePrecision::day;
    } else if (words.size()==2) {
        result.year=number(words[1]);
        if (result.year>=1970 && result.year<=2200) {
            if ((result.month=month(words[0]))!=0) result.precision=ReleasePrecision::month;
            else if (words[0].size()==2 && words[0][0]==L'q' && words[0][1]>=L'1' && words[0][1]<=L'4') {
                result.month=(words[0][1]-L'1')*3+1; result.precision=ReleasePrecision::quarter;
            }
        }
    } else if (words.size()==1) {
        result.year=number(words[0]);
        if (result.year>=1970 && result.year<=2200) result.precision=ReleasePrecision::year;
    }
    if (result.precision==ReleasePrecision::unknown) result.year=result.month=result.day=0;
    return result;
}

std::optional<std::vector<Item>> parseNews(std::string_view bytes, const OwnedGame& game) {
    const auto root = json::parse(bytes, max_feed);
    const auto* news = root ? root->get("appnews") : nullptr;
    const auto* app_id = news ? news->get("appid") : nullptr;
    const auto* items = news ? news->get("newsitems") : nullptr;
    if (!app_id || app_id->intOr() <= 0 || static_cast<uint32_t>(app_id->intOr()) != game.app_id ||
        !items || items->type != json::Value::Type::array) return std::nullopt;
    std::vector<Item> result;
    std::unordered_set<std::string> seen;
    for (const auto& value : items->elements) {
        const auto* id = value.get("gid"); const auto* date = value.get("date"); const auto* owner = value.get("appid");
        const std::string gid = id ? id->stringOr() : "";
        if (gid.empty() || gid.size() > 24 || gid.find_first_not_of("0123456789") != gid.npos ||
            !owner || owner->intOr() <= 0 || static_cast<uint32_t>(owner->intOr()) != game.app_id ||
            field(value,"feedname") != L"steam_community_announcements" || !date || date->type != json::Value::Type::number ||
            !std::isfinite(date->numeric) || date->numeric <= 0 || date->numeric > 4102444800.0) continue;
        Item item;
        item.title = clean(field(value,"title")); item.url = field(value,"url");
        if (item.title.empty() || !steamNewsUrl(item.url) || !seen.insert(gid).second) continue;
        item.id = gid; item.published = static_cast<uint64_t>(date->numeric); item.app_id = game.app_id;
        item.source = clean(game.name);
        if (item.source.empty()) item.source = L"Steam " + std::to_wstring(game.app_id);
        item.date = newsDate(item.published);
        item.image = articleImage(field(value,"contents"));
        item.fallback_images.push_back(L"https://cdn.akamai.steamstatic.com/steam/apps/" + std::to_wstring(game.app_id) + L"/header.jpg");
        result.push_back(std::move(item));
        if (result.size() == 3) break;
    }
    return result;
}

std::wstring steamEventImage(std::string_view bytes, const Item& item, bool english) {
    const auto root = json::parse(bytes,max_feed);
    const auto* events = root ? root->get("events") : nullptr;
    if (!events || events->type != json::Value::Type::array || !item.app_id || item.id.empty()) return {};
    std::wstring image;
    size_t matches = 0;
    for (const auto& event : events->elements) {
        const auto* app = event.get("appid"); const auto* body = event.get("announcement_body");
        const auto* date = body ? body->get("posttime") : nullptr;
        if (!app || app->intOr() <= 0 || static_cast<uint32_t>(app->intOr()) != item.app_id || !body || !date ||
            date->type != json::Value::Type::number || date->numeric != static_cast<double>(item.published) || clean(field(*body,"headline")) != item.title) continue;
        if (++matches > 1) return {}; // API news gids and event gids differ: only an unambiguous exact title/date match.
        const auto data = json::parse(text_encoding::utf8(field(event,"jsondata")),max_feed);
        auto clan = field(*body,"clanid");
        if (clan.empty()) if (const auto* id = body->get("clanid"); id && id->intOr()>0) clan = std::to_wstring(id->intOr());
        if (!data || clan.empty() || clan.size()>10 || clan.find_first_not_of(L"0123456789") != clan.npos) continue;
        for (const auto* key : {"localized_capsule_image","localized_title_image"}) {
            const auto* values = data->get(key);
            if (!values || values->type != json::Value::Type::array) continue;
            for (size_t language : {english ? size_t{0} : size_t{12},size_t{0}}) {
                if (language >= values->elements.size()) continue;
                const auto file = text_encoding::wide(values->elements[language].stringOr());
                if (file.size()<44 || file.size()>46 || file.substr(0,40).find_first_not_of(L"0123456789abcdefABCDEF") != file.npos ||
                    (file.substr(40)!=L".jpg" && file.substr(40)!=L".png" && file.substr(40)!=L".jpeg" && file.substr(40)!=L".webp")) continue;
                image = L"https://clan.akamai.steamstatic.com/images/"+clan+L"/"+file;
                break;
            }
            if (!image.empty()) break;
        }
    }
    return image;
}

std::vector<OwnedGame> selectOwned(std::vector<OwnedGame> games) {
    std::erase_if(games,[](const auto& game) { return !game.app_id || game.app_id > INT32_MAX; });
    std::sort(games.begin(),games.end(),[](const auto& a,const auto& b) {
        if (a.last_played != b.last_played) return a.last_played > b.last_played;
        if (a.installed != b.installed) return a.installed;
        return a.app_id < b.app_id;
    });
    std::unordered_set<uint32_t> ids;
    std::erase_if(games,[&](const auto& game) { return !ids.insert(game.app_id).second; });
    return games;
}

Feed loadOwned(std::vector<OwnedGame> games, bool english, const std::filesystem::path& cache, bool refresh, const Get& get) {
    games = selectOwned(std::move(games));
    Feed result; result.games_total = games.size();
    if (games.size() > owned_limit) games.resize(owned_limit);
    result.games_selected = games.size();
    std::unordered_set<std::string> seen;
    HttpBudget budget(20000);
    for (const auto& game : games) {
        if (httpCancelled()) break;
        const auto file = cache / L"steam-news" / ((english ? L"en-" : L"pl-") + std::to_wstring(game.app_id) + L".v2.json");
        auto saved = parseNews(read(file,max_feed),game);
        if (!saved) saved = parseNews(read(cache/L"steam-news"/((english ? L"en-" : L"pl-")+std::to_wstring(game.app_id)+L".json"),max_feed),game);
        auto items = saved;
        if (refresh || !saved || !fresh(file)) {
            HttpBudget request_budget(2500);
            const auto response = get(L"https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=" + std::to_wstring(game.app_id) +
                L"&count=3&maxlength=0&feeds=steam_community_announcements&l=" + (english ? L"english" : L"polish"),L"",max_feed);
            const std::string text(response.body.begin(),response.body.end());
            items = response.status == 200 ? parseNews(text,game) : std::nullopt;
            if (items) { if (!httpCancelled()) atomic_file::write(file,text); }
            else { items = std::move(saved); result.stale = true; }
        }
        if (items) {
            ++result.games_checked;
            for (auto& item : *items) if (seen.insert(item.id).second) result.items.push_back(std::move(item));
        }
    }
    std::sort(result.items.begin(),result.items.end(),[](const auto& a,const auto& b) {
        return a.published != b.published ? a.published > b.published : a.id < b.id;
    });
    if (result.items.size() > item_limit) result.items.resize(item_limit);
    result.unavailable = !result.games_checked && result.games_selected > 0;
    return result;
}
std::vector<uint8_t> thumbnail(const Item& item, const std::filesystem::path& slot, const Get& get, bool english, ImageSession* session) {
    if (httpCancelled()) return {};
    HttpBudget budget(7500); // Per card: one slow download must not starve all later cards.
    std::vector<std::wstring> candidates;
    const auto add = [&](std::wstring url) {
        if (imageUrl(url) && std::find(candidates.begin(),candidates.end(),url)==candidates.end()) candidates.push_back(std::move(url));
    };
    if (item.app_id && !item.id.empty()) {
        const auto events = metadata(slot.parent_path()/L"steam-images"/(std::to_wstring(item.app_id)+L".events.json"),
            L"https://store.steampowered.com/events/ajaxgetpartnereventspageable/?appid="+std::to_wstring(item.app_id)+L"&offset=0&count=10&l=english",get,session,
            [](const auto& root) { const auto* events = root.get("events"); return events && events->type == json::Value::Type::array; });
        add(steamEventImage(events,item,english));
    }
    add(item.image);
    for (const auto& url : item.fallback_images) add(url);
    if (item.app_id) add(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/"+std::to_wstring(item.app_id)+L"/header.jpg");
    const auto key_file = slot.wstring() + L".url";
    const auto saved_key = read(key_file,4096);
    const auto tryImage = [&](const std::wstring& url) -> std::vector<uint8_t> {
        if (httpCancelled()) return {};
        const auto identity = text_encoding::utf8(url);
        if (saved_key == identity) {
            const auto saved = read(slot,max_image);
            if (validImage(std::span(reinterpret_cast<const uint8_t*>(saved.data()),saved.size()))) return {saved.begin(),saved.end()};
        }
        HttpBudget request(3000);
        const auto response = get(url,L"",max_image);
        if (response.status!=200 || httpCancelled() || !validImage(response.body)) return {};
        auto compact = compactImage(response.body,ImageRole::landscape);
        auto bytes = compact.empty() ? response.body : std::move(compact);
        std::error_code error; std::filesystem::remove(key_file,error);
        if (!error && atomic_file::write(slot,bytes)) atomic_file::write(key_file,identity);
        return bytes;
    };
    for (const auto& url : candidates) if (auto bytes = tryImage(url); !bytes.empty()) return bytes;
    // The Steam store now also uses content-addressed artwork; ask for the real header URL
    // instead of assuming the legacy /apps/<id>/header.jpg exists for every game.
    if (item.app_id && !httpCancelled()) {
        const auto id = std::to_string(item.app_id);
        const auto details = metadata(slot.parent_path()/L"steam-images"/(std::to_wstring(item.app_id)+L".store.json"),
            L"https://store.steampowered.com/api/appdetails?appids="+std::to_wstring(item.app_id)+L"&filters=basic",get,session,[&](const auto& root) {
                const auto* app = root.get(id); const auto* data = app ? app->get("data") : nullptr;
                return data && imageUrl(field(*data,"header_image"));
            });
        const auto root = json::parse(details,max_feed); const auto* app = root ? root->get(id) : nullptr;
        const auto* data = app ? app->get("data") : nullptr;
        if (data) if (auto bytes = tryImage(field(*data,"header_image")); !bytes.empty()) return bytes;
    }
    return {};
}
}
