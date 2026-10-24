#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The single list of emulated platforms. Adding a platform here adds its
// shortcut inference, shelf tile, colour, logo, background and ES-DE media.
// Order matters three ways: shelf order (by maker, then by release: Nintendo,
// PlayStation, Amiga, Retro, PC), shortcut inference priority (first match wins: keep
// Wii U before Switch for "cemu ... switch") and the tile identity
// `emulation_tile_base + index` (a saved place on a shifted tile falls back
// safely; PC stays last as the fallback).
struct EmulationPlatform {
    std::wstring_view name;                     // Display name and Game::platform value.
    std::wstring_view slug;                     // platforms/backgrounds/<slug>.(png|jpg)
    std::wstring_view logo;                     // HVR88 Monochrome-Gaming-Logos file name.
    std::array<std::wstring_view, 2> esde;      // ES-DE system folders, most specific first.
    std::array<std::wstring_view, 4> markers;   // Lower-case text in exe/options/tags/title.
    // Lower-case Steam ROM Manager collection names meaning this system, matched whole after
    // the " - <emulator>" suffix is cut ("Nintendo Switch - Eden" -> "nintendo switch").
    std::array<std::wstring_view, 4> collections;
    float red, green, blue;                     // Tile base colour.
};

inline constexpr uint32_t emulation_tile_base = 0xF0000000u;

inline constexpr std::array<EmulationPlatform, 14> emulation_platforms{{
    {L"GameCube", L"gamecube", L"nintendo_gamecube.png", {L"gc"}, {L"gamecube", L"\\gc\\", L"\\ngc\\"},
     {L"nintendo gamecube", L"gamecube", L"gc", L"ngc"}, 0.29f, 0.25f, 0.55f},
    {L"Wii", L"wii", L"nintendo_wii.png", {L"wii"}, {L"wii"},
     {L"nintendo wii", L"wii"}, 0.18f, 0.48f, 0.72f},
    // Dolphin alone cannot tell which console a game belongs to. Only unresolved shortcuts use this tile.
    {L"Wii / GameCube", L"wii", L"nintendo_wii.png", {L"wii", L"gc"}, {L"dolphin"},
     {L"wii / gamecube", L"gamecube / wii", L"nintendo wii / gamecube", L"dolphin"}, 0.18f, 0.48f, 0.72f},
    {L"Wii U", L"wiiu", L"nintendo_wiiu.png", {L"wiiu"}, {L"cemu", L"wii u"}, {L"nintendo wii u", L"wii u"}, 0.11f, 0.49f, 0.72f},
    {L"Switch", L"switch", L"nintendo_switch.png", {L"switch"}, {L"ryujinx", L"yuzu", L"sudachi", L"switch"},
     {L"nintendo switch", L"switch"}, 0.85f, 0.14f, 0.17f},
    // No "ps1" marker: EmuDeck launchers are PowerShell scripts (*.ps1) for every emulator.
    {L"PS1", L"ps1", L"playstation_flat.png", {L"psx"}, {L"duckstation", L"epsxe", L"pcsxr", L"\\psx\\"},
     {L"sony playstation", L"playstation", L"psx", L"ps1"}, 0.29f, 0.30f, 0.33f},
    {L"PS2", L"ps2", L"playstation_ps2.png", {L"ps2"}, {L"pcsx2", L"ps2", L"playstation 2"},
     {L"sony playstation 2", L"playstation 2", L"ps2"}, 0.12f, 0.25f, 0.54f},
    {L"PS3", L"ps3", L"playstation3_flat.png", {L"ps3"}, {L"rpcs3", L"playstation 3"},
     {L"sony playstation 3", L"playstation 3", L"ps3"}, 0.045f, 0.05f, 0.07f},
    // Leading spaces: "ps4"/"ps5" alone also occur inside unrelated emulator names.
    {L"PS4", L"ps4", L"playstation4_flat.png", {L"ps4"}, {L"shadps4", L"playstation 4", L" ps4"},
     {L"sony playstation 4", L"playstation 4", L"ps4"}, 0.08f, 0.29f, 0.64f},
    {L"PS5", L"ps5", L"playstation5_flat.png", {L"ps5"}, {L"playstation 5", L" ps5"},
     {L"sony playstation 5", L"playstation 5", L"ps5"}, 0.10f, 0.32f, 0.64f},
    {L"PSP", L"psp", L"playstation_psp.png", {L"psp"}, {L"ppsspp", L"psp"},
     {L"sony playstation portable", L"playstation portable", L"psp"}, 0.18f, 0.25f, 0.33f},
    // Before Retro: EmuDeck runs Amiga games through RetroArch's PUAE core.
    {L"Amiga", L"amiga", L"commodore_amiga.png", {L"amiga", L"amigacd32"},
     {L"puae", L"winuae", L"fs-uae", L"\\amiga\\"},
     {L"commodore amiga", L"amiga", L"amiga cd32", L"commodore amiga cd32"}, 0.62f, 0.16f, 0.20f},
    {L"Retro", L"retro", L"playlist_classics.png", {}, {L"retroarch"}, {}, 0.18f, 0.25f, 0.33f},
    // Fallback for every shortcut nothing else claims; it has no markers.
    {L"PC", L"pc", L"windows.png", {}, {}, {}, 0.18f, 0.25f, 0.33f},
}};

inline constexpr const EmulationPlatform* findEmulationPlatform(std::wstring_view name) {
    for (const auto& platform : emulation_platforms) if (platform.name == name) return &platform;
    return nullptr;
}
inline constexpr size_t emulationPlatformIndex(const EmulationPlatform& platform) {
    return static_cast<size_t>(&platform - emulation_platforms.data());
}
