/**
* @file MAccountTransaction.cpp
* @brief Get details about a MEGA transaction.
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

#include "MAccountTransaction.h"

using namespace mega;
using namespace Platform;

MAccountTransaction::MAccountTransaction(MegaAccountTransaction *accountTransaction, bool cMemoryOwn)
{
    this->accountTransaction = accountTransaction;
    this->cMemoryOwn;
}

MAccountTransaction::~MAccountTransaction()
{
    if (cMemoryOwn)
        delete accountTransaction;
}

MegaAccountTransaction* MAccountTransaction::getCPtr()
{
    return accountTransaction;
}

int64 MAccountTransaction::getTimestamp()
{
    return accountTransaction ? accountTransaction->getTimestamp() : 0;
}

String^ MAccountTransaction::getHandle()
{
    if (!accountTransaction) return nullptr;

    std::string utf16handle;
    const char *utf8handle = accountTransaction->getHandle();
    if (!utf8handle)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8handle, &utf16handle);
    delete[] utf8handle;

    return ref new String((wchar_t *)utf16handle.data());
}

String^ MAccountTransaction::getCurrency()
{
    if (!accountTransaction) return nullptr;

    std::string utf16currency;
    const char *utf8currency = accountTransaction->getCurrency();
    if (!utf8currency)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8currency, &utf16currency);
    delete[] utf8currency;

    return ref new String((wchar_t *)utf16currency.data());
}

double MAccountTransaction::getAmount()
{
    return accountTransaction ? accountTransaction->getAmount() : 0;
}