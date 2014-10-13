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
		friend ref class MRequest;

	public:
		virtual ~MAccountDetails();
		uint64 getStorageUsed();
		uint64 getStorageMax();
		uint64 getTransferOwnUsed();
		uint64 getTransferMax();
		MAccountType getProLevel();
		uint64 getStorageUsed(uint64 handle);
		uint64 getNumFiles(uint64 handle);
		uint64 getNumFolders(uint64 handle);

	private:
		MAccountDetails(MegaAccountDetails *accountDetails, bool cMemoryOwn);
		MegaAccountDetails *accountDetails;
		bool cMemoryOwn;
		MegaAccountDetails *getCPtr();
	};
}
