#pragma once

#include "MTransfer.h"

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public ref class MTransferList sealed
	{
		friend ref class MegaSDK;

	public:
		virtual ~MTransferList();
		MTransfer^ get(int i);
		int size();

	private:
		MTransferList(MegaTransferList *transferList, bool cMemoryOwn);
		MegaTransferList *transferList;
		bool cMemoryOwn;
	};
}
