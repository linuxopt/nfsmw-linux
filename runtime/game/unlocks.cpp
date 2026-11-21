// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See unlocks.h.
#include <stdafx.h>
#include "unlocks.h"

#include <kernel/memory.h>
#include <region/region.h>
#include <user/settings.h>

#include <cstdio>

namespace
{
    void Apply()
    {
        const bool on = settings::GetBool(settings::Id::UnlockEverything);
        auto* flag = static_cast<uint8_t*>(g_memory.Translate(region::Addr(game::unlocks::kUnlockAllThings)));
        if (*flag == uint8_t(on))
            return;
        *flag = uint8_t(on);
        fprintf(stderr, "[unlocks] Unlock Everything %s\n", on ? "on: every car, part, track and event" : "off: the game's own unlocks");
    }
}

void game::unlocks::Start()
{
    Apply();
    settings::Subscribe([](settings::Id id) {
        if (id == settings::Id::UnlockEverything)
            Apply();
    });
}
