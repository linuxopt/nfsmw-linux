// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// Settings > Controls as the launcher shows it: the keyboard as an Xbox 360
// controller, one row per control (hid/key_map.h kEntries, in the game's
// page order), each with its two keys. Click a key, then press the new one:
// Esc cancels; a key the game can't use (not in user/key_names.h, or pressed
// with Super held) is refused and the wait goes on, as in the game's menu.
// Right-click a key to clear it. The help line names the other controls
// sharing a row's keys ("Also: ..."), as the game's does.
#pragma once
#include <QWidget>

#include <user/settings.h>

class LauncherWindow;
class QPushButton;

class ControlsTab : public QWidget
{
    Q_OBJECT
public:
    explicit ControlsTab(LauncherWindow* window);

    // The text a key button shows for a stored key code.
    static QString KeyLabel(double stored);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void startWait(settings::Id id);
    void endWait();
    void refresh();
    QString rowHelp(settings::Id id) const;

    LauncherWindow* m_window;
    settings::Id m_waiting = settings::Id::Count;
    QPushButton* m_buttons[settings::kCount]{};
};
