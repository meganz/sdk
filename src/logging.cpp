/**
 * @file logging.cpp
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

#include "mega/logging.h"

#include <ctime>

namespace mega {

ExternalLogger g_externalLogger;
ExclusiveLogger g_exclusiveLogger;

Logger *SimpleLogger::logger = &g_externalLogger;

// by the default, display logs with level equal or less than logInfo
std::atomic<LogLevel> SimpleLogger::logCurrentLevel{logInfo};
long long SimpleLogger::maxPayloadLogSize  = 10240;

thread_local bool SimpleLogger::mThreadLocalLoggingDisabled = false;

#ifdef ENABLE_LOG_PERFORMANCE

thread_local std::array<char, LOGGER_CHUNKS_SIZE> SimpleLogger::mBuffer;
#ifndef NDEBUG
thread_local const SimpleLogger* SimpleLogger::mBufferOwner = nullptr;
#endif

#else

std::string SimpleLogger::getTime()
{
    char ts[50];
    time_t t = std::time(NULL);
    std::tm tm{};

#ifdef WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    if (std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm)) return ts;

    return {};
}
#endif

std::ostream& operator<< (std::ostream& ostr, const std::error_code &value)
{
    return ostr << value.category().name() << ": " << value.message();
}

std::ostream& operator<< (std::ostream& ostr, const std::system_error &se)
{
    return ostr << se.code().category().name() << ": " << se.what();
}



ExternalLogger::ExternalLogger()
{
    logToConsole = false;
}

ExternalLogger::~ExternalLogger()
{
}

void ExternalLogger::addMegaLogger(void* id, LogCallback lc)
{
    std::lock_guard<std::recursive_mutex> g(mutex);
    megaLoggers[id] = lc;
}

void ExternalLogger::removeMegaLogger(void* id)
{
    std::lock_guard<std::recursive_mutex> g(mutex);
    megaLoggers.erase(id);
}

void ExternalLogger::setLogToConsole(bool enable)
{
    this->logToConsole = enable;
}

void ExternalLogger::log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
    , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, unsigned numberMessages = 0
#endif
)
{
    if (!time)
    {
        time = "";
    }

    if (!source)
    {
        source = "";
    }

    if (!message)
    {
        message = "";
    }

    lock_guard<std::recursive_mutex> g(mutex);

    // solve the mystery of why the mutex is recursive
    // if we hit the assert, it's due to logging from inside the processing of the logging?
    assert(!alreadyLogging);
    alreadyLogging = true;


    for (auto& logger : megaLoggers)
    {
        logger.second(time, loglevel, source, message
#ifdef ENABLE_LOG_PERFORMANCE
            , directMessages, directMessagesSizes, numberMessages
#endif
        );

        if (useOnlyFirstMegaLogger)
        {
            break;
        }
    }

    if (logToConsole)
    {
        std::cout << "[" << time << "][" << SimpleLogger::toStr((LogLevel)loglevel) << "] ";
        if (message) std::cout << message;
#ifdef ENABLE_LOG_PERFORMANCE
        for (unsigned i = 0; i < numberMessages; ++i)
        {
            std::cout.write(directMessages[i], directMessagesSizes[i]);
        }
#endif
        std::cout << std::endl;
    }
    alreadyLogging = false;
}


void ExclusiveLogger::log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
    , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, unsigned numberMessages = 0
#endif
)
{
    if (!time)
    {
        time = "";
    }

    if (!source)
    {
        source = "";
    }

    if (!message)
    {
        message = "";
    }

    exclusiveCallback(time, loglevel, source, message
#ifdef ENABLE_LOG_PERFORMANCE
            , directMessages, directMessagesSizes, numberMessages
#endif
    );
}

} // namespace
