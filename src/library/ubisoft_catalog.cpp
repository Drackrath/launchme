#include "ubisoft_catalog.hpp"
#include "ubisoft.hpp"
#include "../net/http.hpp"
#include "../platform/atomic_file.hpp"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

namespace ubisoft {
namespace {
const wchar_t* const uuid_catalog_url = L"https://cdn.jsdelivr.net/gh/mubaraknumann/unifiDB@main/ubisoft/uuid_catalog.json";
const wchar_t* const id_list_url = L"https://cdn.jsdelivr.net/gh/mubaraknumann/unifiDB@main/ubisoft/install_ids.txt";
constexpr ULONGLONG failure_backoff_ms = 15 * 60 * 1000;
constexpr auto unresolved_backoff = std::chrono::hours(24);
std::mutex refresh_lock;
std::atomic<ULONGLONG> failed_at{0};

std::string text(const HttpResponse& reply) {
    return reply.status == 200 ? std::string(reply.body.begin(), reply.body.end()) : std::string{};
}
}

bool refreshNames(const std::filesystem::path& directory, const std::vector<std::pair<std::string, std::string>>& products) {
    if (products.empty()) return false;
    std::lock_guard lock(refresh_lock);
    const auto names_file = directory / L"names.tsv", unresolved_file = directory / L"unresolved.tsv";
    auto names = loadNames(names_file);
    std::vector<Product> wanted;
    for (const auto& [id, uuid] : products) if (title(names, {id, uuid}).empty()) wanted.push_back({id, uuid});
    if (wanted.empty()) return true; // Another worker stored them meanwhile.
    // Products both catalogues lacked at the last download are not fetched again for a day.
    std::error_code ec;
    const auto age = std::filesystem::file_time_type::clock::now() - std::filesystem::last_write_time(unresolved_file, ec);
    if (!ec && age < unresolved_backoff) {
        const auto known = loadNames(unresolved_file);
        if (std::all_of(wanted.begin(), wanted.end(), [&](const Product& product) { return known.contains(product.id); })) return false;
    }
    if (const auto failed = failed_at.load(); failed && GetTickCount64() - failed < failure_backoff_ms) return false;
    HttpBudget budget(20000);
    const auto uuid_catalog = parseUuidCatalog(text(httpGet(uuid_catalog_url, L"Accept: application/json\r\n", 8 * 1024 * 1024)));
    const auto id_list = parseIdList(text(httpGet(id_list_url, L"Accept: text/plain\r\n", 4 * 1024 * 1024)));
    if (uuid_catalog.empty() && id_list.empty()) { failed_at = GetTickCount64(); return false; }
    failed_at = 0;
    bool added = false;
    std::map<std::string, std::wstring> unresolved;
    for (const auto& product : wanted) {
        if (const auto found = uuid_catalog.find(product.uuid); !product.uuid.empty() && found != uuid_catalog.end()) names[product.uuid] = found->second;
        else if (const auto listed = id_list.find(product.id); listed != id_list.end()) names[product.id] = listed->second;
        else { unresolved[product.id] = L"?"; continue; }
        added = true;
    }
    if (added && !atomic_file::write(names_file, serializeNames(names))) return false;
    atomic_file::write(unresolved_file, serializeNames(unresolved));
    return added;
}
}
