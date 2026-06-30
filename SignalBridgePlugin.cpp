#include "SignalBridgePlugin.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "RGBController_SignalBridgeScript.h"
#include "SignalBridgeHid.h"
#include "SignalBridgeScriptScanner.h"

namespace
{
constexpr int ScriptTableColumnCount = 5;
constexpr int OpenRgbTabLabelWidth = 222;
constexpr int OpenRgbTabLabelLeftPadding = 10;
constexpr int OpenRgbTabLabelNameWidth = OpenRgbTabLabelWidth - OpenRgbTabLabelLeftPadding;
constexpr int MaxFrontendLogLines = 2000;

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

QString FormatHex16(std::uint16_t value)
{
    return QString("0x%1").arg(value, 4, 16, QLatin1Char('0'));
}

QString FormatScriptLogLine(const QString& stage, const std::string& source, const std::string& message)
{
    const QString source_text = QString::fromStdString(source).trimmed();
    const QString message_text = QString::fromStdString(message);
    if(source_text.isEmpty())
    {
        return QString("[%1] %2").arg(stage, message_text);
    }
    return QString("[%1] [%2] %3").arg(stage, source_text, message_text);
}

QStringList FormatScriptTable(const std::vector<SignalBridgeScriptMeta>& scripts)
{
    QStringList cells;

    for(const SignalBridgeScriptMeta& meta : scripts)
    {
        const QString source_path = QString::fromStdString(meta.source_path);
        const QString file_name = QFileInfo(source_path).fileName();

        QString name = QString::fromStdString(meta.name);
        if(name.isEmpty())
        {
            name = file_name;
        }

        cells << file_name
              << (meta.vid.has_value() ? FormatHex16(*meta.vid) : QString())
              << QString::fromStdString(meta.device_type)
              << name
              << QString::fromStdString(meta.publisher);
    }

    return cells;
}

QString ConfigKeyForMeta(const SignalBridgeScriptMeta& meta)
{
    QString key = QString::fromStdString(meta.lookup_path.empty() ? meta.source_path : meta.lookup_path);
    key.replace('\\', '/');
    return key;
}

QString FormatPidList(const std::vector<std::uint16_t>& pids)
{
    QStringList values;
    for(std::uint16_t pid : pids)
    {
        values << FormatHex16(pid);
    }
    return values.join(", ");
}

QString DeviceIdentityForHid(const SignalBridgeHidInfo& hid)
{
    if(!hid.serial.empty())
    {
        return QString::fromStdString(hid.serial);
    }

    const std::string normalized = SignalBridgeHidBackend::NormalizeDevicePath(hid.path);
    if(!normalized.empty())
    {
        return QString::fromStdString(normalized);
    }
    return QString::fromStdString(hid.path);
}

QString ConfigKeyForDevice(const SignalBridgeScriptMeta& meta, const SignalBridgeHidInfo& hid)
{
    return ConfigKeyForMeta(meta) + "|" + DeviceIdentityForHid(hid);
}

QJsonObject DeviceRecordForController(const SignalBridgeScriptMeta& meta, const SignalBridgeHidInfo& hid, const QString& key)
{
    const QString source_path = QString::fromStdString(meta.source_path);

    QJsonArray parameters;
    for(const QJsonObject& parameter : meta.control_parameters)
    {
        parameters.append(parameter);
    }

    QJsonObject device;
    device.insert("key", key);
    device.insert("script_key", ConfigKeyForMeta(meta));
    device.insert("name", QString::fromStdString(meta.name));
    device.insert("file", QFileInfo(source_path).fileName());
    device.insert("source_path", source_path);
    device.insert("vid", meta.vid.has_value() ? FormatHex16(*meta.vid) : FormatHex16(hid.vid));
    device.insert("pids", meta.pids.empty() ? FormatHex16(hid.pid) : FormatPidList(meta.pids));
    device.insert("device_type", QString::fromStdString(meta.device_type));
    device.insert("publisher", QString::fromStdString(meta.publisher));
    device.insert("serial", QString::fromStdString(hid.serial));
    device.insert("location", QString::fromStdString(hid.path));
    device.insert("parameters", parameters);
    return device;
}

QString CompactJsonArray(const QJsonArray& devices)
{
    return QString::fromUtf8(QJsonDocument(devices).toJson(QJsonDocument::Compact));
}

bool JsonBool(const QJsonValue& value, bool fallback)
{
    if(value.isBool())
    {
        return value.toBool();
    }
    if(value.isString())
    {
        const QString text = value.toString().trimmed().toLower();
        if(text == "true" || text == "1" || text == "yes" || text == "on")
        {
            return true;
        }
        if(text == "false" || text == "0" || text == "no" || text == "off")
        {
            return false;
        }
    }
    return fallback;
}

double JsonNumber(const QJsonValue& value, double fallback)
{
    if(value.isDouble())
    {
        return value.toDouble();
    }
    if(value.isString())
    {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if(ok)
        {
            return parsed;
        }
    }
    return fallback;
}

QString JsonText(const QJsonValue& value, const QString& fallback = QString())
{
    if(value.isString())
    {
        return value.toString();
    }
    if(value.isBool())
    {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if(value.isDouble())
    {
        return QString::number(value.toDouble());
    }
    return fallback;
}

QJsonValue ParameterDefaultValue(const QJsonObject& parameter)
{
    const QString type = parameter.value("type").toString().toLower();
    const QJsonValue fallback = parameter.value("default");

    if(type == "boolean" || type == "checkbox")
    {
        return JsonBool(fallback, false);
    }
    if(type == "number")
    {
        return JsonNumber(fallback, 0.0);
    }
    if(type == "combobox" || type == "select")
    {
        const QJsonArray values = parameter.value("values").toArray();
        if(fallback.isString())
        {
            return fallback.toString();
        }
        return values.isEmpty() ? QString() : JsonText(values.first());
    }
    return JsonText(fallback);
}

QJsonValue NormalizeParameterValue(const QJsonObject& parameter, const QJsonValue& value)
{
    const QString type = parameter.value("type").toString().toLower();
    const QJsonValue fallback = ParameterDefaultValue(parameter);

    if(value.isUndefined() || value.isNull())
    {
        return fallback;
    }
    if(type == "boolean" || type == "checkbox")
    {
        return JsonBool(value, fallback.toBool(false));
    }
    if(type == "number")
    {
        return JsonNumber(value, fallback.toDouble(0.0));
    }
    if(type == "combobox" || type == "select")
    {
        const QString text = JsonText(value, fallback.toString());
        const QJsonArray values = parameter.value("values").toArray();
        if(values.isEmpty())
        {
            return text;
        }
        for(const QJsonValue& candidate : values)
        {
            if(JsonText(candidate) == text)
            {
                return text;
            }
        }
        return fallback;
    }
    return JsonText(value, fallback.toString());
}

double ParameterNumberBound(const QJsonObject& parameter, const char* key, double fallback)
{
    const QJsonValue value = parameter.value(key);
    return value.isUndefined() ? fallback : JsonNumber(value, fallback);
}

double ParameterNumberStep(const QJsonObject& parameter)
{
    const double step = ParameterNumberBound(parameter, "step", 1.0);
    return step > 0.0 ? step : 1.0;
}

int DecimalPlaces(double value)
{
    QString text = QString::number(value, 'f', 6);
    while(text.contains('.') && text.endsWith('0'))
    {
        text.chop(1);
    }
    if(text.endsWith('.'))
    {
        text.chop(1);
    }

    const int dot = text.indexOf('.');
    return dot < 0 ? 0 : text.size() - dot - 1;
}

int ParameterNumberDecimals(const QJsonObject& parameter, double step)
{
    int decimals = DecimalPlaces(step);
    for(const char* key : { "min", "max", "default" })
    {
        const QJsonValue value = parameter.value(key);
        if(!value.isUndefined())
        {
            decimals = std::max(decimals, DecimalPlaces(JsonNumber(value, 0.0)));
        }
    }
    return std::clamp(decimals, 0, 6);
}

QString DeviceConfigStorePath(ResourceManagerInterface* manager)
{
    if(manager == nullptr)
    {
        return QString();
    }

    const filesystem::path directory = manager->GetConfigurationDirectory() / "SignalBridge";
    filesystem::create_directories(directory);
    return QString::fromStdString((directory / "device_config.json").generic_u8string());
}
}

SignalBridgePlugin::SignalBridgePlugin(QObject* parent)
    : QObject(parent)
{
    connect(this,
            &SignalBridgePlugin::DiscoveryStatusChanged,
            this,
            &SignalBridgePlugin::ApplyDiscoveryStatus,
            Qt::QueuedConnection);
    connect(this,
            &SignalBridgePlugin::ScriptLogReceived,
            this,
            &SignalBridgePlugin::AppendLogLine,
            Qt::QueuedConnection);
}

SignalBridgePlugin::~SignalBridgePlugin()
{
    StopDiscoveryThread();
}

OpenRGBPluginInfo SignalBridgePlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name = "Signal Bridge Plugin";
    info.Description = "Brings support for SignalRGB JavaScript device plugins to OpenRGB.";
    info.Version = SIGNALBRIDGEPLUGIN_VERSION;
    info.Commit = SIGNALBRIDGEPLUGIN_COMMIT;
    info.URL = "";
    info.Location = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label = "SignalBridge";

