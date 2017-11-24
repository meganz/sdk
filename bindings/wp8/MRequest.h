/**
* @file MRequest.h
* @brief Provides information about an asynchronous request.
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

#include "MNode.h"
#include "MAccountDetails.h"
#include "MPricing.h"
#include "MAchievementsDetails.h"

#include "megaapi.h"

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MRequestType
    {
        TYPE_LOGIN, TYPE_CREATE_FOLDER, TYPE_MOVE, TYPE_COPY,
        TYPE_RENAME, TYPE_REMOVE, TYPE_SHARE,
        TYPE_IMPORT_LINK, TYPE_EXPORT, TYPE_FETCH_NODES, TYPE_ACCOUNT_DETAILS,
        TYPE_CHANGE_PW, TYPE_UPLOAD, TYPE_LOGOUT,
        TYPE_GET_PUBLIC_NODE, TYPE_GET_ATTR_FILE,
        TYPE_SET_ATTR_FILE, TYPE_GET_ATTR_USER,
        TYPE_SET_ATTR_USER, TYPE_RETRY_PENDING_CONNECTIONS,
        TYPE_REMOVE_CONTACT, TYPE_CREATE_ACCOUNT,
        TYPE_CONFIRM_ACCOUNT,
        TYPE_QUERY_SIGNUP_LINK, TYPE_ADD_SYNC, TYPE_REMOVE_SYNC,
        TYPE_REMOVE_SYNCS, TYPE_PAUSE_TRANSFERS,
        TYPE_CANCEL_TRANSFER, TYPE_CANCEL_TRANSFERS,
        TYPE_DELETE, TYPE_REPORT_EVENT, TYPE_CANCEL_ATTR_FILE,
        TYPE_GET_PRICING, TYPE_GET_PAYMENT_ID, TYPE_GET_USER_DATA,
        TYPE_LOAD_BALANCING, TYPE_KILL_SESSION, TYPE_SUBMIT_PURCHASE_RECEIPT,
        TYPE_CREDIT_CARD_STORE, TYPE_UPGRADE_ACCOUNT, TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS,
        TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS, TYPE_GET_SESSION_TRANSFER_URL,
        TYPE_GET_PAYMENT_METHODS, TYPE_INVITE_CONTACT, TYPE_REPLY_CONTACT_REQUEST,
        TYPE_SUBMIT_FEEDBACK, TYPE_SEND_EVENT, TYPE_CLEAN_RUBBISH_BIN,
        TYPE_SET_ATTR_NODE, TYPE_CHAT_CREATE, TYPE_CHAT_FETCH, TYPE_CHAT_INVITE,
        TYPE_CHAT_REMOVE, TYPE_CHAT_URL, TYPE_CHAT_GRANT_ACCESS, TYPE_CHAT_REMOVE_ACCESS,
        TYPE_USE_HTTPS_ONLY, TYPE_SET_PROXY,
        TYPE_GET_RECOVERY_LINK, TYPE_QUERY_RECOVERY_LINK, TYPE_CONFIRM_RECOVERY_LINK,
        TYPE_GET_CANCEL_LINK, TYPE_CONFIRM_CANCEL_LINK,
        TYPE_GET_CHANGE_EMAIL_LINK, TYPE_CONFIRM_CHANGE_EMAIL_LINK,
        TYPE_CHAT_UPDATE_PERMISSIONS, TYPE_CHAT_TRUNCATE, TYPE_CHAT_SET_TITLE, TYPE_SET_MAX_CONNECTIONS,
        TYPE_PAUSE_TRANSFER, TYPE_MOVE_TRANSFER, TYPE_CHAT_PRESENCE_URL, TYPE_REGISTER_PUSH_NOTIFICATION,
        TYPE_GET_USER_EMAIL, TYPE_APP_VERSION, TYPE_GET_LOCAL_SSL_CERT, TYPE_SEND_SIGNUP_LINK,
        TYPE_QUERY_DNS, TYPE_QUERY_GELB, TYPE_CHAT_STATS, TYPE_DOWNLOAD_FILE,
        TYPE_QUERY_TRANSFER_QUOTA, TYPE_PASSWORD_LINK, TYPE_GET_ACHIEVEMENTS, 
        TOTAL_OF_REQUEST_TYPES
    };

    public ref class MRequest sealed
    {
        friend ref class MegaSDK;
        friend class DelegateMRequestListener;
        friend class DelegateMListener;

    public:
        virtual ~MRequest();
        MRequest^ copy();
        MRequestType getType();
        String^ getRequestString();
        String^ toString();
        uint64 getNodeHandle();
        String^ getLink();
        uint64 getParentHandle();
        String^ getSessionKey();
        String^ getName();
        String^ getEmail();
        String^ getPassword();
        String^ getNewPassword();
        String^ getPrivateKey();
        int getAccess();
        String^ getFile();
        int getNumRetry();        
        MNode^ getPublicMegaNode();
        int getParamType();
        String^ getText();
        uint64 getNumber();
        bool getFlag();
        uint64 getTransferredBytes();
        uint64 getTotalBytes();
        MAccountDetails^ getMAccountDetails();
        int getTransferTag();
        int getNumDetails();
        int getTag();
        MPricing^ getPricing();
        MAchievementsDetails^ getMAchievementsDetails();

    private:
        MRequest(MegaRequest *megaRequest, bool cMemoryOwn);
        MegaRequest *megaRequest;
        MegaRequest *getCPtr();
        bool cMemoryOwn;
    };
}
