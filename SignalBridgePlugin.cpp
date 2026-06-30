#include "SignalBridgePlugin.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

#include <QAbstractItemView>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "RGBController_SignalBridgeScript.h"
#include "SignalBridgeHid.h"
#include "SignalBridgeScriptScanner.h"

namespace
{
constexpr int ScriptTableColumnCount = 5;

QString FormatHex16(std::uint16_t value)
{
    return QString("0x%1").arg(value, 4, 16, QLatin1Char('0'));
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
}

SignalBridgePlugin::SignalBridgePlugin(QObject* parent)
    : QObject(parent)
{
    connect(this,
            &SignalBridgePlugin::DiscoveryStatusChanged,
            this,
            &SignalBridgePlugin::ApplyDiscoveryStatus,
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
    details_message.clear();
    script_table_items.clear();
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
    details_message.clear();
    script_table_items.clear();

    if(status_label != nullptr)
    {
        status_label->setText(QString::fromStdString(status_message));
    }
    if(details_text != nullptr)
    {
        details_text->clear();
    }
    SetScriptTable(script_table_items, false);
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

    layout->addLayout(view_buttons_layout);

    connect(rescan_button, &QPushButton::clicked, this, &SignalBridgePlugin::DiscoverSignalRgbDevices);
    connect(log_view_button, &QPushButton::clicked, this, &SignalBridgePlugin::ShowLogView);
    connect(script_list_view_button, &QPushButton::clicked, this, &SignalBridgePlugin::ShowScriptListView);
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
    ApplyDiscoveryStatus(generation, "Scanning SignalRGB scripts...", "", QStringList(), true, 0);

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
                    true,
                    progress);
            });
        if(IsDiscoveryStale(generation))
        {
            discovery_running.store(false);
            return;
        }

        discovered_scripts = FormatScriptTable(report.scripts);

        emit DiscoveryStatusChanged(generation, "Enumerating HID interfaces...", "", discovered_scripts, true, 72);
        const std::vector<SignalBridgeHidInfo> hid_devices = new_hid_backend->Enumerate();
        if(IsDiscoveryStale(generation))
        {
            discovery_running.store(false);
            return;
        }
        emit DiscoveryStatusChanged(generation, "Matching scripts to HID devices...", "", discovered_scripts, true, 80);

        std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<SignalBridgeHidInfo>> by_vid_pid;
        for(const SignalBridgeHidInfo& hid : hid_devices)
        {
            by_vid_pid[{ hid.vid, hid.pid }].push_back(hid);
        }

        std::set<std::string> open_groups;
        detail_lines << QString("Script directory: %1").arg(QString::fromStdString(script_dir));
        detail_lines << QString("Scanned scripts: %1").arg(report.scripts.size());
        detail_lines << QString("Scan errors: %1").arg(report.errors.size());
        detail_lines << QString("HID interfaces: %1").arg(hid_devices.size());
        detail_lines << "";

        int last_register_progress = 80;
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
                    if(!ValidateScriptEndpoint(meta, hid))
                    {
                        continue;
                    }

                    try
                    {
                        auto* controller = new RGBController_SignalBridgeScript(new_hid_backend, meta, hid);
                        manager->RegisterRGBController(controller);
                        controllers.push_back(controller);
                        open_groups.insert(group);
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

bool SignalBridgePlugin::ValidateScriptEndpoint(const SignalBridgeScriptMeta& meta, const SignalBridgeHidInfo& hid) const
{
    if(!meta.has_validate)
    {
        return true;
    }

    try
    {
        SignalBridgeJsRuntime runtime = SignalBridgeJsRuntime::CreateValidation(meta);
        QJsonObject endpoint;
        endpoint.insert("interface", hid.interface_number.value_or(-1));
        endpoint.insert("usage", hid.usage.value_or(0));
        endpoint.insert("usage_page", hid.usage_page.value_or(0));
        endpoint.insert("collection", 0);

        QJsonArray args;
        args.append(endpoint);
        return runtime.CallGlobalJson("Validate", args).toBool(false);
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

void SignalBridgePlugin::ShowLogView()
{
    SetActiveView(0);
}

void SignalBridgePlugin::ShowScriptListView()
{
    SetActiveView(1);
}

void SignalBridgePlugin::ApplyDiscoveryStatus(int generation, const QString& status, const QString& details, const QStringList& scripts, bool running, int progress)
{
    if(generation != discovery_generation.load())
    {
        return;
    }

    status_message = status.toStdString();
    details_message = details.toStdString();
    discovery_progress = std::clamp(progress, 0, 100);
    SetStatusText(status_message);

    if(progress_bar != nullptr)
    {
        progress_bar->setRange(0, 100);
        progress_bar->setValue(discovery_progress);
        progress_bar->setVisible(running);
    }

    if(details_text != nullptr)
    {
        details_text->setPlainText(details);
    }
    SetScriptTable(scripts, running);

    if(rescan_button != nullptr)
    {
        rescan_button->setVisible(!running);
        rescan_button->setEnabled(resource_manager != nullptr && !running);
    }
}
