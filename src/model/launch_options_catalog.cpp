#include "launch_options_catalog.hpp"
#include "../i18n/i18n.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>

namespace launch_options_catalog {
namespace {
using O = LaunchOptions;

// One line of text: no control characters, bounded.
bool oneLine(std::string_view value) {
    return value.size() <= 4096 && std::none_of(value.begin(), value.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f; });
}

template<bool O::*M> std::string getToggle(const Option&, const O& options) { return options.*M ? "1" : "0"; }
template<bool O::*M> bool setToggle(const Option&, O& options, std::string_view value) {
    if (value == "1" || value == "true") { options.*M = true; return true; }
    if (value == "0" || value == "false") { options.*M = false; return true; }
    return false;
}
template<uint32_t O::*M> std::string getNumber(const Option&, const O& options) { return std::to_string(options.*M); }
template<uint32_t O::*M> bool setNumber(const Option& option, O& options, std::string_view value) {
    uint32_t number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size() || number < option.minimum || number > option.maximum) return false;
    options.*M = number;
    return true;
}
template<std::string O::*M> std::string getText(const Option&, const O& options) { return options.*M; }
template<std::string O::*M> bool setText(const Option&, O& options, std::string_view value) {
    if (!oneLine(value)) return false;
    options.*M = std::string(value);
    return true;
}
template<std::filesystem::path O::*M> std::string getPath(const Option&, const O& options) { return text_encoding::utf8Lenient((options.*M).wstring()); }
template<std::filesystem::path O::*M> bool setPath(const Option&, O& options, std::string_view value) {
    if (!oneLine(value)) return false;
    if (value.empty()) { (options.*M).clear(); return true; }
    const auto wide = text_encoding::wide(value);
    if (wide.empty()) return false; // Not UTF-8.
    std::filesystem::path path(wide);
    if (!path.is_absolute()) return false;
    options.*M = path.lexically_normal();
    return true;
}
template<class E, E O::*M> std::string getChoice(const Option& option, const O& options) {
    const auto index = static_cast<size_t>(options.*M);
    return index < option.choices.size() ? option.choices[index].value : option.choices.front().value;
}
template<class E, E O::*M> bool setChoice(const Option& option, O& options, std::string_view value) {
    for (size_t i = 0; i < option.choices.size(); ++i)
        if (value == option.choices[i].value) { options.*M = static_cast<E>(i); return true; }
    return false;
}
std::string getTool(const Option&, const O& options) { return options.compat_tool.empty() ? std::string("default") : options.compat_tool; }
bool setTool(const Option&, O& options, std::string_view value) {
    if (value.empty() || !oneLine(value) || value.size() > 512) return false;
    options.compat_tool = std::string(value);
    return true;
}
std::string getEnvironment(const Option&, const O& options) {
    std::string result;
    for (const auto& variable : options.environment) {
        if (!result.empty()) result += '\n';
        result += variable.name + '=' + variable.value;
    }
    return result;
}
bool setEnvironment(const Option&, O& options, std::string_view value) {
    std::vector<O::Variable> result;
    while (!value.empty()) {
        const auto end = std::min(value.find('\n'), value.size());
        auto line = value.substr(0, end);
        value.remove_prefix(std::min(end + 1, value.size()));
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos) continue; // A blank line.
        line.remove_prefix(first);
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || !variableName(line.substr(0, equals)) || !oneLine(line) || result.size() >= 64) return false;
        result.push_back({std::string(line.substr(0, equals)), std::string(line.substr(equals + 1))});
    }
    options.environment = std::move(result);
    return true;
}

// A switch or choice stored as one environment variable (Option::variable).
std::string getVariable(const Option& option, const O& options) {
    for (const auto& variable : options.environment)
        if (variable.name == option.variable) return option.type == Type::toggle ? (variable.value == option.variable_on ? "1" : "0") : variable.value;
    return option.type == Type::toggle ? "0" : std::string{};
}
bool setVariable(const Option& option, O& options, std::string_view value) {
    std::string wanted; // Empty: the variable is not set.
    if (option.type == Type::toggle) {
        if (value == "1" || value == "true") wanted = option.variable_on;
        else if (value != "0" && value != "false") return false;
    } else {
        if (std::none_of(option.choices.begin(), option.choices.end(), [&](const Choice& choice) { return value == choice.value; })) return false;
        wanted = std::string(value);
    }
    std::erase_if(options.environment, [&](const O::Variable& variable) { return variable.name == option.variable; });
    if (wanted.empty()) return true;
    if (options.environment.size() >= 64) return false;
    options.environment.push_back({option.variable, std::move(wanted)});
    return true;
}
constexpr Option flag(const char* id, Section section, const char* variable, const char* on, Text label, Text description, const char* sets,
                      uint8_t runners = any, const char* depends = nullptr) {
    Option option{id, section, Type::toggle, label, description, sets, runners};
    option.depends_on = depends;
    option.variable = variable;
    option.variable_on = on;
    option.get = &getVariable;
    option.set = &setVariable;
    return option;
}
constexpr Option variableChoice(const char* id, Section section, const char* variable, Text label, Text description, const char* sets,
                                std::span<const Choice> choices, uint8_t runners = any) {
    Option option{id, section, Type::choice, label, description, sets, runners, choices};
    option.variable = variable;
    option.get = &getVariable;
    option.set = &setVariable;
    return option;
}

template<bool O::*M>
constexpr Option toggle(const char* id, Section section, Text label, Text description, const char* sets, uint8_t runners = any,
                        const char* depends = nullptr) {
    Option option{id, section, Type::toggle, label, description, sets, runners};
    option.depends_on = depends;
    option.get = &getToggle<M>;
    option.set = &setToggle<M>;
    return option;
}
template<uint32_t O::*M>
constexpr Option number(const char* id, Section section, Text label, Text description, const char* sets, uint32_t minimum, uint32_t maximum,
                        uint8_t runners = any, const char* depends = nullptr) {
    Option option{id, section, Type::number, label, description, sets, runners};
    option.minimum = minimum; option.maximum = maximum;
    option.depends_on = depends;
    option.get = &getNumber<M>;
    option.set = &setNumber<M>;
    return option;
}
template<std::string O::*M>
constexpr Option text(const char* id, Section section, Text label, Text description, const char* sets, uint8_t runners = any,
                      const char* depends = nullptr) {
    Option option{id, section, Type::text, label, description, sets, runners};
    option.depends_on = depends;
    option.get = &getText<M>;
    option.set = &setText<M>;
    return option;
}
template<std::filesystem::path O::*M>
constexpr Option path(const char* id, Section section, Text label, Text description, const char* sets) {
    Option option{id, section, Type::path, label, description, sets, any};
    option.get = &getPath<M>;
    option.set = &setPath<M>;
    return option;
}
template<class E, E O::*M>
constexpr Option choice(const char* id, Section section, Text label, Text description, const char* sets, std::span<const Choice> choices,
                        uint8_t runners = any, const char* depends = nullptr) {
    Option option{id, section, Type::choice, label, description, sets, runners, choices};
    option.depends_on = depends;
    option.get = &getChoice<E, M>;
    option.set = &setChoice<E, M>;
    return option;
}

