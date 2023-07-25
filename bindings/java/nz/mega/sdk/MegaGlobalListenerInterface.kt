/*
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,\
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * @copyright Simplified (2-clause) BSD License.
 * You should have received a copy of the license along with this
 * program.
 */
package nz.mega.sdk

/**
 * Interface to get information about global events.
 *
 *
 * You can implement this interface and start receiving events calling MegaApiJava.addGlobalListener().
 * MegaListener objects can also receive global events.
 */
interface MegaGlobalListenerInterface {
    /**
     * This function is called when there are new or updated contacts in the account.
     *
     *
     * The SDK retains the ownership of the MegaUserList in the second parameter.
     * The list and all the MegaUser objects that it contains will be valid until this function returns.
     * If you want to save the list, use MegaUserList.copy().
     * If you want to save only some of the MegaUser objects, use MegaUser.copy() for those objects.
     * @param api
     * Mega Java API connected to account.
     * @param users
     * List of new or updated Contacts.
     */
    fun onUsersUpdate(api: MegaApiJava, users: ArrayList<MegaUser>?)

    /**
     * This function is called when there are new or updated user alerts in the account
     *
     * The SDK retains the ownership of the MegaUserAlertList in the second parameter. The list and all the
     * MegaUserAlert objects that it contains will be valid until this function returns. If you want to save the
     * list, use MegaUserAlertList::copy. If you want to save only some of the MegaUserAlert objects, use MegaUserAlert::copy
     * for those objects.
     *
     * @param api MegaApi object connected to the account
     * @param userAlerts List that contains the new or updated contacts
     */
    fun onUserAlertsUpdate(api: MegaApiJava, userAlerts: ArrayList<MegaUserAlert>?)

    /**
     * This function is called when there are new or updated nodes in the account.
     *
     *
     * When the full account is reloaded or a large number of server notifications arrives at once,
     * the second parameter will be null.
     * The SDK retains the ownership of the MegaNodeList in the second parameter.
     * The list and all the MegaNode objects that it contains will be valid until this function returns.
     * If you want to save the list, use MegaNodeList.copy().
     * If you want to save only some of the MegaNode objects, use MegaNode.copy() for those nodes.
     *
     * @param api
     * API connected to account.
     * @param nodeList
     * List of new or updated Nodes.
     */
    fun onNodesUpdate(api: MegaApiJava, nodeList: ArrayList<MegaNode>?)

    /**
     * This function is called when an inconsistency is detected in the local cache.
     *
     *
     * You should call MegaApi.fetchNodes() when this callback is received.
     *
     * @param api
     * API connected to account.
     */
    fun onReloadNeeded(api: MegaApiJava)

    /**
     * This function is called when the account has been updated (confirmed/upgraded/downgraded)
     *
     * The usage of this callback to handle the external account confirmation is deprecated.
     * Instead, you should use MegaGlobalListener::onEvent.
     *
     * @param api MegaApi object connected to the account
     */
    fun onAccountUpdate(api: MegaApiJava)

    /**
     * This function is called when there are new or updated contact requests in the account
     *
     * When the full account is reloaded or a large number of server notifications arrives at once, the
     * second parameter will be NULL.
     *
     * The SDK retains the ownership of the MegaContactRequestList in the second parameter. The list and all the
     * MegaContactRequest objects that it contains will be valid until this function returns. If you want to save the
     * list, use MegaContactRequestList::copy. If you want to save only some of the MegaContactRequest objects, use MegaContactRequest::copy
     * for them.
     *
     * @param api MegaApi object connected to the account
     * @param requests List that contains the new or updated contact requests
     */
    fun onContactRequestsUpdate(api: MegaApiJava, requests: ArrayList<MegaContactRequest>?)

