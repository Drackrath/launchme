// Opt-in live smoke: API media capabilities and actual disc scans. Credentials stay in memory;
// only decoded image samples and public field/type names are written to the selected directory.
#include "../src/library/rawg.hpp"
#include "../src/library/screenscraper.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/net/json.hpp"
#include "../src/net/url_encoding.hpp"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>
#include <map>
#include <set>

namespace {
std::string_view body(const HttpResponse& response) { return {reinterpret_cast<const char*>(response.body.data()), response.body.size()}; }
bool stopped(uint32_t status) { return status == 401 || status == 403 || status == 423 || status == 426 || status == 429 || status == 430 || status == 431; }
std::wstring ssUrl(const screenscraper::Config& config, const wchar_t* endpoint) {
    return config.base_url + endpoint + L"?devid=" + url_encoding::component(config.dev_id) +
        L"&devpassword=" + url_encoding::component(config.dev_password) + L"&softname=" + url_encoding::component(config.softname) +
        L"&output=json&ssid=" + url_encoding::component(config.user) + L"&sspassword=" + url_encoding::component(config.password);
}
void catalog(const json::Value& value) {
    if (const auto* name = value.get("nomcourt")) {
        const auto type = name->stringOr();
        if (type.starts_with("support") || type.find("disc") != type.npos)
            std::cout << "SS media catalog: " << type << " | " << (value.get("nom") ? value.get("nom")->stringOr() : "") << '\n';
    }
    for (const auto& item : value.elements) catalog(item);
    for (const auto& [key, item] : value.properties) catalog(item);
}
bool saveSample(const std::vector<uint8_t>& bytes, const std::filesystem::path& file) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> wic; ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
    UINT width = 0, height = 0;
    if (bytes.empty() || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) ||
        FAILED(wic->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(frame->GetSize(&width, &height)) || !width || !height || width > 8192 || height > 8192) return false;
    std::cout << "Decoded image " << width << 'x' << height << "; bytes=" << bytes.size();
    if (SUCCEEDED(wic->CreateFormatConverter(converter.GetAddressOf())) &&
        SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        BYTE pixel[4]{}; WICRect corner{0, 0, 1, 1}, centre{static_cast<INT>(width / 2), static_cast<INT>(height / 2), 1, 1};
        if (SUCCEEDED(converter->CopyPixels(&corner, 4, 4, pixel))) std::cout << "; corner alpha=" << static_cast<int>(pixel[3]);
        if (SUCCEEDED(converter->CopyPixels(&centre, 4, 4, pixel))) std::cout << "; hub alpha=" << static_cast<int>(pixel[3]);
    }
    const bool saved = atomic_file::write(file, bytes);
    std::cout << "; saved=" << saved << "; file=" << file.filename().string() << '\n';
    return saved;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && std::wstring_view(argv[2]) != L"--rawg" && std::wstring_view(argv[2]) != L"--ss-systems")) {
        std::cerr << "Usage: disc_art_probe <existing output directory> [--rawg|--ss-systems]\n"; return 2;
    }
    const std::filesystem::path output = argv[1];
    if (!std::filesystem::is_directory(output)) return 2;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Com { ~Com() { CoUninitialize(); } } com;
    if (argc == 3 && std::wstring_view(argv[2]) == L"--ss-systems") {
        const auto config = screenscraper::load();
        if (!config.ready() || screenscraper::paused(userConfigDirectory() / L"metadata")) return 3;
        HttpBudget budget(25000);
        const auto response = screenscraper::request(ssUrl(config, L"systemesListe.php"), L"Accept: application/json\r\n", 8 * 1024 * 1024, httpGetPrivate, true);
        std::cout << "SS systems HTTP " << response.status << '\n';
        const auto visit = [&](const auto& self, const json::Value& value) -> void {
            if (const auto* names = value.get("noms")) for (const auto& [key, name] : names->properties)
                if (name.stringOr().find("Windows") != std::string::npos) {
                    const auto* id = value.get("id");
                    std::cout << "SS platform " << (id ? id->stringOr(std::to_string(id->intOr())) : "?") << ": " << name.stringOr() << '\n'; break;
                }
            for (const auto& item : value.elements) self(self, item);
            for (const auto& [key, item] : value.properties) self(self, item);
        };
        if (const auto root = json::parse(body(response), 8 * 1024 * 1024)) visit(visit, *root);
        return response.status == 200 ? 0 : 4;
    }
    // Schema plus two explicit games: do not infer global API support from one absent field.
    {
        HttpBudget budget(15000);
        const auto response = httpGet(L"https://api.rawg.io/docs/?format=openapi", L"Accept: */*\r\n", 2 * 1024 * 1024);
        std::cout << "RAWG schema HTTP " << response.status << '\n';
        if (const auto root = json::parse(body(response))) {
            if (const auto* paths = root->get("paths")) {
                std::set<std::string> names;
                for (const auto& [name, value] : paths->properties) if (name.starts_with("/games/")) names.insert(name);
                for (const auto& name : names) std::cout << "RAWG documented path: " << name << '\n';
            }
        }
    }
    const auto key = userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt");
    std::cout << "RAWG credentials configured=" << !key.empty() << '\n';
    if (!key.empty()) for (const auto* slug : {L"castlevania-symphony-of-the-night", L"final-fantasy-vii"}) {
        HttpBudget budget(15000);
        auto response = httpGetPrivate(L"https://api.rawg.io/api/games/" + std::wstring(slug) + L"?key=" + url_encoding::component(key), L"Accept: application/json\r\n", 1024 * 1024);
        std::cout << "RAWG " << text_encoding::utf8(slug) << " HTTP " << response.status << '\n';
        if (stopped(response.status)) break;
        if (const auto alias = json::parse(body(response)); alias && alias->get("redirect") && alias->get("slug")) {
            const auto canonical = alias->get("slug")->stringOr();
            if (!canonical.empty() && canonical.size() <= 160 && canonical.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == canonical.npos) {
                response = httpGetPrivate(L"https://api.rawg.io/api/games/" + text_encoding::wide(canonical) + L"?key=" + url_encoding::component(key), L"Accept: application/json\r\n", 1024 * 1024);
                std::cout << "RAWG canonical " << canonical << " HTTP " << response.status << '\n';
            }
        }
        if (stopped(response.status)) break;
        if (const auto root = json::parse(body(response)); root && response.status == 200) {
            for (const auto* field : {"background_image", "background_image_additional", "screenshots_count", "movies_count", "disc", "disc_image", "discart", "support-2D"}) {
                const auto* value = root->get(field);
                std::cout << "  " << field << ": " << (value && value->type != json::Value::Type::null_value ? "present" : "absent") << '\n';
            }
            std::set<std::string> fields;
            for (const auto& [name, value] : root->properties) fields.insert(name);
            std::cout << "  all field names:"; for (const auto& field : fields) std::cout << ' ' << field; std::cout << '\n';
        }
    }
    if (argc == 3) return 0;
    auto config = screenscraper::load(); config.output = L"json";
    std::cout << "ScreenScraper credentials configured=" << config.ready() << '\n';
    if (!config.ready()) return 0;
    if (screenscraper::paused(userConfigDirectory() / L"metadata")) { std::cout << "SS paused by existing cooldown\n"; return 0; }
    {
        HttpBudget budget(20000);
        const auto response = screenscraper::request(ssUrl(config, L"mediasJeuListe.php"), L"Accept: application/json\r\n", 2 * 1024 * 1024, httpGetPrivate, true);
        std::cout << "SS media catalog HTTP " << response.status << '\n';
        if (stopped(response.status)) return 0;
        if (const auto root = json::parse(body(response)); root && response.status == 200) catalog(*root);
    }
    for (const auto* title : {L"Symphony of the Night", L"Final Fantasy VII"}) {
        HttpBudget budget(60000);
        uint32_t http_status = 0;
        const auto get = [&](const std::wstring& url, const std::wstring& headers, size_t max) {
            auto response = httpGetPrivate(url, headers, max); http_status = response.status; return response;
        };
        auto found = screenscraper::search(config, title, 57, L"en", get); // PlayStation, not similarly named modern editions.
        std::cout << "SS search " << text_encoding::utf8(title) << " HTTP " << http_status << "; games=" << found.games.size() << '\n';
        if (stopped(http_status) || found.quota.exhausted()) break;
        if (found.games.empty()) continue;
        const auto id = found.games.front().id;
        const auto response = screenscraper::request(ssUrl(config, L"jeuInfos.php") + L"&systemeid=57&gameid=" + std::to_wstring(id), L"Accept: application/json\r\n", 8 * 1024 * 1024, get, true);
        auto detail = screenscraper::parse(response.status, body(response), false, L"en"); screenscraper::observeQuota(detail.quota);
        std::cout << "SS game " << id << " HTTP " << response.status << '\n';
        if (stopped(response.status) || detail.quota.exhausted()) break;
        if (!detail.game) continue;
        std::cout << "SS title: " << text_encoding::utf8(detail.game->name) << '\n';
        std::map<std::wstring, size_t> types;
        for (const auto& media : detail.game->media) ++types[media.type];
        for (const auto& [type, count] : types) std::cout << "  media " << text_encoding::utf8(type) << ": " << count << '\n';
        size_t downloaded = 0;
        for (const auto& media : detail.game->media) if (media.type == L"support-2D" && downloaded < 2) {
            uint32_t status = 0;
            auto bytes = screenscraper::downloadMedia(config, media, get, &status);
            std::cout << "SS support-2D region=" << text_encoding::utf8(media.region) << " format=" << text_encoding::utf8(media.format) << " HTTP " << status << '\n';
            if (stopped(status)) break;
            const auto file = output / (L"disc-smoke-ss-" + std::to_wstring(id) + L"-" + std::to_wstring(downloaded) + (media.format == L"jpg" ? L".jpg" : L".png"));
            if (!bytes.empty()) saveSample(bytes, file);
            ++downloaded;
        }
    }
    return 0;
}
