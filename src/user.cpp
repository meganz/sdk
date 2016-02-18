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

namespace mega {
User::User(const char* cemail)
{
    userhandle = UNDEF;
    show = VISIBILITY_UNKNOWN;
    ctime = 0;
    pubkrequested = 0;

    if (cemail)
    {
        email = cemail;
    }
}

bool User::serialize(string* d)
{
    unsigned char l;
    unsigned short ll;
    time_t ts;
    AttrMap attrs;
    char attrVersion = '1';

    d->reserve(d->size() + 100 + attrs.storagesize(10));

    d->append((char*)&userhandle, sizeof userhandle);
    
    // FIXME: use m_time_t & Serialize64 instead
    ts = ctime;
    d->append((char*)&ts, sizeof ts);
    d->append((char*)&show, sizeof show);

    l = email.size();
    d->append((char*)&l, sizeof l);
    d->append(email.c_str(), l);

    d->append((char*)&attrVersion, 1);
    d->append("\0\0\0\0\0\0", 7);

    // serialization of attributes
    for (string_map::iterator it = optattrs.begin(); it != optattrs.end(); it++)
    {
        l = it->first.size();
        d->append((char*)&l, sizeof l);
        d->append(it->first.data(), l);

        ll = it->second.size();
        d->append((char*)&ll, sizeof ll);
        d->append(it->second.data(), ll);
    }

    d->append("", 1);

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
        m.assign(ptr, l);
    }
    ptr += l;

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

    if (v == ME)
    {
        client->me = uh;
    }

    client->mapuser(uh, m.c_str());
    u->set(v, ts);


    if (attrVersion == '\0')
    {
        AttrMap attrs;
        if ((ptr < end) && !(ptr = attrs.unserialize(ptr)))
        {
            return NULL;
        }
    }
    else if (attrVersion == '1')
    {
        string key;
        while ((l = *ptr++))
        {
            key.assign(ptr, l);
            ptr += l;

            ll = MemAccess::get<short>(ptr);
            ptr += sizeof ll;

            u->optattrs[key].assign(ptr, ll);
            ptr += ll;
        }
    }

    if ((ptr < end) && !u->pubk.setkey(AsymmCipher::PUBKEY, (byte*)ptr, end - ptr))
    {
        return NULL;
    }

    return u;
}

bool User::setChanged(const char *an)
{
    if (!strcmp(an, "*keyring"))
    {
        changed.keyring = true;
    }
    else if (!strcmp(an, "*!authring"))
    {
        changed.authring = true;
    }
    else if (!strcmp(an, "*!lstint"))
    {
        changed.lstint = true;
    }
    else if (!strcmp(an, "+puCu255"))
    {
        changed.puCu255 = true;
    }
    else if (!strcmp(an, "+puEd255"))
    {
        changed.puEd255 = true;
    }
    else if (!strcmp(an, "+a"))
    {
        changed.avatar = true;
    }
    else if (!strcmp(an, "firstname"))
    {
        changed.firstname = true;
    }
    else if (!strcmp(an, "lastname"))
    {
        changed.lastname = true;
    }
    else if (!strcmp(an, "country"))
    {
        changed.country = true;
    }
    else if (!strcmp(an, "birthday")   ||
             !strcmp(an, "birthmonth") ||
             !strcmp(an, "birthyear"))
    {
        changed.birthday = true;
    }
    else
    {
        return false;   // attribute not recognized
    }

    return true;
}

// update user attributes
void User::set(visibility_t v, m_time_t ct)
{
    show = v;
    ctime = ct;
}
} // namespace
