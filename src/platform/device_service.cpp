#include "device_service.hpp"
#include "../i18n/i18n.hpp"
#include "game_controllers.hpp"
#include "bluetooth_controllers.hpp"
#include "system_audio.hpp"
#include "user_config.hpp"
#include "atomic_file.hpp"
#include "image_validation.hpp"
#include "../net/http.hpp"
#include "../model/headphones.hpp"
#include "../model/controller_pictures.hpp"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace {
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Foundation;
struct Shared {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<devices::Command> commands;
    devices::Snapshot data;
    devices::Panel panel = devices::Panel::controllers;
    bool visible = false;
    std::atomic_bool stop{false}, cancel{false};
};
template<class Operation> auto wait(const Operation& op, const std::shared_ptr<Shared>& state) {
    const auto deadline = GetTickCount64() + 60000;
    while (op.Status() == AsyncStatus::Started) {
        if (state->stop || state->cancel || GetTickCount64() >= deadline) { op.Cancel(); throw winrt::hresult_canceled(); }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return op.GetResults();
}
// Class-of-device and LE appearance: requested only by the Settings → Dźwięk watcher.
constexpr const wchar_t* cod_major = L"System.Devices.Aep.Bluetooth.Cod.Major";
constexpr const wchar_t* cod_audio = L"System.Devices.Aep.Bluetooth.Cod.Services.Audio";
constexpr const wchar_t* le_category = L"System.Devices.Aep.Bluetooth.Le.Appearance.Category";
int number(const winrt::Windows::Foundation::IInspectable& value) {
    if (!value) return -1;
    if (const auto v = value.try_as<winrt::Windows::Foundation::IReference<uint16_t>>()) return v.Value();
    if (const auto v = value.try_as<winrt::Windows::Foundation::IReference<uint8_t>>()) return v.Value();
    if (const auto v = value.try_as<winrt::Windows::Foundation::IReference<uint32_t>>()) return static_cast<int>(v.Value());
    return -1;
}
bool soundPanel(devices::Panel panel) { return panel == devices::Panel::sound; }
bool audioPanel(devices::Panel panel) { return panel == devices::Panel::audio || soundPanel(panel); }
bool bluetoothPanel(devices::Panel panel) { return panel == devices::Panel::bluetooth || soundPanel(panel); }
void added(const std::shared_ptr<Shared>& state, const DeviceInformation& info) {
    if (info.Name().empty()) return;
    devices::BluetoothDevice device{info.Id().c_str(), info.Name().c_str(), info.Pairing().IsPaired()};
    const auto properties = info.Properties();
    if (auto value = properties.TryLookup(L"System.Devices.Aep.IsConnected")) device.connected = winrt::unbox_value_or<bool>(value, false);
    device.audio = devices::audioDevice(number(properties.TryLookup(cod_major)),
        winrt::unbox_value_or<bool>(properties.TryLookup(cod_audio), false), number(properties.TryLookup(le_category)));
    std::lock_guard lock(state->mutex);
    auto& items = state->data.bluetooth;
    const auto it = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == device.id; });
    if (it == items.end()) items.push_back(std::move(device)); else *it = std::move(device);
    ++state->data.revision;
}
std::wstring pairDevice(const std::wstring& id, const std::shared_ptr<Shared>& state) {
    const auto info = wait(DeviceInformation::CreateFromIdAsync(id, {}, DeviceInformationKind::AssociationEndpoint), state);
    if (info.Pairing().IsPaired()) return tr(L"Urządzenie jest już sparowane.");
    // Consent is the user's explicit activation of this device's Pair row.
    // Only ConfirmOnly is accepted here; PIN/display ceremonies use Windows UI.
    const auto custom = info.Pairing().Custom();
    const auto token = custom.PairingRequested([state](const auto&, const DevicePairingRequestedEventArgs& request) {
        if (!state->stop && !state->cancel && request.PairingKind() == DevicePairingKinds::ConfirmOnly) request.Accept();
    });
    struct Revoke { DeviceInformationCustomPairing custom; winrt::event_token token; ~Revoke() { custom.PairingRequested(token); } } revoke{custom, token};
    const auto result = wait(custom.PairAsync(DevicePairingKinds::ConfirmOnly), state);
    switch (result.Status()) {
    case DevicePairingResultStatus::Paired: return tr(L"Sparowano.");
    case DevicePairingResultStatus::AlreadyPaired: return tr(L"Urządzenie jest już sparowane.");
    case DevicePairingResultStatus::AuthenticationTimeout: return tr(L"Upłynął czas parowania. Włącz tryb parowania urządzenia.");
    case DevicePairingResultStatus::AuthenticationFailure: return tr(L"Nieudane uwierzytelnienie. Spróbuj parowania w Windows.");
    case DevicePairingResultStatus::PairingCanceled: return tr(L"Anulowano parowanie.");
    case DevicePairingResultStatus::NotReadyToPair: return tr(L"Urządzenie nie jest gotowe do parowania.");
    default: return tr(L"Parowanie nie powiodło się (") + std::to_wstring(static_cast<int>(result.Status())) + tr(L"). Otwórz Bluetooth w Windows.");
    }
}
class WindowsDevices final : public DeviceService {
    std::shared_ptr<Shared> state_ = std::make_shared<Shared>();
    std::thread worker_;
public:
    WindowsDevices() : worker_([state = state_] { run(state); }) {}
    ~WindowsDevices() override {
        state_->stop = true; state_->cancel = true; state_->wake.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    void open(devices::Panel panel) override {
        { std::lock_guard lock(state_->mutex); state_->panel = panel; state_->visible = true; state_->data.status.clear(); ++state_->data.revision; state_->commands.push_back({}); }
        state_->wake.notify_all();
    }
    void close() override {
        {
            std::lock_guard lock(state_->mutex); state_->visible = false;
            // Leaving the panel must not discard the last accepted slider value.
            std::erase_if(state_->commands, [](const auto& command) { return command.action != devices::Action::volume && command.action != devices::Action::mute; });
        }
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
        // Images are a one-shot upload delta; the renderer retains the bytes.
        // Avoid copying megabytes of controller art for every audio poll.
        auto images = std::move(state_->data.images);
        auto result = state_->data; result.images = std::move(images); return result;
    }
    static void run(const std::shared_ptr<Shared>& state) {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            {
                SystemAudio audio;
                DeviceWatcher watcher{nullptr};
                devices::Panel watcher_panel = devices::Panel::bluetooth;
                std::vector<devices::Picture> requested;
                ULONGLONG last_volume_write = 0;
                while (!state->stop) {
                    devices::Command command;
                    devices::Panel panel;
                    bool visible;
                    {
                        std::unique_lock lock(state->mutex);
                        state->wake.wait_for(lock, std::chrono::seconds(2), [&] { return state->stop || !state->commands.empty(); });
                        if (state->stop) break;
                        if (!state->commands.empty() && state->commands.front().action == devices::Action::volume) {
                            const auto now = GetTickCount64();
                            if (now < last_volume_write + 33)
                                state->wake.wait_for(lock, std::chrono::milliseconds(last_volume_write + 33 - now), [&] { return state->stop.load(); });
                            if (state->stop) break;
                        }
                        visible = state->visible; panel = state->panel;
                        if (!state->commands.empty()) { command = std::move(state->commands.front()); state->commands.pop_front(); }
                    }
                    if ((!visible || !bluetoothPanel(panel) || panel != watcher_panel) && watcher) {
                        watcher.Stop(); watcher = nullptr;
                    }
                    if (!visible && command.action != devices::Action::volume && command.action != devices::Action::mute) continue;
                    if (command.action == devices::Action::volume) last_volume_write = GetTickCount64();
                    std::wstring status;
                    try {
                        if (command.action == devices::Action::pair) {
                            { std::lock_guard lock(state->mutex); state->data.status = tr(L"Parowanie…"); ++state->data.revision; }
                            status = pairDevice(command.id, state);
                        } else if (command.action == devices::Action::connect || command.action == devices::Action::disconnect) {
                            const bool connect = command.action == devices::Action::connect;
                            { std::lock_guard lock(state->mutex); state->data.status = connect ? tr(L"Łączenie z padem…") : tr(L"Rozłączanie pada…"); ++state->data.revision; }
                            status = setBluetoothControllerConnection(command.id, connect, [state] { return state->stop || state->cancel; });
                        } else if (command.action == devices::Action::volume) status = audio.setVolume(command.value, command.input, command.id);
                        else if (command.action == devices::Action::mute) status = audio.setMuted(command.value != 0, command.input, command.id);
                        else if (command.action == devices::Action::endpoint) status = audio.setDefault(command.id, command.input);
                        if (bluetoothPanel(panel) && !watcher) {
                            { std::lock_guard lock(state->mutex); state->data.bluetooth.clear(); }
                            const wchar_t* query = L"System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\" OR System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\"";
                            watcher_panel = panel;
                            const auto create = [&](bool classified) {
                                watcher = classified ?
                                    DeviceInformation::CreateWatcher(query, {L"System.Devices.Aep.IsConnected", L"System.Devices.Aep.IsPaired",
                                        cod_major, cod_audio, le_category}, DeviceInformationKind::AssociationEndpoint) :
                                    DeviceInformation::CreateWatcher(query, {L"System.Devices.Aep.IsConnected", L"System.Devices.Aep.IsPaired"},
                                        DeviceInformationKind::AssociationEndpoint);
                            watcher.Added([state](const auto&, const DeviceInformation& info) { added(state, info); });
                            watcher.Updated([state](const auto&, const DeviceInformationUpdate& update) {
                                std::lock_guard lock(state->mutex);
                                for (auto& item : state->data.bluetooth) if (item.id == update.Id().c_str()) {
                                    if (auto value = update.Properties().TryLookup(L"System.Devices.Aep.IsPaired")) item.paired = winrt::unbox_value_or<bool>(value, item.paired);
                                    if (auto value = update.Properties().TryLookup(L"System.Devices.Aep.IsConnected")) item.connected = winrt::unbox_value_or<bool>(value, item.connected);
                                }
                                ++state->data.revision;
                            });
                            watcher.Removed([state](const auto&, const DeviceInformationUpdate& info) {
                                std::lock_guard lock(state->mutex);
                                std::erase_if(state->data.bluetooth, [&](const auto& item) { return item.id == info.Id().c_str(); });
                                ++state->data.revision;
                            });
                            watcher.EnumerationCompleted([](const auto&, const auto&) {});
                            watcher.Stopped([state](const DeviceWatcher& sender, const auto&) {
                                if (sender.Status() != DeviceWatcherStatus::Aborted) return;
                                std::lock_guard lock(state->mutex); state->data.status = tr(L"Wyszukiwanie Bluetooth przerwane. Sprawdź adapter w Windows."); ++state->data.revision;
                            });
                            watcher.Start();
                            };
                            // Headphones and speakers are told apart by their class; a system that rejects
                            // these properties still lists the devices, unclassified.
                            if (!soundPanel(panel)) create(false);
                            else try { create(true); } catch (const winrt::hresult_error&) {
                                if (watcher) { try { watcher.Stop(); } catch (const winrt::hresult_error&) {} }
                                watcher = nullptr;
                                create(false);
                            }
                        }
                    } catch (const winrt::hresult_canceled&) { status = tr(L"Anulowano parowanie."); }
                      catch (const winrt::hresult_error& error) { status = tr(L"Błąd urządzenia (") + std::to_wstring(error.code().value) + L")."; }
                    auto pads = panel == devices::Panel::controllers ? controllerPanelControllers() : std::vector<devices::Controller>{};
                    auto endpoints = audioPanel(panel) ? audio.enumerate() : devices::Snapshot{};
                    {
                        std::lock_guard lock(state->mutex);
                        if (panel == devices::Panel::controllers) state->data.controllers = pads;
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
                if (watcher) watcher.Stop();
            }
            winrt::uninit_apartment();
        } catch (...) {
            std::lock_guard lock(state->mutex); state->data.status = tr(L"Nie można uruchomić obsługi urządzeń Windows.");
            state->data.busy = false; ++state->data.revision;
        }
    }
};
bool pngImage(const std::vector<uint8_t>& bytes) {
    const auto info = image_validation::inspect(bytes);
    return info && info->png && info->width >= 64 && info->height >= 64 && info->hasTransparency();
}
}
std::unique_ptr<DeviceService> windowsDeviceService() { return std::make_unique<WindowsDevices>(); }
std::vector<uint8_t> headphonePicture(const std::filesystem::path& cache) {
    const auto valid = [](const auto& bytes) {
        const auto info = image_validation::inspect(bytes);
        return info && info->width >= 64 && info->height >= 64 && info->hasTransparency();
    };
    const auto file = cache / L"ath-m50xbt2.webp";
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (input && input.tellg() > 0 && input.tellg() <= 8 * 1024 * 1024) {
        std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg())); input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (valid(bytes)) return bytes;
    }
    input.close();
    auto response = httpGet(headphones::picture_url, L"", 8 * 1024 * 1024);
    if (response.status != 200 || !valid(response.body)) return {};
    atomic_file::write(file, response.body);
    return response.body;
}
std::vector<uint8_t> controllerPicture(devices::Picture kind, const std::filesystem::path& cache) {
    const auto* photo = devices::controllerPhoto(kind);
    if (!photo) return {};
    const auto file = cache / (std::to_wstring(static_cast<int>(kind)) + L".alpha-v2.png");
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (input && input.tellg() > 0 && input.tellg() <= 8 * 1024 * 1024) {
        std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg())); input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (pngImage(bytes)) return bytes;
    }
    input.close();
    auto response = httpGet(photo->url, L"", 8 * 1024 * 1024);
    if (response.status != 200 || !pngImage(response.body)) return {};
    atomic_file::write(file, response.body);
    return response.body;
}
