/**
* @file MAccountSession.cpp
* @brief Get details about a MEGA session.
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

#include "MAccountSession.h"

using namespace mega;
using namespace Platform;

MAccountSession::MAccountSession(MegaAccountSession *accountSession, bool cMemoryOwn)
{
    this->accountSession = accountSession;
    this->cMemoryOwn;
}

MAccountSession::~MAccountSession()
{
    if (cMemoryOwn)
        delete accountSession;
}

MegaAccountSession* MAccountSession::getCPtr()
{
    return accountSession;
}

int64 MAccountSession::getCreationTimestamp()
{
    return accountSession ? accountSession->getCreationTimestamp() : 0;
}

int64 MAccountSession::getMostRecentUsage()
{
    return accountSession ? accountSession->getMostRecentUsage() : 0;
}

String^ MAccountSession::getUserAgent()
{
    if (!accountSession) return nullptr;

    std::string utf16userAgent;
    const char *utf8userAgent = accountSession->getUserAgent();
    if (!utf8userAgent)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8userAgent, &utf16userAgent);
    delete[] utf8userAgent;

    return ref new String((wchar_t *)utf16userAgent.data());
}

String^ MAccountSession::getIP()
{
    if (!accountSession) return nullptr;

    std::string utf16ip;
    const char *utf8ip = accountSession->getIP();
    if (!utf8ip)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8ip, &utf16ip);
    delete[] utf8ip;

    return ref new String((wchar_t *)utf16ip.data());
}

String^ MAccountSession::getCountry()
{
    if (!accountSession) return nullptr;

    std::string utf16country;
    const char *utf8country = accountSession->getCountry();
    if (!utf8country)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8country, &utf16country);
    delete[] utf8country;

    return ref new String((wchar_t *)utf16country.data());
}

bool MAccountSession::isCurrent()
{
    return accountSession ? accountSession->isCurrent() : false;
}

bool MAccountSession::isAlive()
{
    return accountSession ? accountSession->isAlive() : false;
}

uint64 MAccountSession::getHandle()
{
    return accountSession ? accountSession->getHandle() : ::mega::INVALID_HANDLE;
}
