#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include "updater.hpp"
#include "hashing.hpp"
#include "../net/json.hpp"
#include "text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#else
#include "system.hpp"
#include <cerrno>
#include <cstring>
#include <span>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <fstream>

namespace updater {
namespace {
bool hex64(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string lowered(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return text;
}
std::optional<std::array<uint32_t, 3>> parts(std::wstring_view version) {
    std::array<uint32_t, 3> out{};
    size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        if (at >= version.size() || !std::iswdigit(version[at])) return std::nullopt;
        uint64_t value = 0;
        while (at < version.size() && std::iswdigit(version[at])) {
            value = value * 10 + static_cast<uint64_t>(version[at++] - L'0');
            if (value > 100000) return std::nullopt;
        }
        out[static_cast<size_t>(i)] = static_cast<uint32_t>(value);
        if (i < 2) { if (at >= version.size() || version[at] != L'.') return std::nullopt; ++at; }
    }
    return at == version.size() ? std::optional(out) : std::nullopt;
}
// The notes' own hash line: "SHA-256: `<hex>`".
std::string bodyHash(std::string_view body) {
    const auto at = body.find("SHA-256:");
    if (at == std::string_view::npos) return {};
    std::string hex;
    for (size_t i = at + 8; i < body.size() && hex.size() < 64; ++i) {
        const char c = body[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) hex += c;
        else if (!hex.empty()) break;
    }
    hex = lowered(hex);
    return hex64(hex) ? hex : std::string{};
}
#ifdef _WIN32
// A relative path inside the package, "launchme/" removed; empty when it is not a safe one.
std::filesystem::path packagePath(std::string_view name) {
    constexpr std::string_view root = "launchme/";
    if (!name.starts_with(root)) return {};
    name.remove_prefix(root.size());
    if (name.empty() || name.size() > 400) return {};
    std::filesystem::path out;
    size_t start = 0;
    while (start <= name.size()) {
        const auto end = std::min(name.find('/', start), name.size());
        const auto part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." ||
            std::any_of(part.begin(), part.end(), [](char c) { return c == ':' || c == '\\' || static_cast<unsigned char>(c) < 0x20; })) return {};
        out /= text_encoding::wideLenient(part);
        if (end == name.size()) break;
        start = end + 1;
    }
    return out;
}
#else
// The package's first bytes: ELF, 64-bit, little-endian, an executable or PIE for x86-64, with the
// AppImage type 2 mark in e_ident's padding.
bool appImageFile(std::span<const uint8_t> head) {
    if (head.size() < 64 || std::memcmp(head.data(), "\x7F" "ELF", 4) != 0) return false;
    if (head[4] != 2 || head[5] != 1 || head[6] != 1) return false; // ELFCLASS64, ELFDATA2LSB, EV_CURRENT.
    if (head[8] != 'A' || head[9] != 'I' || head[10] != 2) return false;
    const unsigned type = head[16] | head[17] << 8, machine = head[18] | head[19] << 8;
    return (type == 2 || type == 3) && machine == 62; // ET_EXEC or ET_DYN; EM_X86_64.
}
std::wstring failure(std::wstring what, const std::filesystem::path& file) {
    return what + L" " + file.wstring() + L" (error " + std::to_wstring(errno) + L")";
}
#endif
std::wstring oldName(const std::filesystem::path& file) { return file.wstring() + old_suffix; }
}

bool allowedDownload(std::wstring_view url) {
    return url.starts_with(L"https://github.com/uxandai/launchme/releases/download/") ||
           url.starts_with(L"https://release-assets.githubusercontent.com/") ||
           url.starts_with(L"https://objects.githubusercontent.com/");
}

#ifndef _WIN32
std::optional<std::filesystem::path> appImage() {
    const auto value = system_info::environment(L"APPIMAGE", 4096);
    if (!value || value->empty()) return std::nullopt;
    const std::filesystem::path path(*value);
    std::error_code ec;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, ec)) return std::nullopt;
    return path;
}
#endif

