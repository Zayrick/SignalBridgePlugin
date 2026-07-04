#ifndef SIGNALBRIDGE_SERIAL_TYPES_H
#define SIGNALBRIDGE_SERIAL_TYPES_H

#include <cstdint>
#include <string>

namespace signalbridge
{
struct SerialInfo
{
    std::string port_name;
    std::string system_location;
    std::string description;
    std::string manufacturer;
    std::string serial_number;
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    bool has_vid = false;
    bool has_pid = false;
};

enum class SerialParity
{
    None,
    Odd,
    Even,
};

enum class SerialStopBits
{
    One,
    Two,
};

struct SerialOptions
{
    std::string port_name;
    int baud_rate = 115200;
    SerialParity parity = SerialParity::None;
    int data_bits = 8;
    SerialStopBits stop_bits = SerialStopBits::One;
};
}

#endif // SIGNALBRIDGE_SERIAL_TYPES_H
