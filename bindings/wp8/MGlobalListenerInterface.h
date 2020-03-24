/**
* @file MGlobalListenerInterface.h
* @brief Interface to get information about global events.
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

namespace mega
{
    ref class MegaSDK;

    using namespace Windows::Foundation;
    using Platform::String;

    /**
    * @brief Interface to get information about global events
    *
    * You can implement this interface and start receiving events calling MegaSDK::addGlobalListener
    *
    * MegaListener objects can also receive global events
    *
    * The implementation will receive callbacks from an internal worker thread.
    */
    public interface class MGlobalListenerInterface
    {
    public:
        /**
        * @brief This function is called when there are new or updated contacts in the account
        *
        * The SDK retains the ownership of the MUserList in the second parameter. The list and all the
        * MUser objects that it contains will be valid until this function returns. If you want to save the
        * list, use MUserList::copy. If you want to save only some of the MUser objects, use MUser::copy
        * for those objects.
        *
        * @param api MegaSDK object connected to the account
        * @param users List that contains the new or updated contacts
        */
        void onUsersUpdate(MegaSDK^ api, MUserList ^users);

        /**
        * @brief This function is called when there are new or updated user alerts in the account
        *
        * The SDK retains the ownership of the MUserAlertList in the second parameter. The list and all the
        * MUserAlert objects that it contains will be valid until this function returns. If you want to save the
        * list, use MUserAlertList::copy. If you want to save only some of the MUserAlert objects, use MUserAlert::copy
        * for those objects.
        *
        * @param api MegaSDK object connected to the account
        * @param alerts List that contains the new or updated alerts
        */
        void onUserAlertsUpdate(MegaSDK^ api, MUserAlertList^ alerts);

        /**
        * @brief This function is called when there are new or updated nodes in the account
        *
        * When the full account is reloaded or a large number of server notifications arrives at once, the
        * second parameter will be NULL.
        *
        * The SDK retains the ownership of the MNodeList in the second parameter. The list and all the
        * MNode objects that it contains will be valid until this function returns. If you want to save the
        * list, use MNodeList::copy. If you want to save only some of the MNode objects, use MNode::copy
        * for those nodes.
        *
        * @param api MegaSDK object connected to the account
        * @param nodes List that contains the new or updated nodes
        */
        void onNodesUpdate(MegaSDK^ api, MNodeList^ nodes);

        /**
        * @brief This function is called when the account has been updated (confirmed/upgraded/downgraded)
        *
        * The usage of this callback to handle the external account confirmation is deprecated.
        * Instead, you should use MGlobalListenerInterface::onEvent.
        *
        * @param api MegaSDK object connected to the account
        */
        void onAccountUpdate(MegaSDK^ api);

        /**
        * @brief This function is called when there are new or updated contact requests in the account
        *
        * When the full account is reloaded or a large number of server notifications arrives at once, the
        * second parameter will be NULL.
        *
        * The SDK retains the ownership of the MContactRequestList in the second parameter. The list and all the
        * MContactRequest objects that it contains will be valid until this function returns. If you want to save the
        * list, use MContactRequestList::copy. If you want to save only some of the MContactRequest objects, 
        * use MContactRequest::copy for them.
        *
        * @param api MegaSDK object connected to the account
        * @param requests List that contains the new or updated contact requests
        */
        void onContactRequestsUpdate(MegaSDK^ api, MContactRequestList^ requests);

        /**
        * @brief This function is called when an inconsistency is detected in the local cache
        *
        * You should call MegaSDK::fetchNodes when this callback is received
        *
        * @param api MegaSDK object connected to the account
        */
        void onReloadNeeded(MegaSDK^ api);

        /**
        * The details about the event, like the type of event and optionally any
        * additional parameter, is received in the \c params parameter.
        *
        * You can check the type of event by calling MEvent::getType
        *
        * The SDK retains the ownership of the details of the event (\c event).
        * Don't use them after this functions returns.
        *
        * Currently, the following type of events are notified:
        *
        *  - MEvent::EVENT_COMMIT_DB: when the SDK commits the ongoing DB transaction.
        *  This event can be used to keep synchronization between the SDK cache and the
        *  cache managed by the app thanks to the sequence number.
        *
        *  Valid data in the MEvent object received in the callback:
        *      - MEvent::getText: sequence number recorded by the SDK when this event happened
        *
        *  - MEvent::EVENT_ACCOUNT_CONFIRMATION: when a new account is finally confirmed
        * by the user by confirming the signup link.
        *
        *   Valid data in the MEvent object received in the callback:
        *      - MEvent::getText: email address used to confirm the account
        *
        *  - MEvent::EVENT_CHANGE_TO_HTTPS: when the SDK automatically starts using HTTPS for all
        * its communications. This happens when the SDK is able to detect that MEGA servers can't be
        * reached using HTTP or that HTTP communications are being tampered. Transfers of files and
        * file attributes (thumbnails and previews) use HTTP by default to save CPU usage. Since all data
        * is already end-to-end encrypted, it's only needed to use HTTPS if HTTP doesn't work. Anyway,
        * applications can force the SDK to always use HTTPS using MegaSDK::useHttpsOnly. It's recommended
        * that applications that receive one of these events save that information on its settings and
        * automatically enable HTTPS on next executions of the app to not force the SDK to detect the problem
        * and automatically switch to HTTPS every time that the application starts.
        *
        *  - MEvent::EVENT_DISCONNECT: when the SDK performs a disconnect to reset all the
        * existing open-connections, since they have become unusable. It's recommended that the app
        * receiving this event reset its connections with other servers, since the disconnect
        * performed by the SDK is due to a network change or IP addresses becoming invalid.
        *
        *  - MEvent::EVENT_ACCOUNT_BLOCKED: when the account get blocked, typically because of
        * infringement of the Mega's terms of service repeatedly. This event is followed by an automatic
        * logout.
        *
        *  Valid data in the MEvent object received in the callback:
        *      - MEvent::getText: message to show to the user.
        *      - MEvent::getNumber: code representing the reason for being blocked.
        *          200: suspension message for any type of suspension, but copyright suspension.
        *          300: suspension only for multiple copyright violations.
        *
        * - MEvent::EVENT_STORAGE: when the status of the storage changes.
        *
        * For this event type, MEvent::getNumber provides the current status of the storage
        *
        * There are three possible storage states:
        *     - MegaSDK::STORAGE_STATE_GREEN = 0
        *     There are no storage problems
        *
        *     - MegaSDK::STORAGE_STATE_ORANGE = 1
        *     The account is almost full
        *
        *     - MegaSDK::STORAGE_STATE_RED = 2
        *     The account is full. Uploads have been stopped
        *
        *     - MegaSDK::STORAGE_STATE_CHANGE = 3
        *     There is a possible significant change in the storage state.
        *     It's needed to call MegaApi::getAccountDetails to check the storage status.
        *     After calling it, this callback will be called again with the corresponding
        *     state if there is really a change.
        *
        * - MEvent::EVENT_NODES_CURRENT: when all external changes have been received
        *
        * @param api MegaSDK object connected to the account
        * @param event Details about the event
        */
        void onEvent(MegaSDK^ api, MEvent^ ev);
    };
}
