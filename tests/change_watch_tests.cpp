// Folder change notifications (src/platform/change_watch.hpp; inotify on Linux): changes, subtrees,
// wakes, a watched folder that goes away, and the file/folder stamps.
#include "../src/platform/change_watch.hpp"

#include <Windows.h>

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const auto root = fs::temp_directory_path() / (L"launchme-change-watch-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored;
    fs::remove_all(root, ignored);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{root};
    fs::create_directories(root / L"flat");
    fs::create_directories(root / L"tree" / L"deep");

    WakeSignal wake;
    ChangeWatch watch;
    if (!watch.empty() || watch.directory(root / L"missing")) return 1;
    if (!watch.directory(root / L"flat") || !watch.directory(root / L"tree", true) || watch.size() != 2) return 2;
    watch.wake(wake.event);
    if (watch.wait(50) != ChangeWatch::Result::timeout) return 3;
    std::ofstream(root / L"flat" / L"a.txt") << "x";
    if (watch.wait(1000) != ChangeWatch::Result::changed || watch.wait(50) != ChangeWatch::Result::timeout) return 4;
    // Subtree: an existing subfolder and one created after the watch began.
    std::ofstream(root / L"tree" / L"deep" / L"b.txt") << "x";
    if (watch.wait(1000) != ChangeWatch::Result::changed) return 5;
    fs::create_directories(root / L"tree" / L"new");
    if (!watch.settle(100, 2000)) return 6;
    std::ofstream(root / L"tree" / L"new" / L"c.txt") << "x";
    if (watch.wait(1000) != ChangeWatch::Result::changed || !watch.settle(100, 2000)) return 7;
    // A wake, also when it came before the wait; requested() stays until taken.
    wake.request();
    if (watch.wait(1000) != ChangeWatch::Result::woken || !wake.take() || wake.load()) return 8;
    if (watch.wait(50) != ChangeWatch::Result::timeout) return 9;
    wake.wake();
    if (watch.settle(100, 2000)) return 10; // Woken meanwhile.
    // A watched folder that goes away is a change and is dropped.
    fs::remove_all(root / L"flat");
    if (watch.wait(1000) != ChangeWatch::Result::changed || !watch.settle(100, 2000) || watch.size() != 1) return 11;
    watch.clear();
    if (!watch.empty() || watch.wait(20) != ChangeWatch::Result::timeout) return 12;

    // Stamps: absent, written, rewritten; a folder's matching files only.
    const auto file = root / L"tree" / L"appmanifest_10.acf";
    if (fileStamp(file)) return 13;
    const auto missing = folderStamp(root / L"tree", L"appmanifest_*.acf");
    std::ofstream(file) << "one";
    const auto first = fileStamp(file);
    const auto folder = folderStamp(root / L"tree", L"appmanifest_*.acf");
    if (!first || folder == missing) return 14;
    std::ofstream(root / L"tree" / L"other.txt") << "unrelated";
    if (folderStamp(root / L"tree", L"appmanifest_*.acf") != folder) return 15;
    std::ofstream(file) << "longer";
    if (fileStamp(file) == first || folderStamp(root / L"tree", L"appmanifest_*.acf") == folder) return 16;
    if (folderStamp(root / L"gone", L"*") == folderStamp(root / L"tree", L"*.none")) return 17;
    std::cout << "Change notifications, wakes and stamps passed\n";
    return 0;
}
