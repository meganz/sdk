#pragma once

#include "MUser.h"

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public ref class MUserList sealed
	{
		friend ref class MegaSDK;
		friend class DelegateMListener;
		friend class DelegateMGlobalListener;

	public:
		virtual ~MUserList();
		MUser^ get(int i);
		int size();

	private:
		MUserList(MegaUserList *userList, bool cMemoryOwn);
		MegaUserList *userList;
		bool cMemoryOwn;
	};
}