std::optional<Release> parseRelease(std::string_view text) {
#ifndef _WIN32
    if (!appImage()) return std::nullopt; // Not an AppImage: nothing it could replace.
#endif
    const auto root = json::parse(text, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object) return std::nullopt;
    if (const auto* draft = root->get("draft"); draft && draft->flag) return std::nullopt;
    if (const auto* pre = root->get("prerelease"); pre && pre->flag) return std::nullopt;
    auto tag = root->get("tag_name") ? root->get("tag_name")->stringOr() : std::string{};
    if (tag.starts_with('v')) tag.erase(0, 1);
    Release out;
    out.version = text_encoding::wideLenient(tag);
    if (!parts(out.version)) return std::nullopt;
    const auto page = root->get("html_url") ? root->get("html_url")->stringOr() : std::string{};
    out.page = page.starts_with("https://github.com/uxandai/launchme/") ? text_encoding::wideLenient(page) : std::wstring(releases_page);
    const auto body = root->get("body") ? root->get("body")->stringOr() : std::string{};
    out.notes = plainNotes(body);
#ifdef _WIN32
    const auto wanted = "launchme-" + tag + "-win64.zip";
#else
    const auto wanted = "launchme-" + tag + "-x86_64.AppImage";
#endif
    if (const auto* assets = root->get("assets"); assets && assets->type == json::Value::Type::array) {
        for (const auto& asset : assets->elements) {
            if (!asset.get("name") || asset.get("name")->stringOr() != wanted) continue;
            const auto url = text_encoding::wideLenient(asset.get("browser_download_url") ? asset.get("browser_download_url")->stringOr() : std::string{});
            const auto* size = asset.get("size");
            if (!allowedDownload(url) || !size || size->type != json::Value::Type::number || size->numeric < 1 || size->numeric > 1024.0 * 1024 * 1024) continue;
            out.package_url = url;
            out.package_size = static_cast<uint64_t>(size->numeric);
            auto digest = asset.get("digest") ? asset.get("digest")->stringOr() : std::string{};
            if (digest.starts_with("sha256:")) digest = lowered(digest.substr(7));
            out.sha256 = hex64(digest) ? digest : std::string{};
            break;
        }
    }
    if (out.sha256.empty()) out.sha256 = bodyHash(body);
    if (out.package_url.empty() || out.sha256.empty()) return std::nullopt;
    return out;
}

std::wstring plainNotes(std::string_view markdown) {
    std::wstring out;
    bool skipping = false, blank = true;
    size_t start = 0;
    while (start <= markdown.size()) {
        const auto end = std::min(markdown.find('\n', start), markdown.size());
        std::string line(markdown.substr(start, end - start));
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(' ');
        std::string_view rest = first == std::string::npos ? std::string_view{} : std::string_view(line).substr(first);
        if (rest.starts_with('#')) {
            const auto title = rest.substr(std::min(rest.find_first_not_of("# "), rest.size()));
            skipping = title == "Install";
            if (skipping) continue;
            line = std::string(title);
            if (!out.empty() && !blank) out += L'\n';
        } else if (skipping) continue;
        else if (rest.starts_with("SHA-256")) continue;
        else if (rest.starts_with("- ") || rest.starts_with("* ")) line = std::string(first, ' ') + "\xE2\x80\xA2 " + std::string(rest.substr(2));
        // Emphasis and code marks go; a link keeps its text.
        std::string clean;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '*' || line[i] == '`') continue; // **bold**, *italic*, `code`.
            if (line[i] == '[') {
                const auto close = line.find("](", i);
                const auto paren = close == std::string::npos ? std::string::npos : line.find(')', close);
                if (paren != std::string::npos) { clean += line.substr(i + 1, close - i - 1); i = paren; continue; }
            }
            clean += line[i];
        }
        const bool empty = clean.find_first_not_of(' ') == std::string::npos;
        if (empty) { if (!blank) out += L'\n'; blank = true; continue; }
        out += text_encoding::wideLenient(clean) + L'\n';
        blank = false;
        if (end == markdown.size()) break;
    }
    while (!out.empty() && (out.back() == L'\n' || out.back() == L' ')) out.pop_back();
    return out;
}

