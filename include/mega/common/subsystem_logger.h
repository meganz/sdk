#pragma once

#include <mega/common/logger.h>
#include <mega/log_level_forward.h>

namespace mega
{
namespace common
{

class SubsystemLogger
  : public Logger
{
    // The logger's current log level.
    //
    // The logger will emit messages if and only if the message's log level
    // is less than or equal to the logger's current log level.
    std::atomic<LogLevel> mLogLevel;

public:
    explicit SubsystemLogger(const char* name);

    // Set the logger's log level.
    void logLevel(LogLevel level);

    // Query the logger's log level.
    LogLevel logLevel() const;

    // Check whether messages at this severity have been masked.
    bool masked(int severity) const override;
}; // SubsystemLogger

} // common
} // mega

