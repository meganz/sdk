#pragma once

#include "MegaSDK.h"
#include "MRequestListenerInterface.h"
#include "MRequest.h"
#include "MError.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	private class DelegateMRequestListener : public MegaRequestListener
	{
	public:
		DelegateMRequestListener(MegaSDK^ megaSDK, MRequestListenerInterface^ listener, bool singleListener = true);
		MRequestListenerInterface^ getUserListener();

		void onRequestStart(MegaApi* api, MegaRequest* request);
		void onRequestFinish(MegaApi* api, MegaRequest* request, MegaError* e);
		void onRequestUpdate(MegaApi* api, MegaRequest* request);
		void onRequestTemporaryError(MegaApi* api, MegaRequest* request, MegaError* e);

	private:
		MegaSDK^ megaSDK;
		MRequestListenerInterface^ listener;
		bool singleListener;
	};
}
