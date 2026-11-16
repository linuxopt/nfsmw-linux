// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
#include "online_tab.h"
#include "launcher_window.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lo = launcher::online;

OnlineTab::OnlineTab(LauncherWindow* window) : QWidget(window), m_window(window)
{
    std::string error;
    m_broken = !lo::Load(window->userDir(), m_loaded, error);

    auto* layout = new QVBoxLayout(this);
    m_enabled = new QCheckBox("Play online", this);
    m_enabled->setObjectName("online_enabled");
    m_enabled->setChecked(m_loaded.enabled);
    window->addHelp(m_enabled, "Races against other players in Xbox Live > Quick Match, through the online server. "
                               "Off: the game stays offline.");
    layout->addWidget(m_enabled);

    auto* form = new QFormLayout;
    m_name = new QLineEdit(QString::fromStdString(m_loaded.name), this);
    m_name->setObjectName("online_name");
    m_name->setMaxLength(15);
    m_name->setPlaceholderText("Your name");
    window->addHelp(m_name, "What other players see: up to 15 letters, digits or punctuation. "
                            "Nobody else online can have the same name at the same time.");
    form->addRow("Name", m_name);

    auto* serverRow = new QHBoxLayout;
    m_server = new QLineEdit(QString::fromStdString(m_loaded.server), this);
    m_server->setObjectName("online_server");
    m_server->setPlaceholderText(lo::kDefaultServer);
    window->addHelp(m_server, QString("The online server, as host:port. The default, %1, is SpeedBreaker's; "
                                      "anyone can run one (scripts/online in the source).").arg(lo::kDefaultServer));
    serverRow->addWidget(m_server, 1);
    m_default = new QPushButton("Default", this);
    m_default->setObjectName("online_server_default");
    window->addHelp(m_default, QString("Puts back the default server, %1.").arg(lo::kDefaultServer));
    connect(m_default, &QPushButton::clicked, this, [this] { m_server->setText(lo::kDefaultServer); });
    serverRow->addWidget(m_default);
    form->addRow("Server", serverRow);
    layout->addLayout(form);

    m_note = new QLabel(this);
    m_note->setObjectName("online_note");
    m_note->setWordWrap(true);
    if (m_broken)
        m_note->setText(QString::fromStdString(error) +
                        "\nIt's left as it is unless you change something here, which replaces it.");
    else
        m_note->setText("Other players see your name and an address the server gives you, never your IP. "
                        "The server sees your IP, as any server you connect to does.");
    layout->addWidget(m_note);
    layout->addStretch(1);

    connect(m_enabled, &QCheckBox::toggled, this, &OnlineTab::updateState);
    updateState();
}

void OnlineTab::updateState()
{
    const bool on = m_enabled->isChecked();
    m_name->setEnabled(on);
    m_server->setEnabled(on);
    m_default->setEnabled(on);
}

lo::Config OnlineTab::current() const
{
    lo::Config c = m_loaded;
    c.enabled = m_enabled->isChecked();
    c.name = m_name->text().toStdString();
    c.server = m_server->text().trimmed().toStdString();
    if (c.server.empty())
        c.server = lo::kDefaultServer;
    return c;
}

bool OnlineTab::save(QString& error)
{
    const lo::Config c = current();
    const bool changed = c.enabled != m_loaded.enabled || c.name != m_loaded.name || c.server != m_loaded.server;
    if (!changed)
        return true;
    if (c.enabled)
    {
        std::string problem = lo::NameProblem(c.name);
        if (problem.empty())
            problem = lo::ServerProblem(c.server);
        if (!problem.empty())
        {
            error = "Online: " + QString::fromStdString(problem);
            return false;
        }
    }
    std::string failure;
    if (!lo::Save(m_window->userDir(), c, failure))
    {
        error = "Online: " + QString::fromStdString(failure);
        return false;
    }
    m_loaded = c;
    m_broken = false;
    return true;
}
