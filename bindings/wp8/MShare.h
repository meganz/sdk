#pragma once

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public enum class MShareType
	{
		ACCESS_UNKNOWN = -1,
		ACCESS_READ = 0,
		ACCESS_READWRITE,
		ACCESS_FULL,
		ACCESS_OWNER
	};

	public ref class MShare sealed
	{
		friend ref class MegaSDK;
		friend ref class MShareList;

	public:
		virtual ~MShare();
		String^ getUser();
		uint64 getNodeHandle();
		int getAccess();
		uint64 getTimestamp();

	private:
		MShare(MegaShare *megaShare, bool cMemoryOwn);
		MegaShare *megaShare;
		MegaShare *getCPtr();
		bool cMemoryOwn;
	};
}
