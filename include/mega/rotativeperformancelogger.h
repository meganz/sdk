/**
 * @file rotativeperformancelogger.h
 * @brief Logger class with log rotation and background write to file
 *
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 *
 * This file is also distributed under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
 */

#ifndef ROTATIVEPERFORMANCELOGGER_H
#define ROTATIVEPERFORMANCELOGGER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "megaapi.h"
#include "mega/filesystem.h"

namespace mega {

class RotativePerformanceLoggerLoggingThread;
class RotativePerformanceLogger : public MegaLogger
{
private:
    RotativePerformanceLogger();

    void stopLogger();

public:
    ~RotativePerformanceLogger();

    static RotativePerformanceLogger& Instance();

    void initialize(const char * logsPath, const char * logFileName, bool logToStdout);

    void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
             , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
             ) override;
    bool mLogToStdout = false;

    void setArchiveNumbered();
    void setArchiveTimestamps(long int maxFileAgeSeconds);

    void flushAndClose();
    bool cleanLogs();

private:
    std::unique_ptr<RotativePerformanceLoggerLoggingThread> mLoggingThread;
};

}

#endif //ROTATIVEPERFORMANCELOGGER_H
