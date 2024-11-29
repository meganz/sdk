#include <cassert>
#include <map>

#include <mega/fuse/common/log_level.h>

namespace mega
{
namespace fuse
{

LogLevel toLogLevel(const std::string& level)
{
    static std::map<std::string, LogLevel> levels = {
#define DEFINE_LOG_LEVEL_ENTRY(name) {#name, LOG_LEVEL_ ## name},
        DEFINE_LOG_LEVELS(DEFINE_LOG_LEVEL_ENTRY)
#undef DEFINE_LOG_LEVEL_ENTRY
    }; // levels

    auto i = levels.find(level);

    if (i != levels.end())
        return i->second;

    // Assume some sane default.
    return LOG_LEVEL_INFO;
}

const char* toString(LogLevel level)
{
    assert(level < LOG_LEVEL_ALL);

    switch (level)
    {
#define DEFINE_LOG_LEVEL_CLAUSE(name) case LOG_LEVEL_ ## name: return #name;
        DEFINE_LOG_LEVELS(DEFINE_LOG_LEVEL_CLAUSE);
#undef DEFINE_LOG_LEVEL_CLAUSE
    }

    // Silence the compiler.
    return "N/A";
}

} // fuse
} // mega

