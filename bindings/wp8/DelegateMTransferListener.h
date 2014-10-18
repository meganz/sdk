#pragma once

#include "MegaSDK.h"
#include "MTransferListenerInterface.h"
#include "MTransfer.h"
#include "MError.h"

#include "megaapi.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	private class DelegateMTransferListener : public MegaTransferListener
	{
	public:
		DelegateMTransferListener(MegaSDK^ megaSDK, MTransferListenerInterface^ listener, bool singleListener = true);
		MTransferListenerInterface^ getUserListener();

		void onTransferStart(MegaApi* api, MegaTransfer* transfer);
		void onTransferFinish(MegaApi* api, MegaTransfer* transfer, MegaError* e);
		void onTransferUpdate(MegaApi* api, MegaTransfer* transfer);
		void onTransferTemporaryError(MegaApi* api, MegaTransfer* transfer, MegaError* e);

		bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size);

	private:
		MegaSDK^ megaSDK;
		MTransferListenerInterface^ listener;
		bool singleListener;
	};
}
