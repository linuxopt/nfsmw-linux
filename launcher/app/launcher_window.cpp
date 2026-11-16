// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See launcher_window.h.
#include "launcher_window.h"

#include "controls_tab.h"
#include "option_row.h"
#include "saves_tab.h"
#include "online_tab.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <vector>

#include <cstdlib>
#include <format>

#include <app_menu.h>
#include <release.h>
#include <install/title_icon.h>
#include <rules.h>

void launcher::ApplyReleaseDefaults(const std::filesystem::path& userDir)
{
    const char* gameDir = std::getenv("NFSMW_GAME_DIR");
    const int32_t language = release::DiscLanguageDefault(userDir, gameDir);
    if (language >= 0)
        settings::SetDeviceDefault(settings::Id::GameLanguage, language,
            std::format("the language of the {} disc", release::InstalledRegion(userDir, gameDir)));
}

void launcher::TakePluginPath()
{
    const char* dir = std::getenv("SB_QT_PLUGINS");
    if (!dir || !*dir)
        return;
    QCoreApplication::setLibraryPaths({ QString::fromLocal8Bit(dir) });
    unsetenv("SB_QT_PLUGINS");
}

namespace
{
    const char* kHint = "Point at a setting to see what it does.";
    const char* kDescription = "sb_description";
}

LauncherWindow::LauncherWindow(std::filesystem::path userDir, QWidget* parent) : QWidget(parent), m_userDir(std::move(userDir))
{
    setWindowTitle("SpeedBreaker");
    // The game's own icon (its dashboard art, from the player's default.xex),
    // else SpeedBreaker's; for the dialogs too.
    if (std::optional<std::filesystem::path> game = launcher::release::InstalledGameDir(m_userDir, std::getenv("NFSMW_GAME_DIR")))
        m_titleIcon = install::TitleIconFromXex(*game / "default.xex");
    QPixmap titleIcon;
    if (!m_titleIcon.empty())
        titleIcon.loadFromData(m_titleIcon.data(), uint(m_titleIcon.size()), "PNG");
    const QIcon icon = titleIcon.isNull() ? QIcon(":/AppIcon.png") : QIcon(titleIcon);
    setWindowIcon(icon);
    QApplication::setWindowIcon(icon);
    setMinimumSize(760, 520);

    auto* layout = new QVBoxLayout(this);
    if (launcher::rules::GameRunning("/proc"))
    {
        auto* banner = new QLabel("SpeedBreaker is running: close it first, or its own settings will replace these when it exits.", this);
        banner->setObjectName("running_banner");
        banner->setWordWrap(true);
        layout->addWidget(banner);
    }

    m_tabs = new QTabWidget(this);
    for (size_t c = 0; c < size_t(settings::Category::Count); c++)
    {
        const auto category = settings::Category(c);
        QWidget* page = category == settings::Category::Controls ? new ControlsTab(this) : categoryPage(category);
        if (page)
            m_tabs->addTab(page, QString::fromUtf8(settings::CategoryName(category)));
    }
    m_online = new OnlineTab(this);
    m_tabs->addTab(m_online, "Online");
    m_tabs->addTab(new SavesTab(this), "Saves");
    layout->addWidget(m_tabs, 1);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { describe({}); });

    m_help = new QLabel(this);
    m_help->setObjectName("help");
    m_help->setWordWrap(true);
    m_help->setMinimumHeight(m_help->fontMetrics().lineSpacing() * 3);
    m_help->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    describe({});
    layout->addWidget(m_help);

    auto* bar = new QHBoxLayout;
    m_showAtStart = new QCheckBox("Open this launcher at start", this);
    m_showAtStart->setObjectName("launcher_show_at_start");
    m_showAtStart->setChecked(settings::GetStoredValue(settings::Id::LauncherShowAtStart) != 0.0);
    m_showAtStart->setProperty(kDescription, "Untick to start the game straight away next time. "
                                             "Run the AppImage with --launcher to bring this window back.");
    m_showAtStart->installEventFilter(this);
    connect(m_showAtStart, &QCheckBox::toggled, this, [](bool on) { settings::Set(settings::Id::LauncherShowAtStart, on ? 1.0 : 0.0); });
    bar->addWidget(m_showAtStart);
    bar->addStretch(1);
    // In an AppImage: an entry for it in the desktop's app menu, kept
    // pointing at this AppImage when it has moved or been updated.
    if (const char* appImage = std::getenv("APPIMAGE"); appImage && appImage[0] == '/')
        if (std::optional<std::filesystem::path> data = launcher::app_menu::DataHome(std::getenv("XDG_DATA_HOME"), std::getenv("HOME")))
        {
            m_appImage = appImage;
            m_menuPaths = launcher::app_menu::PathsIn(*data);
            m_appMenu = new QPushButton(this);
            m_appMenu->setObjectName("app_menu");
            addHelp(m_appMenu, "Puts Need for Speed: Most Wanted in your desktop's app menu, with the game's own icon, "
                               "so it starts from there like any other game. Remove takes it out again.");
            connect(m_appMenu, &QPushButton::clicked, this, &LauncherWindow::toggleAppMenu);
            bar->addWidget(m_appMenu);
            std::string error;
            if (launcher::app_menu::Check(m_menuPaths) == launcher::app_menu::State::Ours &&
                !launcher::app_menu::Write(m_menuPaths, m_appImage, menuIcon(), error))
                fprintf(stderr, "[launcher] the app-menu entry: %s\n", error.c_str());
            showAppMenuState();
        }
    auto* playButton = new QPushButton("Play", this);
    playButton->setObjectName("play");
    playButton->setDefault(true);
    playButton->setMinimumWidth(140);
    connect(playButton, &QPushButton::clicked, this, &LauncherWindow::play);
    bar->addWidget(playButton);
    layout->addLayout(bar);
}

