/**
* @file MUser.h
* @brief Represents an user in MEGA
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

#include "megaapi.h"

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MUserVisibility
    {
        VISIBILITY_UNKNOWN  = -1,
        VISIBILITY_HIDDEN   = 0,
        VISIBILITY_VISIBLE  = 1,
        VISIBILITY_INACTIVE = 2,
        VISIBILITY_BLOCKED  = 3
    };

    public enum class MUserChangeType
    {
        CHANGE_TYPE_AUTHRING                    = 0x01,
        CHANGE_TYPE_LSTINT                      = 0x02,
        CHANGE_TYPE_AVATAR                      = 0x04,
        CHANGE_TYPE_FIRSTNAME                   = 0x08,
        CHANGE_TYPE_LASTNAME                    = 0x10,
        CHANGE_TYPE_EMAIL                       = 0x20,
        CHANGE_TYPE_KEYRING                     = 0x40,
        CHANGE_TYPE_COUNTRY                     = 0x80,
        CHANGE_TYPE_BIRTHDAY                    = 0x100,
        CHANGE_TYPE_PUBKEY_CU255                = 0x200,
        CHANGE_TYPE_PUBKEY_ED255                = 0x400,
        CHANGE_TYPE_SIG_PUBKEY_RSA              = 0x800,
        CHANGE_TYPE_SIG_PUBKEY_CU255            = 0x1000,
        CHANGE_TYPE_LANGUAGE                    = 0x2000,
        CHANGE_TYPE_PWD_REMINDER                = 0x4000,
        CHANGE_TYPE_DISABLE_VERSIONS            = 0x8000,
        CHANGE_TYPE_CONTACT_LINK_VERIFICATION   = 0x10000,
        CHANGE_TYPE_RICH_PREVIEWS               = 0x20000,
        CHANGE_TYPE_RUBBISH_TIME                = 0x40000
    };

    public ref class MUser sealed
    {
        friend ref class MegaSDK;
        friend ref class MUserList;

    public:
        virtual ~MUser();

        /**
        * @brief Creates a copy of this MUser object.
        *
        * The resulting object is fully independent of the source MUser,
        * it contains a copy of all internal attributes, so it will be valid after
        * the original object is deleted.
        *
        * You are the owner of the returned object
        *
        * @return Copy of the MUser object
        */
        MUser^ copy();

        /**
        * @brief Returns the email associated with the contact.
        *
        * The email can be used to recover the MUser object later using MegaSDK::getContact
        *
        * The MUser object retains the ownership of the returned string, it will be valid until
        * the MUser object is deleted.
        *
        * @return The email associated with the contact.
        */
        String^ getEmail();

        /**
        * @brief Returns the handle associated with the contact.
        *
        * @return The handle associated with the contact.
        */
        uint64 getHandle();

        /**
        * @brief Get the current visibility of the contact
        *
        * The returned value will be one of these:
        *
        * - VISIBILITY_UNKNOWN = -1
        * The visibility of the contact isn't know
        *
        * - VISIBILITY_HIDDEN = 0
        * The contact is currently hidden
        *
        * - VISIBILITY_VISIBLE = 1
        * The contact is currently visible
        *
        * - VISIBILITY_INACTIVE = 2
        * The contact is currently inactive
        *
        * - VISIBILITY_BLOCKED = 3
        * The contact is currently blocked
        *
        * @note The visibility of your own user is undefined and shouldn't be used.
        * @return Current visibility of the contact
        */
        MUserVisibility getVisibility();

        /**
        * @brief Returns the timestamp when the contact was added to the contact list (in seconds since the epoch)
        * @return Timestamp when the contact was added to the contact list (in seconds since the epoch)
        */
        uint64 getTimestamp();

        /**
        * @brief Returns true if this user has an specific change
        *
        * This value is only useful for users notified by MListener::onUsersUpdate or
        * MGlobalListener::onUsersUpdate that can notify about user modifications.
        *
        * In other cases, the return value of this function will be always false.
        *
        * @param changeType The type of change to check. It can be one of the following values:
        *
        * - MUserChangeType::CHANGE_TYPE_AUTH            = 0x01
        * Check if the user has new or modified authentication information
        *
        * - MUserChangeType::CHANGE_TYPE_LSTINT          = 0x02
        * Check if the last interaction timestamp is modified
        *
        * - MUserChangeType::CHANGE_TYPE_AVATAR          = 0x04
        * Check if the user has a new or modified avatar image, or if the avatar was removed
        *
        * - MUserChangeType::CHANGE_TYPE_FIRSTNAME       = 0x08
        * Check if the user has new or modified firstname
        *
        * - MUserChangeType::CHANGE_TYPE_LASTNAME        = 0x10
        * Check if the user has new or modified lastname
        *
        * - MUserChangeType::CHANGE_TYPE_EMAIL           = 0x20
        * Check if the user has modified email
        *
        * - MUserChangeType::CHANGE_TYPE_KEYRING        = 0x40
        * Check if the user has new or modified keyring
        *
        * - MUserChangeType::CHANGE_TYPE_COUNTRY        = 0x80
        * Check if the user has new or modified country
        *
        * - MUserChangeType::CHANGE_TYPE_BIRTHDAY        = 0x100
        * Check if the user has new or modified birthday, birthmonth or birthyear
        *
        * - MUserChangeType::CHANGE_TYPE_PUBKEY_CU255    = 0x200
        * Check if the user has new or modified public key for chat
        *
        * - MUserChangeType::CHANGE_TYPE_PUBKEY_ED255    = 0x400
        * Check if the user has new or modified public key for signing
        *
        * - MUserChangeType::CHANGE_TYPE_SIG_PUBKEY_RSA  = 0x800
        * Check if the user has new or modified signature for RSA public key
        *
        * - MUserChangeType::CHANGE_TYPE_SIG_PUBKEY_CU255 = 0x1000
        * Check if the user has new or modified signature for Cu25519 public key
        *
        * - MUserChangeType::CHANGE_TYPE_LANGUAGE         = 0x2000
        * Check if the user has modified the preferred language
        *
        * - MUserChangeType::CHANGE_TYPE_PWD_REMINDER     = 0x4000
        * Check if the data related to the password reminder dialog has changed
        *
        * - MUserChangeType::CHANGE_TYPE_DISABLE_VERSIONS     = 0x8000
        * Check if option for file versioning has changed
        *
        * - MUserChangeType::CHANGE_TYPE_CONTACT_LINK_VERIFICATION = 0x10000
        * Check if option for automatic contact-link verification has changed
        *
        * - MUserChangeType::CHANGE_TYPE_RICH_PREVIEWS    = 0x20000
        * Check if option for rich links has changed
        *
        * - MUserChangeType::CHANGE_TYPE_RUBBISH_TIME    = 0x40000
        * Check if rubbish time for autopurge has changed
        *
        * @return true if this user has an specific change
        */
        bool hasChanged(int changeType);

        /**
        * @brief Returns a bit field with the changes of the user
        *
        * This value is only useful for users notified by MListener::onUsersUpdate or
        * MGlobalListener::onUsersUpdate that can notify about user modifications.
        *
        * @return The returned value is an OR combination of these flags:
        *
        * - MUserChangeType::CHANGE_TYPE_AUTH            = 0x01
        * Check if the user has new or modified authentication information
        *
        * - MUserChangeType::CHANGE_TYPE_LSTINT          = 0x02
        * Check if the last interaction timestamp is modified
        *
        * - MUserChangeType::CHANGE_TYPE_AVATAR          = 0x04
        * Check if the user has a new or modified avatar image
        *
        * - MUserChangeType::CHANGE_TYPE_FIRSTNAME       = 0x08
        * Check if the user has new or modified firstname
        *
        * - MUserChangeType::CHANGE_TYPE_LASTNAME        = 0x10
        * Check if the user has new or modified lastname
        *
        * - MUserChangeType::CHANGE_TYPE_EMAIL           = 0x20
        * Check if the user has modified email
        *
        * - MUserChangeType::CHANGE_TYPE_KEYRING        = 0x40
        * Check if the user has new or modified keyring
        *
        * - MUserChangeType::CHANGE_TYPE_COUNTRY        = 0x80
        * Check if the user has new or modified country
        *
        * - MUserChangeType::CHANGE_TYPE_BIRTHDAY        = 0x100
        * Check if the user has new or modified birthday, birthmonth or birthyear
        *
        * - MUserChangeType::CHANGE_TYPE_PUBKEY_CU255    = 0x200
        * Check if the user has new or modified public key for chat
        *
        * - MUserChangeType::CHANGE_TYPE_PUBKEY_ED255    = 0x400
        * Check if the user has new or modified public key for signing
        *
        * - MUserChangeType::CHANGE_TYPE_SIG_PUBKEY_RSA  = 0x800
        * Check if the user has new or modified signature for RSA public key
        *
        * - MUserChangeType::CHANGE_TYPE_SIG_PUBKEY_CU255 = 0x1000
        * Check if the user has new or modified signature for Cu25519 public key
        *
        * - MUserChangeType::CHANGE_TYPE_LANGUAGE         = 0x2000
        * Check if the user has modified the preferred language
        *
        * - MUserChangeType::CHANGE_TYPE_PWD_REMINDER     = 0x4000
        * Check if the data related to the password reminder dialog has changed
        *
        * - MUserChangeType::CHANGE_TYPE_DISABLE_VERSIONS     = 0x8000
        * Check if option for file versioning has changed
        *
        * - MUserChangeType::CHANGE_TYPE_CONTACT_LINK_VERIFICATION = 0x10000
        * Check if option for automatic contact-link verification has changed
        *
        * - MUserChangeType::CHANGE_TYPE_RICH_PREVIEWS    = 0x20000
        * Check if option for rich links has changed
        *
        * - MUserChangeType::CHANGE_TYPE_RUBBISH_TIME    = 0x40000
        * Check if rubbish time for autopurge has changed
        */
        int getChanges();

        /**
        * @brief Indicates if the user is changed by yourself or by another client.
        *
        * This value is only useful for users notified by MListener::onUsersUpdate or
        * MGlobalListener::onUsersUpdate that can notify about user modifications.
        *
        * @return 0 if the change is external. >0 if the change is the result of an
        * explicit request, -1 if the change is the result of an implicit request
        * made by the SDK internally.
        */
        int isOwnChange();

    private:
        MUser(MegaUser *megaUser, bool cMemoryOwn);
        MegaUser *megaUser;
        bool cMemoryOwn;
        MegaUser *getCPtr();
    };
}
