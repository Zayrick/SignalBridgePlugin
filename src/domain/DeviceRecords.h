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
#include "serial/SerialTypes.h"

namespace signalbridge
{
constexpr int ScriptTableColumnCount = 5;

QString ConfigKeyForScript(const ScriptMeta& meta);
QString ConfigKeyForDevice(const ScriptMeta& meta, const HidInfo& hid);
QString ConfigKeyForDevice(const ScriptMeta& meta, const SerialInfo& serial);
QJsonObject DeviceRecordForController(const ScriptMeta& meta, const HidInfo& hid, const QString& key);
QJsonObject DeviceRecordForController(const ScriptMeta& meta, const SerialInfo& serial, const QString& key);
QStringList FormatScriptTable(const std::vector<ScriptMeta>& scripts);
}

#endif // SIGNALBRIDGE_DEVICE_RECORDS_H
