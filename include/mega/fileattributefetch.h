/**
 * @file mega/fileattributefetch.h
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

#ifndef MEGA_FILEATTRIBUTEFETCH_H
#define MEGA_FILEATTRIBUTEFETCH_H 1

#include "backofftimer.h"
#include "types.h"
#include "http.h"

namespace mega {

// file attribute fetching for a specific source cluster
struct MEGA_API FileAttributeFetchChannel
{
    MegaClient *client;

    handle fahref;

    BackoffTimer bt;
    BackoffTimer timeout;

    HttpReq req;
    dstime urltime;
    string posturl;
    size_t inbytes;

    faf_map fafs[2];
    error e;

    // dispatch new and retrying attributes by POSTing to existing URL
    void dispatch();

    // parse fetch result and remove completed attributes from pending
    void parse(int, bool);

    // notify app of nodes that failed to receive their requested attribute
    void failed();

    FileAttributeFetchChannel(MegaClient*);
};

// pending individual attribute fetch
struct MEGA_API FileAttributeFetch
{
    handle nodehandle;
    string nodekey;
    fatype type;
    int retries;
    int tag;

    FileAttributeFetch(handle, string, fatype, int);
};
} // namespace

#endif
