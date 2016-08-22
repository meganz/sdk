/**
* @file MUser.cpp
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

#include "MUser.h"

using namespace mega;
using namespace Platform;

MUser::MUser(MegaUser *megaUser, bool cMemoryOwn)
{
	this->megaUser = megaUser;
	this->cMemoryOwn = cMemoryOwn;
}

MUser::~MUser()
{
	if (cMemoryOwn) 
		delete megaUser;
}

MegaUser *MUser::getCPtr()
{
	return megaUser;
}

String^ MUser::getEmail()
{
	if (!megaUser) return nullptr;

	std::string utf16email;
	const char *utf8email = megaUser->getEmail();
	MegaApi::utf8ToUtf16(utf8email, &utf16email);

	return ref new String((wchar_t *)utf16email.data());
}

uint64 MUser::getHandle()
{
    return megaUser ? megaUser->getHandle() : ::mega::INVALID_HANDLE;
}

MUserVisibility MUser::getVisibility()
{
	return (MUserVisibility) (megaUser ? megaUser->getVisibility() : ::mega::MegaUser::VISIBILITY_UNKNOWN);
}

uint64 MUser::getTimestamp()
{
	return megaUser ? megaUser->getTimestamp() : 0;
}

bool MUser::hasChanged(int changeType)
{
    return megaUser ? megaUser->hasChanged(changeType) : false;
}

int MUser::getChanges()
{
    return megaUser ? megaUser->getChanges() : 0;
}

int MUser::isOwnChange()
{
    return megaUser ? megaUser->isOwnChange() : 0;
}
