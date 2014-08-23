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

MUserVisibility MUser::getVisibility()
{
	return (MUserVisibility) (megaUser ? megaUser->getVisibility() : ::mega::MegaUser::VISIBILITY_UNKNOWN);
}

uint64 MUser::getTimestamp()
{
	return megaUser ? megaUser->getTimestamp() : 0;
}
