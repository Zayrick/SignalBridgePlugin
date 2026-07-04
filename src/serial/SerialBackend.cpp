#include "serial/SerialBackend.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "CSerialPort/SerialPort.h"
#include "CSerialPort/SerialPortInfo.h"

namespace signalbridge
{
namespace
{
std::string SafeString(const char* value)
{
    return value != nullptr ? std::string(value) : std::string();
}

std::string StripWindowsDevicePrefix(const std::string& port_name)
{
    const std::string prefix = "\\\\.\\";
    if(port_name.rfind(prefix, 0) == 0)
    {
        return port_name.substr(prefix.size());
    }
    return port_name;
}

std::string PortNameForOpen(const SerialInfo& info, const SerialOptions& options)
{
    if(!options.port_name.empty())
    {
        return StripWindowsDevicePrefix(options.port_name);
    }
    if(!info.port_name.empty())
    {
        return StripWindowsDevicePrefix(info.port_name);
    }
    return StripWindowsDevicePrefix(info.system_location);
}

bool IsHexDigit(char value)
{
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

std::string ToUpperAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

bool ParseHex16(const std::string& value, std::uint16_t& output)
{
    if(value.empty() || value.size() > 4)
    {
        return false;
    }
    if(!std::all_of(value.begin(), value.end(), IsHexDigit))
    {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 16);
    if(end == value.c_str() || *end != '\0' || parsed > 0xFFFFUL)
    {
        return false;
    }
    output = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseHex16After(const std::string& value, const std::string& marker, std::uint16_t& output)
{
    const std::size_t marker_pos = value.find(marker);
    if(marker_pos == std::string::npos)
    {
        return false;
    }

    const std::size_t start = marker_pos + marker.size();
    std::size_t end = start;
    while(end < value.size() && IsHexDigit(value[end]) && end - start < 4)
    {
        end++;
    }
    return ParseHex16(value.substr(start, end - start), output);
}

bool ParseVidPid(const std::string& hardware_id, std::uint16_t& vid, std::uint16_t& pid)
{
    const std::string normalized = ToUpperAscii(hardware_id);
    const std::size_t colon = normalized.find(':');
    if(colon != std::string::npos &&
       ParseHex16(normalized.substr(0, colon), vid) &&
       ParseHex16(normalized.substr(colon + 1), pid))
    {
        return true;
    }

    if(ParseHex16After(normalized, "VID_", vid) && ParseHex16After(normalized, "PID_", pid))
    {
        return true;
    }

    return ParseHex16After(normalized, "USB:V", vid) && ParseHex16After(normalized, "P", pid);
}

itas109::Parity ToCSerialParity(SerialParity parity)
{
    switch(parity)
    {
        case SerialParity::Odd:
            return itas109::ParityOdd;
        case SerialParity::Even:
            return itas109::ParityEven;
        case SerialParity::None:
        default:
            return itas109::ParityNone;
    }
}

itas109::DataBits ToCSerialDataBits(int data_bits)
{
    switch(std::clamp(data_bits, 5, 8))
    {
        case 5:
            return itas109::DataBits5;
        case 6:
            return itas109::DataBits6;
        case 7:
            return itas109::DataBits7;
        case 8:
        default:
            return itas109::DataBits8;
    }
}

itas109::StopBits ToCSerialStopBits(SerialStopBits stop_bits)
{
    return stop_bits == SerialStopBits::Two ? itas109::StopTwo : itas109::StopOne;
}

SerialInfo FromCSerialInfo(const itas109::SerialPortInfo& info)
{
    SerialInfo result;
    result.port_name = StripWindowsDevicePrefix(SafeString(info.portName));
    result.system_location = result.port_name;
    result.description = SafeString(info.description);

    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    if(ParseVidPid(SafeString(info.hardwareId), vid, pid))
    {
        result.vid = vid;
        result.pid = pid;
        result.has_vid = true;
        result.has_pid = true;
    }
    return result;
}
}

struct SerialConnection::Impl
{
    itas109::CSerialPort port;
    SerialInfo info;
    int baud_rate = 0;
};

SerialConnection::SerialConnection()
    : impl_(std::make_unique<Impl>())
{
}

SerialConnection::~SerialConnection()
{
    Close();
}

bool SerialConnection::Open(const SerialInfo& info, const SerialOptions& options)
{
    Close();

    const std::string port_name = PortNameForOpen(info, options);
    if(port_name.empty())
    {
        return false;
    }

    impl_->info = info;
    impl_->baud_rate = std::max(1, options.baud_rate);
    impl_->port.init(
        port_name.c_str(),
        impl_->baud_rate,
        ToCSerialParity(options.parity),
        ToCSerialDataBits(options.data_bits),
        ToCSerialStopBits(options.stop_bits),
        itas109::FlowNone);
    return impl_->port.open();
}

void SerialConnection::Close()
{
    if(impl_ != nullptr && impl_->port.isOpen())
    {
        impl_->port.close();
    }
}

bool SerialConnection::IsOpen() const
{
    return impl_ != nullptr && impl_->port.isOpen();
}

int SerialConnection::Write(const std::vector<std::uint8_t>& bytes)
{
    if(!IsOpen() || bytes.empty())
    {
        return 0;
    }

    const int written = impl_->port.writeData(bytes.data(), static_cast<int>(bytes.size()));
    if(written <= 0)
    {
        Close();
        return 0;
    }
    return written;
}

std::vector<std::uint8_t> SerialConnection::Read(int max_bytes, int timeout_ms)
{
    (void)timeout_ms;
    if(!IsOpen() || max_bytes == 0)
    {
        return {};
    }

    const int length = std::max(1, max_bytes);
    std::vector<char> buffer(static_cast<std::size_t>(length), 0);
    const int read = impl_->port.readData(buffer.data(), length);
    if(read < 0)
    {
        Close();
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(read));
    for(int idx = 0; idx < read; idx++)
    {
        bytes.push_back(static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(idx)]));
    }
    return bytes;
}

const SerialInfo& SerialConnection::Info() const
{
    return impl_->info;
}

int SerialConnection::BaudRate() const
{
    return IsOpen() ? impl_->baud_rate : 0;
}

std::vector<SerialInfo> SerialBackend::Enumerate() const
{
    std::vector<SerialInfo> ports;
    const std::vector<itas109::SerialPortInfo> available = itas109::CSerialPortInfo::availablePortInfos();
    ports.reserve(available.size());
    for(const itas109::SerialPortInfo& info : available)
    {
        SerialInfo serial = FromCSerialInfo(info);
        if(!serial.port_name.empty())
        {
            ports.push_back(std::move(serial));
        }
    }
    return ports;
}

std::optional<SerialInfo> SerialBackend::FindPort(const std::string& port_name) const
{
    for(const SerialInfo& info : Enumerate())
    {
        const std::string normalized = StripWindowsDevicePrefix(port_name);
        if(info.port_name == normalized || info.system_location == normalized)
        {
            return info;
        }
    }
    return std::nullopt;
}

std::unique_ptr<SerialConnection> SerialBackend::Open(const SerialInfo& info, const SerialOptions& options) const
{
    auto connection = std::make_unique<SerialConnection>();
    if(!connection->Open(info, options))
    {
        return nullptr;
    }
    return connection;
}
}
