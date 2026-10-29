#include "../src/net/http.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/image_validation.hpp"
#include "../src/artwork/text_crop.hpp"
#include "../src/artwork/image_compact.hpp"
#include "../src/net/json.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/platform/text_encoding.hpp"
#include <Windows.h>
#ifdef _WIN32
#include <objbase.h> // CoInitializeEx (Windows.h leaves COM out under WIN32_LEAN_AND_MEAN).
#endif
#include <fstream>
#include <iostream>
#include <regex>
#include <set>

namespace {
std::vector<uint8_t> readImageBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() <= 0 || in.tellg() > 20 * 1024 * 1024) return {};
    std::vector<uint8_t> result(static_cast<size_t>(in.tellg())); in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()))) return {};
    return result;
}
// Restore only when the original's hash matches the crop AND its exact compacted bytes match
// the current image. A replaced/different artwork never inherits the old crop by path alone.
bool repairCrop(const std::filesystem::path& image, const std::filesystem::path& manual) {
    std::ifstream metadata(manual); metadata.imbue(std::locale::classic());
    int version = 0; uint64_t hash = 0; CropFocus focus;
    if (!(metadata >> version >> hash >> focus.x >> focus.y) || version != 1 || !hash) return false;
    metadata.close();
    const auto current = readImageBytes(image);
    if (current.empty()) return false;
    std::vector<std::wstring> routes;
    if (image.parent_path().filename() == L"cover") routes.push_back(L"grids/" + image.stem().wstring());
    else {
        uint32_t id = 0;
        const bool steam = image.parent_path().filename() == L"covers";
        if (steam) id = static_cast<uint32_t>(std::wcstoul(image.filename().c_str(), nullptr, 10));
        else std::ifstream(image.parent_path() / L"0.sgdbid") >> id;
        if (id) for (const auto* size : {L"512x512", L"600x900"})
            routes.push_back(std::wstring(L"grids/") + (steam ? L"steam/" : L"game/") + std::to_wstring(id) + L"?dimensions=" + size + L"&limit=1");
    }
    const auto key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
    if (key.empty()) return false;
    for (const auto& route : routes) {
        const auto response = httpGet(L"https://www.steamgriddb.com/api/v2/" + route, L"Authorization: Bearer " + key + L"\r\n", 512 * 1024);
        if (response.status != 200) continue;
        const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
        const auto* data = parsed ? parsed->get("data") : nullptr;
        if (data && data->type == json::Value::Type::array) data = data->elements.empty() ? nullptr : &data->elements.front();
        const auto* url = data ? data->get("url") : nullptr;
        if (!url || !url->text.starts_with("https://")) continue;
        const auto original = httpGet(text_encoding::wide(url->text), L"", 20 * 1024 * 1024);
        if (original.status != 200) continue;
        uint64_t actual = 14695981039346656037ull;
        for (const auto byte : original.body) { actual ^= byte; actual *= 1099511628211ull; }
        if (actual != hash || compactImage(original.body, ImageRole::cover) != current) continue;
        auto backup = manual; backup += L".before-repair";
        const auto previous = readImageBytes(manual);
        if (previous.empty() || !atomic_file::write(backup, previous, false)) return false;
        return saveManualTextCrop(image, focus);
    }
    return false;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::cout << "image_probe --links URL | --image URL [output.png] | --crops cache-directory | --repair-crops cache-directory\n"; return 1; }
    if (std::wstring_view(argv[1]) == L"--crops" || std::wstring_view(argv[1]) == L"--repair-crops") {
        const bool repair = std::wstring_view(argv[1]) == L"--repair-crops";
        size_t valid = 0, stale = 0, repaired = 0;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(argv[2], ec); !ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {
            if (it->path().extension() != L".manual-focus") continue;
            auto image = it->path(); image.replace_extension(L".img");
            const auto focus = manualTextCrop(image);
            if (focus) ++valid;
            else if (repair && repairCrop(image, it->path())) { ++repaired; std::wcout << L"Restored verified crop: " << std::filesystem::relative(image, argv[2], ec).wstring() << L'\n'; }
            else { ++stale; std::wcout << L"Stale manual crop: " << std::filesystem::relative(image, argv[2], ec).wstring() << L'\n'; }
        }
        std::cout << "Manual crops: valid=" << valid << " repaired=" << repaired << " stale=" << stale << " error=" << ec.value() << '\n';
        return ec ? 1 : 0;
    }
    const auto response = httpGet(argv[2], L"", 12 * 1024 * 1024);
    if (response.status != 200) { std::cout << "HTTP=" << response.status << '\n'; return 2; }
    if (std::wstring_view(argv[1]) == L"--links") {
        std::string page(response.body.begin(), response.body.end());
        for (const auto& pair : {std::pair{"\\/", "/"}, {"&amp;", "&"}})
            for (size_t at = 0; (at = page.find(pair.first, at)) != page.npos;) page.replace(at, strlen(pair.first), pair.second);
        std::cout << "Page bytes=" << page.size() << '\n';
        const std::regex links("(?:https:)?//[^\\\"'\\s<>]+");
        std::set<std::string> urls;
        for (auto it = std::sregex_iterator(page.begin(), page.end(), links); it != std::sregex_iterator(); ++it) {
            auto url = it->str();
            if (url.find(".png") == url.npos && url.find("/is/image/") == url.npos && url.find("store-images.s-microsoft.com/image/") == url.npos) continue;
            if (url.starts_with("//")) url = "https:" + url;
            urls.insert(url);
        }
        for (const auto& url : urls) std::cout << url << '\n';
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 3;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    const auto info = image_validation::inspect(response.body);
    if (!info) return 4;
    std::cout << info->width << 'x' << info->height << "; PNG=" << info->png << "; transparent=" << 100.0 * info->transparent / info->pixels()
        << "%; opaque=" << 100.0 * info->opaque / info->pixels() << "%\n";
    if (!info->png || !info->hasTransparency()) return 6;
    if (argc > 3 && !atomic_file::write(argv[3], response.body)) return 7;
    return 0;
}
