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

    void log(const char *time, int loglevel, const char *source, const char *message);

    void setApiLoggerLevel(int apiLoggerLevel){
        this->apiLoggerLevel=apiLoggerLevel;
    }

    void setCmdLoggerLevel(int cmdLoggerLevel){
        this->cmdLoggerLevel=cmdLoggerLevel;
    }

    int getMaxLogLevel();
};

#endif // MEGACMDLOGGER_H