// Choices in the order of their enum's values.
constexpr Choice sync_choices[] = {
    {"automatic", {L"Automatycznie", L"Automatic"}},
    {"ntsync", {L"NTSync", L"NTSync"}},
    {"fsync", {L"fsync", L"fsync"}},
    {"esync", {L"esync", L"esync"}},
    {"wineserver", {L"Wyłączona (wineserver)", L"Off (wineserver)"}},
};
constexpr Choice hdr_choices[] = {
    {"automatic", {L"Automatycznie", L"Automatic"}},
    {"on", {L"Włączone", L"On"}},
    {"force", {L"Wymuś", L"Force"}},
    {"off", {L"Wyłączone", L"Off"}},
};
constexpr Choice switch_choices[] = {
    {"automatic", {L"Automatycznie", L"Automatic"}},
    {"on", {L"Włączone", L"On"}},
    {"off", {L"Wyłączone", L"Off"}},
};
constexpr Choice present_choices[] = {
    {"automatic", {L"Jak w grze", L"As the game sets"}},
    {"fifo", {L"Włączona (FIFO)", L"On (FIFO)"}},
    {"mailbox", {L"Potrójne buforowanie (mailbox)", L"Triple buffering (mailbox)"}},
    {"immediate", {L"Wyłączona (immediate)", L"Off (immediate)"}},
    {"relaxed", {L"Adaptacyjna (FIFO relaxed)", L"Adaptive (FIFO relaxed)"}},
};
constexpr Choice window_choices[] = {
    {"fullscreen", {L"Pełny ekran", L"Full screen"}},
    {"borderless", {L"Bez ramki", L"Borderless"}},
    {"windowed", {L"Okno", L"Window"}},
};
constexpr Choice filter_choices[] = {
    {"automatic", {L"Domyślny", L"Default"}},
    {"linear", {L"Liniowy", L"Linear"}},
    {"nearest", {L"Najbliższy sąsiad", L"Nearest neighbour"}},
    {"fsr", {L"AMD FSR", L"AMD FSR"}},
    {"nis", {L"NVIDIA NIS", L"NVIDIA NIS"}},
    {"pixel", {L"Pikselowy", L"Pixel"}},
};
constexpr Choice scaler_choices[] = {
    {"automatic", {L"Automatycznie", L"Automatic"}},
    {"integer", {L"Całkowite", L"Integer"}},
    {"fit", {L"Dopasuj", L"Fit"}},
    {"fill", {L"Wypełnij", L"Fill"}},
    {"stretch", {L"Rozciągnij", L"Stretch"}},
};
// Variable choices: "" leaves the variable unset.
constexpr Choice audio_driver_choices[] = {
    {"", {L"Automatycznie", L"Automatic"}},
    {"pipewire", {L"PipeWire", L"PipeWire"}},
    {"pulse", {L"PulseAudio", L"PulseAudio"}},
    {"alsa", {L"ALSA", L"ALSA"}},
};
constexpr Choice xalia_choices[] = {
    {"", {L"Automatycznie", L"Automatic"}},
    {"1", {L"Włączona", L"On"}},
    {"0", {L"Wyłączona", L"Off"}},
};

constexpr SectionInfo section_table[] = {
    {Section::runner, "runner", {L"Środowisko uruchamiania", L"Runner"}},
    {Section::proton, "proton", {L"Proton i Wine", L"Proton and Wine"}},
    {Section::extras, "extras", {L"Dodatki i obejścia", L"Extras and workarounds"}},
    {Section::nvidia, "nvidia", {L"NVIDIA", L"NVIDIA"}},
    {Section::mesa, "mesa", {L"AMD i Mesa", L"AMD and Mesa"}},
    {Section::wrappers, "wrappers", {L"Wrappery", L"Wrappers"}},
    {Section::gamescope, "gamescope", {L"Gamescope", L"Gamescope"}},
    {Section::game, "game", {L"Gra", L"Game"}},
};

