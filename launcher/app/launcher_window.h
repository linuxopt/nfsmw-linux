// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// The launcher's window: one tab per settings category, built from the
// game's own option table (user/settings.h Options()), the rows the game's
// menu would show (launcher/core/rules.h), a help line for the row under the
// mouse or focus, and Play. Saving writes only what was changed
// (settings::SaveTouched); a settings.toml the launcher creates also records
// this version, so the game doesn't take a new player for an updated one.
#pragma once
#include <QWidget>

#include <filesystem>

#include <user/settings.h>

class QCheckBox;
class QLabel;
class QTabWidget;

namespace launcher
{
    // speedbreaker-launcher's exit codes, which AppRun acts on.
    inline constexpr int kExitClosed = 0;  // the window was closed: nothing to start
    inline constexpr int kExitPlay = 10;   // Play: AppRun starts the game
}

class LauncherWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LauncherWindow(std::filesystem::path userDir, QWidget* parent = nullptr);
    // Saves when something changed; false (and settings::LastError) when
    // the save failed.
    bool saveIfChanged();
    QTabWidget* tabs() const { return m_tabs; }
    const std::filesystem::path& userDir() const { return m_userDir; }
    // The help line: `text`, or the default hint for "".
    void describe(const QString& text);

signals:
    void playRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    bool shown(const settings::Option& o) const;
    QWidget* categoryPage(settings::Category category);
    void play();

    std::filesystem::path m_userDir;
    QTabWidget* m_tabs = nullptr;
    QLabel* m_help = nullptr;
    QCheckBox* m_showAtStart = nullptr;
};
