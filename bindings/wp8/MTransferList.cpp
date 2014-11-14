#include "MTransferList.h"

using namespace mega;
using namespace Platform;

MTransferList::MTransferList(MegaTransferList *transferList, bool cMemoryOwn)
{
	this->transferList = transferList;
	this->cMemoryOwn = cMemoryOwn;
}

MTransferList::~MTransferList()
{
	if (cMemoryOwn)
		delete transferList;
}

MTransfer^ MTransferList::get(int i)
{
	return transferList ? ref new MTransfer(transferList->get(i)->copy(), true) : nullptr;
}

int MTransferList::size()
{
	return transferList ? transferList->size() : 0;
}
