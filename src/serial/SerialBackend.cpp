#include "serial/SerialBackend.h"

#include <algorithm>
#include <cstring>

#include <QSerialPortInfo>
#include <QString>

#include "serial_port/serial_port.h"

namespace signalbridge
{
namespace
{
std::string ToStdString(const QString& value)
{
    return value.toStdString();
}

SerialInfo FromQtInfo(const QSerialPortInfo& info)
{
    SerialInfo result;
    result.port_name = ToStdString(info.portName());
    result.system_location = ToStdString(info.systemLocation());
    result.description = ToStdString(info.description());
    result.manufacturer = ToStdString(info.manufacturer());
    result.serial_number = ToStdString(info.serialNumber());
    result.has_vid = info.hasVendorIdentifier();
    result.has_pid = info.hasProductIdentifier();
    result.vid = result.has_vid ? info.vendorIdentifier() : 0;
    result.pid = result.has_pid ? info.productIdentifier() : 0;
    return result;
}

std::string PortNameForOpen(const SerialInfo& info, const SerialOptions& options)
{
    if(!options.port_name.empty())
    {
        return options.port_name;
    }
    if(!info.port_name.empty())
    {
        return info.port_name;
    }
    return info.system_location;
}
}

struct SerialConnection::Impl
{
    ::serial_port port;
    SerialInfo info;
    int baud_rate = 0;
    bool open = false;
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
    impl_->open = impl_->port.serial_open(port_name.c_str(), static_cast<unsigned int>(impl_->baud_rate));
    return impl_->open;
}

void SerialConnection::Close()
{
    if(impl_ != nullptr && impl_->open)
    {
        impl_->port.serial_close();
        impl_->open = false;
    }
}

bool SerialConnection::IsOpen() const
{
    return impl_ != nullptr && impl_->open;
}

int SerialConnection::Write(const std::vector<std::uint8_t>& bytes)
{
    if(!IsOpen() || bytes.empty())
    {
        return 0;
    }

    std::vector<char> payload(bytes.size());
    std::memcpy(payload.data(), bytes.data(), bytes.size());
    const int written = impl_->port.serial_write(payload.data(), static_cast<int>(payload.size()));
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
    const int read = impl_->port.serial_read(buffer.data(), length);
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
    const QList<QSerialPortInfo> available = QSerialPortInfo::availablePorts();
    ports.reserve(static_cast<std::size_t>(available.size()));
    for(const QSerialPortInfo& info : available)
    {
        ports.push_back(FromQtInfo(info));
    }
    return ports;
}

std::optional<SerialInfo> SerialBackend::FindPort(const std::string& port_name) const
{
    for(const SerialInfo& info : Enumerate())
    {
        if(info.port_name == port_name)
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
