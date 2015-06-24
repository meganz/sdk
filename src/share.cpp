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
#include "logging.h"

namespace mega {
Share::Share(User* u, accesslevel_t a, m_time_t t)
{
    user = u;
    access = a;
    ts = t;
}

void Share::serialize(string* d)
{
    handle uh = user ? user->userhandle : 0;
    char a = (char)access;

    d->append((char*)&uh, sizeof uh);
    d->append((char*)&ts, sizeof ts);
    d->append((char*)&a, 1);
    d->append("", 1);
}

bool Share::unserialize(MegaClient* client, int direction, handle h,
                        const byte* ckey, const char** ptr, const char* end, shared_ptr<Node> n)
{
    if (*ptr + sizeof(handle) + sizeof(m_time_t) + 2 > end)
    {
        return 0;
    }

//    client->newshares.push_back(new NewShare(h, direction, MemAccess::get<handle>(*ptr),
//                                             (accesslevel_t)(*ptr)[sizeof(handle) + sizeof(m_time_t)],
//                                             MemAccess::get<m_time_t>(*ptr + sizeof(handle)), key));

    ////////////////

    int outgoing = direction;
    handle peer = MemAccess::get<handle>(*ptr);
    accesslevel_t access = (accesslevel_t)(*ptr)[sizeof(handle) + sizeof(m_time_t)];
    m_time_t ts = MemAccess::get<m_time_t>(*ptr + sizeof(handle));
    byte key[SymmCipher::BLOCKSIZE];
    if(ckey)
        memcpy(key, ckey, sizeof key);
    bool have_key = ckey ? 1 : 0;
    byte auth[SymmCipher::BLOCKSIZE];
    const byte* cauth = NULL;
    if ((outgoing > 0) && cauth)
        memcpy(auth, cauth, sizeof auth);
    bool have_auth = cauth ? 1 : 0;
    bool notify = false;

    if (!n->sharekey && have_key)
    {
        // setting an outbound sharekey requires node authentication
        // unless coming from a trusted source (the local cache)
        bool auth = true;

        if (outgoing > 0)
        {
            if (!client->checkaccess(n, OWNERPRELOGIN))
            {
                LOG_warn << "Attempt to create dislocated outbound share foiled";
                auth = false;
            }
            else
            {
                byte buf[SymmCipher::KEYLENGTH];

                client->handleauth(h, buf);

                if (memcmp(buf, cauth, sizeof buf))
                {
                    LOG_warn << "Attempt to create forged outbound share foiled";
                    auth = false;
                }
            }
        }

        if (auth)
        {
            n->sharekey = new SymmCipher(key);
        }
    }

    if (access == ACCESS_UNKNOWN && !have_key)
    {
        // share was deleted
        if (outgoing)
        {
            if (n->outshares)
            {
                // outgoing share to user u deleted
                if (n->outshares->erase(peer) && notify)
                {
                    n->changed.outshares = true;
                    client->notifynode(n);
                }

                // if no other outgoing shares remain on this node, erase sharekey
                if (!n->outshares->size())
                {
                    delete n->outshares;
                    n->outshares = NULL;

                    delete n->sharekey;
                    n->sharekey = NULL;
                }
            }
        }
        else
        {
            // incoming share deleted - remove tree
            if (n->parenthandle != UNDEF)
            {
                TreeProcDel td;
                client->proctree(n, &td, true);
            }
            else
            {
                if (n->inshare)
                {
                    n->inshare->user->sharing.erase(n->nodehandle);
                    client->notifyuser(n->inshare->user);
                    n->inshare = NULL;
                }
            }
        }
    }
    else
    {
        if (!ISUNDEF(peer))
        {
            if (outgoing)
            {
                // perform mandatory verification of outgoing shares:
                // only on own nodes and signed unless read from cache
                if (client->checkaccess(n, OWNERPRELOGIN))
                {
                    if (!n->outshares)
                    {
                        n->outshares = new share_map;
                    }

                    Share** sharep = &((*n->outshares)[peer]);

                    // modification of existing share or new share
                    if (*sharep)
                    {
                        (*sharep)->update(access, ts);
                    }
                    else
                    {
                        *sharep = new Share(client->finduser(peer, 1), access, ts);
                    }

                    if (notify)
                    {
                        n->changed.outshares = true;
                        client->notifynode(n);
                    }
                }
            }
            else    // inshare
            {
                if (peer)
                {
                    if (!client->checkaccess(n, OWNERPRELOGIN))
                    {
                        // modification of existing share or new share
                        if (n->inshare)
                        {
                            n->inshare->update(access, ts);
                        }
                        else
                        {
                            n->inshare = new Share(client->finduser(peer, 1), access, ts);
                            n->inshare->user->sharing.insert(n->nodehandle);
                        }

                        if (notify)
                        {
                            n->changed.inshare = true;
                            client->notifynode(n);
                        }
                    }
                    else
                    {
                        LOG_warn << "Invalid inbound share location";
                    }
                }
                else
                {
                    LOG_warn << "Invalid null peer on inbound share";
                }
            }
        }
    }

    ////////////////

    *ptr += sizeof(handle) + sizeof(m_time_t) + 2;

    return true;
}

void Share::update(accesslevel_t a, m_time_t t)
{
    access = a;
    ts = t;
}

// coutgoing: < 0 - don't authenticate, > 0 - authenticate using handle auth
NewShare::NewShare(handle ch, int coutgoing, handle cpeer, accesslevel_t caccess,
                   m_time_t cts, const byte* ckey, const byte* cauth)
{
    h = ch;
    outgoing = coutgoing;
    peer = cpeer;
    access = caccess;
    ts = cts;

    if (ckey)
    {
        memcpy(key, ckey, sizeof key);
        have_key = 1;
    }
    else
    {
        have_key = 0;
    }

    if ((outgoing > 0) && cauth)
    {
        memcpy(auth, cauth, sizeof auth);
        have_auth = 1;
    }
    else
    {
        have_auth = 0;
    }
}
} // namespace
