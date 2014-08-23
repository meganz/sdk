#include "DelegateMTransferListener.h"

using namespace mega;
using namespace Platform;

DelegateMTransferListener::DelegateMTransferListener(MegaSDK^ megaSDK, MTransferListenerInterface^ listener, bool singleListener)
{
	this->megaSDK = megaSDK;
	this->listener = listener;
	this->singleListener = singleListener;
}

MTransferListenerInterface^ DelegateMTransferListener::getUserListener()
{
	return listener;
}

void DelegateMTransferListener::onTransferStart(MegaApi* api, MegaTransfer* transfer)
{
	if (listener != nullptr)
		listener->onTransferStart(megaSDK, ref new MTransfer(transfer->copy(), true));
}

void DelegateMTransferListener::onTransferFinish(MegaApi* api, MegaTransfer* transfer, MegaError* e)
{
	if (listener != nullptr)
	{
		listener->onTransferFinish(megaSDK, ref new MTransfer(transfer->copy(), true), ref new MError(e->copy(), true));
		if (singleListener)
			megaSDK->freeTransferListener(this);
	}
}

void DelegateMTransferListener::onTransferUpdate(MegaApi* api, MegaTransfer* transfer)
{
	if (listener != nullptr)
		listener->onTransferUpdate(megaSDK, ref new MTransfer(transfer->copy(), true));
}

void DelegateMTransferListener::onTransferTemporaryError(MegaApi* api, MegaTransfer* transfer, MegaError* e)
{
	if (listener != nullptr)
		listener->onTransferTemporaryError(megaSDK, ref new MTransfer(transfer->copy(), true), ref new MError(e->copy(), true));
}
