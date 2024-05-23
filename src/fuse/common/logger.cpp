#include <cassert>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <mega/fuse/common/log_level.h>
#include <mega/fuse/common/logger.h>
#include <mega/fuse/common/utility.h>

#include <mega/logging.h>

namespace mega
{
namespace fuse
{

std::atomic<LogLevel> Logger::mLogLevel{LOG_LEVEL_INFO};

std::runtime_error Logger::error(const char* filename,
                                 const char* format,
                                 unsigned int line,
                                 ...)
{
    std::va_list arguments;

    // Compute log message.
    va_start(arguments, line);

    auto message = fuse::format(arguments, format);

    va_end(arguments);

    // Instantiate exception.
    std::runtime_error exception(message);

    // Emit log message.
    log(filename, message, line, LOG_LEVEL_ERROR);

    // Return exception to caller.
    return exception;
}

void Logger::log(std::va_list arguments,
                 const char* filename,
                 const char* format,
                 unsigned int line,
                 unsigned int severity)
{
    // Sanity.
    assert(format);

    // Last minute severity check.
    if (masked(static_cast<LogLevel>(severity)))
        return;

    // Emit log message.
    log(filename,
        fuse::format(arguments, format),
        line,
        severity);
}

void Logger::log(const char* filename,
                 const std::string& message,
                 unsigned int line,
                 unsigned int severity)
{
    // Translates FUSE log levels to SDK log levels.
    static const mega::LogLevel lut[LOG_LEVEL_ALL] = {
        logError,
        logWarning,
        logInfo,
        logDebug
    }; // lut

    // Sanity.
    assert(filename);
    assert(severity < LOG_LEVEL_ALL);

    // Last minute severity check.
    if (masked(static_cast<LogLevel>(severity)))
        return;

    std::ostringstream ostream;

    // Compute log message.
    ostream << "FUSE: "
            << std::this_thread::get_id()
            << ": "
            << message;

    // Emit log message.
    SimpleLogger::postLog(lut[severity],
                          ostream.str().c_str(),
                          filename,
                          static_cast<int>(line));
}

void Logger::log(const char* filename,
                 const char* format,
                 unsigned int line,
                 unsigned int severity,
                 ...)
{
    // Sanity.
    assert(filename);
    assert(format);

    std::va_list arguments;

    va_start(arguments, severity);

    // Emit log message.
    log(arguments, filename, format, line, severity);

    va_end(arguments);
}

void Logger::logLevel(LogLevel level)
{
    mLogLevel = level;
}

LogLevel Logger::logLevel()
{
    return mLogLevel;
}

bool Logger::masked(LogLevel severity)
{
    return mLogLevel < severity;
}

} // fuse
} // mega

