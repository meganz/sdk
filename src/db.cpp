/**
 * @file db.cpp
 * @brief Database access interface
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

#include "mega/db.h"
#include "mega/utils.h"

namespace mega {
DbTable::DbTable()
{
    nextid = 0;
}

bool DbTable::putnode(pnode_t n, SymmCipher* key)
{
    string data;

    // TODO: serialize undecryptable nodes too, not only decryptable ones

    if (!n->serialize(&data))
    {
        //Don't return false if there are errors in the serialization
        //to let the SDK continue and save the rest of records
        return true;
    }

    PaddedCBC::encrypt(&data, key);

    // TODO: encrypt n->nodehandle
    // TODO: encrypt n->parenthandle

    string fpstring;
    if(n->type == FILENODE)
        n->serializefingerprint(&fpstring);

    // TODO: encrypt n->fingerprint??
    // check that an empty fingerprint is saves as NULL

    bool result = putnode(n->nodehandle, n->parenthandle, (char *) fpstring.data(), fpstring.size(), (char *)data.data(), data.size());
    if(result)
    {
        //Add to cache?
    }
    return result;
}


bool DbTable::putuser(User * u, SymmCipher* key)
{
    string data;

    if (!u->serialize(&data))
    {
        //Don't return false if there are errors in the serialization
        //to let the SDK continue and save the rest of records
        return true;
    }

    PaddedCBC::encrypt(&data, key);

    // TODO: encrypt u->email

    bool result = putuser((char*) u->email.data(), u->email.size(), (char *)data.data(), data.size());
    if(result)
    {
        //Add to cache?
    }
    return result;
}

bool DbTable::putpcr(PendingContactRequest *pcr, SymmCipher *key)
{
    return true;
}

bool DbTable::delnode(pnode_t n, SymmCipher *key)
{
    // TODO: encrypt n->nodehandle

    bool result = delnode(n->nodehandle);
    if(result)
    {
        //Add to cache?
    }
    return result;
}

bool DbTable::delpcr(PendingContactRequest *pcr, SymmCipher *key)
{
    return true;
}

bool DbTable::getnode(handle h, string* data, SymmCipher* key)
{
    // TODO: encrypt the nodehandle prior to query

    if (getnodebyhandle(h, data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getnode(string *fingerprint, string* data, SymmCipher* key)
{
    // TODO: encrypt the fingerprint prior to query?? not necessary probably

    if (getnodebyfingerprint(fingerprint, data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getuser(string* data, SymmCipher* key)
{
    // TODO: encrypt the user's email??

    if (next(data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

void DbTable::rewindchildren(handle h, SymmCipher *key)
{
    // TODO: encrypt the handle

    rewindchildren(h);
}

bool DbTable::getchildren(string *data, SymmCipher *key)
{
    if (next(data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

} // namespace
