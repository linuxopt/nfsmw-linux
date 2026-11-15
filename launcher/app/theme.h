// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// The launcher's look: always dark (Qt's Fusion style with a dark palette),
// whatever the desktop's theme, so it looks the same on every machine the
// AppImage goes to. Play stands out in the accent colour.
#pragma once

class QApplication;

namespace launcher
{
    // Call once, after the QApplication exists and before any window.
    void ApplyDarkTheme(QApplication& app);
}
