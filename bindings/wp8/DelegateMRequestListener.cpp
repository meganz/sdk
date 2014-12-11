/**
* @file DelegateMRequestListener.cpp
* @brief Delegate to receive information about requests.
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
