/**
* @file MShare.cpp
* @brief Represents the outbound sharing of a folder with an user in MEGA
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

#include "MShare.h"

using namespace mega;
using namespace Platform;

MShare::MShare(MegaShare *megaShare, bool cMemoryOwn)
{
    this->megaShare = megaShare;
    this->cMemoryOwn = cMemoryOwn;
}

MShare::~MShare()
{
    if (cMemoryOwn)
        delete megaShare;
}

MegaShare * MShare::getCPtr()
{
    return megaShare;
}

String^ MShare::getUser()
{
    if (!megaShare) return nullptr;

    std::string utf16user;
    const char *utf8user = megaShare->getUser();
    MegaApi::utf8ToUtf16(utf8user, &utf16user);

    return utf8user ? ref new String((wchar_t *)utf16user.data()) : nullptr;
}

uint64 MShare::getNodeHandle()
{
    return megaShare ? megaShare->getNodeHandle() : ::mega::INVALID_HANDLE;
}

int MShare::getAccess()
{
    return megaShare ? megaShare->getAccess() : MegaShare::ACCESS_UNKNOWN;
}

uint64 MShare::getTimestamp()
{
    return megaShare ? megaShare->getTimestamp() : 0;
}
