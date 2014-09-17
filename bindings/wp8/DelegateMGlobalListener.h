#pragma once

#include "MegaSDK.h"
#include "MGlobalListenerInterface.h"

#include "megaapi.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	private class DelegateMGlobalListener : public MegaGlobalListener
	{
	public:
		DelegateMGlobalListener(MegaSDK^ megaSDK, MGlobalListenerInterface^ listener);
		MGlobalListenerInterface^ getUserListener();

		void onUsersUpdate(MegaApi* api);
		void onNodesUpdate(MegaApi* api);
		void onReloadNeeded(MegaApi* api);

	private:
		MegaSDK^ megaSDK;
		MGlobalListenerInterface^ listener;
	};
}
