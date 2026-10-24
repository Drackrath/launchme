#pragma once
// Which picture a game controller gets: exact USB VID/PID first, then the product
// name Windows reports (Bluetooth and XInput paths often hide or spoof the IDs).
// IDs follow SDL's usb_ids.h / controller_list.h. Portable: no Windows headers.
#include "devices.hpp"
#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>

namespace devices {
struct ControllerModel { uint16_t vendor, product; Picture picture; const wchar_t* name = nullptr; };

inline constexpr ControllerModel controller_models[] = {
    // Sony
    {0x054c, 0x05c4, Picture::dualshock4},   // DualShock 4
    {0x054c, 0x05c5, Picture::dualshock4},   // DualShock 4 Strikepad grip
    {0x054c, 0x09cc, Picture::dualshock4},   // DualShock 4 (second revision)
    {0x054c, 0x0ba0, Picture::dualshock4},   // DualShock 4 USB wireless adapter
    {0x054c, 0x0ce6, Picture::dualsense},
    {0x054c, 0x0df2, Picture::dualsense_edge},
    // Microsoft
    {0x045e, 0x028e, Picture::xbox360},      // Xbox 360 wired
    {0x045e, 0x028f, Picture::xbox360},      // Xbox 360 Play & Charge cable
    {0x045e, 0x0291, Picture::xbox360},      // Xbox 360 wireless receiver (third party)
    {0x045e, 0x02a0, Picture::xbox360},      // Xbox 360 Big Button receiver
    {0x045e, 0x02a1, Picture::xbox360},      // Xbox 360 wireless (XUSB driver)
    {0x045e, 0x02a9, Picture::xbox360},      // Xbox 360 wireless receiver (third party)
    {0x045e, 0x0719, Picture::xbox360},      // Xbox 360 wireless receiver
    {0x045e, 0x02d1, Picture::xbox, L"Xbox One"},
    {0x045e, 0x02dd, Picture::xbox, L"Xbox One"},         // 2015 firmware
    {0x045e, 0x02e0, Picture::xbox, L"Xbox One S"},       // Bluetooth
    {0x045e, 0x02ea, Picture::xbox, L"Xbox One S"},
    {0x045e, 0x02fd, Picture::xbox, L"Xbox One S"},       // Bluetooth
    {0x045e, 0x02ff, Picture::xbox, L"Xbox One"},         // XBOXGIP driver
    {0x045e, 0x0b12, Picture::xbox, L"Xbox Series X|S"},
    {0x045e, 0x0b13, Picture::xbox, L"Xbox Series X|S"},  // BLE
    {0x045e, 0x0b20, Picture::xbox, L"Xbox One S"},       // BLE
    {0x045e, 0x02e3, Picture::xbox_elite, L"Xbox Elite"},
    {0x045e, 0x0b00, Picture::xbox_elite, L"Xbox Elite Series 2"},
    {0x045e, 0x0b05, Picture::xbox_elite, L"Xbox Elite Series 2"}, // Bluetooth
    {0x045e, 0x0b22, Picture::xbox_elite, L"Xbox Elite Series 2"}, // BLE
    {0x045e, 0x0b0a, Picture::xbox_adaptive},
    {0x045e, 0x0b0c, Picture::xbox_adaptive},  // Bluetooth
    {0x045e, 0x0b21, Picture::xbox_adaptive},  // BLE
    // Nintendo (also spoofed by 8BitDo and others in Switch mode)
    {0x057e, 0x2009, Picture::switch_pro},
    {0x057e, 0x2069, Picture::switch_pro, L"Nintendo Switch 2 Pro Controller"},
    {0x057e, 0x2006, Picture::joycon_left},
    {0x057e, 0x2067, Picture::joycon_left},    // Switch 2
    {0x057e, 0x2007, Picture::joycon_right},
    {0x057e, 0x2066, Picture::joycon_right},   // Switch 2
    {0x057e, 0x2008, Picture::joycon_pair},    // combined pair
    {0x057e, 0x2068, Picture::joycon_pair},    // Switch 2 combined pair
    {0x057e, 0x200e, Picture::joycon_pair},    // Charging Grip
    // Valve
    {0x28de, 0x1101, Picture::steam_controller},
    {0x28de, 0x1102, Picture::steam_controller},  // wired
    {0x28de, 0x1105, Picture::steam_controller},  // Bluetooth
    {0x28de, 0x1106, Picture::steam_controller},  // Bluetooth
    {0x28de, 0x1142, Picture::steam_controller},  // wireless dongle
    {0x28de, 0x1201, Picture::steam_controller},
    {0x28de, 0x1202, Picture::steam_controller},
    {0x28de, 0x1302, Picture::steam_controller2, L"Steam Controller (2026)"}, // USB
    {0x28de, 0x1303, Picture::steam_controller2, L"Steam Controller (2026)"}, // BLE
    {0x28de, 0x1304, Picture::steam_controller2, L"Steam Controller (2026)"}, // Puck
    {0x28de, 0x1305, Picture::steam_controller2, L"Steam Controller (2026)"}, // Puck
    {0x28de, 0x1205, Picture::steam_deck},
    {0x28de, 0x11ff, Picture::generic},           // Steam Input virtual pad: the real device is unknown
    // Logitech: DirectInput / XInput mode
    {0x046d, 0xc216, Picture::logitech, L"Logitech F310"}, // DirectInput
    {0x046d, 0xc21d, Picture::logitech, L"Logitech F310"}, // XInput
    {0x046d, 0xc218, Picture::logitech, L"Logitech F510"},
    {0x046d, 0xc21e, Picture::logitech, L"Logitech F510"},
    {0x046d, 0xc219, Picture::logitech, L"Logitech F710"},
    {0x046d, 0xc21f, Picture::logitech, L"Logitech F710"},
    // 8BitDo
    {0x2dc8, 0x9018, Picture::eightbitdo_zero2, L"8BitDo Zero 2"}, // SDL_GameControllerDB
    {0x2dc8, 0x3230, Picture::eightbitdo_zero2, L"8BitDo Zero 2"},
    {0x2dc8, 0x6000, Picture::eightbitdo_pro},  // SF30 Pro
    {0x2dc8, 0x6100, Picture::eightbitdo_pro},  // SF30 Pro (Bluetooth)
    {0x2dc8, 0x6001, Picture::eightbitdo_pro},  // SN30 Pro
    {0x2dc8, 0x6101, Picture::eightbitdo_pro},  // SN30 Pro (Bluetooth)
    {0x2dc8, 0x6003, Picture::eightbitdo_pro, L"8BitDo Pro 2"},
    {0x2dc8, 0x6006, Picture::eightbitdo_pro, L"8BitDo Pro 2"}, // Bluetooth
    {0x2dc8, 0x6009, Picture::eightbitdo_pro, L"8BitDo Pro 3"},
    {0x2dc8, 0x6012, Picture::ultimate2},       // Ultimate 2 Wireless
    {0x2dc8, 0x2002, Picture::eightbitdo_ultimate},  // Ultimate Wired for Xbox
    {0x2dc8, 0x3106, Picture::eightbitdo_ultimate},  // Ultimate Wired
    {0x2dc8, 0x310a, Picture::eightbitdo_ultimate, L"8BitDo Ultimate 2C Wireless"},
};

// Xbox-style pads share XInput; their "IG_" HID shadow is not a second controller.
inline bool xboxFamily(Picture picture) {
    return picture == Picture::xbox || picture == Picture::xbox360 || picture == Picture::xbox_elite || picture == Picture::xbox_adaptive;
}

inline Picture classifyController(uint16_t vendor, uint16_t product, std::wstring_view name) {
    for (const auto& model : controller_models)
        if (model.vendor == vendor && model.product == product) return model.picture;
    std::wstring lower(name);
    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    const auto has = [&](std::wstring_view part) { return lower.find(part) != std::wstring::npos; };
    if (vendor == 0x2dc8 || has(L"8bitdo")) {
        if (has(L"zero 2") || has(L"zero2")) return Picture::eightbitdo_zero2;
        if (has(L"ultimate 2c")) return Picture::eightbitdo_ultimate; // C-style body, not the Ultimate 2
        if (has(L"ultimate 2")) return Picture::ultimate2;
        if (has(L"ultimate")) return Picture::eightbitdo_ultimate;
        if (has(L"pro")) return Picture::eightbitdo_pro;          // Pro 2/3, SN30 Pro(+), SF30 Pro, N30 Pro
        if (has(L"sn30") || has(L"sf30") || has(L"snes")) return Picture::eightbitdo_sn;
    }
    if (has(L"dualsense edge")) return Picture::dualsense_edge;
    if (has(L"dualsense")) return Picture::dualsense;
    if (has(L"dualshock") || (vendor == 0x054c && has(L"wireless controller"))) return Picture::dualshock4;
    if (has(L"joy-con") || has(L"joycon")) {
        const bool left = has(L"(l)") || lower.ends_with(L" l"), right = has(L"(r)") || lower.ends_with(L" r");
        return left == right ? Picture::joycon_pair : left ? Picture::joycon_left : Picture::joycon_right;
    }
    if (lower.starts_with(L"pro controller") || (has(L"switch") && has(L"pro controller"))) return Picture::switch_pro;
    if (has(L"steam deck")) return Picture::steam_deck;
    if (has(L"steam controller")) return has(L"2nd") || has(L"second") || has(L"2026") || has(L"triton") || has(L"steam controller 2") ?
        Picture::steam_controller2 : Picture::steam_controller;
    if (has(L"f310") || has(L"f510") || has(L"f710")) return Picture::logitech;
    if (vendor == 0x045e || has(L"xbox") || has(L"xinput")) {
        if (has(L"elite")) return Picture::xbox_elite;
        if (has(L"adaptive")) return Picture::xbox_adaptive;
        if (has(L"360")) return Picture::xbox360;
        return Picture::xbox;
    }
    if (has(L"ultimate 2") && !has(L"ultimate 2c")) return Picture::ultimate2;
    return Picture::generic;
}

// Windows often supplies its localized HID class description instead of the product
// name. Keep useful product names, but resolve generic ones from the same VID/PID table.
inline std::wstring controllerDisplayName(uint16_t vendor, uint16_t product, std::wstring_view reported) {
    std::wstring lower(reported);
    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    const auto first = lower.find_first_not_of(L" \t\r\n");
    lower = first == lower.npos ? L"" : lower.substr(first, lower.find_last_not_of(L" \t\r\n") - first + 1);
    const bool generic = lower.empty() || lower.find(L"hid") != lower.npos || lower == L"controller" ||
        lower == L"gamepad" || lower == L"usb gamepad" || lower == L"wireless controller" ||
        lower == L"joystick" || lower == L"kontroler" || lower == L"kontroler gier";
    if (!generic) return std::wstring(reported);
    for (const auto& model : controller_models)
        if (model.vendor == vendor && model.product == product && model.name) return model.name;
    switch (classifyController(vendor, product, reported)) {
    case Picture::dualsense: return L"DualSense";
    case Picture::dualsense_edge: return L"DualSense Edge";
    case Picture::dualshock4: return L"DualShock 4";
    case Picture::xbox: return L"Xbox Wireless Controller";
    case Picture::xbox360: return L"Xbox 360";
    case Picture::xbox_elite: return L"Xbox Elite";
    case Picture::xbox_adaptive: return L"Xbox Adaptive Controller";
    case Picture::switch_pro: return L"Nintendo Switch Pro Controller";
    case Picture::joycon_left: return L"Joy-Con (L)";
    case Picture::joycon_right: return L"Joy-Con (R)";
    case Picture::joycon_pair: return L"Joy-Con (L/R)";
    case Picture::steam_controller: return L"Steam Controller";
    case Picture::steam_controller2: return L"Steam Controller (2026)";
    case Picture::steam_deck: return L"Steam Deck";
    case Picture::logitech: return L"Logitech Gamepad";
    case Picture::ultimate2: return L"8BitDo Ultimate 2 Wireless";
    case Picture::eightbitdo_pro: return L"8BitDo Pro";
    case Picture::eightbitdo_ultimate: return L"8BitDo Ultimate";
    case Picture::eightbitdo_sn: return L"8BitDo SN30";
    case Picture::eightbitdo_zero2: return L"8BitDo Zero 2";
    default: return std::wstring(reported);
    }
}
}
