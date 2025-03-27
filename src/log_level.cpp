#include <map>

#include <mega/log_level.h>
#include <mega/utils.h>

namespace mega
{

LogLevel toLogLevel(const std::string& level)
{
    static const std::map<std::string, LogLevel> levels = {
#define DEFINE_LOG_LEVEL_ENTRY(name) \
    {Utils::toUpperUtf8(#name), log ## name},
        DEFINE_LOG_LEVELS(DEFINE_LOG_LEVEL_ENTRY)
#undef DEFINE_LOG_LEVEL_ENTRY
    }; // levels

    auto i = levels.find(level);

    if (i != levels.end())
        return i->second;

    // Assume some sane default.
    return logInfo;
}

const char* toString(LogLevel level)
{
    static const std::map<LogLevel, std::string> strings = {
#define DEFINE_LOG_LEVEL_ENTRY(name) \
    {log ## name, Utils::toUpperUtf8(#name)},
        DEFINE_LOG_LEVELS(DEFINE_LOG_LEVEL_ENTRY)
#undef DEFINE_LOG_LEVEL_ENTRY
    }; // strings

    if (auto i = strings.find(level); i != strings.end())
        return i->second.c_str();

    return "N/A";
}

} // mega

