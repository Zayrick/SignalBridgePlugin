#include "scanning/ScriptMetadataExtractor.h"

#include <algorithm>
#include <stdexcept>

#include <QJsonArray>
#include <QJsonObject>

#include "domain/ControlParameters.h"
#include "runtime/SignalRgbRuntimeFactory.h"

namespace signalbridge
{
namespace
{
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

std::optional<ScriptMeta> ScriptMetadataExtractor::Extract(
    const ScriptSource& script,
    const std::vector<ScriptSource>& catalog,
    const ScriptLogCallback& log_callback) const
{
    QuickJsRuntime runtime = SignalRgbRuntimeFactory::CreateScan(log_callback, script.lookup_path);
    runtime.LoadModule(script.lookup_path, catalog);
    runtime.EvaluateModule();
    if(!runtime.HasModuleExport("Name"))
    {
        return std::nullopt;
    }

    ScriptMeta meta;
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
    MergeControlParameters(meta.control_parameters, runtime.ExportProperties());
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
}
