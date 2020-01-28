/**
* @file DelegateMTransferListener.h
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

#pragma once

#include "MegaSDK.h"
#include "MTransferListenerInterface.h"
#include "MTransfer.h"
#include "MError.h"

#include "megaapi.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	private class DelegateMTransferListener : public MegaTransferListener
	{
	public:
		DelegateMTransferListener(MegaSDK^ megaSDK, MTransferListenerInterface^ listener, bool singleListener = true);
		MTransferListenerInterface^ getUserListener();

		void onTransferStart(MegaApi* api, MegaTransfer* transfer);
		void onTransferFinish(MegaApi* api, MegaTransfer* transfer, MegaError* e);
		void onTransferUpdate(MegaApi* api, MegaTransfer* transfer);
		void onTransferTemporaryError(MegaApi* api, MegaTransfer* transfer, MegaError* e);

		bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size);

	private:
		MegaSDK^ megaSDK;
		MTransferListenerInterface^ listener;
		bool singleListener;
	};
}
