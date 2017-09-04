/**
* @file MUser.h
* @brief Represents an user in MEGA
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

	public enum class MUserVisibility
	{
        VISIBILITY_UNKNOWN  = -1,
        VISIBILITY_HIDDEN   = 0,
        VISIBILITY_VISIBLE  = 1,
        VISIBILITY_INACTIVE = 2,
        VISIBILITY_BLOCKED  = 3
	};

    public enum class MUserChangeType
    {
        CHANGE_TYPE_AUTHRING            = 0x01,
        CHANGE_TYPE_LSTINT              = 0x02,
        CHANGE_TYPE_AVATAR              = 0x04,
        CHANGE_TYPE_FIRSTNAME           = 0x08,
        CHANGE_TYPE_LASTNAME            = 0x10,
        CHANGE_TYPE_EMAIL               = 0x20,
        CHANGE_TYPE_KEYRING             = 0x40,
        CHANGE_TYPE_COUNTRY             = 0x80,
        CHANGE_TYPE_BIRTHDAY            = 0x100,
        CHANGE_TYPE_PUBKEY_CU255        = 0x200,
        CHANGE_TYPE_PUBKEY_ED255        = 0x400,
        CHANGE_TYPE_SIG_PUBKEY_RSA      = 0x800,
        CHANGE_TYPE_SIG_PUBKEY_CU255    = 0x1000,
        CHANGE_TYPE_LANGUAGE            = 0x2000,
        CHANGE_TYPE_PWD_REMINDER        = 0x4000
    };

	public ref class MUser sealed
	{
		friend ref class MegaSDK;
		friend ref class MUserList;

	public:
		virtual ~MUser();
		String^ getEmail();
        uint64 getHandle();
		MUserVisibility getVisibility();
		uint64 getTimestamp();
        bool hasChanged(int changeType);
        int getChanges();
        int isOwnChange();

	private:
		MUser(MegaUser *megaUser, bool cMemoryOwn);
		MegaUser *megaUser;
		bool cMemoryOwn;
		MegaUser *getCPtr();
	};
}
