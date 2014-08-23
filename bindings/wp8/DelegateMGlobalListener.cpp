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

void DelegateMGlobalListener::onUsersUpdate(MegaApi* api)
{
	if (listener != nullptr)
		listener->onUsersUpdate(megaSDK);
}

void DelegateMGlobalListener::onNodesUpdate(MegaApi* api)
{
	if (listener != nullptr)
		listener->onNodesUpdate(megaSDK);
}

void DelegateMGlobalListener::onReloadNeeded(MegaApi* api)
{
	if (listener != nullptr)
		listener->onReloadNeeded(megaSDK);
}
