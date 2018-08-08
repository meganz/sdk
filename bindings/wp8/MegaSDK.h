/**
* @file MegaSDK.h
* @brief Allows to control a MEGA account or a public folder.
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

#include <megaapi.h>
#include <set>

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MSortOrderType {
        ORDER_NONE, ORDER_DEFAULT_ASC, ORDER_DEFAULT_DESC,
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
        USER_ATTR_COUNTRY                   = 10,   // public - char array
        USER_ATTR_BIRTHDAY                  = 11,   // public - char array
        USER_ATTR_BIRTHMONTH                = 12,   // public - char array
        USER_ATTR_BIRTHYEAR                 = 13,   // public - char array
        USER_ATTR_LANGUAGE                  = 14,   // private - char array
        USER_ATTR_PWD_REMINDER              = 15,   // private - char array
        USER_ATTR_DISABLE_VERSIONS          = 16,   // private - byte array
        USER_ATTR_CONTACT_LINK_VERIFICATION = 17    // private - byte array
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
        RETRY_UNKNOWN       = 6
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

        //Utils
        String^ getBase64PwKey(String^ password);
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

        //API requests
        void login(String^ email, String^ password, MRequestListenerInterface^ listener);
        void login(String^ email, String^ password);
        String^ getSequenceNumber();
        String^ dumpSession();
        String^ dumpXMPPSession();
        void fastLogin(String^ email, String^ stringHash, String^ base64pwkey, MRequestListenerInterface^ listener);
        void fastLogin(String^ email, String^ stringHash, String^ base64pwkey);
        void fastLogin(String^ session, MRequestListenerInterface^ listener);
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
        void createAccount(String^ email, String^ password, String^ firstname, String^ lastname, MRequestListenerInterface^ listener);
        void createAccount(String^ email, String^ password, String^ firstname, String^ lastname);
        void fastCreateAccount(String^ email, String^ base64pwkey, String^ name, MRequestListenerInterface^ listener);
        void fastCreateAccount(String^ email, String^ base64pwkey, String^ name);
        void resumeCreateAccount(String^ sid, MRequestListenerInterface^ listener);
        void resumeCreateAccount(String^ sid);
        void sendSignupLink(String^ email, String^ name, String^ password, MRequestListenerInterface^ listener);
        void sendSignupLink(String^ email, String^ name, String^ password);
        void fastSendSignupLink(String^ email, String^ base64pwkey, String^ name, MRequestListenerInterface^ listener);
        void fastSendSignupLink(String^ email, String^ base64pwkey, String^ name);
        void querySignupLink(String^ link, MRequestListenerInterface^ listener);
        void querySignupLink(String^ link);
        void confirmAccount(String^ link, String^ password, MRequestListenerInterface^ listener);
        void confirmAccount(String^ link, String^ password);
        void fastConfirmAccount(String^ link, String^ base64pwkey, MRequestListenerInterface^ listener);
        void fastConfirmAccount(String^ link, String^ base64pwkey);
        void resetPassword(String^ email, bool hasMasterKey, MRequestListenerInterface^ listener);
        void resetPassword(String^ email, bool hasMasterKey);
        void queryResetPasswordLink(String^ link, MRequestListenerInterface^ listener);
        void queryResetPasswordLink(String^ link);
        void confirmResetPassword(String^ link, String^ newPwd, String^ masterKey, MRequestListenerInterface^ listener);
        void confirmResetPassword(String^ link, String^ newPwd, String^ masterKey);
        void confirmResetPasswordWithoutMasterKey(String^ link, String^ newPwd, MRequestListenerInterface^ listener);
        void confirmResetPasswordWithoutMasterKey(String^ link, String^ newPwd);
        void cancelAccount(MRequestListenerInterface^ listener);
        void cancelAccount();
        void queryCancelLink(String^ link, MRequestListenerInterface^ listener);
        void queryCancelLink(String^ link);
        void confirmCancelAccount(String^ link, String^ pwd, MRequestListenerInterface^ listener);
        void confirmCancelAccount(String^ link, String^ pwd);
        void changeEmail(String^ email, MRequestListenerInterface^ listener);
        void changeEmail(String^ email);
        void queryChangeEmailLink(String^ link, MRequestListenerInterface^ listener);
        void queryChangeEmailLink(String^ link);
        void confirmChangeEmail(String^ link, String^ pwd, MRequestListenerInterface^ listener);
        void confirmChangeEmail(String^ link, String^ pwd);
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
         * @param listener MegaRequestListener to track this request
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
        * @param listener MegaRequestListener to track this request
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
        void getUserAttribute(MUser^ user, int type, MRequestListenerInterface^ listener);
        void getUserAttribute(MUser^ user, int type);
        void getUserEmail(MegaHandle handle, MRequestListenerInterface^ listener);
        void getUserEmail(MegaHandle handle);
        void getOwnUserAttribute(int type, MRequestListenerInterface^ listener);
        void getOwnUserAttribute(int type);
        void setUserAttribute(int type, String^ value, MRequestListenerInterface^ listener);
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
        void getPaymentId(uint64 productHandle, MRequestListenerInterface^ listener);
        void getPaymentId(uint64 productHandle);
        void upgradeAccount(uint64 productHandle, int paymentMethod, MRequestListenerInterface^ listener);
        void upgradeAccount(uint64 productHandle, int paymentMethod);
        void submitPurchaseReceipt(int gateway, String^ receipt, MRequestListenerInterface^ listener);
        void submitPurchaseReceipt(int gateway, String^ receipt);
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
        * @param listener MegaRequestListener to track this request
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
        * @param listener MegaRequestListener to track this request
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
        * @param atLogout True if the check is being done just before a logout
        * @param listener MegaRequestListener to track this request
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
        * @brief Change the password of the MEGA account
        *
        * The associated request type with this request is MRequest::TYPE_CHANGE_PW
        * Valid data in the MegaRequest object received on callbacks:
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
        * Valid data in the MegaRequest object received on callbacks:
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
        * Valid data in the MegaRequest object received on callbacks:
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
        * Valid data in the MegaRequest object received on callbacks:
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
        void startDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener);
        void startDownload(MNode^ node, String^ localPath);
        void startDownloadWithAppData(MNode^ node, String^ localPath, String^ appData, MTransferListenerInterface^ listener);
        void startDownloadWithAppData(MNode^ node, String^ localPath, String^ appData);
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
        * - MRetryReason::RETRY_UNKNOWN = 6
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

        uint64 getBandwidthOverquotaDelay();

        MNodeList^ search(MNode^ node, String^ searchString, bool recursive);
        MNodeList^ search(MNode^ node, String^ searchString);
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
         * - MRequest::getParamType - Returns the value MegaSDK::USER_ATTR_CONTACT_LINK_VERIFICATION
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
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MRequest::getText - "0" for disable, "1" for enable
         * - MRequest::getFlag - false if disabled, true if enabled
         *
         * @param listener MegaRequestListener to track this request
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
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
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
        * Even if the automatic logout is disabled, a request of the type MegaRequestType::TYPE_LOGOUT
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
