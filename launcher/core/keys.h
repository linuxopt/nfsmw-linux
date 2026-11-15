// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// Key capture for the Controls tab: a key press's Linux key code to the USB
// HID usage settings.toml stores (user/key_names.h, the same codes as SDL's
// scancodes). Qt reports XKB keycodes (QKeyEvent::nativeScanCode), which are
// the kernel's evdev codes + 8 on X11 and Wayland alike. Only the keys the
// game's table lists map; anything else (F1, Windows/Super, media keys) is
// kUnknown, refused as the in-game menu refuses it. The pairs follow SDL's
// own evdev table (SDL's src/events/scancodes_linux.h). Linux has no code of
// its own for HID 50 ("non_us_hash"): that key arrives as KEY_BACKSLASH.
// Header only.
#pragma once
#include <cstddef>
#include <cstdint>

#include <user/key_names.h>

namespace launcher::keys
{
    struct EvdevKey
    {
        uint16_t evdev;  // linux/input-event-codes.h KEY_*
        uint16_t hid;
    };

    // Ascending by evdev code.
    inline constexpr EvdevKey kEvdevKeys[] = {
        { 1, 41 },    // ESC
        { 2, 30 }, { 3, 31 }, { 4, 32 }, { 5, 33 }, { 6, 34 }, { 7, 35 }, { 8, 36 }, { 9, 37 }, { 10, 38 }, { 11, 39 },  // 1..0
        { 12, 45 },   // MINUS
        { 13, 46 },   // EQUAL
        { 14, 42 },   // BACKSPACE
        { 15, 43 },   // TAB
        { 16, 20 }, { 17, 26 }, { 18, 8 }, { 19, 21 }, { 20, 23 }, { 21, 28 }, { 22, 24 }, { 23, 12 }, { 24, 18 }, { 25, 19 },  // Q..P
        { 26, 47 },   // LEFTBRACE
        { 27, 48 },   // RIGHTBRACE
        { 28, 40 },   // ENTER
        { 29, 224 },  // LEFTCTRL
        { 30, 4 }, { 31, 22 }, { 32, 7 }, { 33, 9 }, { 34, 10 }, { 35, 11 }, { 36, 13 }, { 37, 14 }, { 38, 15 },  // A..L
        { 39, 51 },   // SEMICOLON
        { 40, 52 },   // APOSTROPHE
        { 41, 53 },   // GRAVE
        { 42, 225 },  // LEFTSHIFT
        { 43, 49 },   // BACKSLASH
        { 44, 29 }, { 45, 27 }, { 46, 6 }, { 47, 25 }, { 48, 5 }, { 49, 17 }, { 50, 16 },  // Z..M
        { 51, 54 },   // COMMA
        { 52, 55 },   // DOT
        { 53, 56 },   // SLASH
        { 54, 229 },  // RIGHTSHIFT
        { 55, 85 },   // KPASTERISK
        { 56, 226 },  // LEFTALT
        { 57, 44 },   // SPACE
        { 58, 57 },   // CAPSLOCK
        // 59 F1: not bindable (it opens the in-game menu)
        { 60, 59 }, { 61, 60 }, { 62, 61 }, { 63, 62 }, { 64, 63 }, { 65, 64 }, { 66, 65 }, { 67, 66 }, { 68, 67 },  // F2..F10
        { 69, 83 },   // NUMLOCK
        { 70, 71 },   // SCROLLLOCK
        { 71, 95 }, { 72, 96 }, { 73, 97 },  // KP7 KP8 KP9
        { 74, 86 },   // KPMINUS
        { 75, 92 }, { 76, 93 }, { 77, 94 },  // KP4 KP5 KP6
        { 78, 87 },   // KPPLUS
        { 79, 89 }, { 80, 90 }, { 81, 91 },  // KP1 KP2 KP3
        { 82, 98 },   // KP0
        { 83, 99 },   // KPDOT
        { 86, 100 },  // 102ND (non_us_backslash)
        { 87, 68 },   // F11
        { 88, 69 },   // F12
        { 89, 135 },  // RO (international_1)
        { 92, 138 },  // HENKAN (international_4)
        { 93, 136 },  // KATAKANAHIRAGANA (international_2)
        { 94, 139 },  // MUHENKAN (international_5)
        { 95, 140 },  // KPJPCOMMA (international_6)
        { 96, 88 },   // KPENTER
        { 97, 228 },  // RIGHTCTRL
        { 98, 84 },   // KPSLASH
        { 99, 70 },   // SYSRQ (print_screen)
        { 100, 230 }, // RIGHTALT
        { 102, 74 },  // HOME
        { 103, 82 },  // UP
        { 104, 75 },  // PAGEUP
        { 105, 80 },  // LEFT
        { 106, 79 },  // RIGHT
        { 107, 77 },  // END
        { 108, 81 },  // DOWN
        { 109, 78 },  // PAGEDOWN
        { 110, 73 },  // INSERT
        { 111, 76 },  // DELETE
        { 117, 103 }, // KPEQUAL
        { 119, 72 },  // PAUSE
        { 121, 133 }, // KPCOMMA
        { 124, 137 }, // YEN (international_3)
        { 127, 101 }, // COMPOSE (application)
        { 183, 104 }, { 184, 105 }, { 185, 106 }, { 186, 107 }, { 187, 108 }, { 188, 109 },  // F13..F18
        { 189, 110 }, { 190, 111 }, { 191, 112 }, { 192, 113 }, { 193, 114 }, { 194, 115 },  // F19..F24
    };

    inline constexpr uint16_t HidFromEvdev(uint32_t evdev)
    {
        for (const EvdevKey& k : kEvdevKeys)
            if (k.evdev == evdev)
                return settings::keynames::Known(k.hid) ? k.hid : settings::keynames::kUnknown;
        return settings::keynames::kUnknown;
    }

    inline constexpr uint16_t HidFromXkb(uint32_t nativeScanCode)
    {
        return nativeScanCode < 8 ? settings::keynames::kUnknown : HidFromEvdev(nativeScanCode - 8);
    }
}
