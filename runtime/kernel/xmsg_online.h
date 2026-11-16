// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// XAM's online message apps for online races (xmsg_online.cpp): XGI (0xFB:
// user context, sessions, stats) and XLiveBase (0xFC: logon, service info).
#pragma once
#include <cstdint>

uint32_t XgiDispatch(uint32_t message, uint32_t buffer, uint32_t length);
uint32_t XLiveBaseDispatch(uint32_t message, uint32_t buffer, uint32_t length);