    return info;
}

unsigned int SignalBridgePlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void SignalBridgePlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    resource_manager = resource_manager_ptr;
    status_message = "SignalRGB discovery queued";
    ClearLogOutput();
    script_table_items.clear();
    LoadDeviceConfigStore();
    DiscoverSignalRgbDevices();
}

QWidget* SignalBridgePlugin::GetWidget()
{
    EnsureWidget();
    return widget;
}

QMenu* SignalBridgePlugin::GetTrayMenu()
{
    return nullptr;
}

void SignalBridgePlugin::Unload()
{
    StopDiscoveryThread();
    RemoveControllers(resource_manager);
    hid_backend.reset();
    resource_manager = nullptr;
    status_message = "Plugin unloaded";
    discovery_progress = 0;
    ClearLogOutput();
    script_table_items.clear();
    device_records = QJsonArray();
    selected_device_key.clear();

    if(status_label != nullptr)
    {
        status_label->setText(QString::fromStdString(status_message));
    }
    SetScriptTable(script_table_items, false);
    SetDeviceList(QStringLiteral("[]"), false);
    if(progress_bar != nullptr)
    {
        progress_bar->setRange(0, 100);
        progress_bar->setValue(0);
        progress_bar->setVisible(false);
    }
    if(rescan_button != nullptr)
    {
        rescan_button->setEnabled(false);
        rescan_button->setVisible(true);
    }
}

