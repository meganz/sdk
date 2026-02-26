#pragma once

#include <atomic>
#include <cstdarg>
#include <stdexcept>
#include <string>

#include <mega/log_level_forward.h>
#include <mega/common/logger_forward.h>

namespace mega
{
namespace common
{

class Logger
{
    // The name of the subsystem we're logging for.
    const char *mSubsystemName;

public:
    explicit Logger(const char* subsystemName = nullptr);
    
    Logger(const Logger& other) = delete;

    virtual ~Logger() = default;

    Logger& operator=(const Logger& rhs) = delete;

    // Emit an error message.
    std::runtime_error error(const char* filename,
                             const char* format,
                             unsigned int line,
                             ...) const;

    // Emit a log message.
    void log(const char* filename,
             const std::string& message,
             unsigned int line,
             int severity) const;

    void log(const char* filename,
             const char* format,
             unsigned int line,
             int severity,
             ...) const;

    void logv(std::va_list arguments,
              const char* filename,
              const char* format,
              unsigned int line,
              int severity) const;

    // Check whether messages at this severity have been masked.
    virtual bool masked(int severity) const;
}; // Logger

Logger& logger();

} // common
} // mega

