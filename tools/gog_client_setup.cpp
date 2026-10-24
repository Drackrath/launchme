#include "../src/store/gog_account.hpp"
#include <iostream>

int main() {
    // Provision public application parameters only. Never authenticates a user,
    // reads browser sessions, opens a browser or emits credential values.
    const auto config = gog::loadConfig();
    std::cout << "GOG client profile ready=" << config.valid() << "; manual_callback=" << config.legacy_manual_callback << '\n';
    return config.valid() ? 0 : 1;
}
