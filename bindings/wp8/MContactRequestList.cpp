/**
* @file MContactRequestList.h
* @brief List of MContactRequest objects
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

#include "MContactRequestList.h"

using namespace mega;
using namespace Platform;

MContactRequestList::MContactRequestList(MegaContactRequestList *contactRequestList, bool cMemoryOwn)
{
    this->contactRequestList = contactRequestList;
    this->cMemoryOwn = cMemoryOwn;
}

MContactRequestList::~MContactRequestList()
{
    if (cMemoryOwn)
        delete contactRequestList;
}

MContactRequest^ MContactRequestList::get(int i)
{
    return contactRequestList ? ref new MContactRequest(contactRequestList->get(i)->copy(), true) : nullptr;
}

int MContactRequestList::size()
{
    return contactRequestList ? contactRequestList->size() : 0;
}
