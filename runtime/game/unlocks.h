// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Unlock Everything (Settings > Game): every car, part, track and event the
// game can unlock, available from the start, through the game's own switch.
//
// The game's unlock checks (the PC release's UnlockSystem: IsCarUnlocked and
// its siblings, sub_822E9E80, sub_822E9F50, sub_822EA038, sub_822EA0E8, and
// the race check sub_8233A4C0) each take an unlock filter (1 career, 2 Quick
// Race, 7 all) and, before anything else, return "unlocked" when one byte is
// set: 0x82A2CE00, the PC release's UnlockAllThings (the PC mods' Unlock All
// Things option sets it). About 30 other places read it too: the front end's
// car and track lists among them.
//
// In the retail game nothing sets it: only the cheat code handler's type 0
// (sub_823C4B88: the cheat codes are a table of three, 0x8290DA90, of types
// 3, 4 and 5, the Burger King Challenge, the Castrol Ford GT and Junkman
// parts: no code has type 0) and the E3 demo mode (sub_82237900, when the
// word at 0x82A2CE08 is set) write 1, and the input replay log
// (sub_823CE498) carries it through a recording. It isn't in the save, so
// clearing it puts the game back as it was.
//
// The runtime sets it from the setting as the game starts (before any guest
// code runs) and whenever the setting changes.
//
// NFSMW_UNLOCK_ALL=1/0 overrides the setting for one run.
#pragma once
#include <cstdint>

namespace game::unlocks
{
    constexpr uint32_t kUnlockAllThings = 0x82A2CE00;  // bool (.data)

    // Main thread, once the game's image is in guest memory and before its
    // entry point runs: applies the setting now and on every change.
    void Start();
}
