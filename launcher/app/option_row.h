// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// One settings row's editor, from the game's option table (user/settings.h):
// a check box for a Bool, a list for an Enum, a slider with the game's own
// value text for an Int or Float. It shows the stored value (what the game
// saves, not an environment override) and stores each change with
// settings::Set. Its object name is "<table>.<key>" (as in settings.toml).
#pragma once
#include <QWidget>

#include <user/settings.h>

namespace launcher
{
    QWidget* MakeEditor(const settings::Option& o, QWidget* parent);
}
