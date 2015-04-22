/**
 * @file commands.cpp
 * @brief Implementation of various commands
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

#include "mega/types.h"
#include "mega/command.h"
#include "mega/megaapp.h"
#include "mega/fileattributefetch.h"
#include "mega/base64.h"
#include "mega/transferslot.h"
#include "mega/transfer.h"
#include "mega/utils.h"
#include "mega/user.h"
#include "mega.h"

namespace mega {
HttpReqCommandPutFA::HttpReqCommandPutFA(MegaClient* client, handle cth, fatype ctype, string* cdata)
{
    cmd("ufa");
    arg("s", cdata->size());

    persistent = true;  // object will be recycled either for retry or for
                        // posting to the file attribute server

    th = cth;
    type = ctype;
    data = cdata;

    binary = true;

    tag = client->reqtag;
}

HttpReqCommandPutFA::~HttpReqCommandPutFA()
{
    delete data;
}

void HttpReqCommandPutFA::procresult()
{
    if (client->json.isnumeric())
    {
        status = REQ_FAILURE;
    }
    else
    {
        const char* p = NULL;

        for (;;)
        {
            switch (client->json.getnameid())
            {
                case 'p':
                    p = client->json.getvalue();
                    break;

                case EOO:
                    if (!p)
                    {
                        status = REQ_FAILURE;
                    }
                    else
                    {
                        Node::copystring(&posturl, p);
                        post(client, data->data(), data->size());
                    }
                    return;

                default:
                    if (!client->json.storeobject())
                    {
                        return client->app->putfa_result(th, type, API_EINTERNAL);
                    }
            }
        }
    }
}

CommandGetFA::CommandGetFA(int p, handle fahref, bool chunked)
{
    part = p;

    cmd("ufa");
    arg("fah", (byte*)&fahref, sizeof fahref);

    if (chunked)
    {
        arg("r", 1);
    }
}

void CommandGetFA::procresult()
{
    fafc_map::iterator it = client->fafcs.find(part);

    if (client->json.isnumeric())
    {
        if (it != client->fafcs.end())
        {
            it->second->req.status = REQ_FAILURE;
        }

        return;
    }

    const char* p = NULL;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'p':
                p = client->json.getvalue();
                break;

            case EOO:
                if (it != client->fafcs.end())
                {
                    if (p)
                    {
                        Node::copystring(&it->second->posturl, p);
                        it->second->urltime = Waiter::ds;
                        it->second->dispatch(client);
                    }
                    else
                    {
                        it->second->req.status = REQ_FAILURE;
                    }
                }

                return;

            default:
                if (!client->json.storeobject())
                {
                    it->second->req.status = REQ_FAILURE;
                    return;
                }
        }
    }
}

CommandAttachFA::CommandAttachFA(handle nh, fatype t, handle ah, int ctag)
{
    cmd("pfa");
    arg("n", (byte*)&nh, MegaClient::NODEHANDLE);

    char buf[64];

    sprintf(buf, "%u*", t);
    Base64::btoa((byte*)&ah, sizeof(ah), strchr(buf + 2, 0));
    arg("fa", buf);

    h = nh;
    type = t;
    tag = ctag;
}

void CommandAttachFA::procresult()
{
    error e;

    if (client->json.isnumeric())
    {
         e = (error)client->json.getint();
    }
    else
    {
         string fa;

         if (client->json.storeobject(&fa))
         {
               return client->app->putfa_result(h, type, fa.c_str());
         }

         e = API_EINTERNAL;
    }

    client->app->putfa_result(h, type, e);
}

// request upload target URL
CommandPutFile::CommandPutFile(TransferSlot* ctslot, int ms)
{
    tslot = ctslot;

    cmd("u");
    arg("s", tslot->fa->size);
    arg("ms", ms);
}

void CommandPutFile::cancel()
{
    Command::cancel();
    tslot = NULL;
}

// set up file transfer with returned target URL
void CommandPutFile::procresult()
{
    if (tslot)
    {
        tslot->pendingcmd = NULL;
    }
    else
    {
        canceled = true;
    }

    if (client->json.isnumeric())
    {
        if (!canceled)
        {
            tslot->transfer->failed((error)client->json.getint());
        }
       
        return;
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'p':
                client->json.storeobject(canceled ? NULL : &tslot->tempurl);
                break;

            case EOO:
                if (canceled) return;

                if (tslot->tempurl.size())
                {
                    tslot->starttime = tslot->lastdata = client->waiter->ds;
                    return tslot->progress();
                }
                else
                {
                    return tslot->transfer->failed(API_EINTERNAL);
                }

            default:
                if (!client->json.storeobject())
                {
                    if (!canceled)
                    {
                        tslot->transfer->failed(API_EINTERNAL);
                    }

                    return;
                }
        }
    }
}

// request temporary source URL for DirectRead
CommandDirectRead::CommandDirectRead(DirectReadNode* cdrn)
{
    drn = cdrn;

    cmd("g");
    arg(drn->p ? "n" : "p", (byte*)&drn->h, MegaClient::NODEHANDLE);
    arg("g", 1);
}

void CommandDirectRead::cancel()
{
    Command::cancel();
    drn = NULL;
}

void CommandDirectRead::procresult()
{
    if (drn)
    {
        drn->pendingcmd = NULL;
    }

    if (client->json.isnumeric())
    {
        if (!canceled && drn)
        {
            return drn->cmdresult((error)client->json.getint());
        }
    }
    else
    {
        error e = API_EINTERNAL;

        for (;;)
        {
            switch (client->json.getnameid())
            {
                case 'g':
                    client->json.storeobject(drn ? &drn->tempurl : NULL);
                    e = API_OK;
                    break;

                case 's':
                    if (drn)
                    {
                        drn->size = client->json.getint();
                    }
                    break;

                case 'd':
                    e = API_EBLOCKED;
                    break;

                case 'e':
                    e = (error)client->json.getint();
                    break;

                case EOO:
                    if (!canceled && drn)
                    {
                        drn->cmdresult(e);
                    }
                    
                    return;

                default:
                    if (!client->json.storeobject())
                    {
                        if (!canceled && drn)
                        {
                            drn->cmdresult(API_EINTERNAL);
                        }
                        
                        return;
                    }
            }
        }
    } 
}

// request temporary source URL for full-file access (p == private node)
CommandGetFile::CommandGetFile(TransferSlot* ctslot, byte* key, handle h, bool p)
{
    cmd("g");
    arg(p ? "n" : "p", (byte*)&h, MegaClient::NODEHANDLE);
    arg("g", 1);

    tslot = ctslot;
    ph = h;

    if (!tslot)
    {
        memcpy(filekey, key, FILENODEKEYLENGTH);
    }
}

void CommandGetFile::cancel()
{
    Command::cancel();
    tslot = NULL;
}

// process file credentials
void CommandGetFile::procresult()
{
    if (tslot)
    {
        tslot->pendingcmd = NULL;
    }

    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();

        if (canceled)
        {
            return;
        }

        if (tslot)
        {
            return tslot->transfer->failed(e);
        }

        return client->app->checkfile_result(ph, e);
    }

    const char* at = NULL;
    error e = API_EINTERNAL;
    m_off_t s = -1;
    int d = 0;
    byte* buf;
    time_t ts = 0, tm = 0;

    // credentials relevant to a non-TransferSlot scenario (node query)
    string fileattrstring;
    string filenamestring;
    string filefingerprint;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'g':
                client->json.storeobject(tslot ? &tslot->tempurl : NULL);
                e = API_OK;
                break;

            case 's':
                s = client->json.getint();
                break;

            case 'd':
                d = 1;
                break;

            case MAKENAMEID2('t', 's'):
                ts = client->json.getint();
                break;

            case MAKENAMEID3('t', 'm', 'd'):
                tm = ts + client->json.getint();
                break;

            case MAKENAMEID2('a', 't'):
                at = client->json.getvalue();
                break;

            case MAKENAMEID2('f', 'a'):
                if (tslot)
                {
                    client->json.storeobject(&tslot->fileattrstring);
                }
                else
                {
                    client->json.storeobject(&fileattrstring);
                }
                break;

            case MAKENAMEID3('p', 'f', 'a'):
                if (tslot)
                {
                    tslot->fileattrsmutable = (int)client->json.getint();
                }
                break;

            case 'e':
                e = (error)client->json.getint();
                break;

            case EOO:
                if (d || !at)
                {
                    e = at ? API_EBLOCKED : API_EINTERNAL;

                    if (canceled)
                    {
                        return;
                    }

                    if (tslot)
                    {
                        return tslot->transfer->failed(e);
                    }

                    return client->app->checkfile_result(ph, e);
                }
                else
                {
                    // decrypt at and set filename
                    SymmCipher key;
                    const char* eos = strchr(at, '"');

                    key.setkey(filekey, FILENODE);

                    if ((buf = Node::decryptattr(tslot ? &tslot->transfer->key : &key,
                                                 at, eos ? eos - at : strlen(at))))
                    {
                        JSON json;

                        json.begin((char*)buf + 5);

                        for (;;)
                        {
                            switch (json.getnameid())
                            {
                                case 'c':
                                    if (!json.storeobject(&filefingerprint))
                                    {
                                        delete[] buf;

                                        if (tslot)
                                        {
                                            return tslot->transfer->failed(API_EINTERNAL);
                                        }

                                        return client->app->checkfile_result(ph, API_EINTERNAL);
                                    }
                                    break;

                                case 'n':
                                    if (!json.storeobject(&filenamestring))
                                    {
                                        delete[] buf;

                                        if (tslot)
                                        {
                                            return tslot->transfer->failed(API_EINTERNAL);
                                        }

                                        return client->app->checkfile_result(ph, API_EINTERNAL);
                                    }
                                    break;

                                case EOO:
                                    delete[] buf;

                                    if (tslot)
                                    {
                                        tslot->starttime = tslot->lastdata = client->waiter->ds;

                                        if (tslot->tempurl.size() && s >= 0)
                                        {
                                            return tslot->progress();
                                        }

                                        return tslot->transfer->failed(e);
                                    }
                                    else
                                    {
                                        return client->app->checkfile_result(ph, e, filekey, s, ts, tm,
                                                                             &filenamestring,
                                                                             &filefingerprint,
                                                                             &fileattrstring);
                                    }

                                default:
                                    if (!json.storeobject())
                                    {
                                        delete[] buf;

                                        if (tslot)
                                        {
                                            return tslot->transfer->failed(API_EINTERNAL);
                                        }
                                        else
                                        {
                                            return client->app->checkfile_result(ph, API_EINTERNAL);
                                        }
                                    }
                            }
                        }
                    }

                    if (canceled)
                    {
                        return;
                    }

                    if (tslot)
                    {
                        return tslot->transfer->failed(API_EKEY);
                    }
                    else
                    {
                        return client->app->checkfile_result(ph, API_EKEY);
                    }
                }

            default:
                if (!client->json.storeobject())
                {
                    if (tslot)
                    {
                        return tslot->transfer->failed(API_EINTERNAL);
                    }
                    else
                    {
                        return client->app->checkfile_result(ph, API_EINTERNAL);
                    }
                }
        }
    }
}

CommandSetAttr::CommandSetAttr(MegaClient* client, Node* n, SymmCipher* cipher, const char* prevattr)
{
    cmd("a");
    notself(client);

    string at;

    n->attrs.getjson(&at);
    client->makeattr(cipher, &at, at.c_str(), at.size());

    arg("n", (byte*)&n->nodehandle, MegaClient::NODEHANDLE);
    arg("at", (byte*)at.c_str(), at.size());

    h = n->nodehandle;
    tag = client->reqtag;
    syncop = prevattr;

    if(prevattr)
    {
        pa = prevattr;
    }
}

void CommandSetAttr::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();
#ifdef ENABLE_SYNC
        if(!e && syncop)
        {
            Node* node = client->nodebyhandle(h);
            if(node)
            {
                Sync* sync = NULL;
                for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
                {
                    if((*it)->tag == tag)
                    {
                        sync = (*it);
                        break;
                    }
                }

                if(sync)
                {
                    client->app->syncupdate_remote_rename(sync, node, pa.c_str());
                }
            }
        }
#endif
        client->app->setattr_result(h, e);
    }
    else
    {
        client->json.storeobject();
        client->app->setattr_result(h, API_EINTERNAL);
    }
}

// (the result is not processed directly - we rely on the server-client
// response)
CommandPutNodes::CommandPutNodes(MegaClient* client, handle th,
                                 const char* userhandle, NewNode* newnodes,
                                 int numnodes, int ctag, putsource_t csource)
{
    byte key[FILENODEKEYLENGTH];
    int i;

    nn = newnodes;
    nnsize = numnodes;
    type = userhandle ? USER_HANDLE : NODE_HANDLE;
    source = csource;

    cmd("p");
    notself(client);

    if (userhandle)
    {
        arg("t", userhandle);
    }
    else
    {
        arg("t", (byte*)&th, MegaClient::NODEHANDLE);
    }

    arg("sm",1);

    beginarray("n");

    for (i = 0; i < numnodes; i++)
    {
        beginobject();

        switch (nn[i].source)
        {
            case NEW_NODE:
                arg("h", (byte*)&nn[i].nodehandle, MegaClient::NODEHANDLE);
                break;

            case NEW_PUBLIC:
                arg("ph", (byte*)&nn[i].nodehandle, MegaClient::NODEHANDLE);
                break;

            case NEW_UPLOAD:
                arg("h", nn[i].uploadtoken, sizeof nn->uploadtoken);

                // include pending file attributes for this upload
                string s;

                client->pendingattrstring(nn[i].uploadhandle, &s);

                if (s.size())
                {
                    arg("fa", s.c_str(), 1);
                }
        }

        if (!ISUNDEF(nn[i].parenthandle))
        {
            arg("p", (byte*)&nn[i].parenthandle, MegaClient::NODEHANDLE);
        }

        arg("t", nn[i].type);
        arg("a", (byte*)nn[i].attrstring->data(), nn[i].attrstring->size());

        if (nn[i].nodekey.size() <= sizeof key)
        {
            client->key.ecb_encrypt((byte*)nn[i].nodekey.data(), key, nn[i].nodekey.size());
            arg("k", key, nn[i].nodekey.size());
        }
        else
        {
            arg("k", (const byte*)nn[i].nodekey.data(), nn[i].nodekey.size());
        }

        endobject();
    }

    endarray();

    // add cr element for new nodes, if applicable
    if (type == NODE_HANDLE)
    {
        Node* tn;

        if ((tn = client->nodebyhandle(th)))
        {
            ShareNodeKeys snk;

            for (i = 0; i < numnodes; i++)
            {
                switch (nn[i].source)
                {
                    case NEW_NODE:
                        snk.add((NodeCore*)(nn + i), tn, 0);
                        break;

                    case NEW_UPLOAD:
                        snk.add((NodeCore*)(nn + i), tn, 0, nn[i].uploadtoken, (int)sizeof nn->uploadtoken);
                        break;

                    case NEW_PUBLIC:
                        break;
                }
            }

            snk.get(this);
        }
    }

    tag = ctag;
}

// add new nodes and handle->node handle mapping
void CommandPutNodes::procresult()
{
    error e;

    if (client->json.isnumeric())
    {
        e = (error)client->json.getint();

#ifdef ENABLE_SYNC
        if (source == PUTNODES_SYNC)
        {
            client->app->putnodes_result(e, type, NULL);
            return client->putnodes_sync_result(e, nn, 0);
        }
        else
#endif
        if (source == PUTNODES_APP)
        {
            return client->app->putnodes_result(e, type, nn);
        }
#ifdef ENABLE_SYNC
        else
        {
            return client->putnodes_syncdebris_result(e, nn);
        }
#endif
    }

    e = API_EINTERNAL;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'f':
                if (client->readnodes(&client->json, 1, source, nn, nnsize, tag))
                {
                    e = API_OK;
                }
                break;

            default:
                if (client->json.storeobject())
                {
                    continue;
                }

                e = API_EINTERNAL;
                // fall through
            case EOO:
                client->applykeys();

#ifdef ENABLE_SYNC
                if (source == PUTNODES_SYNC)
                {
                    client->app->putnodes_result(e, type, NULL);
                    client->putnodes_sync_result(e, nn, nnsize);
                }
                else
#endif
                if (source == PUTNODES_APP)
                {
                    client->app->putnodes_result(e, type, nn);
                }
#ifdef ENABLE_SYNC
                else
                {
                    client->putnodes_syncdebris_result(e, nn);
                }
#endif
                return;
        }
    }
}

CommandMoveNode::CommandMoveNode(MegaClient* client, Node* n, Node* t, syncdel_t csyncdel, handle prevparent)
{
    h = n->nodehandle;
    syncdel = csyncdel;
    pp = prevparent;
    syncop = pp != UNDEF;

    cmd("m");
    notself(client);
    arg("n", (byte*)&h, MegaClient::NODEHANDLE);
    arg("t", (byte*)&t->nodehandle, MegaClient::NODEHANDLE);

    TreeProcShareKeys tpsk;
    client->proctree(n, &tpsk);
    tpsk.get(this);

    tag = client->reqtag;
}

void CommandMoveNode::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();
#ifdef ENABLE_SYNC
        if (syncdel != SYNCDEL_NONE)
        {
            Node* syncn = client->nodebyhandle(h);

            if (syncn)
            {
                if (e == API_OK)
                {
                    Node* n;

                    // update all todebris records in the subtree
                    for (node_set::iterator it = client->todebris.begin(); it != client->todebris.end(); it++)
                    {
                        n = *it;

                        do {
                            if (n == syncn)
                            {
                                if(syncop)
                                {
                                    Sync* sync = NULL;
                                    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
                                    {
                                        if((*it)->tag == tag)
                                        {
                                            sync = (*it);
                                            break;
                                        }
                                    }

                                    if(sync)
                                    {
                                        if (n->type == FOLDERNODE)
                                        {
                                            sync->client->app->syncupdate_remote_folder_deletion(sync, n);
                                        }
                                        else
                                        {
                                            sync->client->app->syncupdate_remote_file_deletion(sync, n);
                                        }
                                    }
                                }

                                (*it)->syncdeleted = syncdel;
                                break;
                            }
                        } while ((n = n->parent));
                    }
                }
                else
                {
                    syncn->syncdeleted = SYNCDEL_NONE;
                }
            }
        }
        else if(syncop)
        {
            Node *n = client->nodebyhandle(h);
            if(n)
            {
                Sync *sync = NULL;
                for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
                {
                    if((*it)->tag == tag)
                    {
                        sync = (*it);
                        break;
                    }
                }

                if(sync)
                {
                    client->app->syncupdate_remote_move(sync, n, client->nodebyhandle(pp));
                }
            }
        }
#endif
        client->app->rename_result(h, e);
    }
    else
    {
        client->json.storeobject();
        client->app->rename_result(h, API_EINTERNAL);
    }
}

CommandDelNode::CommandDelNode(MegaClient* client, handle th)
{
    cmd("d");
    notself(client);

    arg("n", (byte*)&th, MegaClient::NODEHANDLE);

    h = th;
    tag = client->reqtag;
}

void CommandDelNode::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->unlink_result(h, (error)client->json.getint());
    }
    else
    {
        error e = API_EINTERNAL;
        for (;;)
        {
            switch (client->json.getnameid())
            {
                case 'r':
                    if (client->json.enterarray())
                    {
                        if(client->json.isnumeric())
                        {
                            e = (error)client->json.getint();
                        }

                        client->json.leavearray();
                    }
                    break;

                case EOO:
                    client->app->unlink_result(h, e);
                    return;

                default:
                    if (!client->json.storeobject())
                    {
                        client->app->unlink_result(h, API_EINTERNAL);
                        return;
                    }
            }
        }
    }
}

CommandKillSessions::CommandKillSessions(MegaClient* client)
{
    cmd("usr");
    arg("ko", 1); // Request to kill all sessions except the current one
    
    h = UNDEF;
    tag = client->reqtag;
}

CommandKillSessions::CommandKillSessions(MegaClient* client, handle sessionid)
{
    cmd("usr");
    beginarray("s");
    element(sessionid, MegaClient::USERHANDLE);
    endarray();
    
    h = sessionid;
    tag = client->reqtag;
}
void CommandKillSessions::procresult()
{
    error e;

    if (client->json.isnumeric())
    {
        e = (error)client->json.getint();
    }
    else
    {
        e = API_EINTERNAL;
    }

    client->app->sessions_killed(h, e);
}

CommandLogout::CommandLogout(MegaClient *client)
{
    cmd("sml");

    tag = client->reqtag;
}

void CommandLogout::procresult()
{
    error e = (error)client->json.getint();
    MegaApp *app = client->app;
    if(!e)
    {
        if (client->sctable)
        {
            client->sctable->remove();
        }

#ifdef ENABLE_SYNC
        for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
        {
            if((*it)->statecachetable)
            {
                (*it)->statecachetable->remove();
            }
        }
#endif
        client->locallogout();
    }
    app->logout_result(e);
}

// login request with user e-mail address and user hash
CommandLogin::CommandLogin(MegaClient* client, const char* email, uint64_t emailhash)
{
    cmd("us");

    // are we just performing a session validation?
    checksession = !email;

    if (!checksession)
    {
        arg("user", email);
        arg("uh", (byte*)&emailhash, sizeof emailhash);
        this->email.assign(email);
    }

    if (client->cachedscsn != UNDEF)
    {
        arg("sn", (byte*)&client->cachedscsn, sizeof client->cachedscsn);
    }

    tag = client->reqtag;
}

// process login result
void CommandLogin::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->login_result((error)client->json.getint());
    }

    byte hash[SymmCipher::KEYLENGTH];
    byte sidbuf[AsymmCipher::MAXKEYLENGTH];
    byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
    int len_k = 0, len_privk = 0, len_csid = 0, len_tsid = 0;
    handle me = UNDEF;
    bool setKeypair = false;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'k':
                len_k = client->json.storebinary(hash, sizeof hash);
                break;

            case 'u':
                me = client->json.gethandle(MegaClient::USERHANDLE);
                //client->json.storebinary((byte*)&me, sizeof me);
                LOG_info << "ME ";
                break;

            case MAKENAMEID4('t', 's', 'i', 'd'):
                len_tsid = client->json.storebinary(sidbuf, sizeof sidbuf);
                break;

            case MAKENAMEID4('c', 's', 'i', 'd'):
                len_csid = client->json.storebinary(sidbuf, sizeof sidbuf);
                break;

            case MAKENAMEID5('p', 'r', 'i', 'v', 'k'):
                len_privk = client->json.storebinary(privkbuf, sizeof privkbuf);
                break;

            case MAKENAMEID2('s', 'n'):
                if (!client->json.getint())
                {
                    // local state cache continuity rejected: read state from
                    // server instead
                    client->cachedscsn = UNDEF;
                }
                break;

            case EOO:
                if (!checksession)
                {
                    if (ISUNDEF(me) || len_k != sizeof hash)
                    {
                        return client->app->login_result(API_EINTERNAL);
                    }

                    // decrypt and set master key
                    client->key.ecb_decrypt(hash);
                    client->key.setkey(hash);
                }

                if (len_tsid)
                {
                    client->setsid(sidbuf, MegaClient::SIDLEN);

                    // account does not have an RSA keypair set: verify
                    // password using symmetric challenge
                    if (!client->checktsid(sidbuf, len_tsid))
                    {
                        return client->app->login_result(API_EKEY);
                    }

                    // add missing RSA keypair
                    setKeypair = true;
                }
                else
                {
                    // account has RSA keypair: decrypt server-provided session ID
                    if (len_privk < 256)
                    {
                        return client->app->login_result(API_EINTERNAL);
                    }

                    // decrypt and set private key
                    client->key.ecb_decrypt(privkbuf, len_privk);

                    if (!client->asymkey.setkey(AsymmCipher::PRIVKEY, privkbuf, len_privk))
                    {
                        return client->app->login_result(API_EKEY);
                    }

                    if (!checksession)
                    {
                        if (len_csid < 32)
                        {
                            return client->app->login_result(API_EINTERNAL);                   
                        }

                        // decrypt and set session ID for subsequent API communication
                        if (!client->asymkey.decrypt(sidbuf, len_csid, sidbuf, MegaClient::SIDLEN))
                        {
                            return client->app->login_result(API_EINTERNAL);
                        }

                        client->setsid(sidbuf, MegaClient::SIDLEN);
                    }
                }
                client->me = me;

                {
                    MegaClient *c = client;
                    int reqtag = tag;

                    // Series of lambdas for performing initialization.
                    // Here we make no assumptions as to whether the ed25519 keys
                    // have been set or not, as the account may have been created
                    // before their addition. If there is a failure to upload the
                    // RSA keys we *must* fail early, as the signature for the key
                    // could change on retry.
                    auto f = [c, reqtag](error e){
                        if(e != API_OK) {
                            c->restag = reqtag;
                            c->app->login_result(e);
                            return;
                        }
                        // Get own signing keys, bail on failure.
                        c->getownsigningkeys([c, reqtag](ValueMap map, error e){
                            if(e != API_OK) {
                                c->restag = reqtag;
                                c->app->login_result(e);
                            }
                            // Fetch ed25519 and RSA keyrings.
                            c->fetchKeyrings([c, reqtag](error e){
                                c->restag = reqtag;
                                c->app->login_result(e);
                            });

                        });
                        // set final parameter to true to reset keys.
                    };

                    // If we need to set the keypair, then do so, and pass in f
                    // as the callback.
                    if(setKeypair)
                    {
                        LOG_info << "Generating and adding missing RSA keypair";
                        c->setkeypair(f);
                    }
                    // Otherwise, just call f.
                    else
                    {
                        f(API_OK);
                    }
                }

                return;

            default:
                if (!client->json.storeobject())
                {
                    return client->app->login_result(API_EINTERNAL);
                }
        }
    }
}

CommandShareKeyUpdate::CommandShareKeyUpdate(MegaClient* client, handle sh, const char* uid, const byte* key, int len)
{
    cmd("k");
    beginarray("sr");

    element(sh, MegaClient::NODEHANDLE);
    element(uid);
    element(key, len);

    endarray();
}

CommandShareKeyUpdate::CommandShareKeyUpdate(MegaClient* client, handle_vector* v)
{
    Node* n;
    byte sharekey[SymmCipher::KEYLENGTH];

    cmd("k");
    beginarray("sr");

    for (int i = v->size(); i--;)
    {
        handle h = (*v)[i];

        if ((n = client->nodebyhandle(h)) && n->sharekey)
        {
            client->key.ecb_encrypt(n->sharekey->key, sharekey, SymmCipher::KEYLENGTH);

            element(h, MegaClient::NODEHANDLE);
            element(client->me, 8);
            element(sharekey, SymmCipher::KEYLENGTH);
        }
    }

    endarray();
}

// add/remove share; include node share keys if new share
CommandSetShare::CommandSetShare(MegaClient* client, Node* n, User* u, accesslevel_t a, int newshare)
{
    byte auth[SymmCipher::BLOCKSIZE];
    byte key[SymmCipher::KEYLENGTH];
    byte asymmkey[AsymmCipher::MAXKEYLENGTH];
    string uid;
    int t;

    tag = client->restag;

    sh = n->nodehandle;
    user = u;
    access = a;

    cmd("s");
    arg("n", (byte*)&sh, MegaClient::NODEHANDLE);

    if (a != ACCESS_UNKNOWN)
    {
        // securely store/transmit share key
        // by creating a symmetrically (for the sharer) and an asymmetrically
        // (for the sharee) encrypted version
        memcpy(key, n->sharekey->key, sizeof key);
        memcpy(asymmkey, key, sizeof key);

        client->key.ecb_encrypt(key);
        arg("ok", key, sizeof key);

        if (u && u->pubk.isvalid())
        {
            t = u->pubk.encrypt(asymmkey, SymmCipher::KEYLENGTH, asymmkey, sizeof asymmkey);
        }

        // outgoing handle authentication
        client->handleauth(sh, auth);
        arg("ha", auth, sizeof auth);
    }

    beginarray("s");
    beginobject();

    arg("u", u ? u->uid.c_str() : MegaClient::EXPORTEDLINK);

    if (a != ACCESS_UNKNOWN)
    {
        arg("r", a);

        if (u && u->pubk.isvalid())
        {
            arg("k", asymmkey, t);
        }
    }

    endobject();
    endarray();

    // only for a fresh share: add cr element with all node keys encrypted to
    // the share key
    if (newshare)
    {
        // the new share's nodekeys for this user: generate node list
        TreeProcShareKeys tpsk(n);
        client->proctree(n, &tpsk);
        tpsk.get(this);
    }
}

// process user element (email/handle pairs)
bool CommandSetShare::procuserresult(MegaClient* client)
{
    while (client->json.enterobject())
    {
        handle uh = UNDEF;
        const char* m = NULL;

        for (;;)
        {
            switch (client->json.getnameid())
            {
                case 'u':
                    uh = client->json.gethandle(MegaClient::USERHANDLE);
                    break;

                case 'm':
                    m = client->json.getvalue();
                    break;

                case EOO:
                    if (!ISUNDEF(uh) && m)
                    {
                        client->mapuser(uh, m);
                    }
                    return true;

                default:
                    if (!client->json.storeobject())
                    {
                        return false;
                    }
            }
        }
    }

    return false;
}

// process result of share addition/modification
void CommandSetShare::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->share_result((error)client->json.getint());
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
            byte key[SymmCipher::KEYLENGTH + 1];

            case MAKENAMEID2('o', 'k'):  // an owner key response will only
                                         // occur if the same share was created
                                         // concurrently with a different key
                if (client->json.storebinary(key, sizeof key + 1) == SymmCipher::KEYLENGTH)
                {
                    Node* n;

                    if ((n = client->nodebyhandle(sh)) && n->sharekey)
                    {
                        client->key.ecb_decrypt(key);
                        n->sharekey->setkey(key);

                        // repeat attempt with corrected share key
                        client->restag = tag;
                        client->reqs[client->r].add(new CommandSetShare(client, n, user, access, 0));
                        return;
                    }
                }
                break;

            case 'u':   // user/handle confirmation
                if (client->json.enterarray())
                {
                    while (procuserresult(client))
                    {}
                    client->json.leavearray();
                }
                break;

            case 'r':
                if (client->json.enterarray())
                {
                    int i = 0;

                    while (client->json.isnumeric())
                    {
                        client->app->share_result(i++, (error)client->json.getint());
                    }

                    client->json.leavearray();
                }
                break;

            case MAKENAMEID3('s', 'n', 'k'):
                client->procsnk(&client->json);
                break;

            case MAKENAMEID3('s', 'u', 'k'):
                client->procsuk(&client->json);
                break;

            case MAKENAMEID2('c', 'r'):
                client->proccr(&client->json);
                break;

            case EOO:
                client->app->share_result(API_OK);
                return;

            default:
                if (!client->json.storeobject())
                {
                    return;
                }
        }
    }
}

CommandEnumerateQuotaItems::CommandEnumerateQuotaItems(MegaClient* client)
{
    cmd("utqa");
    arg("f", 1);

    tag = client->reqtag;
}

void CommandEnumerateQuotaItems::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->enumeratequotaitems_result((error)client->json.getint());
    }

    handle product;
    int prolevel, gbstorage, gbtransfer, months;
    unsigned amount;
    const char* a;
    const char* c;
    const char* d;
    const char* ios;
    const char* android;
    string currency;
    string description;
    string ios_id;
    string android_id;

    while (client->json.enterarray())
    {
        if (ISUNDEF((product = client->json.gethandle(8)))
                || ((prolevel = client->json.getint()) < 0)
                || ((gbstorage = client->json.getint()) < 0)
                || ((gbtransfer = client->json.getint()) < 0)
                || ((months = client->json.getint()) < 0)
                || !(a = client->json.getvalue())
                || !(c = client->json.getvalue())
                || !(d = client->json.getvalue())
                || !(ios = client->json.getvalue())
                || !(android = client->json.getvalue()))
        {
            return client->app->enumeratequotaitems_result(API_EINTERNAL);
        }


        Node::copystring(&currency, c);
        Node::copystring(&description, d);
        Node::copystring(&ios_id, ios);
        Node::copystring(&android_id, android);


        amount = atoi(a) * 100;
        if ((c = strchr(a, '.')))
        {
            c++;
            if ((*c >= '0') && (*c <= '9'))
            {
                amount += (*c - '0') * 10;
            }
            c++;
            if ((*c >= '0') && (*c <= '9'))
            {
                amount += *c - '0';
            }
        }

        client->app->enumeratequotaitems_result(product, prolevel, gbstorage,
                                                gbtransfer, months, amount,
                                                currency.c_str(), description.c_str(),
                                                ios_id.c_str(), android_id.c_str());
        client->json.leavearray();
    }

    client->app->enumeratequotaitems_result(API_OK);
}

CommandPurchaseAddItem::CommandPurchaseAddItem(MegaClient* client, int itemclass,
                                               handle item, unsigned price,
                                               const char* currency, unsigned tax,
                                               const char* country, const char* affiliate)
{
    string sprice;
    sprice.resize(128);
    sprintf((char *)sprice.data(), "%.2f", price/100.0);
    replace( sprice.begin(), sprice.end(), ',', '.');
    cmd("uts");
    arg("it", itemclass);
    arg("si", (byte*)&item, 8);
    arg("p", sprice.c_str());
    arg("c", currency);
    if (affiliate)
    {
        arg("aff", affiliate);
    }
    else
    {
        arg("aff", (m_off_t)0);
    }

    tag = client->reqtag;

    //TODO: Complete this (tax? country?)
}

void CommandPurchaseAddItem::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->additem_result((error)client->json.getint());
    }

    handle item = client->json.gethandle(8);
    if (item != UNDEF)
    {
        client->purchase_basket.push_back(item);
        client->app->additem_result(API_OK);
    }
    else
    {
        client->json.storeobject();
        client->app->additem_result(API_EINTERNAL);
    }
}

CommandPurchaseCheckout::CommandPurchaseCheckout(MegaClient* client, int gateway)
{
    cmd("utc");

    beginarray("s");
    for (handle_vector::iterator it = client->purchase_basket.begin(); it != client->purchase_basket.end(); it++)
    {
        element((byte*)&*it, sizeof(handle));
    }

    endarray();

    arg("m", gateway);

    // empty basket
    client->purchase_begin();

    tag = client->reqtag;
}

void CommandPurchaseCheckout::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->checkout_result((error)client->json.getint());
    }

    string response;

    client->json.storeobject(&response);

    client->app->checkout_result(response.c_str());
}

CommandUserRequest::CommandUserRequest(MegaClient* client, const char* m, visibility_t show)
{
    cmd("ur");
    arg("u", m);
    arg("l", (int)show);

    tag = client->reqtag;
}

void CommandUserRequest::procresult()
{
    error e;

    if (client->json.isnumeric())
    {
        e = (error)client->json.getint();
    }
    else
    {
        client->json.storeobject();
        e = API_OK;
    }

    client->app->invite_result(e);
}

CommandPutUA::CommandPutUA(MegaClient* client, const char *an, const byte* av, unsigned avl)
{
    cmd("up");
    arg(an, av, avl);

    tag = client->reqtag;
}

void CommandPutUA::procresult()
{
    error e;

    if (client->json.isnumeric())
    {
        e = (error)client->json.getint();
    }
    else
    {
        client->json.storeobject();
        e = API_OK;
    }

    client->app->putua_result(e);
}

CommandGetUA::CommandGetUA(MegaClient* client, const char* uid, const char* an, int p)
{
    priv = p;
    user = client->finduser((char*)uid);
    attributename = an;

    cmd("uga");
    arg("ua", an);

    tag = client->reqtag;
}

void CommandGetUA::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();

        return(client->app->getua_result(e));
    }
    else
    {
        string d;
        const char* ptr;
        const char* end;

        if (!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            return(client->app->getua_result(API_EINTERNAL));
        }

        int l = (end - ptr) / 4 * 3 + 3;

        byte* data = new byte[l];

        l = Base64::atob(ptr, data, l);

        if (priv)
        {
            d.assign((char*)data, l);
            delete[] data;

            // Is the data a multiple of the cipher blocksize, then we're using
            // a zero IV.
            if (l % client->key.BLOCKSIZE == 0)
            {
                if (!PaddedCBC::decrypt(&d, &client->key))
                {
                    return(client->app->getua_result(API_EINTERNAL));
                }
            }
            else
            {
                // We need to shave off our 8 byte IV first.
                string iv;
                iv.assign(d, 0, 8);
                string payload;
                payload.assign(d, 8, l - 8);
                d = payload;
                if (!PaddedCBC::decrypt(&d, &client->key, &iv))
                {
                    return(client->app->getua_result(API_EINTERNAL));
                }
            }
            return(client->app->getua_result((byte*)d.data(), d.size()));
        }

        client->app->getua_result(data, l);

        delete[] data;
    }
}

// ATTR

CommandGetUserAttr::CommandGetUserAttr(MegaClient *client, const char *uid,
        const char *an, int p, AttrCallBack callBack) {
    priv = p;
    user = client->finduser(uid);
    attributename = an;
    cmd("uga");
    arg("u", user->uid.c_str());
    arg("ua", an);
    this->callBack = callBack;
    tag = client->reqtag;
}

CommandGetUserAttr::CommandGetUserAttr(MegaClient *client, std::string &email,
        const char *an, int p, AttrCallBack callback) {
    priv = p;
    attributename = an;
    user = NULL;
    cmd("uga");
    arg("u", email.c_str());
    arg("ua", an);
    this->callBack = callback;
    tag = client->reqtag;
}

void
CommandGetUserAttr::procresult() {

    ValueMap map;
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();
        callBack(map, e);
        return;
    }
    else {

        const char *ptr;
        const char *end;
        if(!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
        {

            callBack(map, API_EINTERNAL);
            return;
        }

        int l = (end - ptr) / 4 * 3 + 3;
        byte data[l];
        l = Base64::atob(ptr, data, l);
        char priv = data[0];

        SharedBuffer tlv;

        if(priv == '+'){
            int offset = ((priv = data[1]) == '!') ? 2 : 1;
            tlv = SharedBuffer((unsigned char*)data + offset, l - offset);
            map = ValueMap(new std::map<std::string, SharedBuffer>);
            map->insert({"", tlv});
            callBack(map, API_OK);
        }
        else if(priv == '*'){

            int offset = ((priv = data[1]) == '!') ? 2 : 1;

            std::string d((char*)(data + offset), l - offset);
            if (l % client->key.BLOCKSIZE == 0)
            {
                if (!PaddedCBC::decrypt(&d, &client->key))
                {

                    callBack(map, API_EINTERNAL);
                    return;
                }
            }
            else
            {
                // We need to shave off our 8 byte IV first.
                string iv;
                iv.assign(d, 0, 8);
                string payload;
                payload.assign(d, 8, l - 8);
                d = payload;
                if (!PaddedCBC::decrypt(&d, &client->key, &iv))
                {

                    callBack(map, API_EINTERNAL);
                    return;
                }
            }

            tlv = SharedBuffer((byte*)d.c_str(), d.size());

            try {
               map = UserAttributes::tlvToValueMap(tlv);
               callBack(map, API_OK);
            }
            catch(std::runtime_error &e) {
                callBack(map, API_EINTERNAL);
            }
        }
        else {
            callBack(map, API_EINTERNAL);
            return;
        }

    }

}

CommandSetUserAttr::CommandSetUserAttr(MegaClient *client,
        const char *an, byte *av, unsigned int avl, SetAttrCallBack callBack) {
    int l = avl * (4 * 3 + 3);
    char data[l];
    l = Base64::btoa(av, avl, data);
    cmd("up");
    arg(an, data, l);
    this->callBack = callBack;
    tag = client->reqtag;
}

void
CommandSetUserAttr::procresult() {
    if (client->json.isnumeric())
    {
        LOG_debug << "error processing set";
        error e = (error)client->json.getint();
        callBack(e);
    }
    else {
        LOG_debug << "Attribute Set";
        client->json.storeobject();
        callBack(API_OK);
    }
}

///////////////////////////////////////////////////////////////////
// set node keys (e.g. to convert asymmetric keys to symmetric ones)
CommandNodeKeyUpdate::CommandNodeKeyUpdate(MegaClient* client, handle_vector* v)
{
    byte nodekey[FILENODEKEYLENGTH];

    cmd("k");
    beginarray("nk");

    for (int i = v->size(); i--;)
    {
        handle h = (*v)[i];

        Node* n;

        if ((n = client->nodebyhandle(h)))
        {
            client->key.ecb_encrypt((byte*)n->nodekey.data(), nodekey, n->nodekey.size());

            element(h, MegaClient::NODEHANDLE);
            element(nodekey, n->nodekey.size());
        }
    }

    endarray();
}

CommandSingleKeyCR::CommandSingleKeyCR(handle sh, handle nh, const byte* key, unsigned keylen)
{
    cmd("k");
    beginarray("cr");

    beginarray();
    element(sh, MegaClient::NODEHANDLE);
    endarray();

    beginarray();
    element(nh, MegaClient::NODEHANDLE);
    endarray();

    beginarray();
    element(0);
    element(0);
    element(key, keylen);
    endarray();

    endarray();
}

CommandKeyCR::CommandKeyCR(MegaClient* client, node_vector* rshares, node_vector* rnodes, const char* keys)
{
    cmd("k");
    beginarray("cr");

    beginarray();
    for (int i = 0; i < (int)rshares->size(); i++)
    {
        element((*rshares)[i]->nodehandle, MegaClient::NODEHANDLE);
    }

    endarray();

    beginarray();
    for (int i = 0; i < (int)rnodes->size(); i++)
    {
        element((*rnodes)[i]->nodehandle, MegaClient::NODEHANDLE);
    }

    endarray();

    beginarray();
    appendraw(keys);
    endarray();

    endarray();
}

// a == ACCESS_UNKNOWN: request public key for user handle and respond with
// share key for sn
// otherwise: request public key for user handle and continue share creation
// for node sn to user u with access a
CommandPubKeyRequest::CommandPubKeyRequest(MegaClient* client, User* user)
{
    cmd("uk");
    arg("u", user->uid.c_str());

    tag = client->reqtag;
    u = user;
    callback = [client, this](handle uh, byte *pk, int len_pubk) mutable {
      // satisfy all pending PubKeyAction requests for this user
        while (u->pkrs.size())
        {
             client->restag = tag;
             u->pkrs[0]->proc(client, u);
             delete u->pkrs[0];
             u->pkrs.pop_front();
        }

         if (len_pubk)
         {
             client->notifyuser(u);
         }
    };
}

CommandPubKeyRequest::CommandPubKeyRequest(MegaClient *client, User *user,
        std::function<void(handle, byte*, int)> callback)
{
    cmd("uk");
    arg("u", user->uid.c_str());

    tag = client->reqtag;
    u = user;
    this->callback = callback;
}

void CommandPubKeyRequest::procresult()
{
    byte pubkbuf[AsymmCipher::MAXKEYLENGTH];
    int len_pubk = 0;
    handle uh = UNDEF;

    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();
        if(e != API_ENOENT) //API_ENOENT = unregistered users or accounts without a public key yet
        {
            LOG_err << "Unexpected error in CommandPubKeyRequest: " << e;
        }
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'u':
                uh = client->json.gethandle(MegaClient::USERHANDLE);
                break;

            case MAKENAMEID4('p', 'u', 'b', 'k'):
                len_pubk = client->json.storebinary(pubkbuf, sizeof pubkbuf);
                break;

            case EOO:
                if (!ISUNDEF(uh))
                {
                    client->mapuser(uh, u->email.c_str());
                }

                if (len_pubk && !u->pubk.setkey(AsymmCipher::PUBKEY, pubkbuf, len_pubk))
                {
                    len_pubk = 0;
                }

                if (0)
                {
                    default:
                        if (client->json.storeobject())
                        {
                            continue;
                        }
                        len_pubk = 0;
                }

                callback(uh, pubkbuf, len_pubk);
                return;
        }
    }
}

CommandGetUserData::CommandGetUserData(MegaClient *client)
{
    cmd("ug");

    tag = client->reqtag;
}

void CommandGetUserData::procresult()
{
    string name;
    string pubk;
    string privk;
    handle jid = UNDEF;
    byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
    int len_privk = 0;

    if (client->json.isnumeric())
    {
        return client->app->userdata_result(NULL, NULL, NULL, jid, (error)client->json.getint());
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
        case MAKENAMEID4('n', 'a', 'm', 'e'):
            client->json.storeobject(&name);
            break;

        case 'u':
            jid = client->json.gethandle(MegaClient::USERHANDLE);
            break;

        case MAKENAMEID4('p', 'u', 'b', 'k'):
            client->json.storeobject(&pubk);
            break;

        case MAKENAMEID5('p', 'r', 'i', 'v', 'k'):
            len_privk = client->json.storebinary(privkbuf, sizeof privkbuf);
            client->key.ecb_decrypt(privkbuf, len_privk);
            privk.resize(AsymmCipher::MAXKEYLENGTH * 2);
            privk.resize(Base64::btoa(privkbuf, len_privk, (char *)privk.data()));
            break;

        case EOO:
        {
            client->app->userdata_result(&name, &pubk, &privk, jid, API_OK);
            return;
        }
        default:
            if (!client->json.storeobject())
            {
                return client->app->userdata_result(NULL, NULL, NULL, jid, API_EINTERNAL);
            }
        }
    }
}

CommandGetUserQuota::CommandGetUserQuota(MegaClient* client, AccountDetails* ad, bool storage, bool transfer, bool pro)
{
    details = ad;

    cmd("uq");
    if (storage)
    {
        arg("strg", "1", 0);
    }
    if (transfer)
    {
        arg("xfer", "1", 0);
    }
    if (pro)
    {
        arg("pro", "1", 0);
    }

    tag = client->reqtag;
}

void CommandGetUserQuota::procresult()
{
    short td;
    bool got_storage = false;
    bool got_transfer = false;
    bool got_pro = false;

    if (client->json.isnumeric())
    {
        return client->app->account_details(details, (error)client->json.getint());
    }

    details->pro_level = 0;
    details->subscription_type = 0;

    details->pro_until = 0;

    details->storage_used = 0;
    details->storage_max = 0;
    details->transfer_own_used = 0;
    details->transfer_srv_used = 0;
    details->transfer_max = 0;
    details->transfer_own_reserved = 0;
    details->transfer_srv_reserved = 0;
    details->srv_ratio = 0;

    details->transfer_hist_starttime = 0;
    details->transfer_hist_interval = 3600;
    details->transfer_hist.clear();

    details->transfer_reserved = 0;

    details->transfer_limit = 0;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case MAKENAMEID2('b', 't'):                  // age of transfer
                                                         // window start
                td = (short)client->json.getint();
                if (td != -1)
                {
                    details->transfer_hist_starttime = time(NULL) - (unsigned short)td;
                }
                break;

            case MAKENAMEID3('b', 't', 'i'):
                details->transfer_hist_interval = client->json.getint();
                break;

            case MAKENAMEID3('t', 'a', 'h'):
                if (client->json.enterarray())
                {
                    m_off_t t;

                    while ((t = client->json.getint()) >= 0)
                    {
                        details->transfer_hist.push_back(t);
                    }

                    client->json.leavearray();
                }
                break;

            case MAKENAMEID3('t', 'a', 'r'):
                details->transfer_reserved = client->json.getint();
                break;

            case MAKENAMEID3('t', 'a', 'l'):
                details->transfer_limit = client->json.getint();
                got_transfer = true;
                break;

            case MAKENAMEID3('t', 'u', 'a'):
                details->transfer_own_used += client->json.getint();
                break;

            case MAKENAMEID3('t', 'u', 'o'):
                details->transfer_srv_used += client->json.getint();
                break;

            case MAKENAMEID3('r', 'u', 'a'):
                details->transfer_own_reserved += client->json.getint();
                break;

            case MAKENAMEID3('r', 'u', 'o'):
                details->transfer_srv_reserved += client->json.getint();
                break;

            case MAKENAMEID5('c', 's', 't', 'r', 'g'):
                // storage used
                details->storage_used = client->json.getint();
                break;

            case MAKENAMEID6('c', 's', 't', 'r', 'g', 'n'):
                if (client->json.enterobject())
                {
                    handle h;
                    NodeStorage* ns;

                    while (!ISUNDEF(h = client->json.gethandle()) && client->json.enterarray())
                    {
                        ns = &details->storage[h];

                        ns->bytes = client->json.getint();
                        ns->files = client->json.getint();
                        ns->folders = client->json.getint();

                        client->json.leavearray();
                    }

                    client->json.leaveobject();
                }
                break;

            case MAKENAMEID5('m', 's', 't', 'r', 'g'):
                // total storage quota
                details->storage_max = client->json.getint();
                got_storage = true;
                break;

            case MAKENAMEID6('c', 'a', 'x', 'f', 'e', 'r'):
                // own transfer quota used
                details->transfer_own_used += client->json.getint();
                break;

            case MAKENAMEID6('c', 's', 'x', 'f', 'e', 'r'):
                // third-party transfer quota used
                details->transfer_srv_used += client->json.getint();
                break;

            case MAKENAMEID5('m', 'x', 'f', 'e', 'r'):
                // total transfer quota
                details->transfer_max = client->json.getint();
                got_transfer = true;
                break;

            case MAKENAMEID8('s', 'r', 'v', 'r', 'a', 't', 'i', 'o'):
                // percentage of transfer quota allocated to serving
                details->srv_ratio = client->json.getfloat();
                break;

            case MAKENAMEID5('u', 't', 'y', 'p', 'e'):
                // Pro plan (0 == none)
                details->pro_level = (int)client->json.getint();
                got_pro = 1;
                break;

            case MAKENAMEID5('s', 't', 'y', 'p', 'e'):
                // subscription type
                const char* ptr;
                if ((ptr = client->json.getvalue()))
                {
                    details->subscription_type = *ptr;
                }
                break;

            case MAKENAMEID6('s', 'u', 'n', 't', 'i', 'l'):
                // expiry of last active Pro plan (may be different from current one)
                details->pro_until = client->json.getint();
                break;

            case MAKENAMEID7('b', 'a', 'l', 'a', 'n', 'c', 'e'):
                // account balances
                if (client->json.enterarray())
                {
                    const char* cur;
                    const char* amount;

                    while (client->json.enterarray())
                    {
                        if ((amount = client->json.getvalue()) && (cur = client->json.getvalue()))
                        {
                            int t = details->balances.size();
                            details->balances.resize(t + 1);
                            details->balances[t].amount = atof(amount);
                            memcpy(details->balances[t].currency, cur, 3);
                            details->balances[t].currency[3] = 0;
                        }

                        client->json.leavearray();
                    }

                    client->json.leavearray();
                }
                break;

            case EOO:
                client->app->account_details(details, got_storage, got_transfer, got_pro, false, false, false);
                return;

            default:
                if (!client->json.storeobject())
                {
                    return client->app->account_details(details, API_EINTERNAL);
                }
        }
    }
}

CommandGetUserTransactions::CommandGetUserTransactions(MegaClient* client, AccountDetails* ad)
{
    cmd("utt");

    details = ad;
    tag = client->reqtag;
}

void CommandGetUserTransactions::procresult()
{
    details->transactions.clear();

    while (client->json.enterarray())
    {
        const char* handle = client->json.getvalue();
        time_t ts = client->json.getint();
        const char* delta = client->json.getvalue();
        const char* cur = client->json.getvalue();

        if (handle && (ts > 0) && delta && cur)
        {
            int t = details->transactions.size();
            details->transactions.resize(t + 1);
            memcpy(details->transactions[t].handle, handle, 11);
            details->transactions[t].handle[11] = 0;
            details->transactions[t].timestamp = ts;
            details->transactions[t].delta = atof(delta);
            memcpy(details->transactions[t].currency, cur, 3);
            details->transactions[t].currency[3] = 0;
        }

        client->json.leavearray();
    }

    client->app->account_details(details, false, false, false, false, true, false);
}

CommandGetUserPurchases::CommandGetUserPurchases(MegaClient* client, AccountDetails* ad)
{
    cmd("utp");

    details = ad;
    tag = client->reqtag;
}

void CommandGetUserPurchases::procresult()
{
    client->restag = tag;

    details->purchases.clear();

    while (client->json.enterarray())
    {
        const char* handle = client->json.getvalue();
        const time_t ts = client->json.getint();
        const char* amount = client->json.getvalue();
        const char* cur = client->json.getvalue();
        int method = (int)client->json.getint();

        if (handle && (ts > 0) && amount && cur && (method >= 0))
        {
            int t = details->purchases.size();
            details->purchases.resize(t + 1);
            memcpy(details->purchases[t].handle, handle, 11);
            details->purchases[t].handle[11] = 0;
            details->purchases[t].timestamp = ts;
            details->purchases[t].amount = atof(amount);
            memcpy(details->purchases[t].currency, cur, 3);
            details->purchases[t].currency[3] = 0;
            details->purchases[t].method = method;
        }

        client->json.leavearray();
    }

    client->app->account_details(details, false, false, false, true, false, false);
}

CommandGetUserSessions::CommandGetUserSessions(MegaClient* client, AccountDetails* ad)
{
    cmd("usl");
    arg("x", 1); // Request the additional id and alive information

    details = ad;
    tag = client->reqtag;
}

void CommandGetUserSessions::procresult()
{
    details->sessions.clear();

    while (client->json.enterarray())
    {
        int t = details->sessions.size();
        details->sessions.resize(t + 1);

        details->sessions[t].timestamp = client->json.getint();
        details->sessions[t].mru = client->json.getint();
        client->json.storeobject(&details->sessions[t].useragent);
        client->json.storeobject(&details->sessions[t].ip);

        const char* country = client->json.getvalue();
        memcpy(details->sessions[t].country, country ? country : "\0\0", 2);
        details->sessions[t].country[2] = 0;

        details->sessions[t].current = (int)client->json.getint();

        details->sessions[t].id = client->json.gethandle(8);
        details->sessions[t].alive = (int)client->json.getint();

        client->json.leavearray();
    }

    client->app->account_details(details, false, false, false, false, false, true);
}

CommandSetPH::CommandSetPH(MegaClient* client, Node* n, int del)
{
    cmd("l");
    arg("n", (byte*)&n->nodehandle, MegaClient::NODEHANDLE);

    if (del)
    {
        arg("d", 1);
    }

    h = n->nodehandle;
    tag = client->reqtag;
}

void CommandSetPH::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->exportnode_result((error)client->json.getint());
    }

    handle ph = client->json.gethandle();

    if (ISUNDEF(ph))
    {
        return client->app->exportnode_result(API_EINTERNAL);
    }

    client->app->exportnode_result(h, ph);
}

CommandGetPH::CommandGetPH(MegaClient* client, handle cph, const byte* ckey, int cop)
{
    cmd("g");
    arg("p", (byte*)&cph, MegaClient::NODEHANDLE);

    ph = cph;
    memcpy(key, ckey, sizeof key);
    tag = client->reqtag;
    op = cop;
}

void CommandGetPH::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->openfilelink_result((error)client->json.getint());
    }

    m_off_t s = -1;
    string a, fa;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 's':
                s = client->json.getint();
                break;

            case MAKENAMEID2('a', 't'):
                client->json.storeobject(&a);
                break;

            case MAKENAMEID2('f', 'a'):
                client->json.storeobject(&fa);
                break;

            case EOO:
                // we want at least the attributes
                if (s >= 0)
                {
                    a.resize(Base64::atob(a.c_str(), (byte*)a.data(), a.size()));
                    client->app->openfilelink_result(ph, key, s, &a, &fa, op);
                }
                else
                {
                    client->app->openfilelink_result(API_EINTERNAL);
                }
                return;

            default:
                if (!client->json.storeobject())
                {
                    client->app->openfilelink_result(API_EINTERNAL);
                }
        }
    }
}

CommandSetMasterKey::CommandSetMasterKey(MegaClient* client, const byte* oldkey, const byte* newkey, uint64_t hash)
{
    cmd("up");
    arg("currk", oldkey, SymmCipher::KEYLENGTH);
    arg("k", newkey, SymmCipher::KEYLENGTH);
    arg("uh", (byte*)&hash, sizeof hash);

    tag = client->reqtag;
}

void CommandSetMasterKey::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->changepw_result((error)client->json.getint());
    }
    else
    {
        client->app->changepw_result(API_OK);
    }
}

CommandCreateEphemeralSession::CommandCreateEphemeralSession(MegaClient* client,
                                                             const byte* key,
                                                             const byte* cpw,
                                                             const byte* ssc)
{
    memcpy(pw, cpw, sizeof pw);

    cmd("up");
    arg("k", key, SymmCipher::KEYLENGTH);
    arg("ts", ssc, 2 * SymmCipher::KEYLENGTH);

    tag = client->reqtag;
}

void CommandCreateEphemeralSession::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->ephemeral_result((error)client->json.getint());
    }
    else
    {
        client->resumeephemeral(client->json.gethandle(MegaClient::USERHANDLE), pw, tag);
    }
}

CommandResumeEphemeralSession::CommandResumeEphemeralSession(MegaClient* client, handle cuh, const byte* cpw, int ctag)
{
    memcpy(pw, cpw, sizeof pw);

    uh = cuh;

    cmd("us");
    arg("user", (byte*)&uh, MegaClient::USERHANDLE);

    tag = ctag;
}

void CommandResumeEphemeralSession::procresult()
{
    byte keybuf[SymmCipher::KEYLENGTH];
    byte sidbuf[MegaClient::SIDLEN];
    int havek = 0, havecsid = 0;

    if (client->json.isnumeric())
    {
        return client->app->ephemeral_result((error)client->json.getint());
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'k':
                havek = client->json.storebinary(keybuf, sizeof keybuf) == sizeof keybuf;
                break;

            case MAKENAMEID4('t', 's', 'i', 'd'):
                havecsid = client->json.storebinary(sidbuf, sizeof sidbuf) == sizeof sidbuf;
                break;

            case EOO:
                if (!havek || !havecsid)
                {
                    return client->app->ephemeral_result(API_EINTERNAL);
                }

                client->setsid(sidbuf, sizeof sidbuf);

                client->key.setkey(pw);
                client->key.ecb_decrypt(keybuf);

                client->key.setkey(keybuf);

                if (!client->checktsid(sidbuf, sizeof sidbuf))
                {
                    return client->app->ephemeral_result(API_EKEY);
                }

                client->me = uh;

                return client->app->ephemeral_result(uh, pw);

            default:
                if (!client->json.storeobject())
                {
                    return client->app->ephemeral_result(API_EINTERNAL);
                }
        }
    }
}

CommandSendSignupLink::CommandSendSignupLink(MegaClient* client, const char* email, const char* name, byte* c)
{
    cmd("uc");
    arg("c", c, 2 * SymmCipher::KEYLENGTH);
    arg("n", (byte*)name, strlen(name));
    arg("m", (byte*)email, strlen(email));

    tag = client->reqtag;
}

void CommandSendSignupLink::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->sendsignuplink_result((error)client->json.getint());
    }

    client->json.storeobject();

    client->app->sendsignuplink_result(API_EINTERNAL);
}

CommandQuerySignupLink::CommandQuerySignupLink(MegaClient* client, const byte* code, unsigned len)
{
    confirmcode.assign((char*)code, len);

    cmd("ud");
    arg("c", code, len);

    tag = client->reqtag;
}

void CommandQuerySignupLink::procresult()
{
    string name;
    string email;
    handle uh;
    const char* kc;
    const char* pwcheck;
    string namebuf, emailbuf;
    byte pwcheckbuf[SymmCipher::KEYLENGTH];
    byte kcbuf[SymmCipher::KEYLENGTH];

    if (client->json.isnumeric())
    {
        return client->app->querysignuplink_result((error)client->json.getint());
    }

    if (client->json.storebinary(&name) && client->json.storebinary(&email)
        && (uh = client->json.gethandle(MegaClient::USERHANDLE))
        && (kc = client->json.getvalue()) && (pwcheck = client->json.getvalue()))
    {
        if (!ISUNDEF(uh)
            && (Base64::atob(pwcheck, pwcheckbuf, sizeof pwcheckbuf) == sizeof pwcheckbuf)
            && (Base64::atob(kc, kcbuf, sizeof kcbuf) == sizeof kcbuf))
        {
            client->json.leavearray();

            return client->app->querysignuplink_result(uh, name.c_str(),
                                                       email.c_str(),
                                                       pwcheckbuf, kcbuf,
                                                       (const byte*)confirmcode.data(),
                                                       confirmcode.size());
        }
    }

    client->app->querysignuplink_result(API_EINTERNAL);
}

CommandConfirmSignupLink::CommandConfirmSignupLink(MegaClient* client,
                                                   const byte* code,
                                                   unsigned len,
                                                   uint64_t emailhash)
{
    cmd("up");
    arg("c", code, len);
    arg("uh", (byte*)&emailhash, sizeof emailhash);

    tag = client->reqtag;
}

void CommandConfirmSignupLink::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->confirmsignuplink_result((error)client->json.getint());
    }

    client->json.storeobject();

    client->app->confirmsignuplink_result(API_OK);
}

CommandSetKeyPair::CommandSetKeyPair(MegaClient* client, const byte* privk,
                                     unsigned privklen, const byte* pubk,
                                     unsigned pubklen,
                                     CreateKeypairCallback callback)
{
    cmd("up");
    arg("privk", privk, privklen);
    arg("pubk", pubk, pubklen);
    this->callback = callback;
    tag = client->reqtag;
}

void CommandSetKeyPair::procresult()
{
    if (client->json.isnumeric())
    {
        return callback((error)client->json.getint());
    }

    client->json.storeobject();

    callback(API_OK);
}

// fetch full node tree
CommandFetchNodes::CommandFetchNodes(MegaClient* client)
{
    cmd("f");
    arg("c", "1", 0);
    arg("r", "1", 0);

    tag = client->reqtag;
}

// purge and rebuild node/user tree
void CommandFetchNodes::procresult()
{
    client->purgenodesusersabortsc();
    client->fetchingnodes = false;

    if (client->json.isnumeric())
    {
        return client->app->fetchnodes_result((error)client->json.getint());
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'f':
                // nodes
                if (!client->readnodes(&client->json, 0))
                {
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }
                break;

            case MAKENAMEID2('o', 'k'):
                // outgoing sharekeys
                client->readok(&client->json);
                break;

            case 's':
                // outgoing shares
                client->readoutshares(&client->json);
                break;

            case 'u':
                // users/contacts
                if (!client->readusers(&client->json))
                {
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }
                break;

            case MAKENAMEID2('c', 'r'):
                // crypto key request
                client->proccr(&client->json);
                break;

            case MAKENAMEID2('s', 'r'):
                // sharekey distribution request
                client->procsr(&client->json);
                break;

            case MAKENAMEID2('s', 'n'):
                // share node
                if (!client->setscsn(&client->json))
                {
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }
                break;

            case EOO:
                if (!*client->scsn)
                {
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }

                client->mergenewshares(0);
                client->applykeys();
#ifdef ENABLE_SYNC
                client->syncsup = false;
#endif
//                {
//                    MegaClient *c = client;
//                    int reqtag = tag;
//                    client->getownsigningkeys([c, reqtag](ValueMap map, error e){
//                        if(e == API_OK)
//                        {
//                            c->fetchKeyrings([c, reqtag](error e){
//                                c->restag = reqtag;
//                                c->app->fetchnodes_result(e);
//                            });
//                        }
//                        else
//                        {
//                            c->restag = reqtag;
//                            c->app->fetchnodes_result(e);
//                        }
//
//                    });
//                }
                client->app->fetchnodes_result(API_OK);
                //client->app->fetchnodes_result(API_OK);
                client->initsc();

                // NULL vector: "notify all nodes"
                client->app->nodes_updated(NULL, client->nodes.size());
                for (node_map::iterator it = client->nodes.begin(); it != client->nodes.end(); it++)
                {
                    memset(&(it->second->changed), 0, sizeof it->second->changed);
                }
                return;

            default:
                if (!client->json.storeobject())
                {
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }
        }
    }
}

// report event to server logging facility
CommandReportEvent::CommandReportEvent(MegaClient *client, const char *event, const char *details)
{
    cmd("cds");
    arg("c", event);

    if (details)
    {
        arg("v", details);
    }

    tag = client->reqtag;
}

void CommandReportEvent::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->reportevent_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->reportevent_result(API_EINTERNAL);
    }
}

// load balancing request
CommandLoadBalancing::CommandLoadBalancing(MegaClient *client, const char *service)
{
    this->client = client;
    this->service = service;

    tag = client->reqtag;
}

void CommandLoadBalancing::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->loadbalancing_result(NULL, (error)client->json.getint());
    }
    else
    {
        error e = API_EINTERNAL;
        if(!client->json.enterobject())
        {
            client->app->loadbalancing_result(NULL, API_EINTERNAL);
            return;
        }

        string servers;
        for (;;)
        {
            switch (client->json.getnameid())
            {
                case MAKENAMEID2('o', 'k'):
                    if(client->json.isnumeric() && client->json.getint())
                    {
                        e = API_OK;
                    }
                    break;

                case 'e':
                    if(client->json.isnumeric())
                    {
                        e = (error)client->json.getint();
                    }
                    break;

                case EOO:
                    client->app->loadbalancing_result(e ? NULL : &servers, e);
                    return;

                default:
                    if (!client->json.enterarray())
                    {
                        client->app->loadbalancing_result(NULL, API_EINTERNAL);
                        return;
                    }

                    while(client->json.enterobject())
                    {
                        if(servers.size())
                        {
                            servers.append(";");
                        }

                        while (client->json.getnameid() != EOO)
                        {
                            string data;
                            if (!client->json.storeobject(&data))
                            {
                                client->app->loadbalancing_result(NULL, API_EINTERNAL);
                                return;
                            }
                            if(servers.size())
                            {
                                servers.append(":");
                            }
                            servers.append(data);
                        }
                    }
                    client->json.leavearray();
                    break;
            }
        }
    }
}

CommandSubmitPurchaseReceipt::CommandSubmitPurchaseReceipt(MegaClient *client, int type, const char *receipt)
{
    cmd("vpay");
    arg("t", type);

    if(receipt)
    {
        arg("receipt", (const byte*)receipt, strlen(receipt));
    }

    tag = client->reqtag;
}

void CommandSubmitPurchaseReceipt::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->submitpurchasereceipt_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->submitpurchasereceipt_result(API_EINTERNAL);
    }
}

} // namespace
