#ifndef SIGNALBRIDGE_CONTROL_PARAMETERS_H
#define SIGNALBRIDGE_CONTROL_PARAMETERS_H

#include <vector>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace signalbridge
{
bool JsonBool(const QJsonValue& value, bool fallback);
double JsonNumber(const QJsonValue& value, double fallback);
QString JsonText(const QJsonValue& value, const QString& fallback = QString());

std::vector<QJsonObject> ExtractControlParameters(const QJsonValue& value);
void MergeControlParameters(std::vector<QJsonObject>& target, const QJsonValue& value);

QJsonValue ParameterDefaultValue(const QJsonObject& parameter);
QJsonValue NormalizeParameterValue(const QJsonObject& parameter, const QJsonValue& value);
double ParameterNumberBound(const QJsonObject& parameter, const char* key, double fallback);
double ParameterNumberStep(const QJsonObject& parameter);
int ParameterNumberDecimals(const QJsonObject& parameter, double step);
}

#endif // SIGNALBRIDGE_CONTROL_PARAMETERS_H
