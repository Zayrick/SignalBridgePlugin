#include "ui/DeviceConfigPageFactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFont>
#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTabWidget>

#include "domain/ControlParameters.h"

namespace signalbridge
{
QWidget* CreateDeviceConfigPage(
    QTabWidget* parent,
    const QJsonObject& device,
    const DeviceConfigurationResolver& configuration_resolver,
    const DeviceConfigurationChangedCallback& configuration_changed)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);

    auto* panel = new QFrame(scroll);
    panel->setObjectName("SignalBridgePluginDevicePage");

    auto* layout = new QGridLayout(panel);
    layout->setColumnStretch(0, 3);
    layout->setColumnStretch(1, 1);
    layout->setRowStretch(0, 1);

    auto* controls_frame = new QFrame(panel);
    controls_frame->setObjectName("SignalBridgePluginControlsFrame");
    controls_frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    controls_frame->setAutoFillBackground(true);
    controls_frame->setFrameShape(QFrame::StyledPanel);
    controls_frame->setFrameShadow(QFrame::Sunken);

    auto* controls_layout = new QGridLayout(controls_frame);
    controls_layout->setColumnStretch(0, 0);
    controls_layout->setColumnStretch(1, 1);
    controls_layout->setColumnStretch(2, 1);
    controls_layout->setColumnStretch(3, 1);

    auto* info_frame = new QFrame(panel);
    info_frame->setObjectName("SignalBridgePluginInfoFrame");
    info_frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    info_frame->setAutoFillBackground(true);
    info_frame->setFrameShape(QFrame::StyledPanel);
    info_frame->setFrameShadow(QFrame::Sunken);

    auto* info_layout = new QGridLayout(info_frame);
    info_layout->setColumnStretch(0, 1);

    const QString key = device.value("key").toString();

    auto* title = new QLabel(device.value("name").toString(device.value("file").toString()), info_frame);
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    title->setWordWrap(true);
    info_layout->addWidget(title, 0, 0);

    QStringList info_lines;
    info_lines << device.value("file").toString();
    const QString vid = device.value("vid").toString();
    const QString pids = device.value("pids").toString();
    if(!vid.isEmpty() || !pids.isEmpty())
    {
        info_lines << QString("%1:%2").arg(vid, pids);
    }
    const QString serial = device.value("serial").toString();
    if(!serial.isEmpty())
    {
        info_lines << QObject::tr("Serial: %1").arg(serial);
    }

    auto* source = new QLabel(info_lines.join('\n'), info_frame);
    source->setWordWrap(true);
    info_layout->addWidget(source, 1, 0);
    info_layout->setRowStretch(2, 1);

    layout->addWidget(controls_frame, 0, 0);
    layout->addWidget(info_frame, 0, 1);

    const QJsonArray parameters = device.value("parameters").toArray();
    const QJsonObject configuration = configuration_resolver
                                          ? configuration_resolver(key, device.value("script_key").toString())
                                          : QJsonObject();
    int control_row = 0;

    for(const QJsonValue& value : parameters)
    {
        const QJsonObject parameter = value.toObject();
        const QString property = parameter.value("property").toString();
        if(property.isEmpty())
        {
            continue;
        }

        const QString label = parameter.value("label").toString(property);
        const QString type = parameter.value("type").toString().toLower();
        const QJsonValue current = NormalizeParameterValue(parameter, configuration.value(property));

        auto* label_widget = new QLabel(label + QStringLiteral(":"), controls_frame);
        label_widget->setWordWrap(true);
        controls_layout->addWidget(label_widget, control_row, 0);

        if(type == "combobox" || type == "select")
        {
            auto* combo = new QComboBox(controls_frame);
            combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            const QJsonArray values = parameter.value("values").toArray();
            for(const QJsonValue& item : values)
            {
                combo->addItem(JsonText(item));
            }
            const QString current_text = current.toString();
            if(combo->findText(current_text) < 0)
            {
                combo->addItem(current_text);
            }
            combo->setCurrentText(current_text);
            QObject::connect(combo, &QComboBox::currentTextChanged, combo, [configuration_changed, key, property](const QString& text) {
                if(configuration_changed)
                {
                    configuration_changed(key, property, text);
                }
            });
            controls_layout->addWidget(combo, control_row, 1, 1, 3);
            control_row++;
            continue;
        }

        if(type == "boolean" || type == "checkbox")
        {
            auto* checkbox = new QCheckBox(controls_frame);
            checkbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            checkbox->setChecked(current.toBool(false));
            QObject::connect(checkbox, &QCheckBox::toggled, checkbox, [configuration_changed, key, property](bool checked) {
                if(configuration_changed)
                {
                    configuration_changed(key, property, checked);
                }
            });
            controls_layout->addWidget(checkbox, control_row, 1, 1, 3);
            control_row++;
            continue;
        }

        if(type == "number")
        {
            auto* spin = new QDoubleSpinBox(controls_frame);
            const double step = ParameterNumberStep(parameter);
            spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            spin->setDecimals(ParameterNumberDecimals(parameter, step));
            spin->setRange(
                ParameterNumberBound(parameter, "min", -1000000.0),
                ParameterNumberBound(parameter, "max", 1000000.0));
            spin->setSingleStep(step);
            spin->setValue(current.toDouble(0.0));
            if(JsonBool(parameter.value("live"), true))
            {
                QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), spin, [configuration_changed, key, property](double number) {
                    if(configuration_changed)
                    {
                        configuration_changed(key, property, number);
                    }
                });
            }
            else
            {
                QObject::connect(spin, &QDoubleSpinBox::editingFinished, spin, [configuration_changed, spin, key, property]() {
                    if(configuration_changed)
                    {
                        configuration_changed(key, property, spin->value());
                    }
                });
            }
            controls_layout->addWidget(spin, control_row, 1, 1, 3);
            control_row++;
            continue;
        }

        auto* line_edit = new QLineEdit(controls_frame);
        line_edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        line_edit->setText(current.toString());
        if(type == "color")
        {
            line_edit->setPlaceholderText(QStringLiteral("#RRGGBB"));
        }
        QObject::connect(line_edit, &QLineEdit::editingFinished, line_edit, [configuration_changed, line_edit, key, property]() {
            if(configuration_changed)
            {
                configuration_changed(key, property, line_edit->text());
            }
        });
        controls_layout->addWidget(line_edit, control_row, 1, 1, 3);
        control_row++;
    }

    if(control_row == 0)
    {
        auto* label = new QLabel(QStringLiteral("暂无配置"), controls_frame);
        label->setWordWrap(true);
        controls_layout->addWidget(label, 0, 0, 1, 4);
        control_row = 1;
    }

    controls_layout->setRowStretch(control_row, 1);
    scroll->setWidget(panel);
    return scroll;
}
}
