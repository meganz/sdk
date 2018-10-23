/**
* @file MListenerInterface.h
* @brief Interface to get all information related to a MEGA account.
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

#include "MTransfer.h"
#include "MRequest.h"
#include "MError.h"
#include "MContactRequest.h"
#include "MEvent.h"
#include "MUserAlertList.h"

namespace mega
{
    ref class MegaSDK;

    using namespace Windows::Foundation;
    using Platform::String;

    /**
    * @brief Interface to get all information related to a MEGA account
    *
    * Implementations of this interface can receive all events (request, transfer, global) and two
    * additional events related to the synchronization engine. The SDK will provide a new interface
    * to get synchronization events separately in future updates-
    *
    * Multiple inheritance isn't used for compatibility with other programming languages
    *
    * The implementation will receive callbacks from an internal worker thread.
    *
    */
    public interface class MListenerInterface
    {
    public:

        /**
        * @brief This function is called when a request is about to start being processed
        *
        * The SDK retains the ownership of the request parameter.
        * Don't use it after this functions returns.
        *
        * The api object is the one created by the application, it will be valid until
        * the application deletes it.
        *
        * @param api MegaSDK object that started the request
        * @param request Information about the request
        */
        void onRequestStart(MegaSDK^ api, MRequest^ request);

        /**
        * @brief This function is called when a request has finished
        *
        * There won't be more callbacks about this request.
        * The last parameter provides the result of the request. If the request finished without problems,
        * the error code will be API_OK
        *
        * The SDK retains the ownership of the request and error parameters.
        * Don't use them after this functions returns.
        *
        * The api object is the one created by the application, it will be valid until
        * the application deletes it.
        *
        * @param api MegaSDK object that started the request
        * @param request Information about the request
        * @param e Error information
        */
        void onRequestFinish(MegaSDK^ api, MRequest^ request, MError^ e);

        /**
        * @brief This function is called to inform about the progres of a request
        *
        * Currently, this callback is only used for fetchNodes (MRequest::TYPE_FETCH_NODES) requests
        *
        * The SDK retains the ownership of the request parameter.
        * Don't use it after this functions returns.
        *
        * The api object is the one created by the application, it will be valid until
        * the application deletes it.
        *
        *
        * @param api MegaSDK object that started the request
        * @param request Information about the request
        * @see MRequest::getTotalBytes MRequest::getTransferredBytes
        */
        void onRequestUpdate(MegaSDK^ api, MRequest^ request);

        /**
        * @brief This function is called when there is a temporary error processing a request
        *
        * The request continues after this callback, so expect more MRequestListener::onRequestTemporaryError or
        * a MRequestListener::onRequestFinish callback
        *
        * The SDK retains the ownership of the request and error parameters.
        * Don't use them after this functions returns.
        *
        * The api object is the one created by the application, it will be valid until
        * the application deletes it.
        *
        * @param api MegaSDK object that started the request
        * @param request Information about the request
        * @param error Error information
        */
        void onRequestTemporaryError(MegaSDK^ api, MRequest^ request, MError^ e);

        /**
        * @brief This function is called when a transfer is about to start being processed
        *
        * The SDK retains the ownership of the transfer parameter.
        * Don't use it after this functions returns.
        *
        * The api object is the one created by the application, it will be valid until
        * the application deletes it.
        *
        * @param api MegaSDK object that started the request
        * @param transfer Information about the transfer
        */
        void onTransferStart(MegaSDK^ api, MTransfer^ transfer);

        /**
        * @brief This function is called when a transfer has finished
        *
        * The SDK retains the ownership of the transfer and error parameters.
        * Don't use them after this functions returns.
        *
        * The api object is the one created by the application, it will be valid until
        * the application deletes it.
        *
        * There won't be more callbacks about this transfer.
        * The last parameter provides the result of the transfer. If the transfer finished without problems,
        * the error code will be API_OK
        *
        * @param api MegaSDK object that started the transfer
        * @param transfer Information about the transfer
        * @param error Error information
        */
        void onTransferFinish(MegaSDK^ api, MTransfer^ transfer, MError^ e);

        /**
        * @brief This function is called to inform about the progress of a transfer
        *
        * The SDK retains the ownership of the transfer parameter.
        * Don't use it after this functions returns.
        *
        * The api object is the one created by the application, it will be valid until
        * the application deletes it.
        *
        * @param api MegaSDK object that started the transfer
        * @param transfer Information about the transfer
        *
        * @see MTransfer::getTransferredBytes, MTransfer::getSpeed
        */
        void onTransferUpdate(MegaSDK^ api, MTransfer^ transfer);

        /**
        * @brief This function is called when there is a temporary error processing a transfer
        *
        * The transfer continues after this callback, so expect more MTransferListener::onTransferTemporaryError or
        * a MTransferListener::onTransferFinish callback
        *
        * The SDK retains the ownership of the transfer and error parameters.
        * Don't use them after this functions returns.
        *
        * @param api MegaSDK object that started the transfer
        * @param transfer Information about the transfer
        * @param error Error information
        */
        void onTransferTemporaryError(MegaSDK^ api, MTransfer^ transfer, MError^ e);

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
        void onUsersUpdate(MegaSDK^ api, MUserList^ users);
        
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
        * Instead, you should use MListenerInterface::onEvent.
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
        * Currently, the following type of events are notified:
        *  - MEvent::EVENT_COMMIT_DB: when the SDK commits the ongoing DB transaction.
        *  This event can be used to keep synchronization between the SDK cache and the
        *  cache managed by the app thanks to the sequence number, available at MEvent::getText.
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
        * You can check the type of event by calling MEvent::getType
        *
        * The SDK retains the ownership of the details of the event (\c event).
        * Don't use them after this functions returns.
        *
        * @param api MegaSDK object connected to the account
        * @param event Details about the event
        */
        void onEvent(MegaSDK^ api, MEvent^ ev);
    };
}
