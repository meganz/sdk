#include "MUserList.h"

using namespace mega;
using namespace Platform;

MUserList::MUserList(MegaUserList *userList, bool cMemoryOwn)
{
	this->userList = userList;
	this->cMemoryOwn = cMemoryOwn;
}

MUserList::~MUserList()
{
	if (cMemoryOwn)
		delete userList;
}

MUser^ MUserList::get(int i)
{
	return userList ? ref new MUser(userList->get(i)->copy(), true) : nullptr;
}

int MUserList::size()
{
	return userList ? userList->size() : 0;
}

