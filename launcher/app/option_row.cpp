// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See option_row.h.
#include "option_row.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QWheelEvent>

#include <cmath>

namespace launcher
{
    namespace
    {
        // A list or slider takes the mouse wheel only once it has been
        // clicked (focused): over one that wasn't, the wheel scrolls the page,
        // so scrolling past a row never changes the setting under the pointer.
        class QuietList : public QComboBox
        {
        public:
            using QComboBox::QComboBox;

        protected:
            void wheelEvent(QWheelEvent* e) override
            {
                if (hasFocus())
                    QComboBox::wheelEvent(e);
                else
                    e->ignore();
            }
        };

        class QuietSlider : public QSlider
        {
        public:
            using QSlider::QSlider;

        protected:
            void wheelEvent(QWheelEvent* e) override
            {
                if (hasFocus())
                    QSlider::wheelEvent(e);
                else
                    e->ignore();
            }
        };

        QString Name(const settings::Option& o)
        {
            return QString::fromUtf8(o.table) + "." + QString::fromUtf8(o.key);
        }

        QWidget* BoolEditor(const settings::Option& o, QWidget* parent)
        {
            auto* box = new QCheckBox(parent);
            box->setObjectName(Name(o));
            box->setChecked(settings::GetStoredValue(o.id) != 0.0);
            QObject::connect(box, &QCheckBox::toggled, box, [id = o.id](bool on) { settings::Set(id, on ? 1.0 : 0.0); });
            return box;
        }

        QWidget* EnumEditor(const settings::Option& o, QWidget* parent)
        {
            auto* list = new QuietList(parent);
            list->setObjectName(Name(o));
            list->setFocusPolicy(Qt::StrongFocus);
            const double stored = settings::GetStoredValue(o.id);
            int current = -1;
            for (const settings::EnumEntry& e : o.entries)
            {
                if (double(e.value) == stored)
                    current = list->count();
                list->addItem(QString::fromUtf8(e.label), e.value);
            }
            list->setCurrentIndex(current);
            QObject::connect(list, &QComboBox::currentIndexChanged, list, [list, id = o.id](int index) {
                if (index >= 0)
                    settings::Set(id, list->itemData(index).toDouble());
            });
            return list;
        }

        QWidget* NumberEditor(const settings::Option& o, QWidget* parent)
        {
            auto* box = new QWidget(parent);
            auto* layout = new QHBoxLayout(box);
            layout->setContentsMargins(0, 0, 0, 0);
            auto* slider = new QuietSlider(Qt::Horizontal, box);
            slider->setObjectName(Name(o));
            slider->setFocusPolicy(Qt::StrongFocus);
            auto* value = new QLabel(box);
            value->setMinimumWidth(80);
            const int steps = int(std::lround((o.max - o.min) / o.step));
            slider->setRange(0, steps);
            const double stored = settings::GetStoredValue(o.id);
            slider->setValue(std::clamp(int(std::lround((stored - o.min) / o.step)), 0, steps));
            value->setText(QString::fromStdString(settings::FormatValue(o.id, stored)));
            QObject::connect(slider, &QSlider::valueChanged, value, [value, id = o.id, min = o.min, step = o.step](int i) {
                const double v = min + i * step;
                settings::Set(id, v);
                value->setText(QString::fromStdString(settings::FormatValue(id, settings::GetStoredValue(id))));
            });
            layout->addWidget(slider, 1);
            layout->addWidget(value);
            return box;
        }
    }

    QWidget* MakeEditor(const settings::Option& o, QWidget* parent)
    {
        switch (o.type)
        {
        case settings::Type::Bool: return BoolEditor(o, parent);
        case settings::Type::Enum: return EnumEditor(o, parent);
        case settings::Type::Int:
        case settings::Type::Float: return NumberEditor(o, parent);
        case settings::Type::Key: break;  // the Controls tab's (controls_tab.h)
        }
        return nullptr;
    }
}
