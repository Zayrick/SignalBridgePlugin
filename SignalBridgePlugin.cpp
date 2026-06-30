#include "SignalBridgePlugin.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "RGBController_SignalBridgeScript.h"
#include "SignalBridgeHid.h"
#include "SignalBridgeScriptScanner.h"

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

    if(status_label != nullptr)
    {
        status_label->setText(QString::fromStdString(status_message));
    }
    if(details_text != nullptr)
    {
        details_text->clear();
    }
    if(progress_bar != nullptr)
    {
        progress_bar->setRange(0, 100);
        progress_bar->setValue(0);
    }
    if(rescan_button != nullptr)
    {
        rescan_button->setEnabled(false);
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

    details_text = new QPlainTextEdit(widget);
    details_text->setObjectName("SignalBridgePluginDetailsText");
    details_text->setReadOnly(true);
    layout->addWidget(details_text);

    connect(rescan_button, &QPushButton::clicked, this, &SignalBridgePlugin::DiscoverSignalRgbDevices);
    rescan_button->setEnabled(resource_manager != nullptr && !discovery_running.load());
    SetStatusText(status_message);
    if(details_text != nullptr)
    {
        details_text->setPlainText(QString::fromStdString(details_message));
    }
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
    ApplyDiscoveryStatus(generation, "Scanning SignalRGB scripts...", "", true, 0);

    discovery_thread = std::thread(&SignalBridgePlugin::DiscoveryWorker, this, generation, resource_manager);
}

void SignalBridgePlugin::DiscoveryWorker(int generation, ResourceManagerInterface* manager)
{
    unsigned int matched = 0;
    unsigned int failed = 0;
    QStringList detail_lines;

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
                    true,
                    progress);
            });
        if(IsDiscoveryStale(generation))
        {
            discovery_running.store(false);
            return;
        }

        emit DiscoveryStatusChanged(generation, "Enumerating HID interfaces...", "", true, 72);
        const std::vector<SignalBridgeHidInfo> hid_devices = new_hid_backend->Enumerate();
        if(IsDiscoveryStale(generation))
        {
            discovery_running.store(false);
            return;
        }
        emit DiscoveryStatusChanged(generation, "Matching scripts to HID devices...", "", true, 80);

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

void SignalBridgePlugin::ApplyDiscoveryStatus(int generation, const QString& status, const QString& details, bool running, int progress)
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
    }

    if(details_text != nullptr)
    {
        details_text->setPlainText(details);
    }

    if(rescan_button != nullptr)
    {
        rescan_button->setEnabled(resource_manager != nullptr && !running);
    }
}
