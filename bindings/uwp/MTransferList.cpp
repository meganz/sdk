/**
* @file MTransferList.cpp
* @brief List of MTransfer objects
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
