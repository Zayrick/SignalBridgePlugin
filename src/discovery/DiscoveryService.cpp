#include "discovery/DiscoveryService.h"

#include <algorithm>
#include <memory>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "domain/DeviceRecords.h"
#include "hid/HidBackend.h"
#include "openrgb/SignalBridgeController.h"
#include "runtime/SignalRgbRuntimeFactory.h"
#include "scanning/ScriptScanner.h"
#include "serial/SerialBackend.h"

namespace signalbridge
{
namespace
{
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

bool IsStale(int generation, const DiscoveryCallbacks& callbacks)
{
    return callbacks.is_stale && callbacks.is_stale(generation);
}

void EmitStatus(
    int generation,
    const DiscoveryCallbacks& callbacks,
    const QString& status,
    const QString& details,
    const QStringList& scripts,
    const QString& devices,
    bool running,
    int progress)
{
    if(callbacks.status_changed)
    {
        callbacks.status_changed(generation, status, details, scripts, devices, running, progress);
    }
}

void EmitLog(const DiscoveryCallbacks& callbacks, const QString& line)
{
    if(callbacks.log_line)
    {
        callbacks.log_line(line);
    }
}

bool ValidateScriptEndpoint(
    const ScriptMeta& meta,
    const HidInfo& hid,
    int generation,
    const DiscoveryCallbacks& callbacks)
{
    if(!meta.has_validate)
    {
        return true;
    }

    try
    {
        ScriptLogCallback log_callback =
            [generation, callbacks](const std::string& source, const std::string& message) {
                if(IsStale(generation, callbacks))
                {
                    return;
                }
                EmitLog(callbacks, FormatScriptLogLine("Validate", source, message));
            };
        QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateValidation(meta, log_callback);
        QJsonObject endpoint;
        endpoint.insert("interface", hid.interface_number.value_or(-1));
        endpoint.insert("usage", hid.usage.value_or(0));
        endpoint.insert("usage_page", hid.usage_page.value_or(0));
        endpoint.insert("collection", hid.collection);

        QJsonArray args;
        args.append(endpoint);
        return runtime.CallModuleExportJson("Validate", args).toBool(false);
    }
    catch(...)
    {
        return false;
    }
}

bool IsSerialScript(const ScriptMeta& meta)
{
    return meta.transport_type == "serial";
}

bool IsHidScript(const ScriptMeta& meta)
{
    return meta.transport_type.empty() || meta.transport_type == "hid" || meta.transport_type == "hybrid";
}

}

void DiscoveryService::Discover(
    int generation,
    OpenRgbHostInterface* manager,
    ControllerRegistry& registry,
    DeviceConfigStore& config_store,
    const DiscoveryCallbacks& callbacks) const
{
    unsigned int matched = 0;
    unsigned int failed = 0;
    QStringList detail_lines;
    QStringList discovered_scripts;

    try
    {
        registry.UnregisterAndDeleteControllers(manager);

        if(IsStale(generation, callbacks))
        {
            return;
        }

        auto hid_backend = std::make_shared<HidBackend>();
        auto serial_backend = std::make_shared<SerialBackend>();

        const filesystem::path script_path = manager->GetConfigurationDirectory() / "SignalBridge" / "scripts";
        filesystem::create_directories(script_path);
        const auto script_path_utf8 = script_path.generic_u8string();
        const std::string script_dir(
            reinterpret_cast<const char*>(script_path_utf8.data()),
            script_path_utf8.size());
        int last_scan_progress = -1;
        ScriptLogCallback scan_log_callback =
            [generation, callbacks](const std::string& source, const std::string& message) {
                if(IsStale(generation, callbacks))
                {
                    return;
                }
                EmitLog(callbacks, FormatScriptLogLine("Scan", source, message));
            };
        const ScanReport report = ScanDirectory(
            script_dir,
            [generation, callbacks, &last_scan_progress](std::size_t completed, std::size_t total) {
                if(IsStale(generation, callbacks))
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
                EmitStatus(
                    generation,
                    callbacks,
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
        if(IsStale(generation, callbacks))
        {
            return;
        }

        discovered_scripts = FormatScriptTable(report.scripts);

        EmitStatus(generation, callbacks, "Enumerating HID and serial interfaces...", "", discovered_scripts, QString(), true, 72);
        const std::vector<HidInfo> hid_devices = hid_backend->Enumerate();
        const std::vector<SerialInfo> serial_devices = serial_backend->Enumerate();
        if(IsStale(generation, callbacks))
        {
            return;
        }
        EmitStatus(generation, callbacks, "Matching scripts to devices...", "", discovered_scripts, QString(), true, 80);

        std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<HidInfo>> by_vid_pid;
        for(const HidInfo& hid : hid_devices)
        {
            by_vid_pid[{ hid.vid, hid.pid }].push_back(hid);
        }
        std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<SerialInfo>> serial_by_vid_pid;
        for(const SerialInfo& serial : serial_devices)
        {
            if(serial.has_vid && serial.has_pid)
            {
                serial_by_vid_pid[{ serial.vid, serial.pid }].push_back(serial);
            }
        }

        std::set<std::string> open_groups;
        std::set<QString> open_serial_ports;
        QJsonArray registered_devices;
        detail_lines << QString("Script directory: %1").arg(QString::fromStdString(script_dir));
        detail_lines << QString("Scanned scripts: %1").arg(report.scripts.size());
        detail_lines << QString("Scan errors: %1").arg(report.errors.size());
        detail_lines << QString("HID interfaces: %1").arg(hid_devices.size());
        detail_lines << QString("Serial ports: %1").arg(serial_devices.size());
        detail_lines << "";

        int last_register_progress = 80;
        ScriptLogCallback runtime_log_callback =
            [generation, callbacks](const std::string& source, const std::string& message) {
                if(IsStale(generation, callbacks))
                {
                    return;
                }
                EmitLog(callbacks, FormatScriptLogLine("Runtime", source, message));
            };
        for(std::size_t meta_index = 0; meta_index < report.scripts.size(); meta_index++)
        {
            const ScriptMeta& meta = report.scripts[meta_index];
            if(IsStale(generation, callbacks))
            {
                return;
            }

            const int register_progress = 80 + static_cast<int>(((meta_index + 1) * 19) / std::max<std::size_t>(1, report.scripts.size()));
            if(register_progress != last_register_progress)
            {
                last_register_progress = register_progress;
                EmitStatus(
                    generation,
                    callbacks,
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
                if(IsHidScript(meta))
                {
                    const auto candidates_it = by_vid_pid.find({ *meta.vid, pid });
                    if(candidates_it != by_vid_pid.end())
                    {
                        for(const HidInfo& hid : candidates_it->second)
                        {
                            if(IsStale(generation, callbacks))
                            {
                                return;
                            }

                            const std::string group = HidBackend::NormalizeDevicePath(hid.path);
                            if(!group.empty() && open_groups.find(group) != open_groups.end())
                            {
                                continue;
                            }
                            if(!ValidateScriptEndpoint(meta, hid, generation, callbacks))
                            {
                                continue;
                            }

                            try
                            {
                                const QString config_key = ConfigKeyForDevice(meta, hid);
                                auto controller = std::make_unique<SignalBridgeController>(
                                    hid_backend,
                                    meta,
                                    hid,
                                    config_store.ConfigurationForDevice(config_key, meta),
                                    config_key.toStdString(),
                                    runtime_log_callback);
                                SignalBridgeController* raw_controller = registry.Register(manager, std::move(controller));
                                if(raw_controller == nullptr)
                                {
                                    throw std::runtime_error("failed to register controller");
                                }
                                open_groups.insert(group);
                                QJsonObject device_record = DeviceRecordForController(raw_controller->ScriptMetadata(), hid, config_key);
                                device_record.insert("name", QString::fromStdString(raw_controller->Name()));
                                registered_devices.append(device_record);
                                matched++;
                                detail_lines << QString("Registered: %1 [%2:%3] HID")
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

                if(IsSerialScript(meta))
                {
                    const auto candidates_it = serial_by_vid_pid.find({ *meta.vid, pid });
                    if(candidates_it == serial_by_vid_pid.end())
                    {
                        continue;
                    }

                    for(const SerialInfo& serial : candidates_it->second)
                    {
                        if(IsStale(generation, callbacks))
                        {
                            return;
                        }

                        const QString port = QString::fromStdString(serial.port_name);
                        if(port.isEmpty() || open_serial_ports.find(port) != open_serial_ports.end())
                        {
                            continue;
                        }

                        try
                        {
                            const QString config_key = ConfigKeyForDevice(meta, serial);
                            auto controller = std::make_unique<SignalBridgeController>(
                                serial_backend,
                                meta,
                                serial,
                                config_store.ConfigurationForDevice(config_key, meta),
                                config_key.toStdString(),
                                runtime_log_callback);
                            SignalBridgeController* raw_controller = registry.Register(manager, std::move(controller));
                            if(raw_controller == nullptr)
                            {
                                throw std::runtime_error("failed to register controller");
                            }
                            open_serial_ports.insert(port);
                            QJsonObject device_record = DeviceRecordForController(raw_controller->ScriptMetadata(), serial, config_key);
                            device_record.insert("name", QString::fromStdString(raw_controller->Name()));
                            registered_devices.append(device_record);
                            matched++;
                            detail_lines << QString("Registered: %1 [%2:%3] Serial %4")
                                                .arg(QString::fromStdString(meta.name))
                                                .arg(serial.vid, 4, 16, QLatin1Char('0'))
                                                .arg(serial.pid, 4, 16, QLatin1Char('0'))
                                                .arg(port);
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

        std::string status = "Registered " + std::to_string(matched) + " SignalRGB script device(s)";
        if(failed > 0)
        {
            status += ", " + std::to_string(failed) + " failed";
        }

        EmitStatus(generation,
                   callbacks,
                   QString::fromStdString(status),
                   detail_lines.join('\n'),
                   discovered_scripts,
                   QString::fromUtf8(QJsonDocument(registered_devices).toJson(QJsonDocument::Compact)),
                   false,
                   100);
    }
    catch(const std::exception& err)
    {
        EmitStatus(
            generation,
            callbacks,
            QString::fromStdString(std::string("SignalRGB script discovery failed: ") + err.what()),
            "",
            QStringList(),
            QStringLiteral("[]"),
            false,
            0);
    }
}
}
