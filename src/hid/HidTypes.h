#ifndef SIGNALBRIDGE_HID_TYPES_H
#define SIGNALBRIDGE_HID_TYPES_H

#include <cstdint>
#include <optional>
#include <string>

namespace signalbridge
{
struct HidInfo
{
    std::string path;
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    std::string serial;
    std::string manufacturer;
    std::string product;
    std::optional<int> interface_number;
    std::optional<std::uint16_t> usage;
    std::optional<std::uint16_t> usage_page;
};
}

#endif // SIGNALBRIDGE_HID_TYPES_H
