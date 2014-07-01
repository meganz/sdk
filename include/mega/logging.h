/**
 * @file mega/logging.h
 * @brief Logging class
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

    #include <fstream>  // for std::ofstream

    // output debug messages to file
    std::ofstream debugfile;
    debugfile.open("debug.log");

    // use logDebug level
    SimpleLogger::setLogLevel(logDebug);
    // set output to stdout for all messages
    SimpleLogger::setAllOutputs(&std::cout);

    // add file for debug output only
    SimpleLogger::addOutput(logDebug, &debugfile);
    // print additional components for debug output
    SimpleLogger::setOutputSettings(logDebug, true, true, true);

    ...

    LOG_debug << "test"; // will print message on screen and append line to debugfile
    LOG_info << "informing"; // will only print message on screen


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
    logFatal,   // Very severe error event that will presumably lead the application to abort.
    logError,   // Error information but will continue application to keep running.
    logWarning, // Information representing errors in application but application will keep running
    logInfo,    // Mainly useful to represent current progress of application.
    logDebug,   // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
    logMax
};

// settings for each log level
struct OutputSettings {
    bool enableTime;    // display time component for each log line
    bool enableLevel;   // display log level component for each log line
    bool enableSource;  // display file name and line number component for each log line
};

class MEGA_API SimpleLogger {
    enum LogLevel level;
    bool lineBreak;
    std::ostringstream ostr;
    typedef vector<std::ostream *> OutputStreams;

    static const char *toStr(enum LogLevel ll)
    {
        switch (ll) {
            case logDebug: return "debug";
            case logInfo: return "info";
            case logWarning: return "warn";
            case logError: return "err";
            case logFatal: return "FATAL";
            default: return "";
        }
        return "";
    }

    OutputStreams getOutput(enum LogLevel ll)
    {
        return outputs[ll];
    }

    string getTime();

public:
    typedef std::map<enum LogLevel, OutputStreams> OutputMap;
    static OutputMap outputs;

    typedef std::map<enum LogLevel, struct OutputSettings> OutputSettingsMap;
    static OutputSettingsMap outputSettings;

    static enum LogLevel logCurrentLevel;

    SimpleLogger(enum LogLevel ll, char const* filename, int line, bool lBreak = true);
    ~SimpleLogger();

    template <typename T>
    SimpleLogger& operator<<(T const& obj)
    {
        ostr << obj;
        return *this;
    }

#ifdef MEGA_QT_LOGGING
    SimpleLogger& operator<<(const QString& s)
    {
        ostr << s.toUtf8().constData();
        return *this;
    }
#endif

    // register output stream for log level
    static void addOutput(enum LogLevel ll, std::ostream *os)
    {
        outputs[ll].push_back(os);
    }

    // register output stream for all log levels
    static void setAllOutputs(std::ostream *os)
    {
        for (int i = logFatal; i < logMax; i++)
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

// an empty logger class
class MEGA_API NullLogger {
public:
    template<typename T>
    inline NullLogger& operator<<(T const& obj)
    {
        return *this;
    }
};

// enable debug level if DEBUG symbol is defined
#ifdef DEBUG
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
#else
#define LOG_debug NullLogger()
#define LOGn_debug NullLogger()
#endif // DEBUG

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

} // namespace

#endif
