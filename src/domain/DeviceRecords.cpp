#include "domain/DeviceRecords.h"

#include <QFileInfo>
#include <QJsonDocument>

#include "hid/HidBackend.h"

namespace signalbridge
{
QString FormatHex16(std::uint16_t value)
{
    return QString("0x%1").arg(value, 4, 16, QLatin1Char('0'));
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

QString ConfigKeyForScript(const ScriptMeta& meta)
{
    QString key = QString::fromStdString(meta.lookup_path.empty() ? meta.source_path : meta.lookup_path);
    key.replace('\\', '/');
    return key;
}

QString DeviceIdentityForHid(const HidInfo& hid)
{
    if(!hid.serial.empty())
    {
        return QString::fromStdString(hid.serial);
    }

    const std::string normalized = HidBackend::NormalizeDevicePath(hid.path);
    if(!normalized.empty())
    {
        return QString::fromStdString(normalized);
    }
    return QString::fromStdString(hid.path);
}

QString DeviceIdentityForSerial(const SerialInfo& serial)
{
    if(!serial.serial_number.empty())
    {
        return QString::fromStdString(serial.serial_number);
    }
    if(!serial.port_name.empty())
    {
        return QString::fromStdString(serial.port_name);
    }
    return QString::fromStdString(serial.system_location);
}

QString ConfigKeyForDevice(const ScriptMeta& meta, const HidInfo& hid)
{
    return ConfigKeyForScript(meta) + "|" + DeviceIdentityForHid(hid);
}

QString ConfigKeyForDevice(const ScriptMeta& meta, const SerialInfo& serial)
{
    return ConfigKeyForScript(meta) + "|" + DeviceIdentityForSerial(serial);
}

QJsonObject DeviceRecordForController(const ScriptMeta& meta, const HidInfo& hid, const QString& key)
{
    const QString source_path = QString::fromStdString(meta.source_path);

    QJsonArray parameters;
    for(const QJsonObject& parameter : meta.control_parameters)
    {
        parameters.append(parameter);
    }

    QJsonObject device;
    device.insert("key", key);
    device.insert("script_key", ConfigKeyForScript(meta));
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

QJsonObject DeviceRecordForController(const ScriptMeta& meta, const SerialInfo& serial, const QString& key)
{
    const QString source_path = QString::fromStdString(meta.source_path);

    QJsonArray parameters;
    for(const QJsonObject& parameter : meta.control_parameters)
    {
        parameters.append(parameter);
    }

    QJsonObject device;
    device.insert("key", key);
    device.insert("script_key", ConfigKeyForScript(meta));
    device.insert("name", QString::fromStdString(meta.name));
    device.insert("file", QFileInfo(source_path).fileName());
    device.insert("source_path", source_path);
    device.insert("vid", meta.vid.has_value() ? FormatHex16(*meta.vid) : (serial.has_vid ? FormatHex16(serial.vid) : QString()));
    device.insert("pids", meta.pids.empty() ? (serial.has_pid ? FormatHex16(serial.pid) : QString()) : FormatPidList(meta.pids));
    device.insert("device_type", QString::fromStdString(meta.device_type));
    device.insert("transport_type", QString::fromStdString(meta.transport_type));
    device.insert("publisher", QString::fromStdString(meta.publisher));
    device.insert("serial", QString::fromStdString(serial.serial_number));
    device.insert("location", QString::fromStdString(!serial.port_name.empty() ? serial.port_name : serial.system_location));
    device.insert("parameters", parameters);
    return device;
}

QString CompactJsonArray(const QJsonArray& array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList FormatScriptTable(const std::vector<ScriptMeta>& scripts)
{
    QStringList cells;

    for(const ScriptMeta& meta : scripts)
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
              << QString::fromStdString(meta.transport_type.empty() || meta.transport_type == "hid" ? meta.device_type : meta.device_type + " (" + meta.transport_type + ")")
              << name
              << QString::fromStdString(meta.publisher);
    }

    return cells;
}
}
