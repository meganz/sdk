#pragma once

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public enum class MUserVisibility
	{
		VISIBILITY_UNKNOWN = -1,
		VISIBILITY_HIDDEN = 0,
		VISIBILITY_VISIBLE,
		VISIBILITY_ME
	};

	public ref class MUser sealed
	{
		friend ref class MegaSDK;
		friend ref class MUserList;

	public:
		virtual ~MUser();
		String^ getEmail();
		MUserVisibility getVisibility();
		uint64 getTimestamp();

	private:
		MUser(MegaUser *megaUser, bool cMemoryOwn);
		MegaUser *megaUser;
		bool cMemoryOwn;
		MegaUser *getCPtr();
	};
}
