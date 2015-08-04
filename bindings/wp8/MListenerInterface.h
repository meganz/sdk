/**
* @file MListenerInterface.h
* @brief Interface to provide a listener.
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

#pragma once

#include "MTransfer.h"
#include "MRequest.h"
#include "MError.h"
#include "MContactRequest.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MListenerInterface
	{
	public:
		void onRequestStart(MegaSDK^ api, MRequest^ request);
		void onRequestFinish(MegaSDK^ api, MRequest^ request, MError^ e);
		void onRequestUpdate(MegaSDK^ api, MRequest^ request);
		void onRequestTemporaryError(MegaSDK^ api, MRequest^ request, MError^ e);
		void onTransferStart(MegaSDK^ api, MTransfer^ transfer);
		void onTransferFinish(MegaSDK^ api, MTransfer^ transfer, MError^ e);
		void onTransferUpdate(MegaSDK^ api, MTransfer^ transfer);
		void onTransferTemporaryError(MegaSDK^ api, MTransfer^ transfer, MError^ e);
		void onUsersUpdate(MegaSDK^ api, MUserList^ users);
		void onNodesUpdate(MegaSDK^ api, MNodeList^ nodes);
		void onAccountUpdate(MegaSDK^ api);
        void onContactRequestsUpdate(MegaSDK^ api, MContactRequestList^ requests);
		void onReloadNeeded(MegaSDK^ api);
	};
}
