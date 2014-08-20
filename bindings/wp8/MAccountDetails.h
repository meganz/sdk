#pragma once

#include "megaapi.h"

namespace mega
{
	public enum class MAccountType
	{
		FREE = 0,
		PROI = 1,
		PROII = 2,
		PROIII = 3
	};

	public ref class MAccountDetails sealed
	{
	public:
		virtual ~MAccountDetails();
		uint64 getUsedStorage();
		uint64 getMaxStorage();
		uint64 getOwnUsedTransfer();
		uint64 getMaxTransfer();
		MAccountType getProLevel();

	private:
		MAccountDetails(MegaAccountDetails *accountDetails, bool cMemoryOwn);
		MegaAccountDetails *accountDetails;
		bool cMemoryOwn;
		MegaAccountDetails *getCPtr();
	};
}
