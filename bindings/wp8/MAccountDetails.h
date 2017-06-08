/**
* @file MAccountDetails.h
* @brief Get details about a MEGA account.
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

#pragma once

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public enum class MAccountType
	{
		ACCOUNT_TYPE_FREE = 0,
		ACCOUNT_TYPE_PROI = 1,
		ACCOUNT_TYPE_PROII = 2,
		ACCOUNT_TYPE_PROIII = 3,
		ACCOUNT_TYPE_LITE = 4
	};

	public enum class MSubscriptionStatus
	{
		SUBSCRIPTION_STATUS_NONE = 0,
		SUBSCRIPTION_STATUS_VALID = 1,
		SUBSCRIPTION_STATUS_INVALID = 2
	};

	public ref class MAccountDetails sealed
	{
		friend ref class MRequest;
        friend ref class MAccountBalance;
        friend ref class MAccountSession;
        friend ref class MAccountPurchase;
        friend ref class MAccountTransaction;

	public:
		virtual ~MAccountDetails();
		MAccountType getProLevel();
		int64 getProExpiration();
		MSubscriptionStatus getSubscriptionStatus();
		int64 getSubscriptionRenewTime();
		String^ getSubscriptionMethod();
		String^ getSubscriptionCycle();

		uint64 getStorageMax();
		uint64 getStorageUsed();
		uint64 getTransferMax();
		uint64 getTransferOwnUsed();
		
		int getNumUsageItems();
		uint64 getStorageUsed(uint64 handle);
		uint64 getNumFiles(uint64 handle);
		uint64 getNumFolders(uint64 handle);

        MAccountDetails^ copy();

        int getNumBalances();
        MAccountBalance^ getBalance(int i);

        int getNumSessions();
        MAccountSession^ getSession(int i);

        int getNumPurchases();
        MAccountPurchase^ getPurchase(int i);

        int getNumTransactions();
        MAccountTransaction^ getTransaction(int i);

        int getTemporalBandwidthInterval();
        uint64 getTemporalBandwidth();

        bool isTemporalBandwidthValid();

	private:
		MAccountDetails(MegaAccountDetails *accountDetails, bool cMemoryOwn);
		MegaAccountDetails *accountDetails;
		bool cMemoryOwn;
		MegaAccountDetails *getCPtr();
	};
}