constexpr Option option_table[] = {
    // Runner.
    [] {
        Option option{"compat_tool", Section::runner, Type::tool, {L"Wersja Protona / Wine", L"Proton / Wine version"},
            {L"Narzędzie zgodności tej gry. „Domyślna” to wybór z ustawień globalnych (automatycznie: najnowszy GE-Proton, potem UMU-Proton przez umu, Proton ze Steam, Wine).",
             L"The compatibility tool for this game. \"Default\" is the global choice (automatic: the newest GE-Proton, then UMU-Proton through umu, Steam's Proton, Wine)."},
            "PROTONPATH=<tool> (umu-run), <tool>/proton, <tool>/bin/wine", any};
        option.get = &getTool;
        option.set = &setTool;
        return option;
    }(),
    path<&O::prefix>("prefix", Section::runner, {L"Prefiks", L"Prefix"},
        {L"Folder prefiksu Wine (dysk C: gry). Puste: własny prefiks gry w folderze prefiksów.",
         L"The Wine prefix folder (the game's C: drive). Empty: the game's own prefix in the prefixes folder."},
        "WINEPREFIX, STEAM_COMPAT_DATA_PATH"),
    toggle<&O::prefer_umu>("prefer_umu", Section::runner, {L"Uruchamiaj przez umu", L"Run through umu"},
        {L"Proton działa w Steam Linux Runtime (umu-launcher) z poprawkami protonfixes, jak w Steamie. Wyłączone: Proton bezpośrednio.",
         L"Proton runs in the Steam Linux Runtime (umu-launcher) with protonfixes, as in Steam. Off: Proton directly."},
        "umu-run <program> (PROTON_VERB=waitforexitandrun)", proton),
    text<&O::umu_id>("umu_id", Section::runner, {L"Identyfikator umu (GAMEID)", L"umu ID (GAMEID)"},
        {L"Poprawki protonfixes dla tej gry. Puste: z bazy umu-database dla sklepu, inaczej umu-default.",
         L"The protonfixes for this game. Empty: from umu-database for the store, else umu-default."},
        "GAMEID", proton, "prefer_umu"),

    // Proton and Wine.
    choice<O::Sync, &O::sync>("sync", Section::proton, {L"Synchronizacja", L"Synchronization"},
        {L"Sposób synchronizacji wątków gry. NTSync wymaga jądra 6.14+ z modułem ntsync.",
         L"How the game's threads synchronize. NTSync needs kernel 6.14+ with the ntsync module."},
        "PROTON_USE_NTSYNC=1 | PROTON_NO_NTSYNC=1 PROTON_NO_FSYNC=1 PROTON_NO_ESYNC=1; Wine: WINEFSYNC=1 | WINEESYNC=1", sync_choices),
    toggle<&O::wined3d>("wined3d", Section::proton, {L"WineD3D zamiast DXVK", L"WineD3D instead of DXVK"},
        {L"Direct3D 8–11 przez OpenGL zamiast Vulkana. Dla starych kart i gier, które z DXVK nie działają.",
         L"Direct3D 8–11 through OpenGL instead of Vulkan. For old graphics cards and games that fail with DXVK."},
        "PROTON_USE_WINED3D=1; Wine: WINEDLLOVERRIDES=d3d8,d3d9,d3d10core,d3d11,dxgi=b"),
    toggle<&O::wayland>("wayland", Section::proton, {L"Natywny Wayland", L"Native Wayland"},
        {L"Sterownik Wayland Wine zamiast XWaylanda (Proton 10+, GE-Proton, Wine 9+).",
         L"Wine's Wayland driver instead of XWayland (Proton 10+, GE-Proton, Wine 9+)."},
        "PROTON_ENABLE_WAYLAND=1; Wine: DISPLAY unset"),
    choice<O::Hdr, &O::hdr>("hdr", Section::proton, {L"HDR", L"HDR"},
        {L"Automatycznie: GE-Proton sam wykrywa HDR. Wymuś: HDR na każdym ekranie (Wayland). Wyłączone: gra widzi tylko SDR.",
         L"Automatic: GE-Proton detects HDR itself. Force: HDR on every screen (Wayland). Off: the game sees SDR only."},
        "on: PROTON_ENABLE_HDR=1 DXVK_HDR=1; force: also PROTON_FORCE_HDR=1; off: DXVK_NO_HDR=1", hdr_choices),
    toggle<&O::fsr>("fsr", Section::proton, {L"FSR w pełnym ekranie", L"Full-screen FSR"},
        {L"AMD FSR 1 skaluje grę uruchomioną w niższej rozdzielczości pełnoekranowej (GE-Proton, Wine-TkG).",
         L"AMD FSR 1 upscales a game run at a lower full-screen resolution (GE-Proton, Wine-TkG)."},
        "WINE_FULLSCREEN_FSR=1"),
    number<&O::fsr_strength>("fsr_strength", Section::proton, {L"Ostrość FSR", L"FSR sharpness"},
        {L"0 to najostrzej, 5 najłagodniej; 2 jest domyślne.", L"0 is the sharpest, 5 the softest; 2 is the default."},
        "WINE_FULLSCREEN_FSR_STRENGTH=<0-5>", 0, 5, any, "fsr"),
    text<&O::fsr_resolution>("fsr_resolution", Section::proton, {L"Rozdzielczość FSR", L"FSR resolution"},
        {L"Udawana rozdzielczość ekranu, np. 1280x720, dla gier, które zawsze renderują w natywnej.",
         L"A fake screen resolution, e.g. 1280x720, for games that always render at the native one."},
        "WINE_FULLSCREEN_FSR_CUSTOM_MODE=<width>x<height>", any, "fsr"),
    toggle<&O::fsr4_upgrade>("fsr4_upgrade", Section::proton, {L"Ulepsz do FSR 4", L"Upgrade to FSR 4"},
        {L"Gry z FSR 3.1 dostają FSR 4 (GE-Proton; Radeon RX 7000 i 9000).",
         L"Games with FSR 3.1 get FSR 4 (GE-Proton; Radeon RX 7000 and 9000)."},
        "PROTON_FSR4_UPGRADE=1", proton),
    toggle<&O::xess_upgrade>("xess_upgrade", Section::proton, {L"Ulepsz XeSS", L"Upgrade XeSS"},
        {L"Nowsze biblioteki Intel XeSS dla gier, które go mają (GE-Proton).",
         L"Newer Intel XeSS libraries for games that have it (GE-Proton)."},
        "PROTON_XESS_UPGRADE=1", proton),
    toggle<&O::large_address_aware>("large_address_aware", Section::proton, {L"Duża przestrzeń adresowa (LAA)", L"Large address aware"},
        {L"Gry 32-bitowe mogą używać 4 GB pamięci zamiast 2 GB.", L"32-bit games may use 4 GB of memory instead of 2 GB."},
        "PROTON_FORCE_LARGE_ADDRESS_AWARE=1; Wine: WINE_LARGE_ADDRESS_AWARE=1"),
    toggle<&O::wow64>("wow64", Section::proton, {L"Tryb WoW64", L"WoW64 mode"},
        {L"Programy 32-bitowe przez warstwę WoW64 Wine, bez 32-bitowych bibliotek systemu.",
         L"32-bit programs through Wine's WoW64 layer, without the system's 32-bit libraries."},
        "PROTON_USE_WOW64=1; Wine: WINEARCH=wow64"),
    text<&O::dxvk_hud>("dxvk_hud", Section::proton, {L"Nakładka DXVK", L"DXVK overlay"},
        {L"Co pokazuje DXVK: fps, frametimes, gpuload, full… Puste: nic.",
         L"What DXVK shows: fps, frametimes, gpuload, full… Empty: nothing."},
        "DXVK_HUD=<value>"),
    number<&O::dxvk_frame_rate>("dxvk_frame_rate", Section::proton, {L"Limit klatek DXVK", L"DXVK frame limit"},
        {L"Najwyżej tyle klatek na sekundę (DXVK i VKD3D-Proton). 0: bez limitu.",
         L"At most this many frames per second (DXVK and VKD3D-Proton). 0: no limit."},
        "DXVK_FRAME_RATE=<fps>", 0, 1000),
    toggle<&O::install_dxvk>("install_dxvk", Section::proton, {L"DXVK w prefiksie", L"DXVK in the prefix"},
        {L"Kopiuje DXVK do prefiksu: Direct3D 8–11 przez Vulkana (Proton ma go wbudowanego).",
         L"Copies DXVK into the prefix: Direct3D 8–11 through Vulkan (Proton has it built in)."},
        "system32/syswow64 DLLs, WINEDLLOVERRIDES=d3d8,d3d9,d3d10core,d3d11,dxgi=n,b", wine),
    toggle<&O::install_vkd3d>("install_vkd3d", Section::proton, {L"VKD3D-Proton w prefiksie", L"VKD3D-Proton in the prefix"},
        {L"Kopiuje VKD3D-Proton do prefiksu: Direct3D 12 przez Vulkana.", L"Copies VKD3D-Proton into the prefix: Direct3D 12 through Vulkan."},
        "system32/syswow64 DLLs, WINEDLLOVERRIDES=d3d12,d3d12core=n,b", wine),
    toggle<&O::eac_runtime>("eac_runtime", Section::proton, {L"Easy Anti-Cheat", L"Easy Anti-Cheat"},
        {L"Środowisko EAC dla Linuksa ze Steama (aplikacja „Proton EasyAntiCheat Runtime”). Działa tylko w grach, których twórcy je włączyli.",
         L"Steam's Linux EAC runtime (the \"Proton EasyAntiCheat Runtime\" app). Works only in games whose developers enabled it."},
        "PROTON_EAC_RUNTIME=<Steam library>/steamapps/common/Proton EasyAntiCheat Runtime", proton),
    toggle<&O::battleye_runtime>("battleye_runtime", Section::proton, {L"BattlEye", L"BattlEye"},
        {L"Środowisko BattlEye dla Linuksa ze Steama (aplikacja „Proton BattlEye Runtime”).",
         L"Steam's Linux BattlEye runtime (the \"Proton BattlEye Runtime\" app)."},
        "PROTON_BATTLEYE_RUNTIME=<Steam library>/steamapps/common/Proton BattlEye Runtime", proton),
    text<&O::dll_overrides>("dll_overrides", Section::proton, {L"Nadpisania bibliotek DLL", L"DLL overrides"},
        {L"Np. dinput8=n,b;xinput1_3=n (n: biblioteka gry, b: wbudowana w Wine).",
         L"E.g. dinput8=n,b;xinput1_3=n (n: the game's own library, b: Wine's built-in one)."},
        "WINEDLLOVERRIDES=<value> (after Launchme's own)"),
    text<&O::locale>("locale", Section::proton, {L"Język systemu (locale)", L"System locale"},
        {L"Np. ja_JP.UTF-8 dla gier japońskich. Puste: jak w systemie.", L"E.g. ja_JP.UTF-8 for Japanese games. Empty: the system's."},
        "HOST_LC_ALL=<locale>; Wine: LC_ALL=<locale>"),
    toggle<&O::disable_hidraw>("disable_hidraw", Section::proton, {L"Pady bez hidraw", L"Pads without hidraw"},
        {L"Pady przez SDL zamiast surowego HID: gdy pad działa źle albo jest widoczny dwa razy.",
         L"Pads through SDL instead of raw HID: when a pad misbehaves or shows up twice."},
        "PROTON_DISABLE_HIDRAW=1", proton),
    toggle<&O::prefer_sdl>("prefer_sdl", Section::proton, {L"Pady przez SDL", L"Pads through SDL"},
        {L"GE-Proton obsługuje pady przez SDL (np. gdy gra nie widzi pada Nintendo lub PlayStation).",
         L"GE-Proton handles pads through SDL (e.g. when the game does not see a Nintendo or PlayStation pad)."},
        "PROTON_PREFER_SDL=1", proton),
    toggle<&O::pulse_latency>("pulse_latency", Section::proton, {L"Napraw trzeszczący dźwięk", L"Fix crackling sound"},
        {L"Większy bufor dźwięku PulseAudio/PipeWire (60 ms) dla gier, w których dźwięk trzeszczy lub się rwie.",
         L"A larger PulseAudio/PipeWire sound buffer (60 ms) for games whose sound crackles or stutters."},
        "PULSE_LATENCY_MSEC=60"),
    toggle<&O::old_gl_string>("old_gl_string", Section::proton, {L"Krótka lista rozszerzeń OpenGL", L"Short OpenGL extension list"},
        {L"Dla starych gier OpenGL, które zawieszają się przy starcie przez zbyt długą listę rozszerzeń.",
         L"For old OpenGL games that crash at start on a too long extension list."},
        "PROTON_OLD_GL_STRING=1; Wine: MESA_EXTENSION_MAX_YEAR=2003 __GL_ExtensionStringVersion=17700"),
    toggle<&O::log>("log", Section::proton, {L"Zapisuj szczegółowy log", L"Write a detailed log"},
        {L"Log Protona lub Wine w folderze logów Launchme (logs/wine). Spowalnia grę: włączaj tylko do diagnozy.",
         L"The Proton or Wine log in Launchme's logs folder (logs/wine). Slows the game: enable it only to diagnose."},
        "PROTON_LOG=1 PROTON_LOG_DIR=<config>/logs/wine/<game> UMU_LOG=1; Wine: WINEDEBUG (off: -all)"),

    // Extras and workarounds: one environment variable each (Proton-CachyOS's and GE-Proton's options,
    // github.com/CachyOS/proton-cachyos#proton-cachyos-config-options).
    flag("dxvk_low_latency", Section::extras, "PROTON_DXVK_LOWLATENCY", "1", {L"DXVK z niskim opóźnieniem", L"Low-latency DXVK"},
        {L"Odmiana DXVK z równiejszym tempem klatek i mniejszym opóźnieniem sterowania (Proton-CachyOS).",
         L"A DXVK variant with steadier frame pacing and less input lag (Proton-CachyOS)."},
        "PROTON_DXVK_LOWLATENCY=1", proton),
    flag("vkd3d_low_latency", Section::extras, "PROTON_VKD3D_LOWLATENCY", "1", {L"VKD3D z niskim opóźnieniem (Reflex)", L"Low-latency VKD3D (Reflex)"},
        {L"DirectX 12 z równym tempem klatek i własnym Reflexem (Proton-CachyOS).",
         L"DirectX 12 with steady frame pacing and its own Reflex (Proton-CachyOS)."},
        "PROTON_VKD3D_LOWLATENCY=1", proton),
    flag("low_latency_layer", Section::extras, "LOW_LATENCY_LAYER", "1", {L"Warstwa niskiego opóźnienia", L"Low-latency layer"},
        {L"low_latency_layer: mniejsze opóźnienie w grach Vulkan i DXVK (Proton-CachyOS).",
         L"low_latency_layer: less latency in Vulkan and DXVK games (Proton-CachyOS)."},
        "LOW_LATENCY_LAYER=1", proton),
    flag("optiscaler", Section::extras, "PROTON_USE_OPTISCALER", "1", {L"OptiScaler", L"OptiScaler"},
        {L"Wstrzykuje OptiScaler: zamiana DLSS, FSR i XeSS gry na inne skalowanie, z menu w grze (Insert). Proton-CachyOS.",
         L"Injects OptiScaler: swaps the game's DLSS, FSR or XeSS for another upscaler, with an in-game menu (Insert). Proton-CachyOS."},
        "PROTON_USE_OPTISCALER=1", proton),
    flag("fsr4_indicator", Section::extras, "PROTON_FSR4_INDICATOR", "1", {L"Znak wodny FSR 4", L"FSR 4 watermark"},
        {L"Napis FSR 4 w rogu ekranu: widać, czy ulepszenie działa.", L"The FSR 4 text in the screen's corner: shows whether the upgrade works."},
        "PROTON_FSR4_INDICATOR=1", proton, "fsr4_upgrade"),
    flag("mlfg", Section::extras, "PROTON_MLFG_UPGRADE", "1", {L"Generowanie klatek FSR (MLFG)", L"FSR frame generation (MLFG)"},
        {L"Generowanie klatek FSR 4 przez uczenie maszynowe (FSR 4.0.3 i nowsze).", L"FSR 4's machine-learning frame generation (FSR 4.0.3 and newer)."},
        "PROTON_MLFG_UPGRADE=1", proton, "fsr4_upgrade"),
    flag("local_shader_cache", Section::extras, "PROTON_LOCAL_SHADER_CACHE", "1", {L"Własna pamięć shaderów gry", L"The game's own shader cache"},
        {L"Shadery zapisywane osobno dla gry, jak przy wstępnym buforowaniu Steama (Proton-CachyOS).",
         L"Shaders kept for the game on its own, as Steam's pre-caching does (Proton-CachyOS)."},
        "PROTON_LOCAL_SHADER_CACHE=1", proton),
    flag("media_gst", Section::extras, "PROTON_MEDIA_FORCE_GST", "1", {L"Napraw filmiki (GStreamer)", L"Fix cutscenes (GStreamer)"},
        {L"Gdy przerywniki filmowe są czarne, pokazują pasy kolorów albo nie mają dźwięku.",
         L"When cutscenes are black, show colour bars or have no sound."},
        "PROTON_MEDIA_FORCE_GST=1", proton),
    flag("no_dxgi_manager", Section::extras, "WINE_DO_NOT_CREATE_DXGI_DEVICE_MANAGER", "1", {L"Napraw filmiki (Media Foundation)", L"Fix cutscenes (Media Foundation)"},
        {L"Obejście niepełnego IMFDXGIDeviceManager: filmiki, które się zawieszają albo migają.",
         L"A workaround for the incomplete IMFDXGIDeviceManager: cutscenes that freeze or flicker."},
        "WINE_DO_NOT_CREATE_DXGI_DEVICE_MANAGER=1", proton),
    flag("dxvk_d3d8", Section::extras, "PROTON_DXVK_D3D8", "1", {L"DXVK dla Direct3D 8", L"DXVK for Direct3D 8"},
        {L"Gry Direct3D 8 przez Vulkana zamiast WineD3D.", L"Direct3D 8 games through Vulkan instead of WineD3D."},
        "PROTON_DXVK_D3D8=1", proton),
    flag("d7vk", Section::extras, "PROTON_D7VK_DDRAW", "1", {L"D7VK dla DirectX 7 i starszych", L"D7VK for DirectX 7 and older"},
        {L"DirectDraw i Direct3D 7 przez Vulkana (Proton-CachyOS).", L"DirectDraw and Direct3D 7 through Vulkan (Proton-CachyOS)."},
        "PROTON_D7VK_DDRAW=1", proton),
    flag("dxvk_sarek", Section::extras, "PROTON_DXVK_SAREK", "1", {L"DXVK Sarek dla starszych kart", L"DXVK Sarek for older cards"},
        {L"DXVK dla kart z Vulkanem 1.1–1.2. Nie w grach z anti-cheatem ani sieciowych (Proton-CachyOS).",
         L"DXVK for cards with Vulkan 1.1–1.2. Not in anti-cheat or online games (Proton-CachyOS)."},
        "PROTON_DXVK_SAREK=1", proton),
    flag("no_d3d11", Section::extras, "PROTON_NO_D3D11", "1", {L"Bez DirectX 11", L"No DirectX 11"},
        {L"Gra, która umie, wybierze DirectX 9 (czasem działa lepiej).", L"A game that supports it picks DirectX 9 instead (sometimes it runs better)."},
        "PROTON_NO_D3D11=1", proton),
    flag("no_d3d10", Section::extras, "PROTON_NO_D3D10", "1", {L"Bez DirectX 10", L"No DirectX 10"},
        {L"Gra, która umie, wybierze DirectX 9.", L"A game that supports it picks DirectX 9 instead."},
        "PROTON_NO_D3D10=1", proton),
    flag("heap_delay_free", Section::extras, "PROTON_HEAP_DELAY_FREE", "1", {L"Opóźnione zwalnianie pamięci", L"Delayed memory release"},
        {L"Obejście błędów gier, które używają zwolnionej pamięci (losowe awarie).", L"A workaround for games that use freed memory (random crashes)."},
        "PROTON_HEAP_DELAY_FREE=1", proton),
    flag("dx12_ray_tracing", Section::extras, "VKD3D_CONFIG", "dxr11", {L"Ray tracing w DirectX 12", L"Ray tracing in DirectX 12"},
        {L"Włącza DXR 1.1 w VKD3D-Proton dla gier, które same go nie wybierają.", L"Enables DXR 1.1 in VKD3D-Proton for games that do not pick it themselves."},
        "VKD3D_CONFIG=dxr11"),
    flag("no_wm_decoration", Section::extras, "PROTON_NO_WM_DECORATION", "1", {L"Bez ramek okien (Proton)", L"No window decorations (Proton)"},
        {L"Wine rysuje ramki sam: naprawia pełny ekran bez ramki i kliknięcia przechodzące przez okno.",
         L"Wine draws the frames itself: fixes borderless full screen and clicks passing through the window."},
        "PROTON_NO_WM_DECORATION=1", proton),
    flag("wine_no_wm_decoration", Section::extras, "WINE_NO_WM_DECORATION", "1", {L"Bez ramek okien (Wine)", L"No window decorations (Wine)"},
        {L"Naprawia pełny ekran bez ramki i kliknięcia przechodzące przez okno (Wine-CachyOS).",
         L"Fixes borderless full screen and clicks passing through the window (Wine-CachyOS)."},
        "WINE_NO_WM_DECORATION=1", wine),
    flag("integer_scaling", Section::extras, "WINE_FULLSCREEN_INTEGER_SCALING", "1", {L"Skalowanie całkowite", L"Integer scaling"},
        {L"Ostre piksele, gdy gra w niższej rozdzielczości wypełnia pełny ekran.", L"Sharp pixels when a game at a lower resolution fills the full screen."},
        "WINE_FULLSCREEN_INTEGER_SCALING=1"),
    flag("wayland_frame_fix", Section::extras, "WINE_DISABLE_VULKAN_OPWR", "1", {L"Obraz o klatkę za późno (Wayland)", L"A picture one frame late (Wayland)"},
        {L"Wyłącza rysowanie Vulkana przez inny proces, gdy obraz na Waylandzie się spóźnia.",
         L"Turns off Vulkan's other-process rendering when the picture lags on Wayland."},
        "WINE_DISABLE_VULKAN_OPWR=1", proton),
    flag("kwin_hacks", Section::extras, "WINE_USE_KWIN_HACKS", "1", {L"Poprawki okien dla KDE", L"Window fixes for KDE"},
        {L"Dla KDE Plasma starszego niż 6.4 (Wayland) i 6.6 (X11).", L"For KDE Plasma older than 6.4 (Wayland) and 6.6 (X11)."},
        "WINE_USE_KWIN_HACKS=1", proton),
    variableChoice("xalia", Section::extras, "PROTON_USE_XALIA", {L"Xalia (pad w menu gier)", L"Xalia (a pad in game menus)"},
        {L"Sterowanie padem w oknach, które znają tylko mysz i klawiaturę (np. launchery gier).",
         L"Pad control in windows made for the mouse and keyboard only (e.g. game launchers)."},
        "PROTON_USE_XALIA=1|0", xalia_choices, proton),
    variableChoice("audio_driver", Section::extras, "WINE_AUDIO_DRIVER", {L"Sterownik dźwięku Wine", L"Wine's audio driver"},
        {L"Który system dźwięku Wine używa; zmiana pomaga przy trzaskach i złym przestrzennym dźwięku.",
         L"Which sound system Wine uses; a change helps with crackling and wrong surround sound."},
        "WINE_AUDIO_DRIVER=pipewire|pulse|alsa", audio_driver_choices),
    flag("discord_bridge", Section::extras, "PROTON_DISCORD_BRIDGE", "1", {L"Status w Discordzie", L"Discord status"},
        {L"Gra pokazuje się w statusie Discorda (rpc-bridge, Proton-CachyOS).", L"The game shows in your Discord status (rpc-bridge, Proton-CachyOS)."},
        "PROTON_DISCORD_BRIDGE=1", proton),
    flag("steam_deck", Section::extras, "SteamDeck", "1", {L"Udawaj Steam Decka", L"Pretend to be a Steam Deck"},
        {L"Gra myśli, że działa na Steam Decku (ustawienia i pady jak na Decku).", L"The game thinks it runs on a Steam Deck (its settings and pads as on the Deck)."},
        "SteamDeck=1"),
    flag("wine_sdl_input", Section::extras, "WINE_PREFER_SDL_INPUT", "1", {L"Pady przez SDL", L"Pads through SDL"},
        {L"Wine-CachyOS: gdy gra nie widzi pada.", L"Wine-CachyOS: when the game does not see the pad."},
        "WINE_PREFER_SDL_INPUT=1", wine),
    flag("user_sandbox", Section::extras, "WINEUSERSANDBOX", "1", {L"Foldery użytkownika osobno", L"Separate user folders"},
        {L"Dokumenty i Obrazy prefiksu nie prowadzą do folderów domowych (Wine-CachyOS).",
         L"The prefix's Documents and Pictures do not lead to your home folders (Wine-CachyOS)."},
        "WINEUSERSANDBOX=1", wine),

    // NVIDIA.
    choice<O::Switch, &O::nvapi>("nvapi", Section::nvidia, {L"NVAPI (DLSS, Reflex)", L"NVAPI (DLSS, Reflex)"},
        {L"Biblioteka NVIDII dla DLSS i Reflex. Automatycznie: jak w Protonie (włączona na kartach NVIDIA od Protona 9).",
         L"NVIDIA's library for DLSS and Reflex. Automatic: as Proton does (on for NVIDIA cards since Proton 9)."},
        "on: PROTON_ENABLE_NVAPI=1 DXVK_ENABLE_NVAPI=1; off: PROTON_DISABLE_NVAPI=1", switch_choices),
    toggle<&O::install_dxvk_nvapi>("install_dxvk_nvapi", Section::nvidia, {L"DXVK-NVAPI w prefiksie", L"DXVK-NVAPI in the prefix"},
        {L"Kopiuje DXVK-NVAPI do prefiksu (DLSS w zwykłym Wine; wymaga DXVK lub VKD3D-Proton).",
         L"Copies DXVK-NVAPI into the prefix (DLSS in plain Wine; needs DXVK or VKD3D-Proton)."},
        "system32/syswow64 DLLs, WINEDLLOVERRIDES=nvapi,nvapi64,nvofapi64=n DXVK_ENABLE_NVAPI=1", wine),
    toggle<&O::dlss_upgrade>("dlss_upgrade", Section::nvidia, {L"Najnowsze DLSS", L"Latest DLSS"},
        {L"Pobiera najnowsze biblioteki DLSS i wybiera najnowszy preset (GE-Proton).",
         L"Downloads the newest DLSS libraries and selects the newest preset (GE-Proton)."},
        "PROTON_DLSS_UPGRADE=1", proton),
    toggle<&O::dlss_indicator>("dlss_indicator", Section::nvidia, {L"Wskaźnik DLSS", L"DLSS indicator"},
        {L"Napis DLSS w rogu ekranu: jego wersja i tryb.", L"The DLSS text in the screen's corner: its version and mode."},
        "PROTON_DLSS_INDICATOR=1 DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=1024,DLSSGIndicator=2"),
    toggle<&O::hide_nvidia_gpu>("hide_nvidia_gpu", Section::nvidia, {L"Ukryj kartę NVIDIA", L"Hide the NVIDIA card"},
        {L"Gra widzi kartę AMD (dla gier, które na NVIDII wymagają funkcji tylko z Windows).",
         L"The game sees an AMD card (for games that need Windows-only NVIDIA functions)."},
        "PROTON_HIDE_NVIDIA_GPU=1", proton),
    toggle<&O::prime_offload>("prime_offload", Section::nvidia, {L"Karta NVIDIA w laptopie (PRIME)", L"NVIDIA card in a laptop (PRIME)"},
        {L"Laptop z dwiema kartami: gra renderuje na NVIDII.", L"A laptop with two graphics cards: the game renders on the NVIDIA one."},
        "__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia __VK_LAYER_NV_optimus=NVIDIA_only"),
    number<&O::gl_shader_cache_gb>("gl_shader_cache_gb", Section::nvidia, {L"Pamięć podręczna shaderów (GB)", L"Shader cache (GB)"},
        {L"Rozmiar pamięci podręcznej shaderów sterownika NVIDIA. 0: domyślny sterownika.",
         L"The size of the NVIDIA driver's shader cache. 0: the driver's default."},
        "__GL_SHADER_DISK_CACHE_SIZE=<bytes>", 0, 100),
    toggle<&O::gl_shader_cache_keep>("gl_shader_cache_keep", Section::nvidia, {L"Nie czyść pamięci shaderów", L"Keep the shader cache"},
        {L"Sterownik nie usuwa starych shaderów, gdy pamięć się zapełni.", L"The driver does not delete old shaders when the cache is full."},
        "__GL_SHADER_DISK_CACHE_SKIP_CLEANUP=1"),
    toggle<&O::gl_threaded>("gl_threaded", Section::nvidia, {L"Optymalizacje wątkowe OpenGL", L"OpenGL threaded optimizations"},
        {L"Sterownik NVIDIA przetwarza OpenGL w osobnym wątku (gry OpenGL i WineD3D).",
         L"The NVIDIA driver processes OpenGL on its own thread (OpenGL and WineD3D games)."},
        "__GL_THREADED_OPTIMIZATIONS=1"),
    flag("vulkan_reflex", Section::nvidia, "DXVK_NVAPI_VKREFLEX", "1", {L"Reflex w grach Vulkan", L"Reflex in Vulkan games"},
        {L"Warstwa Reflex DXVK-NVAPI dla gier Vulkan (np. Doom: The Dark Ages, Path of Exile).",
         L"DXVK-NVAPI's Reflex layer for Vulkan games (e.g. Doom: The Dark Ages, Path of Exile)."},
        "DXVK_NVAPI_VKREFLEX=1", proton),
    flag("nvidia_libs", Section::nvidia, "PROTON_NVIDIA_LIBS", "1", {L"Biblioteki NVIDIA (PhysX, CUDA, NVENC)", L"NVIDIA libraries (PhysX, CUDA, NVENC)"},
        {L"Zastępcze nvcuda, nvenc, nvml i nvoptix: sprzętowy PhysX i podobne. Tylko gdy gra ich potrzebuje (Proton-CachyOS).",
         L"Stand-in nvcuda, nvenc, nvml and nvoptix: hardware PhysX and the like. Only when the game needs them (Proton-CachyOS)."},
        "PROTON_NVIDIA_LIBS=1", proton),

    // AMD and Mesa.
    toggle<&O::dri_prime>("dri_prime", Section::mesa, {L"Mocniejsza karta (DRI_PRIME)", L"The discrete card (DRI_PRIME)"},
        {L"Laptop z dwiema kartami AMD lub Intel: gra renderuje na mocniejszej.", L"A laptop with two AMD or Intel cards: the game renders on the faster one."},
        "DRI_PRIME=1"),
    toggle<&O::radv_sam>("radv_sam", Section::mesa, {L"Smart Access Memory", L"Smart Access Memory"},
        {L"RADV trzyma więcej danych w pamięci karty (wymaga Resizable BAR).", L"RADV keeps more data in video memory (needs Resizable BAR)."},
        "RADV_PERFTEST=sam"),
    toggle<&O::radv_nggc>("radv_nggc", Section::mesa, {L"NGG culling", L"NGG culling"},
        {L"Odrzucanie niewidocznych trójkątów w shaderach (RDNA3 i nowsze).", L"Discards invisible triangles in shaders (RDNA3 and newer)."},
        "RADV_PERFTEST=nggc"),
    toggle<&O::radv_emulate_rt>("radv_emulate_rt", Section::mesa, {L"Programowy ray tracing", L"Software ray tracing"},
        {L"Ray tracing emulowany na kartach bez sprzętowego (wolny).", L"Ray tracing emulated on cards without hardware support (slow)."},
        "RADV_EXPERIMENTAL=emulate_rt"),
    toggle<&O::mesa_glthread>("mesa_glthread", Section::mesa, {L"OpenGL w osobnym wątku (Mesa)", L"OpenGL on its own thread (Mesa)"},
        {L"Przyspiesza wiele gier OpenGL i WineD3D na sterownikach Mesa.", L"Speeds up many OpenGL and WineD3D games on Mesa drivers."},
        "mesa_glthread=true"),
    choice<O::PresentMode, &O::present_mode>("present_mode", Section::mesa, {L"Synchronizacja pionowa Vulkan", L"Vulkan vertical sync"},
        {L"Wymusza tryb wyświetlania Vulkana w sterownikach Mesa.", L"Forces the Vulkan present mode of Mesa drivers."},
        "MESA_VK_WSI_PRESENT_MODE=fifo|mailbox|immediate|relaxed", present_choices),
    number<&O::anisotropy>("anisotropy", Section::mesa, {L"Filtrowanie anizotropowe", L"Anisotropic filtering"},
        {L"Wymusza filtrowanie 2×–16× w RADV. 0: jak w grze.", L"Forces 2×–16× filtering in RADV. 0: as the game sets."},
        "RADV_TEX_ANISO=<2-16>", 0, 16),
    text<&O::mesa_shader_cache_size>("mesa_shader_cache_size", Section::mesa, {L"Pamięć shaderów Mesa", L"Mesa shader cache"},
        {L"Największy rozmiar pamięci podręcznej shaderów Mesa, np. 10G.", L"The largest size of Mesa's shader cache, e.g. 10G."},
        "MESA_SHADER_CACHE_MAX_SIZE=<size>"),

    // Wrappers.
    toggle<&O::gamemode>("gamemode", Section::wrappers, {L"GameMode", L"GameMode"},
        {L"Feral GameMode podnosi priorytet gry i taktowanie procesora na czas gry.", L"Feral GameMode raises the game's priority and the CPU clocks while it runs."},
        "gamemoderun"),
    toggle<&O::mangohud>("mangohud", Section::wrappers, {L"MangoHud", L"MangoHud"},
        {L"Nakładka z FPS, czasami klatek, temperaturami i obciążeniem.", L"An overlay with FPS, frame times, temperatures and load."},
        "mangohud; with gamescope: gamescope --mangoapp"),
    toggle<&O::gamescope>("gamescope", Section::wrappers, {L"Gamescope", L"Gamescope"},
        {L"Gra w osobnym kompozytorze: skalowanie FSR lub NIS, limit klatek, stała rozdzielczość.",
         L"The game in its own compositor: FSR or NIS scaling, a frame limit, a fixed resolution."},
        "gamescope <options> --"),
    toggle<&O::game_performance>("game_performance", Section::wrappers, {L"Profil wydajności (game-performance)", L"Performance profile (game-performance)"},
        {L"CachyOS: na czas gry profil zasilania „wydajność” i harmonogram do gier.", L"CachyOS: the performance power profile and the gaming scheduler while the game runs."},
        "game-performance"),
    toggle<&O::dlss_swapper>("dlss_swapper", Section::wrappers, {L"dlss-swapper", L"dlss-swapper"},
        {L"CachyOS: wymusza najnowszy preset DLSS.", L"CachyOS: forces the newest DLSS preset."}, "dlss-swapper"),
    toggle<&O::lsfg>("lsfg", Section::wrappers, {L"Generowanie klatek (lsfg-vk)", L"Frame generation (lsfg-vk)"},
        {L"Lossless Scaling na Linuksie: dodatkowe klatki w grach Vulkan i DXVK. Wymaga lsfg-vk i Lossless Scaling ze Steama.",
         L"Lossless Scaling on Linux: extra frames in Vulkan and DXVK games. Needs lsfg-vk and Lossless Scaling from Steam."},
        "LSFGVK_ENV=1 LSFGVK_MULTIPLIER=<n>"),
    number<&O::lsfg_multiplier>("lsfg_multiplier", Section::wrappers, {L"Mnożnik klatek", L"Frame multiplier"},
        {L"Ile klatek na każdą prawdziwą: 2 to podwojenie.", L"Frames for each real one: 2 doubles them."},
        "LSFGVK_MULTIPLIER=<2-8>", 2, 8, any, "lsfg"),
    number<&O::lsfg_flow_scale>("lsfg_flow_scale", Section::wrappers, {L"Skala przepływu (%)", L"Flow scale (%)"},
        {L"25–100. Mniej: szybciej, ale mniej dokładny ruch. 0: domyślna lsfg-vk (100).", L"25–100. Lower: faster, but less precise motion. 0: lsfg-vk's default (100)."},
        "LSFGVK_FLOW_SCALE=<0.25-1.0>", 0, 100, any, "lsfg"),
    toggle<&O::lsfg_performance>("lsfg_performance", Section::wrappers, {L"Tryb wydajności lsfg-vk", L"lsfg-vk performance mode"},
        {L"Lżejszy model: mniej obciąża kartę, kosztem jakości.", L"A lighter model: less load on the card, at some quality."},
        "LSFGVK_PERFORMANCE_MODE=1", any, "lsfg"),
    text<&O::wrapper>("wrapper", Section::wrappers, {L"Własny wrapper", L"Custom wrapper"},
        {L"Polecenie przed grą, np. obs-gamecapture albo strangle 60 (słowa jak w powłoce).",
         L"A command before the game, e.g. obs-gamecapture or strangle 60 (shell words)."},
        "<command> <arguments> before everything else"),

    // Gamescope.
    number<&O::gamescope_game_width>("gamescope_game_width", Section::gamescope, {L"Szerokość gry", L"Game width"},
        {L"Rozdzielczość, w której gra renderuje. 0: jak okno gamescope.", L"The resolution the game renders at. 0: as gamescope's window."},
        "gamescope -w <width>", 0, 16384, any, "gamescope"),
    number<&O::gamescope_game_height>("gamescope_game_height", Section::gamescope, {L"Wysokość gry", L"Game height"},
        {L"Rozdzielczość, w której gra renderuje. 0: jak okno gamescope.", L"The resolution the game renders at. 0: as gamescope's window."},
        "gamescope -h <height>", 0, 16384, any, "gamescope"),
    number<&O::gamescope_output_width>("gamescope_output_width", Section::gamescope, {L"Szerokość okna", L"Window width"},
        {L"Rozdzielczość okna lub ekranu gamescope. 0: jak ekran.", L"The resolution of gamescope's window or screen. 0: the screen's."},
        "gamescope -W <width>", 0, 16384, any, "gamescope"),
    number<&O::gamescope_output_height>("gamescope_output_height", Section::gamescope, {L"Wysokość okna", L"Window height"},
        {L"Rozdzielczość okna lub ekranu gamescope. 0: jak ekran.", L"The resolution of gamescope's window or screen. 0: the screen's."},
        "gamescope -H <height>", 0, 16384, any, "gamescope"),
    number<&O::gamescope_fps_limit>("gamescope_fps_limit", Section::gamescope, {L"Limit klatek", L"Frame limit"},
        {L"Najwyżej tyle klatek na sekundę. 0: bez limitu.", L"At most this many frames per second. 0: no limit."},
        "gamescope -r <fps>", 0, 1000, any, "gamescope"),
    number<&O::gamescope_fps_limit_unfocused>("gamescope_fps_limit_unfocused", Section::gamescope, {L"Limit klatek w tle", L"Frame limit in the background"},
        {L"Gdy okno gry nie jest aktywne. 0: bez limitu.", L"While the game's window is not focused. 0: no limit."},
        "gamescope -o <fps>", 0, 1000, any, "gamescope"),
    choice<O::GamescopeWindow, &O::gamescope_window>("gamescope_window", Section::gamescope, {L"Okno", L"Window"},
        {L"Pełny ekran, okno bez ramki albo zwykłe okno.", L"Full screen, a borderless window or an ordinary window."},
        "gamescope -f | -b", window_choices, any, "gamescope"),
    choice<O::GamescopeFilter, &O::gamescope_filter>("gamescope_filter", Section::gamescope, {L"Filtr skalowania", L"Upscaling filter"},
        {L"Jak gamescope powiększa obraz gry.", L"How gamescope enlarges the game's picture."},
        "gamescope -F linear|nearest|fsr|nis|pixel", filter_choices, any, "gamescope"),
    choice<O::GamescopeScaler, &O::gamescope_scaler>("gamescope_scaler", Section::gamescope, {L"Dopasowanie", L"Scaling"},
        {L"Jak obraz gry wypełnia okno.", L"How the game's picture fills the window."},
        "gamescope -S integer|fit|fill|stretch", scaler_choices, any, "gamescope"),
    number<&O::gamescope_sharpness>("gamescope_sharpness", Section::gamescope, {L"Ostrość FSR/NIS", L"FSR/NIS sharpness"},
        {L"0 to najostrzej, 20 najłagodniej (tylko z filtrem FSR lub NIS).", L"0 is the sharpest, 20 the softest (FSR or NIS filter only)."},
        "gamescope --sharpness <0-20>", 0, 20, any, "gamescope"),
    toggle<&O::gamescope_hdr>("gamescope_hdr", Section::gamescope, {L"HDR w gamescope", L"HDR in gamescope"},
        {L"Gamescope wyświetla HDR (ekran i kompozytor muszą go obsługiwać).", L"Gamescope outputs HDR (the screen and the compositor must support it)."},
        "gamescope --hdr-enabled", any, "gamescope"),
    toggle<&O::gamescope_grab_cursor>("gamescope_grab_cursor", Section::gamescope, {L"Przechwytuj kursor", L"Grab the cursor"},
        {L"Kursor zostaje w oknie gry (gry, z których ucieka mysz).", L"The cursor stays in the game's window (games whose mouse escapes)."},
        "gamescope --force-grab-cursor", any, "gamescope"),
    text<&O::gamescope_options>("gamescope_options", Section::gamescope, {L"Inne opcje gamescope", L"Other gamescope options"},
        {L"Dodatkowe parametry, np. --adaptive-sync --expose-wayland.", L"More parameters, e.g. --adaptive-sync --expose-wayland."},
        "gamescope <options>", any, "gamescope"),

    // The game.
    [] {
        Option option{"environment", Section::game, Type::environment, {L"Zmienne środowiska", L"Environment variables"},
            {L"Po jednej w wierszu: NAZWA=wartość. Mają pierwszeństwo przed przełącznikami powyżej.",
             L"One per line: NAME=value. They win over the switches above."},
            "<NAME>=<value> (set last)", any};
        option.get = &getEnvironment;
        option.set = &setEnvironment;
        return option;
    }(),
    text<&O::arguments>("arguments", Section::game, {L"Argumenty", L"Arguments"},
        {L"Dodawane po argumentach gry, np. -dx11 -skipintro.", L"Added after the game's own arguments, e.g. -dx11 -skipintro."},
        "<program> <action's arguments> <arguments>"),
    path<&O::working_directory>("working_directory", Section::game, {L"Folder roboczy", L"Working folder"},
        {L"Puste: folder programu gry.", L"Empty: the game program's folder."}, "the started program's current folder"),
    path<&O::before_script>("before_script", Section::game, {L"Skrypt przed uruchomieniem", L"Script before launch"},
        {L"Uruchamiany przed grą; gra startuje, gdy się zakończy.", L"Runs before the game; the game starts when it ends."},
        "<script> (LAUNCHME_GAME_KEY, LAUNCHME_GAME_NAME, LAUNCHME_PREFIX in its environment)"),
    path<&O::after_script>("after_script", Section::game, {L"Skrypt po zakończeniu", L"Script after exit"},
        {L"Uruchamiany po zamknięciu gry.", L"Runs after the game has closed."}, "<script> after the game"),
};

