#include <cassert>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <mega/log_level.h>
#include <mega/common/logger.h>
#include <mega/common/utility.h>

#include <mega/logging.h>

namespace mega
{
namespace common
{

Logger::Logger(const char* subsystemName)
  : mSubsystemName(subsystemName)
{
}

std::runtime_error Logger::error(const char* filename,
                                 const char* format,
                                 unsigned int line,
                                 ...) const
{
    std::va_list arguments;

    // Compute log message.
    va_start(arguments, line);

    auto message = formatv(arguments, format);

    va_end(arguments);

    // Instantiate exception.
    std::runtime_error exception(message);

    // Emit log message.
    log(filename, message, line, logError);

    // Return exception to caller.
    return exception;
}

void Logger::log(const char* filename,
                 const std::string& message,
                 unsigned int line,
                 int severity) const
{
    // Sanity.
    assert(filename);
    assert(severity < logMax);

    // Last minute severity check.
    if (masked(severity))
        return;

    std::ostringstream ostream;

    // Add subsystem name if necessary.
    if (mSubsystemName)
        ostream << mSubsystemName << ": ";

    // Compute log message.
    ostream << std::this_thread::get_id()
            << ": "
            << message;

    // Emit log message.
    SimpleLogger::postLog(static_cast<LogLevel>(severity),
                          ostream.str().c_str(),
                          filename,
                          static_cast<int>(line));
}

void Logger::log(const char* filename,
                 const char* format,
                 unsigned int line,
                 int severity,
                 ...) const
{
    // Sanity.
    assert(filename);
    assert(format);

    std::va_list arguments;

    va_start(arguments, severity);

    // Emit log message.
    logv(arguments, filename, format, line, severity);

    va_end(arguments);
}

void Logger::logv(std::va_list arguments,
                  const char* filename,
                  const char* format,
                  unsigned int line,
                  int severity) const
{
    // Sanity.
    assert(format);

    // Last minute severity check.
    if (masked(severity))
        return;

    // Emit log message.
    log(filename,
        formatv(arguments, format),
        line,
        severity);
}

bool Logger::masked(int severity) const
{
    return SimpleLogger::getLogLevel() < severity;
}

Logger& logger()
{
    static Logger logger;

    return logger;
}

} // common
} // mega

