/**
* @file MAccountPurchase.cpp
* @brief Get details about a MEGA purchase.
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

#include "MAccountPurchase.h"

using namespace mega;
using namespace Platform;

MAccountPurchase::MAccountPurchase(MegaAccountPurchase *accountPurchase, bool cMemoryOwn)
{
    this->accountPurchase = accountPurchase;
    this->cMemoryOwn;
}

MAccountPurchase::~MAccountPurchase()
{
    if (cMemoryOwn)
        delete accountPurchase;
}

MegaAccountPurchase* MAccountPurchase::getCPtr()
{
    return accountPurchase;
}

int64 MAccountPurchase::getTimestamp()
{
    return accountPurchase ? accountPurchase->getTimestamp() : 0;
}

String^ MAccountPurchase::getHandle()
{
    if (!accountPurchase) return nullptr;

    std::string utf16handle;
    const char *utf8handle = accountPurchase->getHandle();
    if (!utf8handle)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8handle, &utf16handle);
    delete[] utf8handle;

    return ref new String((wchar_t *)utf16handle.data());
}

String^ MAccountPurchase::getCurrency()
{
    if (!accountPurchase) return nullptr;

    std::string utf16currency;
    const char *utf8currency = accountPurchase->getCurrency();
    if (!utf8currency)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8currency, &utf16currency);
    delete[] utf8currency;

    return ref new String((wchar_t *)utf16currency.data());
}

double MAccountPurchase::getAmount()
{
    return accountPurchase ? accountPurchase->getAmount() : 0;
}

int MAccountPurchase::getMethod()
{
    return accountPurchase ? accountPurchase->getMethod() : 0;
}
