#pragma once

#include "MTransfer.h"
#include "MError.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MTransferListenerInterface
	{
	public:
		void onTransferStart(MegaSDK^ api, MTransfer^ transfer);
		void onTransferFinish(MegaSDK^ api, MTransfer^ transfer, MError^ e);
		void onTransferUpdate(MegaSDK^ api, MTransfer^ transfer);
		void onTransferTemporaryError(MegaSDK^ api, MTransfer^ transfer, MError^ e);		

		bool onTransferData(MegaSDK^ api, MTransfer^ transfer, const ::Platform::Array<unsigned char>^ data);
	};
}
