#include "DelegateMRequestListener.h"

using namespace mega;
using namespace Platform;

DelegateMRequestListener::DelegateMRequestListener(MegaSDK^ megaSDK, MRequestListenerInterface^ listener, bool singleListener)
{
	this->megaSDK = megaSDK;
	this->listener = listener;
	this->singleListener = singleListener;
}

MRequestListenerInterface^ DelegateMRequestListener::getUserListener()
{
	return listener;
}

void DelegateMRequestListener::onRequestStart(MegaApi* api, MegaRequest* request)
{
	if (listener != nullptr) 
		listener->onRequestStart(megaSDK, ref new MRequest(request->copy(), true));
}

void DelegateMRequestListener::onRequestFinish(MegaApi* api, MegaRequest* request, MegaError* e)
{
	if (listener != nullptr)
	{
		listener->onRequestFinish(megaSDK, ref new MRequest(request->copy(), true), ref new MError(e->copy(), true));
		if (singleListener)
			megaSDK->freeRequestListener(this);
	}
}

void DelegateMRequestListener::onRequestUpdate(MegaApi* api, MegaRequest* request)
{
	if (listener != nullptr)
		listener->onRequestUpdate(megaSDK, ref new MRequest(request->copy(), true));
}

void DelegateMRequestListener::onRequestTemporaryError(MegaApi* api, MegaRequest* request, MegaError* e)
{
	if (listener != nullptr)
		listener->onRequestTemporaryError(megaSDK, ref new MRequest(request->copy(), true), ref new MError(e->copy(), true));
}
