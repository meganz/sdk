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

#if ((defined(ANDROID) || defined(__ANDROID__)) && defined(ENABLE_CRASHLYTICS))
#include "../../third_party/crashlytics.h"
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
    // Note: `time` and `source` are null in performance mode
    virtual void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
          , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, unsigned numberMessages = 0
#endif
                     ) = 0;
};

const static size_t LOGGER_CHUNKS_SIZE = 1024;

/**
 * @brief holds a const char * and its size to pass to SimpleLogger, to use the direct logging logic
 */
class DirectMessage{

private:
    const static size_t directMsgThreshold = 1024; //below this, the msg will be buffered as a normal message
    bool mForce = false;
    size_t mSize = 0;
    const char *mConstChar = nullptr;

public:

    DirectMessage( const char *constChar, bool force = false) //force will set size as max, so as to stay above directMsgThreshold
    {
        mConstChar = constChar;
        mSize = strlen(constChar);
        mForce = force;
    }

    template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
    DirectMessage( const char *constChar, T size, bool force = false)
    {
        mConstChar = constChar;
        mSize = static_cast<size_t>(size);
        mForce = force;
    }

    size_t size() const
    {
        return mSize;
    }

    const char *constChar() const
    {
        return mConstChar;
    }
};

class SimpleLogger
{
public:
    // flag to turn off logging on the log-output thread, to prevent possible deadlock cycles.
    static thread_local bool mThreadLocalLoggingDisabled;

private:
    enum LogLevel level;

#ifndef ENABLE_LOG_PERFORMANCE
    std::ostringstream ostr;
    std::string t;
    std::string fname;

    std::string getTime();
#else

#ifdef WIN32
    static thread_local std::array<char, LOGGER_CHUNKS_SIZE> mBuffer;
#else
    static __thread std::array<char, LOGGER_CHUNKS_SIZE> mBuffer;
#endif
    std::array<char, LOGGER_CHUNKS_SIZE>::iterator mBufferIt;

    using DiffType = std::array<char, LOGGER_CHUNKS_SIZE>::difference_type;
    using NumBuf = char[24];
    const char* filenameStr;
    int lineNum;

    std::vector<DirectMessage> mDirectMessages;
    std::vector<std::string *> mCopiedParts;

