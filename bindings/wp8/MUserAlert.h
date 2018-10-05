/**
* @file MUserAlert.h
* @brief Represents a user alert in MEGA.
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

    public enum class MUserAlertType {
        TYPE_INCOMINGPENDINGCONTACT_REQUEST,
        TYPE_INCOMINGPENDINGCONTACT_CANCELLED,
        TYPE_INCOMINGPENDINGCONTACT_REMINDER,
        TYPE_CONTACTCHANGE_DELETEDYOU,
        TYPE_CONTACTCHANGE_CONTACTESTABLISHED,
        TYPE_CONTACTCHANGE_ACCOUNTDELETED,
        TYPE_CONTACTCHANGE_BLOCKEDYOU,
        TYPE_UPDATEDPENDINGCONTACTINCOMING_IGNORED,
        TYPE_UPDATEDPENDINGCONTACTINCOMING_ACCEPTED,
        TYPE_UPDATEDPENDINGCONTACTINCOMING_DENIED,
        TYPE_UPDATEDPENDINGCONTACTOUTGOING_ACCEPTED,
        TYPE_UPDATEDPENDINGCONTACTOUTGOING_DENIED,
        TYPE_NEWSHARE,
        TYPE_DELETEDSHARE,
        TYPE_NEWSHAREDNODES,
        TYPE_REMOVEDSHAREDNODES,
        TYPE_PAYMENT_SUCCEEDED,
        TYPE_PAYMENT_FAILED,
        TYPE_PAYMENTREMINDER,
        TYPE_TAKEDOWN,
        TYPE_TAKEDOWN_REINSTATED,

        TOTAL_OF_ALERT_TYPES
    };

    /**
    * @brief Represents a user alert in MEGA.
    * Alerts are the notifictions appearing under the bell in the webclient
    *
    * Objects of this class aren't live, they are snapshots of the state
    * in MEGA when the object is created, they are immutable.
    *
    * MUserAlerts can be retrieved with MegaSDK::getUserAlerts
    *
    */
    public ref class MUserAlert sealed
    {
        friend ref class MegaSDK;
        friend ref class MUserAlertList;

    public:
        virtual ~MUserAlert();

        /**
        * @brief Creates a copy of this MUserAlert object.
        *
        * The resulting object is fully independent of the source MUserAlert,
        * it contains a copy of all internal attributes, so it will be valid after
        * the original object is deleted.
        *
        * You are the owner of the returned object
        *
        * @return Copy of the MUserAlert object
        */
        MUserAlert^ copy();

        /**
        * @brief Returns the id of the alert
        *
        * The ids are assigned to alerts sequentially from program start,
        * however there may be gaps.   The id can be used to create an
        * association with a UI element in order to process updates in callbacks.
        *
        * @return Type of alert associated with the object
        */
        unsigned int getId();

        /**
        * @brief Returns whether the alert has been acknowledged by this client or another
        *
        * @return Flag indicating whether the alert has been seen
        */
        bool getSeen();

        /**
        * @brief Returns whether the alert is still relevant to the logged in user.
        *
        * An alert may be relevant initially but become non-relevant, eg. payment reminder.
        * Alerts which are no longer relevant are usually removed from the visible list.
        *
        * @return Flag indicting whether the alert is still relevant
        */
        bool getRelevant();

        /**
        * @brief Returns the type of alert associated with the object
        * @return Type of alert associated with the object
        */
        int getType();

        /**
        * @brief Returns a readable string that shows the type of alert
        *
        * This function returns a pointer to a statically allocated buffer.
        * You don't have to free the returned pointer
        *
        * @return Readable string showing the type of alert
        */
        String^ getTypeString();

        /**
        * @brief Returns the handle of a user related to the alert
        *
        * This value is valid for user related alerts.
        *
        * @return the associated user's handle, otherwise UNDEF
        */
        uint64 getUserHandle();

        /**
        * @brief Returns the handle of a node related to the alert
        *
        * This value is valid for alerts that relate to a single node.
        *
        * @return the relevant node handle, or UNDEF if this alert does not have one.
        */
        uint64 getNodeHandle();

        /**
        * @brief Returns an email related to the alert
        *
        * This value is valid for alerts that relate to another user, provided the
        * user could be looked up at the time the alert arrived.  If it was not available,
        * this function will return false and the client can request it via the userHandle.
        *
        * The SDK retains the ownership of the returned value. It will be valid until
        * the MUserAlert object is deleted.
        *
        * @return email string of the relevant user, or NULL if not available
        */
        String^ getEmail();

        /**
        * @brief Returns the path of a file, folder, or node related to the alert
        *
        * The SDK retains the ownership of the returned value. It will be valid until
        * the MUserAlert object is deleted.
        *
        * This value is valid for those alerts that relate to a single path, provided
        * it could be looked up from the cached nodes at the time the alert arrived.
        * Otherwise, it may be obtainable via the nodeHandle.
        *
        * @return the path string if relevant and available, otherwise NULL
        */
        String^ getPath();

        /**
        * @brief Returns the heading related to this alert
        *
        * The SDK retains the ownership of the returned value. They will be valid until
        * the MUserAlert object is deleted.
        *
        * This value is valid for all alerts, and similar to the strings displayed in the
        * webclient alerts.
        *
        * @return heading related to this alert.
        */
        String^ getHeading();

        /**
        * @brief Returns the title related to this alert
        *
        * The SDK retains the ownership of the returned value. They will be valid until
        * the MUserAlert object is deleted.
        *
        * This value is valid for all alerts, and similar to the strings displayed in the
        * webclient alerts.
        *
        * @return title related to this alert.
        */
        String^ getTitle();

        /**
        * @brief Returns a number related to this alert
        *
        * This value is valid for these alerts:
        * TYPE_NEWSHAREDNODES (0: folder count  1: file count )
        * TYPE_REMOVEDSHAREDNODES (0: item count )
        *
        * @return Number related to this request, or -1 if the index is invalid
        */
        uint64 getNumber(unsigned int index);

        /**
        * @brief Returns a timestamp related to this alert
        *
        * This value is valid for index 0 for all requests, indicating when the alert occurred.
        * Additionally TYPE_PAYMENTREMINDER index 1 is the timestamp of the expiry of the period.
        *
        * @return Timestamp related to this request, or -1 if the index is invalid
        */
        uint64 getTimestamp(unsigned int index);

        /**
        * @brief Returns an additional string, related to the alert
        *
        * The SDK retains the ownership of the returned value. It will be valid until
        * the MUserAlert object is deleted.
        *
        * This value is currently only valid for
        *   TYPE_PAYMENT_SUCCEEDED   index 0: the plan name
        *   TYPE_PAYMENT_FAILED      index 0: the plan name
        *
        * @return a pointer to the string if index is valid; otherwise NULL
        */
        String^ getString(unsigned int index);

    private:
        MUserAlert(MegaUserAlert *megaUserAlert, bool cMemoryOwn);
        MegaUserAlert *megaUserAlert;
        MegaUserAlert *getCPtr();
        bool cMemoryOwn;
    };
}
