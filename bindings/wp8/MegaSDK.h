/**
* @file MegaSDK.h
* @brief Allows to control a MEGA account or a public folder.
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

#include <Windows.h>
#include <Synchapi.h>
#include <string>

#include "MNode.h"
#include "MUser.h"
#include "MTransfer.h"
#include "MTransferData.h"
#include "MRequest.h"
#include "MError.h"
#include "MTransferList.h"
#include "MNodeList.h"
#include "MUserList.h"
#include "MShareList.h"
#include "MListenerInterface.h"
#include "MRequestListenerInterface.h"
#include "MTransferListenerInterface.h"
#include "MGlobalListenerInterface.h"
#include "MTreeProcessorInterface.h"
#include "DelegateMRequestListener.h"
#include "DelegateMTransferListener.h"
#include "DelegateMGlobalListener.h"
#include "DelegateMListener.h"
#include "DelegateMTreeProcessor.h"
#include "DelegateMGfxProcessor.h"
#include "DelegateMLogger.h"
#include "MRandomNumberProvider.h"
#include "MContactRequest.h"
#include "MContactRequestList.h"
#include "MInputStreamAdapter.h"
#include "MInputStream.h"
#include "MChildrenLists.h"
#include "MUserAlertList.h"

#include <megaapi.h>
#include <set>

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MSortOrderType {
        ORDER_NONE = 0, ORDER_DEFAULT_ASC, ORDER_DEFAULT_DESC,
        ORDER_SIZE_ASC, ORDER_SIZE_DESC,
        ORDER_CREATION_ASC, ORDER_CREATION_DESC,
        ORDER_MODIFICATION_ASC, ORDER_MODIFICATION_DESC,
        ORDER_ALPHABETICAL_ASC, ORDER_ALPHABETICAL_DESC
    };

    public enum class MLogLevel {
        LOG_LEVEL_FATAL = 0, 
        LOG_LEVEL_ERROR,   // Error information but will continue application to keep running.
        LOG_LEVEL_WARNING, // Information representing errors in application but application will keep running
        LOG_LEVEL_INFO,    // Mainly useful to represent current progress of application.
        LOG_LEVEL_DEBUG,   // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
        LOG_LEVEL_MAX
    };

    public enum class MAttrType {
        ATTR_TYPE_THUMBNAIL = 0,
        ATTR_TYPE_PREVIEW = 1
    };

    public enum class MUserAttrType{
        USER_ATTR_AVATAR                    = 0,    // public - char array
        USER_ATTR_FIRSTNAME                 = 1,    // public - char array
        USER_ATTR_LASTNAME                  = 2,    // public - char array
        USER_ATTR_AUTHRING                  = 3,    // private - byte array
        USER_ATTR_LAST_INTERACTION          = 4,    // private - byte array
        USER_ATTR_ED25519_PUBLIC_KEY        = 5,    // public - byte array
        USER_ATTR_CU25519_PUBLIC_KEY        = 6,    // public - byte array
        USER_ATTR_KEYRING                   = 7,    // private - byte array
        USER_ATTR_SIG_RSA_PUBLIC_KEY        = 8,    // public - byte array
        USER_ATTR_SIG_CU255_PUBLIC_KEY      = 9,    // public - byte array
        USER_ATTR_LANGUAGE                  = 14,   // private - char array
        USER_ATTR_PWD_REMINDER              = 15,   // private - char array
        USER_ATTR_DISABLE_VERSIONS          = 16,   // private - byte array
        USER_ATTR_CONTACT_LINK_VERIFICATION = 17,   // private - byte array
        USER_ATTR_RICH_PREVIEWS             = 18,   // private - byte array
        USER_ATTR_RUBBISH_TIME              = 19,   // private - byte array
        USER_ATTR_LAST_PSA                  = 20,   // private - char array
        USER_ATTR_STORAGE_STATE             = 21    // private - char array
    };

    public enum class MPaymentMethod {
        PAYMENT_METHOD_BALANCE = 0,
        PAYMENT_METHOD_PAYPAL = 1,
        PAYMENT_METHOD_ITUNES = 2,
        PAYMENT_METHOD_GOOGLE_WALLET = 3,
        PAYMENT_METHOD_BITCOIN = 4,
        PAYMENT_METHOD_UNIONPAY = 5,
        PAYMENT_METHOD_FORTUMO = 6,
        PAYMENT_METHOD_CREDIT_CARD = 8,
        PAYMENT_METHOD_CENTILI = 9,
        PAYMENT_METHOD_WINDOWS_STORE = 13
    };

    public enum class MPasswordStrength{
        PASSWORD_STRENGTH_VERYWEAK  = 0,
        PASSWORD_STRENGTH_WEAK      = 1,
        PASSWORD_STRENGTH_MEDIUM    = 2,
        PASSWORD_STRENGTH_GOOD      = 3,
        PASSWORD_STRENGTH_STRONG    = 4
    };

    public enum class MRetryReason {
        RETRY_NONE          = 0,
        RETRY_CONNECTIVITY  = 1,
        RETRY_SERVERS_BUSY  = 2,
        RETRY_API_LOCK      = 3,
        RETRY_RATE_LIMIT    = 4,
        RETRY_LOCAL_LOCK    = 5,
        RETRY_IGNORE_FILE   = 6,
        RETRY_UNKNOWN       = 7
    };

    public enum class MStorageState {
        STORAGE_STATE_GREEN     = 0,
        STORAGE_STATE_ORANGE    = 1,
        STORAGE_STATE_RED       = 2,
        STORAGE_STATE_CHANGE    = 3
    };

    public ref class MegaSDK sealed
    {
        friend class DelegateMRequestListener;
        friend class DelegateMGlobalListener;
        friend class DelegateMTransferListener;
        friend class DelegateMListener;

    public:
        MegaSDK(String^ appKey, String^ userAgent, MRandomNumberProvider ^randomProvider);
        MegaSDK(String^ appKey, String^ userAgent, String^ basePath, MRandomNumberProvider^ randomProvider);
        MegaSDK(String^ appKey, String^ userAgent, String^ basePath, MRandomNumberProvider^ randomProvider, MGfxProcessorInterface^ gfxProcessor);
        virtual ~MegaSDK();

        //Multiple listener management.
        void addListener(MListenerInterface^ listener);
        void addRequestListener(MRequestListenerInterface^ listener);
        void addTransferListener(MTransferListenerInterface^ listener);
        void addGlobalListener(MGlobalListenerInterface^ listener);
        void removeListener(MListenerInterface^ listener);
        void removeRequestListener(MRequestListenerInterface^ listener);
        void removeTransferListener(MTransferListenerInterface^ listener);
        void removeGlobalListener(MGlobalListenerInterface^ listener);

        // UTILS

        /**
        * @brief Generates a hash based in the provided private key and email
        *
        * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
        * required to log in, this function allows to do this step in a separate function. You should run this function
        * in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin
        *
        * You take the ownership of the returned value.
        *
        * @param base64pwkey Private key returned by MRequest::getPrivateKey in the onRequestFinish callback of createAccount
        * @param email Email to create the hash
        * @return Base64-encoded hash
        *
        * @deprecated This function is only useful for old accounts. Once enabled the new registration logic,
        * this function will return an empty string for new accounts and will be removed few time after.
        */
        String^ getStringHash(String^ base64pwkey, String^ inBuf);

        void getSessionTransferURL(String^ path, MRequestListenerInterface^ listener);
        static MegaHandle base32ToHandle(String^ base32Handle);
        static uint64 base64ToHandle(String^ base64Handle);
        static String^ handleToBase64(MegaHandle handle);
        static String^ userHandleToBase64(MegaHandle handle);
        void retryPendingConnections(bool disconnect, bool includexfers, MRequestListenerInterface^ listener);
        void retryPendingConnections(bool disconnect, bool includexfers);
        void retryPendingConnections(bool disconnect);
        void retryPendingConnections();
        void reconnect();
        static void setStatsID(String^ id);

        /**
        * @brief Use custom DNS servers
        *
        * The SDK tries to automatically get and use DNS servers configured in the system at startup. This function can be used
        * to override that automatic detection and use a custom list of DNS servers. It is also useful to provide working
        * DNS servers to the SDK in platforms in which it can't get them from the system (Windows Phone and Universal Windows Platform).
        *
        * Since the usage of this function implies a change in DNS servers used by the SDK, all connections are
        * closed and restarted using the new list of new DNS servers, so calling this function too often can cause
        * many retries and problems to complete requests. Please use it only at startup or when DNS servers need to be changed.
        *
        * The associated request type with this request is MRequest::TYPE_RETRY_PENDING_CONNECTIONS.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getText - Returns the new list of DNS servers
        *
        * @param dnsServers New list of DNS servers. It must be a list of IPs separated by a comma character ",".
        * IPv6 servers are allowed (without brackets).
        *
        * The usage of this function will trigger the callback MGlobalListener::onEvent and the callback
        * MListener::onEvent with the event type MEvent::EVENT_DISCONNECT.
        *
        * @param listener MRequestListener to track this request
        */
        void setDnsServers(String^ dnsServers, MRequestListenerInterface^ listener);

        /**
        * @brief Use custom DNS servers
        *
        * The SDK tries to automatically get and use DNS servers configured in the system at startup. This function can be used
        * to override that automatic detection and use a custom list of DNS servers. It is also useful to provide working
        * DNS servers to the SDK in platforms in which it can't get them from the system (Windows Phone and Universal Windows Platform).
        *
        * Since the usage of this function implies a change in DNS servers used by the SDK, all connections are
        * closed and restarted using the new list of new DNS servers, so calling this function too often can cause
        * many retries and problems to complete requests. Please use it only at startup or when DNS servers need to be changed.
        *
        * The associated request type with this request is MRequest::TYPE_RETRY_PENDING_CONNECTIONS.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getText - Returns the new list of DNS servers
        *
        * @param dnsServers New list of DNS servers. It must be a list of IPs separated by a comma character ",".
        * IPv6 servers are allowed (without brackets).
        *
        * The usage of this function will trigger the callback MGlobalListener::onEvent and the callback
        * MListener::onEvent with the event type MEvent::EVENT_DISCONNECT.
        */
        void setDnsServers(String^ dnsServers);

        /**
        * @brief Check if server-side Rubbish Bin autopurging is enabled for the current account
        * @return True if this feature is enabled. Otherwise false.
        */
        bool serverSideRubbishBinAutopurgeEnabled();

        /**
        * @brief Check if multi-factor authentication can be enabled for the current account.
        *
        * It's needed to be logged into an account and with the nodes loaded (login + fetchNodes) before
        * using this function. Otherwise it will always return false.
        *
        * @return True if multi-factor authentication can be enabled for the current account, otherwise false.
        */
        bool multiFactorAuthAvailable();

        /**
         * @brief Check if multi-factor authentication is enabled for an account
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_CHECK
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getEmail - Returns the email sent in the first parameter
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getFlag - Returns true if multi-factor authentication is enabled or false if it's disabled.
         *
         * @param email Email to check
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthCheck(String^ email, MRequestListenerInterface^ listener);

        /**
         * @brief Check if multi-factor authentication is enabled for an account
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_CHECK
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getEmail - Returns the email sent in the first parameter
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getFlag - Returns true if multi-factor authentication is enabled or false if it's disabled.
         *
         * @param email Email to check
         */
        void multiFactorAuthCheck(String^ email);

        /**
         * @brief Get the secret code of the account to enable multi-factor authentication
         * The MegaSDK object must be logged into an account to successfully use this function.
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_GET
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getText - Returns the Base32 secret code needed to configure multi-factor authentication.
         *
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthGetCode(MRequestListenerInterface^ listener);

        /**
         * @brief Get the secret code of the account to enable multi-factor authentication
         * The MegaSDK object must be logged into an account to successfully use this function.
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_GET
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getText - Returns the Base32 secret code needed to configure multi-factor authentication.
         */
        void multiFactorAuthGetCode();

        /**
         * @brief Enable multi-factor authentication for the account
         * The MegaSDK object must be logged into an account to successfully use this function.
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_SET
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getFlag - Returns true
         * - MRequest::getPassword - Returns the pin sent in the first parameter
         *
         * @param pin Valid pin code for multi-factor authentication
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthEnable(String^ pin, MRequestListenerInterface^ listener);

        /**
         * @brief Enable multi-factor authentication for the account
         * The MegaSDK object must be logged into an account to successfully use this function.
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_SET
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getFlag - Returns true
         * - MRequest::getPassword - Returns the pin sent in the first parameter
         *
         * @param pin Valid pin code for multi-factor authentication
         */
        void multiFactorAuthEnable(String^ pin);

        /**
         * @brief Disable multi-factor authentication for the account
         * The MegaSDK object must be logged into an account to successfully use this function.
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_SET
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getFlag - Returns false
         * - MRequest::getPassword - Returns the pin sent in the first parameter
         *
         * @param pin Valid pin code for multi-factor authentication
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthDisable(String^ pin, MRequestListenerInterface^ listener);

        /**
         * @brief Disable multi-factor authentication for the account
         * The MegaSDK object must be logged into an account to successfully use this function.
         *
         * The associated request type with this request is MRequest::TYPE_MULTI_FACTOR_AUTH_SET
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getFlag - Returns false
         * - MRequest::getPassword - Returns the pin sent in the first parameter
         *
         * @param pin Valid pin code for multi-factor authentication
         */
        void multiFactorAuthDisable(String^ pin);

        /**
         * @brief Log in to a MEGA account with multi-factor authentication enabled
         *
         * The associated request type with this request is MRequest::TYPE_LOGIN.
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getEmail - Returns the first parameter
         * - MRequest::getPassword - Returns the second parameter
         * - MRequest::getText - Returns the third parameter
         *
         * If the email/password aren't valid the error code provided in onRequestFinish is
         * MError::API_ENOENT.
         *
         * @param email Email of the user
         * @param password Password
         * @param pin Pin code for multi-factor authentication
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthLogin(String^ email, String^ password, String^ pin, MRequestListenerInterface^ listener);

        /**
         * @brief Log in to a MEGA account with multi-factor authentication enabled
         *
         * The associated request type with this request is MRequest::TYPE_LOGIN.
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getEmail - Returns the first parameter
         * - MRequest::getPassword - Returns the second parameter
         * - MRequest::getText - Returns the third parameter
         *
         * If the email/password aren't valid the error code provided in onRequestFinish is
         * MError::API_ENOENT.
         *
         * @param email Email of the user
         * @param password Password
         * @param pin Pin code for multi-factor authentication
         */
        void multiFactorAuthLogin(String^ email, String^ password, String^ pin);

        /**
         * @brief Change the password of a MEGA account with multi-factor authentication enabled
         *
         * The associated request type with this request is MRequest::TYPE_CHANGE_PW
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getPassword - Returns the old password (if it was passed as parameter)
         * - MRequest::getNewPassword - Returns the new password
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * @param oldPassword Old password
         * @param newPassword New password
         * @param pin Pin code for multi-factor authentication
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthChangePassword(String^ oldPassword, String^ newPassword, String^ pin, MRequestListenerInterface^ listener);

        /**
         * @brief Change the password of a MEGA account with multi-factor authentication enabled
         *
         * The associated request type with this request is MRequest::TYPE_CHANGE_PW
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getPassword - Returns the old password (if it was passed as parameter)
         * - MRequest::getNewPassword - Returns the new password
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * @param oldPassword Old password
         * @param newPassword New password
         * @param pin Pin code for multi-factor authentication
         */
        void multiFactorAuthChangePassword(String^ oldPassword, String^ newPassword, String^ pin);

        /**
         * @brief Change the password of a MEGA account with multi-factor authentication enabled
         *
         * The associated request type with this request is MRequest::TYPE_CHANGE_PW
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getPassword - Returns the old password (if it was passed as parameter)
         * - MRequest::getNewPassword - Returns the new password
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * @param newPassword New password
         * @param pin Pin code for multi-factor authentication
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthChangePasswordWithoutOld(String^ newPassword, String^ pin, MRequestListenerInterface^ listener);

        /**
         * @brief Change the password of a MEGA account with multi-factor authentication enabled
         *
         * The associated request type with this request is MRequest::TYPE_CHANGE_PW
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getPassword - Returns the old password (if it was passed as parameter)
         * - MRequest::getNewPassword - Returns the new password
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * @param newPassword New password
         * @param pin Pin code for multi-factor authentication
         */
        void multiFactorAuthChangePasswordWithoutOld(String^ newPassword, String^ pin);

        /**
         * @brief Initialize the change of the email address associated to an account with multi-factor authentication enabled.
         *
         * The associated request type with this request is MRequest::TYPE_GET_CHANGE_EMAIL_LINK.
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getEmail - Returns the email for the account
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * If this request succeeds, a change-email link will be sent to the specified email address.
         * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
         *
         * @param email The new email to be associated to the account.
         * @param pin Pin code for multi-factor authentication
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthChangeEmail(String^ email, String^ pin, MRequestListenerInterface^ listener);

        /**
         * @brief Initialize the change of the email address associated to an account with multi-factor authentication enabled.
         *
         * The associated request type with this request is MRequest::TYPE_GET_CHANGE_EMAIL_LINK.
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getEmail - Returns the email for the account
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * If this request succeeds, a change-email link will be sent to the specified email address.
         * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
         *
         * @param email The new email to be associated to the account.
         * @param pin Pin code for multi-factor authentication
         */
        void multiFactorAuthChangeEmail(String^ email, String^ pin);

        /**
         * @brief Initialize the cancellation of an account.
         *
         * The associated request type with this request is MRequest::TYPE_GET_CANCEL_LINK.
         *
         * If this request succeeds, a cancellation link will be sent to the email address of the user.
         * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * @see MegaSDK::confirmCancelAccount
         *
         * @param pin Pin code for multi-factor authentication
         * @param listener MRequestListener to track this request
         */
        void multiFactorAuthCancelAccount(String^ pin, MRequestListenerInterface^ listener);

        /**
         * @brief Initialize the cancellation of an account.
         *
         * The associated request type with this request is MRequest::TYPE_GET_CANCEL_LINK.
         *
         * If this request succeeds, a cancellation link will be sent to the email address of the user.
         * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getText - Returns the pin code for multi-factor authentication
         *
         * @see MegaSDK::confirmCancelAccount
         *
         * @param pin Pin code for multi-factor authentication
         */
        void multiFactorAuthCancelAccount(String^ pin);

        /**
        * @brief Fetch details related to time zones and the current default
        *
        * The associated request type with this request is MRequest::TYPE_FETCH_TIMEZONE.
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getMTimeZoneDetails - Returns details about timezones and the current default
        *
        * @param listener MRequestListener to track this request
        */
        void fetchTimeZone(MRequestListenerInterface^ listener);

        /**
        * @brief Fetch details related to time zones and the current default
        *
        * The associated request type with this request is MRequest::TYPE_FETCH_TIMEZONE.
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getMTimeZoneDetails - Returns details about timezones and the current default
        */
        void fetchTimeZone();

        //API REQUESTS

        /**
        * @brief Log in to a MEGA account
        *
        * The associated request type with this request is MRequest::TYPE_LOGIN.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the first parameter
        * - MRequest::getPassword - Returns the second parameter
        *
        * If the email/password aren't valid the error code provided in onRequestFinish is
        * MError::API_ENOENT.
        *
        * @param email Email of the user
        * @param password Password
        * @param listener MRequestListener to track this request
        */
        void login(String^ email, String^ password, MRequestListenerInterface^ listener);

        /**
        * @brief Log in to a MEGA account
        *
        * The associated request type with this request is MRequest::TYPE_LOGIN.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the first parameter
        * - MRequest::getPassword - Returns the second parameter
        *
        * If the email/password aren't valid the error code provided in onRequestFinish is
        * MError::API_ENOENT.
        *
        * @param email Email of the user
        * @param password Password
        */
        void login(String^ email, String^ password);

        String^ getSequenceNumber();
        String^ dumpSession();
        String^ dumpXMPPSession();

        /**
        * @brief Log in to a MEGA account using precomputed keys
        *
        * The associated request type with this request is MRequest::TYPE_LOGIN.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the first parameter
        * - MRequest::getPassword - Returns the second parameter
        * - MRequest::getPrivateKey - Returns the third parameter
        *
        * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
        * MError::API_ENOENT.
        *
        * @param email Email of the user
        * @param stringHash Hash of the email returned by MegaSDK::getStringHash
        * @param base64pwkey Private key returned by MRequest::getPrivateKey in the onRequestFinish callback of createAccount
        * @param listener MRequestListener to track this request
        *
        * @deprecated The parameter stringHash is no longer for new accounts so this function will be replaced by another
        * one soon. Please use MegaSDK::login (with email and password) or MegaSDK::fastLogin (with session) instead when possible.
        */
        void fastLogin(String^ email, String^ stringHash, String^ base64pwkey, MRequestListenerInterface^ listener);
        
        /**
        * @brief Log in to a MEGA account using precomputed keys
        *
        * The associated request type with this request is MRequest::TYPE_LOGIN.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the first parameter
        * - MRequest::getPassword - Returns the second parameter
        * - MRequest::getPrivateKey - Returns the third parameter
        *
        * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
        * MError::API_ENOENT.
        *
        * @param email Email of the user
        * @param stringHash Hash of the email returned by MegaSDK::getStringHash
        * @param base64pwkey Private key returned by MRequest::getPrivateKey in the onRequestFinish callback of createAccount
        *
        * @deprecated The parameter stringHash is no longer for new accounts so this function will be replaced by another
        * one soon. Please use MegaSDK::login (with email and password) or MegaSDK::fastLogin (with session) instead when possible.
        */
        void fastLogin(String^ email, String^ stringHash, String^ base64pwkey);

        /**
        * @brief Log in to a MEGA account using a session key
        *
        * The associated request type with this request is MRequest::TYPE_LOGIN.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getSessionKey - Returns the session key
        *
        * @param session Session key previously dumped with MegaSDK::dumpSession
        * @param listener MRequestListener to track this request
        */
        void fastLogin(String^ session, MRequestListenerInterface^ listener);

        /**
        * @brief Log in to a MEGA account using a session key
        *
        * The associated request type with this request is MRequest::TYPE_LOGIN.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getSessionKey - Returns the session key
        *
        * @param session Session key previously dumped with MegaSDK::dumpSession
        */
        void fastLogin(String^ session);
        
        void killSession(MegaHandle sessionHandle, MRequestListenerInterface^ listener);
        void killSession(MegaHandle sessionHandle);
        void killAllSessions(MRequestListenerInterface^ listener);
        void killAllSessions();
        void getOwnUserData(MRequestListenerInterface^ listener);
        void getOwnUserData();
        void getUserData(MUser^ user, MRequestListenerInterface^ listener);
        void getUserData(MUser^ user);
        void getUserDataById(String^ user, MRequestListenerInterface^ listener);
        void getUserDataById(String^ user);
        String^ getAccountAuth();
        void setAccountAuth(String^ auth);

        /**
        * @brief Initialize the creation of a new MEGA account, with firstname and lastname
        *
        * The associated request type with this request is MRequest::TYPE_CREATE_ACCOUNT.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the email for the account
        * - MRequest::getPassword - Returns the password for the account
        * - MRequest::getName - Returns the firstname of the user
        * - MRequest::getText - Returns the lastname of the user
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getSessionKey - Returns the session id to resume the process
        *
        * If this request succeeds, a new ephemeral session will be created for the new user
        * and a confirmation email will be sent to the specified email address. The app may
        * resume the create-account process by using MegaSDK::resumeCreateAccount.
        *
        * If an account with the same email already exists, you will get the error code
        * MError::API_EEXIST in onRequestFinish
        *
        * @param email Email for the account
        * @param password Password for the account
        * @param firstname Firstname of the user
        * @param lastname Lastname of the user
        * @param listener MRequestListener to track this request
        */
        void createAccount(String^ email, String^ password, String^ firstname, String^ lastname, MRequestListenerInterface^ listener);

        /**
        * @brief Initialize the creation of a new MEGA account, with firstname and lastname
        *
        * The associated request type with this request is MRequest::TYPE_CREATE_ACCOUNT.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the email for the account
        * - MRequest::getPassword - Returns the password for the account
        * - MRequest::getName - Returns the firstname of the user
        * - MRequest::getText - Returns the lastname of the user
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getSessionKey - Returns the session id to resume the process
        *
        * If this request succeeds, a new ephemeral session will be created for the new user
        * and a confirmation email will be sent to the specified email address. The app may
        * resume the create-account process by using MegaSDK::resumeCreateAccount.
        *
        * If an account with the same email already exists, you will get the error code
        * MError::API_EEXIST in onRequestFinish
        *
        * @param email Email for the account
        * @param password Password for the account
        * @param firstname Firstname of the user
        * @param lastname Lastname of the user
        */
        void createAccount(String^ email, String^ password, String^ firstname, String^ lastname);

        /**
        * @brief Resume a registration process
        *
        * When a user begins the account registration process by calling MegaSDK::createAccount,
        * an ephemeral account is created.
        *
        * Until the user successfully confirms the signup link sent to the provided email address,
        * you can resume the ephemeral session in order to change the email address, resend the
        * signup link (@see MegaSDK::sendSignupLink) and also to receive notifications in case the
        * user confirms the account using another client (MGlobalListener::onAccountUpdate or
        * MListener::onAccountUpdate).
        *
        * The associated request type with this request is MRequest::TYPE_CREATE_ACCOUNT.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getSessionKey - Returns the session id to resume the process
        * - MRequest::getParamType - Returns the value 1
        *
        * In case the account is already confirmed, the associated request will fail with
        * error MError::API_EARGS.
        *
        * @param sid Session id valid for the ephemeral account (@see MegaSDK::createAccount)
        * @param listener MRequestListener to track this request
        */
        void resumeCreateAccount(String^ sid, MRequestListenerInterface^ listener);

        /**
        * @brief Resume a registration process
        *
        * When a user begins the account registration process by calling MegaSDK::createAccount,
        * an ephemeral account is created.
        *
        * Until the user successfully confirms the signup link sent to the provided email address,
        * you can resume the ephemeral session in order to change the email address, resend the
        * signup link (@see MegaSDK::sendSignupLink) and also to receive notifications in case the
        * user confirms the account using another client (MGlobalListener::onAccountUpdate or
        * MListener::onAccountUpdate).
        *
        * The associated request type with this request is MRequest::TYPE_CREATE_ACCOUNT.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getSessionKey - Returns the session id to resume the process
        * - MRequest::getParamType - Returns the value 1
        *
        * In case the account is already confirmed, the associated request will fail with
        * error MError::API_EARGS.
        *
        * @param sid Session id valid for the ephemeral account (@see MegaSDK::createAccount)
        */
        void resumeCreateAccount(String^ sid);
        
        /**
        * @brief Sends the confirmation email for a new account
        *
        * This function is useful to send the confirmation link again or to send it to a different
        * email address, in case the user mistyped the email at the registration form.
        *
        * @param email Email for the account
        * @param name Firstname of the user
        * @param password Password for the account
        * @param listener MRequestListener to track this request
        */
        void sendSignupLink(String^ email, String^ name, String^ password, MRequestListenerInterface^ listener);
        
        /**
        * @brief Sends the confirmation email for a new account
        *
        * This function is useful to send the confirmation link again or to send it to a different
        * email address, in case the user mistyped the email at the registration form.
        *
        * @param email Email for the account
        * @param name Firstname of the user
        * @param password Password for the account
        */
        void sendSignupLink(String^ email, String^ name, String^ password);
        
        /**
        * @brief Sends the confirmation email for a new account
        *
        * This function is useful to send the confirmation link again or to send it to a different
        * email address, in case the user mistyped the email at the registration form.
        *
        * @param email Email for the account
        * @param name Firstname of the user
        * @param base64pwkey Private key returned by MRequest::getPrivateKey in the onRequestFinish callback of createAccount
        * @param listener MRequestListener to track this request
        *
        * @deprecated This function only works using the old registration method and will be removed soon.
        * Please use MegaSDK::sendSignupLink (with email and password) instead.
        */
        void fastSendSignupLink(String^ email, String^ base64pwkey, String^ name, MRequestListenerInterface^ listener);
        
        /**
        * @brief Sends the confirmation email for a new account
        *
        * This function is useful to send the confirmation link again or to send it to a different
        * email address, in case the user mistyped the email at the registration form.
        *
        * @param email Email for the account
        * @param name Firstname of the user
        * @param base64pwkey Private key returned by MRequest::getPrivateKey in the onRequestFinish callback of createAccount
        *
        * @deprecated This function only works using the old registration method and will be removed soon.
        * Please use MegaSDK::sendSignupLink (with email and password) instead.
        */
        void fastSendSignupLink(String^ email, String^ base64pwkey, String^ name);
        
        /**
        * @brief Get information about a confirmation link or a new signup link
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_SIGNUP_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the confirmation link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getName - Returns the name associated with the link (available only for confirmation links)
        * - MRequest::getFlag - Returns true if the account was automatically confirmed, otherwise false
        *
        * If MRequest::getFlag returns true, the account was automatically confirmed and it's not needed
        * to call MegaSDK::confirmAccount. If it returns false, it's needed to call MegaSDK::confirmAccount
        * as usual. New accounts do not require a confirmation with the password, but old confirmation links
        * require it, so it's needed to check that parameter in onRequestFinish to know how to proceed.
        *
        * @param link Confirmation link (#confirm) or new signup link (#newsignup)
        * @param listener MRequestListener to track this request
        */
        void querySignupLink(String^ link, MRequestListenerInterface^ listener);

        /**
        * @brief Get information about a confirmation link or a new signup link
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_SIGNUP_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the confirmation link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getName - Returns the name associated with the link (available only for confirmation links)
        * - MRequest::getFlag - Returns true if the account was automatically confirmed, otherwise false
        *
        * If MRequest::getFlag returns true, the account was automatically confirmed and it's not needed
        * to call MegaSDK::confirmAccount. If it returns false, it's needed to call MegaSDK::confirmAccount
        * as usual. New accounts do not require a confirmation with the password, but old confirmation links
        * require it, so it's needed to check that parameter in onRequestFinish to know how to proceed.
        *
        * @param link Confirmation link (#confirm) or new signup link (#newsignup)
        */
        void querySignupLink(String^ link);

        /**
        * @brief Confirm a MEGA account using a confirmation link and the user password
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_ACCOUNT
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getLink - Returns the confirmation link
        * - MRequest::getPassword - Returns the password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Email of the account
        * - MRequest::getName - Name of the user
        *
        * As a result of a successfull confirmation, the app will receive the callback
        * MListener::onEvent and MGlobalListener::onEvent with an event of type
        * MEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
        * the account by checking MEvent::getText. @see MListener::onEvent.
        *
        * @param link Confirmation link
        * @param password Password of the account
        * @param listener MRequestListener to track this request
        */
        void confirmAccount(String^ link, String^ password, MRequestListenerInterface^ listener);

        /**
        * @brief Confirm a MEGA account using a confirmation link and the user password
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_ACCOUNT
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getLink - Returns the confirmation link
        * - MRequest::getPassword - Returns the password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Email of the account
        * - MRequest::getName - Name of the user
        *
        * As a result of a successfull confirmation, the app will receive the callback
        * MListener::onEvent and MGlobalListener::onEvent with an event of type
        * MEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
        * the account by checking MEvent::getText. @see MListener::onEvent.
        *
        * @param link Confirmation link
        * @param password Password of the account
        */
        void confirmAccount(String^ link, String^ password);

        /**
        * @brief Confirm a MEGA account using a confirmation link and a precomputed key
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_ACCOUNT
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getLink - Returns the confirmation link
        * - MRequest::getPrivateKey - Returns the base64pwkey parameter
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Email of the account
        * - MRequest::getName - Name of the user
        *
        * As a result of a successfull confirmation, the app will receive the callback
        * MListener::onEvent and MGlobalListener::onEvent with an event of type
        * MEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
        * the account by checking MEvent::getText. @see MListener::onEvent.
        *
        * @param link Confirmation link
        * @param base64pwkey Private key precomputed with MegaSDK::getBase64PwKey
        * @param listener MRequestListener to track this request
        *
        * @deprecated This function only works using the old registration method and will be removed soon.
        * Please use MegaSDK::confirmAccount instead.
        */
        void fastConfirmAccount(String^ link, String^ base64pwkey, MRequestListenerInterface^ listener);

        /**
        * @brief Confirm a MEGA account using a confirmation link and a precomputed key
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_ACCOUNT
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getLink - Returns the confirmation link
        * - MRequest::getPrivateKey - Returns the base64pwkey parameter
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Email of the account
        * - MRequest::getName - Name of the user
        *
        * As a result of a successfull confirmation, the app will receive the callback
        * MListener::onEvent and MGlobalListener::onEvent with an event of type
        * MEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
        * the account by checking MEvent::getText. @see MListener::onEvent.
        *
        * @param link Confirmation link
        * @param base64pwkey Private key precomputed with MegaSDK::getBase64PwKey
        *
        * @deprecated This function only works using the old registration method and will be removed soon.
        * Please use MegaSDK::confirmAccount instead.
        */
        void fastConfirmAccount(String^ link, String^ base64pwkey);

        /**
        * @brief Initialize the reset of the existing password, with and without the Master Key.
        *
        * The associated request type with this request is MRequest::TYPE_GET_RECOVERY_LINK.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the email for the account
        * - MRequest::getFlag - Returns whether the user has a backup of the master key or not.
        *
        * If this request succeeds, a recovery link will be sent to the user.
        * If no account is registered under the provided email, you will get the error code
        * MError::API_ENOENT in onRequestFinish
        *
        * @param email Email used to register the account whose password wants to be reset.
        * @param hasMasterKey True if the user has a backup of the master key. Otherwise, false.
        * @param listener MRequestListener to track this request
        */
        void resetPassword(String^ email, bool hasMasterKey, MRequestListenerInterface^ listener);

        /**
        * @brief Initialize the reset of the existing password, with and without the Master Key.
        *
        * The associated request type with this request is MRequest::TYPE_GET_RECOVERY_LINK.
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getEmail - Returns the email for the account
        * - MRequest::getFlag - Returns whether the user has a backup of the master key or not.
        *
        * If this request succeeds, a recovery link will be sent to the user.
        * If no account is registered under the provided email, you will get the error code
        * MError::API_ENOENT in onRequestFinish
        *
        * @param email Email used to register the account whose password wants to be reset.
        * @param hasMasterKey True if the user has a backup of the master key. Otherwise, false.
        */
        void resetPassword(String^ email, bool hasMasterKey);

        /**
        * @brief Get information about a recovery link created by MegaSDK::resetPassword.
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getFlag - Return whether the link requires masterkey to reset password.
        *
        * @param link Recovery link (#recover)
        * @param listener MRequestListener to track this request
        */
        void queryResetPasswordLink(String^ link, MRequestListenerInterface^ listener);

        /**
        * @brief Get information about a recovery link created by MegaSDK::resetPassword.
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getFlag - Return whether the link requires masterkey to reset password.
        *
        * @param link Recovery link (#recover)
        */
        void queryResetPasswordLink(String^ link);

        /**
        * @brief Set a new password for the account pointed by the recovery link.
        *
        * Recovery links are created by calling MegaSDK::resetPassword and may or may not
        * require to provide the Master Key.
        *
        * @see The flag of the MRequest::TYPE_QUERY_RECOVERY_LINK in MegaSDK::queryResetPasswordLink.
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        * - MRequest::getPassword - Returns the new password
        * - MRequest::getPrivateKey - Returns the Master Key
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getFlag - Return whether the link requires masterkey to reset password.
        *
        * @param link The recovery link sent to the user's email address.
        * @param newPwd The new password to be set.
        * @param masterKey Base64-encoded string containing the master key.
        * @param listener MRequestListener to track this request
        */
        void confirmResetPassword(String^ link, String^ newPwd, String^ masterKey, MRequestListenerInterface^ listener);

        /**
        * @brief Set a new password for the account pointed by the recovery link.
        *
        * Recovery links are created by calling MegaSDK::resetPassword and may or may not
        * require to provide the Master Key.
        *
        * @see The flag of the MRequest::TYPE_QUERY_RECOVERY_LINK in MegaSDK::queryResetPasswordLink.
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        * - MRequest::getPassword - Returns the new password
        * - MRequest::getPrivateKey - Returns the Master Key
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getFlag - Return whether the link requires masterkey to reset password.
        *
        * @param link The recovery link sent to the user's email address.
        * @param newPwd The new password to be set.
        * @param masterKey Base64-encoded string containing the master key.
        */
        void confirmResetPassword(String^ link, String^ newPwd, String^ masterKey);

        /**
        * @brief Set a new password for the account pointed by the recovery link.
        *
        * Recovery links are created by calling MegaSDK::resetPassword and may or may not
        * require to provide the Master Key.
        *
        * @see The flag of the MRequest::TYPE_QUERY_RECOVERY_LINK in MegaSDK::queryResetPasswordLink.
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        * - MRequest::getPassword - Returns the new password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getFlag - Return whether the link requires masterkey to reset password.
        *
        * @param link The recovery link sent to the user's email address.
        * @param newPwd The new password to be set.
        * @param listener MRequestListener to track this request
        */
        void confirmResetPasswordWithoutMasterKey(String^ link, String^ newPwd, MRequestListenerInterface^ listener);

        /**
        * @brief Set a new password for the account pointed by the recovery link.
        *
        * Recovery links are created by calling MegaSDK::resetPassword and may or may not
        * require to provide the Master Key.
        *
        * @see The flag of the MRequest::TYPE_QUERY_RECOVERY_LINK in MegaSDK::queryResetPasswordLink.
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        * - MRequest::getPassword - Returns the new password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        * - MRequest::getFlag - Return whether the link requires masterkey to reset password.
        *
        * @param link The recovery link sent to the user's email address.
        * @param newPwd The new password to be set.
        */
        void confirmResetPasswordWithoutMasterKey(String^ link, String^ newPwd);

        /**
        * @brief Initialize the cancellation of an account.
        *
        * The associated request type with this request is MRequest::TYPE_GET_CANCEL_LINK.
        *
        * If this request succeeds, a cancellation link will be sent to the email address of the user.
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * @see MegaSDK::confirmCancelAccount
        *
        * @param listener MRequestListener to track this request
        */
        void cancelAccount(MRequestListenerInterface^ listener);

        /**
        * @brief Initialize the cancellation of an account.
        *
        * The associated request type with this request is MRequest::TYPE_GET_CANCEL_LINK.
        *
        * If this request succeeds, a cancellation link will be sent to the email address of the user.
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * @see MegaSDK::confirmCancelAccount
        */
        void cancelAccount();

        /**
        * @brief Get information about a cancel link created by MegaSDK::cancelAccount.
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the cancel link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Cancel link (#cancel)
        * @param listener MRequestListener to track this request
        */
        void queryCancelLink(String^ link, MRequestListenerInterface^ listener);

        /**
        * @brief Get information about a cancel link created by MegaSDK::cancelAccount.
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the cancel link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Cancel link (#cancel)
        */
        void queryCancelLink(String^ link);

        /**
        * @brief Effectively parks the user's account without creating a new fresh account.
        *
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * The contents of the account will then be purged after 60 days. Once the account is
        * parked, the user needs to contact MEGA support to restore the account.
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_CANCEL_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        * - MRequest::getPassword - Returns the new password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Cancellation link sent to the user's email address;
        * @param pwd Password for the account.
        * @param listener MRequestListener to track this request
        */
        void confirmCancelAccount(String^ link, String^ pwd, MRequestListenerInterface^ listener);

        /**
        * @brief Effectively parks the user's account without creating a new fresh account.
        *
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * The contents of the account will then be purged after 60 days. Once the account is
        * parked, the user needs to contact MEGA support to restore the account.
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_CANCEL_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the recovery link
        * - MRequest::getPassword - Returns the new password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Cancellation link sent to the user's email address;
        * @param pwd Password for the account.
        */
        void confirmCancelAccount(String^ link, String^ pwd);

        /**
        * @brief Initialize the change of the email address associated to the account.
        *
        * The associated request type with this request is MRequest::TYPE_GET_CHANGE_EMAIL_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getEmail - Returns the email for the account
        *
        * If this request succeeds, a change-email link will be sent to the specified email address.
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * @param email The new email to be associated to the account.
        * @param listener MRequestListener to track this request
        */
        void changeEmail(String^ email, MRequestListenerInterface^ listener);
        
        /**
        * @brief Initialize the change of the email address associated to the account.
        *
        * The associated request type with this request is MRequest::TYPE_GET_CHANGE_EMAIL_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getEmail - Returns the email for the account
        *
        * If this request succeeds, a change-email link will be sent to the specified email address.
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * @param email The new email to be associated to the account.
        */
        void changeEmail(String^ email);

        /**
        * @brief Get information about a change-email link created by MegaSDK::changeEmail.
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the change-email link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Change-email link (#verify)
        * @param listener MRequestListener to track this request
        */
        void queryChangeEmailLink(String^ link, MRequestListenerInterface^ listener);
        
        /**
        * @brief Get information about a change-email link created by MegaSDK::changeEmail.
        *
        * The associated request type with this request is MRequest::TYPE_QUERY_RECOVERY_LINK
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the change-email link
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Change-email link (#verify)
        */
        void queryChangeEmailLink(String^ link);

        /**
        * @brief Effectively changes the email address associated to the account.
        *
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the change-email link
        * - MRequest::getPassword - Returns the password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Change-email link sent to the user's email address.
        * @param pwd Password for the account.
        * @param listener MRequestListener to track this request
        */
        void confirmChangeEmail(String^ link, String^ pwd, MRequestListenerInterface^ listener);

        /**
        * @brief Effectively changes the email address associated to the account.
        *
        * If no user is logged in, you will get the error code MError::API_EACCESS in onRequestFinish().
        *
        * The associated request type with this request is MRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK.
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getLink - Returns the change-email link
        * - MRequest::getPassword - Returns the password
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Return the email associated with the link
        *
        * @param link Change-email link sent to the user's email address.
        * @param pwd Password for the account.
        */
        void confirmChangeEmail(String^ link, String^ pwd);

        /**
        * @brief Check if the MegaApi object is logged in
        * @return 0 if not logged in, Otherwise, a number >= 0
        */
        int isLoggedIn();

        /**
         * @brief Create a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_CREATE.
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getFlag - Returns the value of \c renew parameter
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getNodeHandle - Return the handle of the new contact link
         *
         * @param renew YES to invalidate the previous contact link (if any).
         * @param listener MRequestListener to track this request
         */
        void contactLinkCreateRenew(bool renew, MRequestListenerInterface^ listener);

        /**
         * @brief Create a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_CREATE.
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getFlag - Returns the value of \c renew parameter
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getNodeHandle - Return the handle of the new contact link
         *
         * @param renew YES to invalidate the previous contact link (if any).
         */
        void contactLinkCreateRenew(bool renew);

        /**
         * @brief Get information about a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_QUERY.
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getNodeHandle - Returns the handle of the contact link
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getParentHandle - Returns the userhandle of the contact
         * - MRequest::getEmail - Returns the email of the contact
         * - MRequest::getName - Returns the first name of the contact
         * - MRequest::getText - Returns the last name of the contact
         * - MRequest::getFile - Returns the avatar of the contact (JPG with Base64 encoding)
         *
         * @param handle Handle of the contact link to check
         * @param listener MRequestListener to track this request
         */
        void contactLinkQuery(MegaHandle handle, MRequestListenerInterface^ listener);
        
        /**
         * @brief Get information about a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_QUERY.
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getNodeHandle - Returns the handle of the contact link
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getParentHandle - Returns the userhandle of the contact
         * - MRequest::getEmail - Returns the email of the contact
         * - MRequest::getName - Returns the first name of the contact
         * - MRequest::getText - Returns the last name of the contact
         * - MRequest::getFile - Returns the avatar of the contact (JPG with Base64 encoding)
         *
         * @param handle Handle of the contact link to check
         */
        void contactLinkQuery(MegaHandle handle);

        /**
         * @brief Delete a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_DELETE.
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getNodeHandle - Returns the handle of the contact link
         *
         * @param handle Handle of the contact link to delete
         * If the parameter is INVALID_HANDLE, the active contact link is deleted
         *
         * @param listener MRequestListener to track this request
         */
        void contactLinkDelete(MegaHandle handle, MRequestListenerInterface^ listener);

        /**
         * @brief Delete a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_DELETE.
         *
         * Valid data in the MRequest object received on all callbacks:
         * - MRequest::getNodeHandle - Returns the handle of the contact link
         *
         * @param handle Handle of the contact link to delete
         * If the parameter is INVALID_HANDLE, the active contact link is deleted
         */
        void contactLinkDelete(MegaHandle handle);

        /**
        * @brief Delete the active contact link
        *
        * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_DELETE.
        *
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the contact link
        *
        * @param listener MRequestListener to track this request
        */
        void contactLinkDeleteActive(MRequestListenerInterface^ listener);
        
        /**
        * @brief Delete the active contact link
        *
        * The associated request type with this request is MRequestType::TYPE_CONTACT_LINK_DELETE.
        *
        * Valid data in the MRequest object received on all callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the contact link
        */
        void contactLinkDeleteActive();

        /**
        * @brief Get the next PSA (Public Service Announcement) that should be shown to the user
        *
        * After the PSA has been accepted or dismissed by the user, app should
        * use MegaSDK::setPSA to notify API servers about this event and
        * do not get the same PSA again in the next call to this function.
        *
        * The associated request type with this request is MRequest::TYPE_GET_PSA.
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getNumber - Returns the id of the PSA (useful to call MegaSDK::setPSA later)
        * - MRequest::getName - Returns the title of the PSA
        * - MRequest::getText - Returns the text of the PSA
        * - MRequest::getFile - Returns the URL of the image of the PSA
        * - MRequest::getPassword - Returns the text for the possitive button (or an empty string)
        * - MRequest::getLink - Returns the link for the possitive button (or an empty string)
        *
        * If there isn't any new PSA to show, onRequestFinish will be called with the error
        * code MError::API_ENOENT
        *
        * @param listener MRequestListener to track this request
        * @see MegaSDK::setPSA
        */
        void getPSA(MRequestListenerInterface^ listener);

        /**
        * @brief Get the next PSA (Public Service Announcement) that should be shown to the user
        *
        * After the PSA has been accepted or dismissed by the user, app should
        * use MegaSDK::setPSA to notify API servers about this event and
        * do not get the same PSA again in the next call to this function.
        *
        * The associated request type with this request is MRequest::TYPE_GET_PSA.
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getNumber - Returns the id of the PSA (useful to call MegaSDK::setPSA later)
        * - MRequest::getName - Returns the title of the PSA
        * - MRequest::getText - Returns the text of the PSA
        * - MRequest::getFile - Returns the URL of the image of the PSA
        * - MRequest::getPassword - Returns the text for the possitive button (or an empty string)
        * - MRequest::getLink - Returns the link for the possitive button (or an empty string)
        *
        * If there isn't any new PSA to show, onRequestFinish will be called with the error
        * code MError::API_ENOENT
        *
        * @see MegaSDK::setPSA
        */
        void getPSA();

        /**
        * @brief Notify API servers that a PSA (Public Service Announcement) has been already seen
        *
        * The associated request type with this request is MRequest::TYPE_SET_ATTR_USER.
        *
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the value MUserAttrType::USER_ATTR_LAST_PSA
        * - MRequest::getText - Returns the id passed in the first parameter (as a string)
        *
        * @param id Identifier of the PSA
        * @param listener MRequestListener to track this request
        *
        * @see MegaSDK::getPSA
        */
        void setPSA(int id, MRequestListenerInterface^ listener);

        /**
        * @brief Notify API servers that a PSA (Public Service Announcement) has been already seen
        *
        * The associated request type with this request is MRequest::TYPE_SET_ATTR_USER.
        *
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the value MUserAttrType::USER_ATTR_LAST_PSA
        * - MRequest::getText - Returns the id passed in the first parameter (as a string)
        *
        * @param id Identifier of the PSA
        *
        * @see MegaSDK::getPSA
        */
        void setPSA(int id);

        /**
        * @brief Command to acknowledge user alerts.
        *
        * Other clients will be notified that alerts to this point have been seen.
        *
        * @param listener MRequestListener to track this request
        *
        * @see MegaSDK::getUserAlerts
        */
        void acknowledgeUserAlerts(MRequestListenerInterface^ listener);

        /**
        * @brief Command to acknowledge user alerts.
        *
        * Other clients will be notified that alerts to this point have been seen.
        *
        * @see MegaSDK::getUserAlerts
        */
        void acknowledgeUserAlerts();

        String^ getMyEmail();
        String^ getMyUserHandle();
        MegaHandle getMyUserHandleBinary();
        MUser^ getMyUser();
        bool isAchievementsEnabled();

        /**
        * @brief Check if the password is correct for the current account
        * @param password Password to check
        * @return True if the password is correct for the current account, otherwise false.
        */
        bool checkPassword(String^ password);

        //Logging
        static void setLogLevel(MLogLevel logLevel);
        void addLoggerObject(MLoggerInterface^ logger);
        void removeLoggerObject(MLoggerInterface^ logger);
        static void log(MLogLevel logLevel, String^ message, String^ filename, int line);
        static void log(MLogLevel logLevel, String^ message, String^ filename);
        static void log(MLogLevel logLevel, String^ message);

        void createFolder(String^ name, MNode^ parent, MRequestListenerInterface^ listener);
        void createFolder(String^ name, MNode^ parent);
        bool createLocalFolder(String^ localPath);
        void moveNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener);
        void moveNode(MNode^ node, MNode^ newParent);
        void copyNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener);
        void copyNode(MNode^ node, MNode^ newParent);
        void copyAndRenameNode(MNode^ node, MNode^ newParent, String^ newName, MRequestListenerInterface^ listener);
        void copyAndRenameNode(MNode^ node, MNode^ newParent, String^ newName);
        void renameNode(MNode^ node, String^ newName, MRequestListenerInterface^ listener);
        void renameNode(MNode^ node, String^ newName);
        void remove(MNode^ node, MRequestListenerInterface^ listener);
        void remove(MNode^ node);
        void cleanRubbishBin(MRequestListenerInterface^ listener);
        void cleanRubbishBin();
        void sendFileToUser(MNode^ node, MUser^ user, MRequestListenerInterface^ listener);
        void sendFileToUser(MNode^ node, MUser^ user);
        void sendFileToUserByEmail(MNode^ node, String^ email, MRequestListenerInterface^ listener);
        void sendFileToUserByEmail(MNode^ node, String^ email);
        void share(MNode^ node, MUser^ user, int level, MRequestListenerInterface^ listener);
        void share(MNode^ node, MUser^ user, int level);
        void shareByEmail(MNode^ node, String^ email, int level, MRequestListenerInterface^ listener);
        void shareByEmail(MNode^ node, String^ email, int level);
        void loginToFolder(String^ megaFolderLink, MRequestListenerInterface^ listener);
        void loginToFolder(String^ megaFolderLink);
        void importFileLink(String^ megaFileLink, MNode^ parent, MRequestListenerInterface^ listener);
        void importFileLink(String^ megaFileLink, MNode^ parent);
        void decryptPasswordProtectedLink(String^ link, String^ password, MRequestListenerInterface^ listener);
        void decryptPasswordProtectedLink(String^ link, String^ password);
        void encryptLinkWithPassword(String^ link, String^ password, MRequestListenerInterface^ listener);
        void encryptLinkWithPassword(String^ link, String^ password);
        void getPublicNode(String^ megaFileLink, MRequestListenerInterface^ listener);
        void getPublicNode(String^ megaFileLink);
        void getThumbnail(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener);
        void getThumbnail(MNode^ node, String^ dstFilePath);
        void cancelGetThumbnail(MNode^ node, MRequestListenerInterface^ listener);
        void cancelGetThumbnail(MNode^ node);
        void setThumbnail(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener);
        void setThumbnail(MNode^ node, String^ srcFilePath);
        void getPreview(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener);
        void getPreview(MNode^ node, String^ dstFilePath);
        void cancelGetPreview(MNode^ node, MRequestListenerInterface^ listener);
        void cancelGetPreview(MNode^ node);
        void setPreview(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener);
        void setPreview(MNode^ node, String^ srcFilePath);
        void getUserAvatar(MUser^ user, String^ dstFilePath, MRequestListenerInterface^ listener);
        void getUserAvatar(MUser^ user, String^ dstFilePath);
        void getOwnUserAvatar(String^ dstFilePath, MRequestListenerInterface^ listener);
        void getOwnUserAvatar(String^ dstFilePath);
        void setAvatar(String ^dstFilePath, MRequestListenerInterface^ listener);
        void setAvatar(String ^dstFilePath);
        String^ getUserAvatarColor(MUser^ user);
        String^ getUserHandleAvatarColor(String^ userhandle);

        /**
        * @brief Get an attribute of a MUser.
        *
        * User attributes can be private or public. Private attributes are accessible only by
        * your own user, while public ones are retrievable by any of your contacts.
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getText - Returns the value for public attributes
        * - MRequest::getMegaStringMap - Returns the value for private attributes
        *
        * @param user MUser to get the attribute. If this parameter is set to NULL, the attribute
        * is obtained for the active account
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Get the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Get the lastname of the user (public)
        * MUserAttrType::USER_ATTR_AUTHRING = 3
        * Get the authentication ring of the user (private)
        * MUserAttrType::USER_ATTR_LAST_INTERACTION = 4
        * Get the last interaction of the contacts of the user (private)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Get the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Get the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_KEYRING = 7
        * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
        * MUserAttrType::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
        * Get the signature of RSA public key of the user (public)
        * MUserAttrType::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
        * Get the signature of Cu25519 public key of the user (public)
        * MUserAttrType::USER_ATTR_LANGUAGE = 14
        * Get the preferred language of the user (private, non-encrypted)
        * MUserAttrType::USER_ATTR_PWD_REMINDER = 15
        * Get the password-reminder-dialog information (private, non-encrypted)
        * MUserAttrType::USER_ATTR_DISABLE_VERSIONS = 16
        * Get whether user has versions disabled or enabled (private, non-encrypted)
        * MUserAttrType::USER_ATTR_RICH_PREVIEWS = 18
        * Get whether user generates rich-link messages or not (private)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        * MUserAttrType::USER_ATTR_STORAGE_STATE = 21
        * Get the state of the storage (private non-encrypted)
        *
        * @param listener MRequestListener to track this request
        */
        void getUserAttribute(MUser^ user, int type, MRequestListenerInterface^ listener);

        /**
        * @brief Get an attribute of a MUser.
        *
        * User attributes can be private or public. Private attributes are accessible only by
        * your own user, while public ones are retrievable by any of your contacts.
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getText - Returns the value for public attributes
        * - MRequest::getMegaStringMap - Returns the value for private attributes
        *
        * @param user MUser to get the attribute. If this parameter is set to NULL, the attribute
        * is obtained for the active account
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Get the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Get the lastname of the user (public)
        * MUserAttrType::USER_ATTR_AUTHRING = 3
        * Get the authentication ring of the user (private)
        * MUserAttrType::USER_ATTR_LAST_INTERACTION = 4
        * Get the last interaction of the contacts of the user (private)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Get the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Get the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_KEYRING = 7
        * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
        * MUserAttrType::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
        * Get the signature of RSA public key of the user (public)
        * MUserAttrType::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
        * Get the signature of Cu25519 public key of the user (public)
        * MUserAttrType::USER_ATTR_LANGUAGE = 14
        * Get the preferred language of the user (private, non-encrypted)
        * MUserAttrType::USER_ATTR_PWD_REMINDER = 15
        * Get the password-reminder-dialog information (private, non-encrypted)
        * MUserAttrType::USER_ATTR_DISABLE_VERSIONS = 16
        * Get whether user has versions disabled or enabled (private, non-encrypted)
        * MUserAttrType::USER_ATTR_RICH_PREVIEWS = 18
        * Get whether user generates rich-link messages or not (private)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        * MUserAttrType::USER_ATTR_STORAGE_STATE = 21
        * Get the state of the storage (private non-encrypted)
        */
        void getUserAttribute(MUser^ user, int type);

        /**
        * @brief Get an attribute of any user in MEGA.
        *
        * User attributes can be private or public. Private attributes are accessible only by
        * your own user, while public ones are retrievable by any of your contacts.
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        * - MRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getText - Returns the value for public attributes
        * - MRequest::getMegaStringMap - Returns the value for private attributes
        *
        * @param email_or_handle Email or user handle (Base64 encoded) to get the attribute.
        * If this parameter is set to NULL, the attribute is obtained for the active account.
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Get the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Get the lastname of the user (public)
        * MUserAttrType::USER_ATTR_AUTHRING = 3
        * Get the authentication ring of the user (private)
        * MUserAttrType::USER_ATTR_LAST_INTERACTION = 4
        * Get the last interaction of the contacts of the user (private)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Get the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Get the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_KEYRING = 7
        * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
        * MUserAttrType::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
        * Get the signature of RSA public key of the user (public)
        * MUserAttrType::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
        * Get the signature of Cu25519 public key of the user (public)
        * MUserAttrType::USER_ATTR_LANGUAGE = 14
        * Get the preferred language of the user (private, non-encrypted)
        * MUserAttrType::USER_ATTR_PWD_REMINDER = 15
        * Get the password-reminder-dialog information (private, non-encrypted)
        * MUserAttrType::USER_ATTR_DISABLE_VERSIONS = 16
        * Get whether user has versions disabled or enabled (private, non-encrypted)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        * MUserAttrType::USER_ATTR_STORAGE_STATE = 21
        * Get the state of the storage (private non-encrypted)
        *
        * @param listener MRequestListener to track this request
        */
        void getUserAttributeByEmailOrHandle(String^ email_or_handle, int type, MRequestListenerInterface^ listener);

        /**
        * @brief Get an attribute of any user in MEGA.
        *
        * User attributes can be private or public. Private attributes are accessible only by
        * your own user, while public ones are retrievable by any of your contacts.
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        * - MRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getText - Returns the value for public attributes
        * - MRequest::getMegaStringMap - Returns the value for private attributes
        *
        * @param email_or_handle Email or user handle (Base64 encoded) to get the attribute.
        * If this parameter is set to NULL, the attribute is obtained for the active account.
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Get the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Get the lastname of the user (public)
        * MUserAttrType::USER_ATTR_AUTHRING = 3
        * Get the authentication ring of the user (private)
        * MUserAttrType::USER_ATTR_LAST_INTERACTION = 4
        * Get the last interaction of the contacts of the user (private)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Get the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Get the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_KEYRING = 7
        * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
        * MUserAttrType::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
        * Get the signature of RSA public key of the user (public)
        * MUserAttrType::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
        * Get the signature of Cu25519 public key of the user (public)
        * MUserAttrType::USER_ATTR_LANGUAGE = 14
        * Get the preferred language of the user (private, non-encrypted)
        * MUserAttrType::USER_ATTR_PWD_REMINDER = 15
        * Get the password-reminder-dialog information (private, non-encrypted)
        * MUserAttrType::USER_ATTR_DISABLE_VERSIONS = 16
        * Get whether user has versions disabled or enabled (private, non-encrypted)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        * MUserAttrType::USER_ATTR_STORAGE_STATE = 21
        * Get the state of the storage (private non-encrypted)
        */
        void getUserAttributeByEmailOrHandle(String^ email_or_handle, int type);

        /**
        * @brief Get an attribute of the current account.
        *
        * User attributes can be private or public. Private attributes are accessible only by
        * your own user, while public ones are retrievable by any of your contacts.
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getText - Returns the value for public attributes
        * - MRequest::getMegaStringMap - Returns the value for private attributes
        *
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Get the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Get the lastname of the user (public)
        * MUserAttrType::USER_ATTR_AUTHRING = 3
        * Get the authentication ring of the user (private)
        * MUserAttrType::USER_ATTR_LAST_INTERACTION = 4
        * Get the last interaction of the contacts of the user (private)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Get the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Get the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_KEYRING = 7
        * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
        * MUserAttrType::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
        * Get the signature of RSA public key of the user (public)
        * MUserAttrType::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
        * Get the signature of Cu25519 public key of the user (public)
        * MUserAttrType::USER_ATTR_LANGUAGE = 14
        * Get the preferred language of the user (private, non-encrypted)
        * MUserAttrType::USER_ATTR_PWD_REMINDER = 15
        * Get the password-reminder-dialog information (private, non-encrypted)
        * MUserAttrType::USER_ATTR_DISABLE_VERSIONS = 16
        * Get whether user has versions disabled or enabled (private, non-encrypted)
        * MUserAttrType::USER_ATTR_RICH_PREVIEWS = 18
        * Get whether user generates rich-link messages or not (private)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        * MUserAttrType::USER_ATTR_STORAGE_STATE = 21
        * Get the state of the storage (private non-encrypted)
        *
        * @param listener MRequestListener to track this request
        */
        void getOwnUserAttribute(int type, MRequestListenerInterface^ listener);

        /**
        * @brief Get an attribute of the current account.
        *
        * User attributes can be private or public. Private attributes are accessible only by
        * your own user, while public ones are retrievable by any of your contacts.
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getText - Returns the value for public attributes
        * - MRequest::getMegaStringMap - Returns the value for private attributes
        *
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Get the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Get the lastname of the user (public)
        * MUserAttrType::USER_ATTR_AUTHRING = 3
        * Get the authentication ring of the user (private)
        * MUserAttrType::USER_ATTR_LAST_INTERACTION = 4
        * Get the last interaction of the contacts of the user (private)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Get the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Get the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_KEYRING = 7
        * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
        * MUserAttrType::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
        * Get the signature of RSA public key of the user (public)
        * MUserAttrType::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
        * Get the signature of Cu25519 public key of the user (public)
        * MUserAttrType::USER_ATTR_LANGUAGE = 14
        * Get the preferred language of the user (private, non-encrypted)
        * MUserAttrType::USER_ATTR_PWD_REMINDER = 15
        * Get the password-reminder-dialog information (private, non-encrypted)
        * MUserAttrType::USER_ATTR_DISABLE_VERSIONS = 16
        * Get whether user has versions disabled or enabled (private, non-encrypted)
        * MUserAttrType::USER_ATTR_RICH_PREVIEWS = 18
        * Get whether user generates rich-link messages or not (private)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        * MUserAttrType::USER_ATTR_STORAGE_STATE = 21
        * Get the state of the storage (private non-encrypted)
        */
        void getOwnUserAttribute(int type);

        /**
        * @brief Get the email address of any user in MEGA.
        *
        * The associated request type with this request is MRequest::TYPE_GET_USER_EMAIL
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the user (the provided one as parameter)
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Returns the email address
        *
        * @param handle Handle of the user to get the attribute.
        * @param listener MRequestListener to track this request
        */
        void getUserEmail(MegaHandle handle, MRequestListenerInterface^ listener);

        /**
        * @brief Get the email address of any user in MEGA.
        *
        * The associated request type with this request is MRequest::TYPE_GET_USER_EMAIL
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the user (the provided one as parameter)
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getEmail - Returns the email address
        *
        * @param handle Handle of the user to get the attribute.
        */
        void getUserEmail(MegaHandle handle);
        
        /**
        * @brief Set a public attribute of the current user
        *
        * The associated request type with this request is MRequest::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        * - MRequest::getText - Returns the new value for the attribute
        *
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Set the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Set the lastname of the user (public)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Set the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Set the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        *
        * @param value New attribute value
        * @param listener MRequestListener to track this request
        */
        void setUserAttribute(int type, String^ value, MRequestListenerInterface^ listener);

        /**
        * @brief Set a public attribute of the current user
        *
        * The associated request type with this request is MRequest::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type
        * - MRequest::getText - Returns the new value for the attribute
        *
        * @param type Attribute type
        *
        * Valid values are:
        *
        * MUserAttrType::USER_ATTR_FIRSTNAME = 1
        * Set the firstname of the user (public)
        * MUserAttrType::USER_ATTR_LASTNAME = 2
        * Set the lastname of the user (public)
        * MUserAttrType::USER_ATTR_ED25519_PUBLIC_KEY = 5
        * Set the public key Ed25519 of the user (public)
        * MUserAttrType::USER_ATTR_CU25519_PUBLIC_KEY = 6
        * Set the public key Cu25519 of the user (public)
        * MUserAttrType::USER_ATTR_RUBBISH_TIME = 19
        * Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
        *
        * @param value New attribute value
        */
        void setUserAttribute(int type, String^ value);

        void setCustomNodeAttribute(MNode^ node, String^ attrName, String^ value, MRequestListenerInterface^ listener);
        void setCustomNodeAttribute(MNode^ node, String^ attrName, String^ value);
        void setNodeDuration(MNode^ node, int duration, MRequestListenerInterface^ listener);
        void setNodeDuration(MNode^ node, int duration);
        void setNodeCoordinates(MNode^ node, double latitude, double longitude, MRequestListenerInterface^ listener);
        void setNodeCoordinates(MNode^ node, double latitude, double longitude);
        void exportNode(MNode^ node, MRequestListenerInterface^ listener);
        void exportNode(MNode^ node);
        void exportNodeWithExpireTime(MNode^ node, int64 expireTime, MRequestListenerInterface^ listener);
        void exportNodeWithExpireTime(MNode^ node, int64 expireTime);
        void disableExport(MNode^ node, MRequestListenerInterface^ listener);
        void disableExport(MNode^ node);
        void fetchNodes(MRequestListenerInterface^ listener);
        void fetchNodes();
        void getAccountDetails(MRequestListenerInterface^ listener);
        void getAccountDetails();
        void queryTransferQuota(int64 size, MRequestListenerInterface^ listener);
        void queryTransferQuota(int64 size);
        void getExtendedAccountDetails(bool sessions, bool purchases, bool transactions, MRequestListenerInterface^ listener);
        void getExtendedAccountDetails(bool sessions, bool purchases, bool transactions);
        void getPricing(MRequestListenerInterface^ listener);
        void getPricing();

        /**
        * @brief Get the payment URL for an upgrade
        *
        * The associated request type with this request is MRequest::TYPE_GET_PAYMENT_ID
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the product
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getLink - Payment ID
        *
        * @param productHandle Handle of the product (see MegaSDK::getPricing)
        * @param listener MRequestListener to track this request
        *
        * @see MegaSDK::getPricing
        */
        void getPaymentId(uint64 productHandle, MRequestListenerInterface^ listener);
        
        /**
        * @brief Get the payment URL for an upgrade
        *
        * The associated request type with this request is MRequest::TYPE_GET_PAYMENT_ID
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the product
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getLink - Payment ID
        *
        * @param productHandle Handle of the product (see MegaSDK::getPricing)
        *
        * @see MegaSDK::getPricing
        */
        void getPaymentId(uint64 productHandle);

        /**
        * @brief Get the payment URL for an upgrade
        *
        * The associated request type with this request is MRequest::TYPE_GET_PAYMENT_ID
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the product
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getLink - Payment ID
        *
        * @param productHandle Handle of the product (see MegaSDK::getPricing)
        * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
        * @param listener MRequestListener to track this request
        *
        * @see MegaSDK::getPricing
        */
        void getPaymentIdWithLastPublicHandle(uint64 productHandle, uint64 lastPublicHandle, MRequestListenerInterface^ listener);

        /**
        * @brief Get the payment URL for an upgrade
        *
        * The associated request type with this request is MRequest::TYPE_GET_PAYMENT_ID
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNodeHandle - Returns the handle of the product
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getLink - Payment ID
        *
        * @param productHandle Handle of the product (see MegaSDK::getPricing)
        * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
        *
        * @see MegaSDK::getPricing
        */
        void getPaymentIdWithLastPublicHandle(uint64 productHandle, uint64 lastPublicHandle);
        
        void upgradeAccount(uint64 productHandle, int paymentMethod, MRequestListenerInterface^ listener);
        void upgradeAccount(uint64 productHandle, int paymentMethod);

        /**
        * @brief Submit a purchase receipt for verification
        *
        * The associated request type with this request is MRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
        *
        * @param gateway Payment gateway
        * Currently supported payment gateways are:
        * - MPaymentMethod::PAYMENT_METHOD_ITUNES = 2
        * - MPaymentMethod::PAYMENT_METHOD_GOOGLE_WALLET = 3
        * - MPaymentMethod::PAYMENT_METHOD_WINDOWS_STORE = 13
        *
        * @param receipt Purchase receipt
        * @param listener MRequestListener to track this request
        */
        void submitPurchaseReceipt(int gateway, String^ receipt, MRequestListenerInterface^ listener);

        /**
        * @brief Submit a purchase receipt for verification
        *
        * The associated request type with this request is MRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
        *
        * @param gateway Payment gateway
        * Currently supported payment gateways are:
        * - MPaymentMethod::PAYMENT_METHOD_ITUNES = 2
        * - MPaymentMethod::PAYMENT_METHOD_GOOGLE_WALLET = 3
        * - MPaymentMethod::PAYMENT_METHOD_WINDOWS_STORE = 13
        *
        * @param receipt Purchase receipt
        */
        void submitPurchaseReceipt(int gateway, String^ receipt);

        /**
        * @brief Submit a purchase receipt for verification
        *
        * The associated request type with this request is MRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
        *
        * @param gateway Payment gateway
        * Currently supported payment gateways are:
        * - MPaymentMethod::PAYMENT_METHOD_ITUNES = 2
        * - MPaymentMethod::PAYMENT_METHOD_GOOGLE_WALLET = 3
        * - MPaymentMethod::PAYMENT_METHOD_WINDOWS_STORE = 13
        *
        * @param receipt Purchase receipt
        * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
        * @param listener MRequestListener to track this request
        */
        void submitPurchaseReceiptWithLastPublicHandle(int gateway, String^ receipt, uint64 lastPublicHandle, MRequestListenerInterface^ listener);

        /**
        * @brief Submit a purchase receipt for verification
        *
        * The associated request type with this request is MRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
        *
        * @param gateway Payment gateway
        * Currently supported payment gateways are:
        * - MPaymentMethod::PAYMENT_METHOD_ITUNES = 2
        * - MPaymentMethod::PAYMENT_METHOD_GOOGLE_WALLET = 3
        * - MPaymentMethod::PAYMENT_METHOD_WINDOWS_STORE = 13
        *
        * @param receipt Purchase receipt
        * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
        */
        void submitPurchaseReceiptWithLastPublicHandle(int gateway, String^ receipt, uint64 lastPublicHandle);

        void creditCardStore(String^ address1, String^ address2, String^ city,
            String^ province, String^ country, String^ postalcode,
            String^ firstname, String^ lastname, String^ creditcard,
            String^ expire_month, String^ expire_year, String^ cv2,
            MRequestListenerInterface^ listener);
        void creditCardStore(String^ address1, String^ address2, String^ city,
            String^ province, String^ country, String^ postalcode,
            String^ firstname, String^ lastname, String^ creditcard,
            String^ expire_month, String^ expire_year, String^ cv2);

        void creditCardQuerySubscriptions(MRequestListenerInterface^ listener);
        void creditCardQuerySubscriptions();
        void creditCardCancelSubscriptions(MRequestListenerInterface^ listener);
        void creditCardCancelSubscriptions(String^ reason, MRequestListenerInterface^ listener);
        void creditCardCancelSubscriptions();
        void getPaymentMethods(MRequestListenerInterface^ listener);
        void getPaymentMethods();

        String^ exportMasterKey();
        void masterKeyExported(MRequestListenerInterface^ listener);
        void masterKeyExported();

        /**
        * @brief Notify the user has successfully checked his password
        *
        * This function should be called when the user demonstrates that he remembers
        * the password to access the account
        *
        * As result, the user attribute MUserAttrType::USER_ATTR_PWD_REMINDER will be updated
        * to remember this event. In consequence, MEGA will not continue asking the user
        * to remind the password for the account in a short time.
        *
        * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        * - MRequest::getText - Returns the new value for the attribute
        *
        * @param listener MRequestListener to track this request
        */
        void passwordReminderDialogSucceeded(MRequestListenerInterface^ listener);

        /**
        * @brief Notify the user has successfully checked his password
        *
        * This function should be called when the user demonstrates that he remembers
        * the password to access the account
        *
        * As result, the user attribute MUserAttrType::USER_ATTR_PWD_REMINDER will be updated
        * to remember this event. In consequence, MEGA will not continue asking the user
        * to remind the password for the account in a short time.
        *
        * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        * - MRequest::getText - Returns the new value for the attribute
        */
        void passwordReminderDialogSucceeded();

        /**
        * @brief Notify the user has successfully skipped the password check
        *
        * This function should be called when the user skips the verification of
        * the password to access the account
        *
        * As result, the user attribute MUserAttrType::USER_ATTR_PWD_REMINDER will be updated
        * to remember this event. In consequence, MEGA will not continue asking the user
        * to remind the password for the account in a short time.
        *
        * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        * - MRequest::getText - Returns the new value for the attribute
        *
        * @param listener MRequestListener to track this request
        */
        void passwordReminderDialogSkipped(MRequestListenerInterface^ listener);

        /**
        * @brief Notify the user has successfully skipped the password check
        *
        * This function should be called when the user skips the verification of
        * the password to access the account
        *
        * As result, the user attribute MUserAttrType::USER_ATTR_PWD_REMINDER will be updated
        * to remember this event. In consequence, MEGA will not continue asking the user
        * to remind the password for the account in a short time.
        *
        * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        * - MRequest::getText - Returns the new value for the attribute
        */
        void passwordReminderDialogSkipped();

        /**
        * @brief Notify the user wants to totally disable the password check
        *
        * This function should be called when the user rejects to verify that he remembers
        * the password to access the account and doesn't want to see the reminder again.
        *
        * As result, the user attribute MUserAttrType::USER_ATTR_PWD_REMINDER will be updated
        * to remember this event. In consequence, MEGA will not ask the user
        * to remind the password for the account again.
        *
        * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        * - MRequest::getText - Returns the new value for the attribute
        *
        * @param listener MRequestListener to track this request
        */
        void passwordReminderDialogBlocked(MRequestListenerInterface^ listener);

        /**
        * @brief Notify the user wants to totally disable the password check
        *
        * This function should be called when the user rejects to verify that he remembers
        * the password to access the account and doesn't want to see the reminder again.
        *
        * As result, the user attribute MUserAttrType::USER_ATTR_PWD_REMINDER will be updated
        * to remember this event. In consequence, MEGA will not ask the user
        * to remind the password for the account again.
        *
        * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        * - MRequest::getText - Returns the new value for the attribute
        */
        void passwordReminderDialogBlocked();

        /**
        * @brief Check if the app should show the password reminder dialog to the user
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getFlag - Returns true if the password reminder dialog should be shown
        *
        * If the corresponding user attribute is not set yet, the request will fail with the
        * error code MError::API_ENOENT but the value of MRequest::getFlag will still
        * be valid.
        *
        * @param atLogout True if the check is being done just before a logout
        * @param listener MRequestListener to track this request
        */
        void shouldShowPasswordReminderDialog(bool atLogout, MRequestListenerInterface^ listener);

        /**
        * @brief Check if the app should show the password reminder dialog to the user
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getFlag - Returns true if the password reminder dialog should be shown
        *
        * If the corresponding user attribute is not set yet, the request will fail with the
        * error code MError::API_ENOENT but the value of MRequest::getFlag will still
        * be valid.
        *
        * @param atLogout True if the check is being done just before a logout
        */
        void shouldShowPasswordReminderDialog(bool atLogout);

        /**
        * @brief Check if the master key has been exported
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getAccess - Returns true if the master key has been exported
        *
        * If the corresponding user attribute is not set yet, the request will fail with the
        * error code MError::API_ENOENT.
        *
        * @param listener MRequestListener to track this request
        */
        void isMasterKeyExported(MRequestListenerInterface^ listener);

        /**
        * @brief Check if the master key has been exported
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_PWD_REMINDER
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getAccess - Returns true if the master key has been exported
        *
        * If the corresponding user attribute is not set yet, the request will fail with the
        * error code MError::API_ENOENT.
        */
        void isMasterKeyExported();

        /**
        * @brief Get the number of days for rubbish-bin cleaning scheduler
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_RUBBISH_TIME
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler.
        * Zero means that the rubbish-bin cleaning scheduler is disabled (only if the account is PRO)
        * Any negative value means that the configured value is invalid.
        *
        * @param listener MRequestListener to track this request
        */
        void getRubbishBinAutopurgePeriod(MRequestListenerInterface^ listener);

        /**
        * @brief Get the number of days for rubbish-bin cleaning scheduler
        *
        * The associated request type with this request is MRequest::TYPE_GET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_RUBBISH_TIME
        *
        * Valid data in the MRequest object received in onRequestFinish when the error code
        * is MError::API_OK:
        * - MRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler.
        * Zero means that the rubbish-bin cleaning scheduler is disabled (only if the account is PRO)
        * Any negative value means that the configured value is invalid.
        */
        void getRubbishBinAutopurgePeriod();

        /**
        * @brief Set the number of days for rubbish-bin cleaning scheduler
        *
        * The associated request type with this request is MRequest::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_RUBBISH_TIME
        * - MRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler passed as parameter
        *
        * @param days Number of days for rubbish-bin cleaning scheduler. It must be >= 0.
        * The value zero disables the rubbish-bin cleaning scheduler (only for PRO accounts).
        *
        * @param listener MRequestListener to track this request
        */
        void setRubbishBinAutopurgePeriod(int days, MRequestListenerInterface^ listener);

        /**
        * @brief Set the number of days for rubbish-bin cleaning scheduler
        *
        * The associated request type with this request is MRequest::TYPE_SET_ATTR_USER
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getParamType - Returns the attribute type MUserAttrType::USER_ATTR_RUBBISH_TIME
        * - MRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler passed as parameter
        *
        * @param days Number of days for rubbish-bin cleaning scheduler. It must be >= 0.
        * The value zero disables the rubbish-bin cleaning scheduler (only for PRO accounts).
        */
        void setRubbishBinAutopurgePeriod(int days);

        /**
        * @brief Change the password of the MEGA account
        *
        * The associated request type with this request is MRequest::TYPE_CHANGE_PW
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getPassword - Returns the old password
        * - MRequest::getNewPassword - Returns the new password
        *
        * @param oldPassword Old password
        * @param newPassword New password
        * @param listener MRequestListener to track this request
        */
        void changePassword(String^ oldPassword, String^ newPassword, MRequestListenerInterface^ listener);
        
        /**
        * @brief Change the password of the MEGA account
        *
        * The associated request type with this request is MRequest::TYPE_CHANGE_PW
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getPassword - Returns the old password
        * - MRequest::getNewPassword - Returns the new password
        *
        * @param oldPassword Old password
        * @param newPassword New password
        */
        void changePassword(String^ oldPassword, String^ newPassword);

        /**
        * @brief Change the password of the MEGA account without check the old password
        *
        * The associated request type with this request is MRequest::TYPE_CHANGE_PW
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNewPassword - Returns the new password
        *
        * @param newPassword New password
        * @param listener MRequestListener to track this request
        */
        void changePasswordWithoutOld(String^ newPassword, MRequestListenerInterface^ listener);

        /**
        * @brief Change the password of the MEGA account without check the old password
        *
        * The associated request type with this request is MRequest::TYPE_CHANGE_PW
        * Valid data in the MRequest object received on callbacks:
        * - MRequest::getNewPassword - Returns the new password
        *
        * @param newPassword New password
        */
        void changePasswordWithoutOld(String^ newPassword);
        
        void inviteContact(String^ email, String^ message, MContactRequestInviteActionType action, MRequestListenerInterface^ listener);
        void inviteContact(String^ email, String^ message, MContactRequestInviteActionType action);
        
        /**
         * @brief Invite another person to be your MEGA contact using a contact link handle
         *
         * The user doesn't need to be registered on MEGA. If the email isn't associated with
         * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
         *
         * The associated request type with this request is MRequestType::TYPE_INVITE_CONTACT
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getEmail - Returns the email of the contact
         * - MRequest::getText - Returns the text of the invitation
         * - MRequest::getNumber - Returns the action
         * - MRequest::getNodeHandle - Returns the contact link handle
         *
         * Sending a reminder within a two week period since you started or your last reminder will
         * fail the API returning the error code MError::API_EACCESS.
         *
         * @param email Email of the new contact
         * @param message Message for the user (can be NULL)
         * @param action Action for this contact request. Valid values are:
         * - MContactRequestInviteActionType::INVITE_ACTION_ADD = 0
         * - MContactRequestInviteActionType::INVITE_ACTION_DELETE = 1
         * - MContactRequestInviteActionType::INVITE_ACTION_REMIND = 2
         * @param contactLink Contact link handle of the other account. This parameter is considered only if the
         * \c action is MContactRequestInviteActionType::INVITE_ACTION_ADD. Otherwise, it's ignored and it has no effect.
         *
         * @param listener MRequestListener to track this request
         */
        void inviteContactByLinkHandle(String^ email, String^ message, MContactRequestInviteActionType action, MegaHandle contactLink, MRequestListenerInterface^ listener);
        
        /**
         * @brief Invite another person to be your MEGA contact using a contact link handle
         *
         * The user doesn't need to be registered on MEGA. If the email isn't associated with
         * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
         *
         * The associated request type with this request is MRequestType::TYPE_INVITE_CONTACT
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getEmail - Returns the email of the contact
         * - MRequest::getText - Returns the text of the invitation
         * - MRequest::getNumber - Returns the action
         * - MRequest::getNodeHandle - Returns the contact link handle
         *
         * Sending a reminder within a two week period since you started or your last reminder will
         * fail the API returning the error code MError::API_EACCESS.
         *
         * @param email Email of the new contact
         * @param message Message for the user (can be NULL)
         * @param action Action for this contact request. Valid values are:
         * - MContactRequestInviteActionType::INVITE_ACTION_ADD = 0
         * - MContactRequestInviteActionType::INVITE_ACTION_DELETE = 1
         * - MContactRequestInviteActionType::INVITE_ACTION_REMIND = 2
         * @param contactLink Contact link handle of the other account. This parameter is considered only if the
         * \c action is MContactRequestInviteActionType::INVITE_ACTION_ADD. Otherwise, it's ignored and it has no effect.
         */
        void inviteContactByLinkHandle(String^ email, String^ message, MContactRequestInviteActionType action, MegaHandle contactLink);
        
        void replyContactRequest(MContactRequest^ request, MContactRequestReplyActionType action, MRequestListenerInterface^ listener);
        void replyContactRequest(MContactRequest^ request, MContactRequestReplyActionType action);
                
        void removeContact(MUser^ user, MRequestListenerInterface^ listener);
        void removeContact(MUser^ user);
        
        /**
        * @brief Logout of the MEGA account invalidating the session
        *
        * The associated request type with this request is MRequestType::TYPE_LOGOUT
        *
        * Under certain circumstances, this request might return the error code
        * MError::API_ESID. It should not be taken as an error, since the reason
        * is that the logout action has been notified before the reception of the
        * logout response itself.
        *
        * @param listener MRequestListener to track this request
        */
        void logout(MRequestListenerInterface^ listener);

        /**
        * @brief Logout of the MEGA account invalidating the session
        *
        * The associated request type with this request is MRequestType::TYPE_LOGOUT
        *
        * Under certain circumstances, this request might return the error code
        * MError::API_ESID. It should not be taken as an error, since the reason
        * is that the logout action has been notified before the reception of the
        * logout response itself.
        */
        void logout();

        /**
        * @brief Logout of the MEGA account without invalidating the session
        *
        * The associated request type with this request is MRequestType::TYPE_LOGOUT
        *
        * @param listener MRequestListener to track this request
        */
        void localLogout(MRequestListenerInterface^ listener);

        /**
        * @brief Logout of the MEGA account without invalidating the session
        *
        * The associated request type with this request is MRequestType::TYPE_LOGOUT
        */
        void localLogout();        
        
        int getPasswordStrength(String^ password);
        void submitFeedback(int rating, String^ comment, MRequestListenerInterface^ listener);
        void submitFeedback(int rating, String^ comment);
        void reportDebugEvent(String^ text, MRequestListenerInterface^ listener);
        void reportDebugEvent(String^ text);

        //Transfers
        void startUpload(String^ localPath, MNode^ parent, MTransferListenerInterface^ listener);
        void startUpload(String^ localPath, MNode^ parent);
        void startUploadToFile(String^ localPath, MNode^ parent, String^ fileName, MTransferListenerInterface^ listener);
        void startUploadToFile(String^ localPath, MNode^ parent, String^ fileName);
		void startUploadWithMtime(String^ localPath, MNode^ parent, uint64 mtime, MTransferListenerInterface^ listener);
		void startUploadWithMtime(String^ localPath, MNode^ parent, uint64 mtime);
        void startUploadWithMtimeTempSource(String^ localPath, MNode^ parent, uint64 mtime, bool isSourceTemporary, MTransferListenerInterface^ listener);
        void startUploadWithMtimeTempSource(String^ localPath, MNode^ parent, uint64 mtime, bool isSourceTemporary);
        void startUploadToFileWithMtime(String^ localPath, MNode^ parent, String^ fileName, uint64 mtime, MTransferListenerInterface^ listener);
        void startUploadToFileWithMtime(String^ localPath, MNode^ parent, String^ fileName, uint64 mtime);
        void startUploadWithData(String^ localPath, MNode^ parent, String^ appData, MTransferListenerInterface^ listener);
        void startUploadWithData(String^ localPath, MNode^ parent, String^ appData);
        void startUploadWithDataTempSource(String^ localPath, MNode^ parent, String^ appData, bool isSourceTemporary, MTransferListenerInterface^ listener);
        void startUploadWithDataTempSource(String^ localPath, MNode^ parent, String^ appData, bool isSourceTemporary);

        /**
        * @brief Upload a file or a folder, putting the transfer on top of the upload queue
        * @param localPath Local path of the file or folder
        * @param parent Parent node for the file or folder in the MEGA account
        * @param appData Custom app data to save in the MTransfer object
        * The data in this parameter can be accessed using MTransfer::getAppData in callbacks
        * related to the transfer. If a transfer is started with exactly the same data
        * (local path and target parent) as another one in the transfer queue, the new transfer
        * fails with the error API_EEXISTS and the appData of the new transfer is appended to
        * the appData of the old transfer, using a '!' separator if the old transfer had already
        * appData.
        * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
        * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
        * Use this parameter with caution. Set it to true only if you are sure about what are you doing.
        * @param listener MTransferListener to track this transfer
        */
        void startUploadWithTopPriority(String^ localPath, MNode^ parent, String^ appData, bool isSourceTemporary, MTransferListenerInterface^ listener);

        /**
        * @brief Upload a file or a folder, putting the transfer on top of the upload queue
        * @param localPath Local path of the file or folder
        * @param parent Parent node for the file or folder in the MEGA account
        * @param appData Custom app data to save in the MTransfer object
        * The data in this parameter can be accessed using MTransfer::getAppData in callbacks
        * related to the transfer. If a transfer is started with exactly the same data
        * (local path and target parent) as another one in the transfer queue, the new transfer
        * fails with the error API_EEXISTS and the appData of the new transfer is appended to
        * the appData of the old transfer, using a '!' separator if the old transfer had already
        * appData.
        * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
        * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
        * Use this parameter with caution. Set it to true only if you are sure about what are you doing.
        */
        void startUploadWithTopPriority(String^ localPath, MNode^ parent, String^ appData, bool isSourceTemporary);

        void startDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener);
        void startDownload(MNode^ node, String^ localPath);
        void startDownloadWithAppData(MNode^ node, String^ localPath, String^ appData, MTransferListenerInterface^ listener);
        void startDownloadWithAppData(MNode^ node, String^ localPath, String^ appData);

        /**
        * @brief Download a file or a folder from MEGA, putting the transfer on top of the download queue.
        * @param node MNode that identifies the file or folder
        * @param localPath Destination path for the file or folder
        * If this path is a local folder, it must end with a '\' or '/' character and the file name
        * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
        * one of these characters, the file will be downloaded to a file in that path.
        * @param appData Custom app data to save in the MTransfer object
        * The data in this parameter can be accessed using MTransfer::getAppData in callbacks
        * related to the transfer.
        * @param listener MTransferListener to track this transfer
        */
        void startDownloadWithTopPriority(MNode^ node, String^ localPath, String^ appData, MTransferListenerInterface^ listener);

        /**
        * @brief Download a file or a folder from MEGA, putting the transfer on top of the download queue.
        * @param node MNode that identifies the file or folder
        * @param localPath Destination path for the file or folder
        * If this path is a local folder, it must end with a '\' or '/' character and the file name
        * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
        * one of these characters, the file will be downloaded to a file in that path.
        * @param appData Custom app data to save in the MTransfer object
        * The data in this parameter can be accessed using MTransfer::getAppData in callbacks
        * related to the transfer.
        */
        void startDownloadWithTopPriority(MNode^ node, String^ localPath, String^ appData);

        void startStreaming(MNode^ node, uint64 startPos, uint64 size, MTransferListenerInterface^ listener);
        void retryTransfer(MTransfer^ transfer, MTransferListenerInterface^ listener);
        void retryTransfer(MTransfer^ transfer);
        void cancelTransfer(MTransfer^ transfer, MRequestListenerInterface^ listener);
        void cancelTransfer(MTransfer^ transfer);
        void cancelTransferByTag(int transferTag, MRequestListenerInterface^ listener);
        void cancelTransferByTag(int transferTag);
        void cancelTransfers(int direction, MRequestListenerInterface^ listener);
        void cancelTransfers(int direction);        
        void pauseTransfers(bool pause, MRequestListenerInterface^ listener);
        void pauseTransfers(bool pause);
        void pauseTransfersDirection(bool pause, int direction, MRequestListenerInterface^ listener);
        void pauseTransfersDirection(bool pause, int direction);
        void pauseTransfer(MTransfer^ transfer, bool pause, MRequestListenerInterface^ listener);
        void pauseTransfer(MTransfer^ transfer, bool pause);
        void pauseTransferByTag(int transferTag, bool pause, MRequestListenerInterface^ listener);
        void pauseTransferByTag(int transferTag, bool pause);
        void moveTransferUp(MTransfer^ transfer, MRequestListenerInterface^ listener);
        void moveTransferUp(MTransfer^ transfer);
        void moveTransferUpByTag(int transferTag, MRequestListenerInterface^ listener);
        void moveTransferUpByTag(int transferTag);
        void moveTransferDown(MTransfer^ transfer, MRequestListenerInterface^ listener);
        void moveTransferDown(MTransfer^ transfer);
        void moveTransferDownByTag(int transferTag, MRequestListenerInterface^ listener);
        void moveTransferDownByTag(int transferTag);
        void moveTransferToFirst(MTransfer^ transfer, MRequestListenerInterface^ listener);
        void moveTransferToFirst(MTransfer^ transfer);
        void moveTransferToFirstByTag(int transferTag, MRequestListenerInterface^ listener);
        void moveTransferToFirstByTag(int transferTag);
        void moveTransferToLast(MTransfer^ transfer, MRequestListenerInterface^ listener);
        void moveTransferToLast(MTransfer^ transfer);
        void moveTransferToLastByTag(int transferTag, MRequestListenerInterface^ listener);
        void moveTransferToLastByTag(int transferTag);
        void moveTransferBefore(MTransfer^ transfer, MTransfer^ prevTransfer, MRequestListenerInterface^ listener);
        void moveTransferBefore(MTransfer^ transfer, MTransfer^ prevTransfer);
        void moveTransferBeforeByTag(int transferTag, int prevTransferTag, MRequestListenerInterface^ listener);
        void moveTransferBeforeByTag(int transferTag, int prevTransferTag);
        void enableTransferResumption(String^ loggedOutId);
        void enableTransferResumption();
        void disableTransferResumption(String^ loggedOutId);
        void disableTransferResumption();
        bool areTransfersPaused(int direction);
        void setUploadLimit(int bpslimit);
        void setDownloadMethod(int method);
        void setUploadMethod(int method);
        bool setMaxDownloadSpeed(int64 bpslimit);
        bool setMaxUploadSpeed(int64 bpslimit);
        int getMaxDownloadSpeed();
        int getMaxUploadSpeed();
        int getCurrentDownloadSpeed();
        int getCurrentUploadSpeed();
        int getCurrentSpeed(int type);
        int getDownloadMethod();
        int getUploadMethod();
        MTransferData^ getTransferData(MTransferListenerInterface^ listener);
        MTransferData^ getTransferData();
        MTransfer^ getFirstTransfer(int type);
        void notifyTransfer(MTransfer^ transfer, MTransferListenerInterface^ listener);
        void notifyTransfer(MTransfer^ transfer);
        void notifyTransferByTag(int transferTag, MTransferListenerInterface^ listener);
        void notifyTransferByTag(int transferTag);
        MTransferList^ getTransfers();
        MTransferList^ getStreamingTransfers();
        MTransfer^ getTransferByTag(int transferTag);
        MTransferList^ getTransfers(MTransferType type);
        MTransferList^ getChildTransfers(int transferTag);
        
        /**
        * @brief Check if the SDK is waiting to complete a request and get the reason
        * @return State of SDK.
        *
        * Valid values are:
        * - MRetryReason::RETRY_NONE = 0
        * SDK is not waiting for the server to complete a request
        *
        * - MRetryReason::RETRY_CONNECTIVITY = 1
        * SDK is waiting for the server to complete a request due to connectivity issues
        *
        * - MRetryReason::RETRY_SERVERS_BUSY = 2
        * SDK is waiting for the server to complete a request due to a HTTP error 500
        *
        * - MRetryReason::RETRY_API_LOCK = 3
        * SDK is waiting for the server to complete a request due to an API lock (API error -3)
        *
        * - MRetryReason::RETRY_RATE_LIMIT = 4,
        * SDK is waiting for the server to complete a request due to a rate limit (API error -4)
        *
        * - MRetryReason::RETRY_LOCAL_LOCK = 5
        * SDK is waiting for a local locked file
        *
        * - MRetryReason::RETRY_IGNORE_FILE = 6
        * SDK is waiting for an ignore file to load.
        *
        * - MRetryReason::RETRY_UNKNOWN = 7
        * SDK is waiting for the server to complete a request with unknown reason
        *
        */
        int isWaiting();

        //Statistics
        int getNumPendingUploads();
        int getNumPendingDownloads();
        int getTotalUploads();
        int getTotalDownloads();
        void resetTotalDownloads();
        void resetTotalUploads();
        void updateStats();
        uint64 getNumNodes();
        uint64 getTotalDownloadedBytes();
        uint64 getTotalUploadedBytes();
        uint64 getTotalDownloadBytes();
        uint64 getTotalUploadBytes();
        
        //Filesystem
        int getNumChildren(MNode^ parent);
        int getNumChildFiles(MNode^ parent);
        int getNumChildFolders(MNode^ parent);
        MNodeList^ getChildren(MNode^ parent, int order);
        MNodeList^ getChildren(MNode^ parent);
        MChildrenLists^ getFileFolderChildren(MNode^ parent, int order);
        MChildrenLists^ getFileFolderChildren(MNode^ parent);
        bool hasChildren(MNode^ parent);
        int getIndex(MNode^ node, int order);
        int getIndex(MNode^ node);
        MNode^ getChildNode(MNode^ parent, String^ name);
        MNode^ getParentNode(MNode^ node);
        String^ getNodePath(MNode^ node);
        MNode^ getNodeByPath(String^ path, MNode^ n);
        MNode^ getNodeByPath(String^ path);
        MNode^ getNodeByHandle(uint64 handle);
        MNode^ getNodeByBase64Handle(String^ base64Handle);
        MContactRequest^ getContactRequestByHandle(MegaHandle handle);
        MUserList^ getContacts();
        MUser^ getContact(String^ email);

        /**
        * @brief Get all MUserAlerts for the logged in user
        *
        * You take the ownership of the returned value
        *
        * @return List of MUserAlert objects
        */
        MUserAlertList^ getUserAlerts();

        /**
        * @brief Get the number of unread user alerts for the logged in user
        *
        * @return Number of unread user alerts
        */
        int getNumUnreadUserAlerts();

        MNodeList^ getInShares(MUser^ user);
        MNodeList^ getInShares();
        MShareList^ getInSharesList();
        MUser^ getUserFromInShare(MNode^ node);
        bool isShared(MNode^ node);
        bool isOutShare(MNode^ node);
        bool isInShare(MNode^ node);
        bool isPendingShare(MNode^ node);
        MShareList^ getOutShares();
        MShareList^ getOutShares(MNode^ node);
        MShareList^ getPendingOutShares();
        MShareList^ getPendingOutShares(MNode^ megaNode);
        MNodeList^ getPublicLinks();
        MContactRequestList^ getIncomingContactRequests();
        MContactRequestList^ getOutgoingContactRequests();

        int getAccess(MNode^ node);
        uint64 getSize(MNode^ node);
        static void removeRecursively(String^ path);

        //Fingerprint
        String^ getFileFingerprint(String^ filePath);
        String^ getFileFingerprint(MInputStream^ inputStream, uint64 mtime);
        String^ getNodeFingerprint(MNode^ node);
        MNode^ getNodeByFingerprint(String^ fingerprint);
        MNode^ getNodeByFingerprint(String^ fingerprint, MNode^ parent);
        MNodeList^ getNodesByFingerprint(String^ fingerprint);
        MNode^ getExportableNodeByFingerprint(String^ fingerprint);
        MNode^ getExportableNodeByFingerprint(String^ fingerprint, String^ name);
        bool hasFingerprint(String^ fingerprint);
        
        //CRC
        String^ getCRCFromFile(String^ filePath);
        String^ getCRCFromFingerprint(String^ fingerprint);
        String^ getCRCFromNode(MNode^ node);
        MNode^ getNodeByCRC(String^ crc, MNode^ parent);

        //Permissions
        MError^ checkAccess(MNode^ node, int level);
        MError^ checkMove(MNode^ node, MNode^ target);

        bool isFilesystemAvailable();
        MNode^ getRootNode();
        MNode^ getRootNode(MNode^ node);
        MNode^ getInboxNode();
        MNode^ getRubbishNode();
        bool isInCloud(MNode^ node);
        bool isInRubbish(MNode^ node);
        bool isInInbox(MNode^ node);

        /**
        * @brief Get the time (in seconds) during which transfers will be stopped due to a bandwidth overquota
        * @return Time (in seconds) during which transfers will be stopped, otherwise 0
        */
        uint64 getBandwidthOverquotaDelay();

        /**
        * @brief Search nodes containing a search string in their name
        *
        * The search is case-insensitive.
        *
        * You take the ownership of the returned value.
        *
        * @param node The parent node of the tree to explore
        * @param searchString Search string. The search is case-insensitive
        * @param recursive True if you want to seach recursively in the node tree.
        * False if you want to seach in the children of the node only.
        *
        * @param order Order for the returned list
        * Valid values for this parameter are:
        * - MSortOrderType::ORDER_NONE = 0
        * Undefined order
        *
        * - MSortOrderType::ORDER_DEFAULT_ASC = 1
        * Folders first in alphabetical order, then files in the same order
        *
        * - MSortOrderType::ORDER_DEFAULT_DESC = 2
        * Files first in reverse alphabetical order, then folders in the same order
        *
        * - MSortOrderType::ORDER_SIZE_ASC = 3
        * Sort by size, ascending
        *
        * - MSortOrderType::ORDER_SIZE_DESC = 4
        * Sort by size, descending
        *
        * - MSortOrderType::ORDER_CREATION_ASC = 5
        * Sort by creation time in MEGA, ascending
        *
        * - MSortOrderType::ORDER_CREATION_DESC = 6
        * Sort by creation time in MEGA, descending
        *
        * - MSortOrderType::ORDER_MODIFICATION_ASC = 7
        * Sort by modification time of the original file, ascending
        *
        * - MSortOrderType::ORDER_MODIFICATION_DESC = 8
        * Sort by modification time of the original file, descending
        *
        * - MSortOrderType::ORDER_ALPHABETICAL_ASC = 9
        * Sort in alphabetical order, ascending
        *
        * - MSortOrderType::ORDER_ALPHABETICAL_DESC = 10
        * Sort in alphabetical order, descending
        *
        * @return List of nodes that contain the desired string in their name
        */
        MNodeList^ search(MNode^ node, String^ searchString, bool recursive, int order);

        /**
        * @brief Search nodes containing a search string in their name
        *
        * The search is case-insensitive.
        *
        * You take the ownership of the returned value.
        *
        * @param node The parent node of the tree to explore
        * @param searchString Search string. The search is case-insensitive
        * @param recursive True if you want to seach recursively in the node tree.
        * False if you want to seach in the children of the node only.
        *
        * @return List of nodes that contain the desired string in their name
        */
        MNodeList^ search(MNode^ node, String^ searchString, bool recursive);

        /**
        * @brief Search nodes containing a search string in their name
        *
        * The search is case-insensitive.
        *
        * The search will consider every accessible node for the account:
        *  - Cloud drive
        *  - Inbox
        *  - Rubbish bin
        *  - Incoming shares from other users
        *
        * You take the ownership of the returned value.
        *
        * @param searchString Search string. The search is case-insensitive
        * @param order Order for the returned list
        * Valid values for this parameter are:
        * - MSortOrderType::ORDER_NONE = 0
        * Undefined order
        *
        * - MSortOrderType::ORDER_DEFAULT_ASC = 1
        * Folders first in alphabetical order, then files in the same order
        *
        * - MSortOrderType::ORDER_DEFAULT_DESC = 2
        * Files first in reverse alphabetical order, then folders in the same order
        *
        * - MSortOrderType::ORDER_SIZE_ASC = 3
        * Sort by size, ascending
        *
        * - MSortOrderType::ORDER_SIZE_DESC = 4
        * Sort by size, descending
        *
        * - MSortOrderType::ORDER_CREATION_ASC = 5
        * Sort by creation time in MEGA, ascending
        *
        * - MSortOrderType::ORDER_CREATION_DESC = 6
        * Sort by creation time in MEGA, descending
        *
        * - MSortOrderType::ORDER_MODIFICATION_ASC = 7
        * Sort by modification time of the original file, ascending
        *
        * - MSortOrderType::ORDER_MODIFICATION_DESC = 8
        * Sort by modification time of the original file, descending
        *
        * - MSortOrderType::ORDER_ALPHABETICAL_ASC = 9
        * Sort in alphabetical order, ascending
        *
        * - MSortOrderType::ORDER_ALPHABETICAL_DESC = 10
        * Sort in alphabetical order, descending
        *
        * @return List of nodes that contain the desired string in their name
        */
        MNodeList^ globalSearch(String^ searchString, int order);
        
        /**
        * @brief Search nodes containing a search string in their name
        *
        * The search is case-insensitive.
        *
        * The search will consider every accessible node for the account:
        *  - Cloud drive
        *  - Inbox
        *  - Rubbish bin
        *  - Incoming shares from other users
        *
        * You take the ownership of the returned value.
        *
        * @param searchString Search string. The search is case-insensitive
        *
        * @return List of nodes that contain the desired string in their name
        */
        MNodeList^ globalSearch(String^ searchString);
        
        bool processMegaTree(MNode^ node, MTreeProcessorInterface^ processor, bool recursive);
        bool processMegaTree(MNode^ node, MTreeProcessorInterface^ processor);

        MNode^ authorizeNode(MNode^ node);
        
        void changeApiUrl(String^ apiURL, bool disablepkp);
        void changeApiUrl(String^ apiURL);
        bool setLanguage(String^ languageCode);

        /**
         * @brief Enable or disable the automatic approval of incoming contact requests using a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
         *
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getParamType - Returns the value MUserAttrType::USER_ATTR_CONTACT_LINK_VERIFICATION
         *
         * Valid data in the MRequest object received in onRequestFinish:
         * - MRequest::getText - "0" for disable, "1" for enable
         *
         * @param disable True to disable the automatic approval of incoming contact requests using a contact link
         * @param listener MRequestListener to track this request
         */
        void setContactLinksOption(bool disable, MRequestListenerInterface^ listener);
        
        /**
         * @brief Enable or disable the automatic approval of incoming contact requests using a contact link
         *
         * The associated request type with this request is MRequestType::TYPE_SET_ATTR_USER
         *
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getParamType - Returns the value MUserAttrType::USER_ATTR_CONTACT_LINK_VERIFICATION
         *
         * Valid data in the MRequest object received in onRequestFinish:
         * - MRequest::getText - "0" for disable, "1" for enable
         *
         * @param disable True to disable the automatic approval of incoming contact requests using a contact link
         */
        void setContactLinksOption(bool disable);
        
        /**
         * @brief Check if the automatic approval of incoming contact requests using contact links is enabled or disabled
         *
         * If the option has never been set, the error code will be MError::API_ENOENT.
         *
         * The associated request type with this request is MRequestType::TYPE_GET_ATTR_USER
         *
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getParamType - Returns the value MUserAttrType::USER_ATTR_CONTACT_LINK_VERIFICATION
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getText - "0" for disable, "1" for enable
         * - MRequest::getFlag - false if disabled, true if enabled
         *
         * @param listener MRequestListener to track this request
         */
        void getContactLinksOption(MRequestListenerInterface^ listener);
        
        /**
         * @brief Check if the automatic approval of incoming contact requests using contact links is enabled or disabled
         *
         * If the option has never been set, the error code will be MError::API_ENOENT.
         *
         * The associated request type with this request is MRequestType::TYPE_GET_ATTR_USER
         *
         * Valid data in the MRequest object received on callbacks:
         * - MRequest::getParamType - Returns the value MUserAttrType::USER_ATTR_CONTACT_LINK_VERIFICATION
         *
         * Valid data in the MRequest object received in onRequestFinish when the error code
         * is MError::API_OK:
         * - MRequest::getText - "0" for disable, "1" for enable
         * - MRequest::getFlag - false if disabled, true if enabled
         */
        void getContactLinksOption();

        /**
        * @brief Keep retrying when public key pinning fails
        *
        * By default, when the check of the MEGA public key fails, it causes an automatic
        * logout. Pass false to this function to disable that automatic logout and
        * keep the SDK retrying the request.
        *
        * Even if the automatic logout is disabled, a request of the type MRequestType::TYPE_LOGOUT
        * will be automatically created and callbacks (onRequestStart, onRequestFinish) will
        * be sent. However, logout won't be really executed and in onRequestFinish the error code
        * for the request will be MError::API_EINCOMPLETE
        *
        * @param enable true to keep retrying failed requests due to a fail checking the MEGA public key
        * or false to perform an automatic logout in that case
        */
        void retrySSLerrors(bool enable);

        /**
        * @brief Enable / disable the public key pinning
        *
        * Public key pinning is enabled by default for all sensible communications.
        * It is strongly discouraged to disable this feature.
        *
        * @param enable true to keep public key pinning enabled, false to disable it
        */
        void setPublicKeyPinning(bool enable);

        bool createThumbnail(String^ imagePath, String^ dstPath);
        bool createPreview(String^ imagePath, String^ dstPath);

        bool isOnline();

        void getAccountAchievements(MRequestListenerInterface^ listener);
        void getAccountAchievements();
        void getMegaAchievements(MRequestListenerInterface^ listener);
        void getMegaAchievements();

    private:
        std::set<DelegateMRequestListener *> activeRequestListeners;
        std::set<DelegateMTransferListener *> activeTransferListeners;
        std::set<DelegateMGlobalListener *> activeGlobalListeners;
        std::set<DelegateMListener *> activeMegaListeners;
        CRITICAL_SECTION listenerMutex;

        MegaRequestListener *createDelegateMRequestListener(MRequestListenerInterface^ listener, bool singleListener = true);
        MegaTransferListener *createDelegateMTransferListener(MTransferListenerInterface^ listener, bool singleListener = true);
        MegaGlobalListener *createDelegateMGlobalListener(MGlobalListenerInterface^ listener);
        MegaListener *createDelegateMListener(MListenerInterface^ listener);
        MegaTreeProcessor *createDelegateMTreeProcessor(MTreeProcessorInterface^ processor);

        void freeRequestListener(DelegateMRequestListener *listener);
        void freeTransferListener(DelegateMTransferListener *listener);

        std::set<DelegateMLogger *> activeLoggers;
        CRITICAL_SECTION loggerMutex;

        MegaLogger *createDelegateMLogger(MLoggerInterface^ logger);
        void freeLogger(DelegateMLogger *logger);

        MegaApi *megaApi;
        DelegateMGfxProcessor *externalGfxProcessor;
        MegaApi *getCPtr();
    };
}
