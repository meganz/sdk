/**
 * @file examples/megacmd/megacmd.cpp
 * @brief MegaCMD: Controls message logging
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

// different outstreams for every thread. to gather all the output data
map<int, ostream *> outstreams;
map<int, int> threadLogLevel;
map<int, int> threadoutCode;

int getCurrentThread(){
    //return std::this_thread::get_id();
    //return std::thread::get_id();
#ifdef USE_QT
    return MegaThread::currentThreadId(); //TODO: create this in thread class

#elif USE_PTHREAD
    return pthread_self();

#endif
}

ostream &getCurrentOut(){
    int currentThread = getCurrentThread();
    if (outstreams.find(currentThread) == outstreams.end())
    {
        return cout;
    }
    else
    {
        return *outstreams[currentThread];
    }
}

int getCurrentOutCode(){
    int currentThread = getCurrentThread();
    if (threadoutCode.find(currentThread) == threadoutCode.end())
    {
        return 0; //default OK
    }
    else
    {
        return threadoutCode[currentThread];
    }
}


int getCurrentThreadLogLevel(){
    int currentThread = getCurrentThread();
    if (threadLogLevel.find(currentThread) == threadLogLevel.end())
    {
        return -1;
    }
    else
    {
        return threadLogLevel[currentThread];
    }
}

void setCurrentThreadLogLevel(int level){
    threadLogLevel[getCurrentThread()] = level;
}

void setCurrentThreadOutStream(ostream *s){
    outstreams[getCurrentThread()] = s;
}

void setCurrentOutCode(int outCode){
    threadoutCode[getCurrentThread()] = outCode;
}

void MegaCMDLogger::log(const char *time, int loglevel, const char *source, const char *message)
{
    if (strstr(source, "megacmd") != NULL) // all sources within the megacmd folder
    {
        if (loglevel <= cmdLoggerLevel)
        {
            *output << "[" << SimpleLogger::toStr(mega::LogLevel(loglevel)) << "] " << message << endl;
        }

        int currentThreadLogLevel = getCurrentThreadLogLevel();
        if (currentThreadLogLevel < 0)
        {
            currentThreadLogLevel = cmdLoggerLevel;
        }
        if (( loglevel <= currentThreadLogLevel ) && ( &OUTSTREAM != output ))
        {
            OUTSTREAM << "[" << SimpleLogger::toStr(mega::LogLevel(loglevel)) << "] " << message << endl;
        }
    }
    else
    {
        if (loglevel <= apiLoggerLevel)
        {
            *output << "[API:" << SimpleLogger::toStr(mega::LogLevel(loglevel)) << "] " << message << endl;
        }

        int currentThreadLogLevel = getCurrentThreadLogLevel();
        if (currentThreadLogLevel < 0)
        {
            currentThreadLogLevel = apiLoggerLevel;
        }
        if (( loglevel <= currentThreadLogLevel ) && ( &OUTSTREAM != output )) //since it happens in the sdk thread, this shall be false
        {
            OUTSTREAM << "[API:" << SimpleLogger::toStr(mega::LogLevel(loglevel)) << "] " << message << endl;
        }
    }
}

int MegaCMDLogger::getMaxLogLevel()
{
    return max(max(getCurrentThreadLogLevel(), cmdLoggerLevel), apiLoggerLevel);
}

