// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// speedbreaker-launcher [--launcher]: the window before the game (the Linux
// AppImage's AppRun runs it). It never starts the game itself: it exits
// with launcher::kExitPlay for Play and kExitClosed when the window is
// closed, and AppRun starts the game (also when this fails to start at all).
// With "Open this launcher at start" off it exits with kExitPlay at once,
// before any window, unless --launcher asks for the window.
#include "launcher_window.h"
#include "theme.h"

#include <QApplication>
#include <QMessageBox>
#include <QPushButton>

#include <cstdio>
#include <cstring>

#include <user/paths.h>
#include <user/settings.h>

int main(int argc, char** argv)
{
    bool forced = false;
    for (int i = 1; i < argc; i++)
        forced |= std::strcmp(argv[i], "--launcher") == 0;

    // First, as the game does: a machine still on the old nfsmw-recomp data
    // folder must not get a second, new one from the launcher.
    if (std::string moved = MigrateUserFolders(); !moved.empty())
        fputs(moved.c_str(), stderr);
    launcher::ApplyReleaseDefaults(GetUserPath());
    const bool loaded = settings::Load();
    if (loaded && !forced && !settings::GetBool(settings::Id::LauncherShowAtStart))
        return launcher::kExitPlay;

    QApplication app(argc, argv);
    launcher::ApplyDarkTheme(app);
    QApplication::setApplicationName("SpeedBreaker");
    QGuiApplication::setDesktopFileName("speedbreaker");

    if (!loaded)
    {
        // A settings.toml that doesn't read: the game keeps it as .bad and
        // starts on defaults; the launcher doesn't write over it.
        QMessageBox box(QMessageBox::Warning, "SpeedBreaker",
            QString::fromStdString(settings::LastError()) + "\n\nThe game will start with its default settings.");
        QPushButton* play = box.addButton("Play", QMessageBox::AcceptRole);
        box.addButton("Quit", QMessageBox::RejectRole);
        box.exec();
        return box.clickedButton() == play ? launcher::kExitPlay : launcher::kExitClosed;
    }

    LauncherWindow window(GetUserPath());
    QObject::connect(&window, &LauncherWindow::playRequested, &app, [&app] { app.exit(launcher::kExitPlay); });
    window.resize(860, 620);
    window.show();
    return app.exec();
}
