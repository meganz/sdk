/**
 * @file examples/megacmd/megacmdlogger.cpp
 * @brief MEGAcmd: Controls message logging
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
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
 */

#include "megacmdlogger.h"

#include <map>

#include <sys/types.h>
using namespace std;
using namespace mega;

// different outstreams for every thread. to gather all the output data
map<uint64_t, ostream *> outstreams;
map<uint64_t, int> threadLogLevel;
map<uint64_t, int> threadoutCode;

ostream &getCurrentOut()
{
    uint64_t currentThread = MegaThread::currentThreadId();
    if (outstreams.find(currentThread) == outstreams.end())
    {
        return cout;
    }
    else
    {
        return *outstreams[currentThread];
    }
}

bool interactiveThread()
{
    uint64_t currentThread = MegaThread::currentThreadId();
    if (outstreams.find(currentThread) == outstreams.end())
    {
        return true;
    }
    else
    {
        return false;
    }
}

int getCurrentOutCode()
{
    uint64_t currentThread = MegaThread::currentThreadId();
    if (threadoutCode.find(currentThread) == threadoutCode.end())
    {
        return 0; //default OK
    }
    else
    {
        return threadoutCode[currentThread];
    }
}


int getCurrentThreadLogLevel()
{
    uint64_t currentThread = MegaThread::currentThreadId();
    if (threadLogLevel.find(currentThread) == threadLogLevel.end())
    {
        return -1;
    }
    else
    {
        return threadLogLevel[currentThread];
    }
}

void setCurrentThreadLogLevel(int level)
{
    threadLogLevel[MegaThread::currentThreadId()] = level;
}

void setCurrentThreadOutStream(ostream *s)
{
    outstreams[MegaThread::currentThreadId()] = s;
}

void setCurrentOutCode(int outCode)
{
    threadoutCode[MegaThread::currentThreadId()] = outCode;
}

void MegaCMDLogger::log(const char *time, int loglevel, const char *source, const char *message)
{
    if (strstr(source, "megacmd") != NULL) // all sources within the megacmd folder
    {
        if (loglevel <= cmdLoggerLevel)
        {
            *output << "[" << SimpleLogger::toStr(LogLevel(loglevel)) << ": " << time << "] " << message << endl;
        }

        int currentThreadLogLevel = getCurrentThreadLogLevel();
        if (currentThreadLogLevel < 0)
        {
            currentThreadLogLevel = cmdLoggerLevel;
        }
        if (( loglevel <= currentThreadLogLevel ) && ( &OUTSTREAM != output ))
        {
            OUTSTREAM << "[" << SimpleLogger::toStr(LogLevel(loglevel)) << ": " << time << "] " << message << endl;
        }
    }
    else
    {
        if (loglevel <= apiLoggerLevel)
        {
            if (( apiLoggerLevel <= MegaApi::LOG_LEVEL_DEBUG ) && !strcmp(message, "Request (RETRY_PENDING_CONNECTIONS) starting"))
            {
                return;
            }
            if (( apiLoggerLevel <= MegaApi::LOG_LEVEL_DEBUG ) && !strcmp(message, "Request (RETRY_PENDING_CONNECTIONS) finished"))
            {
                return;
            }

            *output << "[API:" << SimpleLogger::toStr(LogLevel(loglevel)) << ": " << time << "] " << message << endl;
        }

        int currentThreadLogLevel = getCurrentThreadLogLevel();

        if (currentThreadLogLevel < 0)
        {
            currentThreadLogLevel = apiLoggerLevel;
        }
        if (( loglevel <= currentThreadLogLevel ) && ( &OUTSTREAM != output )) //since it happens in the sdk thread, this shall be false
        {
            OUTSTREAM << "[API:" << SimpleLogger::toStr(LogLevel(loglevel)) << ": " << time << "] " << message << endl;
        }
    }
}

int MegaCMDLogger::getMaxLogLevel()
{
    return max(max(getCurrentThreadLogLevel(), cmdLoggerLevel), apiLoggerLevel);
}

