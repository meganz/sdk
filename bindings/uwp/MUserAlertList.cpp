/**
* @file MUserAlertList.cpp
* @brief List of MUserAlert objects.
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

#include "MUserAlertList.h"

using namespace mega;
using namespace Platform;

MUserAlertList::MUserAlertList(MegaUserAlertList *userAlertList, bool cMemoryOwn)
{
    this->userAlertList = userAlertList;
    this->cMemoryOwn = cMemoryOwn;
}

MUserAlertList::~MUserAlertList()
{
    if (cMemoryOwn)
        delete userAlertList;
}

MUserAlertList^ MUserAlertList::copy()
{
    return userAlertList ? ref new MUserAlertList(userAlertList->copy(), true) : nullptr;
}

MUserAlert^ MUserAlertList::get(int i)
{
    return userAlertList ? ref new MUserAlert(userAlertList->get(i)->copy(), true) : nullptr;
}

int MUserAlertList::size()
{
    return userAlertList ? userAlertList->size() : 0;
}