void SignalBridgePlugin::EnsureWidget()
{
    if(widget != nullptr)
    {
        return;
    }

    widget = new QWidget();
    widget->setObjectName("SignalBridgePluginWidget");

    auto* layout = new QVBoxLayout(widget);

    status_label = new QLabel(widget);
    status_label->setObjectName("SignalBridgePluginStatus");
    layout->addWidget(status_label);

    progress_bar = new QProgressBar(widget);
    progress_bar->setObjectName("SignalBridgePluginProgress");
    progress_bar->setRange(0, 100);
    progress_bar->setValue(discovery_progress);
    progress_bar->setTextVisible(true);
    layout->addWidget(progress_bar);

    rescan_button = new QPushButton(tr("Rescan SignalRGB Scripts"), widget);
    rescan_button->setObjectName("SignalBridgePluginRescanButton");
    layout->addWidget(rescan_button);

    view_stack = new QStackedWidget(widget);
    view_stack->setObjectName("SignalBridgePluginViewStack");

    details_text = new QPlainTextEdit(view_stack);
    details_text->setObjectName("SignalBridgePluginDetailsText");
    details_text->setReadOnly(true);
    view_stack->addWidget(details_text);

    scripts_table = new QTableWidget(view_stack);
    scripts_table->setObjectName("SignalBridgePluginScriptTable");
    scripts_table->setColumnCount(ScriptTableColumnCount);
    scripts_table->setHorizontalHeaderLabels({
        tr("File Name"),
        tr("VID"),
        tr("Device Type"),
        tr("Script Name"),
        tr("Publisher"),
    });
    scripts_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scripts_table->setSelectionMode(QAbstractItemView::NoSelection);
    scripts_table->setAlternatingRowColors(true);
    scripts_table->verticalHeader()->setVisible(false);
    scripts_table->horizontalHeader()->setStretchLastSection(true);
    scripts_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    view_stack->addWidget(scripts_table);

    devices_tab_bar = new QTabWidget(view_stack);
    devices_tab_bar->setObjectName("SignalBridgePluginDeviceTabBar");
    devices_tab_bar->setTabPosition(QTabWidget::West);
    view_stack->addWidget(devices_tab_bar);

    layout->addWidget(view_stack, 1);

    auto* view_buttons_layout = new QHBoxLayout();

    log_view_button = new QPushButton(tr("Log Output"), widget);
    log_view_button->setObjectName("SignalBridgePluginLogViewButton");
    log_view_button->setCheckable(true);
    view_buttons_layout->addWidget(log_view_button);

    script_list_view_button = new QPushButton(tr("Script List"), widget);
    script_list_view_button->setObjectName("SignalBridgePluginScriptListViewButton");
    script_list_view_button->setCheckable(true);
    view_buttons_layout->addWidget(script_list_view_button);

    device_list_view_button = new QPushButton(tr("Devices"), widget);
    device_list_view_button->setObjectName("SignalBridgePluginDeviceListViewButton");
    device_list_view_button->setCheckable(true);
    view_buttons_layout->addWidget(device_list_view_button);

    layout->addLayout(view_buttons_layout);

    connect(rescan_button, &QPushButton::clicked, this, &SignalBridgePlugin::DiscoverSignalRgbDevices);
    connect(log_view_button, &QPushButton::clicked, this, &SignalBridgePlugin::ShowLogView);
    connect(script_list_view_button, &QPushButton::clicked, this, &SignalBridgePlugin::ShowScriptListView);
    connect(device_list_view_button, &QPushButton::clicked, this, &SignalBridgePlugin::ShowDeviceListView);
    connect(devices_tab_bar, &QTabWidget::currentChanged, this, &SignalBridgePlugin::OnDeviceSelectionChanged);
    const bool running = discovery_running.load();
    progress_bar->setVisible(running);
    rescan_button->setVisible(!running);
    rescan_button->setEnabled(resource_manager != nullptr && !running);
    SetStatusText(status_message);
    if(details_text != nullptr)
    {
        details_text->setPlainText(QString::fromStdString(details_message));
    }
    SetScriptTable(script_table_items, running);
    SetDeviceList(QString::fromUtf8(QJsonDocument(device_records).toJson(QJsonDocument::Compact)), running);
    SetActiveView(0);
}

