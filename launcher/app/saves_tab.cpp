// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See saves_tab.h.
#include "saves_tab.h"

#include "launcher_window.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>
#include <set>

#include <backups.h>
#include <sbsave.h>

namespace fs = std::filesystem;
using namespace launcher;

namespace
{
    QString When(fs::file_time_type t)
    {
        auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
        qint64 ms = std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count();
        return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms), QLocale::ShortFormat);
    }

    QString Path(const fs::path& p)
    {
        return QString::fromStdString(p.string());
    }

    fs::path FromQ(const QString& s)
    {
        return fs::path(s.toStdString());
    }
}

SavesTab::SavesTab(LauncherWindow* window) : m_window(window)
{
    auto* layout = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    m_list->setObjectName("saves_list");
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_list, &QListWidget::currentRowChanged, this, &SavesTab::selectionChanged);
    layout->addWidget(m_list, 1);
    m_empty = new QLabel("No saves yet: they appear here after you save in the game.", this);
    m_empty->setObjectName("saves_empty");
    m_empty->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_empty);

    auto* bar = new QHBoxLayout;
    auto add = [&](const char* text, const char* name, void (SavesTab::*slot)()) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(name);
        connect(b, &QPushButton::clicked, this, slot);
        bar->addWidget(b);
        return b;
    };
    m_backup = add("Back Up", "backup", &SavesTab::backUp);
    m_restore = add("Restore…", "restore", &SavesTab::restore);
    m_export = add("Export…", "export", &SavesTab::exportSave);
    add("Import…", "import", &SavesTab::importSave);
    m_delete = add("Delete…", "delete", &SavesTab::remove);
    bar->addStretch(1);
    auto* open = new QPushButton("Open Backups Folder", this);
    open->setObjectName("open_backups");
    connect(open, &QPushButton::clicked, this, [this] {
        fs::path dir = backups::Dir(m_window->userDir());
        std::error_code ec;
        fs::create_directories(dir, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(Path(dir)));
    });
    bar->addWidget(open);
    layout->addLayout(bar);
    refresh();
}

void SavesTab::refresh()
{
    m_profiles = saves::List(m_window->userDir());
    std::set<std::string> folders;
    for (const saves::Profile& p : m_profiles)
        folders.insert(p.folder);
    m_list->clear();
    for (const saves::Profile& p : m_profiles)
    {
        QString title = QString::fromStdString(p.displayName.empty() ? p.name : p.displayName);
        if (!p.displayName.empty() && p.displayName != p.name)
            title += QString("  (%1)").arg(QString::fromStdString(p.name));
        if (folders.size() > 1)
            title += QString("  · %1").arg(saves::ReleaseLabel(p.folder));
        m_list->addItem(title + "\nLast played " + When(p.lastPlayed));
    }
    m_empty->setVisible(m_profiles.empty());
    m_list->setVisible(!m_profiles.empty());
    if (!m_profiles.empty())
        m_list->setCurrentRow(0);
    selectionChanged();
}

const saves::Profile* SavesTab::selected() const
{
    int row = m_list->currentRow();
    return row >= 0 && size_t(row) < m_profiles.size() ? &m_profiles[size_t(row)] : nullptr;
}

void SavesTab::selectionChanged()
{
    const bool any = selected() != nullptr;
    for (QPushButton* b : { m_backup, m_restore, m_export, m_delete })
        b->setEnabled(any);
}

void SavesTab::backUp()
{
    const saves::Profile* p = selected();
    if (!p)
        return;
    std::string error;
    if (auto out = backups::Backup(m_window->userDir(), *p, std::chrono::system_clock::now(), error))
        m_window->describe("Backed up as " + Path(out->filename()) + " in the backups folder.");
    else
        m_window->message("The save couldn't be backed up: " + QString::fromStdString(error));
}

void SavesTab::restore()
{
    const saves::Profile* p = selected();
    if (!p)
        return;
    std::vector<backups::Entry> list = backups::List(m_window->userDir(), *p);
    if (list.empty())
    {
        m_window->message("There are no backups of " + QString::fromStdString(p->name) + " yet.");
        return;
    }
    QStringList names;
    for (const backups::Entry& e : list)
        names << When(e.when) + "  (" + Path(e.path.filename()) + ")";
    bool ok = false;
    QString pick = QInputDialog::getItem(this, "Restore", "Restore " + QString::fromStdString(p->name) + " from the backup of:",
        names, 0, false, &ok);
    if (!ok)
        return;
    const backups::Entry& chosen = list[size_t(names.indexOf(pick))];
    if (!m_window->confirm("Replace " + QString::fromStdString(p->name) + " with the backup from " + When(chosen.when) +
                           "? The save there now is backed up first."))
        return;
    std::string error;
    if (!backups::Replace(m_window->userDir(), chosen.path, std::chrono::system_clock::now(), error))
        m_window->message(QString::fromStdString(error));
    refresh();
}

void SavesTab::exportSave()
{
    const saves::Profile* p = selected();
    if (!p)
        return;
    QString to = QFileDialog::getSaveFileName(this, "Export Save", QDir::home().filePath(QString::fromStdString(p->name) + ".sbsave"),
        "SpeedBreaker saves (*.sbsave)");
    if (to.isEmpty())
        return;
    if (!to.endsWith(".sbsave"))
        to += ".sbsave";
    std::string error;
    if (sbsave::Write(m_window->userDir(), *p, FromQ(to), error))
        m_window->describe("Exported to " + to + ". Import it on another machine from this tab.");
    else
        m_window->message("The save couldn't be exported: " + QString::fromStdString(error));
}

void SavesTab::importSave()
{
    QString from = QFileDialog::getOpenFileName(this, "Import Save", QDir::homePath(), "SpeedBreaker saves (*.sbsave)");
    if (from.isEmpty())
        return;
    sbsave::Info info;
    std::string error;
    if (!sbsave::Inspect(FromQ(from), info, error))
    {
        m_window->message(QString::fromStdString(error));
        return;
    }
    const fs::path folder = m_window->userDir() / info.folder;
    std::error_code ec;
    if ((fs::exists(folder / info.name, ec) || fs::exists(folder / (info.name + ".xcontent"), ec)) &&
        !m_window->confirm("Replace " + QString::fromStdString(info.name) + "? It will be backed up first."))
        return;
    if (!backups::Replace(m_window->userDir(), FromQ(from), std::chrono::system_clock::now(), error))
        m_window->message(QString::fromStdString(error));
    else
        m_window->describe("Imported " + QString::fromStdString(info.name) + " (" + saves::ReleaseLabel(info.folder) + ").");
    refresh();
}

void SavesTab::remove()
{
    const saves::Profile* p = selected();
    if (!p)
        return;
    if (!m_window->confirm("Delete " + QString::fromStdString(p->name) + "? A backup is kept in the backups folder."))
        return;
    std::string error;
    if (!backups::Remove(m_window->userDir(), *p, std::chrono::system_clock::now(), error))
        m_window->message(QString::fromStdString(error));
    refresh();
}
