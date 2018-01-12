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

namespace mega {
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

bool User::serialize(string* d)
{
    unsigned char l;
    unsigned short ll;
    time_t ts;
    AttrMap attrmap;
    char attrVersion = '1';

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
    d->append("\0\0\0\0\0\0", 7);

    // serialization of attributes
    l = (unsigned char)attrs.size();
    d->append((char*)&l, sizeof l);
    for (userattr_map::iterator it = attrs.begin(); it != attrs.end(); it++)
    {
        d->append((char*)&it->first, sizeof it->first);

        ll = it->second.size();
        d->append((char*)&ll, sizeof ll);
        d->append(it->second.data(), ll);

        if (attrsv.find(it->first) != attrsv.end())
        {
            ll = attrsv[it->first].size();
            d->append((char*)&ll, sizeof ll);
            d->append(attrsv[it->first].data(), ll);
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

    if (ptr + sizeof(char) > end)
    {
        return NULL;
    }

    attrVersion = MemAccess::get<char>(ptr);
    ptr += sizeof(attrVersion);

    for (i = 7; i--;)
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

    if (attrVersion == '\0')
    {
        AttrMap attrmap;
        if ((ptr < end) && !(ptr = attrmap.unserialize(ptr, end)))
        {
            client->discarduser(uh);
            return NULL;
        }
    }
    else if (attrVersion == '1')
    {
        attr_t key;

        if (ptr + sizeof(char) > end)
        {
            client->discarduser(uh);
            return NULL;
        }

        l = *ptr++;
        for (int i = 0; i < l; i++)
        {
            if (ptr + sizeof key + sizeof(ll) > end)
            {
                client->discarduser(uh);
                return NULL;
            }

            key = MemAccess::get<attr_t>(ptr);
            ptr += sizeof key;

            ll = MemAccess::get<short>(ptr);
            ptr += sizeof ll;

            if (ptr + ll + sizeof(ll) > end)
            {
                client->discarduser(uh);
                return NULL;
            }

            u->attrs[key].assign(ptr, ll);
            ptr += ll;

            ll = MemAccess::get<short>(ptr);
            ptr += sizeof ll;

            if (ll)
            {
                if (ptr + ll > end)
                {
                    client->discarduser(uh);
                    return NULL;
                }
                u->attrsv[key].assign(ptr,ll);
                ptr += ll;
            }
        }
    }

#ifdef ENABLE_CHAT
    const string *av = (u->isattrvalid(ATTR_KEYRING)) ? u->getattr(ATTR_KEYRING) : NULL;
    if (av)
    {
        TLVstore *tlvRecords = TLVstore::containerToTLVrecords(av, &client->key);
        if (tlvRecords)
        {
            if (tlvRecords->find(EdDSA::TLV_KEY))
            {
                client->signkey = new EdDSA((unsigned char *) tlvRecords->get(EdDSA::TLV_KEY).data());
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

            if (tlvRecords->find(ECDH::TLV_KEY))
            {
                client->chatkey = new ECDH((unsigned char *) tlvRecords->get(ECDH::TLV_KEY).data());
                if (!client->chatkey->initializationOK)
                {
                    delete client->chatkey;
                    client->chatkey = NULL;
                    LOG_warn << "Failed to load chat key from local cache.";
                }
                else
                {
                    LOG_info << "Chat key succesfully loaded from local cache.";
                }
            }

            delete tlvRecords;
        }
        else
        {
            LOG_warn << "Failed to decrypt keyring from cache";
        }
    }
#endif

    if ((ptr < end) && !u->pubk.setkey(AsymmCipher::PUBKEY, (byte*)ptr, end - ptr))
    {
        client->discarduser(uh);
        return NULL;
    }

    return u;
}

void User::setattr(attr_t at, string *av, string *v)
{
    setChanged(at);

    if (at != ATTR_AVATAR)  // avatar is saved to disc
    {
        attrs[at] = *av;
    }

    attrsv[at] = v ? *v : "N";
}

void User::invalidateattr(attr_t at)
{
    setChanged(at);
    attrsv.erase(at);
}

void User::removeattr(attr_t at)
{
    setChanged(at);
    attrs.erase(at);
    attrsv.erase(at);
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
    return attrsv.count(at);
}

string User::attr2string(attr_t type)
{
    string attrname;

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

        case ATTR_UNKNOWN:  // empty string
            break;
    }

    return attrname;
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
    else
    {
        return ATTR_UNKNOWN;   // attribute not recognized
    }
}

bool User::needversioning(attr_t at)
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
            return 0;

        case ATTR_AUTHRING:
        case ATTR_LAST_INT:
        case ATTR_ED25519_PUBK:
        case ATTR_CU25519_PUBK:
        case ATTR_SIG_RSA_PUBK:
        case ATTR_SIG_CU255_PUBK:
        case ATTR_KEYRING:
            return 1;

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
        case ATTR_LAST_INT:
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
            return '^';

        default:
            return '0';
    }
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

    bool lastSuccess = (numDetails & 0x01) != 0;
    bool lastSkipped = (numDetails & 0x02) != 0;
    bool mkExported = (numDetails & 0x04) != 0;
    bool dontShowAgain = (numDetails & 0x08) != 0;
    bool lastLogin = (numDetails & 0x10) != 0;

    bool changed = false;

    // Timestamp for last successful validation of password in PRD
    time_t tsLastSuccess;
    size_t len = oldValue.find(":");
    string buf = oldValue.substr(0, len) + "#"; // add character control '#' for conversion
    oldValue = oldValue.substr(len + 1);    // skip ':'
    if (lastSuccess)
    {
        changed = true;
        tsLastSuccess = time(NULL);
    }
    else
    {
        char *pEnd = NULL;
        tsLastSuccess = strtol(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastSuccess == LONG_MAX || tsLastSuccess == LONG_MIN)
        {
            tsLastSuccess = 0;
            changed = true;
        }
    }

    // Timestamp for last time the PRD was skipped
    time_t tsLastSkipped;
    len = oldValue.find(":");
    buf = oldValue.substr(0, len) + "#";
    oldValue = oldValue.substr(len + 1);
    if (lastSkipped)
    {
        tsLastSkipped = time(NULL);
        changed = true;
    }
    else
    {
        char *pEnd = NULL;
        tsLastSkipped = strtol(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastSkipped == LONG_MAX || tsLastSkipped == LONG_MIN)
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
        int tmp = strtol(buf.data(), &pEnd, 10);
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
        int tmp = strtol(buf.data(), &pEnd, 10);
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
    time_t tsLastLogin = 0;
    len = oldValue.length();
    if (lastLogin)
    {
        tsLastLogin = time(NULL);
        changed = true;
    }
    else
    {
        buf = oldValue.substr(0, len) + "#";

        char *pEnd = NULL;
        tsLastLogin = strtol(buf.data(), &pEnd, 10);
        if (*pEnd != '#' || tsLastLogin == LONG_MAX || tsLastLogin == LONG_MIN)
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
} // namespace
