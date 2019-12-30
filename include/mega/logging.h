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

    4) Performance mode can be enabled via defining ENABLE_LOG_PERFORMANCE at compile time.

    In performance mode, the `SimpleLogger` does not lock mutexes nor does it heap-allocate.
    Only `loglevel` and `message` of the `Logger` are populated where `message` will include
    file/line. It is assumed that the subclass of `Logger` provides timing information.

    In performance mode, only outputting to a logger assigned through `setOutputClass` is supported.
    Output streams are not supported.
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

#include "mega/utils.h"

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
    // Note: `time` and `source` are null in performance mode
    virtual void log(const char *time, int loglevel, const char *source, const char *message) = 0;
};

typedef std::vector<std::ostream *> OutputStreams;

class OutputMap : public std::array<OutputStreams, unsigned(logMax)+1> {};

class SimpleLogger
{
    enum LogLevel level;

#ifndef ENABLE_LOG_PERFORMANCE
    std::ostringstream ostr;
    std::string t;
    std::string fname;

    std::string getTime();

    // logging can occur from multiple threads, so we need to protect the lists of loggers to send to
    // though the loggers themselves are presumed to be owned elsewhere, and the pointers must remain valid
    // actual output to the loggers is not synchronised (at least, not by this class)
    static std::mutex outputs_mutex;
    static OutputMap outputs;
    static OutputStreams getOutput(enum LogLevel ll);
#else
    std::array<char, 256> mBuffer; // will be stack-allocated since SimpleLogger is stack-allocated
    std::array<char, 256>::iterator mBufferIt;
    using DiffType = std::array<char, 256>::difference_type;
    using NumBuf = std::array<char, 24>;

    template<typename DataIterator>
    void copyToBuffer(const DataIterator dataIt, DiffType currentSize)
    {
        DiffType start = 0;
        while (currentSize > 0)
        {
            const auto size = std::min(currentSize, std::distance(mBufferIt, mBuffer.end() - 1));
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
        *mBufferIt = '\0';
        if (logger)
        {
            logger->log(nullptr, level, nullptr, mBuffer.data());
        }
        mBufferIt = mBuffer.begin();
    }

    template<typename T>
    typename std::enable_if<std::is_enum<T>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%d", static_cast<int>(value));
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_pointer<T>::value && !std::is_same<T, char*>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%p", reinterpret_cast<const void*>(value));
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && !std::is_same<T, long>::value && !std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%d", value);
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && std::is_same<T, long>::value && !std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%ld", value);
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && !std::is_same<T, long>::value && std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%lld", value);
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && !std::is_same<T, unsigned long>::value && !std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%u", value);
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && std::is_same<T, unsigned long>::value && !std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%lu", value);
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && !std::is_same<T, unsigned long>::value && std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%llu", value);
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf.data(), buf.size(), "%g", value);
        copyToBuffer(buf.data(), std::min(size, static_cast<int>(buf.size()) - 1));
    }

    void logValue(const char* value)
    {
        copyToBuffer(value, static_cast<DiffType>(std::strlen(value)));
    }

    void logValue(const std::string& value)
    {
        copyToBuffer(value.begin(), static_cast<DiffType>(value.size()));
    }
#endif

public:
    static Logger *logger;

    static enum LogLevel logCurrentLevel;

    SimpleLogger(const enum LogLevel ll, const char* filename, const int line)
    : level{ll}
#ifdef ENABLE_LOG_PERFORMANCE
    , mBufferIt{mBuffer.begin()}
#endif
    {
#ifdef ENABLE_LOG_PERFORMANCE
        const char * actualFileName = strrchr(filename, '/'); //The project part of the path uses `/` for Windows too.
        if (actualFileName)
        {
            logValue(actualFileName+1);
        }
        else
        {
            logValue(filename);
        }
        copyToBuffer(":", 1);
        logValue(line);
        copyToBuffer(" ", 1);
#else
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
#endif
    }

    ~SimpleLogger()
    {
#ifdef ENABLE_LOG_PERFORMANCE
        outputBuffer();
#else
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
#endif
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
#ifdef ENABLE_LOG_PERFORMANCE
        if (obj)
        {
            logValue(obj);
        }
        else
        {
            copyToBuffer("(NULL)", 6);
        }
#else
        if (obj)
        {
            ostr << obj;
        }
        else
        {
            ostr << "(NULL)";
        }
#endif
        return *this;
    }

    template <typename T, typename = typename std::enable_if<std::is_scalar<T>::value>::type>
    SimpleLogger& operator<<(const T obj)
    {
        static_assert(!std::is_same<T, std::nullptr_t>::value, "T cannot be nullptr_t");
#ifdef ENABLE_LOG_PERFORMANCE
        logValue(obj);
#else
        ostr << obj;
#endif
        return *this;
    }

    template <typename T, typename = typename std::enable_if<!std::is_scalar<T>::value>::type>
    SimpleLogger& operator<<(const T& obj)
    {
#ifdef ENABLE_LOG_PERFORMANCE
        logValue(obj);
#else
        ostr << obj;
#endif
        return *this;
    }

#ifdef MEGA_QT_LOGGING
    SimpleLogger& operator<<(const QString& s)
    {
#ifdef ENABLE_LOG_PERFORMANCE
        logValue(s.toUtf8().constData());
#else
        ostr << s.toUtf8().constData();
#endif
        return *this;
    }
#endif

    // set output class
    static void setOutputClass(Logger *logger_class)
    {
        logger = logger_class;
    }

    // set the current log level. all logs which are higher than this level won't be handled
    static void setLogLevel(enum LogLevel ll)
    {
        SimpleLogger::logCurrentLevel = ll;
    }

#ifndef ENABLE_LOG_PERFORMANCE
    // register output stream for log level
    static void addOutput(enum LogLevel ll, std::ostream *os);

    // register output stream for all log levels
    static void setAllOutputs(std::ostream *os);

    // Synchronizes all registered stream buffers with their controlled output sequence
    static void flush();
#endif
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
