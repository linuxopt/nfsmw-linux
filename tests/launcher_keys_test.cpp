// The desktop launcher's key capture (launcher/core/keys.h): a key press's
// Linux key code (evdev, or XKB's = evdev + 8, as Qt reports it) to the USB
// HID usage settings.toml stores (user/key_names.h), for exactly the keys a
// Controls row can hold.
#include <keys.h>

#include <cstdio>
#include <user/key_names.h>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main()
{
    using namespace launcher::keys;
    using settings::keynames::kKeys;
    using settings::keynames::kUnknown;

    // Every bindable key comes from exactly one Linux key code, except
    // non_us_hash: Linux reports that key (HID 50) as KEY_BACKSLASH, as SDL
    // does, so it can't be told apart.
    for (const auto& k : kKeys)
    {
        int from = 0;
        for (uint32_t e = 0; e < 768; e++)
            from += HidFromEvdev(e) == k.code;
        if (k.code == 50)
            CHECK(from == 0, "non_us_hash has no Linux key code of its own (%d)", from);
        else
            CHECK(from == 1, "%s (%u) from %d key codes", k.token, unsigned(k.code), from);
    }
    // Nothing outside the table.
    for (uint32_t e = 0; e < 768; e++)
    {
        uint16_t h = HidFromEvdev(e);
        CHECK(h == kUnknown || settings::keynames::Known(h), "evdev %u gives %u, not a bindable key", e, unsigned(h));
    }
    CHECK(HidFromEvdev(17) == 26, "KEY_W is w");
    CHECK(HidFromEvdev(42) == 225, "KEY_LEFTSHIFT is left_shift");
    CHECK(HidFromEvdev(1) == 41, "KEY_ESC is escape");
    CHECK(HidFromEvdev(125) == kUnknown, "KEY_LEFTMETA isn't bindable");
    CHECK(HidFromEvdev(59) == kUnknown, "KEY_F1 isn't bindable (it opens the in-game menu)");
    CHECK(HidFromEvdev(60) == 59, "KEY_F2 is f2");
    CHECK(HidFromEvdev(183) == 104 && HidFromEvdev(194) == 115, "F13 and F24");
    CHECK(HidFromEvdev(96) == 88, "KEY_KPENTER is keypad_enter");
    CHECK(HidFromXkb(25) == 26, "XKB 25 is evdev 17, w");
    CHECK(HidFromXkb(9) == 41, "XKB 9 is Esc");
    CHECK(HidFromXkb(0) == kUnknown && HidFromXkb(7) == kUnknown, "XKB codes below 8 are no key");
    CHECK(HidFromEvdev(100000) == kUnknown, "out of range");

    if (g_failures)
        printf("launcher_keys_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_keys_test: all passed\n");
    return g_failures ? 1 : 0;
}
