#ifndef MEGACMDLOGGER_H
#define MEGACMDLOGGER_H

#include "megaapi_impl.h"
#include "megacmd.h"
#include <fstream>

#define OUTSTREAM getCurrentOut()

int getCurrentThread();
ostream &getCurrentOut();
void setCurrentThreadOutStream(ostream *);
int getCurrentThreadLogLevel();
void setCurrentThreadLogLevel(int);

class MegaCMDLogger: public MegaLogger{
private:
    int apiLoggerLevel;
    int cmdLoggerLevel;
    ostream * output;
public:
    MegaCMDLogger(ostream * outstr)
    {
        this->output = outstr;
        this->apiLoggerLevel=MegaApi::LOG_LEVEL_ERROR;
    }

    void log(const char *time, int loglevel, const char *source, const char *message)
    {
        if (strstr(source, "megacmd") != NULL) //TODO: warning: what if new files are added
        {
            if (loglevel<=cmdLoggerLevel)
            {
                *output << "[" << SimpleLogger::toStr(mega::LogLevel(loglevel))<< "] " << message << endl;
            }

            int currentThreadLogLevel = getCurrentThreadLogLevel();
            if (currentThreadLogLevel < 0) currentThreadLogLevel=cmdLoggerLevel;
            if (loglevel<=currentThreadLogLevel && &OUTSTREAM != output) //TODO: ERRSTREAM? (2 sockets?)
                OUTSTREAM << "[" << SimpleLogger::toStr(mega::LogLevel(loglevel))<< "] " << message << endl;
        }
        else{
            if (loglevel<=apiLoggerLevel)
            {
                *output << "[API:" << SimpleLogger::toStr(mega::LogLevel(loglevel))<< "] " << message << endl;
            }

            int currentThreadLogLevel = getCurrentThreadLogLevel();
            if (currentThreadLogLevel < 0) currentThreadLogLevel=apiLoggerLevel;
            if (loglevel<=currentThreadLogLevel && &OUTSTREAM != output) //since it happens in the sdk thread, this shall be false
                OUTSTREAM << "[API:" << SimpleLogger::toStr(mega::LogLevel(loglevel))<< "] " << message << endl;
        }
    }

    void setApiLoggerLevel(int apiLoggerLevel){
        this->apiLoggerLevel=apiLoggerLevel;
    }

    void setCmdLoggerLevel(int cmdLoggerLevel){
        this->cmdLoggerLevel=cmdLoggerLevel;
    }
};

#endif // MEGACMDLOGGER_H
