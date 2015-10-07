/**
* @file MShare.h
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

#pragma once

#include "megaapi.h"

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MShareType
    {
        ACCESS_UNKNOWN = -1,
        ACCESS_READ = 0,
        ACCESS_READWRITE = 1,
        ACCESS_FULL = 2,
        ACCESS_OWNER = 3
    };

    public ref class MShare sealed
    {
        friend ref class MegaSDK;
        friend ref class MShareList;

    public:
        virtual ~MShare();
        String^ getUser();
        uint64 getNodeHandle();
        int getAccess();
        uint64 getTimestamp();

    private:
        MShare(MegaShare *megaShare, bool cMemoryOwn);
        MegaShare *megaShare;
        MegaShare *getCPtr();
        bool cMemoryOwn;
    };
}
