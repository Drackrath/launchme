// SystemAudio on Linux (system_audio.hpp): the PulseAudio client API, served by PipeWire
// (pipewire-pulse) or PulseAudio itself. Endpoints are sink and source names; monitors of outputs
// are not inputs. One connection per SystemAudio: its own thread (pa_threaded_mainloop) keeps the
// change subscription, so read() asks the server again only after something changed.
#include "system_audio.hpp"
#include "../i18n/i18n.hpp"
#include "text_encoding.hpp"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
struct Device {
    std::string name;
    std::wstring description;
    pa_cvolume volume{};
    bool muted = false;
    std::optional<int> battery;
};

// PipeWire's Bluetooth nodes carry the headset's level ("bluetooth.battery", e.g. "80%") when BlueZ reports it.
std::optional<int> battery(const pa_proplist* properties) {
    const char* value = properties ? pa_proplist_gets(properties, "bluetooth.battery") : nullptr;
    if (!value || !*value) return {};
    char* end = nullptr;
    const long level = std::strtol(value, &end, 10);
    if (end == value || level < 0 || level > 100) return {};
    return static_cast<int>(level);
}

template <class Info> Device device(const Info* info) {
    Device result{info->name ? info->name : "", text_encoding::wideLenient(info->description ? info->description : ""), info->volume,
                  info->mute != 0, battery(info->proplist)};
    return result;
}
}

struct SystemAudio::Impl {
    pa_threaded_mainloop* loop = nullptr;
    pa_context* context = nullptr;
    std::atomic_bool changed{true}; // Set by the subscription: the cache below is stale.
    struct Default { bool available = false; Device device; };
    std::array<Default, 2> defaults;

    struct Lock {
        pa_threaded_mainloop* loop;
        explicit Lock(pa_threaded_mainloop* l) : loop(l) { pa_threaded_mainloop_lock(loop); }
        ~Lock() { pa_threaded_mainloop_unlock(loop); }
    };

