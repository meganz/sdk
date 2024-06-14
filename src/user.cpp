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
#include "mega/megaclient.h"
#include "mega/logging.h"
#include "mega/base64.h"

namespace mega {

constexpr char User::NO_VERSION[];
constexpr char User::NON_EXISTING[];

User::User(const char* cemail)
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

bool User::mergeUserAttribute(attr_t type, const string_map &newValuesMap, TLVstore &tlv)
{
    bool modified = false;

    for (const auto &it : newValuesMap)
    {
        const char *key = it.first.c_str();
        string newValue = it.second;
        string currentValue;
        string buffer;
        if (tlv.get(key, buffer) && !buffer.empty())  // the key may not exist in the current user attribute
        {
            Base64::btoa(buffer, currentValue);
        }
        if (newValue != currentValue)
        {
            if ((type == ATTR_ALIAS
                 || type == ATTR_DEVICE_NAMES
                 || type == ATTR_CC_PREFS
                 || type == ATTR_APPS_PREFS) && newValue[0] == '\0')
            {
                // alias/deviceName/appPrefs being removed
                tlv.reset(key);
            }
            else
            {
                tlv.set(key, Base64::atob(newValue));
            }
            modified = true;
        }
    }

    return modified;
}

bool User::serialize(string* d) const
{
    unsigned char l;
    unsigned short ll;
    time_t ts;
    AttrMap attrmap;
    char attrVersion = '2';
    // Version 1: attributes are serialized along with its version
    // Version 2: size of attributes use 4B (uint32_t) instead of 2B (unsigned short)

    d->reserve(d->size() + 100 + attrmap.storagesize(10));

    d->append((char*)&userhandle, sizeof userhandle);

    // FIXME: use m_time_t & Serialize64 instead
    ts = ctime;
    d->append((char*)&ts, sizeof ts);
    d->append((char*)&show, sizeof show);

    l = (unsigned char)email.size();
    d->append((char*)&l, sizeof l);
    d->append(email.c_str(), l);

    d->append((char*)&attrVersion, 1);

    char bizMode = 0;
    if (mBizMode != BIZ_MODE_UNKNOWN) // convert number to ascii
    {
        bizMode = static_cast<char>('0' + mBizMode);
    }

    d->append((char*)&bizMode, 1);
    d->append("\0\0\0\0\0", 6);

    // serialization of attributes
    l = (unsigned char)attrs.size();
    d->append((char*)&l, sizeof l);
    for (userattr_map::const_iterator it = attrs.begin(); it != attrs.end(); it++)
    {
        d->append((char*)&it->first, sizeof it->first);

        uint32_t valueSize = static_cast<uint32_t>(it->second.size());
        d->append((char*)&valueSize, sizeof valueSize);
        d->append(it->second.data(), valueSize);

        auto itattrsv = attrsv.find(it->first);
        if (itattrsv != attrsv.end())
        {
            ll = (unsigned short)itattrsv->second.size();
            d->append((char*)&ll, sizeof ll);
            d->append(itattrsv->second.data(), ll);
        }
        else
        {
            ll = 0;
            d->append((char*)&ll, sizeof ll);
        }
    }

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
    unsigned short ll;
    string m;
    User* u;
    const char* ptr = d->data();
    const char* end = ptr + d->size();
    int i;
    char attrVersion;

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

    l = *ptr++;
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

    attrVersion = MemAccess::get<char>(ptr);
    ptr += sizeof(attrVersion);

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

    if ((i >= 0) || !(u = client->finduser(uh, 1)))
    {
        return NULL;
    }

    client->mapuser(uh, m.c_str());
    u->set(v, ts);
    u->resetTag();
    u->mBizMode = bizMode;

    if (attrVersion == '\0')
    {
        AttrMap attrmap;
        if ((ptr < end) && !(ptr = attrmap.unserialize(ptr, end)))
        {
            client->discarduser(uh);
            return NULL;
        }
    }
    else if (attrVersion == '1' || attrVersion == '2')
    {
        attr_t key;

        // attrVersion = 1 -> size of value uses 2 bytes
        // attrVersion = 2 -> size of value uses 4 bytes
        uint32_t valueSize = 0;
        size_t sizeLength = (attrVersion == '1') ? sizeof ll : sizeof valueSize;

        if (ptr + sizeof(char) > end)
        {
            client->discarduser(uh);
            return NULL;
        }

        l = *ptr++;
        for (int i = 0; i < l; i++)
        {
            if (ptr + sizeof key + sizeLength > end)
            {
                client->discarduser(uh);
                return NULL;
            }

            key = MemAccess::get<attr_t>(ptr);
            ptr += sizeof key;

            if (attrVersion == '1')
            {
                valueSize = MemAccess::get<short>(ptr);
            }
            else // attrVersion == '2'
            {
                valueSize = MemAccess::get<uint32_t>(ptr);
            }
            ptr += sizeLength;

            if (ptr + valueSize + sizeof ll > end)
            {
                client->discarduser(uh);
                return NULL;
            }

            // check it's not loaded by `ug` for own user, and that the
            // attribute still exists (has not been removed)
            if (!u->isattrvalid(key) && !u->nonExistingAttribute(key))
            {
                u->attrs[key].assign(ptr, valueSize);
            }

            ptr += valueSize;

            ll = MemAccess::get<short>(ptr);
            ptr += sizeof ll;

            if (ll)
            {
                if (ptr + ll > end)
                {
                    client->discarduser(uh);
                    return NULL;
                }

                // check it's not loaded by `ug` for own user, and that the
                // attribute still exists (has not been removed)
                if (!u->isattrvalid(key) && !u->nonExistingAttribute(key))
                {
                    u->attrsv[key].assign(ptr,ll);
                }

                ptr += ll;
            }
        }
    }

    // initialize private Ed25519 and Cu25519 from cache
    if (u->userhandle == client->me)
    {
        string prEd255, prCu255;

        const string* keys = u->getattr(ATTR_KEYS);
        if (keys)
        {
            client->mKeyManager.setKey(client->key);
            if (client->mKeyManager.fromKeysContainer(*keys))
            {
                prEd255 = client->mKeyManager.privEd25519();
                prCu255 = client->mKeyManager.privCu25519();
            }
        }

        if (!client->mKeyManager.generation())
        {
            const string *av = (u->isattrvalid(ATTR_KEYRING)) ? u->getattr(ATTR_KEYRING) : NULL;
            if (av)
            {
                unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(av, &client->key));
                if (tlvRecords)
                {
                    tlvRecords->get(EdDSA::TLV_KEY, prEd255);
                    tlvRecords->get(ECDH::TLV_KEY, prCu255);
                }
                else
                {
                    LOG_warn << "Failed to decrypt keyring from cache";
                }
            }
        }

        if (prEd255.size())
        {
            client->signkey = new EdDSA(client->rng, (unsigned char *) prEd255.data());
            if (!client->signkey->initializationOK)
            {
                delete client->signkey;
                client->signkey = NULL;
                LOG_warn << "Failed to load chat key from local cache.";
            }
            else
            {
                LOG_info << "Signing key loaded from local cache.";
            }
        }
        if (prCu255.size())
        {
            client->chatkey = new ECDH(prCu255);
            if (!client->chatkey->initializationOK)
            {
                delete client->chatkey;
                client->chatkey = NULL;
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

void User::setattr(attr_t at, string *av, string *v)
{
    setChanged(at);

    if (at != ATTR_AVATAR)  // avatar is saved to disc
    {
        attrs[at] = *av;
    }

    attrsv[at] = v ? *v : NO_VERSION;
}

void User::invalidateattr(attr_t at)
{
    setChanged(at);
    attrsv.erase(at);
}

void User::removeattr(attr_t at, bool ownUser)
{
    if (isattrvalid(at))
    {
        setChanged(at);
    }

    attrs.erase(at);
    if (ownUser)
        attrsv[at] = NON_EXISTING; // it allows to avoid fetch from servers
    else
        attrsv.erase(at);
}

void User::removeattr(attr_t at, const string& version)
{
    if (isattrvalid(at))
    {
        setChanged(at);
    }

    attrs.erase(at);
    attrsv[at] = version;
}

// updates the user attribute value+version only if different
int User::updateattr(attr_t at, std::string *av, std::string *v)
{
    if (attrsv[at] == *v)
    {
        return 0;
    }

    setattr(at, av, v);
    return 1;
}

bool User::nonExistingAttribute(attr_t at) const
{
    auto it = attrsv.find(at);
    if (it != attrsv.end() && it->second == NON_EXISTING)
    {
        assert(attrs.find(at) == attrs.end());
        return true;
    }

    return false;
}

// returns the value if there is value (even if it's invalid by now)
const string * User::getattr(attr_t at)
{
    userattr_map::const_iterator it = attrs.find(at);
    if (it != attrs.end())
    {
        return &(it->second);
    }

    return NULL;
}

bool User::isattrvalid(attr_t at)
{
    return attrs.count(at) && attrsv.count(at) && attrsv.find(at)->second != NON_EXISTING;
}

string User::attr2string(attr_t type)
{
    string attrname;

    // Special first character (required, except for the oldest attributes):
    // `+` is public and unencrypted
    // `#` is 'protected' and unencrypted, the API will allow contacts to fetch it but not give it out to non-contacts
    // `^` is private but unencrypted, i.e.the API won't give it out to anybody except you, but the API can read the value as well
    // `*` is private and encrypted, API only gives it to you and the API doesn't have a way to know the true value
    // `%` business usage

    // Special second character (optional)
    // ! only store a single copy and do not keep a history of changes
    // ~ only store one time (ignore subsequent updates, and no history of course)

    switch(type)
    {
        case ATTR_AVATAR:
            attrname = "+a";
            break;

        case ATTR_FIRSTNAME:
            attrname = "firstname";
            break;

        case ATTR_LASTNAME:
            attrname = "lastname";
            break;

        case ATTR_AUTHRING:
            attrname = "*!authring";
            break;

        case ATTR_AUTHCU255:
            attrname = "*!authCu255";
            break;

        case ATTR_LAST_INT:
            attrname = "*!lstint";
            break;

        case ATTR_ED25519_PUBK:
            attrname = "+puEd255";
            break;

        case ATTR_CU25519_PUBK:
            attrname = "+puCu255";
            break;

        case ATTR_SIG_RSA_PUBK:
            attrname = "+sigPubk";
            break;

        case ATTR_SIG_CU255_PUBK:
            attrname = "+sigCu255";
            break;

        case ATTR_KEYRING:
            attrname = "*keyring";
            break;

        case ATTR_COUNTRY:
            attrname = "country";
            break;

        case ATTR_BIRTHDAY:
            attrname = "birthday";
            break;

        case ATTR_BIRTHMONTH:
            attrname = "birthmonth";
            break;

        case ATTR_BIRTHYEAR:
            attrname = "birthyear";
            break;

        case ATTR_LANGUAGE:
            attrname = "^!lang";
            break;

        case ATTR_PWD_REMINDER:
            attrname = "^!prd";
            break;

        case ATTR_DISABLE_VERSIONS:
            attrname = "^!dv";
            break;

        case ATTR_NO_CALLKIT:
            attrname = "^!nokit";
            break;

        case ATTR_CONTACT_LINK_VERIFICATION:
            attrname = "^clv";
            break;

        case ATTR_RICH_PREVIEWS:
            attrname = "*!rp";
            break;

        case ATTR_LAST_PSA:
            attrname = "^!lastPsa";
            break;

        case ATTR_RUBBISH_TIME:
            attrname = "^!rubbishtime";
            break;

        case ATTR_STORAGE_STATE:
            attrname = "^!usl";
            break;

        case ATTR_GEOLOCATION:
            attrname = "*!geo";
            break;

        case ATTR_CAMERA_UPLOADS_FOLDER:
            attrname = "*!cam";
            break;

        case ATTR_MY_CHAT_FILES_FOLDER:
            attrname = "*!cf";
            break;

        case ATTR_PUSH_SETTINGS:
            attrname = "^!ps";
            break;

        case ATTR_UNSHAREABLE_KEY:
            attrname = "*~usk";  // unshareable key (for encrypting attributes that should not be shared)
            break;

        case ATTR_ALIAS:
            attrname =  "*!>alias";
            break;

        case ATTR_DEVICE_NAMES:
            attrname =  "*!dn";
            break;

        case ATTR_MY_BACKUPS_FOLDER:
            attrname = "^!bak";
            break;

        case ATTR_COOKIE_SETTINGS:
            attrname = "^!csp";
            break;

        case ATTR_JSON_SYNC_CONFIG_DATA:
            attrname = "*~jscd";
            break;

        case ATTR_KEYS:
            attrname =  "^!keys";
            break;

        case ATTR_APPS_PREFS:
            attrname =  "*!aPrefs";
            break;

        case ATTR_CC_PREFS:
            attrname = "*!ccPref";
            break;

        case ATTR_VISIBLE_WELCOME_DIALOG:
            attrname = "^!weldlg";
            break;

        case ATTR_VISIBLE_TERMS_OF_SERVICE:
            attrname = "^!tos";
            break;

        case ATTR_PWM_BASE:
            attrname = "pwmh";
            break;

        case ATTR_ENABLE_TEST_NOTIFICATIONS:
            attrname = "^!tnotif";
            break;

        case ATTR_LAST_READ_NOTIFICATION:
            attrname = "^!lnotif";
            break;

        case ATTR_LAST_ACTIONED_BANNER:
            attrname = "^!lbannr";
            break;

        case ATTR_UNKNOWN:  // empty string
            break;
    }

    return attrname;
}

string User::attr2longname(attr_t type)
{
    string longname;

    switch(type)
    {
    case ATTR_AVATAR:
        longname = "AVATAR";
        break;

    case ATTR_FIRSTNAME:
        longname = "FIRSTNAME";
        break;

    case ATTR_LASTNAME:
        longname = "LASTNAME";
        break;

    case ATTR_AUTHRING:
        longname = "AUTHRING";
        break;

    case ATTR_AUTHCU255:
        longname = "AUTHCU255";
        break;

    case ATTR_LAST_INT:
        longname = "LAST_INT";
        break;

    case ATTR_ED25519_PUBK:
        longname = "ED25519_PUBK";
        break;

    case ATTR_CU25519_PUBK:
        longname = "CU25519_PUBK";
        break;

    case ATTR_SIG_RSA_PUBK:
        longname = "SIG_RSA_PUBK";
        break;

    case ATTR_SIG_CU255_PUBK:
        longname = "SIG_CU255_PUBK";
        break;

    case ATTR_KEYRING:
        longname = "KEYRING";
        break;

    case ATTR_COUNTRY:
        longname = "COUNTRY";
        break;

    case ATTR_BIRTHDAY:
        longname = "BIRTHDAY";
        break;

    case ATTR_BIRTHMONTH:
        longname = "BIRTHMONTH";
        break;

    case ATTR_BIRTHYEAR:
        longname = "BIRTHYEAR";
        break;

    case ATTR_LANGUAGE:
        longname = "LANGUAGE";
        break;

    case ATTR_PWD_REMINDER:
        longname = "PWD_REMINDER";
        break;

    case ATTR_DISABLE_VERSIONS:
        longname = "DISABLE_VERSIONS";
        break;

    case ATTR_NO_CALLKIT:
        longname = "NO_CALLKIT";
        break;

    case ATTR_CONTACT_LINK_VERIFICATION:
        longname = "CONTACT_LINK_VERIFICATION";
        break;

    case ATTR_RICH_PREVIEWS:
        longname = "RICH_PREVIEWS";
        break;

    case ATTR_LAST_PSA:
        longname = "LAST_PSA";
        break;

    case ATTR_RUBBISH_TIME:
        longname = "RUBBISH_TIME";
        break;

    case ATTR_STORAGE_STATE:
        longname = "STORAGE_STATE";
        break;

    case ATTR_GEOLOCATION:
        longname = "GEOLOCATION";
        break;

    case ATTR_UNSHAREABLE_KEY:
        longname = "UNSHAREABLE_KEY";
        break;

    case ATTR_CAMERA_UPLOADS_FOLDER:
        longname = "CAMERA_UPLOADS_FOLDER";
        break;

    case ATTR_MY_CHAT_FILES_FOLDER:
        longname = "MY_CHAT_FILES_FOLDER";
        break;

    case ATTR_UNKNOWN:
        longname = "";  // empty string
        break;

    case ATTR_PUSH_SETTINGS:
        longname = "PUSH_SETTINGS";
        break;

    case ATTR_ALIAS:
        longname = "ALIAS";
        break;

    case ATTR_DEVICE_NAMES:
        longname = "DEVICE_NAMES";
        break;

    case ATTR_MY_BACKUPS_FOLDER:
        longname = "ATTR_MY_BACKUPS_FOLDER";
        break;

    case ATTR_COOKIE_SETTINGS:
        longname = "ATTR_COOKIE_SETTINGS";
        break;

    case ATTR_JSON_SYNC_CONFIG_DATA:
        longname = "JSON_SYNC_CONFIG_DATA";
        break;

    case ATTR_KEYS:
        longname = "KEYS";
        break;

    case ATTR_APPS_PREFS:
        longname = "APPS_PREFS";
        break;
    case ATTR_CC_PREFS:
        longname = "CC_PREFS";
        break;

    case ATTR_VISIBLE_WELCOME_DIALOG:
        longname = "VISIBLE_WELCOME_DIALOG";
        break;

    case ATTR_VISIBLE_TERMS_OF_SERVICE:
        longname = "VISIBLE_TERMS_OF_SERVICE";
        break;

    case ATTR_PWM_BASE:
        longname = "PWM_BASE";
        break;

    case ATTR_ENABLE_TEST_NOTIFICATIONS:
        longname = "ENABLE_TEST_NOTIFICATIONS";
        break;

    case ATTR_LAST_READ_NOTIFICATION:
        longname = "LAST_READ_NOTIFICATION";
        break;

    case ATTR_LAST_ACTIONED_BANNER:
        longname = "LAST_ACTIONED_BANNER";
        break;
    }

    return longname;
}


attr_t User::string2attr(const char* name)
{
    if (!strcmp(name, "*keyring"))
    {
        return ATTR_KEYRING;
    }
    else if (!strcmp(name, "*!authring"))
    {
        return ATTR_AUTHRING;
    }
    else if (!strcmp(name, "*!authCu255"))
    {
        return ATTR_AUTHCU255;
    }
    else if (!strcmp(name, "*!lstint"))
    {
        return ATTR_LAST_INT;
    }
    else if (!strcmp(name, "+puCu255"))
    {
        return ATTR_CU25519_PUBK;
    }
    else if (!strcmp(name, "+puEd255"))
    {
        return ATTR_ED25519_PUBK;
    }
    else if (!strcmp(name, "+sigPubk"))
    {
        return ATTR_SIG_RSA_PUBK;
    }
    else if (!strcmp(name, "+sigCu255"))
    {
        return ATTR_SIG_CU255_PUBK;
    }
    else if (!strcmp(name, "+a"))
    {
        return ATTR_AVATAR;
    }
    else if (!strcmp(name, "firstname"))
    {
        return ATTR_FIRSTNAME;
    }
    else if (!strcmp(name, "lastname"))
    {
        return ATTR_LASTNAME;
    }
    else if (!strcmp(name, "country"))
    {
        return ATTR_COUNTRY;
    }
    else if (!strcmp(name, "birthday"))
    {
        return ATTR_BIRTHDAY;
    }
    else if(!strcmp(name, "birthmonth"))
    {
        return ATTR_BIRTHMONTH;
    }
    else if(!strcmp(name, "birthyear"))
    {
        return ATTR_BIRTHYEAR;
    }
    else if(!strcmp(name, "^!lang"))
    {
        return ATTR_LANGUAGE;
    }
    else if(!strcmp(name, "^!prd"))
    {
        return ATTR_PWD_REMINDER;
    }
    else if(!strcmp(name, "^!dv"))
    {
        return ATTR_DISABLE_VERSIONS;
    }
    else if(!strcmp(name, "^!nokit"))
    {
        return ATTR_NO_CALLKIT;
    }
    else if(!strcmp(name, "^clv"))
    {
        return ATTR_CONTACT_LINK_VERIFICATION;
    }
    else if(!strcmp(name, "*!rp"))
    {
        return ATTR_RICH_PREVIEWS;
    }
    else if(!strcmp(name, "^!lastPsa"))
    {
        return ATTR_LAST_PSA;
    }
    else if(!strcmp(name, "^!rubbishtime"))
    {
        return ATTR_RUBBISH_TIME;
    }
    else if(!strcmp(name, "^!usl"))
    {
        return ATTR_STORAGE_STATE;
    }
    else if(!strcmp(name, "*!geo"))
    {
        return ATTR_GEOLOCATION;
    }
    else if (!strcmp(name, "*!cam"))
    {
        return ATTR_CAMERA_UPLOADS_FOLDER;
    }
    else if(!strcmp(name, "*!cf"))
    {
        return ATTR_MY_CHAT_FILES_FOLDER;
    }
    else if(!strcmp(name, "^!ps"))
    {
        return ATTR_PUSH_SETTINGS;
    }
    else if (!strcmp(name, "*~usk"))
    {
        return ATTR_UNSHAREABLE_KEY;
    }
    else if (!strcmp(name, "*!>alias"))
    {
        return ATTR_ALIAS;
    }
    else if (!strcmp(name, "*!dn"))
    {
        return ATTR_DEVICE_NAMES;
    }
    else if (!strcmp(name, "^!bak"))
    {
        return ATTR_MY_BACKUPS_FOLDER;
    }
    else if (!strcmp(name, "^!csp"))
    {
        return ATTR_COOKIE_SETTINGS;
    }
    else if (!strcmp(name, "*~jscd"))
    {
        return ATTR_JSON_SYNC_CONFIG_DATA;
    }
    else if (!strcmp(name, "^!keys"))
    {
        return ATTR_KEYS;
    }
    else if (!strcmp(name, "*!aPrefs"))
    {
        return ATTR_APPS_PREFS;
    }
    else if (!strcmp(name, "*!ccPref"))
    {
        return ATTR_CC_PREFS;
    }
    else if(!strcmp(name, "^!weldlg"))
    {
        return ATTR_VISIBLE_WELCOME_DIALOG;
    }
    else if(!strcmp(name, "^!tos"))
    {
        return ATTR_VISIBLE_TERMS_OF_SERVICE;
    }
    else if(!strcmp(name, "^!tnotif"))
    {
        return ATTR_ENABLE_TEST_NOTIFICATIONS;
    }
    else if(!strcmp(name, "^!lnotif"))
    {
        return ATTR_LAST_READ_NOTIFICATION;
    }
    else if(!strcmp(name, "^!lbannr"))
    {
        return ATTR_LAST_ACTIONED_BANNER;
    }
    else
    {
        return ATTR_UNKNOWN;   // attribute not recognized
    }
}

int User::needversioning(attr_t at)
{
    switch(at)
    {
        case ATTR_AVATAR:
        case ATTR_FIRSTNAME:
        case ATTR_LASTNAME:
        case ATTR_COUNTRY:
        case ATTR_BIRTHDAY:
        case ATTR_BIRTHMONTH:
        case ATTR_BIRTHYEAR:
        case ATTR_LANGUAGE:
        case ATTR_PWD_REMINDER:
        case ATTR_DISABLE_VERSIONS:
        case ATTR_NO_CALLKIT:
        case ATTR_RICH_PREVIEWS:
        case ATTR_LAST_PSA:
        case ATTR_RUBBISH_TIME:
        case ATTR_GEOLOCATION:
        case ATTR_MY_CHAT_FILES_FOLDER:
        case ATTR_PUSH_SETTINGS:
        case ATTR_COOKIE_SETTINGS:
            return 0;

        case ATTR_LAST_INT:
        case ATTR_ED25519_PUBK:
        case ATTR_CU25519_PUBK:
        case ATTR_SIG_RSA_PUBK:
        case ATTR_SIG_CU255_PUBK:
        case ATTR_KEYRING:
        case ATTR_AUTHRING:
        case ATTR_AUTHCU255:
        case ATTR_CONTACT_LINK_VERIFICATION:
        case ATTR_ALIAS:
        case ATTR_CAMERA_UPLOADS_FOLDER:
        case ATTR_UNSHAREABLE_KEY:
        case ATTR_DEVICE_NAMES:
        case ATTR_JSON_SYNC_CONFIG_DATA:
        case ATTR_MY_BACKUPS_FOLDER:
        case ATTR_KEYS:
        case ATTR_APPS_PREFS:
        case ATTR_CC_PREFS:
        case ATTR_VISIBLE_WELCOME_DIALOG:
        case ATTR_VISIBLE_TERMS_OF_SERVICE:
        case ATTR_ENABLE_TEST_NOTIFICATIONS:
        case ATTR_LAST_READ_NOTIFICATION:
        case ATTR_LAST_ACTIONED_BANNER:
            return 1;

        case ATTR_STORAGE_STATE: //putua is forbidden for this attribute
            assert(false);
        default:
            return -1;
    }
}

char User::scope(attr_t at)
{
    switch(at)
    {
        case ATTR_KEYRING:
        case ATTR_AUTHRING:
        case ATTR_AUTHCU255:
        case ATTR_LAST_INT:
        case ATTR_RICH_PREVIEWS:
        case ATTR_GEOLOCATION:
        case ATTR_CAMERA_UPLOADS_FOLDER:
        case ATTR_MY_CHAT_FILES_FOLDER:
        case ATTR_UNSHAREABLE_KEY:
        case ATTR_ALIAS:
        case ATTR_DEVICE_NAMES:
        case ATTR_JSON_SYNC_CONFIG_DATA:
        case ATTR_APPS_PREFS:
        case ATTR_CC_PREFS:
            return '*';

        case ATTR_AVATAR:
        case ATTR_ED25519_PUBK:
        case ATTR_CU25519_PUBK:
        case ATTR_SIG_RSA_PUBK:
        case ATTR_SIG_CU255_PUBK:
            return '+';

        case ATTR_LANGUAGE:
        case ATTR_PWD_REMINDER:
        case ATTR_DISABLE_VERSIONS:
        case ATTR_NO_CALLKIT:
        case ATTR_CONTACT_LINK_VERIFICATION:
        case ATTR_LAST_PSA:
        case ATTR_RUBBISH_TIME:
        case ATTR_STORAGE_STATE:
        case ATTR_PUSH_SETTINGS:
        case ATTR_COOKIE_SETTINGS:
        case ATTR_MY_BACKUPS_FOLDER:
        case ATTR_KEYS:
        case ATTR_VISIBLE_WELCOME_DIALOG:
        case ATTR_VISIBLE_TERMS_OF_SERVICE:
        case ATTR_PWM_BASE:
        case ATTR_ENABLE_TEST_NOTIFICATIONS:
        case ATTR_LAST_READ_NOTIFICATION:
        case ATTR_LAST_ACTIONED_BANNER:
            return '^';

        default:
            return '0';
    }
}

bool User::isAuthring(attr_t at)
{
    return (at == ATTR_AUTHRING || at == ATTR_AUTHCU255);
}

size_t User::getMaxAttributeSize(attr_t at)
{
    std::string attributeName = attr2string(at);
    if (attributeName.size() > 2 && (attributeName[1] == '!' || attributeName[1] == '~'))
    {
        return MAX_USER_VAR_SIZE;
    }

    return MAX_USER_ATTRIBUTE_SIZE;
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
            flagMkExported = tmp;
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
            flagDontShowAgain = tmp;
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

const string *User::getattrversion(attr_t at)
{
    userattr_map::iterator it = attrsv.find(at);
    if (it != attrsv.end())
    {
        return &(it->second);
    }

    return NULL;
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

        default:
            return false;
    }

    return true;
}

void User::setTag(int tag)
{
    if (this->tag != 0)    // external changes prevail
    {
        this->tag = tag;
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

AuthRing::AuthRing(attr_t type, const TLVstore &authring)
    : mType(type)
{
    string authType = "";
    string authValue;
    if (authring.get(authType, authValue))
    {
        if (!deserialize(authValue))
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

    TLVstore tlv;
    tlv.set("", buf);

    return tlv.tlvRecordsToContainer(rng, &key);
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
