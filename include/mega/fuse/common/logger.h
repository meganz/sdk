#pragma once

#include <atomic>
#include <cstdarg>
#include <stdexcept>
#include <string>

#include <mega/fuse/common/log_level_forward.h>
#include <mega/fuse/common/logger_forward.h>

namespace mega
{
namespace fuse
{

class Logger
{
    // The logger's current log level.
    //
    // The logger will emit messages if and only if the message's log level
    // is less than or equal to the logger's current log level.
    static std::atomic<LogLevel> mLogLevel;

public:
    // Emit an error message.
    static std::runtime_error error(const char* filename,
                                    const char* format,
                                    unsigned int line,
                                    ...);

    // Emit a log message.
    static void log(std::va_list arguments,
                    const char* filename,
                    const char* format,
                    unsigned int line,
                    unsigned int severity);

    static void log(const char* filename,
                    const std::string& message,
                    unsigned int line,
                    unsigned int severity);

    static void log(const char* filename,
                    const char* format,
                    unsigned int line,
                    unsigned int severity,
                    ...);

    // Set the logger's log level.
    static void logLevel(LogLevel level);

    // Query the logger's log level.
    static LogLevel logLevel();

    // Check whether messages at this severity have been masked.
    static bool masked(LogLevel severity);
}; // Logger

} // fuse
} // mega

