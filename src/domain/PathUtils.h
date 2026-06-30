#ifndef SIGNALBRIDGE_PATH_UTILS_H
#define SIGNALBRIDGE_PATH_UTILS_H

#include <string>

namespace signalbridge
{
std::string LowerAscii(std::string value);
std::string NormalizeLookupPath(std::string path);
std::string LookupDir(const std::string& path);
std::string JoinLookupPath(const std::string& lhs, const std::string& rhs);
}

#endif // SIGNALBRIDGE_PATH_UTILS_H
