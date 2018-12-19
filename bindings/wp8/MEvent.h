/**
* @file MError.h
* @brief Provides information about an event.
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

#pragma once

#include "megaapi.h"

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MEventType
    {
        EVENT_COMMIT_DB             = 0,
        EVENT_ACCOUNT_CONFIRMATION  = 1,
        EVENT_CHANGE_TO_HTTPS       = 2,
        EVENT_DISCONNECT            = 3,
        EVENT_ACCOUNT_BLOCKED       = 4,
        EVENT_STORAGE               = 5,
        EVENT_NODES_CURRENT         = 6
    };

    public ref class MEvent sealed
    {
        friend ref class MegaSDK;
        friend class DelegateMListener;
        friend class DelegateMGlobalListener;

    public:
        virtual ~MEvent();
        MEvent^ copy();
        MEventType getType();
        String^ getText();
        int getNumber();

    private:
        MEvent(MegaEvent *megaEvent, bool cMemoryOwn);
        MegaEvent *megaEvent;
        MegaEvent *getCPtr();
        bool cMemoryOwn;
    };
}
