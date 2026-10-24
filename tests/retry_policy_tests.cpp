#include "../src/artwork/retry_policy.hpp"

#include <iostream>

int main() {
    const auto first = artwork_retry::delay(0, 250);
    const auto second = artwork_retry::delay(1, 250);
    const auto third = artwork_retry::delay(2, 250);
    if (artwork_retry::max_retries != 3 || first != std::chrono::milliseconds(5250) ||
        second <= first || third <= second ||
        artwork_retry::cooldown(false) <= artwork_retry::cooldown(true)) {
        std::cerr << "Artwork retries would flood the API instead of backing off\n";
        return 1;
    }
}
