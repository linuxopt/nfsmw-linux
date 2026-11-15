// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See controls_tab.h.
#include "controls_tab.h"

#include "launcher_window.h"

#include <QAction>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <hid/key_map.h>
#include <keys.h>
#include <user/key_names.h>

namespace
{
    const char* kNoKey = "none";
}

ControlsTab::ControlsTab(LauncherWindow* window) : m_window(window)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setHorizontalSpacing(16);
    int row = 0;
    for (const hid::keymap::Entry& e : hid::keymap::kEntries)
    {
        auto* label = new QLabel(QString::fromUtf8(settings::Info(e.id).label), page);
        grid->addWidget(label, row, 0);
        int column = 1;
        for (settings::Id id : { e.id, e.id2 })
        {
            auto* button = new QPushButton(page);
            button->setObjectName(QString("keys.") + settings::Info(id).key);
            button->setContextMenuPolicy(Qt::ActionsContextMenu);
            auto* clear = new QAction("Clear", button);
            connect(clear, &QAction::triggered, this, [this, id] {
                endWait();
                settings::Set(id, double(settings::keynames::kNone));
                refresh();
            });
            button->addAction(clear);
            connect(button, &QPushButton::clicked, this, [this, id] { startWait(id); });
            button->installEventFilter(this);
            m_buttons[size_t(id)] = button;
            grid->addWidget(button, row, column++);
        }
        row++;
    }
    grid->setRowStretch(row, 1);
    scroll->setWidget(page);
    outer->addWidget(scroll, 1);

    auto* bar = new QHBoxLayout;
    bar->addStretch(1);
    auto* reset = new QPushButton("Reset Controls", this);
    reset->setObjectName("reset_controls");
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!m_window->confirm("Put every key back to the game's default layout?"))
            return;
        endWait();
        for (const hid::keymap::Entry& e : hid::keymap::kEntries)
        {
            settings::Reset(e.id);
            settings::Reset(e.id2);
        }
        refresh();
    });
    bar->addWidget(reset);
    outer->addLayout(bar);
    refresh();
}

QString ControlsTab::KeyLabel(double stored)
{
    if (stored == double(settings::keynames::kNone))
        return kNoKey;
    const char* token = settings::keynames::Token(uint16_t(stored));
    if (!token || !token[0])
        return "?";
    // "left_shift" -> "Left Shift", "w" -> "W", "keypad_1" -> "Keypad 1".
    QString text = QString::fromUtf8(token);
    QStringList words = text.split('_');
    for (QString& w : words)
        if (!w.isEmpty())
            w[0] = w[0].toUpper();
    return words.join(' ');
}

QString ControlsTab::rowHelp(settings::Id id) const
{
    const hid::keymap::Entry* mine = nullptr;
    for (const hid::keymap::Entry& e : hid::keymap::kEntries)
        if (e.id == id || e.id2 == id)
            mine = &e;
    QString help = QString::fromUtf8(settings::Info(mine ? mine->id : id).description);
    if (!mine)
        return help;
    // The other controls that have any of this row's keys, in page order.
    const double keys[2] = { settings::GetStoredValue(mine->id), settings::GetStoredValue(mine->id2) };
    QStringList shared;
    for (const hid::keymap::Entry& other : hid::keymap::kEntries)
    {
        if (other.control == mine->control)
            continue;
        const double theirs[2] = { settings::GetStoredValue(other.id), settings::GetStoredValue(other.id2) };
        bool any = false;
        for (double k : keys)
            for (double t : theirs)
                any |= k != double(settings::keynames::kNone) && k == t;
        if (any)
            shared << QString::fromUtf8(settings::Info(other.id).label);
    }
    if (!shared.isEmpty())
        help += "\nAlso: " + shared.join(", ");
    return help + "\nClick a key to change it, right-click to clear it.";
}

void ControlsTab::startWait(settings::Id id)
{
    m_waiting = id;
    refresh();
    m_buttons[size_t(id)]->setFocus();
    m_window->describe("Press the new key for " + QString::fromUtf8(settings::Info(id).label) + ". Esc keeps the old one.");
}

void ControlsTab::endWait()
{
    m_waiting = settings::Id::Count;
    refresh();
}

void ControlsTab::refresh()
{
    for (const hid::keymap::Entry& e : hid::keymap::kEntries)
        for (settings::Id id : { e.id, e.id2 })
            if (QPushButton* b = m_buttons[size_t(id)])
                b->setText(id == m_waiting ? QString("Press a key…") : KeyLabel(settings::GetStoredValue(id)));
}

bool ControlsTab::eventFilter(QObject* watched, QEvent* event)
{
    auto* button = qobject_cast<QPushButton*>(watched);
    if (!button)
        return QWidget::eventFilter(watched, event);
    settings::Id id = settings::Id::Count;
    for (size_t i = 0; i < settings::kCount; i++)
        if (m_buttons[i] == button)
            id = settings::Id(i);

    if ((event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) && m_waiting == settings::Id::Count)
        m_window->describe(rowHelp(id));
    if (event->type() == QEvent::FocusOut && m_waiting == id)
        endWait();
    if (event->type() != QEvent::KeyPress || m_waiting != id)
        return QWidget::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);
    if (key->isAutoRepeat())
        return true;
    if (key->key() == Qt::Key_Escape)
    {
        endWait();
        m_window->describe(rowHelp(id));
        return true;
    }
    const uint16_t code = launcher::keys::HidFromXkb(key->nativeScanCode());
    if (code == settings::keynames::kUnknown || (key->modifiers() & Qt::MetaModifier))
    {
        m_window->describe("That key can't be used. Press another, or Esc to keep the old one.");
        return true;  // the wait goes on
    }
    settings::Set(id, double(code));
    endWait();
    m_window->describe(rowHelp(id));
    return true;
}
