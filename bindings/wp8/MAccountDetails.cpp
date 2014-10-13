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

uint64 MAccountDetails::getStorageUsed() 
{
	return accountDetails ? accountDetails->getStorageUsed() : 0;
}

uint64 MAccountDetails::getStorageMax() 
{
	return accountDetails ? accountDetails->getStorageMax() : 0;
}

uint64 MAccountDetails::getTransferOwnUsed()
{
	return accountDetails ? accountDetails->getTransferOwnUsed() : 0;
}

uint64 MAccountDetails::getTransferMax()
{
	return accountDetails ? accountDetails->getTransferMax() : 0;
}

MAccountType MAccountDetails::getProLevel()
{
	return (MAccountType) (accountDetails ? accountDetails->getProLevel() : 0);
}

uint64 MAccountDetails::getStorageUsed(uint64 handle)
{
	return accountDetails ? accountDetails->getStorageUsed(handle) : 0;
}

uint64 MAccountDetails::getNumFiles(uint64 handle)
{
	return accountDetails ? accountDetails->getNumFiles(handle) : 0;
}

uint64 MAccountDetails::getNumFolders(uint64 handle)
{
	return accountDetails ? accountDetails->getNumFolders(handle) : 0;
}
