/**
 * @file share.cpp
 * @brief Classes for manipulating share credentials
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

#include "mega/share.h"

namespace mega {
Share::Share(User* u, accesslevel_t a, m_time_t t, PendingContactRequest* pending)
{
    user = u;
    access = a;
    ts = t;
    this->pcr = pending;
}

void Share::serialize(string* d)
{
    handle uh = user ? user->userhandle : 0;
    char a = (char)access;
    char version = 1;
    handle ph = pcr!=NULL ? pcr->id : UNDEF;

    d->append((char*)&uh, sizeof uh);
    d->append((char*)&ts, sizeof ts);
    d->append((char*)&a, 1);
    d->append((char*)&version, 1);
    d->append((char*)&ph, sizeof ph);
}

NewShare* Share::unserialize(int direction, handle h,
                        const byte* key, const char** ptr, const char* end)
{
    if (*ptr + sizeof(handle) + sizeof(m_time_t) + 2 > end)
    {
        return nullptr;
    }

    char version_flag =  (*ptr)[sizeof(handle) + sizeof(m_time_t) + 1];
    handle ph = UNDEF;
    if (version_flag >= 1)
    {
        // Pending flag exists
        ph = MemAccess::get<handle>(*ptr + sizeof(handle) + sizeof(m_time_t) + 2);
    }
    auto newShare = new NewShare(h, direction, MemAccess::get<handle>(*ptr),
                                 (accesslevel_t)(*ptr)[sizeof(handle) + sizeof(m_time_t)],
                                 MemAccess::get<m_time_t>(*ptr + sizeof(handle)), key, NULL, ph);

    *ptr += sizeof(handle) + sizeof(m_time_t) + 2;
    if (version_flag >= 1)
    {
        *ptr += sizeof(handle);
    }

    return newShare;
}

void Share::update(accesslevel_t a, m_time_t t, PendingContactRequest* pending)
{
    access = a;
    ts = t;
    pcr = pending;
}

// coutgoing: < 0 - don't authenticate, > 0 - authenticate using handle auth
NewShare::NewShare(handle ch, int coutgoing, handle cpeer, accesslevel_t caccess,
                   m_time_t cts, const byte* ckey, const byte* cauth, handle cpending,bool cupgrade_pending_to_full, bool okremoved)
{
    h = ch;
    outgoing = coutgoing;
    peer = cpeer;
    access = caccess;
    ts = cts;
    pending = cpending;
    upgrade_pending_to_full = cupgrade_pending_to_full;
    remove_key = okremoved;

    if (ckey && !SymmCipher::isZeroKey(ckey, SymmCipher::BLOCKSIZE))
    {
        memcpy(key, ckey, sizeof key);
        have_key = 1;
    }
    else
    {
        memset(key, 0, sizeof key);
        have_key = 0;
    }

    if ((outgoing > 0) && cauth)
    {
        memcpy(auth, cauth, sizeof auth);
        have_auth = 1;
    }
    else
    {
        memset(auth, 0, sizeof auth);
        have_auth = 0;
    }
}
} // namespace