bool LauncherWindow::shown(const settings::Option& o) const
{
    if (o.hidden || o.type == settings::Type::Key)
        return false;
    if (o.id == settings::Id::HdTextures)
        return launcher::rules::HdTexturesShown(m_userDir);
    if (o.id == settings::Id::SmoothMotion)
    {
        std::vector<double> rates;
        for (QScreen* s : QGuiApplication::screens())
            rates.push_back(s->refreshRate());
        return launcher::rules::SmoothMotionShown(rates, launcher::rules::SteamDeck("/sys/class/dmi/id"),
            settings::GetStoredValue(o.id) != 0.0);
    }
    return true;
}

QWidget* LauncherWindow::categoryPage(settings::Category category)
{
    if (category == settings::Category::Controls)
        return nullptr;  // its own tab (controls_tab.h)
    std::vector<const settings::Option*> rows;
    for (const settings::Option& o : settings::Options())
        if (o.category == category && shown(o))
            rows.push_back(&o);
    if (rows.empty())
        return nullptr;

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(16);
    int row = 0;
    for (const settings::Option* o : rows)
    {
        QWidget* editor = launcher::MakeEditor(*o, page);
        if (!editor)
            continue;
        auto* label = new QLabel(QString::fromUtf8(o->label), page);
        label->setBuddy(editor);
        QString description = QString::fromUtf8(o->description);
        if (const char* env = settings::EnvOverride(o->id))
        {
            auto* note = new QLabel(QString("Set by %1 for this run").arg(env), page);
            note->setObjectName(QString::fromUtf8(o->key) + "_env");
            note->setStyleSheet("QLabel { color: palette(mid); }");
            grid->addWidget(note, row, 2);
            description += QString("\n%1 is set, so the game uses its value this time; this setting is still saved.").arg(env);
        }
        for (QWidget* w : { static_cast<QWidget*>(label), editor })
        {
            w->setProperty(kDescription, description);
            w->installEventFilter(this);
        }
        for (QWidget* child : editor->findChildren<QWidget*>())
        {
            child->setProperty(kDescription, description);
            child->installEventFilter(this);
        }
        grid->addWidget(label, row, 0);
        grid->addWidget(editor, row, 1);
        row++;
    }
    grid->setRowStretch(row, 1);
    scroll->setWidget(page);
    return scroll;
}

std::vector<uint8_t> LauncherWindow::menuIcon() const
{
    if (!m_titleIcon.empty())
        return m_titleIcon;
    QFile f(":/AppIcon.png");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = f.readAll();
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

void LauncherWindow::showAppMenuState()
{
    if (!m_appMenu)
        return;
    m_appMenu->setText(launcher::app_menu::Check(m_menuPaths) == launcher::app_menu::State::Ours ? "Remove from App Menu" : "Add to App Menu");
}

void LauncherWindow::toggleAppMenu()
{
    std::string error;
    if (launcher::app_menu::Check(m_menuPaths) == launcher::app_menu::State::Ours)
    {
        if (launcher::app_menu::Remove(m_menuPaths, error))
            inform("Need for Speed: Most Wanted is out of the app menu.");
        else
            message(QString::fromStdString("Couldn't remove it from the app menu: " + error));
    }
    else if (launcher::app_menu::Write(m_menuPaths, m_appImage, menuIcon(), error))
        inform("Need for Speed: Most Wanted is in the app menu now (Games). If this AppImage moves, open the launcher "
                "once from its new place and the entry follows it.");
    else
        message(QString::fromStdString("Couldn't add it to the app menu: " + error));
    showAppMenuState();
}

void LauncherWindow::describe(const QString& text)
{
    if (m_help)
        m_help->setText(text.isEmpty() ? QString(kHint) : text);
}

bool LauncherWindow::confirm(const QString& question)
{
    if (confirmHook)
        return confirmHook(question);
    return QMessageBox::question(this, "SpeedBreaker", question, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) ==
           QMessageBox::Yes;
}

void LauncherWindow::message(const QString& text)
{
    if (messageHook)
        messageHook(text);
    else
        QMessageBox::warning(this, "SpeedBreaker", text);
}

void LauncherWindow::inform(const QString& text)
{
    if (messageHook)
        messageHook(text);
    else
        QMessageBox::information(this, "SpeedBreaker", text);
}

bool LauncherWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn)
    {
        QVariant d = watched->property(kDescription);
        if (d.isValid())
            describe(d.toString());
    }
    return QWidget::eventFilter(watched, event);
}

bool LauncherWindow::saveIfChanged()
{
    if (!settings::Dirty())
        return true;
    const bool hadFile = settings::FileFound();
    if (!settings::SaveTouched())
        return false;
    if (!hadFile)
        settings::RecordVersion(SB_VERSION);
    return true;
}

void LauncherWindow::addHelp(QWidget* widget, const QString& text)
{
    widget->setProperty(kDescription, text);
    widget->installEventFilter(this);
}

void LauncherWindow::play()
{
    if (!saveIfChanged())
    {
        message(QString::fromStdString(settings::LastError()));
        return;
    }
    QString error;
    if (!m_online->save(error))
    {
        m_tabs->setCurrentWidget(m_online);
        message(error);
        return;
    }
    emit playRequested();
}

void LauncherWindow::closeEvent(QCloseEvent* event)
{
    if (!saveIfChanged())
    {
        message(QString::fromStdString(settings::LastError()));
        event->ignore();
        return;
    }
    QString error;
    if (!m_online->save(error) && !confirm(error + "\n\nClose without saving the online settings?"))
    {
        m_tabs->setCurrentWidget(m_online);
        event->ignore();
        return;
    }
    event->accept();
}
