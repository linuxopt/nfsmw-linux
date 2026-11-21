// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Requests from SpeedBreaker's own menu to the game's front end: open one of
// its screens (some the retail game never opens: the developers' car
// customizer, the PC release's display options) or play one of its movies.
//
// The front end opens a screen by queueing its package (an .fng in
// FRONTEND\FRONTB.LZC) with cFEng::QueuePackagePush (sub_82284910: the
// cFEng object, whose pointer is at 0x82A2C5C4, the package name, three
// zeros), as every menu does. A movie plays through FEAnyMovie.fng:
// sub_822869C0(the screen to come back to, the movie's name: "blacklist_01",
// "storyfmv_bla134" ...) keeps both names and queues it (the Blacklist's
// rival bio does it so, sub_8224E538). The screen table at 0x828FF220 still
// lists UI_DebugCarCustomize.fng (sub_822D5670) and OptionsPCDisplay.fng (the
// Options class, sub_822D4710), and their packages are on the disc.
//
// The requests wait in a queue for the game's own thread: the frame
// (sub_823AFE20, once per pass of the main loop) takes them before it runs,
// and only while the front end is up (the game flow state, 0x82A39AD8, is 3
// at the menus: 0 and 1 while booting; cFEng::QueuePackagePush tests it for
// 6, another state): in a race they wait.
//
// The PAL releases' copies of the two front-end functions are found by
// region::Addr; before the first call the runtime checks that the running
// release's code there starts as the USA function does (branch targets and
// address halves aside) and otherwise turns this off, with a line in the log.
//
// NFSMW_FE_ACTIONS="45:screen:UI_DebugCarCustomize.fng,60:movie:blacklist_01"
// queues requests at seconds of guest time since launch (automated tests).
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace game::fe_actions
{
    constexpr uint32_t kQueuePackagePush = 0x82284910;
    constexpr uint32_t kLaunchMovie = 0x822869C0;
    constexpr uint32_t kFengPointer = 0x82A2C5C4;
    constexpr uint32_t kGameFlowState = 0x82A39AD8;
    constexpr uint32_t kFrontEndState = 3;

    // Any thread: queue a request. False when there is no front end to take
    // it (the release's code didn't check out).
    bool OpenScreen(std::string_view package);
    bool PlayMovie(std::string_view movie);

    // Any thread: the front end was up at the last frame (the menu greys its
    // rows otherwise).
    bool FrontEndUp();
    // Any thread: the requests can work on this release at all.
    bool Available();

    // The game's movies, as the front end names them: the 15 rival intros,
    // the 9 story scenes, the tutorials, and the attract and logo movies.
    struct Movie
    {
        const char* name;   // the file's stem (Movies/<name>_english_ntsc.wmv)
        const char* label;  // what the menu shows
    };
    std::span<const Movie> Movies();

    // An instruction compare that ignores what differs between releases:
    // a branch's target and an address half (tests).
    bool SameCode(std::span<const uint32_t> expected, std::span<const uint32_t> actual);
}
