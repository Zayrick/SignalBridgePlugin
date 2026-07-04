#ifndef SIGNALBRIDGE_SERIAL_BACKEND_H
#define SIGNALBRIDGE_SERIAL_BACKEND_H

#include <memory>
#include <optional>
#include <vector>

#include "serial/SerialTypes.h"

namespace signalbridge
{
class SerialConnection
{
public:
    SerialConnection();
    ~SerialConnection();

    SerialConnection(const SerialConnection&) = delete;
    SerialConnection& operator=(const SerialConnection&) = delete;

    bool Open(const SerialInfo& info, const SerialOptions& options);
    void Close();
    bool IsOpen() const;
    int Write(const std::vector<std::uint8_t>& bytes);
    std::vector<std::uint8_t> Read(int max_bytes, int timeout_ms);
    const SerialInfo& Info() const;
    int BaudRate() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class SerialBackend
{
public:
    std::vector<SerialInfo> Enumerate() const;
    std::optional<SerialInfo> FindPort(const std::string& port_name) const;
    std::unique_ptr<SerialConnection> Open(const SerialInfo& info, const SerialOptions& options) const;
};
}

#endif // SIGNALBRIDGE_SERIAL_BACKEND_H