    /**
     * The details about the event, like the type of event and optionally any
     * additional parameter, is received in the \c params parameter.
     *
     * You can check the type of event by calling MegaEvent::getType
     *
     * The SDK retains the ownership of the details of the event (\c event).
     * Don't use them after this functions returns.
     *
     * Currently, the following type of events are notified:
     *
     * - MegaEvent::EVENT_COMMIT_DB: when the SDK commits the ongoing DB transaction.
     * This event can be used to keep synchronization between the SDK cache and the
     * cache managed by the app thanks to the sequence number.
     *
     * Valid data in the MegaEvent object received in the callback:
     * - MegaEvent::getText: sequence number recorded by the SDK when this event happened
     *
     * - MegaEvent::EVENT_ACCOUNT_CONFIRMATION: when a new account is finally confirmed
     * by the user by confirming the signup link.
     *
     * Valid data in the MegaEvent object received in the callback:
     * - MegaEvent::getText: email address used to confirm the account
     *
     * - MegaEvent::EVENT_CHANGE_TO_HTTPS: when the SDK automatically starts using HTTPS for all
     * its communications. This happens when the SDK is able to detect that MEGA servers can't be
     * reached using HTTP or that HTTP communications are being tampered. Transfers of files and
     * file attributes (thumbnails and previews) use HTTP by default to save CPU usage. Since all data
     * is already end-to-end encrypted, it's only needed to use HTTPS if HTTP doesn't work. Anyway,
     * applications can force the SDK to always use HTTPS using MegaApi::useHttpsOnly. It's recommended
     * that applications that receive one of these events save that information on its settings and
     * automatically enable HTTPS on next executions of the app to not force the SDK to detect the problem
     * and automatically switch to HTTPS every time that the application starts.
     *
     * - MegaEvent::EVENT_DISCONNECT: when the SDK performs a disconnect to reset all the
     * existing open-connections, since they have become unusable. It's recommended that the app
     * receiving this event reset its connections with other servers, since the disconnect
     * performed by the SDK is due to a network change or IP addresses becoming invalid.
     *
     * - MegaEvent::EVENT_ACCOUNT_BLOCKED: when the account get blocked, typically because of
     * infringement of the Mega's terms of service repeatedly. This event is followed by an automatic
     * logout.
     *
     * Valid data in the MegaEvent object received in the callback:
     * - MegaEvent::getText: message to show to the user.
     * - MegaEvent::getNumber: code representing the reason for being blocked.
     * 200: suspension message for any type of suspension, but copyright suspension.
     * 300: suspension only for multiple copyright violations.
     * 400: the subuser account has been disabled.
     * 401: the subuser account has been removed.
     * 500: The account needs to be verified by an SMS code.
     * 700: the account is supended for Weak Account Protection.
     *
     * - MegaEvent::EVENT_STORAGE: when the status of the storage changes.
     *
     * For this event type, MegaEvent::getNumber provides the current status of the storage
     *
     * There are three possible storage states:
     * - MegaApi::STORAGE_STATE_GREEN = 0
     * There are no storage problems
     *
     * - MegaApi::STORAGE_STATE_ORANGE = 1
     * The account is almost full
     *
     * - MegaApi::STORAGE_STATE_RED = 2
     * The account is full. Uploads have been stopped
     *
     * - MegaApi::STORAGE_STATE_CHANGE = 3
     * There is a possible significant change in the storage state.
     * It's needed to call MegaApi::getAccountDetails to check the storage status.
     * After calling it, this callback will be called again with the corresponding
     * state if there is really a change.
     *
     * - MegaApi::STORAGE_STATE_PAYWALL = 4
     * The account has been full for a long time. Now most of actions are disallowed.
     * It's needed to call MegaApi::getUserData in order to retrieve the deadline/warnings
     * timestamps. @see MegaApi::getOverquotaDeadlineTs and MegaApi::getOverquotaWarningsTs.
     *
     * - MegaEvent::EVENT_NODES_CURRENT: when all external changes have been received
     *
     * - MegaEvent::EVENT_MEDIA_INFO_READY: when codec-mappings have been received
     *
     * - MegaEvent::EVENT_STORAGE_SUM_CHANGED: when the storage sum has changed.
     *
     * For this event type, MegaEvent::getNumber provides the new storage sum.
     *
     * - MegaEvent::EVENT_BUSINESS_STATUS: when the status of a business account has changed.
     *
     * For this event type, MegaEvent::getNumber provides the new business status.
     *
     * The posible values are:
     * - BUSINESS_STATUS_EXPIRED = -1
     * - BUSINESS_STATUS_INACTIVE = 0
     * - BUSINESS_STATUS_ACTIVE = 1
     * - BUSINESS_STATUS_GRACE_PERIOD = 2
     *
     * - MegaEvent::EVENT_KEY_MODIFIED: when the key of a user has changed.
     *
     * For this event type, MegaEvent::getHandle provides the handle of the user whose key has been modified.
     * For this event type, MegaEvent::getNumber provides type of key that has been modified.
     *
     * The possible values are:
     * - Public chat key (Cu25519)     = 0
     * - Public signing key (Ed25519)  = 1
     * - Public RSA key                = 2
     * - Signature of chat key         = 3
     * - Signature of RSA key          = 4
     *
     * - MegaEvent::EVENT_GLOBAL_FLAGS_READY: when the global flags are available/updated.
     *
     * @param api MegaApi object connected to the account
     * @param event Details about the event
     */
    fun onEvent(api: MegaApiJava, event: MegaEvent?)

    /**
     * This function is called when a Set has been updated (created / updated / removed)
     *
     * The SDK retains the ownership of the MegaSetList in the second parameter. The list and all the
     * MegaSet objects that it contains will be valid until this function returns. If you want to save the
     * list, use MegaSetList::copy. If you want to save only some of the MegaSet objects, use MegaSet::copy
     * for them.
     *
     * @param api MegaApi object connected to the account
     * @param sets List that contains the new or updated Sets
     */
    fun onSetsUpdate(api: MegaApiJava, sets: ArrayList<MegaSet>?)

    /**
     * This function is called when a Set-Element has been updated (created / updated / removed)
     *
     * The SDK retains the ownership of the MegaSetElementList in the second parameter. The list and all the
     * MegaSetElement objects that it contains will be valid until this function returns. If you want to save the
     * list, use MegaSetElementList::copy. If you want to save only some of the MegaSetElement objects, use
     * MegaSetElement::copy for them.
     *
     * @param api MegaApi object connected to the account
     * @param elements List that contains the new or updated Set-Elements
     */
    fun onSetElementsUpdate(api: MegaApiJava, elements: ArrayList<MegaSetElement>?)

    /**
     * @brief This function is called with the state of the synchronization engine has changed
     *
     * You can call MegaApi::isScanning and MegaApi::isWaiting to know the global state
     * of the synchronization engine.
     *
     * @param api MegaApi object related to the event
     */
    fun onGlobalSyncStateChanged(api: MegaApiJava)
}