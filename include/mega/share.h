/**
 * @file mega/share.h
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

#ifndef MEGA_SHARE_H
#define MEGA_SHARE_H 1

#include "types.h"
#include "user.h"
#include "megaclient.h"

namespace mega {

struct NewShare;

// share credentials
struct MEGA_API Share
{
    accesslevel_t access;
    User* user;
    m_time_t ts;

    PendingContactRequest* pcr;

    void update(accesslevel_t, m_time_t, PendingContactRequest* = NULL);

    void serialize(string*);
    static NewShare* unserialize(int, handle, const byte *, const char**, const char*);

    Share(User*, accesslevel_t, m_time_t, PendingContactRequest* = NULL);
};

// new share credentials (will be merged into node as soon as it appears)
struct MEGA_API NewShare
{
    handle h;
    int outgoing;
    handle peer;
    accesslevel_t access;
    m_time_t ts;

    handle pending;
    bool upgrade_pending_to_full;
    bool have_key, have_auth, remove_key;

    byte key[SymmCipher::BLOCKSIZE];
    byte auth[SymmCipher::BLOCKSIZE];

    NewShare(handle, int, handle, accesslevel_t, m_time_t, const byte*, const byte* = NULL, handle = UNDEF, bool = false, bool = false);
};

} // namespace

#endif
