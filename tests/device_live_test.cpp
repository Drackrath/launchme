#include "../src/platform/device_service.hpp"
#include "../src/platform/game_controllers.hpp"
#include "../src/platform/bluetooth_controllers.hpp"
#include "../src/platform/system_audio.hpp"
#include "../src/platform/text_encoding.hpp"
#include <winrt/base.h>
#include <iostream>
#include <iomanip>
#include <thread>

namespace {
struct InputProbe {
    ControllerInput input;
    ULONGLONG next_report = 0;
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* probe = reinterpret_cast<InputProbe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (probe && message == WM_INPUT) {
            const auto handle = reinterpret_cast<HRAWINPUT>(lparam);
            probe->input.rawInput(handle);
            if (GetTickCount64() >= probe->next_report) {
                probe->next_report = GetTickCount64() + 500;
                UINT size = 0;
                GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
                std::vector<uint8_t> bytes(size);
                if (size && size <= 65536 && GetRawInputData(handle, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
                    const auto* raw = reinterpret_cast<const RAWINPUT*>(bytes.data());
                    if (raw->header.dwType == RIM_TYPEHID && raw->data.hid.dwCount) {
                        RID_DEVICE_INFO info{sizeof(info)}; UINT info_size = sizeof(info);
                        GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICEINFO, &info, &info_size);
                        std::cout << "HID vid=" << std::hex << info.hid.dwVendorId << " pid=" << info.hid.dwProductId
                                  << std::dec << " report bytes=" << raw->data.hid.dwSizeHid << " prefix=";
                        for (DWORD i = 0; i < std::min<DWORD>(16, raw->data.hid.dwSizeHid); ++i)
                            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(raw->data.hid.bRawData[i]) << ' ';
                        std::cout << std::dec << '\n';
                    }
                }
            }
        } else if (probe && message == WM_INPUT_DEVICE_CHANGE) probe->input.deviceChanged(reinterpret_cast<HANDLE>(lparam));
        return DefWindowProcW(window, message, wparam, lparam);
    }
};
int inputProbe() {
    WNDCLASSW type{}; type.lpfnWndProc = InputProbe::windowProc;
    type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LaunchmeControllerProbe";
    RegisterClassW(&type);
    const auto window = CreateWindowW(type.lpszClassName, L"Controller input probe", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
    if (!window) return 3;
    InputProbe probe; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&probe));
    probe.input.initialize(window);
    const auto start = GetTickCount64();
    ULONGLONG next = 0;
    while (GetTickCount64() - start < 10000) {
        MSG message{};
        while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        const auto state = probe.input.read();
        if (GetTickCount64() >= next) {
            next = GetTickCount64() + 500;
            std::cout << "Input connected=" << state.connected << " source=" << (state.id.starts_with(L"xinput:") ? "XInput" : "HID")
                      << " name=" << text_encoding::utf8(state.name) << " x=" << state.x << " y=" << state.y
                      << " buttons=0x" << std::hex << state.buttons << std::dec << '\n';
        }
        Sleep(10);
    }
    DestroyWindow(window);
    return 0;
}
}

int main(int argc, char** argv) {
    winrt::init_apartment();
    if (argc == 2 && std::string_view(argv[1]) == "--input") return inputProbe();
    if (argc == 2 && (std::string_view(argv[1]) == "--controllers" || std::string_view(argv[1]) == "--controller-panel")) {
        enumerateControllers(); // Allow WGI's initial device discovery to complete.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const bool panel = std::string_view(argv[1]) == "--controller-panel";
        const auto controllers = panel ? controllerPanelControllers() : enumerateControllers();
        std::cout << (panel ? "Panel controllers=" : "Connected controllers=") << controllers.size() << '\n';
        for (const auto& controller : controllers)
            std::cout << "name=" << text_encoding::utf8(controller.name) << "; picture=" << static_cast<int>(controller.picture)
                      << "; connected=" << controller.connected << "; bluetooth=" << text_encoding::utf8(controller.bluetooth_id)
                      << "; id=" << text_encoding::utf8(controller.id) << '\n';
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-devices-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    SystemAudio audio;
    if (argc == 2 && std::string_view(argv[1]) == "--headphones") {
        const auto connected = audio.headphonesConnected();
        const auto image = headphonePicture(root);
        std::cout << "ATH-M50xBT2 connected=" << (connected ? (*connected ? "yes" : "no") : "unknown")
                  << "; validated picture bytes=" << image.size() << '\n';
        if (!connected || image.empty()) return 4;
        if (headphonePicture(root) != image) return 5; // Valid cached WebP takes the same path on reconnect.
        return 0;
    }
    const auto snapshot = audio.enumerate();
    std::cout << "Audio endpoints=" << snapshot.endpoints.size() << "; output readable=" << snapshot.audio[0].available << "; input readable=" << snapshot.audio[1].available
              << "; controllers=" << enumerateControllers().size() << '\n';
    for (const auto kind : {devices::Picture::dualsense, devices::Picture::ultimate2, devices::Picture::xbox}) {
        const auto bytes = controllerPicture(kind, root);
        if (bytes.empty()) { std::cerr << "Official PNG failed for kind " << static_cast<int>(kind) << '\n'; return 1; }
        std::cout << "Validated official PNG " << static_cast<int>(kind) << ": " << bytes.size() << " bytes\n";
    }
    const auto service = windowsDeviceService();
    service->open(devices::Panel::bluetooth);
    std::this_thread::sleep_for(std::chrono::seconds(4));
    const auto bt = service->snapshot();
    std::cout << "Bluetooth discovery entries=" << bt.bluetooth.size() << "; error=" << !bt.status.empty() << '\n';
    service->close();
    if (!bt.status.empty()) return 2;
    // Read-only: no submit(), pairing, mute, volume or default endpoint changes.
}
