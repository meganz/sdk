#pragma once

#include <string>

#include <mega/log_level_forward.h>

namespace mega
{

#define DEFINE_LOG_LEVELS(expander) \
    expander(Fatal) \
    expander(Error) \
    expander(Warning) \
    expander(Info) \
    expander(Debug) \
    expander(Verbose)

enum LogLevel : int
{
#define DEFINE_LOG_LEVEL_ENUMERANT(name) log ## name,
    DEFINE_LOG_LEVELS(DEFINE_LOG_LEVEL_ENUMERANT)
#undef DEFINE_LOG_LEVEL_ENUMERANT
}; // LogLevel

#define PLUS1(name) + 1

constexpr auto logMax = DEFINE_LOG_LEVELS(PLUS1);

#undef PLUS1

LogLevel toLogLevel(const std::string& level);

const char* toString(LogLevel level);

} // mega

