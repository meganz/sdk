#pragma once

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MGlobalListenerInterface
	{
	public:
		void onUsersUpdate(MegaSDK^ api);
		void onNodesUpdate(MegaSDK^ api);
		void onReloadNeeded(MegaSDK^ api);
	};
}
