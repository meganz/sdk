/**
* @file MPricing.cpp
* @brief Details about pricing plans
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

#include "MPricing.h"

using namespace mega;
using namespace Platform;

MPricing::MPricing(MegaPricing *pricing, bool cMemoryOwn)
{
	this->pricing = pricing;
	this->cMemoryOwn;
}

MPricing::~MPricing()
{
	if (cMemoryOwn)
		delete pricing;
}

int MPricing::getNumProducts()
{
	return pricing ? pricing->getNumProducts() : 0;
}

MegaHandle MPricing::getHandle(int productIndex)
{
	return pricing ? pricing->getHandle(productIndex) : INVALID_HANDLE;
}

MAccountType MPricing::getProLevel(int productIndex)
{
	return pricing ? (MAccountType)pricing->getProLevel(productIndex) : MAccountType::ACCOUNT_TYPE_FREE;
}

int MPricing::getGBStorage(int productIndex)
{
	return pricing ? pricing->getGBStorage(productIndex) : 0;
}

int MPricing::getGBTransfer(int productIndex)
{
	return pricing ? pricing->getGBTransfer(productIndex) : 0;
}

int MPricing::getMonths(int productIndex)
{
	return pricing ? pricing->getMonths(productIndex) : 0;
}

int MPricing::getAmount(int productIndex)
{
	return pricing ? pricing->getAmount(productIndex) : 0;
}

String^ MPricing::getCurrency(int productIndex)
{
	if (!pricing) return nullptr;

	std::string utf16currency;
	const char *utf8currency = pricing->getCurrency(productIndex);
	MegaApi::utf8ToUtf16(utf8currency, &utf16currency);

	return utf8currency ? ref new String((wchar_t *)utf16currency.data()) : nullptr;
}

String^ MPricing::getDescription(int productIndex)
{
	if (!pricing) return nullptr;

	std::string utf16description;
	const char *utf8description = pricing->getDescription(productIndex);
	MegaApi::utf8ToUtf16(utf8description, &utf16description);

	return utf8description ? ref new String((wchar_t *)utf16description.data()) : nullptr;
}

MPricing^ MPricing::copy()
{
	return pricing ? ref new MPricing(pricing->copy(), true) : nullptr;
}
