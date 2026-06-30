#ifndef SIGNALBRIDGE_DEVICE_RECORDS_H
#define SIGNALBRIDGE_DEVICE_RECORDS_H

#include <cstdint>
#include <string>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "domain/ScriptTypes.h"
#include "hid/HidTypes.h"

namespace signalbridge
{
constexpr int ScriptTableColumnCount = 5;

QString FormatHex16(std::uint16_t value);
QString FormatPidList(const std::vector<std::uint16_t>& pids);
QString ConfigKeyForScript(const ScriptMeta& meta);
QString DeviceIdentityForHid(const HidInfo& hid);
QString ConfigKeyForDevice(const ScriptMeta& meta, const HidInfo& hid);
QJsonObject DeviceRecordForController(const ScriptMeta& meta, const HidInfo& hid, const QString& key);
QString CompactJsonArray(const QJsonArray& array);
QStringList FormatScriptTable(const std::vector<ScriptMeta>& scripts);
}

#endif // SIGNALBRIDGE_DEVICE_RECORDS_H
