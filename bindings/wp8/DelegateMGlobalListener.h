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

		void onUsersUpdate(MegaApi* api, MegaUserList *users);
		void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
		void onReloadNeeded(MegaApi* api);

	private:
		MegaSDK^ megaSDK;
		MGlobalListenerInterface^ listener;
	};
}
