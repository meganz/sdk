#pragma once

#include "MTransfer.h"
#include "MRequest.h"
#include "MError.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MListenerInterface
	{
	public:
		void onRequestStart(MegaSDK^ api, MRequest^ request);
		void onRequestFinish(MegaSDK^ api, MRequest^ request, MError^ e);
		void onRequestUpdate(MegaSDK^ api, MRequest^ request);
		void onRequestTemporaryError(MegaSDK^ api, MRequest^ request, MError^ e);
		void onTransferStart(MegaSDK^ api, MTransfer^ transfer);
		void onTransferFinish(MegaSDK^ api, MTransfer^ transfer, MError^ e);
		void onTransferUpdate(MegaSDK^ api, MTransfer^ transfer);
		void onTransferTemporaryError(MegaSDK^ api, MTransfer^ transfer, MError^ e);
		void onUsersUpdate(MegaSDK^ api);
		void onNodesUpdate(MegaSDK^ api);
		void onReloadNeeded(MegaSDK^ api);
	};
}
