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
 *
 * This file is also distributed under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
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
    SimpleLogger::setAllOutputs(&std::cout);

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
            std::cout << "{" << time << "}" << " [" << source << "] " << message << std::endl;
        }
    };

    ...
    MyOutput output;

    // let's output both Debug (verbose) and Info (just messages) logs
    // and send logs to remote Log Server
    SimpleLogger::setOutputSettings(logDebug, true, true, true);
    SimpleLogger::setOutputSettings(logInfo, false, false, false);
    SimpleLogger::setLogLevel(logDebug);
    SimpleLogger::setAllOutputs(&std::cout);
    SimpleLogger::setOutputClass(output);
    SimpleLogger::setOutputClass(&myOutput);

    LOG_debug << "test";
    LOG_info << "informing";


    3)
    if MEGA_QT_LOGGING defined:

    QString a = QString::fromAscii("test1");
    LOG_info << a;
    LOG_info << QString::fromAscii("test2");

    4) Performance mode can be enabled via:
    mega::SimpleLogger::setPeformanceMode(true);

    In performance mode, the `SimpleLogger` does not lock mutexes nor does it heap-allocate.
    Only `loglevel` and `message` of the `Logger` are populated where `message` will include
    file/line. It is assumed that the subclass of `Logger` provides timing information.

    In performance mode, only outputting to a logger assigned through `setOutputClass` is supported.
    Output streams set via `addOutput` or `setAllOutputs` are ignored.
*/
#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// define MEGA_QT_LOGGING to support QString
#ifdef MEGA_QT_LOGGING
    #include <QString>
#endif

namespace mega {

// available log levels
enum LogLevel
{
    logFatal = 0, // Very severe error event that will presumably lead the application to abort.
    logError,     // Error information but will continue application to keep running.
    logWarning,   // Information representing errors in application but application will keep running
    logInfo,      // Mainly useful to represent current progress of application.
    logDebug,     // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
    logMax
};

// Output Log Interface
class Logger
{
public:
    virtual ~Logger() = default;
    virtual void log(const char *time, int loglevel, const char *source, const char *message) = 0;
};

typedef std::vector<std::ostream *> OutputStreams;

class OutputMap : public std::array<OutputStreams, unsigned(logMax)+1> {};

class SimpleLogger
{
    enum LogLevel level;
    std::ostringstream ostr;
    std::string t;
    std::string fname;

    std::string getTime();

    std::array<char, 256> mBuffer; // used in performance mode (stack-allocated since SimpleLogger is normally stack-allocated)
    std::array<char, 256>::iterator mBufferIt; // used in performance mode

    // logging can occur from multiple threads, so we need to protect the lists of loggers to send to
    // though the loggers themselves are presumed to be owned elsewhere, and the pointers must remain valid
    // actual output to the loggers is not synchronised (at least, not by this class)
    static std::mutex outputs_mutex;
    static OutputMap outputs;
    static OutputStreams getOutput(enum LogLevel ll);

    static bool performanceMode;

    template<typename DataIterator>
    void copyToBuffer(const DataIterator dataIt, size_t currentSize)
    {
        size_t start = 0;
        while (currentSize > 0)
        {
            const auto size = std::min(currentSize, static_cast<size_t>(std::distance(mBufferIt, mBuffer.end() - 1)));
            mBufferIt = std::copy(dataIt + start, dataIt + start + size, mBufferIt);
            if (mBufferIt == mBuffer.end() - 1)
            {
                outputBuffer();
            }
            start += size;
            currentSize -= size;
        }
    }

    void outputBuffer()
    {
        if (logger)
        {
            *mBufferIt = 0;
            logger->log(nullptr, level, nullptr, mBuffer.data());
            mBufferIt = mBuffer.begin();
        }
    }

    template<typename T>
    typename std::enable_if<std::is_enum<T>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%d", static_cast<int>(value));
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_pointer<T>::value && !std::is_same<T, char*>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%p", reinterpret_cast<const void*>(value));
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && !std::is_same<T, long>::value && !std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%d", value);
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && std::is_same<T, long>::value && !std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%ld", value);
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && !std::is_same<T, long>::value && std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%lld", value);
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && !std::is_same<T, unsigned long>::value && !std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%u", value);
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && std::is_same<T, unsigned long>::value && !std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%lu", value);
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && !std::is_same<T, unsigned long>::value && std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%llu", value);
        copyToBuffer(str, size);
    }

    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value>::type
    logValue(const T value)
    {
        char str[20];
        const auto size = std::sprintf(str, "%f", value);
        copyToBuffer(str, size);
    }

