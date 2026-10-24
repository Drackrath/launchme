// Dry run of the DRM-free folder scanner: drm_free_probe <folder> [<folder>...]
// Prints what every subfolder would become (title, executable, arguments). Reads only;
// no catalog is written and RAWG is not asked.
#include "../src/library/drm_free.hpp"

#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    if (argc < 2) { std::wcout << L"usage: drm_free_probe <folder> [<folder>...]\n"; return 2; }
    drm_free::Catalog catalog;
    for (int i = 1; i < argc; ++i) catalog.roots.emplace_back(argv[i]);
    drm_free::ScanOptions options; options.full = true; options.now = 1;
    const auto started = GetTickCount64();
    const auto result = drm_free::scan(catalog, options);
    // One line per game: folder | title [year] | second query | executable below the folder [arguments].
    for (const auto& entry : result.games) {
        std::wcout << entry.folder.filename().native() << L" | " << entry.title;
        if (entry.year) std::wcout << L" [" << entry.year << L']';
        std::wcout << L" | " << entry.product << L" | " << entry.executable.lexically_relative(entry.folder).native();
        if (!entry.arguments.empty()) std::wcout << L" [" << entry.arguments << L']';
        if (entry.steam_app_id) std::wcout << L" steam:" << entry.steam_app_id;
        std::wcout << L'\n';
    }
    std::wcout << result.games.size() << L" games in " << (GetTickCount64() - started) << L" ms\n";
    return 0;
}
