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

#if defined(WINDOWS_PHONE) && !defined(__STDC_LIMIT_MACROS)
#define __STDC_LIMIT_MACROS
#endif

#include "mega/logging.h"

#include <ctime>

#if defined(WINDOWS_PHONE)
#include <stdint.h>
#endif

namespace mega {

Logger *SimpleLogger::logger = nullptr;

// by the default, display logs with level equal or less than logInfo
enum LogLevel SimpleLogger::logCurrentLevel = logInfo;
long long SimpleLogger::maxPayloadLogSize  = 10240;

#ifdef ENABLE_LOG_PERFORMANCE

#ifdef WIN64
thread_local std::array<char, LOGGER_CHUNKS_SIZE> SimpleLogger::mBuffer;
#elif WIN32
//thread_local std::array<char, LOGGER_CHUNKS_SIZE> SimpleLogger::mBuffer; 
#else
__thread std::array<char, LOGGER_CHUNKS_SIZE> SimpleLogger::mBuffer; 
#endif

#else
// static member initialization
std::mutex SimpleLogger::outputs_mutex;
OutputMap SimpleLogger::outputs;

std::string SimpleLogger::getTime()
{
    char ts[50];
    time_t t = std::time(NULL);

    if (!std::strftime(ts, sizeof(ts), "%H:%M:%S", std::gmtime(&t))) {
        ts[0] = '\0';
    }
    return ts;
}

void SimpleLogger::flush()
{
    for (auto& o : outputs)
    {
        OutputStreams::iterator iter;
        OutputStreams vec;

        {
            std::lock_guard<std::mutex> guard(outputs_mutex);
            vec = o;
        }

        for (iter = vec.begin(); iter != vec.end(); iter++)
        {
            std::ostream *os = *iter;
            os->flush();
        }
    }
}

OutputStreams SimpleLogger::getOutput(enum LogLevel ll)
{
    assert(unsigned(ll) < outputs.size());
    std::lock_guard<std::mutex> guard(outputs_mutex);
    return outputs[ll];
}

void SimpleLogger::addOutput(enum LogLevel ll, std::ostream *os)
{
    assert(unsigned(ll) < outputs.size());
    std::lock_guard<std::mutex> guard(outputs_mutex);
    outputs[ll].push_back(os);
}

void SimpleLogger::setAllOutputs(std::ostream *os)
{
    std::lock_guard<std::mutex> guard(outputs_mutex);
    for (auto& o : outputs)
    {
        o.push_back(os);
    }
}
#endif

} // namespace
