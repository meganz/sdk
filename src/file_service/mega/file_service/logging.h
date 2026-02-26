#pragma once

#include <mega/common/logging.h>
#include <mega/file_service/logger.h>

// Emit a debug message.
#define FSDebug1(format) LogDebug1(::mega::file_service::logger(), (format))

#define FSDebugF(format, ...) LogDebugF(::mega::file_service::logger(), (format), __VA_ARGS__)

// Emit an info message.
#define FSInfo1(format) LogInfo1(::mega::file_service::logger(), (format))

#define FSInfoF(format, ...) LogInfoF(::mega::file_service::logger(), (format), __VA_ARGS__)

// Emit an error message.
#define FSError1(format) LogError1(::mega::file_service::logger(), (format))

#define FSErrorF(format, ...) LogErrorF(::mega::file_service::logger(), (format), __VA_ARGS__)

// Emit a warning message.
#define FSWarning1(format) LogWarning1(::mega::file_service::logger(), (format))

#define FSWarningF(format, ...) LogWarningF(::mega::file_service::logger(), (format), __VA_ARGS__)