constexpr Verb verb_table[] = {
    {"vcrun2022", {L"Visual C++ 2015–2022", L"Visual C++ 2015–2022"}},
    {"dotnet48", {L".NET Framework 4.8", L".NET Framework 4.8"}},
    {"dotnetdesktop8", {L".NET 8 Desktop Runtime", L".NET 8 Desktop Runtime"}},
    {"d3dcompiler_47", {L"Kompilator shaderów d3dcompiler_47", L"The d3dcompiler_47 shader compiler"}},
    {"d3dx9", {L"DirectX 9 (d3dx9)", L"DirectX 9 (d3dx9)"}},
    {"xact", {L"XACT (dźwięk w starszych grach)", L"XACT (audio in older games)"}},
    {"physx", {L"NVIDIA PhysX", L"NVIDIA PhysX"}},
    {"xna40", {L"XNA Framework 4.0", L"XNA Framework 4.0"}},
    {"corefonts", {L"Czcionki Microsoft (corefonts)", L"Microsoft fonts (corefonts)"}},
    {"cjkfonts", {L"Czcionki chińskie, japońskie i koreańskie", L"Chinese, Japanese and Korean fonts"}},
};

// The DLLs mod loaders and injectors replace, with what loads them (as Heroic and Lutris users set them).
constexpr DllPreset dll_table[] = {
    {"winhttp", "n,b", {L"winhttp: BepInEx (mody gier Unity)", L"winhttp: BepInEx (Unity game mods)"}},
    {"version", "n,b", {L"version: ładowarki modów (Cyber Engine Tweaks, RED4ext)", L"version: mod loaders (Cyber Engine Tweaks, RED4ext)"}},
    {"dinput8", "n,b", {L"dinput8: Ultimate ASI Loader, Script Hook", L"dinput8: Ultimate ASI Loader, Script Hook"}},
    {"winmm", "n,b", {L"winmm: ASI Loader, poprawki starych gier", L"winmm: ASI Loader, fixes for old games"}},
    {"dsound", "n,b", {L"dsound: DSOAL (dźwięk 3D), ASI Loader", L"dsound: DSOAL (3D sound), ASI Loader"}},
    {"dxgi", "n,b", {L"dxgi: ReShade, Special K (DirectX 10–12)", L"dxgi: ReShade, Special K (DirectX 10–12)"}},
    {"d3d9", "n,b", {L"d3d9: ReShade (DirectX 9)", L"d3d9: ReShade (DirectX 9)"}},
    {"d3d11", "n,b", {L"d3d11: ReShade, Special K (DirectX 11)", L"d3d11: ReShade, Special K (DirectX 11)"}},
    {"d3dcompiler_47", "n", {L"d3dcompiler_47: kompilator shaderów gry (ReShade)", L"d3dcompiler_47: the game's shader compiler (ReShade)"}},
    {"dwmapi", "n,b", {L"dwmapi: UE4SS (mody Unreal Engine)", L"dwmapi: UE4SS (Unreal Engine mods)"}},
    {"xinput1_3", "n,b", {L"xinput1_3: x360ce, UE4SS", L"xinput1_3: x360ce, UE4SS"}},
    {"mscoree,mshtml", "", {L"Bez Wine Mono i Gecko (szybszy nowy prefiks)", L"No Wine Mono and Gecko (a faster new prefix)"}},
};

