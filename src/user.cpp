/**
 * @file user.cpp
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

#include "mega/user.h"

#include "mega/base64.h"
#include "mega/logging.h"
#include "mega/megaclient.h"
#include "mega/tlv.h"
#include "mega/user_attribute_manager.h"

namespace mega {

User::User(const char* cemail):
    mAttributeManager{std::make_unique<UserAttributeManager>()}
{
    userhandle = UNDEF;
    show = VISIBILITY_UNKNOWN;
    ctime = 0;
    pubkrequested = false;
    isTemporary = false;
    resetTag();

    if (cemail)
    {
        email = cemail;
    }

    memset(&changed, 0, sizeof(changed));
}

// Use explicit destructor here to allow using smart pointers with incomplete type in class
// definition.
User::~User() = default;

bool User::mergeUserAttribute(attr_t type, const string_map& newValuesMap, string_map& destination)
{
    bool modified = false;

    for (const auto &it : newValuesMap)
    {
        const string& key = it.first;
        const string& newValue = it.second;
        string currentValue;
        if (auto itD = destination.find(key); itD != destination.end() && !itD->second.empty())
        {
            Base64::btoa(itD->second, currentValue);
        }
        if (newValue != currentValue)
        {
            if ((type == ATTR_ALIAS
                 || type == ATTR_DEVICE_NAMES
                 || type == ATTR_CC_PREFS
                 || type == ATTR_APPS_PREFS) && newValue[0] == '\0')
            {
                // alias/deviceName/appPrefs being removed
                destination.erase(key);
            }
            else
            {
                destination[key] = Base64::atob(newValue);
            }
            modified = true;
        }
    }

    return modified;
}

bool User::serialize(string* d) const
{
    unsigned char l;
    time_t ts;
    AttrMap attrmap;

    d->reserve(d->size() + 100 + attrmap.storagesize(10));

    d->append((char*)&userhandle, sizeof userhandle);

    // FIXME: use m_time_t & Serialize64 instead
    ts = ctime;
    d->append((char*)&ts, sizeof ts);
    d->append((char*)&show, sizeof show);

    l = (unsigned char)email.size();
    d->append((char*)&l, sizeof l);
    d->append(email.c_str(), l);

    mAttributeManager->serializeAttributeFormatVersion(*d);

    char bizMode = 0;
    if (mBizMode != BIZ_MODE_UNKNOWN) // convert number to ascii
    {
        bizMode = static_cast<char>('0' + mBizMode);
    }

    d->append((char*)&bizMode, 1);
    d->append("\0\0\0\0\0", 6);

    // serialization of attributes
    mAttributeManager->serializeAttributes(*d);

    if (pubk.isvalid())
    {
        pubk.serializekey(d, AsymmCipher::PUBKEY);
    }

    return true;
}

User* User::unserialize(MegaClient* client, string* d)
{
    handle uh;
    time_t ts;
    visibility_t v;
    unsigned char l;
    string m;
    User* u;
    const char* ptr = d->data();
    const char* end = ptr + d->size();
    int i;

    if (ptr + sizeof(handle) + sizeof(time_t) + sizeof(visibility_t) + 2 > end)
    {
        return NULL;
    }

    uh = MemAccess::get<handle>(ptr);
    ptr += sizeof uh;

    // FIXME: use m_time_t & Serialize64
    ts = MemAccess::get<time_t>(ptr);
    ptr += sizeof ts;

    v = MemAccess::get<visibility_t>(ptr);
    ptr += sizeof v;

    l = static_cast<unsigned char>(*ptr++);
    if (l)
    {
        if (ptr + l > end)
        {
            return NULL;
        }
        m.assign(ptr, l);
    }
    ptr += l;

    if (ptr + sizeof(char) + sizeof(char) > end)
    {
        return NULL;
    }

    char attrVersion = UserAttributeManager::unserializeAttributeFormatVersion(ptr);

    char bizModeValue = MemAccess::get<char>(ptr);
    ptr += sizeof(bizModeValue);
    BizMode bizMode;
    switch (bizModeValue)
    {
        case '0':
            bizMode = BIZ_MODE_SUBUSER;
            break;
        case '1':
            bizMode = BIZ_MODE_MASTER;
            break;
        default:
            bizMode = BIZ_MODE_UNKNOWN;
            break;
    }

    for (i = 6; i--;)
    {
        if (ptr + MemAccess::get<unsigned char>(ptr) < end)
        {
            ptr += MemAccess::get<unsigned char>(ptr) + 1;
        }
    }

    if (i >= 0)
    {
        return NULL;
    }

    u = client->finduser(uh, 1);
    if (!u)
    {
        return NULL;
    }

    client->mapuser(uh, m.c_str());
    u->set(v, ts);
    u->resetTag();
    u->mBizMode = bizMode;

    if (!u->unserializeAttributes(ptr, end, attrVersion))
    {
        client->discarduser(uh);
        return nullptr;
    }

    // initialize private Ed25519 and Cu25519 from cache
    if (u->userhandle == client->me)
    {
        string prEd255, prCu255;

        const UserAttribute* keysAttribute = u->getAttribute(ATTR_KEYS);
        if (keysAttribute && !keysAttribute->isNotExisting())
        {
            client->mKeyManager.setKey(client->key);
            if (client->mKeyManager.fromKeysContainer(keysAttribute->value()))
            {
                prEd255 = client->mKeyManager.privEd25519();
                prCu255 = client->mKeyManager.privCu25519();
            }
        }

        if (!client->mKeyManager.generation())
        {
            const UserAttribute* attribute = u->getAttribute(ATTR_KEYRING);
            if (attribute && attribute->isValid())
            {
                unique_ptr<string_map> records{
                    tlv::containerToRecords(attribute->value(), client->key)};
                if (records)
                {
                    prEd255.swap((*records)[EdDSA::TLV_KEY]);
                    prCu255.swap((*records)[ECDH::TLV_KEY]);
                }
                else
                {
                    LOG_warn << "Failed to decrypt keyring from cache";
                }
            }
        }

        if (prEd255.size())
        {
            client->mEd255Key = new EdDSA(client->rng, (unsigned char*)prEd255.data());
            if (!client->mEd255Key->initializationOK)
            {
                delete client->mEd255Key;
                client->mEd255Key = NULL;
                LOG_warn << "Failed to load chat key from local cache.";
            }
            else
            {
                LOG_info << "Signing key loaded from local cache.";
            }
        }
        if (prCu255.size())
        {
            client->mX255Key = new ECDH(prCu255);
            if (!client->mX255Key->initializationOK)
            {
                delete client->mX255Key;
                client->mX255Key = NULL;
                LOG_warn << "Failed to load chat key from local cache.";
            }
            else
            {
                LOG_info << "Chat key successfully loaded from local cache.";
            }
        }
    }

    if ((ptr < end) && !u->pubk.setkey(AsymmCipher::PUBKEY, (byte*)ptr, int(end - ptr)))
    {
        client->discarduser(uh);
        return NULL;
    }

    return u;
}

bool User::unserializeAttributes(const char*& from, const char* upTo, char formatVersion)
{
    return mAttributeManager->unserializeAttributes(from, upTo, formatVersion);
}

void User::removepkrs(MegaClient* client)
{
    while (!pkrs.empty())  // protect any pending pubKey request
    {
        auto& pka = pkrs.front();
        if (pka->cmd)
        {
            pka->cmd->invalidateUser();
        }
        pka->proc(client, this);
        pkrs.pop_front();
    }
}

void User::setAttribute(attr_t at, const string& value, const string& version)
{
    setChanged(at);
    mAttributeManager->set(at, value, version);
}

bool User::updateAttributeIfDifferentVersion(attr_t at, const string& value, const string& version)
{
    if (mAttributeManager->setIfNewVersion(at, value, version))
    {
        setChanged(at);
        return true;
    }

    return false;
}

void User::setAttributeExpired(attr_t at)
{
    if (mAttributeManager->setExpired(at))
    {
        setChanged(at);
    }
}

const UserAttribute* User::getAttribute(attr_t at) const
{
    return mAttributeManager->get(at);
}

void User::removeAttribute(attr_t at)
{
    if (mAttributeManager->erase(at))
    {
        setChanged(at);
    }
}

void User::removeAttributeUpdateVersion(attr_t at, const string& version)
{
    if (mAttributeManager->eraseUpdateVersion(at, version))
    {
        setChanged(at);
    }
}

void User::cacheNonExistingAttributes()
{
    mAttributeManager->cacheNonExistingAttributes();
}

string User::attr2string(attr_t type)
{
    return UserAttributeManager::getName(type);
}

string User::attr2longname(attr_t type)
{
    return UserAttributeManager::getLongName(type);
}


attr_t User::string2attr(const char* name)
{
    return UserAttributeManager::getType(name);
}

int User::needversioning(attr_t at)
{
    return UserAttributeManager::getVersioningEnabled(at);
}

char User::scope(attr_t at)
{
    return UserAttributeManager::getScope(at);
}

bool User::isAuthring(attr_t at)
{
    return (at == ATTR_AUTHRING || at == ATTR_AUTHCU255);
}

size_t User::getMaxAttributeSize(attr_t at)
{
    return UserAttributeManager::getMaxSize(at);
}

bool User::mergePwdReminderData(int numDetails, const char *data, unsigned int size, string *newValue)
{
    if (numDetails == 0)
    {
        return false;
    }

    // format: <lastSuccess>:<lastSkipped>:<mkExported>:<dontShowAgain>:<lastLogin>
    string oldValue;
    if (data && size)
    {
        oldValue.assign(data, size);

        // ensure the old value has a valid format
        if (std::count(oldValue.begin(), oldValue.end(), ':') != 4
                || oldValue.length() < 9)
        {
            oldValue = "0:0:0:0:0";
        }
    }
    else    // no existing value, set with default values and update it consequently
    {
        oldValue = "0:0:0:0:0";
    }

    bool lastSuccess = (numDetails & PWD_LAST_SUCCESS) != 0;
    bool lastSkipped = (numDetails & PWD_LAST_SKIPPED) != 0;
    bool mkExported = (numDetails & PWD_MK_EXPORTED) != 0;
    bool dontShowAgain = (numDetails & PWD_DONT_SHOW) != 0;
    bool lastLogin = (numDetails & PWD_LAST_LOGIN) != 0;

    bool changed = false;

    // Timestamp for last successful validation of password in PRD
    m_time_t tsLastSuccess;
    size_t len = oldValue.find(":");
    string buf = oldValue.substr(0, len) + "#"; // add character control '#' for conversion
    oldValue = oldValue.substr(len + 1);    // skip ':'
    if (lastSuccess)
    {
        changed = true;
        tsLastSuccess = m_time();
    }
    else
    {
        char *pEnd = NULL;
        tsLastSuccess = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastSuccess == LLONG_MAX || tsLastSuccess == LLONG_MIN)
        {
            tsLastSuccess = 0;
            changed = true;
        }
    }

    // Timestamp for last time the PRD was skipped
    m_time_t tsLastSkipped;
    len = oldValue.find(":");
    buf = oldValue.substr(0, len) + "#";
    oldValue = oldValue.substr(len + 1);
    if (lastSkipped)
    {
        tsLastSkipped = m_time();
        changed = true;
    }
    else
    {
        char *pEnd = NULL;
        tsLastSkipped = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastSkipped == LLONG_MAX || tsLastSkipped == LLONG_MIN)
        {
            tsLastSkipped = 0;
            changed = true;
        }
    }

    // Flag for Recovery Key exported
    bool flagMkExported;
    len = oldValue.find(":");
    if (len != 1)
    {
        return false;
    }
    buf = oldValue.substr(0, len) + "#";
    oldValue = oldValue.substr(len + 1);
    if (mkExported && !(buf.at(0) == '1'))
    {
        flagMkExported = true;
        changed = true;
    }
    else
    {
        char *pEnd = NULL;
        long tmp = strtol(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || (tmp != 0 && tmp != 1))
        {
            flagMkExported = false;
            changed = true;
        }
        else
        {
            flagMkExported = tmp != 0;
        }
    }

    // Flag for "Don't show again" the PRD
    bool flagDontShowAgain;
    len = oldValue.find(":");
    if (len != 1 || len + 1 == oldValue.length())
    {
        return false;
    }
    buf = oldValue.substr(0, len) + "#";
    oldValue = oldValue.substr(len + 1);
    if (dontShowAgain && !(buf.at(0) == '1'))
    {
        flagDontShowAgain = true;
        changed = true;
    }
    else
    {
        char *pEnd = NULL;
        long tmp = strtol(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || (tmp != 0 && tmp != 1))
        {
            flagDontShowAgain = false;
            changed = true;
        }
        else
        {
            flagDontShowAgain = tmp != 0;
        }
    }

    // Timestamp for last time user logged in
    m_time_t tsLastLogin = 0;
    len = oldValue.length();
    if (lastLogin)
    {
        tsLastLogin = m_time();
        changed = true;
    }
    else
    {
        buf = oldValue.substr(0, len) + "#";

        char *pEnd = NULL;
        tsLastLogin = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastLogin == LLONG_MAX || tsLastLogin == LLONG_MIN)
        {
            tsLastLogin = 0;
            changed = true;
        }
    }

    std::stringstream value;
    value << tsLastSuccess << ":" << tsLastSkipped << ":" << flagMkExported
        << ":" << flagDontShowAgain << ":" << tsLastLogin;

    *newValue = value.str();

    return changed;
}

m_time_t User::getPwdReminderData(int numDetail, const char *data, unsigned int size)
{
    if (!numDetail || !data || !size)
    {
        return 0;
    }

    // format: <lastSuccess>:<lastSkipped>:<mkExported>:<dontShowAgain>:<lastLogin>
    string value;
    value.assign(data, size);

    // ensure the value has a valid format
    if (std::count(value.begin(), value.end(), ':') != 4
            || value.length() < 9)
    {
        return 0;
    }

    bool lastSuccess = (numDetail & PWD_LAST_SUCCESS) != 0;
    bool lastSkipped = (numDetail & PWD_LAST_SKIPPED) != 0;
    bool mkExported = (numDetail & PWD_MK_EXPORTED) != 0;
    bool dontShowAgain = (numDetail & PWD_DONT_SHOW) != 0;
    bool lastLogin = (numDetail & PWD_LAST_LOGIN) != 0;

    // Timestamp for last successful validation of password in PRD
    m_time_t tsLastSuccess;
    size_t len = value.find(":");
    string buf = value.substr(0, len) + "#"; // add character control '#' for conversion
    value = value.substr(len + 1);    // skip ':'
    if (lastSuccess)
    {
        char *pEnd = NULL;
        tsLastSuccess = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastSuccess == LLONG_MAX || tsLastSuccess == LLONG_MIN)
        {
            tsLastSuccess = 0;
        }
        return tsLastSuccess;
    }

    // Timestamp for last time the PRD was skipped
    m_time_t tsLastSkipped;
    len = value.find(":");
    buf = value.substr(0, len) + "#";
    value = value.substr(len + 1);
    if (lastSkipped)
    {
        char *pEnd = NULL;
        tsLastSkipped = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastSkipped == LLONG_MAX || tsLastSkipped == LLONG_MIN)
        {
            tsLastSkipped = 0;
        }
        return tsLastSkipped;
    }

    // Flag for Recovery Key exported
    len = value.find(":");
    buf = value.substr(0, len) + "#";
    value = value.substr(len + 1);
    if (mkExported)
    {
        char *pEnd = NULL;
        m_time_t flagMkExported = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || (flagMkExported != 0 && flagMkExported != 1))
        {
            flagMkExported = 0;
        }
        return flagMkExported;
    }

    // Flag for "Don't show again" the PRD
    len = value.find(":");
    buf = value.substr(0, len) + "#";
    value = value.substr(len + 1);
    if (dontShowAgain)
    {
        char *pEnd = NULL;
        m_time_t flagDontShowAgain = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || (flagDontShowAgain != 0 && flagDontShowAgain != 1))
        {
            flagDontShowAgain = 0;
        }
        return flagDontShowAgain;
    }

    // Timestamp for last time user logged in
    m_time_t tsLastLogin = 0;
    len = value.length();
    if (lastLogin)
    {
        buf = value.substr(0, len) + "#";

        char *pEnd = NULL;
        tsLastLogin = strtoll(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastLogin == LLONG_MAX || tsLastLogin == LLONG_MIN)
        {
            tsLastLogin = 0;
        }
        return tsLastLogin;
    }

    return 0;
}

bool User::setChanged(attr_t at)
{
    switch(at)
    {
        case ATTR_AVATAR:
            changed.avatar = true;
            break;

        case ATTR_FIRSTNAME:
            changed.firstname = true;
            break;

        case ATTR_LASTNAME:
            changed.lastname = true;
            break;

        case ATTR_AUTHRING:
            changed.authring = true;
            break;

        case ATTR_AUTHCU255:
            changed.authcu255 = true;
            break;

        case ATTR_LAST_INT:
            changed.lstint = true;
            break;

        case ATTR_ED25519_PUBK:
            changed.puEd255 = true;
            break;

        case ATTR_CU25519_PUBK:
            changed.puCu255 = true;
            break;

        case ATTR_SIG_RSA_PUBK:
            changed.sigPubk = true;
            break;

        case ATTR_SIG_CU255_PUBK:
            changed.sigCu255 = true;
            break;

        case ATTR_KEYRING:
            changed.keyring = true;
            break;

        case ATTR_COUNTRY:
            changed.country = true;
            break;

        case ATTR_BIRTHDAY:
        case ATTR_BIRTHMONTH:
        case ATTR_BIRTHYEAR:
            changed.birthday = true;
            break;

        case ATTR_LANGUAGE:
            changed.language = true;
            break;

        case ATTR_PWD_REMINDER:
            changed.pwdReminder = true;
            break;

        case ATTR_DISABLE_VERSIONS:
            changed.disableVersions = true;
            break;

        case ATTR_NO_CALLKIT:
            changed.noCallKit = true;
            break;

        case ATTR_CONTACT_LINK_VERIFICATION:
            changed.contactLinkVerification = true;
            break;

        case ATTR_RICH_PREVIEWS:
            changed.richPreviews = true;
            break;

        case ATTR_LAST_PSA:
            changed.lastPsa = true;
            break;

        case ATTR_RUBBISH_TIME:
            changed.rubbishTime = true;
            break;

        case ATTR_STORAGE_STATE:
            changed.storageState = true;
            break;

        case ATTR_GEOLOCATION:
            changed.geolocation = true;
            break;

        case ATTR_CAMERA_UPLOADS_FOLDER:
            changed.cameraUploadsFolder = true;
            break;

        case ATTR_MY_CHAT_FILES_FOLDER:
            changed.myChatFilesFolder = true;
            break;

        case ATTR_PUSH_SETTINGS:
            changed.pushSettings = true;
            break;

        case ATTR_ALIAS:
            changed.alias = true;
            break;

        case ATTR_UNSHAREABLE_KEY:
            changed.unshareablekey = true;
            break;

        case ATTR_DEVICE_NAMES:
            changed.devicenames = true;
            break;

        case ATTR_MY_BACKUPS_FOLDER:
            changed.myBackupsFolder = true;
            break;

        case ATTR_COOKIE_SETTINGS:
            changed.cookieSettings = true;
            break;

        case ATTR_JSON_SYNC_CONFIG_DATA:
            changed.jsonSyncConfigData = true;
            break;

        case ATTR_KEYS:
            changed.keys = true;
            changed.authring = true;
            break;

        case ATTR_APPS_PREFS:
            changed.aPrefs = true;
            break;

        case ATTR_CC_PREFS:
            changed.ccPrefs = true;
            break;

        case ATTR_ENABLE_TEST_NOTIFICATIONS:
            changed.enableTestNotifications = true;
            break;

        case ATTR_LAST_READ_NOTIFICATION:
            changed.lastReadNotification = true;
            break;

        case ATTR_LAST_ACTIONED_BANNER:
            changed.lastActionedBanner = true;
            break;

        case ATTR_ENABLE_TEST_SURVEYS:
            changed.enableTestSurveys = true;
            break;

        default:
            return false;
    }

    return true;
}

void User::setTag(int newTag)
{
    if (tag != 0) // external changes prevail
    {
        tag = newTag;
    }
}

int User::getTag()
{
    return tag;
}

void User::resetTag()
{
    tag = -1;
}

// update user attributes
void User::set(visibility_t v, m_time_t ct)
{
    show = v;
    ctime = ct;
}

string User::attributePrefixInTLV(attr_t type, bool modifier)
{
    if (type == ATTR_DEVICE_NAMES && modifier)
    {
        return "ext:";
    }

    return string();
}

AuthRing::AuthRing(attr_t type, const string_map& authring):
    mType(type)
{
    if (auto it = authring.find(""); it != authring.end())
    {
        if (!deserialize(it->second))
        {
            LOG_warn << "Excess data while deserializing Authring (TLV) of type: " << type;
        }
    }
}

AuthRing::AuthRing(attr_t type, const std::string &authValue)
    : mType(type)
{
    if (!deserialize(authValue))
    {
        LOG_warn << "Excess data while deserializing Authring (string) of type: " << type;
    }
}

bool AuthRing::deserialize(const string& authValue)
{
    if (authValue.empty()) return true;

    handle userhandle;
    byte authFingerprint[20];
    signed char authMethod = AUTH_METHOD_UNKNOWN;

    const char *ptr = authValue.data();
    const char *end = ptr + authValue.size();
    unsigned recordSize = 29;   // <handle.8> <fingerprint.20> <authLevel.1>
    while (ptr + recordSize <= end)
    {
        memcpy(&userhandle, ptr, sizeof(userhandle));
        ptr += sizeof(userhandle);

        memcpy(authFingerprint, ptr, sizeof(authFingerprint));
        ptr += sizeof(authFingerprint);

        memcpy(&authMethod, ptr, sizeof(authMethod));
        ptr += sizeof(authMethod);

        mFingerprint[userhandle] = string((const char*) authFingerprint, sizeof(authFingerprint));
        mAuthMethod[userhandle] = static_cast<AuthMethod>(authMethod);
    }

    return ptr == end;

}

std::string* AuthRing::serialize(PrnGen &rng, SymmCipher &key) const
{
    string buf = serializeForJS();
    string_map records{
        {{}, std::move(buf)}
    };
    return tlv::recordsToContainer(std::move(records), rng, key).release();
}

string AuthRing::serializeForJS() const
{
    string buf;

    map<handle, string>::const_iterator itFingerprint;
    map<handle, AuthMethod>::const_iterator itAuthMethod;
    for (itFingerprint = mFingerprint.begin(), itAuthMethod = mAuthMethod.begin();
         itFingerprint != mFingerprint.end() && itAuthMethod != mAuthMethod.end();
         itFingerprint++, itAuthMethod++)
    {
        buf.append((const char *)&itFingerprint->first, sizeof(handle));
        buf.append(itFingerprint->second);
        buf.append((const char *)&itAuthMethod->second, 1);
    }

    return buf;
}

bool AuthRing::isTracked(handle uh) const
{
    return mAuthMethod.find(uh) != mAuthMethod.end();
}

AuthMethod AuthRing::getAuthMethod(handle uh) const
{
    AuthMethod authMethod = AUTH_METHOD_UNKNOWN;
    auto it = mAuthMethod.find(uh);
    if (it != mAuthMethod.end())
    {
        authMethod = it->second;
    }
    return authMethod;
}

std::string AuthRing::getFingerprint(handle uh) const
{
    string fingerprint;
    auto it = mFingerprint.find(uh);
    if (it != mFingerprint.end())
    {
        fingerprint = it->second;
    }
    return fingerprint;
}

vector<handle> AuthRing::getTrackedUsers() const
{
    vector<handle> users;
    for (auto &it : mFingerprint)
    {
        users.push_back(it.first);
    }
    return users;
}

void AuthRing::add(handle uh, const std::string &fingerprint, AuthMethod authMethod)
{
    assert(mFingerprint.find(uh) == mFingerprint.end());
    assert(mAuthMethod.find(uh) == mAuthMethod.end());
    mFingerprint[uh] = fingerprint;
    mAuthMethod[uh] = authMethod;
    mNeedsUpdate = true;
}

void AuthRing::update(handle uh, AuthMethod authMethod)
{
    mAuthMethod.at(uh) = authMethod;
    mNeedsUpdate = true;
}

attr_t AuthRing::keyTypeToAuthringType(attr_t at)
{
    if (at == ATTR_ED25519_PUBK)
    {
        return ATTR_AUTHRING;
    }
    else if (at == ATTR_CU25519_PUBK)
    {
        return ATTR_AUTHCU255;
    }

    assert(false);
    return ATTR_UNKNOWN;
}

attr_t AuthRing::signatureTypeToAuthringType(attr_t at)
{
    if (at == ATTR_SIG_CU255_PUBK)
    {
        return ATTR_AUTHCU255;
    }

    assert(false);
    return ATTR_UNKNOWN;
}

attr_t AuthRing::authringTypeToSignatureType(attr_t at)
{
    if (at == ATTR_AUTHCU255)
    {
        return ATTR_SIG_CU255_PUBK;
    }

    assert(false);
    return ATTR_UNKNOWN;
}

std::string AuthRing::authMethodToStr(AuthMethod authMethod)
{
    if (authMethod == AUTH_METHOD_SEEN)
    {
        return "seen";
    }
    else if (authMethod == AUTH_METHOD_FINGERPRINT)
    {
        return "fingerprint comparison";
    }
    else if (authMethod == AUTH_METHOD_SIGNATURE)
    {
        return "signature verified";
    }

    return "unknown";
}

string AuthRing::toString(const AuthRing &authRing)
{
    auto uhVector = authRing.getTrackedUsers();
    ostringstream result;
    for (auto& i : uhVector)
    {
        result << "\t[" << toHandle(i) << "] " << Base64::btoa(authRing.getFingerprint(i)) << " | " <<AuthRing::authMethodToStr(authRing.getAuthMethod(i)) << std::endl;
    }
    return result.str();
}

std::string AuthRing::fingerprint(const std::string &pubKey, bool hexadecimal)
{
    HashSHA256 hash;
    hash.add((const byte *)pubKey.data(), static_cast<unsigned>(pubKey.size()));

    string result;
    hash.get(&result);
    result.erase(20);   // keep only the most significant 160 bits

    if (hexadecimal)
    {
        return Utils::stringToHex(result);
    }

    return result;
}

bool AuthRing::isSignedKey() const
{
    return mType != ATTR_AUTHRING;
}

bool AuthRing::areCredentialsVerified(handle uh) const
{
    if (isSignedKey())
    {
        return getAuthMethod(uh) == AUTH_METHOD_SIGNATURE;
    }
    else
    {
        return getAuthMethod(uh) == AUTH_METHOD_FINGERPRINT;
    }
}

} // namespace
