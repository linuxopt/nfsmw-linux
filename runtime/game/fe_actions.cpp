// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See fe_actions.h.
#include <stdafx.h>
#include "fe_actions.h"

#include <cpu/guest_time.h>
#include <kernel/heap.h>
#include <kernel/memory.h>
#include <region/region.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace
{
    using namespace game::fe_actions;

    constexpr Movie kMovies[] = {
        { "blacklist_15", "Blacklist 15: Sonny" },
        { "blacklist_14", "Blacklist 14: Taz" },
        { "blacklist_13", "Blacklist 13: Vic" },
        { "blacklist_12", "Blacklist 12: Izzy" },
        { "blacklist_11", "Blacklist 11: Big Lou" },
        { "blacklist_10", "Blacklist 10: Baron" },
        { "blacklist_09", "Blacklist 9: Earl" },
        { "blacklist_08", "Blacklist 8: Jewels" },
        { "blacklist_07", "Blacklist 7: Kaze" },
        { "blacklist_06", "Blacklist 6: Ming" },
        { "blacklist_05", "Blacklist 5: Webster" },
        { "blacklist_04", "Blacklist 4: JV" },
        { "blacklist_03", "Blacklist 3: Ronnie" },
        { "blacklist_02", "Blacklist 2: Bull" },
        { "blacklist_01", "Blacklist 1: Razor" },
        { "storyfmv_rac01", "Story: rac01" },
        { "storyfmv_raz08", "Story: raz08" },
        { "storyfmv_cro06_coh06a", "Story: cro06" },
        { "storyfmv_pin11", "Story: pin11" },
        { "storyfmv_bus12", "Story: bus12" },
        { "storyfmv_saf25", "Story: saf25" },
        { "storyfmv_rap30", "Story: rap30" },
        { "storyfmv_bla134", "Story: bla134" },
        { "storyfmv_her136", "Story: her136" },
        { "attract_movie", "Attract movie" },
        { "bounty_tutorial", "Tutorial: Bounty" },
        { "drag_tutorial", "Tutorial: Drag" },
        { "pursuit_tutorial", "Tutorial: Pursuit" },
        { "speedtrap_tutorial", "Tutorial: Speedtrap" },
        { "tollbooth_tutorial", "Tutorial: Tollbooth" },
        { "ealogo", "EA logo" },
        { "eahd_bumper", "EA HD bumper" },
        { "psa", "Public service announcement" },
    };

    // The USA functions' first instructions (SameCode compares them).
    constexpr uint32_t kPushCode[] = { 0x7D8802A6, 0x48439211, 0x9421FF80, 0x3D6082A4, 0x7C7D1B78, 0x396B9AB8, 0x7C9F2378, 0x7CBC2B78 };
    constexpr uint32_t kMovieCode[] = { 0x3D608291, 0x39400040, 0x396B7D90, 0x7D0B1850, 0x7D2858AE, 0x394AFFFF, 0x2B090000, 0x992B0000 };

    // The screen the movie screen comes back to when its movie ends.
    constexpr const char* kReturnScreen = "MainMenu.fng";

    struct Request
    {
        bool movie;
        std::string name;
    };

    std::mutex s_lock;
    std::deque<Request> s_queue;
    std::map<std::string, uint32_t> s_names;  // guest copies of the names (the push keeps the pointer)
    std::atomic<bool> s_frontEnd{ false };
    std::atomic<int> s_checked{ 0 };  // 0 not yet, 1 good, -1 not this release's code

    // NFSMW_FE_ACTIONS, sorted by time.
    struct Timed
    {
        double at;
        Request request;
    };
    std::vector<Timed> s_timed;
    size_t s_nextTimed = 0;
    const int64_t s_start = guesttime::NowNs();

    void ParseTimed()
    {
        const char* v = std::getenv("NFSMW_FE_ACTIONS");
        if (!v || !*v)
            return;
        std::string_view all(v);
        while (!all.empty())
        {
            const size_t comma = all.find(',');
            std::string_view item = all.substr(0, comma);
            all = comma == std::string_view::npos ? std::string_view() : all.substr(comma + 1);
            const size_t c1 = item.find(':'), c2 = c1 == std::string_view::npos ? c1 : item.find(':', c1 + 1);
            if (c2 == std::string_view::npos)
                continue;
            const std::string kind(item.substr(c1 + 1, c2 - c1 - 1));
            s_timed.push_back({ std::atof(std::string(item.substr(0, c1)).c_str()), { kind == "movie", std::string(item.substr(c2 + 1)) } });
        }
        std::sort(s_timed.begin(), s_timed.end(), [](const Timed& a, const Timed& b) { return a.at < b.at; });
    }

    bool CodeAt(uint32_t guest, std::span<const uint32_t> expected)
    {
        std::vector<uint32_t> actual(expected.size());
        const auto* p = static_cast<const uint8_t*>(g_memory.Translate(guest));
        for (size_t i = 0; i < actual.size(); i++)
            actual[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        return SameCode(expected, actual) && g_memory.FindFunction(guest) != nullptr;
    }

    bool Check()
    {
        int checked = s_checked.load(std::memory_order_acquire);
        if (checked == 0)
        {
            const bool good = CodeAt(region::Addr(kQueuePackagePush), kPushCode) && CodeAt(region::Addr(kLaunchMovie), kMovieCode);
            checked = good ? 1 : -1;
            s_checked.store(checked, std::memory_order_release);
            if (!good)
                fprintf(stderr, "[fe] this release's front-end code isn't where it's expected: the menu's screens and movies are off\n");
        }
        return checked > 0;
    }

    uint32_t GuestName(const std::string& name)
    {
        auto it = s_names.find(name);
        if (it != s_names.end())
            return it->second;
        void* host = g_userHeap.Alloc(name.size() + 1);
        if (!host)
            return 0;
        std::memcpy(host, name.c_str(), name.size() + 1);
        const uint32_t guest = g_memory.MapVirtual(host);
        s_names.emplace(name, guest);
        return guest;
    }

    void Call(PPCContext& ctx, uint8_t* base, uint32_t guest)
    {
        g_memory.FindFunction(guest)(ctx, base);
    }

    // The game's thread, at the start of a frame.
    void Service(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t state = PPC_LOAD_U32(region::Addr(kGameFlowState));
        static uint32_t s_lastState = ~0u;
        if (state != s_lastState && std::getenv("NFSMW_FE_ACTIONS"))
            fprintf(stderr, "[fe] game flow state %u\n", state);
        s_lastState = state;
        const bool frontEnd = state == kFrontEndState;
        s_frontEnd.store(frontEnd, std::memory_order_relaxed);
        if (s_nextTimed < s_timed.size() && double(guesttime::NowNs() - s_start) / 1e9 >= s_timed[s_nextTimed].at)
        {
            std::lock_guard lock(s_lock);
            s_queue.push_back(s_timed[s_nextTimed++].request);
        }
        if (!frontEnd)
            return;
        Request request;
        {
            std::lock_guard lock(s_lock);
            if (s_queue.empty())
                return;
            request = std::move(s_queue.front());
            s_queue.pop_front();
        }
        if (!Check())
            return;
        const PPCContext saved = ctx;
        const uint32_t name = GuestName(request.name);
        if (request.movie)
        {
            const uint32_t back = GuestName(kReturnScreen);
            if (name && back)
            {
                ctx.r3.u64 = back;
                ctx.r4.u64 = name;
                Call(ctx, base, region::Addr(kLaunchMovie));
                fprintf(stderr, "[fe] playing the movie %s\n", request.name.c_str());
            }
        }
        else if (name)
        {
            ctx.r3.u64 = PPC_LOAD_U32(region::Addr(kFengPointer));
            ctx.r4.u64 = name;
            ctx.r5.u64 = 0;
            ctx.r6.u64 = 0;
            ctx.r7.u64 = 0;
            Call(ctx, base, region::Addr(kQueuePackagePush));
            fprintf(stderr, "[fe] opening the screen %s\n", request.name.c_str());
        }
        ctx = saved;
    }

    bool Queue(bool movie, std::string_view name)
    {
        if (s_checked.load(std::memory_order_acquire) < 0)
            return false;
        std::lock_guard lock(s_lock);
        s_queue.push_back({ movie, std::string(name) });
        return true;
    }

    const bool s_parsed = [] {
        ParseTimed();
        return true;
    }();
}

bool game::fe_actions::OpenScreen(std::string_view package) { return Queue(false, package); }
bool game::fe_actions::PlayMovie(std::string_view movie) { return Queue(true, movie); }
bool game::fe_actions::FrontEndUp() { return s_frontEnd.load(std::memory_order_relaxed); }
bool game::fe_actions::Available() { return s_checked.load(std::memory_order_acquire) >= 0; }
std::span<const game::fe_actions::Movie> game::fe_actions::Movies() { return kMovies; }

bool game::fe_actions::SameCode(std::span<const uint32_t> expected, std::span<const uint32_t> actual)
{
    if (expected.size() != actual.size())
        return false;
    for (size_t i = 0; i < expected.size(); i++)
    {
        const uint32_t opcode = expected[i] >> 26;
        uint32_t mask = 0xFFFFFFFF;
        if (opcode == 18)  // b, bl: the target
            mask = 0xFC000003;
        else if (opcode == 14 || opcode == 15)  // addi, lis: an address half
            mask = 0xFFFF0000;
        if ((expected[i] & mask) != (actual[i] & mask))
            return false;
    }
    return true;
}

// The frame (sub_823AFE20), once per pass of the main loop: the queued
// requests first, on the game's thread.
extern "C" PPC_FUNC(__imp__sub_823AFE20);
SB_HOOK(sub_823AFE20)
{
    Service(ctx, base);
    SB_ORIGINAL(sub_823AFE20)(ctx, base);
}
