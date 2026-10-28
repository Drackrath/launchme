// Manual probe on real hardware (Linux counterpart of device_live_test.cpp). Read-only: no pairing,
// connecting or rumble. --input reads pads for 10 s; --controllers / --controller-panel list them;
// --steam-controller also runs the experimental Steam Controller reader; default: the Bluetooth panel.
#include "../src/platform/bluetooth_controllers.hpp"
#include "../src/platform/device_service.hpp"
#include "../src/platform/game_controllers.hpp"
#include "../src/platform/text_encoding.hpp"
#include <iostream>
#include <thread>

namespace {
int inputProbe(bool steam) {
    ControllerInput input;
    input.initialize(nullptr);
    input.steamController(steam);
    const auto start = GetTickCount64();
    ULONGLONG next = 0;
    while (GetTickCount64() - start < 10000) {
        const auto state = input.read();
        if (GetTickCount64() >= next) {
            next = GetTickCount64() + 500;
            std::cout << "Input connected=" << state.connected << " id=" << text_encoding::utf8(state.id)
                      << " name=" << text_encoding::utf8(state.name) << " x=" << state.x << " y=" << state.y
                      << " lt=" << state.left_trigger << " buttons=0x" << std::hex << state.buttons << std::dec
                      << " battery=" << (state.battery ? std::to_string(*state.battery) : "-") << '\n';
        }
        Sleep(10);
    }
    return 0;
}
}

int main(int argc, char** argv) {
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode == "--input" || mode == "--steam-controller") return inputProbe(mode == "--steam-controller");
    if (mode == "--controllers" || mode == "--controller-panel") {
        const bool panel = mode == "--controller-panel";
        const auto controllers = panel ? controllerPanelControllers() : enumerateControllers();
        std::cout << (panel ? "Panel controllers=" : "Connected controllers=") << controllers.size() << '\n';
        for (const auto& controller : controllers)
            std::cout << "name=" << text_encoding::utf8(controller.name) << "; picture=" << static_cast<int>(controller.picture)
                      << "; connection=" << text_encoding::utf8(controller.connection) << "; connected=" << controller.connected
                      << "; radio_off=" << controller.radio_off << "; battery=" << (controller.battery ? std::to_string(*controller.battery) : "-")
                      << "; bluetooth=" << text_encoding::utf8(controller.bluetooth_id) << "; id=" << text_encoding::utf8(controller.id) << '\n';
        return 0;
    }
    const auto service = systemDeviceService();
    service->open(devices::Panel::bluetooth);
    std::this_thread::sleep_for(std::chrono::seconds(4));
    const auto bt = service->snapshot();
    std::cout << "Bluetooth discovery entries=" << bt.bluetooth.size() << "; status=" << text_encoding::utf8(bt.status) << '\n';
    for (const auto& device : bt.bluetooth)
        std::cout << "  " << text_encoding::utf8(device.name) << " paired=" << device.paired << " connected=" << device.connected
                  << " audio=" << device.audio << '\n';
    service->close();
    return bt.status.empty() ? 0 : 2;
}
