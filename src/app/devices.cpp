#include "launcher.hpp"
#include "../model/controller_pictures.hpp"
#include <cmath>
#include <winrt/base.h>

// Census is independent of the active input pad and the Controllers panel: idle
// pads present at startup count too. WGI/HID enumeration stays off the UI thread.
void Launcher::initializeControllerMonitor() {
    const auto alive = alive_;
    const auto refresh = controller_rescan_;
    const auto hwnd = hwnd_;
    work_.start([alive, refresh, hwnd, read = read_controllers_] {
        winrt::init_apartment();
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        std::vector<devices::Controller> previous;
        bool first = true;
        int followups = 0;
        while (alive->load()) {
            refresh->take();
            auto controllers = read();
            if (!alive->load()) return;
            const auto same = [](const auto& a, const auto& b) {
                return a.id == b.id && a.name == b.name && a.picture == b.picture &&
                    a.battery == b.battery && a.charging == b.charging && a.approximate == b.approximate;
            };
            if (first || !std::equal(previous.begin(), previous.end(), controllers.begin(), controllers.end(), same)) {
                auto message = std::make_unique<ControllersMessage>();
                message->controllers = controllers;
                if (!PostMessageW(hwnd, controllers_message, 0, reinterpret_cast<LPARAM>(message.get()))) return;
                message.release(); previous = std::move(controllers); first = false;
            }
            // No census on a timer while nothing changes: Raw Input's arrival/removal and Windows'
            // device notifications wake it at once. Windows.Gaming.Input can list a new pad a moment
            // after it arrived, so a change is looked at again after 1 and 3 s; the batteries of
            // connected pads are read again every 30 s.
            const DWORD timeout = followups == 2 ? 1000 : followups == 1 ? 2000 : previous.empty() ? INFINITE : 30000;
            const DWORD waited = WaitForSingleObject(refresh->event, timeout);
            if (!alive->load()) return;
            if (waited == WAIT_OBJECT_0) followups = 2;
            else if (followups > 0) --followups;
        }
    });
}

