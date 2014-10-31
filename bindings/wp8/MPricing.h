#pragma once

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public ref class MPricing sealed
	{
		friend ref class MRequest;

	public:
		virtual ~MPricing();
		int getNumProducts();
		MegaHandle getHandle(int productIndex);
		int getProLevel(int productIndex);
		int getGBStorage(int productIndex);
		int getGBTransfer(int productIndex);
		int getMonths(int productIndex);
		int getAmount(int productIndex);
		String^ getCurrency(int productIndex);

	private:
		MPricing(MegaPricing *accountDetails, bool cMemoryOwn);
		MegaPricing *pricing;
		bool cMemoryOwn;
		MegaAccountDetails *getCPtr();
	};
}

