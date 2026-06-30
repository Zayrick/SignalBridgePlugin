#ifndef SIGNALBRIDGE_QUICKJS_VALUE_H
#define SIGNALBRIDGE_QUICKJS_VALUE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <QJsonValue>

extern "C" {
#include "quickjs.h"
}

namespace signalbridge
{
std::string JsonValueToString(const QJsonValue& value);
QJsonValue JsonValueFromString(const char* value);
JSValue JsonToJsValue(JSContext* context, const QJsonValue& value, const std::string& name);
QJsonValue JsValueToJson(JSContext* context, JSValueConst value);
std::vector<std::uint8_t> JsValueToBytes(JSContext* context, JSValueConst value);
JSValue BytesToJsArray(JSContext* context, const std::vector<std::uint8_t>& bytes);
int JsToInt(JSContext* context, JSValueConst value, int fallback);
std::size_t JsToSize(JSContext* context, JSValueConst value, std::size_t fallback);
}

#endif // SIGNALBRIDGE_QUICKJS_VALUE_H
