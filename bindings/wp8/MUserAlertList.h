/**
* @file MUserAlertList.h
* @brief List of MUserAlert objects.
*
* (c) 2013-2018 by Mega Limited, Auckland, New Zealand
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

#include "MUserAlert.h"

#include <megaapi.h>

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    /**
    * @brief List of MUserAlert objects
    *
    * A MUserAlertList has the ownership of the MUserAlert objects that it contains, so they will be
    * only valid until the MUserAlertList is deleted. If you want to retain a MUserAlert returned by
    * a MUserAlertList, use MUserAlert::copy.
    *
    * Objects of this class are immutable.
    *
    * @see MegaSDK::getUserAlerts
    *
    */
    public ref class MUserAlertList sealed
    {
        friend ref class MegaSDK;
        friend class DelegateMListener;
        friend class DelegateMGlobalListener;

    public:
        virtual ~MUserAlertList();

        /**
        * @brief Creates a copy of this MUserAlertList object.
        *
        * The resulting object is fully independent of the source MUserAlertList,
        * it contains a copy of all internal attributes, so it will be valid after
        * the original object is deleted.
        *
        * You are the owner of the returned object
        *
        * @return Copy of the MUserAlertList object
        */
        MUserAlertList^ copy();

        /**
        * @brief Returns the MUserAlert at the position i in the MUserAlertList
        *
        * The MUserAlertList retains the ownership of the returned MUserAlert. It will be only valid until
        * the MUserAlertList is deleted.
        *
        * If the index is >= the size of the list, this function returns NULL.
        *
        * @param i Position of the MUserAlert that we want to get for the list
        * @return MUserAlert at the position i in the list
        */
        MUserAlert^ get(int i);

        /**
        * @brief Returns the number of MUserAlert objects in the list
        * @return Number of MUserAlert objects in the list
        */
        int size();

    private:
        MUserAlertList(MegaUserAlertList *userAlertList, bool cMemoryOwn);
        MegaUserAlertList *userAlertList;
        bool cMemoryOwn;
    };
}
