#pragma once

#include <mega/log_level.h>
#include <mega/common/logging.h>
#include <mega/fuse/common/logger.h>

#include <mega/logging.h>

// Emit a debug message.
#define FUSEDebug1(format) \
  LogDebug1(::mega::fuse::logger(), (format))

#define FUSEDebugF(format, ...) \
  LogDebugF(::mega::fuse::logger(), (format), __VA_ARGS__)

// Emit an info message.
#define FUSEInfo1(format) \
  LogInfo1(::mega::fuse::logger(), (format))

#define FUSEInfoF(format, ...) \
  LogInfoF(::mega::fuse::logger(), (format), __VA_ARGS__)

// Emit an error message.
#define FUSEError1(format) \
  LogError1(::mega::fuse::logger(), (format))

#define FUSEErrorF(format, ...) \
  LogErrorF(::mega::fuse::logger(), (format), __VA_ARGS__)

// Emit a warning message.
#define FUSEWarning1(format) \
  LogWarning1(::mega::fuse::logger(), (format))

#define FUSEWarningF(format, ...) \
  LogWarningF(::mega::fuse::logger(), (format), __VA_ARGS__)

