/**
 * @file mega/logging.h
 * @brief Logging class
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

/* Usage example:

   1)
    #include <fstream>  // for std::ofstream

    // output debug messages to file
    std::ofstream debugfile;
    debugfile.open("debug.log");

    // use logDebug level
    SimpleLogger::setLogLevel(logDebug);
    // set output to stdout for all messages
    SimpleLogger::setAllOutputs(&std::COUT);

    // add file for debug output only
    SimpleLogger::addOutput(logDebug, &debugfile);
    // print additional components for debug output
    SimpleLogger::setOutputSettings(logDebug, true, true, true);

    ...
    LOG_debug << "test"; // will print message on screen and append line to debugfile
    LOG_info << "informing"; // will only print message on screen


    2)
    // set output class for all types of logs (for example - send logs to remote Log Server)
    class MyOutput: public Logger {
    public:
        void log(const char *time, int loglevel, const char *source, const char *message) {
            std::COUT << "{" << time << "}" << " [" << source << "] " << message << std::endl;
        }
    };

    ...
    MyOutput output;

    // let's output both Debug (verbose) and Info (just messages) logs
    // and send logs to remote Log Server
    SimpleLogger::setOutputSettings(logDebug, true, true, true);
    SimpleLogger::setOutputSettings(logInfo, false, false, false);
    SimpleLogger::setLogLevel(logDebug);
    SimpleLogger::setAllOutputs(&std::COUT);
    SimpleLogger::setOutputClass(output);
    SimpleLogger::setOutputClass(&myOutput);

    LOG_debug << "test";
    LOG_info << "informing";


    3)
    if MEGA_QT_LOGGING defined:

    QString a = QString::fromAscii("test1");
    LOG_info << a;
    LOG_info << QString::fromAscii("test2");
*/

#ifndef MEGA_LOGGING_H
#define MEGA_LOGGING_H 1

#include "mega.h"

// define MEGA_QT_LOGGING to support QString
#ifdef MEGA_QT_LOGGING
    #include <QString>
#endif

namespace mega {

// available log levels
enum LogLevel {
    logFatal = 0, // Very severe error event that will presumably lead the application to abort.
    logError,     // Error information but will continue application to keep running.
    logWarning,   // Information representing errors in application but application will keep running
    logInfo,      // Mainly useful to represent current progress of application.
    logDebug,     // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
    logMax
};

// settings for each log level
struct OutputSettings {
    bool enableTime;    // display time component for each log line
    bool enableLevel;   // display log level component for each log line
    bool enableSource;  // display file name and line number component for each log line
};

// Output Log Interface
class MEGA_API Logger {
public:
    virtual void log(const char *time, int loglevel, const char *source, const char *message) = 0;
};

typedef std::map<enum LogLevel, struct OutputSettings> OutputSettingsMap;
typedef vector<std::ostream *> OutputStreams;

class MEGA_API OutputMap : public std::map<enum LogLevel, OutputStreams>
{
public:
    OutputMap() : std::map<enum LogLevel, OutputStreams>()
    {
        for (int i = logFatal; i <= logMax; i++)
        {
            (*this)[static_cast<LogLevel>(i)];
        }
    }
};

class MEGA_API SimpleLogger {
    enum LogLevel level;
    bool lineBreak;
    std::ostringstream ostr;
    std::string t;
    std::string fname;

    OutputStreams getOutput(enum LogLevel ll)
    {
        return outputs[ll];
    }

    string getTime();

public:
    static OutputMap outputs;
    static Logger *logger;
    static char base64Handle[14];
    static OutputSettingsMap outputSettings;

    static enum LogLevel logCurrentLevel;

    SimpleLogger(enum LogLevel ll, char const* filename, int line, bool lBreak = true);
    ~SimpleLogger();

    static const char *toStr(enum LogLevel ll)
    {
        switch (ll) {
            case logMax: return "verbose";
            case logDebug: return "debug";
            case logInfo: return "info";
            case logWarning: return "warn";
            case logError: return "err";
            case logFatal: return "FATAL";
            default: return "";
        }
        return "";
    }

