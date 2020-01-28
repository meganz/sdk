/**
* @file DelegateMLogger.cpp
* @brief Delegate to receive SDK logs.
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

#include "DelegateMLogger.h"

#include <windows.h>
#include <sstream>

using namespace mega;
using namespace std;

DelegateMLogger::DelegateMLogger(MLoggerInterface^ logger)
{
    this->logger = logger;
}

MLoggerInterface^ DelegateMLogger::getUserLogger()
{
    return this->logger;
}

void DelegateMLogger::log(const char *time, int loglevel, const char *source, const char *message)
{
    if (logger != nullptr)
    {
        std::string utf16time;
        if (time)
            MegaApi::utf8ToUtf16(time, &utf16time);

        std::string utf16source;
        if (source)
            MegaApi::utf8ToUtf16(source, &utf16source);

        std::string utf16message;
        if (message)
            MegaApi::utf8ToUtf16(message, &utf16message);

        logger->log(time ? ref new String((wchar_t *)utf16time.c_str()) : nullptr,
            loglevel,
            source ? ref new String((wchar_t *)utf16source.c_str()) : nullptr,
            message ? ref new String((wchar_t *)utf16message.c_str()) : nullptr);
    }
    else
    {
        std::ostringstream oss;
        oss << time;
        switch (loglevel)
        {
        case MegaApi::LOG_LEVEL_DEBUG:
            oss << " (debug): ";
            break;
        case MegaApi::LOG_LEVEL_ERROR:
            oss << " (error): ";
            break;
        case MegaApi::LOG_LEVEL_FATAL:
            oss << " (fatal): ";
            break;
        case MegaApi::LOG_LEVEL_INFO:
            oss << " (info):  ";
            break;
        case MegaApi::LOG_LEVEL_MAX:
            oss << " (verb):  ";
            break;
        case MegaApi::LOG_LEVEL_WARNING:
            oss << " (warn):  ";
            break;
        }

        oss << message;
        string filename = source;
        if (filename.size())
        {
            unsigned int index = filename.find_last_of('\\');
            if (index != string::npos && filename.size() > (index + 1))
            {
                filename = filename.substr(index + 1);
            }
            oss << " (" << filename << ")";
        }

        oss << endl;
        string output = oss.str();
        string utf16output;
        MegaApi::utf8ToUtf16(output.c_str(), &utf16output);
        OutputDebugString((wchar_t *)utf16output.data());
    }
}
