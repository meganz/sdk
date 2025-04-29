#pragma once

#include <mega/log_level.h>
#include <mega/common/logger.h>
#include <mega/logging.h>

// Keep things DRY.
#define Log1(logger, format, severity) \
{ \
    if (!(logger).masked((severity))) \
        (logger).log(::mega::log_file_leafname(__FILE__), \
                     (format), \
                     __LINE__, \
                     (severity)); \
} \
while (0)

#define LogF(logger, format, severity, ...) do \
{ \
    if (!(logger).masked((severity))) \
        (logger).log(::mega::log_file_leafname(__FILE__), \
                     (format), \
                     __LINE__, \
                     (severity), \
                     __VA_ARGS__); \
} \
while (0)

// Emit a debug message.
#define LogDebug1(logger, format) \
  Log1((logger), (format), ::mega::logDebug)

#define LogDebugF(logger, format, ...) \
  LogF((logger), (format), ::mega::logDebug, __VA_ARGS__)

// Emit an info message.
#define LogInfo1(logger, format) \
  Log1((logger), (format), ::mega::logInfo)

#define LogInfoF(logger, format, ...) \
  LogF((logger), (format), ::mega::logInfo, __VA_ARGS__)

// Emit an error message.
#define LogError1(logger, format) \
  (logger).error(::mega::log_file_leafname(__FILE__), (format), __LINE__)

#define LogErrorF(logger, format, ...) \
  (logger).error(::mega::log_file_leafname(__FILE__), (format), __LINE__, __VA_ARGS__)

// Emit a warning message.
#define LogWarning1(logger, format) \
  Log1((logger), (format), ::mega::logWarning)

#define LogWarningF(logger, format, ...) \
  LogF((logger), (format), ::mega::logWarning, __VA_ARGS__)

