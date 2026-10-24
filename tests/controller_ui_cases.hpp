// Included after LauncherUiTest: the controllers panel with one fake device per
// family. No downloaded art is injected, so every preview is the drawn picture
// (also the offline fallback for DualSense / Xbox / Ultimate 2). Always painted:
// into LAUNCHME_UI_CAPTURE_DIR when set, otherwise into the temporary config folder.
#include "../src/model/controller_models.hpp"

int LauncherUiTest::controllerFamilies(HWND window) {
    struct Family { uint16_t vendor, product; const wchar_t* name; devices::Picture picture; const wchar_t* file; };
    using devices::Picture;
    const Family families[] = {
        {0x054c, 0x0ce6, L"DualSense Wireless Controller", Picture::dualsense, L"dualsense"},
        {0x054c, 0x0df2, L"DualSense Edge Wireless Controller", Picture::dualsense_edge, L"dualsense-edge"},
        {0x054c, 0x09cc, L"Wireless Controller", Picture::dualshock4, L"dualshock4"},
        {0x045e, 0x0b12, L"Xbox Wireless Controller", Picture::xbox, L"xbox"},
        {0x045e, 0x0b00, L"Xbox Elite Wireless Controller Series 2", Picture::xbox_elite, L"xbox-elite"},
        {0x045e, 0x028e, L"Controller (XBOX 360 For Windows)", Picture::xbox360, L"xbox-360"},
        {0x045e, 0x0b0a, L"Xbox Adaptive Controller", Picture::xbox_adaptive, L"xbox-adaptive"},
        {0x057e, 0x2009, L"Pro Controller", Picture::switch_pro, L"switch-pro"},
        {0x057e, 0x2006, L"Joy-Con (L)", Picture::joycon_left, L"joycon-left"},
        {0x057e, 0x2007, L"Joy-Con (R)", Picture::joycon_right, L"joycon-right"},
        {0x057e, 0x200e, L"Joy-Con (L/R)", Picture::joycon_pair, L"joycon-pair"},
        {0x2dc8, 0x6012, L"8BitDo Ultimate 2 Wireless Controller", Picture::ultimate2, L"8bitdo-ultimate2"},
        {0x2dc8, 0x310a, L"8BitDo Ultimate 2C Wireless Controller", Picture::eightbitdo_ultimate, L"8bitdo-ultimate-c"},
        {0x2dc8, 0x6003, L"8BitDo Pro 2", Picture::eightbitdo_pro, L"8bitdo-pro2"},
        {0x0000, 0x0000, L"8Bitdo SN30 GamePad", Picture::eightbitdo_sn, L"8bitdo-sn30"},
        {0x2dc8, 0x9018, L"8BitDo Zero 2", Picture::eightbitdo_zero2, L"8bitdo-zero2"},
        {0x28de, 0x1142, L"Steam Controller", Picture::steam_controller, L"steam-controller"},
        {0x28de, 0x1304, L"Steam Controller (2026)", Picture::steam_controller2, L"steam-controller2"},
        {0x28de, 0x1205, L"Steam Deck", Picture::steam_deck, L"steam-deck"},
        {0x046d, 0xc21f, L"Wireless Gamepad F710", Picture::logitech, L"logitech-f710"},
        {0x0079, 0x0006, L"USB Gamepad", Picture::generic, L"generic"},
    };
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 800;
    const auto capture = [&](const std::wstring& name, const std::function<void()>& draw, bool clear_notice = true) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        const auto target = count && count < 4096 ? std::filesystem::path(folder) : userConfigDirectory();
        if (clear_notice) app.notifications_.clear();
        return app.renderer_.captureForTest(target / name, draw);
    };
    auto fake = std::make_unique<FakeDevices>();
    auto* backend = fake.get();
    for (size_t i = 0; i < std::size(families); ++i) {
        const auto& family = families[i];
        // The same classification the Windows enumeration uses.
        const auto picture = devices::classifyController(family.vendor, family.product, family.name);
        if (picture != family.picture) return 801;
        backend->data.controllers.push_back({L"pad" + std::to_wstring(i), family.name, L"USB", picture});
    }
    backend->data.controllers.push_back({L"unclassified", L"Kontroler HID", L"HID", Picture::none}); // Default value draws as generic.
    app.device_service_ = std::move(fake);
    app.openDevices(devices::Panel::controllers); app.pollDevices();
    if (app.device_state_.rows.size() != std::size(families) + 2 || !app.device_state_.data.images.empty()) return 802;
    // The gallery opens on the first pad; Right walks through every family.
    for (size_t i = 0; i <= std::size(families); ++i) {
        if (i) app.deviceAdjust(1);
        if (app.device_state_.selected != static_cast<int>(i) + 1) return 803;
        const std::wstring file = i < std::size(families) ? families[i].file : L"unclassified";
        if (!capture(L"controllers-family-" + file + L".png", [&] { app.paint(); })) return 804;
    }
    bool gallery = false;
    if (!capture(L"controllers-family-gallery.png", [&] { gallery = app.renderer_.paintControllerGalleryForTest(); }) || !gallery) return 805;
    // Photo completion uses the same message route as startup/hotplug, even before
    // visiting the devices panel. It must not replace the connected-device census.
    std::vector<uint8_t> photo = pngFixture(75, 120, 88);
    wchar_t image_file[4096]{};
    const auto image_length = GetEnvironmentVariableW(L"LAUNCHME_UI_STEAM_CONTROLLER_IMAGE", image_file, 4096);
    if (image_length && image_length < 4096) {
        std::ifstream input(image_file, std::ios::binary);
        photo.assign(std::istreambuf_iterator<char>{input}, {});
        if (photo.empty()) return 806;
    }
    const devices::Controller steam{L"steam2-fixture", L"Steam Controller (2026)", L"USB / Puck", Picture::steam_controller2};
    auto connected = std::make_unique<ControllersMessage>(); connected->controllers = {steam};
    app.onControllers(std::move(connected));
    auto image = std::make_unique<ControllersMessage>(); image->image = Picture::steam_controller2; image->bytes = std::move(photo);
    app.onControllers(std::move(image));
    if (app.connected_controllers_ != 1 || app.notifications_.size() != 1 ||
        !app.controller_images_.contains(static_cast<int>(Picture::steam_controller2))) return 807;
    backend->data.controllers = {steam}; app.next_panel_poll_ = 0; app.pollDevices();
    app.device_state_.selected = 1; app.device_state_.scroll = 0;
    app.notifications_.front().age = 1;
    if (!capture(L"controllers-steam-controller2-photo-toast.png", [&] { app.paint(); }, false)) return 808;
    const auto zero_length = GetEnvironmentVariableW(L"LAUNCHME_UI_ZERO2_IMAGE", image_file, 4096);
    if (zero_length && zero_length < 4096) {
        std::ifstream input(image_file, std::ios::binary);
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
        if (bytes.empty()) return 809;
        const devices::Controller zero{L"zero2-fixture", L"8BitDo Zero 2", L"Bluetooth", Picture::eightbitdo_zero2};
        app.connected_pads_.clear(); app.notifications_.clear();
        auto connected_zero = std::make_unique<ControllersMessage>(); connected_zero->controllers = {zero};
        app.onControllers(std::move(connected_zero));
        auto zero_image = std::make_unique<ControllersMessage>(); zero_image->image = zero.picture; zero_image->bytes = std::move(bytes);
        app.onControllers(std::move(zero_image));
        backend->data.controllers = {zero}; app.next_panel_poll_ = 0; app.pollDevices();
        app.device_state_.selected = 1; app.device_state_.scroll = 0;
        app.notifications_.front().age = 1;
        if (!capture(L"controllers-8bitdo-zero2-photo-toast.png", [&] { app.paint(); }, false)) return 810;
    }
    // Explicit connection actions, stable focus after a HID device disappears,
    // BLE fallback and pairing all pass through the real UI with a fake backend.
    devices::Controller zero{L"hid-zero", L"8BitDo Zero 2", L"Bluetooth", Picture::eightbitdo_zero2};
    zero.bluetooth_id = L"bluetooth:001122334455";
    backend->data.controllers = {zero,
        {L"ble-xbox", L"Xbox Series X|S", L"Bluetooth LE", Picture::xbox},
        {L"usb-pad", L"USB pad", L"USB", Picture::generic}};
    app.next_panel_poll_ = 0; app.pollDevices();
    if (app.device_state_.rows.size() != 4 || app.device_state_.rows[1].action != device_ui::Kind::disconnect ||
        app.device_state_.rows[2].action != device_ui::Kind::windows_bluetooth || app.device_state_.rows[3].action) return 811;
    app.device_state_.selected = 1;
    const auto commands = backend->actions.size();
    const auto viewport = app.renderer_.viewport();
    const device_ui::Layout connection_layout{viewport.width, viewport.height, devices::Panel::controllers};
    const int tiles = static_cast<int>(app.device_state_.rows.size());
    const auto controller_tile = connection_layout.tile(app.device_state_.tileOf(1), tiles, connection_layout.focusTile(app.device_state_));
    app.deviceClick(controller_tile.left + 24, controller_tile.top + 40); // The picture selects; it never disconnects.
    if (backend->actions.size() != commands) return 812;
    const auto connection_button = device_ui::Layout::tileAction(controller_tile);
    app.deviceClick(connection_button.left + 20, connection_button.top + 20);
    if (backend->actions.size() != commands + 1 || backend->actions.back().action != devices::Action::disconnect ||
        backend->actions.back().id != zero.bluetooth_id || app.device_state_.selectedController()->id != zero.id) return 813;
    backend->data.busy = true; app.next_panel_poll_ = 0; app.pollDevices(); app.deviceAccept();
    if (backend->actions.size() != commands + 1) return 814;
    backend->data.busy = false;
    backend->data.controllers[0].id = zero.bluetooth_id;
    backend->data.controllers[0].connected = false;
    app.next_panel_poll_ = 0; app.pollDevices();
    if (app.device_state_.selected != 1 || app.device_state_.rows[1].action != device_ui::Kind::connect ||
        app.device_state_.selectedController()->connected) return 815;
    app.deviceAccept(); // Enter/A on the focused pad activates its inline button too.
    if (backend->actions.back().action != devices::Action::connect || backend->actions.back().id != zero.bluetooth_id) return 816;
    app.deviceAdjust(1); app.deviceAccept();
    if (dispatched.empty() || dispatched.back() != L"ms-settings:bluetooth") return 817;
    if (!capture(L"controllers-bluetooth-actions.png", [&] { app.paint(); })) return 818;
    app.device_state_.selected = 0; app.deviceAccept();
    if (app.device_state_.panel != devices::Panel::bluetooth) return 819;
    return 0;
}