bool newer(std::wstring_view candidate, std::wstring_view current) {
    const auto a = parts(candidate), b = parts(current);
    return a && b && *a > *b;
}

std::string sha256File(const std::filesystem::path& file) {
    return hashing::fileHex(hashing::Algorithm::sha256, file);
}

#ifdef _WIN32
std::optional<std::vector<std::filesystem::path>> unpack(const std::filesystem::path& zip, const std::filesystem::path& staging, std::wstring& error) {
    std::error_code ec;
    if (std::filesystem::exists(staging, ec)) std::filesystem::remove_all(staging, ec);
    if (!std::filesystem::create_directories(staging, ec)) { error = L"Cannot create " + staging.wstring(); return std::nullopt; }
    FILE* file = nullptr;
    if (_wfopen_s(&file, zip.c_str(), L"rb") || !file) { error = L"Cannot open the package"; return std::nullopt; }
    struct Close { FILE* f; ~Close() { std::fclose(f); } } close{file};
    const auto size = std::filesystem::file_size(zip, ec);
    mz_zip_archive archive{};
    if (ec || !mz_zip_reader_init_cfile(&archive, file, size, 0)) { error = L"The package is not a ZIP"; return std::nullopt; }
    struct End { mz_zip_archive* a; ~End() { mz_zip_reader_end(a); } } end{&archive};
    const auto count = mz_zip_reader_get_num_files(&archive);
    if (count == 0 || count > 2000) { error = L"Unexpected package contents"; return std::nullopt; }
    // Every entry checked before anything is written.
    std::vector<std::pair<mz_uint, std::filesystem::path>> entries;
    uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) { error = L"Unreadable package entry"; return std::nullopt; }
        std::string name = stat.m_filename;
        if (stat.m_is_directory || name.ends_with('/')) continue;
        auto relative = packagePath(name);
        if (relative.empty()) { error = L"Unsafe path in the package: " + text_encoding::wideLenient(name); return std::nullopt; }
        total += stat.m_uncomp_size;
        if (total > 1024ULL * 1024 * 1024) { error = L"The package is too large"; return std::nullopt; }
        entries.emplace_back(i, std::move(relative));
    }
    if (std::none_of(entries.begin(), entries.end(), [](const auto& e) { return e.second == L"launchme.exe"; })) {
        error = L"The package has no launchme.exe"; return std::nullopt;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& [index, relative] : entries) {
        const auto target = staging / relative;
        std::filesystem::create_directories(target.parent_path(), ec);
        FILE* out = nullptr;
        if (_wfopen_s(&out, target.c_str(), L"wb") || !out) { error = L"Cannot write " + target.wstring(); return std::nullopt; }
        const bool written = mz_zip_reader_extract_to_cfile(&archive, index, out, 0);
        const bool closed = std::fclose(out) == 0;
        if (!written || !closed) { error = L"Cannot unpack " + relative.wstring(); return std::nullopt; }
        files.push_back(relative);
    }
    return files;
}
#else
std::optional<std::vector<std::filesystem::path>> unpack(const std::filesystem::path& package, const std::filesystem::path& staging, std::wstring& error) {
    std::error_code ec;
    if (std::filesystem::exists(staging, ec)) std::filesystem::remove_all(staging, ec);
    if (!std::filesystem::create_directories(staging, ec)) { error = L"Cannot create " + staging.wstring(); return std::nullopt; }
    std::ifstream in(package, std::ios::binary);
    std::array<uint8_t, 64> head{};
    if (!in || !in.read(reinterpret_cast<char*>(head.data()), head.size())) { error = L"Cannot open the package"; return std::nullopt; }
    if (!appImageFile(head)) { error = L"The package is not an x86-64 AppImage"; return std::nullopt; }
    const auto size = std::filesystem::file_size(package, ec);
    if (ec || size > 1024ULL * 1024 * 1024) { error = L"The package is too large"; return std::nullopt; }
    const auto running = appImage();
    const std::filesystem::path name = running ? running->filename() : std::filesystem::path(L"launchme.AppImage");
    const auto target = staging / name;
    if (!std::filesystem::copy_file(package, target, std::filesystem::copy_options::overwrite_existing, ec)) {
        error = L"Cannot write " + target.wstring(); return std::nullopt;
    }
    std::filesystem::permissions(target, std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                                 std::filesystem::perms::others_read | std::filesystem::perms::others_exec, ec);
    if (ec) { error = L"Cannot make " + target.wstring() + L" executable"; return std::nullopt; }
    return std::vector<std::filesystem::path>{name};
}
#endif

