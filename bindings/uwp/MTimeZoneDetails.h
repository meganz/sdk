/**
* @file MTimeZoneDetails.h
* @brief Get details about timezones and the current default.
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

#include <megaapi.h>

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    /**
    * @brief Provides information about timezones
    *
    * This object is related to results of the function MegaSDK::fetchTimeZone
    *
    * Objects of this class aren't live, they are snapshots of the state of the contents of the
    * folder when the object is created, they are immutable.
    *
    */
    public ref class MTimeZoneDetails sealed
    {
        friend ref class MRequest;

    public:
        virtual ~MTimeZoneDetails();

        /**
        * @brief Creates a copy of this MTimeZoneDetails object
        *
        * The resulting object is fully independent of the source MTimeZoneDetails,
        * it contains a copy of all internal attributes, so it will be valid after
        * the original object is deleted.
        *
        * You are the owner of the returned object
        *
        * @return Copy of the MTimeZoneDetails object
        */
        MTimeZoneDetails^ copy();

        /**
        * @brief Returns the number of timezones in this object
        *
        * @return Number of timezones in this object
        */
        int getNumTimeZones();

        /**
        * @brief Returns the timezone at an index
        *
        * The MTimeZoneDetails object retains the ownership of the returned string.
        * It will be only valid until the MTimeZoneDetails object is deleted.
        *
        * @param index Index in the list (it must be lower than MTimeZoneDetails::getNumTimeZones)
        * @return Timezone at an index
        */
        String^ getTimeZone(int index);

        /**
        * @brief Returns the current time offset of the time zone at an index, respect to UTC (in seconds, it can be negative)
        *
        * @param index Index in the list (it must be lower than MTimeZoneDetails::getNumTimeZones)
        * @return Current time offset of the time zone at an index, respect to UTC (in seconds, it can be negative)
        * @see MTimeZoneDetails::getTimeZone
        */
        int getTimeOffset(int index);

        /**
        * @brief Get the default time zone index
        *
        * If there isn't any good default known, this function will return -1
        *
        * @return Default time zone index, or -1 if there isn't a good default known
        */
        int getDefault();

    private:
        MTimeZoneDetails(MegaTimeZoneDetails *timeZoneDetails, bool cMemoryOwn);
        MegaTimeZoneDetails *timeZoneDetails;
        bool cMemoryOwn;
        MegaTimeZoneDetails *getCPtr();
    };
}