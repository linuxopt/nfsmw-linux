// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// The Saves tab: the player's save profiles (launcher/core/saves.h), and
// Back Up, Restore, Export, Import and Delete (launcher/core/backups.h,
// sbsave.h). Replacing or deleting a profile backs it up first, always.
#pragma once
#include <QWidget>

#include <vector>

#include <saves.h>

class LauncherWindow;
class QLabel;
class QListWidget;
class QPushButton;

class SavesTab : public QWidget
{
    Q_OBJECT
public:
    explicit SavesTab(LauncherWindow* window);
    void refresh();

private:
    const launcher::saves::Profile* selected() const;
    void backUp();
    void restore();
    void exportSave();
    void importSave();
    void remove();
    void selectionChanged();

    LauncherWindow* m_window;
    std::vector<launcher::saves::Profile> m_profiles;
    QListWidget* m_list = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_backup = nullptr;
    QPushButton* m_restore = nullptr;
    QPushButton* m_export = nullptr;
    QPushButton* m_delete = nullptr;
};