void SignalBridgePlugin::DiscoverSignalRgbDevices()
{
    if(resource_manager == nullptr)
    {
        return;
    }

    if(discovery_running.load())
    {
        SetStatusText("SignalRGB discovery is already running");
        return;
    }

    if(discovery_thread.joinable())
    {
        discovery_thread.join();
    }

    discovery_cancel_requested.store(false);
    const int generation = discovery_generation.fetch_add(1) + 1;
    discovery_running.store(true);
    ClearLogOutput();
    ApplyDiscoveryStatus(generation, "Scanning SignalRGB scripts...", "", QStringList(), QStringLiteral("[]"), true, 0);

    discovery_thread = std::thread(&SignalBridgePlugin::DiscoveryWorker, this, generation, resource_manager);
}

void SignalBridgePlugin::DiscoveryWorker(int generation, ResourceManagerInterface* manager)
{
    unsigned int matched = 0;
    unsigned int failed = 0;
    QStringList detail_lines;
    QStringList discovered_scripts;

    try
    {
        RemoveControllers(manager);
        hid_backend.reset();

        if(IsDiscoveryStale(generation))
        {
            discovery_running.store(false);
            return;
        }

        auto new_hid_backend = std::make_shared<SignalBridgeHidBackend>();
        hid_backend = new_hid_backend;

        const filesystem::path script_path = manager->GetConfigurationDirectory() / "SignalBridge" / "scripts";
        filesystem::create_directories(script_path);
        const std::string script_dir = script_path.generic_u8string();
        int last_scan_progress = -1;
        SignalBridgeScriptLogCallback scan_log_callback =
            [this, generation](const std::string& source, const std::string& message) {
                if(IsDiscoveryStale(generation))
                {
                    return;
                }
                emit this->ScriptLogReceived(FormatScriptLogLine("Scan", source, message));
            };
        const SignalBridgeScanReport report = SignalBridgeScriptScanner().ScanDirectory(
            script_dir,
            [this, generation, &last_scan_progress](std::size_t completed, std::size_t total, const std::string&) {
                if(IsDiscoveryStale(generation))
                {
                    return;
                }

                const int progress = total > 0
                                         ? static_cast<int>((completed * 70) / total)
                                         : 70;
                if(progress == last_scan_progress && completed != total)
                {
                    return;
                }

                last_scan_progress = progress;
                emit DiscoveryStatusChanged(
                    generation,
                    QString("Scanning SignalRGB scripts (%1/%2)...")
                        .arg(static_cast<qulonglong>(completed))
                        .arg(static_cast<qulonglong>(total)),
                    "",
                    QStringList(),
                    QString(),
                    true,
                    progress);
            },
            scan_log_callback);
        if(IsDiscoveryStale(generation))
        {
            discovery_running.store(false);
            return;
        }

        discovered_scripts = FormatScriptTable(report.scripts);

        emit DiscoveryStatusChanged(generation, "Enumerating HID interfaces...", "", discovered_scripts, QString(), true, 72);
        const std::vector<SignalBridgeHidInfo> hid_devices = new_hid_backend->Enumerate();
        if(IsDiscoveryStale(generation))
        {
            discovery_running.store(false);
            return;
        }
        emit DiscoveryStatusChanged(generation, "Matching scripts to HID devices...", "", discovered_scripts, QString(), true, 80);

        std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<SignalBridgeHidInfo>> by_vid_pid;
        for(const SignalBridgeHidInfo& hid : hid_devices)
        {
            by_vid_pid[{ hid.vid, hid.pid }].push_back(hid);
        }

        std::set<std::string> open_groups;
        QJsonArray registered_devices;
        detail_lines << QString("Script directory: %1").arg(QString::fromStdString(script_dir));
        detail_lines << QString("Scanned scripts: %1").arg(report.scripts.size());
        detail_lines << QString("Scan errors: %1").arg(report.errors.size());
        detail_lines << QString("HID interfaces: %1").arg(hid_devices.size());
        detail_lines << "";

        int last_register_progress = 80;
        SignalBridgeScriptLogCallback runtime_log_callback =
            [this, generation](const std::string& source, const std::string& message) {
                if(IsDiscoveryStale(generation))
                {
                    return;
                }
                emit this->ScriptLogReceived(FormatScriptLogLine("Runtime", source, message));
            };
        for(std::size_t meta_index = 0; meta_index < report.scripts.size(); meta_index++)
        {
            const SignalBridgeScriptMeta& meta = report.scripts[meta_index];
            if(IsDiscoveryStale(generation))
            {
                discovery_running.store(false);
                return;
            }

            const int register_progress = 80 + static_cast<int>(((meta_index + 1) * 19) / std::max<std::size_t>(1, report.scripts.size()));
            if(register_progress != last_register_progress)
            {
                last_register_progress = register_progress;
                emit DiscoveryStatusChanged(
                    generation,
                    QString("Registering SignalRGB devices (%1/%2)...")
                        .arg(static_cast<qulonglong>(meta_index + 1))
                        .arg(static_cast<qulonglong>(report.scripts.size())),
                    "",
                    discovered_scripts,
                    QString(),
                    true,
                    register_progress);
            }

            if(!meta.vid.has_value())
            {
                continue;
            }

            for(std::uint16_t pid : meta.pids)
            {
                const auto candidates_it = by_vid_pid.find({ *meta.vid, pid });
                if(candidates_it == by_vid_pid.end())
                {
                    continue;
                }

                for(const SignalBridgeHidInfo& hid : candidates_it->second)
                {
                    if(IsDiscoveryStale(generation))
                    {
                        discovery_running.store(false);
                        return;
                    }

                    const std::string group = SignalBridgeHidBackend::NormalizeDevicePath(hid.path);
                    if(!group.empty() && open_groups.find(group) != open_groups.end())
                    {
                        continue;
                    }
                    if(!ValidateScriptEndpoint(meta, hid, generation))
                    {
                        continue;
                    }

                    try
                    {
                        const QString config_key = ConfigKeyForDevice(meta, hid);
                        auto* controller = new RGBController_SignalBridgeScript(
                            new_hid_backend,
                            meta,
                            hid,
                            ConfigurationForDevice(config_key, meta),
                            config_key.toStdString(),
                            runtime_log_callback);
                        manager->RegisterRGBController(controller);
                        {
                            std::lock_guard<std::mutex> lock(controller_mutex);
                            controllers.push_back(controller);
                        }
                        open_groups.insert(group);
                        registered_devices.append(DeviceRecordForController(controller->ScriptMeta(), hid, config_key));
                        matched++;
                        detail_lines << QString("Registered: %1 [%2:%3]")
                                            .arg(QString::fromStdString(meta.name))
                                            .arg(hid.vid, 4, 16, QLatin1Char('0'))
                                            .arg(hid.pid, 4, 16, QLatin1Char('0'));
                    }
                    catch(const std::exception& err)
                    {
                        failed++;
                        detail_lines << QString("Failed: %1 - %2")
                                            .arg(QString::fromStdString(meta.name))
                                            .arg(err.what());
                    }
                }
            }
        }

        if(!report.errors.empty())
        {
            detail_lines << "";
            detail_lines << "Script scan errors:";
            const std::size_t error_limit = std::min<std::size_t>(report.errors.size(), 20);
            for(std::size_t idx = 0; idx < error_limit; idx++)
            {
                detail_lines << QString("%1: %2")
                                    .arg(QString::fromStdString(report.errors[idx].path))
                                    .arg(QString::fromStdString(report.errors[idx].error));
            }
            if(report.errors.size() > error_limit)
            {
                detail_lines << QString("... %1 more").arg(report.errors.size() - error_limit);
            }
        }

        std::string status = "Registered " + std::to_string(matched) + " SignalRGB HID script device(s)";
        if(failed > 0)
        {
            status += ", " + std::to_string(failed) + " failed";
        }

        discovery_running.store(false);
        emit DiscoveryStatusChanged(generation,
                                    QString::fromStdString(status),
                                    detail_lines.join('\n'),
                                    discovered_scripts,
                                    CompactJsonArray(registered_devices),
                                    false,
                                    100);
    }
    catch(const std::exception& err)
    {
        discovery_running.store(false);
        emit DiscoveryStatusChanged(
            generation,
            QString::fromStdString(std::string("SignalRGB script discovery failed: ") + err.what()),
            "",
            QStringList(),
            QStringLiteral("[]"),
            false,
            0);
    }
}

