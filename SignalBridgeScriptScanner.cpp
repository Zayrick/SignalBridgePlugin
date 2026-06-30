#include "SignalBridgeScriptScanner.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

namespace
{
QString NormalizeLookupPath(QString path)
{
    path.replace('\\', '/');
    QStringList parts;
    for(const QString& part : path.split('/'))
    {
        if(part.isEmpty() || part == ".")
        {
            continue;
        }
        if(part == "..")
        {
            if(!parts.isEmpty())
            {
                parts.removeLast();
            }
            continue;
        }
        parts.append(part);
    }
    return parts.join('/');
}

std::optional<std::uint16_t> ValueToU16(const QJsonValue& value)
{
    bool ok = false;
    int parsed = 0;
    if(value.isDouble())
    {
        parsed = static_cast<int>(value.toDouble());
        ok = true;
    }
    else if(value.isString())
    {
        QString text = value.toString().trimmed();
        if(text.startsWith("0x", Qt::CaseInsensitive))
        {
            parsed = text.mid(2).toInt(&ok, 16);
        }
        else
        {
            parsed = text.toInt(&ok, 10);
        }
    }

    if(!ok || parsed < 0 || parsed > 0xFFFF)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::vector<std::uint16_t> ExtractPidList(const QJsonValue& value)
{
    if(const std::optional<std::uint16_t> pid = ValueToU16(value))
    {
        return { *pid };
    }

    std::vector<std::uint16_t> pids;
    for(const QJsonValue& item : value.toArray())
    {
        if(const std::optional<std::uint16_t> pid = ValueToU16(item))
        {
            pids.push_back(*pid);
        }
    }
    return pids;
}

std::vector<std::string> ExtractStringArray(const QJsonValue& value)
{
    std::vector<std::string> result;
    for(const QJsonValue& item : value.toArray())
    {
        result.push_back(item.toString().toStdString());
    }
    return result;
}

std::vector<std::pair<int, int>> ExtractPositions(const QJsonValue& value)
{
    std::vector<std::pair<int, int>> result;
    for(const QJsonValue& item : value.toArray())
    {
        if(item.isArray())
        {
            const QJsonArray array = item.toArray();
            if(array.size() >= 2)
            {
                result.push_back({ array.at(0).toInt(), array.at(1).toInt() });
            }
        }
        else if(item.isObject())
        {
            const QJsonObject object = item.toObject();
            result.push_back({ object.value("x").toInt(), object.value("y").toInt() });
        }
    }
    return result;
}

std::vector<QJsonObject> ExtractControlParameters(const QJsonValue& value)
{
    std::vector<QJsonObject> result;
    for(const QJsonValue& item : value.toArray())
    {
        if(!item.isObject())
        {
            continue;
        }

        QJsonObject parameter = item.toObject();
        const QString property = parameter.value("property").toString().trimmed();
        if(property.isEmpty())
        {
            continue;
        }

        parameter.insert("property", property);
        if(parameter.value("label").toString().isEmpty())
        {
            parameter.insert("label", property);
        }
        if(parameter.value("type").toString().isEmpty())
        {
            parameter.insert("type", "text");
        }

        result.push_back(parameter);
    }
    return result;
}

void MergeControlParameters(std::vector<QJsonObject>& target, const QJsonValue& value)
{
    for(const QJsonObject& parameter : ExtractControlParameters(value))
    {
        const QString property = parameter.value("property").toString();
        const auto existing = std::find_if(target.begin(), target.end(), [&property](const QJsonObject& item) {
            return item.value("property").toString() == property;
        });
        if(existing == target.end())
        {
            target.push_back(parameter);
        }
        else
        {
            *existing = parameter;
        }
    }
}

unsigned int JsonUInt(const QJsonValue& value, unsigned int fallback)
{
    if(!value.isDouble())
    {
        return fallback;
    }
    const double raw = value.toDouble();
    if(raw < 0.0)
    {
        return fallback;
    }
    return static_cast<unsigned int>(raw);
}
}

SignalBridgeScanReport SignalBridgeScriptScanner::ScanDirectory(
    const std::string& script_directory,
    SignalBridgeScanProgressCallback progress_callback) const
{
    SignalBridgeScanReport report;
    const QDir root(QString::fromStdString(script_directory));
    std::vector<QString> paths;

    QDirIterator it(root.absolutePath(), QStringList{ "*.js" }, QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext())
    {
        paths.push_back(it.next());
    }
    std::sort(paths.begin(), paths.end());

    std::vector<SignalBridgeScriptSource> sources;
    sources.reserve(paths.size());
    for(const QString& path : paths)
    {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            report.errors.push_back({ path.toStdString(), "read error" });
            continue;
        }
        const QString relative = NormalizeLookupPath(root.relativeFilePath(path));
        sources.push_back({
            path.toStdString(),
            relative.toStdString(),
            QString::fromUtf8(file.readAll()).toStdString(),
        });
    }

