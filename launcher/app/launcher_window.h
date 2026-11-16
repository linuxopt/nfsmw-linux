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
#include <functional>
#include <string>
#include <vector>

#include <user/settings.h>
#include <app_menu.h>

class QCheckBox;
class QPushButton;
class QLabel;
class QTabWidget;

namespace launcher
{
    // speedbreaker-launcher's exit codes, which AppRun acts on.
    inline constexpr int kExitClosed = 0;  // the window was closed: nothing to start
    inline constexpr int kExitPlay = 10;   // Play: AppRun starts the game

    // Before settings::Load, as the game does (runtime/main.cpp
    // SetReleaseDefaults): an installed PAL disc's language is the Language
    // row's default (launcher/core/release.h), so the launcher shows and
    // saves it as the game would.
    void ApplyReleaseDefaults(const std::filesystem::path& userDir);

    // Before the QApplication: AppRun passes Qt's plugin folder (the
    // AppImage's lib/qt/plugins) as SB_QT_PLUGINS, not QT_PLUGIN_PATH, which
    // every program the launcher opens (the file manager for Open Backups
    // Folder) would inherit and load the AppImage's plugins into its own Qt.
    // Makes it Qt's library path and removes it from the environment.
    void TakePluginPath();
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
    // A yes/cancel question and a warning, through the hooks when set (the
    // tests answer them; no dialog opens).
    bool confirm(const QString& question);
    void message(const QString& text);
    void inform(const QString& text);  // news, not a warning (the same hook)
    std::function<bool(const QString&)> confirmHook;
    std::function<void(const QString&)> messageHook;
    // `widget` shows `text` in the help line when hovered or focused.
    void addHelp(QWidget* widget, const QString& text);

signals:
    void playRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    bool shown(const settings::Option& o) const;
    QWidget* categoryPage(settings::Category category);
    void play();
    // Add to App Menu (launcher/core/app_menu.h): its label for the entry's state, and the click.
    void showAppMenuState();
    void toggleAppMenu();
    std::vector<uint8_t> menuIcon() const;  // the game's icon, else SpeedBreaker's

    std::filesystem::path m_userDir;
    QTabWidget* m_tabs = nullptr;
    QLabel* m_help = nullptr;
    QCheckBox* m_showAtStart = nullptr;
    class OnlineTab* m_online = nullptr;
    QPushButton* m_appMenu = nullptr;
    std::string m_appImage;               // $APPIMAGE: the AppImage this runs from ("" outside one)
    launcher::app_menu::Paths m_menuPaths; // where its entry goes (with m_appMenu)
    std::vector<uint8_t> m_titleIcon;     // the game's own icon (a PNG), or empty
};
