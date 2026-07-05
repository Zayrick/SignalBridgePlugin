#ifndef SIGNALBRIDGE_LOG_BUFFER_H
#define SIGNALBRIDGE_LOG_BUFFER_H

#include <QString>
#include <QStringList>

namespace signalbridge
{
struct LogAppendResult
{
    QStringList appended;
    QString text;
    bool rebuild = false;
    int count = 0;
};

class LogBuffer
{
public:
    void Clear();
    LogAppendResult Append(const QString& line);
    QString Text() const;

private:
    QStringList lines_;
};
}

#endif // SIGNALBRIDGE_LOG_BUFFER_H