    std::size_t completed = 0;
    for(const SignalBridgeScriptSource& source : sources)
    {
        try
        {
            std::optional<SignalBridgeScriptMeta> meta = ScanScript(source, sources);
            if(meta.has_value())
            {
                report.scripts.push_back(std::move(*meta));
            }
        }
        catch(const std::exception& err)
        {
            report.errors.push_back({ source.source_path, err.what() });
        }

        completed++;
        if(progress_callback)
        {
            progress_callback(completed, sources.size(), source.source_path);
        }
    }

    return report;
}

std::optional<SignalBridgeScriptMeta> SignalBridgeScriptScanner::ScanScript(
    const SignalBridgeScriptSource& script,
    const std::vector<SignalBridgeScriptSource>& catalog) const
{
    SignalBridgeJsRuntime runtime = SignalBridgeJsRuntime::CreateScan();
    runtime.LoadModule(script.lookup_path, catalog);
    runtime.EvaluateModule();
    if(!runtime.HasModuleExport("Name"))
    {
        return std::nullopt;
    }

    SignalBridgeScriptMeta meta;
    meta.source_path = script.source_path;
    meta.lookup_path = script.lookup_path;
    meta.module_sources = runtime.LoadedModuleSources();

    meta.name = runtime.CallModuleExportJson("Name").toString().toStdString();
    if(meta.name.empty())
    {
        throw std::runtime_error("Name() did not return a string");
    }

    if(runtime.HasModuleExport("ControllableParameters"))
    {
        MergeControlParameters(meta.control_parameters, runtime.CallModuleExportJson("ControllableParameters"));
    }
    MergeControlParameters(meta.control_parameters, runtime.CallGlobalJson("__srgb_export_properties"));
    runtime.ApplyConfiguration(meta, QJsonObject());

    meta.vid = ValueToU16(runtime.CallModuleExportJson("VendorId"));
    meta.pids = ExtractPidList(runtime.CallModuleExportJson("ProductId"));

    const QJsonArray size = runtime.CallModuleExportJson("Size").toArray();
    meta.width = size.size() > 0 ? std::max(1u, JsonUInt(size.at(0), 1)) : 1;
    meta.height = size.size() > 1 ? std::max(1u, JsonUInt(size.at(1), 1)) : 1;
    meta.device_type = runtime.CallModuleExportJson("DeviceType").toString().toStdString();
    meta.publisher = runtime.CallModuleExportJson("Publisher").toString().toStdString();
    meta.image_url = runtime.CallModuleExportJson("ImageUrl").toString().toStdString();
    meta.led_names = ExtractStringArray(runtime.CallModuleExportJson("LedNames"));
    meta.led_positions = ExtractPositions(runtime.CallModuleExportJson("LedPositions"));
    meta.has_validate = runtime.HasModuleExport("Validate");

    return meta;
}
