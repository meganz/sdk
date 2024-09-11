#ifndef MEGA_USER_ATTRIBUTE_TYPES_H
#define MEGA_USER_ATTRIBUTE_TYPES_H

#include <cstddef> // required to define size_t

namespace mega
{

enum attr_t
{
    ATTR_UNKNOWN = -1,
    ATTR_AVATAR = 0, // public - byte array (image) or "none" magic value - non-versioned
    ATTR_FIRSTNAME = 1, // protected - char array - non-versioned
    ATTR_LASTNAME = 2, // protected - char array - non-versioned
    ATTR_AUTHRING = 3, // (deprecated) private - byte array - versioned
    ATTR_LAST_INT = 4, // private - byte array - versioned
    ATTR_ED25519_PUBK = 5, // public - byte array - versioned
    ATTR_CU25519_PUBK = 6, // public - byte array - versioned
    ATTR_KEYRING = 7, // (deprecated) private - byte array - versioned
    ATTR_SIG_RSA_PUBK = 8, // public - byte array - versioned
    ATTR_SIG_CU255_PUBK = 9, // public - byte array - versioned
    ATTR_COUNTRY = 10, // private - char array - non-versioned
    ATTR_BIRTHDAY = 11, // private - char array - non-versioned
    ATTR_BIRTHMONTH = 12, // private - char array - non-versioned
    ATTR_BIRTHYEAR = 13, // private - char array - non-versioned
    ATTR_LANGUAGE = 14, // private, non-encrypted - char array - non-versioned
    ATTR_PWD_REMINDER = 15, // private, non-encrypted - char array - non-versioned
    ATTR_DISABLE_VERSIONS = 16, // private, non-encrypted - char array - non-versioned
    ATTR_CONTACT_LINK_VERIFICATION = 17, // private, non-encrypted - char array - versioned
    ATTR_RICH_PREVIEWS = 18, // private - byte array - non-versioned
    ATTR_RUBBISH_TIME = 19, // private, non-encrypted - char array - non-versioned
    ATTR_LAST_PSA = 20, // private - char array - non-versioned
    ATTR_STORAGE_STATE = 21, // private, non-encrypted - char array - non-versioned
    ATTR_GEOLOCATION = 22, // private - byte array - non-versioned
    ATTR_CAMERA_UPLOADS_FOLDER = 23, // private - byte array - versioned
    ATTR_MY_CHAT_FILES_FOLDER = 24, // private - byte array - non-versioned
    ATTR_PUSH_SETTINGS = 25, // private, non-encrypted - char array - non-versioned
    ATTR_UNSHAREABLE_KEY = 26, // private - char array - versioned
    ATTR_ALIAS = 27, // private - byte array - versioned
    // ATTR_AUTHRSA = 28, // (deprecated) private - byte array
    ATTR_AUTHCU255 = 29, // (deprecated) private - byte array - versioned
    ATTR_DEVICE_NAMES = 30, // private - byte array - versioned
    ATTR_MY_BACKUPS_FOLDER = 31, // private, non-encrypted - char array - non-versioned
    // ATTR_BACKUP_NAMES = 32, // (deprecated) private - byte array - versioned
    ATTR_COOKIE_SETTINGS = 33, // private - byte array - non-versioned
    ATTR_JSON_SYNC_CONFIG_DATA = 34, // private - byte array - non-versioned
    // ATTR_DRIVE_NAMES = 35, // private - byte array - versioned (merged with ATTR_DEVICE_NAMES and
    // removed)
    ATTR_NO_CALLKIT = 36, // private, non-encrypted - char array - non-versioned
    ATTR_KEYS = 37, // private, non-encrypted (but encrypted to derived key from MK) - byte array -
                    // versioned
    ATTR_APPS_PREFS = 38, // private - byte array - versioned
    ATTR_CC_PREFS = 39, // private - byte array - versioned
    ATTR_VISIBLE_WELCOME_DIALOG = 40, // private, non-encrypted - byte array - versioned
    ATTR_VISIBLE_TERMS_OF_SERVICE = 41, // private, non-encrypted - byte array - versioned
    ATTR_PWM_BASE = 42, // private, non-encrypted (controlled by API) - char array - non-versioned
    ATTR_ENABLE_TEST_NOTIFICATIONS = 43, // private, non-encrypted - char array - versioned
    ATTR_LAST_READ_NOTIFICATION = 44, // private, non-encrypted - char array - versioned
    ATTR_LAST_ACTIONED_BANNER = 45, // private, non-encrypted - char array - versioned
    ATTR_ENABLE_TEST_SURVEYS = 46, // private - non-encrypted - char array - non-versioned
};

enum UserAttrScope : char
{
    ATTR_SCOPE_UNKNOWN = '\0',
    ATTR_SCOPE_PUBLIC_UNENCRYPTED = '+',
    ATTR_SCOPE_PROTECTED_UNENCRYPTED = '#', // contacts can fetch it but not give it out
                                            // to non-contacts
    ATTR_SCOPE_PRIVATE_UNENCRYPTED = '^', // can only be fetched by you
    ATTR_SCOPE_PRIVATE_ENCRYPTED = '*', // can only be fetched by you, and API cannot read it
    ATTR_SCOPE_BUSINESS_UNENCRYPTED = '%', // probably not used
    ATTR_SCOPE_BUSINESS_ENCRYPTED = '$', // not used
};

static constexpr size_t MAX_USER_VAR_SIZE =
    16 * 1024 * 1024; // 16MB - User attributes whose second character is ! or ~ (per example
                      // *!dn, ^!keys", ...)
static constexpr size_t MAX_USER_ATTRIBUTE_SIZE = 64 * 1024; // 64kB  - Other user attributes

} // namespace

#endif // MEGA_USER_ATTRIBUTE_TYPES_H
