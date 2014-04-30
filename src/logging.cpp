/**
 * @file logging.cpp
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

#include "mega/logging.h"

namespace mega {

// static member initialization
SimpleLogger::OutputMap SimpleLogger::outputs;
SimpleLogger::OutputSettingsMap SimpleLogger::outputSettings;
// by the default, display logs with level equal or less than logInfo
enum LogLevel SimpleLogger::logCurrentLevel = logInfo;

SimpleLogger::SimpleLogger(enum LogLevel ll, char const* filename, int line, bool lBreak)
{
    struct OutputSettings settings = outputSettings[ll];
    level = ll;
    lineBreak = lBreak;

    if (settings.enableTime)
        ostr << "[" << getTime() << "] ";
    if (settings.enableLevel)
        ostr << "[" << toStr(ll) << "] ";
    if (settings.enableSource)
        ostr << filename << ":" << line << " ";
}

SimpleLogger::~SimpleLogger()
{
    OutputStreams::iterator iter;
    OutputStreams vec;

    if (lineBreak)
        ostr << std::endl;

    vec = getOutput(level);

    for (iter = vec.begin(); iter != vec.end(); iter++)
    {
        **iter << ostr.str();
    }
}

std::string SimpleLogger::getTime()
{
    char ts[50];
    time_t t = time(NULL);

    if (!strftime(ts, sizeof(ts), "%H:%M:%S", gmtime(&t))) {
        ts[0] = '\0';
    }
    return ts;
}

void SimpleLogger::flush()
{
    for (int i = logFatal; i < logMax; i++)
    {
        OutputStreams::iterator iter;
        OutputStreams vec;

        vec = outputs[static_cast<LogLevel>(i)];;

        for (iter = vec.begin(); iter != vec.end(); iter++)
        {
            std::ostream *os = *iter;
            os->flush();
        }
    }
}

} // namespace
