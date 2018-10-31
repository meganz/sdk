/**
* @file MUserAlert.cpp
* @brief Represents a user alert in MEGA.
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

#include "MUserAlert.h"

using namespace mega;
using namespace Platform;

MUserAlert::MUserAlert(MegaUserAlert *megaUserAlert, bool cMemoryOwn)
{
    this->megaUserAlert = megaUserAlert;
    this->cMemoryOwn = cMemoryOwn;
}

MUserAlert::~MUserAlert()
{
    if (cMemoryOwn)
        delete megaUserAlert;
}

MegaUserAlert* MUserAlert::getCPtr()
{
    return megaUserAlert;
}

MUserAlert^ MUserAlert::copy()
{
    return megaUserAlert ? ref new MUserAlert(megaUserAlert->copy(), true) : nullptr;
}

unsigned int MUserAlert::getId()
{
    return megaUserAlert ? megaUserAlert->getId() : (unsigned)-1;
}

bool MUserAlert::getSeen()
{
    return megaUserAlert ? megaUserAlert->getSeen() : false;
}

bool MUserAlert::getRelevant()
{
    return megaUserAlert ? megaUserAlert->getRelevant() : false;
}

int MUserAlert::getType()
{
    return megaUserAlert ? megaUserAlert->getType() : -1;
}

String^ MUserAlert::getTypeString()
{
    if (!megaUserAlert) return nullptr;

    std::string utf16type;
    const char *utf8type = megaUserAlert->getTypeString();
    if (!utf8type)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8type, &utf16type);
    delete[] utf8type;

    return ref new String((wchar_t *)utf16type.data());
}

uint64 MUserAlert::getUserHandle()
{
    return megaUserAlert ? megaUserAlert->getUserHandle() : ::mega::INVALID_HANDLE;
}

uint64 MUserAlert::getNodeHandle()
{
    return megaUserAlert ? megaUserAlert->getNodeHandle() : ::mega::INVALID_HANDLE;
}

String^ MUserAlert::getEmail()
{
    if (!megaUserAlert) return nullptr;

    std::string utf16email;
    const char *utf8email = megaUserAlert->getEmail();
    if (!utf8email)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8email, &utf16email);
    delete[] utf8email;

    return ref new String((wchar_t *)utf16email.data());
}

String^ MUserAlert::getPath()
{
    if (!megaUserAlert) return nullptr;

    std::string utf16path;
    const char *utf8path = megaUserAlert->getPath();
    if (!utf8path)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8path, &utf16path);
    delete[] utf8path;

    return ref new String((wchar_t *)utf16path.data());
}

String^ MUserAlert::getHeading()
{
    if (!megaUserAlert) return nullptr;

    std::string utf16heading;
    const char *utf8heading = megaUserAlert->getHeading();
    if (!utf8heading)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8heading, &utf16heading);
    delete[] utf8heading;

    return ref new String((wchar_t *)utf16heading.data());
}

String^ MUserAlert::getTitle()
{
    if (!megaUserAlert) return nullptr;

    std::string utf16title;
    const char *utf8title = megaUserAlert->getTitle();
    if (!utf8title)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8title, &utf16title);
    delete[] utf8title;

    return ref new String((wchar_t *)utf16title.data());
}

uint64 MUserAlert::getNumber(unsigned int index)
{
    return megaUserAlert ? megaUserAlert->getNumber(index) : -1;
}

uint64 MUserAlert::getTimestamp(unsigned int index)
{
    return megaUserAlert ? megaUserAlert->getTimestamp(index) : -1;
}

String^ MUserAlert::getString(unsigned int index)
{
    if (!megaUserAlert) return nullptr;

    std::string utf16string;
    const char *utf8string = megaUserAlert->getString(index);
    if (!utf8string)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8string, &utf16string);
    delete[] utf8string;

    return ref new String((wchar_t *)utf16string.data());
}
