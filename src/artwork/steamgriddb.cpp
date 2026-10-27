#include "steamgriddb.hpp"
#include "../i18n/i18n.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
#include <cmath>
#include <unordered_set>

namespace {
using text_encoding::wide;
uint32_t number(const json::Value* value) {
    return value && value->type == json::Value::Type::number && std::isfinite(value->numeric) &&
           value->numeric > 0 && value->numeric <= UINT32_MAX && std::floor(value->numeric) == value->numeric ?
           static_cast<uint32_t>(value->numeric) : 0;
}
std::wstring url(const json::Value* value) {
    const auto text = value ? value->stringOr() : std::string{};
    if (!text.starts_with("https://") || text.size() > 2048 || text.find_first_of("\r\n\t ") != std::string::npos) return {};
    return wide(text);
}
std::wstring error(uint32_t status) {
    if (status == 401 || status == 403) return tr(L"SteamGridDB odrzucił klucz API.");
    if (status == 429) return tr(L"Limit zapytań SteamGridDB. Spróbuj ponownie za chwilę.");
    return tr(L"Nie można pobrać grafik ze SteamGridDB (HTTP ") + std::to_wstring(status) + L").";
}
std::string_view body(const HttpResponse& response) {
    return {reinterpret_cast<const char*>(response.body.data()), response.body.size()};
}
}

GridPage parseGridPage(std::string_view bytes) {
    GridPage result;
    const auto root = json::parse(bytes);
    const auto* data = root ? root->get("data") : nullptr;
    if (!data || data->type != json::Value::Type::array) {
        result.error = tr(L"SteamGridDB zwrócił nieprawidłową odpowiedź.");
        return result;
    }
    result.more = data->elements.size() >= grid_page_size;
    std::unordered_set<uint32_t> seen;
    for (const auto& item : data->elements) {
        GridAsset asset;
        asset.id = number(item.get("id"));
        asset.url = url(item.get("url"));
        asset.thumbnail = url(item.get("thumb"));
        const auto width = number(item.get("width")), height = number(item.get("height"));
        if (!asset.id || asset.url.empty() || width > 8192 || height > 8192 || !seen.insert(asset.id).second) continue;
        asset.width = static_cast<int>(width); asset.height = static_cast<int>(height);
        const auto* author = item.get("author");
        const auto* name = author ? author->get("name") : nullptr;
        asset.author = name ? wide(name->stringOr()) : std::wstring{};
        if (asset.author.size() > 80) asset.author.resize(80);
        result.assets.push_back(std::move(asset));
        if (result.assets.size() == grid_page_size) break;
    }
    return result;
}

GridPage browseSteamGridDB(const Game& game, ArtworkKind kind, int page, GridSource source,
                           const std::wstring& key, const ArtworkHttp& get) {
    if (kind == ArtworkKind::disc) return {{}, {}, false, tr(L"Grafiki płyt pochodzą ze ScreenScraper.")};
    if (!isGame(game) || gameKey(game).empty() || page < 0) return {{}, {}, false, tr(L"Brak gry.")};
    if (key.empty()) return {{}, {}, false, tr(L"Brak klucza SteamGridDB.")};
    if (key.size() > 1024 || key.find_first_of(L"\r\n") != std::wstring::npos) return {{}, {}, false, tr(L"Nieprawidłowy klucz SteamGridDB.")};
    const auto headers = L"Accept: application/json\r\nAuthorization: Bearer " + key + L"\r\n";
    // Covers, posters and wide tiles are all SGDB "grids", told apart by their dimensions.
    const auto endpoint = kind == ArtworkKind::hero ? L"heroes" : kind == ArtworkKind::logo ? L"logos" : L"grids";
    const auto dimensions = kind == ArtworkKind::cover ? L"&dimensions=512x512,600x900" :
                            kind == ArtworkKind::poster ? L"&dimensions=600x900,342x482,660x930" :
                            kind == ArtworkKind::landscape ? L"&dimensions=920x430,460x215" : L"";
    const auto fetch = [&](GridSource selected) {
        const auto address = L"https://www.steamgriddb.com/api/v2/" + std::wstring(endpoint) + L'/' +
            (selected.steam ? L"steam/" : L"game/") + std::to_wstring(selected.id) +
            L"?types=static&mimes=" + (kind == ArtworkKind::logo ? std::wstring(L"image/png") : std::wstring(L"image/png,image/jpeg")) +
            L"&limit=" + std::to_wstring(grid_page_size) +
            L"&page=" + std::to_wstring(page) + dimensions;
        const auto response = get(address, headers, 2 * 1024 * 1024);
        GridPage result;
        if (response.status == 200) result = parseGridPage(body(response));
        else if (response.status != 404) result.error = error(response.status);
        result.source = selected;
        return result;
    };
    if (source.id) return fetch(source);
    if (steamArtwork(game)) {
        auto result = fetch({game.app_id, true});
        if (!result.assets.empty() || !result.error.empty() || result.more) return result;
    }
    const auto matched = resolveGridGame(artworkTitle(game), key, get);
    return matched.source.id ? fetch(matched.source) : GridPage{{}, {}, false, matched.error};
}

bool decodableArtwork(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 128 || bytes.size() > 20 * 1024 * 1024 ||
        !((bytes[0] == 0x89 && bytes[1] == 'P') || (bytes[0] == 0xff && bytes[1] == 0xd8))) return false;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return false;
    struct Apartment { HRESULT result; ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); } } apartment{com};
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    UINT w = 0, h = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(frame->GetSize(&w, &h)) ||
        !w || !h || w > 8192 || h > 8192 || static_cast<uint64_t>(w) * h > 32 * 1024 * 1024) return false;
    uint8_t pixels[32 * 32 * 4]{};
    return SUCCEEDED(factory->CreateBitmapScaler(scaler.GetAddressOf())) &&
        SUCCEEDED(scaler->Initialize(frame.Get(), 32, 32, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory->CreateFormatConverter(converter.GetAddressOf())) &&
        SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->CopyPixels(nullptr, 32 * 4, sizeof(pixels), pixels));
}

std::vector<uint8_t> downloadGridImage(const GridAsset& asset, bool thumbnail, const ArtworkHttp& get) {
    for (const auto& address : {thumbnail ? asset.thumbnail : asset.url, thumbnail ? asset.url : std::wstring{}}) {
        if (address.empty() || !address.starts_with(L"https://")) continue;
        auto response = get(address, L"Accept: image/png, image/jpeg\r\n", 20 * 1024 * 1024);
        if (response.status == 200 && decodableArtwork(response.body)) return std::move(response.body);
    }
    return {};
}
