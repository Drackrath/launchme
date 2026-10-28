// Linux controller plumbing without hardware: sysfs/procfs parsing against a fake tree, HID report
// descriptors, BlueZ classification. Nothing opens a device, starts SDL or talks to D-Bus.
#include "../src/platform/bluez.hpp"
#include "../src/platform/game_controllers.hpp"
#include "../src/platform/linux_hid.hpp"
#include "../src/platform/steam_controller.hpp"
#include <Windows.h>
#include <xinput.h>
#include <fstream>
#include <iostream>

namespace {
void write(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}
}

int main() {
    const auto hid = linux_hid::parseUevent("DRIVER=playstation\nHID_ID=0005:0000054C:00000CE6\nHID_NAME=DualSense Wireless Controller\n"
                                            "HID_PHYS=00:1a:7d:da:71:13\nHID_UNIQ=a0:ab:51:12:34:56\nMODALIAS=hid:b0005g0000v0000054Cp00000CE6\n");
    if (!hid || hid->bus != linux_hid::bus_bluetooth || hid->vendor != 0x054c || hid->product != 0x0ce6 ||
        hid->name != "DualSense Wireless Controller" || hid->uniq != "a0:ab:51:12:34:56") return 1;
    if (linux_hid::parseUevent("HID_NAME=x\n") || linux_hid::parseUevent("HID_ID=0003:zz:0001\n")) return 2;
    if (linux_hid::macAddress("a0:ab:51:12:34:56") != 0xa0ab51123456ull || linux_hid::macAddress("a0ab51123456") ||
        linux_hid::macAddress("00:00:00:00:00:00") || linux_hid::macAddress("a0:ab:51:12:34:5g")) return 3;
    // Gamepad application collection with a nested physical one, then the Steam Controller's vendor
    // collection (32-bit usage page item), then a long item that must be skipped.
    const uint8_t descriptor[] = {0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x09, 0x30, 0x81, 0x02, 0xc0, 0xc0,
                                  0x07, 0x00, 0xff, 0x00, 0x00, 0x09, 0x01, 0xa1, 0x01, 0x09, 0x02, 0xa1, 0x02, 0xc0, 0xc0,
                                  0xfe, 0x01, 0x00, 0x00, 0x06, 0x00, 0xff, 0x0a, 0x02, 0x00, 0xa1, 0x01, 0xc0};
    const auto collections = linux_hid::topCollections(descriptor);
    if (collections != std::vector<std::pair<uint16_t, uint16_t>>{{1, 5}, {0xff00, 1}, {0xff00, 2}}) return 4;
    if (!steam_controller::inputCollection(collections[1].first, collections[1].second)) return 5;
    if (!linux_hid::topCollections(std::span(descriptor).first(4)).empty()) return 6; // Truncated: no collection, no overrun.

    const auto root = std::filesystem::temp_directory_path() / ("launchme-controllers-" + std::to_string(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    const auto sys = root / "sys";
    // hidraw7: a Bluetooth DualSense with its battery; event3: a USB xpad pad (no HID parent).
    const auto sense = sys / "devices/bt/0005:054C:0CE6.0001";
    write(sense / "uevent", "HID_ID=0005:0000054C:00000CE6\nHID_NAME=DualSense Wireless Controller\nHID_UNIQ=a0:ab:51:12:34:56\n");
    write(sense / "report_descriptor", std::string(reinterpret_cast<const char*>(descriptor), 16));
    write(sense / "power_supply/ps-controller-battery-a0:ab:51:12:34:56/capacity", "55\n");
    write(sense / "power_supply/ps-controller-battery-a0:ab:51:12:34:56/status", "Charging\n");
    std::filesystem::create_directories(sys / "class/hidraw/hidraw7");
    std::filesystem::create_directory_symlink(sense, sys / "class/hidraw/hidraw7/device");
    const auto usb = sys / "devices/usb/1-1:1.0";
    const auto input = usb / "input/input9";
    write(usb / "uevent", "DRIVER=xpad\n");
    write(input / "id/bustype", "0003\n"); write(input / "id/vendor", "045e\n"); write(input / "id/product", "028e\n");
    write(input / "name", "Microsoft X-Box 360 pad\n");
    std::filesystem::create_directory_symlink(usb, input / "device");
    std::filesystem::create_directories(sys / "class/input/event3");
    std::filesystem::create_directory_symlink(input, sys / "class/input/event3/device");

    const auto raw = linux_hid::node(L"/dev/hidraw7", sys);
    if (!raw || raw->product != 0x0ce6 || raw->bus != linux_hid::bus_bluetooth || raw->uniq != "a0:ab:51:12:34:56") return 7;
    const auto level = linux_hid::battery(raw->device);
    if (!level || level->level != 55 || !level->charging) return 8;
    const auto event = linux_hid::node(L"/dev/input/event3", sys);
    if (!event || event->bus != linux_hid::bus_usb || event->vendor != 0x045e || event->product != 0x028e ||
        event->name != "Microsoft X-Box 360 pad" || linux_hid::battery(event->device)) return 9;
    if (linux_hid::node(L"/dev/input/event4", sys) || linux_hid::node(L"/dev/input/js0", sys) || linux_hid::node(L"sdl:3", sys)) return 10;
    const auto nodes = linux_hid::hidraws(sys, "/dev");
    if (nodes.size() != 1 || nodes[0].node != "/dev/hidraw7" || nodes[0].descriptor.size() != 16 ||
        linux_hid::topCollections(nodes[0].descriptor) != std::vector<std::pair<uint16_t, uint16_t>>{{1, 5}}) return 11;

    // Steam is "steam" in /proc/<pid>/comm; its helpers and non-numeric entries don't count.
    const auto proc = root / "proc";
    write(proc / "self/comm", "steam\n");
    write(proc / "4242/comm", "steamwebhelper\n");
    if (linux_hid::processRunning("steam", proc)) return 12;
    write(proc / "4243/comm", "steam\n");
    if (!linux_hid::processRunning("steam", proc)) return 13;

    bluez::Device pad;
    pad.icon = "input-gaming";
    if (!bluez::gamepad(pad) || bluez::audio(pad)) return 14;
    pad.icon.clear(); pad.device_class = 0x002508; // Peripheral, gamepad.
    if (!bluez::gamepad(pad)) return 15;
    pad.device_class = 0x002540; // Peripheral, keyboard.
    if (bluez::gamepad(pad)) return 16;
    pad.device_class = 0; pad.appearance = 0x03c4;
    if (!bluez::gamepad(pad)) return 17;
    bluez::Device headphones; headphones.device_class = 0x240404; // Audio/Video, headset; audio service bit.
    if (!bluez::audio(headphones) || bluez::gamepad(headphones)) return 18;
    headphones.device_class = 0; headphones.appearance = 0x0941; // Wearable audio category (0x25).
    if (!bluez::audio(headphones)) return 19;
    if (bluez::modalias("usb:v054Cp0CE6d0100") != std::pair<uint16_t, uint16_t>{0x054c, 0x0ce6} ||
        bluez::modalias("bluetooth:v045Ep0B13d0509") != std::pair<uint16_t, uint16_t>{0x045e, 0x0b13} ||
        bluez::modalias("usb:v054C") || bluez::modalias("")) return 20;
    if (bluez::shortError("org.bluez.Error.AuthenticationFailed") != "AuthenticationFailed" || bluez::shortError("Failed") != "Failed") return 21;

    if (gameBarUsesGuideButton() || steam_controller::active() || !steam_controller::pads().empty()) return 22;
    // The shared parsers with XInput's bit values (device_tests covers them fully; it needs the
    // Windows-only frame_clock.hpp for now).
    std::vector<uint8_t> report(64); report[0] = 1; report[1] = report[2] = 128; report[8] = 0x22; report[10] = 0x01; report[53] = 0x14;
    const auto dualsense = parseDualSense(report);
    if (!dualsense.connected || dualsense.buttons != (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_RIGHT | pad_guide) || dualsense.battery != 45 || !dualsense.charging) return 23;
    std::vector<uint8_t> triton(54); triton[0] = 0x42; triton[2] = 0x41; triton[3] = 0x40; triton[12] = 0x80; triton[13] = 0x03;
    const auto steam = steam_controller::parseState(triton);
    if (!steam || steam->buttons != (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK) || steam->y != 896) return 24;
    if (dualSenseRumbleReport(true, true, 7)[77] != 0xca) return 25;
    std::cout << "sysfs, hidraw descriptors, /proc and BlueZ classification passed\n";
}
