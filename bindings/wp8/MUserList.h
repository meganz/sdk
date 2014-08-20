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

	public:
		virtual ~MUserList();
		MUser^ get(int i);
		int size();

	private:
		MUserList(UserList *userList, bool cMemoryOwn);
		UserList *userList;
		UserList *getCPtr();
		bool cMemoryOwn;
	};
}
