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
#include "mega/base64.h"

namespace mega {
DbTable::DbTable()
{

}

bool DbTable::putrootnodes(handle *rootnodes, SymmCipher *key)
{
    string data;

    for (int i=0; i<3; i++)
    {
        encrypthandle(rootnodes[i], &data, key);

        if (!putrootnode(i+1, &data))    // 0: scsn 1-3: rootnodes
        {
            return false;
        }
    }

    return true;
}

bool DbTable::getrootnodes(handle *rootnodes, SymmCipher *key)
{
    string data;

    for (int i=0; i<3; i++)
    {
        if (!getrootnode(i+1, &data))    // 0: scsn 1-3: rootnodes
        {
            return false;
        }

        decrypthandle(&rootnodes[i], &data, key);
    }

    return true;
}

bool DbTable::putnode(pnode_t n, SymmCipher* key)
{
    string data;
    string h, ph, fp;

    // TODO: serialize undecryptable nodes too, not only decryptable ones
    // This changes should avoid `serialize()` returning false -> change if() below

    if (!n->serialize(&data))
    {
        //Don't return false if there are errors in the serialization
        //to let the SDK continue and save the rest of records
        return true;
    }

    PaddedCBC::encrypt(&data, key);

    encrypthandle(n->nodehandle, &h, key);

    encrypthandle(n->parenthandle, &ph, key, true);

    if(n->type == FILENODE)
    {
        n->serializefingerprint(&fp);
        PaddedCBC::encrypt(&fp, key);
    }

    bool result = putnode(&h, &ph, &fp, &data);// (char *) fpstring.data(), fpstring.size(), (char *)data.data(), data.size());

    if(result)
    {
        // TODO: Add to cache?
    }
    return result;
}


bool DbTable::putuser(User * u, SymmCipher* key)
{
    string data;
    u->serialize(&data);
    PaddedCBC::encrypt(&data, key);

    string email = u->email;
    PaddedCBC::encrypt(&email, key);

    return putuser(&email, &data);
}

bool DbTable::putpcr(PendingContactRequest *pcr, SymmCipher *key)
{
    string data;
    pcr->serialize(&data);
    PaddedCBC::encrypt(&data, key);

    string id;
    encrypthandle(pcr->id, &id, key);

    return putpcr(&id, &data);
}

bool DbTable::delnode(pnode_t n, SymmCipher *key)
{
    string hstring;
    encrypthandle(n->nodehandle, &hstring, key);

    bool result = delnode(&hstring);
    if(result)
    {
        // TODO: delete from cache?
    }
    return result;
}

bool DbTable::delpcr(PendingContactRequest *pcr, SymmCipher *key)
{
    string id;
    encrypthandle(pcr->id, &id, key);

    return delpcr(&id);
}

bool DbTable::getnode(handle h, string* data, SymmCipher* key)
{
    string hstring;
    encrypthandle(h, &hstring, key);

    if (getnodebyhandle(&hstring, data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getnode(string *fingerprint, string* data, SymmCipher* key)
{
    PaddedCBC::encrypt(fingerprint, key);

    if (getnodebyfingerprint(fingerprint, data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getuser(string* data, SymmCipher* key)
{
    if (next(data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getpcr(string *data, SymmCipher* key)
{
    if (next(data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

void DbTable::rewindchildren(handle h, SymmCipher *key)
{
    string hstring;
    encrypthandle(h, &hstring, key, true);

    rewindchildren(&hstring);
}

bool DbTable::getchildren(string *data, SymmCipher *key)
{
    if (next(data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

void DbTable::encrypthandle(handle h, string *hstring, SymmCipher *key, bool applyXor)
{
    hstring->resize(sizeof(h));
    hstring->resize(Base64::btoa((const byte *)&h, sizeof(h), (char *) hstring->data()));

    PaddedCBC::encrypt(hstring, key);

    if (applyXor)
    {
        int size = hstring->size();
        byte src[size];
        byte dst[size];

        memcpy(src, hstring->data(), size);

        SymmCipher::xorblock(src, dst);

        hstring->assign((char*)dst, size);
    }
}

void DbTable::decrypthandle(handle *h, string *hstring, SymmCipher *key, bool applyXor)
{
    if (applyXor)
    {
        int size = hstring->size();
        byte src[size];
        byte dst[size];

        memcpy(src, hstring->data(), size);

        SymmCipher::xorblock(src, dst);

        hstring->assign((char*)dst, size);
    }

    PaddedCBC::decrypt(hstring, key);

    Base64::atob(hstring->data(), (byte *)h, 6);
}

} // namespace