// Variables often found on ProtonDB and the PCGamingWiki, beyond the switches above.
constexpr VariablePreset variable_table[] = {
    // (PROTON_NO_D3D11, PROTON_HEAP_DELAY_FREE, VKD3D_CONFIG=dxr11 and SteamDeck are switches in Dodatki i obejścia.)
    {"WINE_CPU_TOPOLOGY", "4:0,1,2,3", {L"Najwyżej 4 rdzenie procesora (stare gry)", L"At most 4 CPU cores (old games)"}},
    {"WINEALSA_CHANNELS", "2", {L"Dźwięk stereo zamiast przestrzennego", L"Stereo instead of surround sound"}},
    {"vblank_mode", "0", {L"Bez synchronizacji pionowej OpenGL (Mesa)", L"No OpenGL vertical sync (Mesa)"}},
    {"__GL_SYNC_TO_VBLANK", "0", {L"Bez synchronizacji pionowej OpenGL (NVIDIA)", L"No OpenGL vertical sync (NVIDIA)"}},
    {"MANGOHUD_CONFIG", "fps_limit=60", {L"Limit 60 klatek w MangoHud", L"A 60 frames limit in MangoHud"}},
    {"WINEDEBUG", "-all", {L"Bez komunikatów diagnostycznych Wine", L"No Wine debug messages"}},
};

