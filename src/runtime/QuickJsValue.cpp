#include "runtime/QuickJsValue.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

namespace signalbridge
{
std::string JsonValueToString(const QJsonValue& value)
{
    const QByteArray wrapped = QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact);
    if(wrapped.size() < 2)
    {
        return "null";
    }
    return std::string(wrapped.constData() + 1, static_cast<std::size_t>(wrapped.size() - 2));
}

QJsonValue JsonValueFromString(const char* value)
{
    if(value == nullptr)
    {
        return QJsonValue();
    }

    const QByteArray wrapped = QByteArray("[") + value + "]";
    const QJsonDocument document = QJsonDocument::fromJson(wrapped);
    if(document.isArray() && !document.array().isEmpty())
    {
        return document.array().first();
    }
    return QJsonValue(QString::fromUtf8(value));
}

JSValue JsonToJsValue(JSContext* context, const QJsonValue& value, const std::string& name)
{
    const std::string json = JsonValueToString(value);
    JSValue parsed = JS_ParseJSON(context, json.c_str(), json.size(), name.c_str());
    if(JS_IsException(parsed))
    {
        return parsed;
    }
    return parsed;
}

QJsonValue JsValueToJson(JSContext* context, JSValueConst value)
{
    if(JS_IsUndefined(value) || JS_IsNull(value))
    {
        return QJsonValue();
    }
    if(JS_IsBool(value))
    {
        return QJsonValue(JS_ToBool(context, value) != 0);
    }
    if(JS_IsNumber(value))
    {
        double number = 0.0;
        if(JS_ToFloat64(context, &number, value) == 0)
        {
            return QJsonValue(number);
        }
    }
    if(JS_IsString(value))
    {
        const char* str = JS_ToCString(context, value);
        const QString result = QString::fromUtf8(str != nullptr ? str : "");
        if(str != nullptr)
        {
            JS_FreeCString(context, str);
        }
        return result;
    }

    JSValue json_string = JS_JSONStringify(context, value, JS_UNDEFINED, JS_UNDEFINED);
    if(JS_IsException(json_string) || JS_IsUndefined(json_string))
    {
        JS_FreeValue(context, json_string);
        return QJsonValue();
    }

    const char* str = JS_ToCString(context, json_string);
    QJsonValue result = JsonValueFromString(str);
    if(str != nullptr)
    {
        JS_FreeCString(context, str);
    }
    JS_FreeValue(context, json_string);
    return result;
}

std::vector<std::uint8_t> JsValueToBytes(JSContext* context, JSValueConst value)
{
    const QJsonValue json = JsValueToJson(context, value);
    const QJsonArray array = json.toArray();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(array.size()));
    for(const QJsonValue& item : array)
    {
        const int clamped = std::clamp(item.toInt(0), 0, 255);
        bytes.push_back(static_cast<std::uint8_t>(clamped));
    }
    return bytes;
}

JSValue BytesToJsArray(JSContext* context, const std::vector<std::uint8_t>& bytes)
{
    JSValue array = JS_NewArray(context);
    for(std::size_t idx = 0; idx < bytes.size(); idx++)
    {
        JS_SetPropertyUint32(context, array, static_cast<std::uint32_t>(idx), JS_NewInt32(context, bytes[idx]));
    }
    return array;
}

int JsToInt(JSContext* context, JSValueConst value, int fallback)
{
    int32_t result = fallback;
    if(JS_ToInt32(context, &result, value) != 0)
    {
        return fallback;
    }
    return result;
}

std::size_t JsToSize(JSContext* context, JSValueConst value, std::size_t fallback)
{
    uint32_t result = static_cast<uint32_t>(fallback);
    if(JS_ToUint32(context, &result, value) != 0)
    {
        return fallback;
    }
    return static_cast<std::size_t>(result);
}
}