    Impl() {
        loop = pa_threaded_mainloop_new();
        if (!loop) return;
        context = pa_context_new(pa_threaded_mainloop_get_api(loop), "Launchme");
        if (!context) return;
        pa_context_set_state_callback(context, [](pa_context*, void* self) {
            pa_threaded_mainloop_signal(static_cast<Impl*>(self)->loop, 0);
        }, this);
        pa_context_set_subscribe_callback(context, [](pa_context*, pa_subscription_event_type_t, uint32_t, void* self) {
            static_cast<Impl*>(self)->changed.store(true);
        }, this);
        if (pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0 || pa_threaded_mainloop_start(loop) < 0) return;
        Lock lock(loop);
        for (;;) {
            const auto state = pa_context_get_state(context);
            if (state == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(state)) return;
            pa_threaded_mainloop_wait(loop);
        }
        const auto mask = static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER);
        if (auto* operation = pa_context_subscribe(context, mask, nullptr, nullptr)) pa_operation_unref(operation);
    }
    ~Impl() {
        if (loop) pa_threaded_mainloop_stop(loop);
        if (context) { pa_context_disconnect(context); pa_context_unref(context); }
        if (loop) pa_threaded_mainloop_free(loop);
    }
    bool ready() const { return context && pa_context_get_state(context) == PA_CONTEXT_READY; }
    // With the loop locked: waits for an operation whose callbacks signal the loop.
    bool wait(pa_operation* operation) {
        if (!operation) return false;
        while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING && ready()) pa_threaded_mainloop_wait(loop);
        const bool done = pa_operation_get_state(operation) == PA_OPERATION_DONE;
        pa_operation_unref(operation);
        return done;
    }
    // The server's default output and input names.
    std::optional<std::array<std::string, 2>> defaultNames() {
        if (!ready()) return {};
        struct Request { Impl* self; std::array<std::string, 2> names; } request{this, {}};
        Lock lock(loop);
        if (!wait(pa_context_get_server_info(context, [](pa_context*, const pa_server_info* info, void* user) {
                auto& request = *static_cast<Request*>(user);
                if (info && info->default_sink_name) request.names[0] = info->default_sink_name;
                if (info && info->default_source_name) request.names[1] = info->default_source_name;
                pa_threaded_mainloop_signal(request.self->loop, 0);
            }, &request))) return {};
        return request.names;
    }
    // Outputs, or inputs without the outputs' monitors; nullopt when the server does not answer.
    std::optional<std::vector<Device>> list(bool input) {
        if (!ready()) return {};
        struct Request { Impl* self; std::vector<Device> devices; } request{this, {}};
        Lock lock(loop);
        pa_operation* operation = input
            ? pa_context_get_source_info_list(context, [](pa_context*, const pa_source_info* info, int last, void* user) {
                  auto& request = *static_cast<Request*>(user);
                  if (last) { pa_threaded_mainloop_signal(request.self->loop, 0); return; }
                  if (info && info->monitor_of_sink == PA_INVALID_INDEX) request.devices.push_back(device(info));
              }, &request)
            : pa_context_get_sink_info_list(context, [](pa_context*, const pa_sink_info* info, int last, void* user) {
                  auto& request = *static_cast<Request*>(user);
                  if (last) { pa_threaded_mainloop_signal(request.self->loop, 0); return; }
                  if (info) request.devices.push_back(device(info));
              }, &request);
        if (!wait(operation)) return {};
        return request.devices;
    }
    // Calls that only succeed or fail (volume, mute, default device).
    struct Result { Impl* self; bool success = false; };
    static void finished(pa_context*, int success, void* user) {
        auto& result = *static_cast<Result*>(user);
        result.success = success != 0;
        pa_threaded_mainloop_signal(result.self->loop, 0);
    }
    template <class Call> bool change(Call call) {
        if (!ready()) return false;
        Result result{this};
        Lock lock(loop);
        return wait(call(&result)) && result.success;
    }
    // Both defaults, again from the server when the subscription saw a change.
    void refresh(bool force) {
        if (!force && !changed.load()) return;
        changed.store(false);
        defaults = {};
        const auto names = defaultNames();
        if (!names) { changed.store(true); return; }
        for (const bool input : {false, true}) {
            const auto devices = list(input);
            if (!devices) { changed.store(true); continue; }
            for (const auto& item : *devices)
                if (item.name == (*names)[input]) defaults[input] = {true, item};
        }
    }
};

SystemAudio::SystemAudio() : impl_(std::make_unique<Impl>()) {}
SystemAudio::~SystemAudio() = default;

bool SystemAudio::read(VolumeLevel& value, bool input) {
    impl_->refresh(false);
    const auto& current = impl_->defaults[input];
    if (!current.available || current.device.name.empty()) { endpoint_ids_[input].clear(); return false; }
    endpoint_ids_[input] = text_encoding::wideLenient(current.device.name);
    // 100% in the system mixer is 1; boosted levels above it read as full.
    value.level = std::clamp(static_cast<float>(pa_cvolume_max(&current.device.volume)) / PA_VOLUME_NORM, 0.0f, 1.0f);
    value.muted = current.device.muted;
    return true;
}

void SystemAudio::refreshDevice() { impl_->changed.store(true); endpoint_ids_ = {}; }

std::optional<bool> SystemAudio::headphonesConnected() {
    const auto state = headphonesState();
    return state ? std::optional<bool>{state->connected} : std::nullopt;
}

std::optional<headphones::State> SystemAudio::headphonesState() {
    const auto outputs = impl_->list(false);
    if (!outputs) return {};
    headphones::State state;
    for (const auto& output : *outputs) {
        if (!headphones::supported(output.description)) continue;
        state.connected = true; // Several profiles of one headset are one connection.
        state.battery = output.battery;
        if (state.battery) return state;
    }
    return state;
}