void SignalBridgePlugin::StopDiscoveryThread()
{
    discovery_cancel_requested.store(true);
    discovery_generation.fetch_add(1);
    if(discovery_thread.joinable())
    {
        discovery_thread.join();
    }
    discovery_running.store(false);
}

bool SignalBridgePlugin::IsDiscoveryStale(int generation) const
{
    return discovery_cancel_requested.load() || generation != discovery_generation.load();
}

void SignalBridgePlugin::RemoveControllers(ResourceManagerInterface* manager)
{
    std::lock_guard<std::mutex> lock(controller_mutex);
    if(manager != nullptr)
    {
        for(RGBController_SignalBridgeScript* controller : controllers)
        {
            manager->UnregisterRGBController(controller);
        }
    }
    for(RGBController_SignalBridgeScript* controller : controllers)
    {
        delete controller;
    }
    controllers.clear();
}

bool SignalBridgePlugin::ValidateScriptEndpoint(
    const SignalBridgeScriptMeta& meta,
    const SignalBridgeHidInfo& hid,
    int generation)
{
    if(!meta.has_validate)
    {
        return true;
    }

    try
    {
        SignalBridgeScriptLogCallback log_callback =
            [this, generation](const std::string& source, const std::string& message) {
                if(IsDiscoveryStale(generation))
                {
                    return;
                }
                emit this->ScriptLogReceived(FormatScriptLogLine("Validate", source, message));
            };
        SignalBridgeJsRuntime runtime = SignalBridgeJsRuntime::CreateValidation(meta, log_callback);
        QJsonObject endpoint;
        endpoint.insert("interface", hid.interface_number.value_or(-1));
        endpoint.insert("usage", hid.usage.value_or(0));
        endpoint.insert("usage_page", hid.usage_page.value_or(0));
        endpoint.insert("collection", 0);

        QJsonArray args;
        args.append(endpoint);
        return runtime.CallModuleExportJson("Validate", args).toBool(false);
    }
    catch(...)
    {
        return false;
    }
}

