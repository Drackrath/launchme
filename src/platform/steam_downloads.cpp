#include "steam_downloads.hpp"
#include <Windows.h>
#include <vector>

namespace {
// Slots are the published ABI of SteamClient021 and STEAMAPPS_INTERFACE_VERSION008,
// verified against Valve's steamworks_sdk_162 headers in ValveSoftware/Proton.
// AppManager is additionally restricted to a verified PE build below. The
// public DLC API does not return counters for ordinary base-game downloads.
template<class Return, class... Args>
Return invoke(void* instance, size_t slot, Args... args) {
    const auto vtable = *static_cast<void***>(instance);
    using Method = Return(__thiscall*)(void*, Args...);
    return reinterpret_cast<Method>(vtable[slot])(instance, args...);
}
DWORD steamPid() {
    DWORD value = 0, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}
#if defined(_M_X64)
// The loaded module's sections, read in place (nothing is called).
struct Image {
    const uint8_t* base = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    size_t count = 0;
    size_t size = 0;
    explicit Image(HMODULE module) : base(reinterpret_cast<const uint8_t*>(module)) {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) { base = nullptr; return; }
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) { base = nullptr; return; }
        sections = IMAGE_FIRST_SECTION(nt); count = nt->FileHeader.NumberOfSections; size = nt->OptionalHeader.SizeOfImage;
    }
    // RVA inside a section with all of `flags` (code: executable, names: initialized read-only data).
    bool in(uintptr_t address, DWORD flags) const {
        if (!base || address < reinterpret_cast<uintptr_t>(base)) return false;
        const auto rva = address - reinterpret_cast<uintptr_t>(base);
        for (size_t i = 0; i < count; ++i)
            if ((sections[i].Characteristics & flags) == flags && rva >= sections[i].VirtualAddress &&
                rva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize) return true;
        return false;
    }
    // RVAs of every complete, zero-terminated `name` in the module's read-only data.
    std::vector<uint32_t> strings(const char* name) const {
        std::vector<uint32_t> found;
        const size_t length = strlen(name) + 1;
        for (size_t i = 0; base && i < count; ++i) {
            const auto& section = sections[i];
            if ((section.Characteristics & (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ)) != (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ) ||
                (section.Characteristics & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))) continue;
            const auto* begin = base + section.VirtualAddress;
            const auto* end = begin + section.Misc.VirtualSize;
            for (const auto* at = begin; at + length <= end; ++at) {
                at = static_cast<const uint8_t*>(memchr(at, name[0], static_cast<size_t>(end - at)));
                if (!at || at + length > end) break;
                if (memcmp(at, name, length) == 0 && (at == begin || at[-1] == 0)) found.push_back(static_cast<uint32_t>(at - base));
            }
        }
        return found;
    }
};
// The vtable slot whose function loads one of `names` (the method's own name), or -1.
int methodSlot(const Image& image, void* instance, const std::vector<uint32_t>& names) {
    if (!instance || names.empty()) return -1;
    const auto* const* vtable = *static_cast<const void* const* const*>(instance);
    if (!image.in(reinterpret_cast<uintptr_t>(vtable), IMAGE_SCN_MEM_READ)) return -1;
    int found = -1;
    for (int slot = 0; slot < 256; ++slot) {
        if (!image.in(reinterpret_cast<uintptr_t>(vtable + slot) + sizeof(void*) - 1, IMAGE_SCN_MEM_READ)) break;
        const auto address = reinterpret_cast<uintptr_t>(vtable[slot]);
        if (!image.in(address, IMAGE_SCN_MEM_EXECUTE)) break; // Past the vtable's end.
        DWORD64 module_base = 0;
        const auto* function = RtlLookupFunctionEntry(address, &module_base, nullptr);
        if (!function || module_base != reinterpret_cast<uintptr_t>(image.base) || function->EndAddress <= function->BeginAddress) continue;
        const std::span<const uint8_t> code(image.base + function->BeginAddress, function->EndAddress - function->BeginAddress);
        for (const auto name : names)
            if (referencesRva(code, function->BeginAddress, name)) {
                if (found >= 0 && found != slot) return -1; // Ambiguous: never guess.
                found = slot;
            }
    }
    return found;
}
#endif
}
struct SteamDownloads::Impl {
    std::filesystem::path root;
    HMODULE module = nullptr;
    void* client = nullptr;
    void* apps = nullptr;
    void* manager = nullptr;
    int update_slot = -1, enabled_slot = -1; // GetUpdateInfo, BIsDownloadingEnabled: found by name.
    int pipe = 0, user = 0;
    DWORD pid = 0;
    ULONGLONG retry = 0;
    explicit Impl(std::filesystem::path directory) : root(std::move(directory)) {}
    void close() {
        apps = manager = nullptr;
        update_slot = enabled_slot = -1;
        if (client && user && pipe) invoke<void>(client, 4, pipe, user);
        if (client && pipe) invoke<bool>(client, 1, pipe);
        user = pipe = 0; client = nullptr;
        if (module) FreeLibrary(module);
        module = nullptr;
    }
    bool connect() {
#if !defined(_M_X64)
        return false;
#else
        const auto current = steamPid();
        if (pid != current) { close(); pid = current; retry = 0; }
        if (apps) return true;
        if (!current || GetTickCount64() < retry) return false;
        retry = GetTickCount64() + 10000;
        close();
        if (!root.is_absolute()) return false;
        module = LoadLibraryExW((root / L"steamclient64.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return false;
        using Factory = void* (__cdecl*)(const char*, int*);
        const auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "CreateInterface"));
        if (!factory || !(client = factory("SteamClient021", nullptr))) return false;
        pipe = invoke<int>(client, 0);
        if (!pipe) return false;
        user = invoke<int>(client, 2, pipe);
        if (!user) return false;
        apps = invoke<void*>(client, 15, user, pipe, "STEAMAPPS_INTERFACE_VERSION008");
        // Versioned engine interface: 7 IsValidHSteamUserPipe, 43 GetIClientAppManager (getters only).
        // The AppManager's own slots are then matched by name in this very DLL (see the header).
        const Image image(module);
        auto* engine = factory("CLIENTENGINE_INTERFACE_VERSION005", nullptr);
        void* candidate = nullptr;
        if (engine && image.in(reinterpret_cast<uintptr_t>(engine), IMAGE_SCN_MEM_READ) && invoke<bool>(engine, 7, pipe, user))
            candidate = invoke<void*>(engine, 43, user, pipe);
        if (candidate) {
            update_slot = methodSlot(image, candidate, image.strings("GetUpdateInfo"));
            enabled_slot = methodSlot(image, candidate, image.strings("BIsDownloadingEnabled"));
            if (update_slot >= 0 && enabled_slot >= 0 && update_slot != enabled_slot) manager = candidate;
        }
        return apps != nullptr;
#endif
    }
    ~Impl() { close(); }
};
SteamDownloads::SteamDownloads(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
SteamDownloads::~SteamDownloads() = default;
bool SteamDownloads::connected() const { return impl_->apps != nullptr; }
bool SteamDownloads::appManagerConnected() const { return impl_->manager != nullptr; }
std::optional<DownloadBytes> SteamDownloads::read(uint32_t app) {
    if (!app || !impl_->connect()) return {};
    if (impl_->manager) {
        // Oversized storage protects against padding/tail differences; the
        // known prefix/counter layout is checked independently before use.
        alignas(8) uint8_t bytes[4096]{};
        if (invoke<bool>(impl_->manager, static_cast<size_t>(impl_->update_slot), app, bytes))
            if (auto result = steamUpdateCounters(bytes)) {
                if (!invoke<bool>(impl_->manager, static_cast<size_t>(impl_->enabled_slot))) result->phase = InstallPhase::paused;
                return result;
            }
    }
    DownloadBytes bytes;
    if (!invoke<bool>(impl_->apps, 22, app, &bytes.done, &bytes.total) || !bytes.total || bytes.done > bytes.total) return {};
    return bytes;
}
