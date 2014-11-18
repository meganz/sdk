#pragma once

#include "MegaSDK.h"
#include "MTransfer.h"
#include "MRequest.h"
#include "MError.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	private class DelegateMListener : public MegaListener
	{
	public:
		DelegateMListener(MegaSDK^ megaSDK, MListenerInterface^ listener);
		MListenerInterface^ getUserListener();

		void onRequestStart(MegaApi* api, MegaRequest* request);
		void onRequestFinish(MegaApi* api, MegaRequest* request, MegaError* e);
		void onRequestUpdate(MegaApi* api, MegaRequest* request);
		void onRequestTemporaryError(MegaApi* api, MegaRequest* request, MegaError* e);
		void onTransferStart(MegaApi* api, MegaTransfer* transfer);
		void onTransferFinish(MegaApi* api, MegaTransfer* transfer, MegaError* e);
		void onTransferUpdate(MegaApi* api, MegaTransfer* transfer);
		void onTransferTemporaryError(MegaApi* api, MegaTransfer* transfer, MegaError* e);
		void onUsersUpdate(MegaApi* api, MegaUserList *users);
		void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
		void onReloadNeeded(MegaApi* api);

	private:
		MegaSDK^ megaSDK;
		MListenerInterface^ listener;
	};
}