void SignalBridgePlugin::SetStatusText(const std::string& text)
{
    status_message = text;
    if(status_label != nullptr)
    {
        status_label->setText(QString::fromStdString(status_message));
    }
}

void SignalBridgePlugin::ClearLogOutput()
{
    log_lines.clear();
    details_message.clear();

    if(details_text != nullptr)
    {
        details_text->clear();
    }
    if(log_view_button != nullptr)
    {
        log_view_button->setText(tr("Log Output"));
    }
}

void SignalBridgePlugin::AppendLogLine(const QString& line)
{
    if(line.isNull())
    {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QStringList appended;
    for(const QString& raw_line : line.split('\n'))
    {
        const QString entry = QString("[%1] %2").arg(timestamp, raw_line);
        log_lines.append(entry);
        appended.append(entry);
    }

    bool rebuild = false;
    while(log_lines.size() > MaxFrontendLogLines)
    {
        log_lines.removeFirst();
        rebuild = true;
    }

    const QString log_text = log_lines.join('\n');
    details_message = log_text.toStdString();
    if(log_view_button != nullptr)
    {
        log_view_button->setText(tr("Log Output (%1)").arg(log_lines.size()));
    }

    if(details_text != nullptr)
    {
        if(rebuild)
        {
            details_text->setPlainText(log_text);
        }
        else
        {
            for(const QString& entry : appended)
            {
                details_text->appendPlainText(entry);
            }
        }

        if(QScrollBar* scroll_bar = details_text->verticalScrollBar())
        {
            scroll_bar->setValue(scroll_bar->maximum());
        }
    }
}

void SignalBridgePlugin::SetActiveView(int index)
{
    if(view_stack != nullptr)
    {
        view_stack->setCurrentIndex(index);
    }

    if(log_view_button != nullptr)
    {
        log_view_button->setChecked(index == 0);
    }

    if(script_list_view_button != nullptr)
    {
        script_list_view_button->setChecked(index == 1);
    }

    if(device_list_view_button != nullptr)
    {
        device_list_view_button->setChecked(index == 2);
    }
}

void SignalBridgePlugin::SetScriptTable(const QStringList& scripts, bool running)
{
    script_table_items = scripts;
    const int script_count = script_table_items.size() / ScriptTableColumnCount;

    if(script_list_view_button != nullptr)
    {
        script_list_view_button->setText(tr("Script List (%1)").arg(script_count));
    }

    if(scripts_table == nullptr)
    {
        return;
    }

    scripts_table->setRowCount(0);
    if(running && script_table_items.isEmpty())
    {
        scripts_table->setRowCount(1);
        scripts_table->setItem(0, 0, new QTableWidgetItem(tr("Scanning SignalRGB scripts...")));
        return;
    }

    if(script_table_items.isEmpty())
    {
        scripts_table->setRowCount(1);
        scripts_table->setItem(0, 0, new QTableWidgetItem(tr("No SignalRGB scripts found.")));
        return;
    }

    scripts_table->setRowCount(script_count);
    for(int row = 0; row < script_count; row++)
    {
        for(int column = 0; column < ScriptTableColumnCount; column++)
        {
            const int cell_index = row * ScriptTableColumnCount + column;
            scripts_table->setItem(row, column, new QTableWidgetItem(script_table_items.at(cell_index)));
        }
    }
}

void SignalBridgePlugin::SetDeviceList(const QString& devices, bool running)
{
    if(!devices.isNull())
    {
        const QJsonDocument document = QJsonDocument::fromJson(devices.toUtf8());
        device_records = document.isArray() ? document.array() : QJsonArray();
    }

    if(device_list_view_button != nullptr)
    {
        device_list_view_button->setText(tr("Devices (%1)").arg(device_records.size()));
    }

    if(devices_tab_bar == nullptr)
    {
        return;
    }

    const QString previous_key = selected_device_key;
    QSignalBlocker blocker(devices_tab_bar);
    while(devices_tab_bar->count() > 0)
    {
        QWidget* page = devices_tab_bar->widget(0);
        devices_tab_bar->removeTab(0);
        delete page;
    }

    if(running && device_records.isEmpty())
    {
        selected_device_key.clear();
        auto* page = new QWidget(devices_tab_bar);
        auto* layout = new QVBoxLayout(page);
        auto* label = new QLabel(tr("Scanning SignalRGB scripts..."), page);
        label->setWordWrap(true);
        layout->addWidget(label);
        layout->addStretch(1);
        AddOpenRgbStyleDeviceTab(devices_tab_bar, page, tr("Scanning"));
        return;
    }

    if(device_records.isEmpty())
    {
        selected_device_key.clear();
        auto* page = new QWidget(devices_tab_bar);
        auto* layout = new QVBoxLayout(page);
        auto* label = new QLabel(tr("No SignalRGB devices found."), page);
        label->setWordWrap(true);
        layout->addWidget(label);
        layout->addStretch(1);
        AddOpenRgbStyleDeviceTab(devices_tab_bar, page, tr("No Devices"));
        return;
    }

    int selected_index = 0;
    for(int row = 0; row < device_records.size(); row++)
    {
        const QJsonObject device = device_records.at(row).toObject();
        const QString key = device.value("key").toString();
        if(!previous_key.isEmpty() && key == previous_key)
        {
            selected_index = row;
        }

        const QString display_name = device.value("name").toString(device.value("file").toString());
        AddOpenRgbStyleDeviceTab(devices_tab_bar, CreateDeviceConfigPage(device), display_name);
    }

    devices_tab_bar->setCurrentIndex(selected_index);
    selected_device_key = device_records.at(selected_index).toObject().value("key").toString();
}

QWidget* SignalBridgePlugin::CreateDeviceConfigPage(const QJsonObject& device)
{
    auto* scroll = new QScrollArea(devices_tab_bar);
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
        info_lines << tr("Serial: %1").arg(serial);
    }

    auto* source = new QLabel(info_lines.join('\n'), info_frame);
    source->setWordWrap(true);
    info_layout->addWidget(source, 1, 0);
    info_layout->setRowStretch(2, 1);

    layout->addWidget(controls_frame, 0, 0);
    layout->addWidget(info_frame, 0, 1);

    const QJsonArray parameters = device.value("parameters").toArray();
    const QJsonObject configuration = ConfigurationForDevice(key, device.value("script_key").toString());
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
            connect(combo, &QComboBox::currentTextChanged, this, [this, key, property](const QString& text) {
                SetDeviceConfigurationValue(key, property, text);
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
            connect(checkbox, &QCheckBox::toggled, this, [this, key, property](bool checked) {
                SetDeviceConfigurationValue(key, property, checked);
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
                connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key, property](double number) {
                    SetDeviceConfigurationValue(key, property, number);
                });
            }
            else
            {
                connect(spin, &QDoubleSpinBox::editingFinished, this, [this, spin, key, property]() {
                    SetDeviceConfigurationValue(key, property, spin->value());
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
        connect(line_edit, &QLineEdit::editingFinished, this, [this, line_edit, key, property]() {
            SetDeviceConfigurationValue(key, property, line_edit->text());
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

void SignalBridgePlugin::LoadDeviceConfigStore()
{
    std::lock_guard<std::mutex> lock(config_mutex);
    device_config_store = QJsonObject();

    try
    {
        QFile file(DeviceConfigStorePath(resource_manager));
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if(!document.isObject())
        {
            return;
        }

        const QJsonObject root = document.object();
        device_config_store = root.value("devices").isObject() ? root.value("devices").toObject() : root;
    }
    catch(...)
    {
        device_config_store = QJsonObject();
    }
}

void SignalBridgePlugin::SaveDeviceConfigStore() const
{
    QJsonObject snapshot;
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        snapshot = device_config_store;
    }

    try
    {
        const QString path = DeviceConfigStorePath(resource_manager);
        if(path.isEmpty())
        {
            return;
        }

        QJsonObject root;
        root.insert("version", 1);
        root.insert("devices", snapshot);

        QFile file(path);
        if(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        }
    }
    catch(...)
    {
    }
}

QJsonObject SignalBridgePlugin::ConfigurationForDevice(const QString& key, const QString& script_key) const
{
    std::lock_guard<std::mutex> lock(config_mutex);
    QJsonObject configuration = device_config_store.value(script_key).toObject();
    const QJsonObject exact = device_config_store.value(key).toObject();
    for(auto it = exact.begin(); it != exact.end(); ++it)
    {
        configuration.insert(it.key(), it.value());
    }
    return configuration;
}

QJsonObject SignalBridgePlugin::ConfigurationForDevice(const QString& key, const SignalBridgeScriptMeta& meta) const
{
    return ConfigurationForDevice(key, ConfigKeyForMeta(meta));
}

void SignalBridgePlugin::SetDeviceConfigurationValue(const QString& key, const QString& property, const QJsonValue& value)
{
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        QJsonObject device = device_config_store.value(key).toObject();
        device.insert(property, value);
        device_config_store.insert(key, device);
    }

    SaveDeviceConfigStore();
    ApplyConfigurationToControllers(key, property, value);
}

void SignalBridgePlugin::ApplyConfigurationToControllers(const QString& key, const QString& property, const QJsonValue& value)
{
    std::lock_guard<std::mutex> lock(controller_mutex);
    for(RGBController_SignalBridgeScript* controller : controllers)
    {
        if(controller == nullptr || QString::fromStdString(controller->ConfigKey()) != key)
        {
            continue;
        }
        controller->SetConfigurationValue(property, value);
    }
}

void SignalBridgePlugin::ShowLogView()
{
    SetActiveView(0);
}

void SignalBridgePlugin::ShowScriptListView()
{
    SetActiveView(1);
}

void SignalBridgePlugin::ShowDeviceListView()
{
    SetActiveView(2);
}

void SignalBridgePlugin::OnDeviceSelectionChanged()
{
    if(devices_tab_bar == nullptr)
    {
        return;
    }

    const int index = devices_tab_bar->currentIndex();
    if(index >= 0 && index < device_records.size())
    {
        selected_device_key = device_records.at(index).toObject().value("key").toString();
    }
    else
    {
        selected_device_key.clear();
    }
}

void SignalBridgePlugin::ApplyDiscoveryStatus(
    int generation,
    const QString& status,
    const QString& details,
    const QStringList& scripts,
    const QString& devices,
    bool running,
    int progress)
{
    if(generation != discovery_generation.load())
    {
        return;
    }

    status_message = status.toStdString();
    discovery_progress = std::clamp(progress, 0, 100);
    SetStatusText(status_message);
    if(!status.isEmpty())
    {
        AppendLogLine(QString("[Discovery] %1").arg(status));
    }
    if(!details.isEmpty())
    {
        AppendLogLine(QString("[Discovery]\n%1").arg(details));
    }

    if(progress_bar != nullptr)
    {
        progress_bar->setRange(0, 100);
        progress_bar->setValue(discovery_progress);
        progress_bar->setVisible(running);
    }

    SetScriptTable(scripts, running);
    if(!devices.isNull())
    {
        SetDeviceList(devices, running);
    }

    if(rescan_button != nullptr)
    {
        rescan_button->setVisible(!running);
        rescan_button->setEnabled(resource_manager != nullptr && !running);
    }
}
