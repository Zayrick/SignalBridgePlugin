#ifndef SIGNALBRIDGE_HID_REPORT_DESCRIPTOR_H
#define SIGNALBRIDGE_HID_REPORT_DESCRIPTOR_H

#include <cstdint>
#include <vector>

namespace signalbridge
{
struct HidTopLevelCollection
{
    std::uint16_t usage = 0;
    std::uint16_t usage_page = 0;
    int collection = 0;
};

std::vector<HidTopLevelCollection> ParseHidTopLevelCollections(
    const std::vector<std::uint8_t>& descriptor);
}

#endif // SIGNALBRIDGE_HID_REPORT_DESCRIPTOR_H
