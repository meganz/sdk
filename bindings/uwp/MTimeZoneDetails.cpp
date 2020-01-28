/**
* @file MTimeZoneDetails.h
* @brief Get details about timezones and the current default.
*
* (c) 2013-2018 by Mega Limited, Auckland, New Zealand
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

#include "MTimeZoneDetails.h"

using namespace mega;
using namespace Platform;

MTimeZoneDetails::MTimeZoneDetails(MegaTimeZoneDetails *timeZoneDetails, bool cMemoryOwn)
{
    this->timeZoneDetails = timeZoneDetails;
    this->cMemoryOwn = cMemoryOwn;
}

MTimeZoneDetails::~MTimeZoneDetails()
{
    if (cMemoryOwn)
        delete timeZoneDetails;
}

MegaTimeZoneDetails* MTimeZoneDetails::getCPtr()
{
    return timeZoneDetails;
}

MTimeZoneDetails^ MTimeZoneDetails::copy()
{
    return timeZoneDetails ? ref new MTimeZoneDetails(timeZoneDetails->copy(), true) : nullptr;
}

int MTimeZoneDetails::getNumTimeZones()
{
    return timeZoneDetails ? timeZoneDetails->getNumTimeZones() : 0;
}

String^ MTimeZoneDetails::getTimeZone(int index)
{
    if (!timeZoneDetails) return nullptr;

    std::string utf16timeZone;
    const char *utf8timeZone = timeZoneDetails->getTimeZone(index);
    MegaApi::utf8ToUtf16(utf8timeZone, &utf16timeZone);

    return utf8timeZone ? ref new String((wchar_t *)utf16timeZone.data()) : nullptr;
}

int MTimeZoneDetails::getTimeOffset(int index)
{
    return timeZoneDetails ? timeZoneDetails->getTimeOffset(index) : 0;
}

int MTimeZoneDetails::getDefault()
{
    return timeZoneDetails ? timeZoneDetails->getDefault() : -1;
}