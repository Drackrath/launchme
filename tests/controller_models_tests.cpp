// Controller family classification: exact USB IDs, product-name fallbacks and the
// stable enum values that name the cached vendor PNGs.
#include "../src/model/controller_models.hpp"
#include "../src/platform/controller_overrides.hpp"
#include <iostream>
#include <set>
#include <utility>

namespace {
int failures = 0;
void expect(uint16_t vendor, uint16_t product, const wchar_t* name, devices::Picture expected) {
    const auto actual = devices::classifyController(vendor, product, name);
    if (actual == expected) return;
    ++failures;
    std::wcerr << L"Wrong picture for " << std::hex << vendor << L':' << product << std::dec << L" \"" << name
               << L"\": " << static_cast<int>(actual) << L" instead of " << static_cast<int>(expected) << L'\n';
}
}

int main() {
    using devices::Picture;
    // Cache files are "<value>.alpha-v2.png": the downloaded families keep their numbers.
    static_assert(static_cast<int>(Picture::dualsense) == 1 && static_cast<int>(Picture::ultimate2) == 2 &&
                  static_cast<int>(Picture::xbox) == 3 && static_cast<int>(Picture::steam_controller2) == 20);

    // The table is unambiguous and never maps a device to `none`.
    std::set<std::pair<uint16_t, uint16_t>> seen;
    for (const auto& model : devices::controller_models) {
        if (!seen.insert({model.vendor, model.product}).second || model.picture == Picture::none || model.picture >= Picture::count) {
            std::cerr << "Bad table entry " << std::hex << model.vendor << ':' << model.product << '\n';
            return 1;
        }
    }

    // USB VID/PID. Names are deliberately misleading: the IDs win.
    expect(0x054c, 0x05c4, L"", Picture::dualshock4);
    expect(0x054c, 0x09cc, L"Wireless Controller", Picture::dualshock4);
    expect(0x054c, 0x0ba0, L"", Picture::dualshock4);
    expect(0x054c, 0x0ce6, L"Wireless Controller", Picture::dualsense);
    expect(0x054c, 0x0df2, L"DualSense Wireless Controller", Picture::dualsense_edge);
    expect(0x045e, 0x028e, L"Controller", Picture::xbox360);
    expect(0x045e, 0x0719, L"", Picture::xbox360);
    expect(0x045e, 0x02a1, L"", Picture::xbox360);
    expect(0x045e, 0x0b12, L"Xbox Wireless Controller", Picture::xbox);
    expect(0x045e, 0x02ea, L"", Picture::xbox);
    expect(0x045e, 0x02ff, L"", Picture::xbox);
    expect(0x045e, 0x02e3, L"", Picture::xbox_elite);
    expect(0x045e, 0x0b00, L"Xbox Wireless Controller", Picture::xbox_elite);
    expect(0x045e, 0x0b22, L"", Picture::xbox_elite);
    expect(0x045e, 0x0b0a, L"", Picture::xbox_adaptive);
    expect(0x045e, 0x0b21, L"", Picture::xbox_adaptive);
    expect(0x057e, 0x2009, L"Pro Controller", Picture::switch_pro);
    expect(0x057e, 0x2069, L"", Picture::switch_pro);
    expect(0x057e, 0x2006, L"", Picture::joycon_left);
    expect(0x057e, 0x2007, L"", Picture::joycon_right);
    expect(0x057e, 0x200e, L"", Picture::joycon_pair);
    expect(0x057e, 0x2008, L"", Picture::joycon_pair);
    expect(0x28de, 0x1102, L"", Picture::steam_controller);
    expect(0x28de, 0x1142, L"", Picture::steam_controller);
    for (const auto product : {0x1302, 0x1303, 0x1304, 0x1305})
        expect(0x28de, static_cast<uint16_t>(product), L"Steam Controller", Picture::steam_controller2);
    expect(0x28de, 0x1205, L"", Picture::steam_deck);
    expect(0x28de, 0x11ff, L"Steam Virtual Gamepad", Picture::generic); // Steam Input: the real pad is unknown.
    expect(0x046d, 0xc216, L"Logitech Dual Action", Picture::logitech);
    expect(0x046d, 0xc21d, L"", Picture::logitech);
    expect(0x046d, 0xc219, L"", Picture::logitech);
    expect(0x046d, 0xc21f, L"", Picture::logitech);
    expect(0x2dc8, 0x6003, L"", Picture::eightbitdo_pro);
    expect(0x2dc8, 0x6001, L"", Picture::eightbitdo_pro);
    expect(0x2dc8, 0x6100, L"", Picture::eightbitdo_pro);
    expect(0x2dc8, 0x6012, L"", Picture::ultimate2);
    expect(0x2dc8, 0x310a, L"", Picture::eightbitdo_ultimate);
    expect(0x2dc8, 0x3106, L"", Picture::eightbitdo_ultimate);
    expect(0x2dc8, 0x9018, L"Wireless Gamepad", Picture::eightbitdo_zero2);
    expect(0x2dc8, 0x3230, L"Controller", Picture::eightbitdo_zero2);

    // Product-name fallbacks (Bluetooth, XInput and unknown PIDs).
    expect(0x054c, 0x1234, L"Wireless Controller", Picture::dualshock4);
    expect(0, 0, L"Wireless Controller", Picture::generic); // Not Sony: too common to guess.
    expect(0, 0, L"DUALSHOCK®4 USB Wireless Adaptor", Picture::dualshock4);
    expect(0, 0, L"DualSense Edge Wireless Controller", Picture::dualsense_edge);
    expect(0, 0, L"DualSense Wireless Controller", Picture::dualsense);
    expect(0, 0, L"Pro Controller", Picture::switch_pro);
    expect(0, 0, L"Nintendo Switch Pro Controller", Picture::switch_pro);
    expect(0x146b, 0x0d01, L"Revolution Pro Controller", Picture::generic); // Not a Switch pad.
    expect(0, 0, L"Joy-Con (L)", Picture::joycon_left);
    expect(0, 0, L"Joy-Con (R)", Picture::joycon_right);
    expect(0, 0, L"Joy-Con (L/R)", Picture::joycon_pair);
    expect(0, 0, L"Joy-Con", Picture::joycon_pair);
    expect(0, 0, L"Controller (XBOX 360 For Windows)", Picture::xbox360);
    expect(0, 0, L"Xbox 360 Wireless Receiver", Picture::xbox360);
    expect(0, 0, L"Xbox Elite Wireless Controller Series 2", Picture::xbox_elite);
    expect(0, 0, L"Xbox Adaptive Controller", Picture::xbox_adaptive);
    expect(0, 0, L"Xbox Wireless Controller", Picture::xbox);
    expect(0, 0, L"XINPUT compatible HID device", Picture::xbox);
    expect(0x045e, 0x0fff, L"", Picture::xbox); // Unknown Microsoft pad.
    expect(0, 0, L"Steam Deck", Picture::steam_deck);
    expect(0, 0, L"Steam Controller", Picture::steam_controller);
    expect(0, 0, L"Steam Controller (2nd generation)", Picture::steam_controller2);
    expect(0, 0, L"Steam Controller (2026)", Picture::steam_controller2);
    expect(0, 0, L"Logitech Gamepad F310", Picture::logitech);
    expect(0, 0, L"Wireless Gamepad F710", Picture::logitech);
    expect(0x2dc8, 0x9999, L"8BitDo Ultimate 2C Wireless Controller", Picture::eightbitdo_ultimate); // Not the Ultimate 2.
    expect(0x2dc8, 0x9999, L"8BitDo Ultimate 2 Wireless Controller", Picture::ultimate2);
    expect(0, 0, L"8BitDo Ultimate C 2.4G", Picture::eightbitdo_ultimate);
    expect(0, 0, L"8BitDo Pro 2", Picture::eightbitdo_pro);
    expect(0, 0, L"8Bitdo SN30 Pro+", Picture::eightbitdo_pro);
    expect(0, 0, L"8Bitdo SF30 Pro", Picture::eightbitdo_pro);
    expect(0, 0, L"8Bitdo SN30 GamePad", Picture::eightbitdo_sn);
    expect(0, 0, L"8BitDo Zero 2 gamepad", Picture::eightbitdo_zero2);
    expect(0x2dc8, 0x9999, L"Zero2", Picture::eightbitdo_zero2);
    expect(0, 0, L"8BitDo Zero", Picture::generic); // First generation is not a Zero 2.
    expect(0x2dc8, 0x9999, L"", Picture::generic);
    expect(0, 0, L"Ultimate 2 Wireless", Picture::ultimate2);
    expect(0, 0, L"USB Gamepad", Picture::generic);
    expect(0, 0, L"", Picture::generic);

    // The XInput HID shadow check covers every Xbox-style family, nothing else.
    for (int i = 0; i < static_cast<int>(Picture::count); ++i) {
        const auto picture = static_cast<Picture>(i);
        const bool xbox = picture == Picture::xbox || picture == Picture::xbox360 || picture == Picture::xbox_elite || picture == Picture::xbox_adaptive;
        if (devices::xboxFamily(picture) != xbox) { std::cerr << "xboxFamily mismatch for " << i << '\n'; return 2; }
    }
    if (failures) return 3;
    // A generic localized HID name is not the product name. Known IDs resolve it;
    // a real vendor name and unknown hardware remain intact.
    const auto name = devices::controllerDisplayName;
    if (name(0x2dc8, 0x9018, L"Kontroler HID") != L"8BitDo Zero 2") return 6;
    if (name(0x28de, 0x1304, L"Kontroler HID") != L"Steam Controller (2026)") return 5;
    if (name(0x045e, 0x0b13, L"Kontroler gier zgodny z HID") != L"Xbox Series X|S" ||
        name(0x045e, 0x0b12, L"HID-compliant game controller") != L"Xbox Series X|S" ||
        name(0x045e, 0x0b20, L"Controller") != L"Xbox One S" ||
        name(0x045e, 0x0b22, L"") != L"Xbox Elite Series 2" ||
        name(0x054c, 0x0ce6, L"Wireless Controller") != L"DualSense" ||
        name(0x046d, 0xc21f, L"USB Gamepad") != L"Logitech F710" ||
        name(0x2dc8, 0x6003, L"HID Gamepad") != L"8BitDo Pro 2" ||
        name(0, 0, L"Kontroler gier zgodny z HID") != L"Kontroler gier zgodny z HID" ||
        name(0x045e, 0x028e, L"8BitDo Ultimate 2 Wireless") != L"8BitDo Ultimate 2 Wireless") return 4;
    // Two Switch-mode devices share IDs/names. A user override must affect only
    // the exact selected device and preserve its connection/battery/identity.
    std::vector<devices::Controller> pads{
        {LR"(\\?\HID#pad-one)", L"Wireless Gamepad", L"HID", Picture::switch_pro, 65},
        {LR"(\\?\HID#pad-two)", L"Pro Controller", L"HID", Picture::switch_pro}};
    applyControllerOverrides(pads, R"({"\\\\?\\hid#pad-one":"8BitDo Zero 2"})");
    if (pads[0].name != L"8BitDo Zero 2" || pads[0].picture != Picture::eightbitdo_zero2 ||
        pads[0].id != LR"(\\?\HID#pad-one)" || pads[0].connection != L"HID" || pads[0].battery != 65 ||
        pads[1].name != L"Pro Controller" || pads[1].picture != Picture::switch_pro) return 7;
    for (auto text : {R"({"\\\\?\\HID#pad":"8BitDo Zero 2"})", R"({"":"8BitDo Zero 2"})",
                      R"({"\\\\?\\HID#pad-two":21})", R"({"\\\\?\\HID#pad-two":"Unknown gamepad"})", "{"})
        applyControllerOverrides(pads, text);
    if (pads[1].picture != Picture::switch_pro || pads[1].name != L"Pro Controller") return 8;
    std::cout << std::size(devices::controller_models) << " USB IDs, name fallbacks and per-device overrides classified\n";
}
