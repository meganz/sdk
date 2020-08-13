/**
 * @file pubkeyaction.cpp
 * @brief Classes for managing public keys
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

#include "mega/pubkeyaction.h"
#include "mega/megaapp.h"
#include "mega/command.h"

namespace mega {
PubKeyAction::PubKeyAction()
{ 
    cmd = NULL; 
}

PubKeyActionPutNodes::PubKeyActionPutNodes(NewNode* newnodes, int numnodes, int ctag)
{
    nn = newnodes;
    nc = numnodes;
    tag = ctag;
}

void PubKeyActionPutNodes::proc(MegaClient* client, User* u)
{
    if (u && u->pubk.isvalid())
    {
        byte buf[AsymmCipher::MAXKEYLENGTH];
        int t;

        // re-encrypt all node keys to the user's public key
        for (int i = nc; i--;)
        {
            if (!(t = u->pubk.encrypt(client->rng, (const byte*)nn[i].nodekey.data(), nn[i].nodekey.size(), buf, sizeof buf)))
            {
                return client->app->putnodes_result(API_EINTERNAL, USER_HANDLE, nn);
            }

            nn[i].nodekey.assign((char*)buf, t);
        }

        client->reqs.add(new CommandPutNodes(client, UNDEF, u->uid.c_str(), nn, nc, tag));
    }
    else
    {
        client->app->putnodes_result(API_ENOENT, USER_HANDLE, nn);
    }
}

// sharekey distribution request for handle h
PubKeyActionSendShareKey::PubKeyActionSendShareKey(handle h)
{
    sh = h;
}

void PubKeyActionSendShareKey::proc(MegaClient* client, User* u)
{
    Node* n;

    // only the share owner distributes share keys
    if (u && u->pubk.isvalid() && (n = client->nodebyhandle(sh)) && n->sharekey && client->checkaccess(n, OWNER))
    {
        int t;
        byte buf[AsymmCipher::MAXKEYLENGTH];

        if ((t = u->pubk.encrypt(client->rng, n->sharekey->key, SymmCipher::KEYLENGTH, buf, sizeof buf)))
        {
            client->reqs.add(new CommandShareKeyUpdate(client, sh, u->uid.c_str(), buf, t));
        }
    }
}

void PubKeyActionCreateShare::proc(MegaClient* client, User* u)
{
    Node* n;
    int newshare;

    // node vanished: bail
    if (!(n = client->nodebyhandle(h)))
    {
        return client->app->share_result(API_ENOENT);
    }

    // do we already have a share key for this node?
    if ((newshare = !n->sharekey))
    {
        // no: create
        byte key[SymmCipher::KEYLENGTH];

        client->rng.genblock(key, sizeof key);

        n->sharekey = new SymmCipher(key);
    }

    // we have all ingredients ready: the target user's public key, the share
    // key and all nodes to share
    client->restag = tag;
    client->reqs.add(new CommandSetShare(client, n, u, a, newshare, NULL, selfemail.c_str()));
}

// share node sh with access level sa
PubKeyActionCreateShare::PubKeyActionCreateShare(handle sh, accesslevel_t sa, int ctag, const char* personal_representation)
{
    h = sh;
    a = sa;
    tag = ctag;

    if (personal_representation)
    {
        selfemail = personal_representation;
    }
}

void PubKeyActionNotifyApp::proc(MegaClient *client, User *u)
{
    client->app->pubkey_result(u);
}

PubKeyActionNotifyApp::PubKeyActionNotifyApp(int ctag)
{
    tag = ctag;
}

} // namespace
