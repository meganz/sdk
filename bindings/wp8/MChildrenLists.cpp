/**
* @file MChildrenLists.cpp
* @brief Lists of file and folder children MegaNode objects.
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

#include "MChildrenLists.h"

using namespace mega;
using namespace Platform;

MChildrenLists::MChildrenLists(MegaChildrenLists *childrenLists, bool cMemoryOwn)
{
    this->childrenLists = childrenLists;
    this->cMemoryOwn = cMemoryOwn;
}

MChildrenLists::~MChildrenLists()
{
    if (cMemoryOwn)
        delete childrenLists;
}

MChildrenLists^ MChildrenLists::copy()
{
    return childrenLists ? ref new MChildrenLists(childrenLists->copy(), true) : nullptr;
}

MNodeList^ MChildrenLists::getFolderList()
{
    return childrenLists ? ref new MNodeList(childrenLists->getFolderList()->copy(), true) : nullptr;
}

MNodeList^ MChildrenLists::getFileList()
{
    return childrenLists ? ref new MNodeList(childrenLists->getFileList()->copy(), true) : nullptr;
}
