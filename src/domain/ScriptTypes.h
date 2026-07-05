#ifndef SIGNALBRIDGE_SCRIPT_TYPES_H
#define SIGNALBRIDGE_SCRIPT_TYPES_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QJsonObject>

namespace signalbridge
{
struct ScriptSource
{
    std::string source_path;
    std::string lookup_path;
    std::string source;
};

struct ScriptMeta
{
    std::string source_path;
    std::string lookup_path;
    std::string name;
    std::optional<std::uint16_t> vid;
    std::vector<std::uint16_t> pids;
    unsigned int width = 1;
    unsigned int height = 1;
    std::string transport_type;
    std::string device_type;
    std::string publisher;
    std::vector<std::string> led_names;
    std::vector<std::pair<int, int>> led_positions;
    std::vector<QJsonObject> control_parameters;
    bool has_validate = false;
    std::vector<ScriptSource> module_sources;
};

struct EndpointDescriptor
{
    int interface_number = 0;
    int usage = 0;
    int usage_page = 0;
};

using ScriptLogCallback = std::function<void(const std::string& source, const std::string& message)>;
}

#endif // SIGNALBRIDGE_SCRIPT_TYPES_H
