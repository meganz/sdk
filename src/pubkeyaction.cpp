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

PubKeyActionPutNodes::PubKeyActionPutNodes(vector<NewNode>&& newnodes, int ctag, CommandPutNodes::Completion&& c)
    : nn(std::move(newnodes))
    , completion(std::move(c))
{
    tag = ctag;
}

void PubKeyActionPutNodes::proc(MegaClient* client, User* u)
{
    if (u && u->pubk.isvalid())
    {
        byte buf[AsymmCipher::MAXKEYLENGTH];
        int t;

        // re-encrypt all node keys to the user's public key
        for (size_t i = nn.size(); i--;)
        {
            if (!(t = u->pubk.encrypt(client->rng, (const byte*)nn[i].nodekey.data(), nn[i].nodekey.size(), buf, sizeof buf)))
            {
                if (completion) completion(API_EINTERNAL, USER_HANDLE, nn, false, tag);
                else client->app->putnodes_result(API_EINTERNAL, USER_HANDLE, nn, false, tag);
                return;
            }

            nn[i].nodekey.assign((char*)buf, t);
        }

        client->reqs.add(new CommandPutNodes(client, NodeHandle(), u->uid.c_str(), NoVersioning, std::move(nn), tag, PUTNODES_APP, nullptr, std::move(completion), false));
        // 'canChangeVault' is false here because this code path is to write to user's Inbox, which should not require "vw:1"
    }
    else
    {
        if (completion) completion(API_ENOENT, USER_HANDLE, nn, false, tag);
        else client->app->putnodes_result(API_ENOENT, USER_HANDLE, nn, false, tag);
    }
}

// sharekey distribution request for handle h
PubKeyActionSendShareKey::PubKeyActionSendShareKey(handle h)
{
    sh = h;
}

void PubKeyActionSendShareKey::proc(MegaClient* client, User* u)
{
    std::shared_ptr<Node> n;

    // only the share owner distributes share keys
    if (u && u->pubk.isvalid() && (n = client->nodebyhandle(sh)) && n->sharekey && client->checkaccess(n.get(), OWNER))
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
    assert(!client->mKeyManager.isSecure());
    // This class can be removed as soon as isSecure() is true
    // It's the same as the isSecure() code path in MegaClient::setshare
    // but having the public RSA key of the target to send the
    // share key also using the old method

    std::shared_ptr<Node> n;

    // node vanished: bail
    if (!(n = client->nodebyhandle(h)))
    {
        completion(API_ENOENT, mWritable);
        return;
    }

    // We need to copy the user if it's temporary because
    // it will be deleted when this function finishes
    User *user = u;
    if (u && u->isTemporary)
    {
        user = new User(u->email.c_str());
        user->set(u->show, u->ctime);
        user->uid = u->uid;
        user->userhandle = u->userhandle;
        user->pubk = u->pubk;
        user->isTemporary = true;
    }

    client->setShareCompletion(n.get(), user, a, mWritable, selfemail.c_str(), tag, std::move(completion));
}

// share node sh with access level sa
PubKeyActionCreateShare::PubKeyActionCreateShare(handle sh, accesslevel_t sa, int ctag, bool writable, const char* personal_representation, std::function<void(Error, bool writeable)> f)
{
    h = sh;
    a = sa;
    tag = ctag;
    mWritable = writable;
    completion = std::move(f);
    assert(completion);

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
