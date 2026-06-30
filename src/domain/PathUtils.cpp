#include "domain/PathUtils.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace signalbridge
{
std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizeLookupPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');

    std::vector<std::string> parts;
    std::size_t start = 0;
    while(start <= path.size())
    {
        const std::size_t slash = path.find('/', start);
        const std::string part = slash == std::string::npos
                                     ? path.substr(start)
                                     : path.substr(start, slash - start);

        if(part.empty() || part == ".")
        {
        }
        else if(part == "..")
        {
            if(!parts.empty())
            {
                parts.pop_back();
            }
        }
        else
        {
            parts.push_back(part);
        }

        if(slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }

    std::string normalized;
    for(std::size_t idx = 0; idx < parts.size(); idx++)
    {
        if(idx > 0)
        {
            normalized += '/';
        }
        normalized += parts[idx];
    }
    return normalized;
}

std::string LookupDir(const std::string& path)
{
    const std::string normalized = NormalizeLookupPath(path);
    const std::size_t slash = normalized.find_last_of('/');
    return slash == std::string::npos ? std::string() : normalized.substr(0, slash);
}

std::string JoinLookupPath(const std::string& lhs, const std::string& rhs)
{
    return lhs.empty() ? rhs : lhs + "/" + rhs;
}
}
