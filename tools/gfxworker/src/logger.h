/**
* @file Logger.h
* @brief Logger class with log rotation and background write to file
*
* (c) 2013 by Mega Limited, Auckland, New Zealand
*
* This file is part of the MEGAproxy.
*
* MEGAproxy is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* @copyright Simplified (2-clause) BSD License.
*
* You should have received a copy of the license along with this
* program.
*/

#pragma once

#include <mega/types.h>
#include "megaapi.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mega {
namespace gfx {

#define GENERATE_LOG_LEVELS_FROM_CFG_STRING \
    GENERATOR_MACRO(mega::MegaApi::LOG_LEVEL_FATAL   , "fatal") \
    GENERATOR_MACRO(mega::MegaApi::LOG_LEVEL_ERROR   , "error") \
    GENERATOR_MACRO(mega::MegaApi::LOG_LEVEL_WARNING , "warn") \
    GENERATOR_MACRO(mega::MegaApi::LOG_LEVEL_INFO    , "info") \
    GENERATOR_MACRO(mega::MegaApi::LOG_LEVEL_DEBUG   , "debug") \
    GENERATOR_MACRO(mega::MegaApi::LOG_LEVEL_MAX     , "max")

class MegaFileLoggerLoggingThread;

class MegaFileLogger : public ::mega::MegaLogger
{
    friend class MegaFileLoggerLoggingThread;

    static thread_local std::string sThreadName;

    void stopLogger();

public:
/* sDefaultLogLevelStr is used when:
   1. MegaFileLogger is initiated, before any call to MegaFileLogger::setLogLevel()
   2. The default configuration value for megaproxy_log_level
   3. The log level string passed to MegaFileLogger::setLogLevel()
      is not one of the possible values of {fatal, error, warning, info, debug, max} */
    static const std::string sDefaultLogLevelStr;

    MegaFileLogger();
    ~MegaFileLogger();

    void initialize(const char * logsPath, const char * logFileName, bool logToStdout);

    static void setThreadName(const std::string &threadName)
    {
        sThreadName = threadName;
    }

    void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
             , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
             ) override;
    bool mLogToStdout = false;

    void setArchiveNumbered();
    void setMaxArchiveAge(std::chrono::seconds maxAgeSeconds);
    void setMaxArchivesToKeep(int maxArchives);
    void setLogFileSize(size_t size);

    void flushAndClose();
    void flush();
    bool cleanLogs();
    int logLevelFromString(const std::string&);
    void setLogLevel(int);
    void setLogLevel(const std::string&);

    // one global instance
    static MegaFileLogger& get();
private:
    std::unique_ptr<MegaFileLoggerLoggingThread> mLoggingThread;
    std::unordered_map<std::string, int> mLogLevelStringToEnumMap;
    std::atomic_bool mInited;
};

}
}