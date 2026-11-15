// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See theme.h.
#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>
#include <QtGlobal>

namespace launcher
{
    void ApplyDarkTheme(QApplication& app)
    {
        QApplication::setStyle("Fusion");
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        // Where the platform draws the title bar (Wayland, KDE), dark too.
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
        const QColor text(0xe6, 0xe6, 0xe9), dimText(0x6b, 0x6d, 0x73);
        const QColor window(0x1e, 0x1f, 0x23), base(0x16, 0x17, 0x1a), button(0x2b, 0x2d, 0x32);
        const QColor accent(0x2f, 0x6f, 0xd6);
        QPalette p;
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, QColor(0x25, 0x26, 0x2b));
        p.setColor(QPalette::ToolTipBase, button);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8c, 0x93));
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, button);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
        p.setColor(QPalette::Light, QColor(0x3d, 0x3f, 0x46));
        p.setColor(QPalette::Midlight, QColor(0x33, 0x35, 0x3b));
        p.setColor(QPalette::Mid, QColor(0x8a, 0x8c, 0x93));  // secondary text (the "Set by ..." notes)
        p.setColor(QPalette::Dark, QColor(0x12, 0x13, 0x15));
        p.setColor(QPalette::Shadow, Qt::black);
        p.setColor(QPalette::Highlight, accent);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(0x6e, 0xa8, 0xff));
        p.setColor(QPalette::LinkVisited, QColor(0xb1, 0x8c, 0xff));
        for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText })
            p.setColor(QPalette::Disabled, role, dimText);
        p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3d, 0x3f, 0x46));
        p.setColor(QPalette::Disabled, QPalette::Button, QColor(0x24, 0x25, 0x29));
        app.setPalette(p);
        app.setStyleSheet(
            "QPushButton#play { background: #2f6fd6; color: white; font-weight: bold; border: 1px solid #3f7fe6;"
            " border-radius: 4px; padding: 6px 18px; }"
            "QPushButton#play:hover { background: #3a7ae2; }"
            "QPushButton#play:pressed { background: #2860bd; }"
            "QLabel#running_banner { background: #4a3b12; color: #ffd98a; padding: 8px; border-radius: 4px; }"
            "QToolTip { color: #e6e6e9; background: #2b2d32; border: 1px solid #3d3f46; }");
    }
}
