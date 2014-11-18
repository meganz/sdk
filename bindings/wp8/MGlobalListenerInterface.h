#pragma once

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MGlobalListenerInterface
	{
	public:
		void onUsersUpdate(MegaSDK^ api, MUserList ^users);
		void onNodesUpdate(MegaSDK^ api, MNodeList^ nodes);
		void onReloadNeeded(MegaSDK^ api);
	};
}
