#include "../src/store/epic_legendary.hpp"
#include "../src/platform/user_config.hpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || std::wstring_view(argv[1]) != L"--prepare-helper") {
        std::cout << "Usage: epic_backend_probe --prepare-helper (public configuration + verified Legendary download; no login)\n";
        return 2;
    }
    const auto root = userConfigDirectory();
    const auto config = epic::prepareConfig(root / L"accounts" / L"epic-client.bin");
    std::cout << "public_client_profile=" << config.valid() << '\n';
    const auto helper = epic::prepareLegendary(root, [] { return false; });
    std::cout << "helper_sha256_verified=" << !helper.empty() << '\n';
    return config.valid() && !helper.empty() ? 0 : 1;
}
