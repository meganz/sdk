/**
* @file DelegateMGlobalListener.cpp
* @brief Delegate to get information about global events.
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

void DelegateMGlobalListener::onAccountUpdate(MegaApi* api)
{
	if (listener != nullptr)
		listener->onAccountUpdate(megaSDK);
}

void DelegateMGlobalListener::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
{
    if (listener != nullptr)
        listener->onContactRequestsUpdate(megaSDK, requests ? ref new MContactRequestList(requests->copy(), true) : nullptr);
}

void DelegateMGlobalListener::onReloadNeeded(MegaApi* api)
{
	if (listener != nullptr)
		listener->onReloadNeeded(megaSDK);
}
