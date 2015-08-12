/**
* @file DelegateMListener.cpp
* @brief Delegate to get all information related to a MEGA account.
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

#include "DelegateMListener.h"

using namespace mega;
using namespace Platform;

DelegateMListener::DelegateMListener(MegaSDK^ megaSDK, MListenerInterface^ listener)
{
    this->megaSDK = megaSDK;
    this->listener = listener;
}

MListenerInterface^ DelegateMListener::getUserListener()
{
    return listener;
}

void DelegateMListener::onRequestStart(MegaApi* api, MegaRequest* request)
{
    if (listener != nullptr)
        listener->onRequestStart(megaSDK, ref new MRequest(request->copy(), true));
}

void DelegateMListener::onRequestFinish(MegaApi* api, MegaRequest* request, MegaError* e)
{
    if(listener != nullptr)
        listener->onRequestFinish(megaSDK, ref new MRequest(request->copy(), true), ref new MError(e->copy(), true));
}

void DelegateMListener::onRequestUpdate(MegaApi* api, MegaRequest* request)
{
    if (listener != nullptr)
        listener->onRequestUpdate(megaSDK, ref new MRequest(request->copy(), true));
}

void DelegateMListener::onRequestTemporaryError(MegaApi* api, MegaRequest* request, MegaError* e)
{
    if (listener != nullptr)
        listener->onRequestTemporaryError(megaSDK, ref new MRequest(request->copy(), true), ref new MError(e->copy(), true));
}

void DelegateMListener::onTransferStart(MegaApi* api, MegaTransfer* transfer)
{
    if (listener != nullptr)
        listener->onTransferStart(megaSDK, ref new MTransfer(transfer->copy(), true));
}

void DelegateMListener::onTransferFinish(MegaApi* api, MegaTransfer* transfer, MegaError* e)
{
    if (listener != nullptr)
        listener->onTransferFinish(megaSDK, ref new MTransfer(transfer->copy(), true), ref new MError(e->copy(), true));
}

void DelegateMListener::onTransferUpdate(MegaApi* api, MegaTransfer* transfer)
{
    if (listener != nullptr)
        listener->onTransferUpdate(megaSDK, ref new MTransfer(transfer->copy(), true));
}

void DelegateMListener::onTransferTemporaryError(MegaApi* api, MegaTransfer* transfer, MegaError* e)
{
    if (listener != nullptr)
        listener->onTransferTemporaryError(megaSDK, ref new MTransfer(transfer->copy(), true), ref new MError(e->copy(), true));
}

void DelegateMListener::onUsersUpdate(MegaApi* api, MegaUserList *users)
{
    if (listener != nullptr)
        listener->onUsersUpdate(megaSDK, users ? ref new MUserList(users->copy(), true) : nullptr);
}

void DelegateMListener::onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
{
    if (listener != nullptr)
        listener->onNodesUpdate(megaSDK, nodes ? ref new MNodeList(nodes->copy(), true) : nullptr);
}

void DelegateMListener::onAccountUpdate(MegaApi* api)
{
    if (listener != nullptr)
        listener->onAccountUpdate(megaSDK);
}

void DelegateMListener::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
{
    if (listener != nullptr)
        listener->onContactRequestsUpdate(megaSDK, requests ? ref new MContactRequestList(requests->copy(), true) : nullptr);
}

void DelegateMListener::onReloadNeeded(MegaApi* api)
{
    if (listener != nullptr)
        listener->onReloadNeeded(megaSDK);
}
