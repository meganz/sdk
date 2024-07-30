#pragma once

#include <string>

#include <mega/fuse/common/log_level_forward.h>

namespace mega
{
namespace fuse
{

#define DEFINE_LOG_LEVELS(expander) \
    expander(ERROR) \
    expander(WARNING) \
    expander(INFO) \
    expander(DEBUG) \

enum LogLevel : unsigned int
{
#define DEFINE_LOG_LEVEL_ENUMERANT(name) LOG_LEVEL_ ## name,
    DEFINE_LOG_LEVELS(DEFINE_LOG_LEVEL_ENUMERANT)
#undef DEFINE_LOG_LEVEL_ENUMERANT
}; // LogLevel

constexpr auto LOG_LEVEL_ALL = LOG_LEVEL_DEBUG + 1;

LogLevel toLogLevel(const std::string& level);

const char* toString(LogLevel level);

} // fuse
} // mega

