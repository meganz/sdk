#include "MAccountDetails.h"

using namespace mega;
using namespace Platform;

MAccountDetails::MAccountDetails(MegaAccountDetails *accountDetails, bool cMemoryOwn)
{
	this->accountDetails = accountDetails;
	this->cMemoryOwn;
}

MAccountDetails::~MAccountDetails()
{
	if (cMemoryOwn)
		delete accountDetails;
}

MegaAccountDetails* MAccountDetails::getCPtr()
{
	return accountDetails;
}

uint64 MAccountDetails::getUsedStorage() 
{
	return accountDetails ? accountDetails->getStorageUsed() : 0;
}

uint64 MAccountDetails::getMaxStorage() 
{
	return accountDetails ? accountDetails->getStorageMax() : 0;
}

uint64 MAccountDetails::getOwnUsedTransfer() 
{
	return accountDetails ? accountDetails->getTransferOwnUsed() : 0;
}

uint64 MAccountDetails::getMaxTransfer()
{
	return accountDetails ? accountDetails->getTransferMax() : 0;
}

MAccountType MAccountDetails::getProLevel()
{
	return (MAccountType) (accountDetails ? accountDetails->getProLevel() : 0);
}


