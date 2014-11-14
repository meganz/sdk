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
	return pricing ? (MAccountType)pricing->getProLevel(productIndex) : MAccountType::FREE;
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
