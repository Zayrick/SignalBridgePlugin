#include "ui/SignalBridgeWidget.h"

#include <algorithm>
#include <utility>

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "domain/DeviceRecords.h"

namespace signalbridge
{
namespace
{
constexpr int OpenRgbTabLabelWidth = 222;
constexpr int OpenRgbTabLabelLeftPadding = 10;
constexpr int OpenRgbTabLabelNameWidth = OpenRgbTabLabelWidth - OpenRgbTabLabelLeftPadding;

QWidget* CreateOpenRgbStyleTabLabel(const QString& text, QWidget* parent)
{
    auto* tab_label = new QWidget(parent);
    tab_label->setMinimumWidth(OpenRgbTabLabelWidth);
    tab_label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    auto* layout = new QHBoxLayout(tab_label);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addSpacing(OpenRgbTabLabelLeftPadding);

    auto* name = new QLabel(text, tab_label);
    name->setMinimumWidth(OpenRgbTabLabelNameWidth);
    name->setMaximumWidth(OpenRgbTabLabelNameWidth);
    name->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    name->setWordWrap(true);
    layout->addWidget(name);

    return tab_label;
}

int AddOpenRgbStyleDeviceTab(QTabWidget* tab_widget, QWidget* page, const QString& label)
{
    const int index = tab_widget->addTab(page, QString());
    tab_widget->tabBar()->setTabButton(index, QTabBar::LeftSide, CreateOpenRgbStyleTabLabel(label, tab_widget->tabBar()));
    tab_widget->tabBar()->setTabToolTip(index, label);
    return index;
}

void ConfigureScriptTable(QTableWidget* table)
{
    table->setObjectName("SignalBridgePluginScriptTable");
    table->setColumnCount(ScriptTableColumnCount);
    table->setHorizontalHeaderLabels({
        QObject::tr("File Name"),
        QObject::tr("VID"),
        QObject::tr("Device Type"),
        QObject::tr("Script Name"),
        QObject::tr("Publisher"),
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

void SetScriptTableRows(QTableWidget* table, const QStringList& scripts, bool running)
{
    const int script_count = scripts.size() / ScriptTableColumnCount;
    table->setRowCount(0);
    if(running && scripts.isEmpty())
    {
        table->setRowCount(1);
        table->setItem(0, 0, new QTableWidgetItem(QObject::tr("Scanning SignalRGB scripts...")));
        return;
    }

    if(scripts.isEmpty())
    {
        table->setRowCount(1);
        table->setItem(0, 0, new QTableWidgetItem(QObject::tr("No SignalRGB scripts found.")));
        return;
    }

    table->setRowCount(script_count);
    for(int row = 0; row < script_count; row++)
    {
        for(int column = 0; column < ScriptTableColumnCount; column++)
        {
            const int cell_index = row * ScriptTableColumnCount + column;
            table->setItem(row, column, new QTableWidgetItem(scripts.at(cell_index)));
        }
    }
}
}

SignalBridgeWidget::SignalBridgeWidget(
    DeviceConfigurationResolver configuration_resolver,
    DeviceConfigurationChangedCallback configuration_changed,
    RescanCallback rescan,
    QWidget* parent)
    : QWidget(parent)
    , configuration_resolver_(std::move(configuration_resolver))
    , configuration_changed_(std::move(configuration_changed))
    , rescan_(std::move(rescan))
{
    setObjectName("SignalBridgePluginWidget");

    auto* layout = new QVBoxLayout(this);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("SignalBridgePluginStatus");
    layout->addWidget(status_label_);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setObjectName("SignalBridgePluginProgress");
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(discovery_progress_);
    progress_bar_->setTextVisible(true);
    layout->addWidget(progress_bar_);

    rescan_button_ = new QPushButton(tr("Rescan SignalRGB Scripts"), this);
    rescan_button_->setObjectName("SignalBridgePluginRescanButton");
    layout->addWidget(rescan_button_);

    view_stack_ = new QStackedWidget(this);
    view_stack_->setObjectName("SignalBridgePluginViewStack");

    details_text_ = new QPlainTextEdit(view_stack_);
    details_text_->setObjectName("SignalBridgePluginDetailsText");
    details_text_->setReadOnly(true);
    view_stack_->addWidget(details_text_);

    scripts_table_ = new QTableWidget(view_stack_);
    ConfigureScriptTable(scripts_table_);
    view_stack_->addWidget(scripts_table_);

    devices_tab_bar_ = new QTabWidget(view_stack_);
    devices_tab_bar_->setObjectName("SignalBridgePluginDeviceTabBar");
    devices_tab_bar_->setTabPosition(QTabWidget::West);
    view_stack_->addWidget(devices_tab_bar_);

    layout->addWidget(view_stack_, 1);

    auto* view_buttons_layout = new QHBoxLayout();

    log_view_button_ = new QPushButton(tr("Log Output"), this);
    log_view_button_->setObjectName("SignalBridgePluginLogViewButton");
    log_view_button_->setCheckable(true);
    view_buttons_layout->addWidget(log_view_button_);

    script_list_view_button_ = new QPushButton(tr("Script List"), this);
    script_list_view_button_->setObjectName("SignalBridgePluginScriptListViewButton");
    script_list_view_button_->setCheckable(true);
    view_buttons_layout->addWidget(script_list_view_button_);

    device_list_view_button_ = new QPushButton(tr("Devices"), this);
    device_list_view_button_->setObjectName("SignalBridgePluginDeviceListViewButton");
    device_list_view_button_->setCheckable(true);
    view_buttons_layout->addWidget(device_list_view_button_);

    layout->addLayout(view_buttons_layout);

    connect(rescan_button_, &QPushButton::clicked, this, [this]() {
        if(rescan_)
        {
            rescan_();
        }
    });
    connect(log_view_button_, &QPushButton::clicked, this, [this]() { SetActiveView(0); });
    connect(script_list_view_button_, &QPushButton::clicked, this, [this]() { SetActiveView(1); });
    connect(device_list_view_button_, &QPushButton::clicked, this, [this]() { SetActiveView(2); });
    connect(devices_tab_bar_, &QTabWidget::currentChanged, this, [this]() { OnDeviceSelectionChanged(); });

    progress_bar_->setVisible(false);
    SetScriptTable(script_table_items_, false);
    SetDeviceList(QStringLiteral("[]"), false);
    SetActiveView(0);
    SetResourceAvailable(false);
}

void SignalBridgeWidget::SetResourceAvailable(bool available)
{
    resource_available_ = available;
    const bool running = progress_bar_ != nullptr && progress_bar_->isVisible();
    if(rescan_button_ != nullptr)
    {
        rescan_button_->setEnabled(resource_available_ && !running);
        rescan_button_->setVisible(!running);
    }
}

void SignalBridgeWidget::SetStatusText(const QString& text)
{
    status_message_ = text;
    if(status_label_ != nullptr)
    {
        status_label_->setText(status_message_);
    }
}

void SignalBridgeWidget::ClearLogOutput()
{
    log_buffer_.Clear();
    if(details_text_ != nullptr)
    {
        details_text_->clear();
    }
    if(log_view_button_ != nullptr)
    {
        log_view_button_->setText(tr("Log Output"));
    }
}

void SignalBridgeWidget::AppendLogLine(const QString& line)
{
    const LogAppendResult result = log_buffer_.Append(line);
    if(log_view_button_ != nullptr)
    {
        log_view_button_->setText(tr("Log Output (%1)").arg(result.count));
    }

    if(details_text_ != nullptr)
    {
        if(result.rebuild)
        {
            details_text_->setPlainText(result.text);
        }
        else
        {
            for(const QString& entry : result.appended)
            {
                details_text_->appendPlainText(entry);
            }
        }

        if(QScrollBar* scroll_bar = details_text_->verticalScrollBar())
        {
            scroll_bar->setValue(scroll_bar->maximum());
        }
    }
}

void SignalBridgeWidget::ApplyDiscoveryStatus(
    const QString& status,
    const QString& details,
    const QStringList& scripts,
    const QString& devices,
    bool running,
    int progress)
{
    discovery_progress_ = std::clamp(progress, 0, 100);
    SetStatusText(status);
    if(!status.isEmpty())
    {
        AppendLogLine(QString("[Discovery] %1").arg(status));
    }
    if(!details.isEmpty())
    {
        AppendLogLine(QString("[Discovery]\n%1").arg(details));
    }

    if(progress_bar_ != nullptr)
    {
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(discovery_progress_);
        progress_bar_->setVisible(running);
    }

    SetScriptTable(scripts, running);
    if(!devices.isNull())
    {
        SetDeviceList(devices, running);
    }

    if(rescan_button_ != nullptr)
    {
        rescan_button_->setVisible(!running);
        rescan_button_->setEnabled(resource_available_ && !running);
    }
}

void SignalBridgeWidget::SetActiveView(int index)
{
    if(view_stack_ != nullptr)
    {
        view_stack_->setCurrentIndex(index);
    }

    if(log_view_button_ != nullptr)
    {
        log_view_button_->setChecked(index == 0);
    }

    if(script_list_view_button_ != nullptr)
    {
        script_list_view_button_->setChecked(index == 1);
    }

    if(device_list_view_button_ != nullptr)
    {
        device_list_view_button_->setChecked(index == 2);
    }
}

void SignalBridgeWidget::SetScriptTable(const QStringList& scripts, bool running)
{
    script_table_items_ = scripts;
    const int script_count = script_table_items_.size() / ScriptTableColumnCount;

    if(script_list_view_button_ != nullptr)
    {
        script_list_view_button_->setText(tr("Script List (%1)").arg(script_count));
    }

    if(scripts_table_ != nullptr)
    {
        SetScriptTableRows(scripts_table_, script_table_items_, running);
    }
}

void SignalBridgeWidget::SetDeviceList(const QString& devices, bool running)
{
    if(!devices.isNull())
    {
        const QJsonDocument document = QJsonDocument::fromJson(devices.toUtf8());
        device_records_ = document.isArray() ? document.array() : QJsonArray();
    }

    if(device_list_view_button_ != nullptr)
    {
        device_list_view_button_->setText(tr("Devices (%1)").arg(device_records_.size()));
    }

    const QString previous_key = selected_device_key_;
    QSignalBlocker blocker(devices_tab_bar_);
    while(devices_tab_bar_->count() > 0)
    {
        QWidget* page = devices_tab_bar_->widget(0);
        devices_tab_bar_->removeTab(0);
        delete page;
    }

    if(running && device_records_.isEmpty())
    {
        selected_device_key_.clear();
        auto* page = new QWidget(devices_tab_bar_);
        auto* layout = new QVBoxLayout(page);
        auto* label = new QLabel(tr("Scanning SignalRGB scripts..."), page);
        label->setWordWrap(true);
        layout->addWidget(label);
        layout->addStretch(1);
        AddOpenRgbStyleDeviceTab(devices_tab_bar_, page, tr("Scanning"));
        return;
    }

    if(device_records_.isEmpty())
    {
        selected_device_key_.clear();
        auto* page = new QWidget(devices_tab_bar_);
        auto* layout = new QVBoxLayout(page);
        auto* label = new QLabel(tr("No SignalRGB devices found."), page);
        label->setWordWrap(true);
        layout->addWidget(label);
        layout->addStretch(1);
        AddOpenRgbStyleDeviceTab(devices_tab_bar_, page, tr("No Devices"));
        return;
    }

    int selected_index = 0;
    for(int row = 0; row < device_records_.size(); row++)
    {
        const QJsonObject device = device_records_.at(row).toObject();
        const QString key = device.value("key").toString();
        if(!previous_key.isEmpty() && key == previous_key)
        {
            selected_index = row;
        }

        const QString display_name = device.value("name").toString(device.value("file").toString());
        AddOpenRgbStyleDeviceTab(
            devices_tab_bar_,
            CreateDeviceConfigPage(devices_tab_bar_, device, configuration_resolver_, configuration_changed_),
            display_name);
    }

    devices_tab_bar_->setCurrentIndex(selected_index);
    selected_device_key_ = device_records_.at(selected_index).toObject().value("key").toString();
}

void SignalBridgeWidget::OnDeviceSelectionChanged()
{
    const int index = devices_tab_bar_->currentIndex();
    if(index >= 0 && index < device_records_.size())
    {
        selected_device_key_ = device_records_.at(index).toObject().value("key").toString();
    }
    else
    {
        selected_device_key_.clear();
    }
}
}
