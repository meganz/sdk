/**
 * @file fileattributefetch.cpp
 * @brief Classes for file attributes fetching
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

#include "mega/fileattributefetch.h"
#include "mega/megaclient.h"
#include "mega/megaapp.h"
#include "mega/logging.h"

namespace mega {
FileAttributeFetchChannel::FileAttributeFetchChannel()
{
    req.binary = true;
    req.status = REQ_READY;
    urltime = 0;
    fahref = UNDEF;
    inbytes = 0;
    e = API_EINTERNAL;
}

FileAttributeFetch::FileAttributeFetch(handle h, fatype t, int ctag)
{
    nodehandle = h;
    type = t;
    retries = 0;
    tag = ctag;
}

void FileAttributeFetchChannel::dispatch(MegaClient* client)
{
    faf_map::iterator it;
 
    // reserve space
    req.outbuf.clear();
    req.outbuf.reserve((fafs[0].size() + fafs[1].size()) * sizeof(handle));

    for (int i = 2; i--; )
    {
        for (it = fafs[i].begin(); it != fafs[i].end(); )
        {
            req.outbuf.append((char*)&it->first, sizeof(handle));

            if (!i)
            {
                // move from fresh to pending
                fafs[1][it->first] = it->second;
                fafs[0].erase(it++);
            }
            else
            {
                it++;
            }
        }
    }

    if (req.outbuf.size())
    {
        LOG_debug << "Getting file attribute";
        e = API_EFAILED;
        inbytes = 0;
        req.in.clear();
        req.posturl = posturl;
        req.post(client);

        timeout.backoff(150);
    }
    else
    {
        timeout.reset();
        req.status = REQ_PREPARED;
    }
}

// communicate received file attributes to the application
void FileAttributeFetchChannel::parse(MegaClient* client, int /*fac*/, bool final)
{
#pragma pack(push,1)
    struct FaHeader
    {
        handle h;
        uint32_t len;
    };
#pragma pack(pop)

    const char* ptr = req.data();
    const char* endptr = ptr + req.size();
    Node* n;
    SymmCipher* cipher;
    faf_map::iterator it;
    uint32_t falen = 0;

    // data is structured as (handle.8.le / position.4.le) + attribute data
    // attributes are CBC-encrypted with the file's key
    for (;;)
    {
        if (ptr == endptr) break;

        if (ptr + sizeof(FaHeader) > endptr || ptr + sizeof(FaHeader) + (falen = ((FaHeader*)ptr)->len) > endptr)
        {
            if (final || falen > 16*1048576)
            {
                break; 
            }
            else
            {
                req.purge(ptr - req.data());
            }

            break;
        }

        it = fafs[1].find(((FaHeader*)ptr)->h);

        ptr += sizeof(FaHeader);

        // locate fetch request (could have been deleted by the application in the meantime)
        if (it != fafs[1].end())
        {
            // locate related node (could have been deleted)
            if ((n = client->nodebyhandle(it->second->nodehandle)))
            {
                client->restag = it->second->tag;

                if (!(falen & (SymmCipher::BLOCKSIZE - 1)))
                {
                    if ((cipher = n->nodecipher()))
                    {
                        cipher->cbc_decrypt((byte*)ptr, falen);
                        client->app->fa_complete(n, it->second->type, ptr, falen);
                    }

                    delete it->second;
                    fafs[1].erase(it);
                }
            }
        }

        ptr += falen;
    }
}

// notify the application of the request failure and remove records no longer needed
void FileAttributeFetchChannel::failed(MegaClient* client)
{
    for (faf_map::iterator it = fafs[1].begin(); it != fafs[1].end(); )
    {
        client->restag = it->second->tag;

        if (client->app->fa_failed(it->second->nodehandle, it->second->type, it->second->retries, e))
        {
            // no retry desired
            delete it->second;
            fafs[1].erase(it++);
        }
        else
        {
            // retry
            it->second->retries++;

            // move from pending to fresh
            fafs[0][it->first] = it->second;
            fafs[1].erase(it++);
            req.status = REQ_PREPARED;
        }
    }
}
} // namespace
