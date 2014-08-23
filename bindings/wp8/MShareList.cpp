#include "MShareList.h"

using namespace mega;
using namespace Platform;

MShareList::MShareList(ShareList *shareList, bool cMemoryOwn)
{
	this->shareList = shareList;
	this->cMemoryOwn = cMemoryOwn;
}

MShareList::~MShareList()
{
	if (cMemoryOwn)
		delete shareList;
}

MShare^ MShareList::get(int i)
{
	return shareList ? ref new MShare(shareList->get(i)->copy(), true) : nullptr;
}

int MShareList::size()
{
	return shareList ? shareList->size() : 0;
}
