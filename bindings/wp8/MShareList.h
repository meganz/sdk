#pragma once

#include "MShare.h"

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public ref class MShareList sealed
	{
		friend ref class MegaSDK;

	public:
		virtual ~MShareList();
		MShare^ get(int i);
		int size();

	private:
		MShareList(MegaShareList *shareList, bool cMemoryOwn);
		MegaShareList *shareList;
		bool cMemoryOwn;
	};
}
