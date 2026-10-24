// Manual backup/verification helper; never run by CTest against a real user profile.
#include "../src/net/companion_backup.hpp"
#include "../src/net/companion_characters.hpp"
#include "../src/platform/text_encoding.hpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    companion::backup::Result result;
    if (argc == 5 && std::wstring_view(argv[1]) == L"--export") {
        const auto look = companion::lookFromKey(text_encoding::utf8(argv[2]));
        if (!look) { std::cerr << "Unknown companion\n"; return 2; }
        companion::backup::Input input;
        input.look = *look; input.config = userConfigDirectory(); input.settings = loadAppSettings();
        input.destination = argv[3]; input.executable = argv[4];
        result = companion::backup::create(input);
    } else if (argc == 3 && std::wstring_view(argv[1]) == L"--verify") result = companion::backup::verify(argv[2]);
    else { std::cerr << "Usage: companion_backup --export <key> <existing destination> <launchme.exe> | --verify <backup folder>\n"; return 2; }
    if (!result.ok) { std::cerr << text_encoding::utf8(result.error) << '\n'; return 1; }
    std::cout << text_encoding::utf8(result.path.wstring()) << "\nfiles=" << result.files << " conversations=" << result.conversations << " bytes=" << result.bytes << '\n';
    return 0;
}