    template<typename DataIterator>
    void copyToBuffer(const DataIterator dataIt, DiffType currentSize)
    {
        if (mThreadLocalLoggingDisabled) return;

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

    void outputBuffer(bool lastcall = false)
    {
        if (mThreadLocalLoggingDisabled) return;

        *mBufferIt = '\0';
        if (!mDirectMessages.empty()) // some part has already been passed as direct, we'll do all directly
        {
            if (lastcall) //the mBuffer can be reused
            {
                mDirectMessages.push_back(DirectMessage(mBuffer.data(), std::distance(mBuffer.begin(), mBufferIt)));
            }
            else //reached LOGGER_CHUNKS_SIZE, we need to copy mBuffer contents
            {
                std::string *newStr  = new string(mBuffer.data());
                mCopiedParts.emplace_back( newStr);
                string * back = mCopiedParts[mCopiedParts.size()-1];
                mDirectMessages.push_back(DirectMessage(back->data(), back->size()));
            }
        }
        else if (logger)
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
        const auto size = snprintf(buf, sizeof(buf), "%d", static_cast<int>(value));
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_pointer<T>::value && !std::is_same<T, char*>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%p", reinterpret_cast<const void*>(value));
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && !std::is_same<T, long>::value && !std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%d", value);
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && std::is_same<T, long>::value && !std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%ld", value);
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value
                            && !std::is_same<T, long>::value && std::is_same<T, long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%lld", value);
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && !std::is_same<T, unsigned long>::value && !std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%u", value);
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && std::is_same<T, unsigned long>::value && !std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%lu", value);
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value
                            && !std::is_same<T, unsigned long>::value && std::is_same<T, unsigned long long>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%llu", value);
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value>::type
    logValue(const T value)
    {
        NumBuf buf;
        const auto size = snprintf(buf, sizeof(buf), "%g", value);
        copyToBuffer(buf, std::min(size, static_cast<int>(sizeof(buf)) - 1));
    }

    void logValue(const char* value)
    {
        copyToBuffer(value, static_cast<DiffType>(std::strlen(value)));
    }

    void logValue(const std::string& value)
    {
        copyToBuffer(value.begin(), static_cast<DiffType>(value.size()));
    }

    void logValue(const ::mega::Error& value)   // ::mega:: when building MEGAchat on windows, else ambiguity errors
    {
        logValue(error(value));
    }

    void logValue(const std::error_code& value)
    {
        logValue(value.category().name());
        logValue(":");
        logValue(value.message());
    }

    void logValue(const std::system_error& se)
    {
        logValue(se.code().category().name());
        logValue(": ");
        logValue(se.what());
    }
#endif

public:
    static Logger *logger;

    static enum LogLevel logCurrentLevel;

    static long long maxPayloadLogSize; //above this, the msg will be truncated by [ ... ]

    SimpleLogger(const enum LogLevel ll, const char* filename, const int line)
    : level{ll}
#ifdef ENABLE_LOG_PERFORMANCE
    , mBufferIt{mBuffer.begin()}
    , filenameStr(filename)
    , lineNum(line)
#endif
    {
        if (mThreadLocalLoggingDisabled) return;

#ifndef ENABLE_LOG_PERFORMANCE
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
        if (mThreadLocalLoggingDisabled) return;

#ifdef ENABLE_LOG_PERFORMANCE
        if (filenameStr && lineNum != -1)
        {
            copyToBuffer(" [", 2);
            logValue(filenameStr);  // put filename and line last, to keep the main text nicely column aligned
            copyToBuffer(":", 1);
            logValue(lineNum);
            copyToBuffer("]", 1);
        }

        outputBuffer(true);

        if (!mDirectMessages.empty())
        {
            if (logger)
            {
                std::unique_ptr<const char *[]> dm(new const char *[mDirectMessages.size()]);
                std::unique_ptr<size_t[]> dms(new size_t[mDirectMessages.size()]);
                unsigned i = 0;
                for (const auto & d : mDirectMessages)
                {
                    dm[i] = d.constChar();
                    dms[i] = d.size();
                    i++;
                }

                logger->log(nullptr, level, nullptr, "", dm.get(), dms.get(), static_cast<int>(i));
            }
        }
        for (auto &s: mCopiedParts)
        {
            delete s;
        }
#else
        if (logger)
            logger->log(t.c_str(), level, fname.c_str(), ostr.str().c_str());
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

    template <typename T>
    SimpleLogger& operator<<(const std::unique_ptr<T>& ptr)
    {
#ifdef ENABLE_LOG_PERFORMANCE
        if (!ptr)
        {
            logValue("<empty unique ptr>");
        }
        else
        {
            logValue(*ptr.get());
        }
#else
        if (!ptr)
        {
            ostr << "<empty unique ptr>";
        }
        else
        {
            ostr << *ptr.get();
        }
#endif
        return *this;
    }

    template <typename T>
    SimpleLogger& operator<<(const std::shared_ptr<T>& ptr)
    {
#ifdef ENABLE_LOG_PERFORMANCE
        if (!ptr)
        {
            logValue("<empty shared ptr>");
        }
        else
        {
            logValue(*ptr.get());
        }
#else
        if (!ptr)
        {
            ostr << "<empty shared ptr>";
        }
        else
        {
            ostr << *ptr.get();
        }
#endif
        return *this;
    }

    SimpleLogger& operator<<(const DirectMessage &obj)
    {
#ifndef ENABLE_LOG_PERFORMANCE
        ostr.write(obj.constChar(), obj.size());
#else
        // careful using constChar() without taking size() into account: *this << obj.constChar(); ended up with 2MB+ lines from fetchnodes.

        if (mBufferIt != mBuffer.begin()) //something was appended to the buffer before this direct msg
        {
            *mBufferIt = '\0';
            std::string *newStr  = new string(mBuffer.data());
            mCopiedParts.emplace_back( newStr);
            string * back = mCopiedParts[mCopiedParts.size()-1];

            mDirectMessages.push_back(DirectMessage(back->data(), back->size()));
            mBufferIt = mBuffer.begin();
        }

        mDirectMessages.push_back(DirectMessage(obj.constChar(), obj.size()));
#endif
        return *this;
    }

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

    // set the limit of size to requests payload
    static void setMaxPayloadLogSize(long long size)
    {
        maxPayloadLogSize = size;
    }

    // Log messages forwarded from the client app though the configured logging mechanisms.
    // These do not go through the LOG_<level> macros.
    static void postLog(LogLevel logLevel, const char *message, const char *filename, int line)
    {
        if (logCurrentLevel < logLevel) return;
        SimpleLogger logger(logLevel, filename ? filename : "", line);
        if (message) logger << message;
    }
};

// source file leaf name - maybe to be compile time calculated one day
template<std::size_t N> inline const char* log_file_leafname( const char (&fullpath)[N]) {
    for (auto i = N - 1; --i; )
    {
        if (fullpath[i] == '/' || fullpath[i] == '\\')
            return &fullpath[i+1];
    }
    return fullpath;
}

std::ostream& operator <<(std::ostream&, const std::system_error&);
std::ostream& operator <<(std::ostream&, const std::error_code&);

#define LOG_verbose \
    if (::mega::SimpleLogger::logCurrentLevel >= ::mega::logMax) \
        ::mega::SimpleLogger(::mega::logMax, ::mega::log_file_leafname(__FILE__), __LINE__)

#define LOG_debug \
    if (::mega::SimpleLogger::logCurrentLevel >= ::mega::logDebug) \
        ::mega::SimpleLogger(::mega::logDebug, ::mega::log_file_leafname(__FILE__), __LINE__)

#define LOG_info \
    if (::mega::SimpleLogger::logCurrentLevel >= ::mega::logInfo) \
        ::mega::SimpleLogger(::mega::logInfo, ::mega::log_file_leafname(__FILE__), __LINE__)

#define LOG_warn \
    if (::mega::SimpleLogger::logCurrentLevel >= ::mega::logWarning) \
        ::mega::SimpleLogger(::mega::logWarning, ::mega::log_file_leafname(__FILE__), __LINE__)

#define LOG_err \
    if (::mega::SimpleLogger::logCurrentLevel >= ::mega::logError) \
        ::mega::SimpleLogger(::mega::logError, ::mega::log_file_leafname(__FILE__), __LINE__)

#define LOG_fatal \
    ::mega::SimpleLogger(::mega::logFatal, ::mega::log_file_leafname(__FILE__), __LINE__)

#if (defined(ANDROID) || defined(__ANDROID__))
inline void crashlytics_log(const char* msg)
{
#ifdef ENABLE_CRASHLYTICS
    firebase::crashlytics::Log(msg);
#endif
}
#endif

// moved from the intermediate layer
class ExternalLogger : public Logger
{
public:

    typedef std::function<
        void(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
            , const char **directMessages, size_t *directMessagesSizes, unsigned numberMessages
#endif
            )> LogCallback;

    ExternalLogger();
    ~ExternalLogger();
    void addMegaLogger(void* id, LogCallback);
    void removeMegaLogger(void* id);
    void setLogLevel(int logLevel);
    void setLogToConsole(bool enable);
    void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
        , const char **directMessages, size_t *directMessagesSizes, unsigned numberMessages
#endif
    ) override;

private:
    std::recursive_mutex mutex;
    map<void*, LogCallback> megaLoggers;
    bool logToConsole;
    bool alreadyLogging = false;
};


class ExclusiveLogger : public Logger
{
    // A lock-free adapter for loggers that require not to lock the mutex (e.g. RotativePerformanceLogger)
    // Note: we are using this being extra precautiuos: we don't let these loggers to work with any other external loggers. Hence the Exclusive.

public:

    typedef std::function<
        void(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
            , const char **directMessages, size_t *directMessagesSizes, unsigned numberMessages
#endif
            )> LogCallback;

    void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
        , const char **directMessages, size_t *directMessagesSizes, unsigned numberMessages
#endif
    ) override;

    LogCallback exclusiveCallback;
};


// This used to be a static member of MegaApi_impl
// However, megacli could not use or test it from there since it
// uses the SDK core directly, and not the intermediate layer
// So, although globals and singletons are not ideal, moving it here
// is one step forwards in tidying that up.
extern ExternalLogger g_externalLogger;
extern ExclusiveLogger g_exclusiveLogger;

} // namespace