    static const char *toNodeHandle(handle nodeHandle)
    {
        Base64::btoa((byte*)&(nodeHandle), MegaClient::NODEHANDLE, base64Handle);
        return base64Handle;
    }

    template <typename T>
    SimpleLogger& operator<<(T* obj)
    {
        if(obj != NULL)
            ostr << obj;
        else
            ostr << "(NULL)";

        return *this;
    }

    template <typename T>
    SimpleLogger& operator<<(T const& obj)
    {
        return *this;
    }

#ifdef MEGA_QT_LOGGING
    SimpleLogger& operator<<(const QString& s)
    {
        ostr << s.toUtf8().constData();
        return *this;
    }
#endif

    // set output class
    static void setOutputClass(Logger *logger_class)
    {
        logger = logger_class;
    }

    // register output stream for log level
    static void addOutput(enum LogLevel ll, std::ostream *os)
    {
        outputs[ll].push_back(os);
    }

    // register output stream for all log levels
    static void setAllOutputs(std::ostream *os)
    {
        for (int i = logFatal; i <= logMax; i++)
            outputs[static_cast<LogLevel>(i)].push_back(os);
    }

    // set the current log level. all logs which are higher than this level won't be handled
    static void setLogLevel(enum LogLevel ll)
    {
        SimpleLogger::logCurrentLevel = ll;
    }

    // Synchronizes all registered stream buffers with their controlled output sequence
    static void flush();

    // set output settings for log level
    static void setOutputSettings(enum LogLevel ll, bool enableTime, bool enableLevel, bool enableSource)
    {
        outputSettings[ll].enableTime = enableTime;
        outputSettings[ll].enableLevel = enableLevel;
        outputSettings[ll].enableSource = enableSource;
    }

};

// output VERBOSE log with line break
#define LOG_verbose \
    if (SimpleLogger::logCurrentLevel < logMax) ;\
    else \
        SimpleLogger(logMax, __FILE__, __LINE__)

// output VERBOSE log without line break
#define LOGn_verbose \
    if (SimpleLogger::logCurrentLevel < logMax) ;\
    else \
        SimpleLogger(logMax, __FILE__, __LINE__, false)

// output DEBUG log with line break
#define LOG_debug \
    if (SimpleLogger::logCurrentLevel < logDebug) ;\
    else \
        SimpleLogger(logDebug, __FILE__, __LINE__)

// output DEBUG log without line break
#define LOGn_debug \
    if (SimpleLogger::logCurrentLevel < logDebug) ;\
    else \
        SimpleLogger(logDebug, __FILE__, __LINE__, false)

#define LOG_info \
    if (SimpleLogger::logCurrentLevel < logInfo) ;\
    else \
        SimpleLogger(logInfo, __FILE__, __LINE__)
#define LOGn_info \
    if (SimpleLogger::logCurrentLevel < logInfo) ;\
    else \
        SimpleLogger(logInfo, __FILE__, __LINE__, false)

#define LOG_warn \
    if (SimpleLogger::logCurrentLevel < logWarning) ;\
    else \
        SimpleLogger(logWarning, __FILE__, __LINE__)
#define LOGn_warn \
    if (SimpleLogger::logCurrentLevel < logWarning) ;\
    else \
        SimpleLogger(logWarning, __FILE__, __LINE__, false)

#define LOG_err \
    if (SimpleLogger::logCurrentLevel < logError) ;\
    else \
        SimpleLogger(logError, __FILE__, __LINE__)
#define LOGn_err \
    if (SimpleLogger::logCurrentLevel < logError) ;\
    else \
        SimpleLogger(logError, __FILE__, __LINE__, false)

#define LOG_fatal \
    SimpleLogger(logFatal, __FILE__, __LINE__)
#define LOGn_fatal \
    SimpleLogger(logFatal, __FILE__, __LINE__, false)

#define LOG_NODEHANDLE(x) SimpleLogger::toNodeHandle(x)

} // namespace

#endif