// WINEDLLOVERRIDES entries: "names=mode" separated by ';' (names by ',', lowercase, without ".dll").
struct OverrideEntry {
    std::vector<std::string> names;
    std::string mode;
    bool has_mode = false;
};
std::string dllName(std::string_view name) {
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
    std::string result(name);
    for (char& ch : result) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    if (result.size() > 4 && result.ends_with(".dll")) result.resize(result.size() - 4);
    return result;
}
std::vector<OverrideEntry> overrideEntries(std::string_view text) {
    std::vector<OverrideEntry> result;
    while (!text.empty()) {
        const auto end = std::min(text.find(';'), text.size());
        const auto part = text.substr(0, end);
        text.remove_prefix(std::min(end + 1, text.size()));
        OverrideEntry entry;
        const auto equals = part.find('=');
        auto names = part.substr(0, equals);
        if (equals != std::string_view::npos) { entry.has_mode = true; entry.mode = dllName(part.substr(equals + 1)); }
        while (!names.empty()) {
            const auto comma = std::min(names.find(','), names.size());
            if (auto name = dllName(names.substr(0, comma)); !name.empty()) entry.names.push_back(std::move(name));
            names.remove_prefix(std::min(comma + 1, names.size()));
        }
        if (!entry.names.empty()) result.push_back(std::move(entry));
    }
    return result;
}
std::vector<std::string> presetNames(std::string_view dlls) {
    std::vector<std::string> result;
    for (const auto& entry : overrideEntries(std::string(dlls) + "=x"))
        for (const auto& name : entry.names) result.push_back(name);
    return result;
}
}

