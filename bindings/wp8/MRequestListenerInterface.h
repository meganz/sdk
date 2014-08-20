#pragma once

#include "MRequest.h"
#include "MError.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MRequestListenerInterface
	{
	public:
		void onRequestStart(MegaSDK^ api, MRequest^ request);
		void onRequestFinish(MegaSDK^ api, MRequest^ request, MError^ e);
		void onRequestUpdate(MegaSDK^ api, MRequest^ request);
		void onRequestTemporaryError(MegaSDK^ api, MRequest^ request, MError^ e);
	};
}
