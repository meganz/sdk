/**
* @file MTransferList.h
* @brief List of MTransfer objects
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

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public ref class MTransferList sealed
	{
		friend ref class MegaSDK;

	public:
		virtual ~MTransferList();
		MTransfer^ get(int i);
		int size();

	private:
		MTransferList(MegaTransferList *transferList, bool cMemoryOwn);
		MegaTransferList *transferList;
		bool cMemoryOwn;
	};
}
