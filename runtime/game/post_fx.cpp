// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See post_fx.h.
#include <stdafx.h>
#include "post_fx.h"

#include <kernel/function.h>
#include <region/region.h>
#include <user/settings.h>

#include <bit>

// The treatment's curve and constants, each frame: the black point scaled by
// Haze for this call only.
extern "C" PPC_FUNC(__imp__sub_82223BB8);
SB_HOOK(sub_82223BB8)
{
    const int percent = settings::GetInt(settings::Id::Haze);
    if (percent >= 100)
    {
        SB_ORIGINAL(sub_82223BB8)(ctx, base);
        return;
    }
    const uint32_t field = ctx.r3.u32 + game::post_fx::kCurveBlack;
    const uint32_t game = PPC_LOAD_U32(field);
    PPC_STORE_U32(field, std::bit_cast<uint32_t>(game::post_fx::HazeBlack(std::bit_cast<float>(game), percent)));
    SB_ORIGINAL(sub_82223BB8)(ctx, base);
    PPC_STORE_U32(field, game);
}
