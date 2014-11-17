#include "DelegateMGlobalListener.h"

using namespace mega;
using namespace Platform;

DelegateMGlobalListener::DelegateMGlobalListener(MegaSDK^ megaSDK, MGlobalListenerInterface^ listener)
{
	this->megaSDK = megaSDK;
	this->listener = listener;
}

MGlobalListenerInterface^ DelegateMGlobalListener::getUserListener()
{
	return listener;
}

void DelegateMGlobalListener::onUsersUpdate(MegaApi* api, MegaUserList *users)
{
	if (listener != nullptr)
		listener->onUsersUpdate(megaSDK, users ? ref new MUserList(users->copy(), true) : nullptr);
}

void DelegateMGlobalListener::onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
{
	if (listener != nullptr)
		listener->onNodesUpdate(megaSDK, nodes ? ref new MNodeList(nodes->copy(), true) : nullptr);
}

void DelegateMGlobalListener::onReloadNeeded(MegaApi* api)
{
	if (listener != nullptr)
		listener->onReloadNeeded(megaSDK);
}
