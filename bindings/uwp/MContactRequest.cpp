/**
* @file MContactRequest.h
* @brief Represents a contact request with an user in MEGA
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

#include "MContactRequest.h"

using namespace mega;
using namespace Platform;

MContactRequest::MContactRequest(MegaContactRequest *megaContactRequest, bool cMemoryOwn)
{
    this->megaContactRequest = megaContactRequest;
    this->cMemoryOwn = cMemoryOwn;
}

MContactRequest::~MContactRequest()
{
    if (cMemoryOwn)
        delete megaContactRequest;
}

MegaContactRequest * MContactRequest::getCPtr()
{
    return megaContactRequest;
}

MegaHandle MContactRequest::getHandle()
{
    return megaContactRequest ? megaContactRequest->getHandle() : ::mega::INVALID_HANDLE;
}

String^ MContactRequest::getSourceEmail()
{
    if (!megaContactRequest) return nullptr;

    std::string utf16sourceEmail;
    const char *utf8sourceEmail = megaContactRequest->getSourceEmail();
    MegaApi::utf8ToUtf16(utf8sourceEmail, &utf16sourceEmail);

    return utf8sourceEmail ? ref new String((wchar_t *)utf16sourceEmail.data()) : nullptr;
}

String^ MContactRequest::getSourceMessage()
{
    if (!megaContactRequest) return nullptr;

    std::string utf16sourceMessage;
    const char *utf8sourceMessage = megaContactRequest->getSourceMessage();
    MegaApi::utf8ToUtf16(utf8sourceMessage, &utf16sourceMessage);

    return utf8sourceMessage ? ref new String((wchar_t *)utf16sourceMessage.data()) : nullptr;
}

String^ MContactRequest::getTargetEmail()
{
    if (!megaContactRequest) return nullptr;

    std::string utf16targetEmail;
    const char *utf8targetEmail = megaContactRequest->getTargetEmail();
    MegaApi::utf8ToUtf16(utf8targetEmail, &utf16targetEmail);

    return utf8targetEmail ? ref new String((wchar_t *)utf16targetEmail.data()) : nullptr;
}

int64_t MContactRequest::getCreationTime()
{
    return megaContactRequest ? megaContactRequest->getCreationTime() : 0;
}

int64_t MContactRequest::getModificationTime()
{
    return megaContactRequest ? megaContactRequest->getModificationTime() : 0;
}

int MContactRequest::getStatus()
{
    return megaContactRequest ? megaContactRequest->getStatus() : MegaContactRequest::STATUS_UNRESOLVED;
}

bool MContactRequest::isOutgoing()
{
    return megaContactRequest ? megaContactRequest->isOutgoing() : false;
}