devices::Snapshot SystemAudio::enumerate() {
    devices::Snapshot result;
    refreshDevice();
    for (const bool input : {false, true}) {
        VolumeLevel volume;
        auto& channel = result.audio[input];
        channel.available = read(volume, input);
        channel.volume = volume.level; channel.muted = volume.muted; channel.endpoint = endpoint_ids_[input];
    }
    if (!impl_->ready()) { result.status = tr(L"Nie można odczytać urządzeń audio."); return result; }
    for (const bool input : {false, true}) {
        const auto devices = impl_->list(input);
        if (!devices) continue;
        for (const auto& item : *devices) {
            const auto id = text_encoding::wideLenient(item.name);
            if (id.empty()) continue;
            result.endpoints.push_back({id, item.description.empty() ? tr(L"Urządzenie audio") : item.description, input,
                                        id == endpoint_ids_[input]});
        }
    }
    return result;
}

std::wstring SystemAudio::setVolume(float level, bool input, const std::wstring& expected) {
    if (!std::isfinite(level)) return tr(L"Nieprawidłowy poziom głośności.");
    refreshDevice(); VolumeLevel old;
    if (!read(old, input)) return input ? tr(L"Brak dostępnego wejścia audio.") : tr(L"Brak dostępnego wyjścia audio.");
    if (!expected.empty() && expected != endpoint_ids_[input]) return tr(L"Urządzenie audio zmieniło się. Ponów zmianę.");
    const auto& current = impl_->defaults[input].device;
    pa_cvolume volume = current.volume; // Scaled as a whole: the balance between channels stays.
    if (!pa_cvolume_valid(&volume)) pa_cvolume_set(&volume, 2, PA_VOLUME_NORM);
    pa_cvolume_scale(&volume, static_cast<pa_volume_t>(std::lround(std::clamp(level, 0.0f, 1.0f) * PA_VOLUME_NORM)));
    const auto name = current.name;
    const bool done = impl_->change([&](Impl::Result* result) {
        return input ? pa_context_set_source_volume_by_name(impl_->context, name.c_str(), &volume, &Impl::finished, result)
                     : pa_context_set_sink_volume_by_name(impl_->context, name.c_str(), &volume, &Impl::finished, result);
    });
    return done ? L"" : tr(L"Nie udało się zmienić głośności.");
}

std::wstring SystemAudio::setMuted(bool muted, bool input, const std::wstring& expected) {
    refreshDevice(); VolumeLevel old;
    if (!read(old, input)) return input ? tr(L"Brak dostępnego wejścia audio.") : tr(L"Brak dostępnego wyjścia audio.");
    if (!expected.empty() && expected != endpoint_ids_[input]) return tr(L"Urządzenie audio zmieniło się. Ponów zmianę.");
    const auto name = impl_->defaults[input].device.name;
    const bool done = impl_->change([&](Impl::Result* result) {
        return input ? pa_context_set_source_mute_by_name(impl_->context, name.c_str(), muted, &Impl::finished, result)
                     : pa_context_set_sink_mute_by_name(impl_->context, name.c_str(), muted, &Impl::finished, result);
    });
    return done ? L"" : tr(L"Nie udało się zmienić wyciszenia.");
}

std::wstring SystemAudio::setDefault(const std::wstring& id, bool input) {
    const auto available = enumerate();
    if (std::none_of(available.endpoints.begin(), available.endpoints.end(), [&](const auto& e) { return e.id == id && e.input == input; }))
        return tr(L"Urządzenie nie jest już dostępne.");
    // One default per direction (no Windows roles); the server moves the playing streams to it.
    const auto name = text_encoding::utf8(id);
    const bool done = impl_->change([&](Impl::Result* result) {
        return input ? pa_context_set_default_source(impl_->context, name.c_str(), &Impl::finished, result)
                     : pa_context_set_default_sink(impl_->context, name.c_str(), &Impl::finished, result);
    });
    refreshDevice();
    return done ? L"" : tr(L"Zmiana nie powiodła się. Przywrócono poprzednie urządzenia.");
}
