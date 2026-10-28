#include "device_service.hpp"
#include "bluetooth_controllers.hpp"
#include "bluez.hpp"
#include "system_audio.hpp"
#include "user_config.hpp"
#include "text_encoding.hpp"
#include "../i18n/i18n.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// BlueZ for the Bluetooth panel and pairing; controllers from bluetooth_controllers_linux; audio
// endpoints from SystemAudio (PulseAudio/PipeWire); vendor photos from device_service.cpp.
namespace {
struct Shared {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<devices::Command> commands;
    devices::Snapshot data;
    devices::Panel panel = devices::Panel::controllers;
    bool visible = false;
    std::atomic_bool stop{false}, cancel{false};
};
bool soundPanel(devices::Panel panel) { return panel == devices::Panel::sound; }
bool audioPanel(devices::Panel panel) { return panel == devices::Panel::audio || soundPanel(panel); }
bool bluetoothPanel(devices::Panel panel) { return panel == devices::Panel::bluetooth || soundPanel(panel); }
std::wstring pairingStatus(const std::string& error) {
    if (error.empty()) return tr(L"Sparowano.");
    if (error == "cancelled" || error == "org.bluez.Error.AuthenticationCanceled") return tr(L"Anulowano parowanie.");
    if (error == "org.bluez.Error.AlreadyExists") return tr(L"Urządzenie jest już sparowane.");
    if (error == "org.bluez.Error.AuthenticationTimeout" || error == "org.freedesktop.DBus.Error.NoReply")
        return tr(L"Upłynął czas parowania. Włącz tryb parowania urządzenia.");
    if (error == "org.bluez.Error.NotReady" || error == "org.bluez.Error.ConnectionAttemptFailed")
        return tr(L"Urządzenie nie jest gotowe do parowania.");
    return tr(L"Parowanie nie powiodło się (") + text_encoding::wideLenient(bluez::shortError(error)) + L").";
}
// Consent is the user's activation of this device's Pair row. Without a registered agent BlueZ
// pairs only "just works" devices (most gamepads, headphones); PIN ceremonies use the desktop's agent.
std::wstring pairDevice(bluez::Session& session, const std::wstring& id, const std::shared_ptr<Shared>& state) {
    const auto path = text_encoding::utf8(id);
    const auto objects = session.objects();
    const auto device = std::find_if(objects.devices.begin(), objects.devices.end(), [&](const auto& item) { return item.path == path; });
    if (device == objects.devices.end()) return pairingStatus("org.bluez.Error.DoesNotExist");
    if (device->paired) return tr(L"Urządzenie jest już sparowane.");
    const auto cancelled = [&] { return state->stop || state->cancel; };
    const auto error = session.call(path, "org.bluez.Device1", "Pair", cancelled, 60000);
    if (error == "cancelled") session.call(path, "org.bluez.Device1", "CancelPairing", [] { return false; }, 5000);
    return pairingStatus(error);
}
std::vector<devices::BluetoothDevice> bluetoothDevices(const bluez::Objects& objects) {
    std::vector<devices::BluetoothDevice> result;
    for (const auto& device : objects.devices) {
        if (device.name.empty()) continue; // As on Windows: nameless advertisements are not listed.
        result.push_back({text_encoding::wideLenient(device.path), text_encoding::wideLenient(device.name),
                          device.paired || device.bonded, device.connected, bluez::audio(device)});
    }
    return result;
}

class LinuxDevices final : public DeviceService {
    std::shared_ptr<Shared> state_ = std::make_shared<Shared>();
    std::thread worker_;
public:
    LinuxDevices() : worker_([state = state_] { run(state); }) {}
    ~LinuxDevices() override {
        state_->stop = true; state_->cancel = true; state_->wake.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    void open(devices::Panel panel) override {
        { std::lock_guard lock(state_->mutex); state_->panel = panel; state_->visible = true; state_->data.status.clear(); ++state_->data.revision; state_->commands.push_back({}); }
        state_->wake.notify_all();
    }
    void close() override {
        { std::lock_guard lock(state_->mutex); state_->visible = false; state_->commands.clear(); }
        state_->cancel = true; state_->wake.notify_all();
    }
    void submit(devices::Command command) override {
        if (command.action == devices::Action::cancel) { state_->cancel = true; return; }
        std::lock_guard lock(state_->mutex);
        if (state_->data.busy && command.action != devices::Action::volume && command.action != devices::Action::mute) return;
        if (command.action == devices::Action::pair || command.action == devices::Action::connect || command.action == devices::Action::disconnect) state_->cancel = false;
        if (command.action == devices::Action::pair || command.action == devices::Action::endpoint ||
            command.action == devices::Action::connect || command.action == devices::Action::disconnect) state_->data.busy = true;
        ++state_->data.revision;
        devices::queueCommand(state_->commands, std::move(command)); state_->wake.notify_one();
    }
    devices::Snapshot snapshot() override {
        std::lock_guard lock(state_->mutex);
        auto images = std::move(state_->data.images);
        auto result = state_->data; result.images = std::move(images); return result;
    }
    static void run(const std::shared_ptr<Shared>& state) {
        bluez::Session session; // Holds the discovery: BlueZ ends it when this client leaves.
        SystemAudio audio;
        std::vector<std::string> discovering;
        std::vector<devices::Picture> requested;
        uint64_t last_volume_write = 0;
        const auto stopDiscovery = [&] { for (const auto& adapter : discovering) session.stopDiscovery(adapter); discovering.clear(); };
        while (!state->stop) {
            devices::Command command;
            devices::Panel panel;
            bool visible;
            {
                std::unique_lock lock(state->mutex);
                state->wake.wait_for(lock, std::chrono::seconds(2), [&] { return state->stop || !state->commands.empty(); });
                if (state->stop) break;
                // A dragged volume slider sends a burst: at most one write every 33 ms.
                if (!state->commands.empty() && state->commands.front().action == devices::Action::volume) {
                    const auto now = GetTickCount64();
                    if (now < last_volume_write + 33)
                        state->wake.wait_for(lock, std::chrono::milliseconds(last_volume_write + 33 - now), [&] { return state->stop.load(); });
                    if (state->stop) break;
                }
                visible = state->visible; panel = state->panel;
                if (!state->commands.empty()) { command = std::move(state->commands.front()); state->commands.pop_front(); }
            }
            if ((!visible || !bluetoothPanel(panel)) && !discovering.empty()) stopDiscovery();
            if (!visible && command.action != devices::Action::volume && command.action != devices::Action::mute) continue;
            if (command.action == devices::Action::volume) last_volume_write = GetTickCount64();
            std::wstring status;
            if (command.action == devices::Action::pair) {
                { std::lock_guard lock(state->mutex); state->data.status = tr(L"Parowanie…"); ++state->data.revision; }
                status = pairDevice(session, command.id, state);
            } else if (command.action == devices::Action::connect || command.action == devices::Action::disconnect) {
                const bool connect = command.action == devices::Action::connect;
                { std::lock_guard lock(state->mutex); state->data.status = connect ? tr(L"Łączenie z padem…") : tr(L"Rozłączanie pada…"); ++state->data.revision; }
                status = setBluetoothControllerConnection(command.id, connect, [state] { return state->stop || state->cancel; });
            } else if (command.action == devices::Action::volume) status = audio.setVolume(command.value, command.input, command.id);
            else if (command.action == devices::Action::mute) status = audio.setMuted(command.value != 0, command.input, command.id);
            else if (command.action == devices::Action::endpoint) status = audio.setDefault(command.id, command.input);
            std::vector<devices::BluetoothDevice> nearby;
            if (bluetoothPanel(panel)) {
                const auto objects = session.objects();
                if (discovering.empty())
                    for (const auto& adapter : objects.adapters)
                        if (adapter.powered && session.startDiscovery(adapter.path)) discovering.push_back(adapter.path);
                nearby = bluetoothDevices(objects);
                // No BlueZ (not installed, not running, no system bus) or no adapter at all.
                if ((!objects.available || objects.adapters.empty()) && status.empty())
                    status = tr(L"Wyszukiwanie Bluetooth przerwane. Sprawdź adapter w Windows.");
            }
            auto pads = panel == devices::Panel::controllers ? controllerPanelControllers() : std::vector<devices::Controller>{};
            auto endpoints = audioPanel(panel) ? audio.enumerate() : devices::Snapshot{};
            {
                std::lock_guard lock(state->mutex);
                if (panel == devices::Panel::controllers) state->data.controllers = pads;
                if (bluetoothPanel(panel)) state->data.bluetooth = std::move(nearby);
                if (audioPanel(panel)) {
                    state->data.endpoints = std::move(endpoints.endpoints);
                    for (int channel = 0; channel < 2; ++channel) {
                        endpoints.audio[channel].acknowledged = state->data.audio[channel].acknowledged;
                        state->data.audio[channel] = std::move(endpoints.audio[channel]);
                    }
                    if (status.empty() && !endpoints.status.empty()) status = endpoints.status;
                }
                if (state->visible && state->panel == panel && (command.action != devices::Action::refresh || !status.empty())) state->data.status = status;
                if (command.action == devices::Action::volume || command.action == devices::Action::mute)
                    state->data.audio[command.input].acknowledged = command.sequence;
                state->data.busy = std::any_of(state->commands.begin(), state->commands.end(), [](const auto& queued) {
                    return queued.action == devices::Action::endpoint || queued.action == devices::Action::pair ||
                           queued.action == devices::Action::connect || queued.action == devices::Action::disconnect;
                });
                ++state->data.revision;
            }
            for (const auto& pad : pads) {
                if (state->stop || pad.picture == devices::Picture::none ||
                    std::find(requested.begin(), requested.end(), pad.picture) != requested.end()) continue;
                requested.push_back(pad.picture);
                auto bytes = controllerPicture(pad.picture, userConfigDirectory() / L"controllers");
                if (bytes.empty()) continue;
                std::lock_guard lock(state->mutex);
                state->data.images[pad.picture] = std::move(bytes); ++state->data.revision;
            }
        }
        stopDiscovery();
    }
};
}
std::unique_ptr<DeviceService> systemDeviceService() { return std::make_unique<LinuxDevices>(); }
