#include "megacmdlogger.h"

#include <map>

#include <sys/types.h>
using namespace std;

// different outstreams for every thread. to gather all the output data
map<int, ostream *> outstreams; //TODO: put this somewhere inside MegaCmdOutput class
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
    if (strstr(source, "megacmd") != NULL) //TODO: warning: what if new files are added
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
        if (( loglevel <= currentThreadLogLevel ) && ( &OUTSTREAM != output )) //TODO: ERRSTREAM? (2 sockets?)
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

