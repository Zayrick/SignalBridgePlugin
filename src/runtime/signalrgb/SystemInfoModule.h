#ifndef SIGNALBRIDGE_SIGNALRGB_SYSTEM_INFO_MODULE_H
#define SIGNALBRIDGE_SIGNALRGB_SYSTEM_INFO_MODULE_H

#include <QJsonObject>

namespace signalbridge
{
QJsonObject MotherboardInfo();
QJsonObject BiosInfo();
QJsonObject RamInfo();
}

#endif // SIGNALBRIDGE_SIGNALRGB_SYSTEM_INFO_MODULE_H