const wchar_t* text(const Text& value) { return i18n::english() ? value.english : value.polish; }
std::span<const SectionInfo> sections() { return section_table; }
std::span<const Option> options() { return option_table; }

const Option* find(std::string_view id) {
    for (const auto& option : option_table) if (id == option.id) return &option;
    return nullptr;
}

const Option* forVariable(std::string_view name) {
    for (const auto& option : option_table) if (option.variable && name == option.variable) return &option;
    return nullptr;
}

std::string value(const LaunchOptions& options, std::string_view id) {
    const auto* option = find(id);
    return option ? option->get(*option, options) : std::string{};
}

bool setValue(LaunchOptions& options, std::string_view id, std::string_view value) {
    const auto* option = find(id);
    if (!option) return false;
    auto copy = options;
    if (!option->set(*option, copy, value)) return false;
    options = std::move(copy);
    return true;
}

bool variableName(std::string_view name) {
    if (name.empty() || name.size() > 128 || (name[0] >= '0' && name[0] <= '9')) return false;
    return std::all_of(name.begin(), name.end(), [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
    });
}

std::span<const Verb> winetricksVerbs() { return verb_table; }
std::span<const DllPreset> dllPresets() { return dll_table; }
std::span<const VariablePreset> variablePresets() { return variable_table; }

