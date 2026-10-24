#include "../src/ui/category_menu.hpp"

#include <iostream>

int main() {
    const float scroll = category_menu::maxScroll(900, 30);
    if (scroll <= 900 || category_menu::hit(780, 900, 30, scroll) < 20 ||
        category_menu::hit(20, 900, 30, scroll) != -1 ||
        category_menu::hit(895, 900, 30, scroll) != -1 ||
        category_menu::maxScroll(900, 5) != 0) {
        std::cerr << "Long Steam collection menu cannot scroll or hit visible rows\n";
        return 1;
    }
}
