// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// The Online tab: online play on or off, the player's name and the server
// (online.toml, launcher/core/online.h). Saved with the rest on Play and
// Close, only when something changed.
#pragma once
#include <QWidget>

#include <online.h>

class LauncherWindow;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class OnlineTab : public QWidget
{
    Q_OBJECT
public:
    explicit OnlineTab(LauncherWindow* window);
    // Writes online.toml when something changed. False, with the reason in
    // `error`, when a field can't be used or the file can't be written.
    bool save(QString& error);
    // What's on screen, as it would be saved.
    launcher::online::Config current() const;

private:
    void updateState();

    LauncherWindow* m_window;
    launcher::online::Config m_loaded;
    bool m_broken = false;   // online.toml didn't parse: left alone unless changed here
    QCheckBox* m_enabled = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_server = nullptr;
    QPushButton* m_default = nullptr;
    QLabel* m_note = nullptr;
};
