#include "domain/ControlParameters.h"

#include <algorithm>

#include <QJsonArray>

namespace signalbridge
{
bool JsonBool(const QJsonValue& value, bool fallback)
{
    if(value.isBool())
    {
        return value.toBool();
    }
    if(value.isString())
    {
        const QString text = value.toString().trimmed().toLower();
        if(text == "true" || text == "1" || text == "yes" || text == "on")
        {
            return true;
        }
        if(text == "false" || text == "0" || text == "no" || text == "off")
        {
            return false;
        }
    }
    return fallback;
}

double JsonNumber(const QJsonValue& value, double fallback)
{
    if(value.isDouble())
    {
        return value.toDouble();
    }
    if(value.isString())
    {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if(ok)
        {
            return parsed;
        }
    }
    return fallback;
}

QString JsonText(const QJsonValue& value, const QString& fallback)
{
    if(value.isString())
    {
        return value.toString();
    }
    if(value.isBool())
    {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if(value.isDouble())
    {
        return QString::number(value.toDouble());
    }
    return fallback;
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

QJsonValue ParameterDefaultValue(const QJsonObject& parameter)
{
    const QString type = parameter.value("type").toString().toLower();
    const QJsonValue fallback = parameter.value("default");

    if(type == "boolean" || type == "checkbox")
    {
        return JsonBool(fallback, false);
    }
    if(type == "number")
    {
        return JsonNumber(fallback, 0.0);
    }
    if(type == "combobox" || type == "select")
    {
        const QJsonArray values = parameter.value("values").toArray();
        if(fallback.isString())
        {
            return fallback.toString();
        }
        return values.isEmpty() ? QString() : JsonText(values.first());
    }
    return JsonText(fallback);
}

QJsonValue NormalizeParameterValue(const QJsonObject& parameter, const QJsonValue& value)
{
    const QString type = parameter.value("type").toString().toLower();
    const QJsonValue fallback = ParameterDefaultValue(parameter);

    if(value.isUndefined() || value.isNull())
    {
        return fallback;
    }
    if(type == "boolean" || type == "checkbox")
    {
        return JsonBool(value, fallback.toBool(false));
    }
    if(type == "number")
    {
        return JsonNumber(value, fallback.toDouble(0.0));
    }
    if(type == "combobox" || type == "select")
    {
        const QString text = JsonText(value, fallback.toString());
        const QJsonArray values = parameter.value("values").toArray();
        if(values.isEmpty())
        {
            return text;
        }
        for(const QJsonValue& candidate : values)
        {
            if(JsonText(candidate) == text)
            {
                return text;
            }
        }
        return fallback;
    }
    return JsonText(value, fallback.toString());
}

double ParameterNumberBound(const QJsonObject& parameter, const char* key, double fallback)
{
    const QJsonValue value = parameter.value(key);
    return value.isUndefined() ? fallback : JsonNumber(value, fallback);
}

double ParameterNumberStep(const QJsonObject& parameter)
{
    const double step = ParameterNumberBound(parameter, "step", 1.0);
    return step > 0.0 ? step : 1.0;
}

namespace
{
int DecimalPlaces(double value)
{
    QString text = QString::number(value, 'f', 6);
    while(text.contains('.') && text.endsWith('0'))
    {
        text.chop(1);
    }
    if(text.endsWith('.'))
    {
        text.chop(1);
    }

    const int dot = text.indexOf('.');
    return dot < 0 ? 0 : text.size() - dot - 1;
}
}

int ParameterNumberDecimals(const QJsonObject& parameter, double step)
{
    int decimals = DecimalPlaces(step);
    for(const char* key : { "min", "max", "default" })
    {
        const QJsonValue value = parameter.value(key);
        if(!value.isUndefined())
        {
            decimals = std::max(decimals, DecimalPlaces(JsonNumber(value, 0.0)));
        }
    }
    return std::clamp(decimals, 0, 6);
}
}
