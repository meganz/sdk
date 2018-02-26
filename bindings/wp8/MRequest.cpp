/**
* @file MRequest.cpp
* @brief Provides information about an asynchronous request.
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

#include "MRequest.h"

using namespace mega;
using namespace Platform;

MRequest::MRequest(MegaRequest *megaRequest, bool cMemoryOwn)
{
    this->megaRequest = megaRequest;
    this->cMemoryOwn = cMemoryOwn;
}

MRequest::~MRequest()
{
    if (cMemoryOwn)
        delete megaRequest;
}

MegaRequest* MRequest::getCPtr()
{
    return megaRequest;
}

MRequest^ MRequest::copy()
{
    return megaRequest ? ref new MRequest(megaRequest->copy(), true) : nullptr;
}

MRequestType MRequest::getType()
{
    return (MRequestType) (megaRequest ? megaRequest->getType() : -1);
}

String^ MRequest::getRequestString()
{
    if (!megaRequest) return nullptr;

    std::string utf16request;
    const char *utf8request = megaRequest->getRequestString();
    MegaApi::utf8ToUtf16(utf8request, &utf16request);

    return utf8request ? ref new String((wchar_t *)utf16request.data()) : nullptr;
}

String^ MRequest::toString()
{
    return getRequestString();
}

uint64 MRequest::getNodeHandle()
{
    return megaRequest ? megaRequest->getNodeHandle() : ::mega::INVALID_HANDLE;
}

String^ MRequest::getLink()
{
    if (!megaRequest) return nullptr;

    std::string utf16link;
    const char *utf8link = megaRequest->getLink();
    MegaApi::utf8ToUtf16(utf8link, &utf16link);

    return utf8link ? ref new String((wchar_t *)utf16link.data()) : nullptr;
}

uint64 MRequest::getParentHandle()
{
    return megaRequest ? megaRequest->getParentHandle() : ::mega::INVALID_HANDLE;
}

String^ MRequest::getSessionKey()
{
    if (!megaRequest) return nullptr;

    std::string utf16session;
    const char *utf8session = megaRequest->getSessionKey();
    MegaApi::utf8ToUtf16(utf8session, &utf16session);
    
    return utf8session ? ref new String((wchar_t *)utf16session.data()) : nullptr;
}

String^ MRequest::getName()
{
    if (!megaRequest) return nullptr;

    std::string utf16name;
    const char *utf8name = megaRequest->getName();
    MegaApi::utf8ToUtf16(utf8name, &utf16name);

    return utf8name ? ref new String((wchar_t *)utf16name.data()) : nullptr;
}

String^ MRequest::getEmail()
{
    if (!megaRequest) return nullptr;

    std::string utf16email;
    const char *utf8email = megaRequest->getEmail();
    MegaApi::utf8ToUtf16(utf8email, &utf16email);

    return utf8email ? ref new String((wchar_t *)utf16email.data()) : nullptr;
}

String^ MRequest::getPassword()
{
    if (!megaRequest) return nullptr;

    std::string utf16password;
    const char *utf8password = megaRequest->getPassword();
    MegaApi::utf8ToUtf16(utf8password, &utf16password);

    return utf8password ? ref new String((wchar_t *)utf16password.data()) : nullptr;
}

String^ MRequest::getNewPassword()
{
    if (!megaRequest) return nullptr;

    std::string utf16password;
    const char *utf8password = megaRequest->getNewPassword();
    MegaApi::utf8ToUtf16(utf8password, &utf16password);

    return utf8password ? ref new String((wchar_t *)utf16password.data()) : nullptr;
}

String^ MRequest::getPrivateKey()
{
    if (!megaRequest) return nullptr;

    std::string utf16privateKey;
    const char *utf8privateKey = megaRequest->getPrivateKey();
    MegaApi::utf8ToUtf16(utf8privateKey, &utf16privateKey);

    return utf8privateKey ? ref new String((wchar_t *)utf16privateKey.data()) : nullptr;
}

int MRequest::getAccess()
{
    return megaRequest ? megaRequest->getAccess() : -1;
}

String^ MRequest::getFile()
{
    if (!megaRequest) return nullptr;

    std::string utf16file;
    const char *utf8file = megaRequest->getFile();
    MegaApi::utf8ToUtf16(utf8file, &utf16file);

    return utf8file ? ref new String((wchar_t *)utf16file.data()) : nullptr;
}

int MRequest::getNumRetry()
{
    return megaRequest ? megaRequest->getNumRetry() : 0;
}

MNode^ MRequest::getPublicMegaNode()
{
    if (megaRequest == nullptr) return nullptr;
    
    MegaNode *node = megaRequest->getPublicMegaNode();    
    
    return node ? ref new MNode(node, true) : nullptr;
}

int MRequest::getParamType()
{
    return megaRequest ? megaRequest->getParamType() : 0;
}

String^ MRequest::getText()
{
    if (!megaRequest) return nullptr;

    std::string utf16text;
    const char *utf8text = megaRequest->getText();
    MegaApi::utf8ToUtf16(utf8text, &utf16text);

    return utf8text ? ref new String((wchar_t *)utf16text.data()) : nullptr;
}

uint64 MRequest::getNumber()
{
    return megaRequest ? megaRequest->getNumber() : 0;
}

bool MRequest::getFlag()
{
    return megaRequest ? megaRequest->getFlag() : 0;
}

uint64 MRequest::getTransferredBytes()
{
    return megaRequest ? megaRequest->getTransferredBytes() : 0;
}

uint64 MRequest::getTotalBytes()
{
    return megaRequest ? megaRequest->getTotalBytes() : 0;
}

MAccountDetails^ MRequest::getMAccountDetails()
{
    return megaRequest ? ref new MAccountDetails(megaRequest->getMegaAccountDetails(), true) : nullptr;
}

int MRequest::getTransferTag()
{
    return megaRequest ? megaRequest->getTransferTag() : 0;
}

int MRequest::getNumDetails()
{
    return megaRequest ? megaRequest->getNumDetails() : 0;
}

int MRequest::getTag()
{
    return megaRequest ? megaRequest->getTag() : 0;
}

MPricing^ MRequest::getPricing()
{
    return megaRequest ? ref new MPricing(megaRequest->getPricing(), true) : nullptr;
}

MAchievementsDetails^ MRequest::getMAchievementsDetails()
{
    return megaRequest ? ref new MAchievementsDetails(megaRequest->getMegaAchievementsDetails(), true) : nullptr;
}
