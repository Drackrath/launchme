#include "../src/library/battlenet.hpp"
#include "../src/library/pc_platforms.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    int samples = 1;
    if (argc == 3 && std::string_view(argv[1]) == "--watch") samples = std::clamp(std::atoi(argv[2]), 1, 120);
    else if (argc != 1) { std::cerr << "Usage: battlenet_install_probe [--watch 1..120]\n"; return 2; }
    const auto discovery = discoverPcInstallations();
    std::wstring client;
    for (const auto& entry : discovery.clients) if (entry.source == GameSource::battlenet) client = entry.executable.wstring();
    std::vector<battlenet::RegistryHint> hints;
    for (const auto& program : discovery.programs) if (const auto uid = battlenet::uninstallUid(program.uninstall); !uid.empty()) hints.push_back({uid, program.directory});
    std::vector<Game> previous;
    bool available = false;
    for (int i = 0; i < samples; ++i) {
        if (i) std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto start = std::chrono::steady_clock::now();
        auto scan = battlenet::scan(discovery.battlenet_database, discovery.battlenet_aggregate, client, hints, previous);
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto count = [&](auto predicate) { return std::count_if(scan.games.begin(), scan.games.end(), predicate); };
        std::cout << "sample=" << i + 1 << " available=" << scan.available << " client=" << !client.empty() << " games=" << scan.games.size()
            << " ready=" << count([](const Game& g) { return g.installed; })
            << " installing=" << count([](const Game& g) { return g.installation.phase == InstallPhase::staging; })
            << " unknown=" << count([](const Game& g) { return g.installation.phase == InstallPhase::unknown; })
            << " percentages=" << count([](const Game& g) { return g.installation.percent().has_value(); })
            << " scan_ms=" << ms << '\n';
        previous = std::move(scan.games); available = scan.available;
    }
    return available ? 0 : 1;
}
