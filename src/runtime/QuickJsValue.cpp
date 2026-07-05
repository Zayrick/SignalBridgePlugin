#include "runtime/QuickJsValue.h"

#include <algorithm>
#include <cstring>

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

namespace signalbridge
{
namespace
{
constexpr std::size_t kMaxByteArrayLength = 16 * 1024 * 1024;

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

void DiscardPendingException(JSContext* context)
{
    JSValue exception = JS_GetException(context);
    JS_FreeValue(context, exception);
}

std::uint8_t JsValueToByte(JSContext* context, JSValueConst value)
{
    int32_t number = 0;
    if(JS_ToInt32(context, &number, value) != 0)
    {
        DiscardPendingException(context);
        return 0;
    }
    return static_cast<std::uint8_t>(std::clamp(number, 0, 255));
}

std::vector<std::uint8_t> ArrayBufferToBytes(JSContext* context, JSValueConst value, std::size_t offset, std::size_t length)
{
    std::size_t buffer_size = 0;
    std::uint8_t* buffer = JS_GetArrayBuffer(context, &buffer_size, value);
    if(buffer == nullptr)
    {
        DiscardPendingException(context);
        return {};
    }
    if(offset > buffer_size)
    {
        return {};
    }

    const std::size_t size = std::min({ length, buffer_size - offset, kMaxByteArrayLength });
    std::vector<std::uint8_t> bytes(size);
    if(size > 0)
    {
        std::memcpy(bytes.data(), buffer + offset, size);
    }
    return bytes;
}

std::vector<std::uint8_t> TypedArrayToBytes(JSContext* context, JSValueConst value)
{
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t bytes_per_element = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(context, value, &offset, &length, &bytes_per_element);
    if(JS_IsException(buffer))
    {
        JS_FreeValue(context, buffer);
        DiscardPendingException(context);
        return {};
    }

    std::vector<std::uint8_t> bytes = ArrayBufferToBytes(context, buffer, offset, length);
    JS_FreeValue(context, buffer);
    return bytes;
}

std::vector<std::uint8_t> ArrayLikeToBytes(JSContext* context, JSValueConst value)
{
    JSValue length_value = JS_GetPropertyStr(context, value, "length");
    if(JS_IsException(length_value))
    {
        JS_FreeValue(context, length_value);
        DiscardPendingException(context);
        return {};
    }

    uint32_t length = 0;
    if(JS_ToUint32(context, &length, length_value) != 0)
    {
        JS_FreeValue(context, length_value);
        DiscardPendingException(context);
        return {};
    }
    JS_FreeValue(context, length_value);

    length = static_cast<uint32_t>(std::min<std::size_t>(length, kMaxByteArrayLength));
    std::vector<std::uint8_t> bytes;
    bytes.reserve(length);
    for(uint32_t idx = 0; idx < length; idx++)
    {
        JSValue item = JS_GetPropertyUint32(context, value, idx);
        if(JS_IsException(item))
        {
            JS_FreeValue(context, item);
            DiscardPendingException(context);
            bytes.push_back(0);
            continue;
        }
        bytes.push_back(JsValueToByte(context, item));
        JS_FreeValue(context, item);
    }
    return bytes;
}
}

JSValue JsonToJsValue(JSContext* context, const QJsonValue& value, const std::string& name)
{
    const std::string json = JsonValueToString(value);
    return JS_ParseJSON(context, json.c_str(), json.size(), name.c_str());
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
    if(JS_IsArrayBuffer(value))
    {
        return ArrayBufferToBytes(context, value, 0, kMaxByteArrayLength);
    }

    if(JS_GetTypedArrayType(value) >= 0)
    {
        return TypedArrayToBytes(context, value);
    }

    if(JS_IsObject(value))
    {
        return ArrayLikeToBytes(context, value);
    }

    return {};
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