std::optional<std::string> dllOverride(std::string_view overrides, std::string_view dll) {
    const auto wanted = dllName(dll);
    std::optional<std::string> result; // The last entry naming it wins, as in Wine.
    for (const auto& entry : overrideEntries(overrides))
        if (std::find(entry.names.begin(), entry.names.end(), wanted) != entry.names.end()) result = entry.mode;
    return result;
}

bool presetSet(std::string_view overrides, const DllPreset& preset) {
    for (const auto& name : presetNames(preset.dlls)) {
        const auto mode = dllOverride(overrides, name);
        if (!mode || *mode != preset.mode) return false;
    }
    return true;
}

std::string withDllOverride(std::string_view overrides, std::string_view dlls, std::optional<std::string_view> mode) {
    const auto names = presetNames(dlls);
    std::string result;
    const auto append = [&](const std::vector<std::string>& list, bool has_mode, const std::string& entry_mode) {
        if (list.empty()) return;
        if (!result.empty()) result += ';';
        for (size_t i = 0; i < list.size(); ++i) result += (i ? "," : "") + list[i];
        if (has_mode) result += '=' + entry_mode;
    };
    for (auto entry : overrideEntries(overrides)) {
        std::erase_if(entry.names, [&](const std::string& name) { return std::find(names.begin(), names.end(), name) != names.end(); });
        append(entry.names, entry.has_mode, entry.mode);
    }
    if (mode) append(names, true, dllName(*mode));
    return result;
}

std::vector<DllEntry> dllEntries(std::string_view overrides) {
    std::vector<DllEntry> result;
    for (const auto& entry : overrideEntries(overrides)) {
        DllEntry item;
        for (size_t i = 0; i < entry.names.size(); ++i) item.dlls += (i ? "," : "") + entry.names[i];
        item.mode = entry.mode;
        result.push_back(std::move(item));
    }
    return result;
}
}
