#include "../src/library/visibility.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc == 2) {
        const fs::path root = argv[1];
        const auto excluded = excludedSteamApps(root);
        std::cout << "active=" << activeSteamId(root) << " excluded=" << excluded.size()
                  << " example_private=" << excluded.contains(3711590) << '\n';
        return 0;
    }
    const auto root = fs::temp_directory_path() /
                      (L"launchme-visibility-" + std::to_wstring(GetCurrentProcessId()));
    const auto selected = root / "userdata" / "39734314" / "config";
    const auto other = root / "userdata" / "39734349" / "config";
    fs::create_directories(root / "config");
    fs::create_directories(selected / "cloudstorage");
    fs::create_directories(other);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{root};
    {
        std::ofstream login(root / "config" / "loginusers.vdf");
        login << "\"users\"\n{\n\"76561198000000077\"\n{\n"
              << "\"AutoLogin\" \"0\"\n\"timestamp\" \"999\"\n}\n"
              << "\"76561198000000042\"\n{\n"
              << "\"AutoLogin\" \"1\"\n\"timestamp\" \"100\"\n}\n}\n";
    }
    {
        std::ofstream local(selected / "localconfig.vdf");
        local << "\"PrivateApps_39734314\" \"[10,11,35]\"\n";
    }
    {
        std::ofstream local(other / "localconfig.vdf");
        local << "\"PrivateApps_39734349\" \"[99]\"\n";
    }
    {
        std::ofstream cloud(selected / "cloudstorage" / "cloud-storage-namespace-1.json");
        cloud << R"([ ["user-collections.hidden",{"key":"user-collections.hidden","value":"{\"id\":\"hidden\",\"added\":[20,21,35],\"removed\":[21,35]}","version":"1"}] ])";
    }
    const auto excluded = excludedSteamApps(root);
    if (!excluded.contains(10) || !excluded.contains(11) || !excluded.contains(20) ||
        !excluded.contains(35) || excluded.contains(21) || excluded.contains(99)) {
        std::cerr << "Active account's private/hidden lists or removed IDs were parsed incorrectly\n";
        return 1;
    }
    return 0;
}
