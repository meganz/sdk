#include <mega/common/subsystem_logger.h>
#include <mega/log_level.h>

namespace mega
{
namespace common
{

SubsystemLogger::SubsystemLogger(const char* subsystemName)
  : Logger(subsystemName)
  , mLogLevel(logInfo)
{
}

void SubsystemLogger::logLevel(LogLevel level)
{
    mLogLevel = level;
}

LogLevel SubsystemLogger::logLevel() const
{
    return mLogLevel;
}

bool SubsystemLogger::masked(int severity) const
{
    return mLogLevel < severity;
}

} // common
} // mega