    void logValue(const char* value)
    {
        copyToBuffer(value, std::strlen(value));
    }

    void logValue(const std::string& value)
    {
        copyToBuffer(value.begin(), value.size());
    }

public:
    static Logger *logger;

    static enum LogLevel logCurrentLevel;

    SimpleLogger(const enum LogLevel ll, const char* filename, const int line)
    : level{ll}
    , mBufferIt{mBuffer.begin()}
    {
        if (performanceMode)
        {
            logValue(filename);
            copyToBuffer(":", 1);
            logValue(line);
            copyToBuffer(" ", 1);
            return;
        }

        if (!logger)
        {
            return;
        }

        t = getTime();
        std::ostringstream oss;
        oss << filename;
        if(line >= 0)
        {
            oss << ":" << line;
        }
        fname = oss.str();
    }

    ~SimpleLogger()
    {
        if (performanceMode)
        {
            outputBuffer();
            return;
        }

        OutputStreams::iterator iter;
        OutputStreams vec;

        if (logger)
            logger->log(t.c_str(), level, fname.c_str(), ostr.str().c_str());

        ostr << std::endl;

        vec = getOutput(level);

        for (iter = vec.begin(); iter != vec.end(); iter++)
        {
            **iter << ostr.str();
        }
    }

    static const char *toStr(enum LogLevel ll)
    {
        switch (ll)
        {
            case logMax: return "verbose";
            case logDebug: return "debug";
            case logInfo: return "info";
            case logWarning: return "warn";
            case logError: return "err";
            case logFatal: return "FATAL";
        }
        assert(false);
        return "";
    }

    template <typename T>
    SimpleLogger& operator<<(T* obj)
    {
        const char* null = "(NULL)";

        if (performanceMode)
        {
            if (obj != NULL)
            {
                logValue(obj);
            }
            else
            {
                copyToBuffer(null, 6);
            }
            return *this;
        }

        if(obj != NULL)
            ostr << obj;
        else
            ostr << null;

        return *this;
    }

    template <typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value>::type>
    SimpleLogger& operator<<(const T obj)
    {
        if (performanceMode)
        {
            logValue(obj);
            return *this;
        }

        ostr << obj;
        return *this;
    }

    template <typename T, typename = typename std::enable_if<!std::is_arithmetic<T>::value>::type>
    SimpleLogger& operator<<(const T& obj)
    {
        if (performanceMode)
        {
            logValue(obj);
            return *this;
        }

        ostr << obj;
        return *this;
    }

#ifdef MEGA_QT_LOGGING
    SimpleLogger& operator<<(const QString& s)
    {
        if (performanceMode)
        {
            logValue(s.toUtf8().constData());
            return *this;
        }

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
    static void addOutput(enum LogLevel ll, std::ostream *os);

    // register output stream for all log levels
    static void setAllOutputs(std::ostream *os);

    // set the current log level. all logs which are higher than this level won't be handled
    static void setLogLevel(enum LogLevel ll)
    {
        SimpleLogger::logCurrentLevel = ll;
    }

    // set whether we're in performance mode (default: false)
    static void setPeformanceMode(bool enable)
    {
        SimpleLogger::performanceMode = enable;
    }

    // are we in performance mode?
    static bool inPerformanceMode()
    {
        return SimpleLogger::performanceMode;
    }

    // Synchronizes all registered stream buffers with their controlled output sequence
    static void flush();
};

#define LOG_verbose \
    if (::mega::SimpleLogger::logCurrentLevel < ::mega::logMax) ;\
    else \
        ::mega::SimpleLogger(::mega::logMax, __FILE__, __LINE__)

#define LOG_debug \
    if (::mega::SimpleLogger::logCurrentLevel < ::mega::logDebug) ;\
    else \
        ::mega::SimpleLogger(::mega::logDebug, __FILE__, __LINE__)

#define LOG_info \
    if (::mega::SimpleLogger::logCurrentLevel < ::mega::logInfo) ;\
    else \
        ::mega::SimpleLogger(::mega::logInfo, __FILE__, __LINE__)

#define LOG_warn \
    if (::mega::SimpleLogger::logCurrentLevel < ::mega::logWarning) ;\
    else \
        ::mega::SimpleLogger(::mega::logWarning, __FILE__, __LINE__)

#define LOG_err \
    if (::mega::SimpleLogger::logCurrentLevel < ::mega::logError) ;\
    else \
        ::mega::SimpleLogger(::mega::logError, __FILE__, __LINE__)

#define LOG_fatal \
    ::mega::SimpleLogger(::mega::logFatal, __FILE__, __LINE__)

} // namespace
