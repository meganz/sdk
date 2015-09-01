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
#include "mega/logging.h"

namespace mega {
DbTable::DbTable(SymmCipher *key)
{
    this->key = key;
}

bool DbTable::putrootnodes(handle *rootnodes)
{
    string data;

    for (int i=0; i<3; i++)
    {
        encrypthandle(rootnodes[i], &data);

        if (!putrootnode(i+1, &data))    // 0: scsn 1-3: rootnodes
        {
            return false;
        }
    }

    return true;
}

bool DbTable::getrootnodes(handle *rootnodes)
{
    string data;

    for (int i=0; i<3; i++)
    {
        if (!getrootnode(i+1, &data))    // 0: scsn 1-3: rootnodes
        {
            return false;
        }

        decrypthandle(&rootnodes[i], &data);
    }

    return true;
}

bool DbTable::putnode(pnode_t n)
{
    string data;
//    string h, ph;
    string fp;

    n->serialize(&data);

    PaddedCBC::encrypt(&data, key);

//    encrypthandle(n->nodehandle, &h);

//    encrypthandle(n->parenthandle, &ph);

    if(n->type == FILENODE)
    {
        n->serializefingerprint(&fp);
        PaddedCBC::encrypt(&fp, key);
    }

    int shared = 0;
    if (n->outshares)
    {
        shared = 1;
    }
    if (n->inshare)
    {
        shared = 2;
    }
    if (n->pendingshares)
    {
        shared += 3;
        // A node may have outshares and pending shares at the same time (value=4)
        // A node cannot be an inshare and a pending share at the same time
    }

//  bool result = putnode(&h, &ph, &fp, n->attrstring, shared, &data);
    bool result = putnode(n->nodehandle, n->parenthandle, &fp, n->attrstring, shared, &data);

    if(!result)
    {
        LOG_err << "Error recording node " << n->nodehandle;
    }

    return result;
}

bool DbTable::putuser(User * u)
{
    if (ISUNDEF(u->userhandle))
    {
        LOG_debug << "Skipping the recording of a non-existing user";
        // The SDK creates a User during share's creation, even if the target email is not a contact yet
        // The User should not be written into DB as a user, but as a pending contact

        return true;
    }

    string data;
    u->serialize(&data);
    PaddedCBC::encrypt(&data, key);

//    string email = u->email;
//    PaddedCBC::encrypt(&email, key);

    return putuser(u->userhandle, &data);
}

bool DbTable::putpcr(PendingContactRequest *pcr)
{
    string data;
    pcr->serialize(&data);
    PaddedCBC::encrypt(&data, key);

//    string id;
//    encrypthandle(pcr->id, &id);

//    return putpcr(&id, &data);
    return putpcr(pcr->id, &data);
}

//bool DbTable::delnode(pnode_t n)
//{
////    string hstring;
////    encrypthandle(n->nodehandle, &hstring);

////    bool result = delnode(&hstring);
//    return delnode(n->nodehandle);
//}

//bool DbTable::delpcr(PendingContactRequest *pcr)
//{
////    string id;
////    encrypthandle(pcr->id, &id);

////    return delpcr(&id);
//    return delpcr(pcr->id);
//}

bool DbTable::getnode(handle h, string* data)
{
//    string hstring;
//    encrypthandle(h, &hstring);

//    if (getnodebyhandle(&hstring, data))
    if (getnodebyhandle(h, data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getnode(string *fingerprint, string* data)
{
    PaddedCBC::encrypt(fingerprint, key);

    if (getnodebyfingerprint(fingerprint, data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getuser(string* data)
{
    if (next(data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

bool DbTable::getpcr(string *data)
{
    if (next(data))
    {
        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

//bool DbTable::getnumchildren(handle ph, int *count)
//{
////    string hstring;
////    encrypthandle(ph, &hstring);

////    return getnumchildren(&hstring, count);
//    return getnumchildren(ph, count);
//}

//bool DbTable::getnumchildfiles(handle ph, int *count)
//{
////    string hstring;
////    encrypthandle(ph, &hstring);

////    return getnumchildfiles(&hstring, count);
//    return getnumchildfiles(ph, count);
//}

//bool DbTable::getnumchildfolders(handle ph, int *count)
//{
////    string hstring;
////    encrypthandle(ph, &hstring);

////    return getnumchildfolders(&hstring, count);
//    return getnumchildfolders(ph, count);
//}

handle_vector * DbTable::gethandleschildren(handle ph)
{
    handle_vector *hchildren = new handle_vector;
    handle h;

//    string hstring;
//    encrypthandle(ph, &hstring);

//    rewindhandleschildren(&hstring);
    rewindhandleschildren(ph);

//    while (next(&hstring))
    while (nexthandle(&h))
    {
//        decrypthandle(&h, &hstring);
        hchildren->push_back(h);
    }

    return hchildren;
}

handle_vector *DbTable::gethandlesencryptednodes()
{
    handle_vector *hencryptednodes = new handle_vector;

//    string hstring;
    handle h;

    rewindhandlesencryptednodes();

//    while (next(&hstring))
    while (nexthandle(&h))
    {
//        decrypthandle(&h, &hstring);
        hencryptednodes->push_back(h);
    }

    return hencryptednodes;
}

// if 'h' is defined, get only the outshares that are child nodes of 'h'
handle_vector *DbTable::gethandlesoutshares(handle ph)
{
    handle_vector *hshares = new handle_vector;

//    string hstring;
    handle h;
//    if (ph != UNDEF)
//    {
//        encrypthandle(ph, &hstring);
//    }

//    rewindhandlesoutshares(&hstring);
    rewindhandlesoutshares(ph);

//    while (next(&hstring))
    while (nexthandle(&h))
    {
//        decrypthandle(&h, &hstring);
        hshares->push_back(h);
    }

    return hshares;
}

// if 'h' is defined, get only the pending shares that are child nodes of 'h'
handle_vector *DbTable::gethandlespendingshares(handle ph)
{
    handle_vector *hshares = new handle_vector;

//    string hstring;
    handle h;
//    if (ph != UNDEF)
//    {
//        encrypthandle(ph, &hstring);
//    }

//    rewindhandlespendingshares(&hstring);
    rewindhandlespendingshares(ph);

//    while (next(&hstring))
    while (nexthandle(&h))
    {
//        decrypthandle(&h, &hstring);
        hshares->push_back(h);
    }

    return hshares;

}

void DbTable::encrypthandle(handle h, string *hstring)
{
    hstring->resize(sizeof(handle) * 4/3 + 3);
    hstring->resize(Base64::btoa((const byte *)&h, sizeof(handle), (char *) hstring->data()));

    PaddedCBC::encrypt(hstring, key);
}

void DbTable::decrypthandle(handle *h, string *hstring)
{
    if (PaddedCBC::decrypt(hstring, key))
    {
        Base64::atob(hstring->data(), (byte *)h, hstring->size());
    }
}

} // namespace
