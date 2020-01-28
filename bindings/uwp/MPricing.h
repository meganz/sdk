/**
* @file MPricing.h
* @brief Details about pricing plans
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

#include "megaapi.h"
#include "MAccountDetails.h"

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
		MAccountType getProLevel(int productIndex);
		int getGBStorage(int productIndex);
		int getGBTransfer(int productIndex);
		int getMonths(int productIndex);
		int getAmount(int productIndex);
		String^ getCurrency(int productIndex);
		String^ getDescription(int productIndex);
		MPricing^ copy();

	private:
		MPricing(MegaPricing *accountDetails, bool cMemoryOwn);
		MegaPricing *pricing;
		bool cMemoryOwn;
		MegaAccountDetails *getCPtr();
	};
}

