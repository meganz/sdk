#pragma once

#include <mega/fuse/common/log_level.h>
#include <mega/fuse/common/logger.h>

#include <mega/logging.h>

// Keep things DRY.
#define FUSELog1(format, line, severity) do \
{ \
    if (!Logger::masked((severity))) \
        Logger::log(::mega::log_file_leafname(__FILE__), \
                    (format), \
                    __LINE__, \
                    (severity)); \
} \
while (0)

#define FUSELogF(format, line, severity, ...) do \
{ \
    if (!Logger::masked((severity))) \
        Logger::log(::mega::log_file_leafname(__FILE__), \
                    (format), \
                    __LINE__, \
                    (severity), \
                    __VA_ARGS__); \
} \
while (0)

// Emit a debug message.
#define FUSEDebug1(format) \
  FUSELog1((format), __LINE__, LOG_LEVEL_DEBUG)

#define FUSEDebugF(format, ...) \
  FUSELogF((format), __LINE__, LOG_LEVEL_DEBUG, __VA_ARGS__)

// Emit an info message.
#define FUSEInfo1(format) \
  FUSELog1((format), __LINE__, LOG_LEVEL_INFO)

#define FUSEInfoF(format, ...) \
  FUSELogF((format), __LINE__, LOG_LEVEL_INFO, __VA_ARGS__)

// Emit an error message.
#define FUSEError1(format) \
  Logger::error(::mega::log_file_leafname(__FILE__), \
                (format), \
                __LINE__)

#define FUSEErrorF(format, ...) \
  Logger::error(::mega::log_file_leafname(__FILE__), \
                (format), \
                __LINE__, \
                __VA_ARGS__)

// Emit a warning message.
#define FUSEWarning1(format) \
  FUSELog1((format), __LINE__, LOG_LEVEL_WARNING)

#define FUSEWarningF(format, ...) \
  FUSELogF((format), __LINE__, LOG_LEVEL_WARNING, __VA_ARGS__)