#ifdef _WIN32
void rollback(const std::filesystem::path& app, const std::vector<std::filesystem::path>& files) {
    for (const auto& relative : files) {
        const auto target = app / relative;
        const std::filesystem::path old = oldName(target);
        std::error_code ec;
        if (std::filesystem::exists(old, ec)) {
            if (std::filesystem::exists(target, ec)) DeleteFileW(target.c_str());
            MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        } else DeleteFileW(target.c_str()); // A file the package added.
    }
}

bool apply(const std::filesystem::path& staging, const std::vector<std::filesystem::path>& files, const std::filesystem::path& app, std::wstring& error) {
    std::vector<std::filesystem::path> done;
    for (const auto& relative : files) {
        const auto target = app / relative, staged = staging / relative;
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (std::filesystem::exists(target, ec)) {
            const std::filesystem::path old = oldName(target);
            DeleteFileW(old.c_str()); // A leftover of an earlier update, if no longer in use.
            if (!MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                error = L"Cannot replace " + target.wstring() + L" (error " + std::to_wstring(GetLastError()) + L")";
                rollback(app, done);
                return false;
            }
        }
        done.push_back(relative);
        if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED)) {
            error = L"Cannot install " + target.wstring() + L" (error " + std::to_wstring(GetLastError()) + L")";
            rollback(app, done);
            return false;
        }
    }
    return true;
}
#else
void rollback(const std::filesystem::path& app, const std::vector<std::filesystem::path>& files) {
    for (const auto& relative : files) {
        const auto target = app / relative;
        const std::filesystem::path old = oldName(target);
        std::error_code ec;
        if (std::filesystem::exists(old, ec)) {
            // Back in one step; when the new one never moved in, both names are the same file.
            if (std::filesystem::equivalent(old, target, ec)) std::filesystem::remove(old, ec);
            else ::rename(old.c_str(), target.c_str());
        }
        else std::filesystem::remove(target, ec); // A file the package added.
    }
}

bool apply(const std::filesystem::path& staging, const std::vector<std::filesystem::path>& files, const std::filesystem::path& app, std::wstring& error) {
    std::vector<std::filesystem::path> done;
    for (const auto& relative : files) {
        const auto target = app / relative, staged = staging / relative;
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (std::filesystem::exists(target, ec)) {
            // The old copy stays under its name until the new one replaces it: a second name (a
            // copy where the file system has no hard links), so there is never a moment without one.
            const std::filesystem::path old = oldName(target);
            std::filesystem::remove(old, ec); // A leftover of an earlier update.
            if (::link(target.c_str(), old.c_str()) != 0 &&
                !std::filesystem::copy_file(target, old, std::filesystem::copy_options::overwrite_existing, ec)) {
                error = failure(L"Cannot keep", target);
                rollback(app, done);
                return false;
            }
        }
        done.push_back(relative);
        if (::rename(staged.c_str(), target.c_str()) != 0) { // Atomic; the running AppImage stays mapped.
            error = failure(L"Cannot install", target);
            rollback(app, done);
            return false;
        }
    }
    return true;
}
#endif

void cleanup(const std::filesystem::path& app) {
    std::error_code ec;
    std::vector<std::filesystem::path> old;
    for (auto it = std::filesystem::recursive_directory_iterator(app, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 4) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(ec) && it->path().wstring().ends_with(old_suffix)) old.push_back(it->path());
    }
#ifdef _WIN32
    for (const auto& file : old) DeleteFileW(file.c_str());
#else
    for (const auto& file : old) std::filesystem::remove(file, ec);
#endif
    std::filesystem::remove_all(app / work_folder, ec);
}
}
