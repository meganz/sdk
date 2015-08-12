/**
* @file MGlobalListenerInterface.h
* @brief Interface to provide a global listener.
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

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MGlobalListenerInterface
	{
	public:
		void onUsersUpdate(MegaSDK^ api, MUserList ^users);
		void onNodesUpdate(MegaSDK^ api, MNodeList^ nodes);
		void onAccountUpdate(MegaSDK^ api);
        void onContactRequestsUpdate(MegaSDK^ api, MContactRequestList^ requests);
		void onReloadNeeded(MegaSDK^ api);
	};
}
