#include "ui/LogBuffer.h"

#include <QDateTime>

namespace signalbridge
{
namespace
{
constexpr int MaxFrontendLogLines = 2000;
}

void LogBuffer::Clear()
{
    lines_.clear();
}

LogAppendResult LogBuffer::Append(const QString& line)
{
    LogAppendResult result;
    if(line.isNull())
    {
        result.text = Text();
        result.count = lines_.size();
        return result;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    for(const QString& raw_line : line.split('\n'))
    {
        const QString entry = QString("[%1] %2").arg(timestamp, raw_line);
        lines_.append(entry);
        result.appended.append(entry);
    }

    while(lines_.size() > MaxFrontendLogLines)
    {
        lines_.removeFirst();
        result.rebuild = true;
    }

    result.text = Text();
    result.count = lines_.size();
    return result;
}

QString LogBuffer::Text() const
{
    return lines_.join('\n');
}
}
