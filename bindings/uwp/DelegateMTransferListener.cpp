/**
* @file DelegateMTransferListener.cpp
* @brief Delegate to receive information about transfers.
*
* (c) 2013-2014 by Mega Limited, Auckland, New Zealand
*
* This file is part of the MEGA SDK - Client Access Engine.
*
* Applications using the MEGA API must present a valid application key
* and comply with the the rules set forth in the Terms of Service.
*
* The MEGA SDK is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* @copyright Simplified (2-clause) BSD License.
*
* You should have received a copy of the license along with this
* program.
*/

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

bool DelegateMTransferListener::onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size)
{
	if (listener != nullptr)
		return listener->onTransferData(megaSDK, ref new MTransfer(transfer->copy(), true), ::Platform::ArrayReference<unsigned char>((unsigned char *)buffer, size));

	return false;
}
