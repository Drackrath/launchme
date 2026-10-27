#include "aurelia.hpp"
#include "../net/http_download.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/updater.hpp"
#include "../platform/text_encoding.hpp"
#include <fstream>
#include <set>

namespace aurelia {
namespace {
bool version(std::wstring_view value) {
    if (value.empty() || value.size() > 64 || value.front() == L'.') return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || c == L'.' || c == L'v' || c == L'-' || (c >= L'a' && c <= L'z'); }) && value.find(L"..") == value.npos;
}
std::string readSmall(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() <= 0 || in.tellg() > 4096) return {};
    std::string out(static_cast<size_t>(in.tellg()), '\0'); in.seekg(0);
    return in.read(out.data(), out.size()) ? out : std::string{};
}
bool pe64(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary); IMAGE_DOS_HEADER dos{}; DWORD sig = 0; IMAGE_FILE_HEADER header{};
    if (!in.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64 || dos.e_lfanew > 1024 * 1024) return false;
    in.seekg(dos.e_lfanew);
    return in.read(reinterpret_cast<char*>(&sig), sizeof(sig)) && sig == IMAGE_NT_SIGNATURE &&
           in.read(reinterpret_cast<char*>(&header), sizeof(header)) && header.Machine == IMAGE_FILE_MACHINE_AMD64 && !(header.Characteristics & IMAGE_FILE_DLL);
}
}
InstallationInfo installed(const std::filesystem::path& root) {
    const auto v = text_encoding::wideLenient(readSmall(root / L"current.txt"));
    if (!version(v)) return {};
    const auto exe = root / v / asset_name;
    std::error_code ec; if (!std::filesystem::is_regular_file(exe, ec)) return {};
    return {exe, v};
}
bool allowedDownload(std::wstring_view url) {
    if (!url.starts_with(L"https://")) return false;
    const auto host_end = url.find(L'/', 8); if (host_end == url.npos) return false;
    const auto host = url.substr(8, host_end - 8);
    return (host == L"github.com" && url.substr(host_end).starts_with(L"/Drackrath/Aurelia/releases/download/")) ||
           host == L"release-assets.githubusercontent.com" || host == L"objects.githubusercontent.com";
}
std::optional<Release> parseRelease(std::string_view body) {
    const auto data = json::parse(body); if (!data || flag(*data, "draft") || flag(*data, "prerelease")) return {};
    Release r; r.version = text_encoding::wideLenient(text(*data, "tag_name")); if (!version(r.version)) return {};
    const auto* assets = data->get("assets"); if (!assets || assets->type != json::Value::Type::array) return {};
    bool found = false;
    for (const auto& a : assets->elements) if (text(a, "name") == "aurelia_windows_x86_64.exe") {
        if (found) return {}; found = true;
        r.url = text_encoding::wideLenient(text(a, "browser_download_url"));
        const auto expected = L"https://github.com/Drackrath/Aurelia/releases/download/" + r.version + L"/" + asset_name;
        if (r.url != expected) return {};
        r.size = integer(a.get("size")).value_or(0); if (r.size < 1024 || r.size > 256 * 1024 * 1024) return {};
        const auto digest = text(a, "digest"); if (!digest.starts_with("sha256:") || digest.size() != 71) return {};
        r.sha256 = digest.substr(7);
        if (!std::all_of(r.sha256.begin(), r.sha256.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) return {};
    }
    return found ? std::optional(r) : std::nullopt;
}
bool publish(const std::filesystem::path& root, const Release& release, const std::filesystem::path& downloaded) {
    std::error_code ec;
    if (!version(release.version) || std::filesystem::file_size(downloaded, ec) != release.size || ec ||
        updater::sha256File(downloaded) != release.sha256 || !pe64(downloaded)) return false;
    const auto directory = root / release.version; std::filesystem::create_directories(directory, ec); if (ec) return false;
    const auto destination = directory / asset_name;
    if (std::filesystem::exists(destination, ec)) {
        if (updater::sha256File(destination) != release.sha256) return false;
    } else if (!MoveFileExW(downloaded.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return false;
    return atomic_file::writeDurable(root / L"current.txt", text_encoding::utf8(release.version));
}
Reply installRelease(const std::filesystem::path& root, const Release& release, const Cancel& cancel,
                     const std::function<void(uint64_t, uint64_t)>& progress) {
    Reply r; std::error_code ec; std::filesystem::create_directories(root, ec);
    if (ec) { r.error = tr(L"Nie można zapisać Aurelii"); return r; }
    const auto partial = root / L"download.part";
    const HANDLE lock = CreateFileW((root / L"install.lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) { r.error = tr(L"Instalacja Aurelii już trwa"); return r; }
    // A new release must never resume the partial of an older release.
    const auto identity = readSmall(root / L"download.sha256");
    if (identity != release.sha256) { std::filesystem::remove(partial, ec); atomic_file::write(root / L"download.sha256", release.sha256); }
    const auto stopped = [&] { return cancel && cancel(); };
    const auto error = httpDownload(release.url, partial, release.size, stopped,
        [&](uint64_t done) { if (progress) progress(done, release.size); }, allowedDownload);
    r.cancelled = stopped();
    r.ok = !r.cancelled && error == DownloadError::none && publish(root, release, partial);
    CloseHandle(lock);
    if (!r.ok) r.error = r.cancelled ? tr(L"Anulowano") : tr(L"Nie udało się pobrać lub zweryfikować Aurelii");
    return r;
}
}
