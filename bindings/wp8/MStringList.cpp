/**
* @file MStringList.cpp
* @brief List of String objects
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

#include "MStringList.h"

using namespace mega;
using namespace Platform;

MStringList::MStringList(MegaStringList *stringList, bool cMemoryOwn)
{
    this->stringList = stringList;
    this->cMemoryOwn = cMemoryOwn;
}

MStringList::~MStringList()
{
    if (cMemoryOwn)
        delete stringList;
}

String^ MStringList::get(int i)
{
    std::string utf16string;
    const char *utf8string = stringList->get(i);
    if (!utf8string)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8string, &utf16string);
    delete[] utf8string;
    
    return stringList ? ref new String((wchar_t *)utf16string.data()) : nullptr;
}

int MStringList::size()
{
    return stringList ? stringList->size() : 0;
}
