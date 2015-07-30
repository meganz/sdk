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

#pragma once

#include "megaapi.h"

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MContactRequestStatusType
    {
        STATUS_UNRESOLVED = 0,
        STATUS_ACCEPTED,
        STATUS_DENIED,
        STATUS_IGNORED,
        STATUS_DELETED,
        STATUS_REMINDED
    };

    public enum class MContactRequestReplyActionType
    {
        REPLY_ACTION_ACCEPT = 0,
        REPLY_ACTION_DENY,
        REPLY_ACTION_IGNORE
    };

    public enum class MContactRequestInviteActionType
    {
        INVITE_ACTION_ADD = 0,
        INVITE_ACTION_DELETE,
        INVITE_ACTION_REMIND
    };

    public ref class MContactRequest sealed
    {
        friend ref class MegaSDK;
        friend ref class MContactRequestList;
        
    public:        
        virtual ~MContactRequest();
        MegaHandle getHandle();
        String^ getSourceEmail();
        String^ getSourceMessage();
        String^ getTargetEmail();
        int64_t getCreationTime();
        int64_t getModificationTime();
        int getStatus();
        bool isOutgoing();

    private:
        MContactRequest(MegaContactRequest *megaContactRequest, bool cMemoryOwn);
        MegaContactRequest *megaContactRequest;
        MegaContactRequest *getCPtr();
        bool cMemoryOwn;
    };
}

