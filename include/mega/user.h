/**
 * @file mega/user.h
 * @brief Class for manipulating user / contact data
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

#ifndef MEGA_USER_H
#define MEGA_USER_H 1

#include "attrmap.h"

namespace mega {

class UserAttributeManager;

// user/contact
struct MEGA_API User : public Cacheable
{
    // user handle
    handle userhandle;

    // string identifier for API requests (either e-mail address or ASCII user
    // handle)
    string uid;

    // e-mail address
    string email;

    // visibility status
    visibility_t show;

    // shares by this user
    handle_set sharing;

    // contact establishment timestamp
    m_time_t ctime;

    BizMode mBizMode = BIZ_MODE_UNKNOWN;

    struct
    {
        bool keyring : 1;   // private keys
        bool authring : 1;  // authentication information of the contact (signing key)
        bool authcu255 : 1; // authentication information of the contact (Cu25519 key)
        bool lstint : 1;    // last interaction with the contact
        bool puEd255 : 1;   // public key for Ed25519
        bool puCu255 : 1;   // public key for Cu25519
        bool sigPubk : 1;   // signature for RSA public key
        bool sigCu255 : 1;  // signature for Cu255199 public key
        bool avatar : 1;    // avatar image
        bool firstname : 1;
        bool lastname : 1;
        bool country : 1;
        bool birthday : 1;      // wraps status of birthday, birthmonth, birthyear
        bool email : 1;
        bool language : 1;      // preferred language code
        bool pwdReminder : 1;   // password-reminder-dialog information
        bool disableVersions : 1;   // disable fileversioning
        bool noCallKit : 1;   // disable CallKit
        bool contactLinkVerification : 1; // Verify contact requests with contact links
        bool richPreviews : 1;  // enable messages with rich previews
        bool lastPsa : 1;
        bool rubbishTime : 1;   // days to keep nodes in rubbish bin before auto clean
        bool storageState : 1;  // state of the storage (0 = green, 1 = orange, 2 = red)
        bool geolocation : 1;   // enable send geolocations
        bool cameraUploadsFolder : 1;   // target folder for Camera Uploads
        bool myChatFilesFolder : 1;   // target folder for my chat files
        bool pushSettings : 1;  // push notification settings
        bool alias : 1; // user's aliases
        bool unshareablekey : 1;    // key to encrypt unshareable node attributes
        bool devicenames : 1; // device or external drive names
        bool myBackupsFolder : 1; // target folder for My Backups
        bool cookieSettings : 1; // bit map to indicate whether some cookies are enabled or not
        bool jsonSyncConfigData : 1;
        bool drivenames : 1;    // drive names
        bool keys : 1;
        bool aPrefs : 1;    // apps preferences
        bool ccPrefs : 1;   // content consumption preferences
        bool enableTestNotifications : 1; // list of IDs for enabled notifications
        bool lastReadNotification : 1; // ID of last read notification
        bool lastActionedBanner : 1; // ID of last actioner banner
        bool enableTestSurveys: 1; // list of handles for enabled test surveys
    } changed;

    // user's public key
    AsymmCipher pubk;
    struct
    {
        bool pubkrequested : 1;
        bool isTemporary : 1;
    };

    // actions to take after arrival of the public key
    deque<std::unique_ptr<PubKeyAction>> pkrs;

private:
    std::unique_ptr<UserAttributeManager> mAttributeManager;

    // source tag
    int tag;

public:
    void set(visibility_t, m_time_t);

    bool serialize(string*) const override;
    static User* unserialize(class MegaClient *, string*);
    bool unserializeAttributes(const char*& from, const char* upTo, char formatVersion);

    void removepkrs(MegaClient*);

    // attribute methods: set/get/expire...
    void setAttribute(attr_t at, const string& value, const string& version);
    bool setAttributeIfDifferentVersion(attr_t at, const string& value, const string& version);
    const string *getattr(attr_t at);
    const string *getattrversion(attr_t at);
    void setAttributeExpired(attr_t at);
    bool isattrvalid(attr_t at);
    void removeAttribute(attr_t at);
    void removeAttributeUpdateVersion(attr_t at, const string& version); // remove in up2/upv V3 ?

    void cacheNonExistingAttributes();
    // Returns true if attribute was cached as non-existing. Avoid requesting it from server.
    bool nonExistingAttribute(attr_t at) const;

    static string attr2string(attr_t at);
    static string attr2longname(attr_t at);
    static attr_t string2attr(const char *name);
    static int needversioning(attr_t at);
    static char scope(attr_t at);
    static bool isAuthring(attr_t at);
    static size_t getMaxAttributeSize(attr_t at);

    enum {
        PWD_LAST_SUCCESS = 0x01,
        PWD_LAST_SKIPPED = 0x02,
        PWD_MK_EXPORTED = 0x04,
        PWD_DONT_SHOW = 0x08,
        PWD_LAST_LOGIN = 0x10
    };

    static const int PWD_SHOW_AFTER_ACCOUNT_AGE = 7 * 24 * 60 * 60;
    static const int PWD_SHOW_AFTER_LASTSUCCESS = 3 * 30 * 24 * 60 * 60;
    static const int PWD_SHOW_AFTER_LASTLOGIN = 14 * 24 * 60 * 60;
    static const int PWD_SHOW_AFTER_LASTSKIP = 3 * 30 * 24 * 60 * 60;
    static const int PWD_SHOW_AFTER_LASTSKIP_LOGOUT = 1 * 30 * 24 * 60 * 60;

    static bool mergePwdReminderData(int numDetails, const char *data, unsigned int size, string *newValue);
    static m_time_t getPwdReminderData(int numDetail, const char *data, unsigned int size);

    bool setChanged(attr_t at);

    void setTag(int tag);
    int getTag();
    void resetTag();

    User(const char* = NULL);
    ~User() override;

    // merges the new values in the given TLV. Returns true if TLV is changed.
    static bool mergeUserAttribute(attr_t type, const string_map &newValuesMap, TLVstore &tlv);
    static string attributePrefixInTLV(attr_t type, bool modifier);
};

class AuthRing
{
public:
    // create authring of 'type' from the encrypted TLV container
    AuthRing(attr_t type, const TLVstore &authring);

    // create authring of 'type' from the TLV value (undecrypted already, no Type nor Length)
    AuthRing(attr_t type, const string& authring);

    // return true if authring has changed (data can be pubKey or keySignature depending on authMethod)
    void add(handle uh, const std::string &fingerprint, AuthMethod authMethod);

    // assumes the key is already tracked for uh (otherwise, it will throw)
    void update(handle uh, AuthMethod authMethod);

    // return the authring as tlv container, ready to set as user's attribute [*!authring | *!authCu255 | *!authRSA]
    std::string *serialize(PrnGen &rng, SymmCipher &key) const;

    // return a binary buffer compatible with Webclient, to store authrings in user's attribute ^!keys
    std::string serializeForJS() const;

    // false if uh is not tracked in the authring
    bool isTracked(handle uh) const;

    // true for Cu25519 and RSA, false for Ed25519
    bool isSignedKey() const;

    // true if key is tracked and authentication method is fingerprint/signature-verified
    bool areCredentialsVerified(handle uh) const;

    // returns AUTH_METHOD_UNKNOWN if no authentication is found for the given user
    AuthMethod getAuthMethod(handle uh) const;

    // returns the fingerprint of the public key for a given user, or empty string if user is not found
    string getFingerprint(handle uh) const;

    // returns the list of tracked users
    vector<handle> getTrackedUsers() const;

    // returns most significant 160 bits from SHA256, whether in binary or hexadecimal
    static string fingerprint(const string &pubKey, bool hexadecimal = false);

    // returns the authring type for a given attribute type associated to a public key
    static attr_t keyTypeToAuthringType(attr_t at);

    // returns the authring type for a given attribute type associated to a signature
    static attr_t signatureTypeToAuthringType(attr_t at);

    // returns the attribute type associated to the corresponding signature for a given authring type
    static attr_t authringTypeToSignatureType(attr_t at);

    // returns a human-friendly string for a given authentication method
    static string authMethodToStr(AuthMethod authMethod);

    static string toString(const AuthRing& authRing);

    bool needsUpdate() const { return mNeedsUpdate; }

private:
    attr_t mType;
    map<handle, string> mFingerprint;
    map<handle, AuthMethod> mAuthMethod;

    // indicates if the authring has changed and needs to update value in server
    bool mNeedsUpdate = false;

    bool deserialize(const std::string &authValue);
};

} // namespace

#endif
