#ifndef SIGNALBRIDGE_TOPOLOGY_MAPPER_H
#define SIGNALBRIDGE_TOPOLOGY_MAPPER_H

#include <string>
#include <vector>

#include <QJsonObject>

#include "RGBController/RGBController.h"
#include "domain/ScriptTypes.h"

namespace signalbridge
{
struct ZoneTarget
{
    enum class Kind
    {
        Main,
        Channel,
        Subdevice,
    };

    Kind kind = Kind::Main;
    std::string key;
    unsigned int width = 1;
    unsigned int height = 1;
    std::vector<int> matrix_map;
    std::vector<std::string> led_names;
};

struct TopologyResult
{
    std::string controller_name;
    std::vector<zone> zones;
    std::vector<ZoneTarget> targets;
};

TopologyResult BuildOpenRgbTopology(const QJsonObject& topology, const ScriptMeta& meta, const std::string& current_name);
void DeleteZoneMaps(std::vector<zone>& zones);
void RebuildOpenRgbLedList(
    const std::vector<zone>& zones,
    const std::vector<ZoneTarget>& targets,
    device_type type,
    std::vector<led>& leds,
    std::vector<std::string>& led_alt_names);
}

#endif // SIGNALBRIDGE_TOPOLOGY_MAPPER_H
