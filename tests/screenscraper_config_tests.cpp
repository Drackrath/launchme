#include "../src/library/screenscraper_config.hpp"
#include "../src/platform/user_config.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int main() {
    namespace fs = std::filesystem;
    using namespace screenscraper;
    const auto root = fs::temp_directory_path() / (L"launchme-screenscraper-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root);
    SetEnvironmentVariableW(L"LAUNCHME_CONFIG_DIR", root.c_str());
    for (const auto& field : fields) SetEnvironmentVariableW(field.environment, nullptr);

    // Defaults: nothing built in except the public API address, xml (as ES-DE) and our own software name.
    auto config = load();
    if (config.ready() || !config.dev_id.empty() || config.softname != L"Launchme" || config.output != L"xml" ||
        config.base_url != default_base_url) return 1;

    if (!save(Field::dev_id, L" my-dev ") || !save(Field::dev_password, L"dev secret") || !save(Field::softname, L"My Launcher 1.0") ||
        !save(Field::output, L"JSON") || !save(Field::base_url, L"https://example.test/api2") || !save(Field::user, L"player") ||
        !save(Field::password, L"pässword")) return 2;
    config = load();
    if (!config.ready() || config.dev_id != L"my-dev" || config.dev_password != L"dev secret" || config.softname != L"My Launcher 1.0" ||
        config.output != L"json" || config.base_url != L"https://example.test/api2/" || config.user != L"player" || config.password != L"pässword") return 3;

    // Passwords are DPAPI-encrypted on disk, never plain text.
    std::ifstream raw(root / L"screenscraper_dev_password.dat", std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.find("dev secret") != std::string::npos) return 4;
    raw.close();

    // Invalid values are refused and keep the stored ones.
    if (save(Field::base_url, L"http://insecure.test/") || save(Field::output, L"ini") || save(Field::dev_id, L"a\nb")) return 5;
    if (normalizedBaseUrl(L"https://api.screenscraper.fr/api2") != L"https://api.screenscraper.fr/api2/" ||
        !normalizedBaseUrl(L"https://x.test/a?b=1").empty() || !normalizedBaseUrl(L"ftp://x.test/").empty()) return 6;
    if (load().base_url != L"https://example.test/api2/") return 7;

    // The environment overrides the files; an emptied field returns to its default.
    SetEnvironmentVariableW(L"SCREENSCRAPER_DEV_PASSWORD", L"from-env");
    if (load().dev_password != L"from-env") return 8;
    SetEnvironmentVariableW(L"SCREENSCRAPER_DEV_PASSWORD", nullptr);
    if (!save(Field::softname, L"") || !save(Field::password, L"") || load().softname != L"Launchme" ||
        !load().password.empty() || stored(Field::password) || !stored(Field::dev_password)) return 9;
    if (!findField("base_url") || findField("missing")) return 10;
    return 0;
}
