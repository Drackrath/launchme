#pragma once
// Keys by their Windows virtual-key value. The launcher's key handling (modalKey, desktopKey, every
// screen's *Key(WPARAM key, ...)) is written against VK_* values, so the other systems' keyboard
// events are translated into the same numbers (platform/sdl_window.cpp) rather than the other way
// round. Letters are 'A'..'Z' and digits '0'..'9', as VK_ values are.
#ifdef _WIN32
#include <Windows.h>
#endif

namespace keys {
constexpr unsigned none = 0;
constexpr unsigned back = 0x08;
constexpr unsigned tab = 0x09;
constexpr unsigned enter = 0x0D;
constexpr unsigned shift = 0x10;
constexpr unsigned control = 0x11;
constexpr unsigned alt = 0x12; // VK_MENU
constexpr unsigned pause = 0x13;
constexpr unsigned caps_lock = 0x14;
constexpr unsigned escape = 0x1B;
constexpr unsigned space = 0x20;
constexpr unsigned page_up = 0x21; // VK_PRIOR
constexpr unsigned page_down = 0x22; // VK_NEXT
constexpr unsigned end = 0x23;
constexpr unsigned home = 0x24;
constexpr unsigned left = 0x25;
constexpr unsigned up = 0x26;
constexpr unsigned right = 0x27;
constexpr unsigned down = 0x28;
constexpr unsigned print_screen = 0x2C; // VK_SNAPSHOT
constexpr unsigned insert = 0x2D;
constexpr unsigned del = 0x2E;
constexpr unsigned apps = 0x5D; // The context-menu key.
constexpr unsigned numpad0 = 0x60; // ... numpad9 = 0x69
constexpr unsigned multiply = 0x6A;
constexpr unsigned add = 0x6B;
constexpr unsigned subtract = 0x6D;
constexpr unsigned decimal = 0x6E;
constexpr unsigned divide = 0x6F;
constexpr unsigned f1 = 0x70; // ... f24 = 0x87
constexpr unsigned f2 = 0x71;
constexpr unsigned f5 = 0x74;
constexpr unsigned f6 = 0x75;
constexpr unsigned f8 = 0x77;
constexpr unsigned f10 = 0x79;
constexpr unsigned f11 = 0x7A;
constexpr unsigned f12 = 0x7B;
constexpr unsigned oem_1 = 0xBA;     // ;: on a US keyboard
constexpr unsigned oem_plus = 0xBB;  // =+
constexpr unsigned oem_comma = 0xBC;
constexpr unsigned oem_minus = 0xBD;
constexpr unsigned oem_period = 0xBE;
constexpr unsigned oem_2 = 0xBF;     // /?
constexpr unsigned oem_3 = 0xC0;     // `~
constexpr unsigned oem_4 = 0xDB;     // [{
constexpr unsigned oem_5 = 0xDC;     // \|
constexpr unsigned oem_6 = 0xDD;     // ]}
constexpr unsigned oem_7 = 0xDE;     // '"
constexpr unsigned oem_102 = 0xE2;   // <> beside the left Shift (ISO keyboards)

constexpr unsigned letter(char upper) { return static_cast<unsigned>(upper); }
constexpr unsigned digit(int value) { return static_cast<unsigned>('0' + value); }
constexpr unsigned numpad(int value) { return numpad0 + static_cast<unsigned>(value); }
constexpr unsigned function(int number) { return f1 + static_cast<unsigned>(number - 1); }
} // namespace keys

#ifdef _WIN32
static_assert(keys::back == VK_BACK && keys::tab == VK_TAB && keys::enter == VK_RETURN && keys::shift == VK_SHIFT &&
              keys::control == VK_CONTROL && keys::alt == VK_MENU && keys::escape == VK_ESCAPE && keys::space == VK_SPACE &&
              keys::page_up == VK_PRIOR && keys::page_down == VK_NEXT && keys::end == VK_END && keys::home == VK_HOME &&
              keys::left == VK_LEFT && keys::up == VK_UP && keys::right == VK_RIGHT && keys::down == VK_DOWN &&
              keys::insert == VK_INSERT && keys::del == VK_DELETE && keys::apps == VK_APPS && keys::numpad0 == VK_NUMPAD0 &&
              keys::add == VK_ADD && keys::subtract == VK_SUBTRACT && keys::divide == VK_DIVIDE && keys::f1 == VK_F1 &&
              keys::f5 == VK_F5 && keys::f6 == VK_F6 && keys::f8 == VK_F8 && keys::f11 == VK_F11 && keys::oem_1 == VK_OEM_1 &&
              keys::oem_plus == VK_OEM_PLUS && keys::oem_minus == VK_OEM_MINUS && keys::oem_102 == VK_OEM_102,
              "keys:: mirrors the Windows virtual-key values");
#endif
