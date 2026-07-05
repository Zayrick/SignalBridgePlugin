#ifndef SIGNALBRIDGE_COLOR_FRAME_H
#define SIGNALBRIDGE_COLOR_FRAME_H

#include <cstdint>
#include <vector>

namespace signalbridge
{
struct ColorFrame
{
    std::vector<std::uint8_t> colors;
    int width = 1;
    int led_count = 0;
};

using RuntimeColorFrame = ColorFrame;
}

#endif // SIGNALBRIDGE_COLOR_FRAME_H
