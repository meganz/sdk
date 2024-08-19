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
#include "mega/megaclient.h"

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

void PubKeyActionNotifyApp::proc(MegaClient *client, User *u)
{
    client->app->pubkey_result(u);
}

PubKeyActionNotifyApp::PubKeyActionNotifyApp(int ctag)
{
    tag = ctag;
}

} // namespace