void Launcher::requestControllerPicture(devices::Picture picture) {
    if (!runtime_started_ || controller_images_.contains(static_cast<int>(picture)) ||
        !devices::controllerPhoto(picture) ||
        !controller_image_requests_.insert(static_cast<int>(picture)).second) return;
    const auto alive = alive_;
    const auto hwnd = hwnd_;
    if (!work_.start([alive, hwnd, picture] {
        winrt::init_apartment();
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        auto message = std::make_unique<ControllersMessage>();
        message->image = picture;
        message->bytes = controllerPicture(picture, userConfigDirectory() / L"controllers");
        if (!alive->load()) return;
        if (PostMessageW(hwnd, controllers_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) controller_image_requests_.erase(static_cast<int>(picture));
}

void Launcher::onControllers(std::unique_ptr<ControllersMessage> message) {
    if (message->image != devices::Picture::none) {
        const int picture = static_cast<int>(message->image);
        controller_image_requests_.erase(picture);
        if (!message->bytes.empty() && controller_images_.insert(picture).second) {
            renderer_.setPlatformBadge(L"controller:" + std::to_wstring(picture), std::move(message->bytes));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return; // Late image completion must not restore an old controller list or emit a toast.
    }
    const auto contains = [](const auto& pads, const auto& id) {
        return std::any_of(pads.begin(), pads.end(), [&](const auto& pad) { return pad.id == id; });
    };
    controller_input_.enrich(message->controllers);
    const auto notify = [&](const devices::Controller& pad, bool connected) {
        showNotification(NotificationKind::controller, connected ? tr(L"Kontroler połączony") : tr(L"Kontroler rozłączony"), pad.name);
        auto& notification = notifications_.back();
        notification.connected = connected;
        notification.controller_picture = pad.picture;
        notification.device_id = pad.id;
        notification.battery = connected ? pad.battery : std::nullopt;
    };
    for (const auto& pad : connected_pads_)
        if (!contains(message->controllers, pad.id)) notify(pad, false);
    for (const auto& pad : message->controllers) {
        if (!contains(connected_pads_, pad.id)) notify(pad, true);
        for (auto& notice : notifications_)
            if (notice.kind == NotificationKind::controller && notice.connected && notice.device_id == pad.id)
                notice.battery = pad.battery;
        requestControllerPicture(pad.picture);
    }
    connected_pads_ = std::move(message->controllers);
    onControllerCount(connected_pads_.size());
}

void Launcher::onControllerCount(size_t count) {
    if (count >= 2 && connected_controllers_ < 2) couch_category_pending_ = true;
    if (count < 2) couch_category_pending_ = false;
    connected_controllers_ = count;
    focusCouchCategory();
}

void Launcher::focusCouchCategory() {
    if (!couch_category_pending_ || scanning_ || library_transition_ || categories_.empty()) return;
    // Complete an in-progress edit or launch first, then handle the pending connection.
    if (pending_launch_ || exit_prompt_ || power_menu_ || gogInstallOpen() || customEditorOpen() || identifyOpen() ||
        settingsOpen() || artworkOpen() || cropOpen() || screensaverActive() || gameOverlayOpen()) return;
    const auto matches = [](std::wstring_view name) {
        const auto first = name.find_first_not_of(L" \t\r\n");
        if (first == name.npos) return false;
        name = name.substr(first, name.find_last_not_of(L" \t\r\n") - first + 1);
        return CompareStringOrdinal(name.data(), static_cast<int>(name.size()), L"couch", -1, TRUE) == CSTR_EQUAL ||
               CompareStringOrdinal(name.data(), static_cast<int>(name.size()), L"kanapa", -1, TRUE) == CSTR_EQUAL;
    };
    const auto found = std::find_if(categories_.begin(), categories_.end(), [&](const Category& category) {
        return !category.games.empty() && matches(category.title);
    });
    couch_category_pending_ = false; // One jump per transition to two pads, never pin the user to the shelf.
    if (found == categories_.end()) return;
    const size_t category = static_cast<size_t>(found - categories_.begin());
    noteUserInput(); // The couch choice takes precedence over a still-pending startup bookmark.
    cancelEnterHold(); cancelTabHold();
    dismissMenu(); closeQuick(); closeCategoryMenu(); closeFriends();
    menu_progress_ = quick_progress_ = category_menu_progress_ = friends_progress_ = 0;
    // Reuse normal closing paths for library controls, nested views and device services.
    for (int i = 0; i < 8 && screen_ != ScreenMode::shelves; ++i) back();
    if (screen_ != ScreenMode::shelves) { couch_category_pending_ = true; return; }
    if (xmbActive()) {
        const auto tile = std::find_if(xmb_top_.begin(), xmb_top_.end(), [&](const Game& game) {
            return game.kind == GameKind::category && game.app_id == category;
        });
        if (tile == xmb_top_.end()) return;
        xmbFocus(static_cast<size_t>(tile - xmb_top_.begin()), -1);
    } else if (flixActive()) {
        const auto row = std::find_if(flix_rows_.begin(), flix_rows_.end(), [&](const FlixRow& item) { return item.category == category; });
        if (row == flix_rows_.end()) return;
        flixFocus(static_cast<size_t>(row - flix_rows_.begin()), categories_[category].selected);
    } else if (metroActive()) {
        const auto page = std::find_if(metro_pages_.begin(), metro_pages_.end(), [&](const MetroPage& item) { return item.category == category; });
        if (page == metro_pages_.end()) return;
        metroFocus(page->begin + std::min(categories_[category].selected, page->count - 1));
    } else showCategory(category);
    audio_.navigate();
}

void Launcher::openDevices(devices::Panel panel) {
    pointerUp();
    desktopLeaveLibrary(); rememberPlace();
    // Bluetooth from a device panel returns to it; opened from elsewhere (Settings) Back leaves it.
    if (!devicesOpen()) { device_return_ = screen_; bluetooth_from_panel_ = false; }
    else if (panel == devices::Panel::bluetooth) { bluetooth_return_ = device_state_.panel; bluetooth_from_panel_ = true; }
    closeQuick(); dismissMenu(); closeFriends(); closeCategoryMenu(); cancelEnterHold();
    quick_progress_ = menu_progress_ = friends_progress_ = category_menu_progress_ = 0;
    screen_ = ScreenMode::devices;
    beginScreenTransition();
    device_state_ = {}; device_state_.panel = panel; device_state_.rebuild();
    if (!device_service_ && runtime_started_) device_service_ = windowsDeviceService();
    if (device_service_) device_service_->open(panel);
    next_panel_poll_ = 0;
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::pollDevices() {
    if (!devicesOpen() || !device_service_ || GetTickCount64() < next_panel_poll_) return;
    next_panel_poll_ = GetTickCount64() + 120;
    auto snapshot = device_service_->snapshot();
    if (device_state_.panel == devices::Panel::audio && snapshot.revision && snapshot.revision == device_state_.data.revision) return;
    controller_input_.enrich(snapshot.controllers);
    for (int column = 0; column < 2; ++column) {
        auto& incoming = snapshot.audio[column];
        const auto& previous = device_state_.data.audio[column];
        if (column == 0 && incoming.endpoint != previous.endpoint) volume_monitor_.refresh();
        if (column == 1 && device_state_.pending_audio[column] && incoming.acknowledged >= device_state_.pending_audio[column] && incoming.available)
            showVolume(incoming.volume, incoming.muted, true);
        if (device_state_.pending_audio[column] > incoming.acknowledged && incoming.available && incoming.endpoint == previous.endpoint) {
            incoming.volume = previous.volume; incoming.muted = previous.muted;
        } else device_state_.pending_audio[column] = 0;
        if ((audio_drag_column_ == column || device_state_.audio_edit_column == column) &&
            (!incoming.available || incoming.endpoint != previous.endpoint)) endAudioEditing();
    }
    device_state_.data = std::move(snapshot);
    for (const auto& [kind, bytes] : device_state_.data.images) {
        if (!controller_images_.insert(static_cast<int>(kind)).second) continue;
        renderer_.setPlatformBadge(L"controller:" + std::to_wstring(static_cast<int>(kind)), bytes);
    }
    device_state_.rebuild();
    const auto view = renderer_.viewport();
    device_ui::Layout{view.width, view.height, device_state_.panel}.ensure(device_state_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::deviceMove(int amount) {
    endAudioEditing();
    const int before = device_state_.selected;
    device_state_.fresh = false;
    if (device_state_.panel == devices::Panel::audio) device_state_.moveAudio(amount);
    else if (device_state_.gallery()) {
        // Gallery: Up to Back, Down from Back to the first pad; Left/Right move between tiles.
        if (amount < 0) device_state_.selected = -1;
        else if (device_state_.selected < 0) device_state_.selected = device_state_.rowOf(0);
    }
    else device_state_.selected = std::clamp(device_state_.selected + amount, -1, static_cast<int>(device_state_.rows.size()) - 1);
    if (before == device_state_.selected) { navigationBoundary(); return; }
    const auto view = renderer_.viewport();
    device_ui::Layout{view.width, view.height, device_state_.panel}.ensure(device_state_);
    audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::deviceAdjust(int amount) {
    if (device_state_.gallery() && amount) {
        device_state_.fresh = false;
        if (device_state_.selected < 0) { navigationBoundary(); return; }
        const int next = device_state_.tileOf(device_state_.selected) + (amount > 0 ? 1 : -1);
        if (next < 0 || next >= static_cast<int>(device_state_.rows.size())) { navigationBoundary(); return; }
        device_state_.selected = device_state_.rowOf(next);
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (device_state_.panel != devices::Panel::audio) return;
    const int selected = device_state_.selected;
    if (amount && selected >= 0 && selected < static_cast<int>(device_state_.rows.size()) &&
        device_state_.rows[selected].kind == device_ui::Kind::volume) {
        // As the PS5 control centre: Left/Right on the focused slider is the volume itself, 1% a
        // step; held, the pad repeats it every frame or two (input.cpp) for a smooth glide.
        // The columns are switched by LB/RB, Tab, or Left/Right from the mute button and the list.
        const bool input = device_state_.rows[selected].input;
        auto& channel = device_state_.data.audio[input];
        if (device_service_ && channel.available && !device_state_.data.busy) {
            device_state_.audio_edit_column = input ? 1 : 0;
            const float before = channel.volume;
            deviceSetVolume(input, before + (amount > 0 ? 0.01f : -0.01f));
            if (std::abs(channel.volume - before) < 0.001f) navigationBoundary(); // 0% or 100%.
            return;
        }
    }
    deviceColumn(amount);
}
void Launcher::deviceColumn(int direction) {
    if (!devicesOpen() || device_state_.panel != devices::Panel::audio) return;
    endAudioEditing();
    device_state_.switchAudioColumn(direction);
    const auto view = renderer_.viewport(); device_ui::AudioLayout{view.width, view.height}.ensure(device_state_);
    audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::endAudioEditing() {
    if (device_state_.audio_edit_column < 0 && audio_drag_column_ < 0) return;
    device_state_.audio_edit_column = -1;
    pointerUp();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::deviceSetVolume(bool input, float value) {
    if (!std::isfinite(value) || device_state_.audio_edit_column != (input ? 1 : 0)) return;
    auto& channel = device_state_.data.audio[input];
    if (!device_service_ || !channel.available || device_state_.data.busy) return;
    value = std::round(std::clamp(value, 0.0f, 1.0f) * 100) / 100;
    if (std::abs(channel.volume - value) < 0.005f) return;
    const auto sequence = ++audio_command_sequence_;
    device_state_.pending_audio[input] = sequence;
    channel.volume = value;
    device_service_->submit({devices::Action::volume, channel.endpoint, value, input, sequence});
    next_panel_poll_ = 0; InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::deviceDrag(float x, float) {
    if (!devicesOpen() || audio_drag_column_ < 0) return;
    const auto view = renderer_.viewport(); const device_ui::AudioLayout layout{view.width, view.height};
    const auto track = layout.track(audio_drag_column_);
    deviceSetVolume(audio_drag_column_ != 0, (x - track.left) / (track.right - track.left));
}
void Launcher::deviceWheel(float mouse_x, int amount) {
    if (device_state_.panel == devices::Panel::audio) {
        const auto view = renderer_.viewport();
        const int column = mouse_x / view.scale >= view.width * 0.5f ? 1 : 0;
        if (device_state_.audio_column != column) deviceColumn(column ? 1 : -1);
    }
    if (device_state_.gallery()) { deviceAdjust(amount); return; } // The wheel slides the gallery.
    deviceMove(amount);
}
void Launcher::showVolume(float level, bool muted, bool input) {
    const float age = volume_notice_ ? std::min(volume_notice_->age, 0.16f) : 0;
    volume_notice_ = VolumeNotice{std::clamp(level, 0.0f, 1.0f), muted, input, age};
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::deviceAccept() {
    if (device_state_.selected == -1) { back(); return; }
    if (device_state_.selected < 0 || device_state_.selected >= static_cast<int>(device_state_.rows.size())) return;
    const auto row = device_state_.rows[device_state_.selected];
    using Kind = device_ui::Kind;
    const auto kind = row.action.value_or(row.kind);
    if (kind == Kind::volume) {
        if (device_state_.audio_edit_column >= 0) endAudioEditing();
        else if (!device_state_.data.busy && device_state_.data.audio[row.input].available) device_state_.audio_edit_column = row.input ? 1 : 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (kind == Kind::cancel) { if (device_service_) device_service_->submit({devices::Action::cancel}); return; }
    if (device_state_.data.busy) return;
    if (kind == Kind::bluetooth) { openDevices(devices::Panel::bluetooth); return; }
    if (kind == Kind::windows_bluetooth || kind == Kind::windows_audio) {
        const auto result = execute_(hwnd_, GameAction{kind == Kind::windows_audio ? L"ms-settings:sound" : L"ms-settings:bluetooth"});
        if (result <= 32) device_state_.data.status = tr(L"Nie udało się otworzyć ustawień Windows.");
    } else if (device_service_) {
        if (kind == Kind::mute && device_state_.data.audio[row.input].available) {
            auto& channel = device_state_.data.audio[row.input];
            const auto sequence = ++audio_command_sequence_; device_state_.pending_audio[row.input] = sequence;
            channel.muted = !channel.muted;
            device_service_->submit({devices::Action::mute, channel.endpoint, channel.muted ? 1.0f : 0.0f, row.input, sequence});
        }
        else if (kind == Kind::endpoint && !row.checked) device_service_->submit({devices::Action::endpoint, row.id, 0, row.input});
        else if (kind == Kind::pair && !row.checked) device_service_->submit({devices::Action::pair, row.id});
        else if (kind == Kind::connect || kind == Kind::disconnect)
            device_service_->submit({kind == Kind::connect ? devices::Action::connect : devices::Action::disconnect, row.target});
    }
    next_panel_poll_ = 0; InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::deviceClick(float x, float y) {
    const auto view = renderer_.viewport();
    const device_ui::Layout layout{view.width, view.height, device_state_.panel};
    if (layout.back().contains(x, y)) { back(); return; }
    if (device_state_.gallery()) {
        // A tile selects its pad; its button (or the "Sparuj" tile) acts.
        const int index = layout.hitGallery(device_state_, x, y);
        if (index < 0) return;
        const int which = device_state_.tileOf(index), focus = layout.focusTile(device_state_);
        const auto tile = layout.tile(which, static_cast<int>(device_state_.rows.size()), focus);
        device_state_.selected = index; device_state_.fresh = false;
        const auto& row = device_state_.rows[index];
        if (row.kind != device_ui::Kind::controller || (row.action && device_ui::Layout::tileAction(tile).contains(x, y))) deviceAccept();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const device_ui::AudioLayout audio_layout{view.width, view.height};
    const int index = device_state_.panel == devices::Panel::audio ? audio_layout.hit(device_state_, x, y) : layout.hit(x, y, device_state_.rows.size(), device_state_.scroll);
    if (index < 0) return;
    endAudioEditing();
    device_state_.selected = index;
    if (device_state_.panel == devices::Panel::audio && !device_state_.audioFooter(index)) device_state_.audio_column = device_state_.rows[index].input ? 1 : 0;
    if (device_state_.rows[index].kind == device_ui::Kind::controller) {
        // Clicking the name just previews/selects the pad. Only the compact button acts.
        if (device_state_.rows[index].action && layout.controllerAction(index, device_state_.scroll).contains(x, y)) deviceAccept();
    } else if (device_state_.rows[index].kind == device_ui::Kind::volume) {
        const int column = device_state_.rows[index].input ? 1 : 0;
        if (device_state_.data.audio[column].available && !device_state_.data.busy && audio_layout.track(column).contains(x, y)) {
            device_state_.audio_edit_column = column;
            audio_drag_column_ = column; SetCapture(hwnd_); deviceDrag(x, y);
        }
    } else deviceAccept();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
