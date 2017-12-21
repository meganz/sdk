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
HttpReqCommandPutFA::HttpReqCommandPutFA(MegaClient* client, handle cth, fatype ctype, string* cdata, bool checkAccess)
{
    cmd("ufa");
    arg("s", cdata->size());

    if (checkAccess)
    {
        arg("h", (byte*)&cth, MegaClient::NODEHANDLE);
    }

    progressreported = 0;
    persistent = true;  // object will be recycled either for retry or for
                        // posting to the file attribute server

    if (client->usehttps)
    {
        arg("ssl", 2);
    }

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
    error e;
    client->looprequested = true;

    if (client->json.isnumeric())
    {
        e = (error)client->json.getint();

        if (e == API_EAGAIN || e == API_ERATELIMIT)
        {
            status = REQ_FAILURE;
        }
        else
        {
            if (e == API_EACCESS)
            {
                // create a custom attribute indicating thumbnail can't be restored from this account
                Node *n = client->nodebyhandle(th);

                char me64[12];
                Base64::btoa((const byte*)&client->me, MegaClient::USERHANDLE, me64);

                if (n && client->checkaccess(n, FULL) &&
                        (n->attrs.map.find('f') == n->attrs.map.end() || n->attrs.map['f'] != me64) )
                {
                    LOG_debug << "Restoration of file attributes is not allowed for current user (" << me64 << ").";
                    n->attrs.map['f'] = me64;

                    int creqtag = client->reqtag;
                    client->reqtag = 0;
                    client->setattr(n);
                    client->reqtag = creqtag;
                }
            }

            status = REQ_SUCCESS;
            return client->app->putfa_result(th, type, e);
        }
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
                        LOG_debug << "Sending file attribute data";
                        Node::copystring(&posturl, p);
                        progressreported = 0;
                        HttpReq::type = REQ_BINARY;
                        post(client, data->data(), data->size());
                    }
                    return;

                default:
                    if (!client->json.storeobject())
                    {
                        status = REQ_SUCCESS;
                        return client->app->putfa_result(th, type, API_EINTERNAL);
                    }
            }
        }
    }
}

m_off_t HttpReqCommandPutFA::transferred(MegaClient *client)
{
    if (httpiohandle)
    {
        return client->httpio->postpos(httpiohandle);
    }

    return 0;
}

CommandGetFA::CommandGetFA(MegaClient *client, int p, handle fahref)
{
    part = p;

    cmd("ufa");
    arg("fah", (byte*)&fahref, sizeof fahref);

    if (client->usehttps)
    {
        arg("ssl", 2);
    }

    arg("r", 1);
}

void CommandGetFA::procresult()
{
    fafc_map::iterator it = client->fafcs.find(part);
    client->looprequested = true;

    if (client->json.isnumeric())
    {
        if (it != client->fafcs.end())
        {            
            faf_map::iterator fafsit;
            for (fafsit = it->second->fafs[0].begin(); fafsit != it->second->fafs[0].end(); )
            {
                // move from fresh to pending
                it->second->fafs[1][fafsit->first] = fafsit->second;
                it->second->fafs[0].erase(fafsit++);
            }

            it->second->e = (error)client->json.getint();
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
                        faf_map::iterator fafsit;
                        for (fafsit = it->second->fafs[0].begin(); fafsit != it->second->fafs[0].end(); )
                        {
                            // move from fresh to pending
                            it->second->fafs[1][fafsit->first] = fafsit->second;
                            it->second->fafs[0].erase(fafsit++);
                        }

                        it->second->e = API_EINTERNAL;
                        it->second->req.status = REQ_FAILURE;
                    }
                }

                return;

            default:
                if (!client->json.storeobject())
                {
                    faf_map::iterator fafsit;
                    for (fafsit = it->second->fafs[0].begin(); fafsit != it->second->fafs[0].end(); )
                    {
                        // move from fresh to pending
                        it->second->fafs[1][fafsit->first] = fafsit->second;
                        it->second->fafs[0].erase(fafsit++);
                    }

                    it->second->e = API_EINTERNAL;
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
CommandPutFile::CommandPutFile(MegaClient* client, TransferSlot* ctslot, int ms)
{
    tslot = ctslot;

    cmd("u");

    if (client->usehttps)
    {
        arg("ssl", 2);
    }

    arg("v", 2);
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

    std::vector<std::string> tempurls;
    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'p':
                tempurls.push_back("");
                client->json.storeobject(canceled ? NULL : &tempurls.back());
                break;

            case EOO:
                if (canceled) return;

                if (tempurls.size() == 1)
                {
                    tslot->transferbuf.setIsRaid(false, tslot->transfer, tempurls, tslot->transfer->pos, tslot->maxDownloadRequestSize);
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
CommandDirectRead::CommandDirectRead(MegaClient *client, DirectReadNode* cdrn)
{
    drn = cdrn;

    cmd("g");
    arg(drn->p ? "n" : "p", (byte*)&drn->h, MegaClient::NODEHANDLE);
    arg("g", 1);

    if (client->usehttps)
    {
        arg("ssl", 2);
    }
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
        dstime tl = 0;

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

                case MAKENAMEID2('t', 'l'):
                    tl = client->json.getint();
                    break;

                case EOO:
                    if (!canceled && drn)
                    {
                        if (e == API_EOVERQUOTA && !tl)
                        {
                            // default retry interval
                            tl = MegaClient::DEFAULT_BW_OVERQUOTA_BACKOFF_SECS;
                        }

                        drn->cmdresult(e, e == API_EOVERQUOTA ? tl * 10 : 0);
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
CommandGetFile::CommandGetFile(MegaClient *client, TransferSlot* ctslot, byte* key, handle h, bool p, const char *privateauth, const char *publicauth)
{
    cmd("g");
    arg(p ? "n" : "p", (byte*)&h, MegaClient::NODEHANDLE);
    arg("g", 1);
    arg("v", 2);  // version 2: server can supply details for cloudraid files

    if (client->usehttps)
    {
        arg("ssl", 2);
    }

    if (privateauth)
    {
        arg("esid", privateauth);
    }

    if (publicauth)
    {
        arg("en", publicauth);
    }

    tslot = ctslot;
    priv = p;
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
    dstime tl = 0;
    int d = 0;
    byte* buf;
    time_t ts = 0, tm = 0;

    // credentials relevant to a non-TransferSlot scenario (node query)
    string fileattrstring;
    string filenamestring;
    string filefingerprint;
    std::vector<string> tempurls;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'g':
                if (client->json.enterarray())   // now that we are requesting v2, the reply will be an array of 6 URLs for a raid download, or a single URL for the original direct download
                {
                    for (;;) {
                        std::string tu;
                        if (!client->json.storeobject(&tu))
                        {
                            break;
                        }
                        tempurls.push_back(tu);
                    }
                    client->json.leavearray();
                }
                else
                {
                    std::string tu;
                    if (client->json.storeobject(&tu))
                    {
                        tempurls.push_back(tu);
                    }
                }
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

            case MAKENAMEID2('t', 'l'):
                tl = client->json.getint();
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

                    if ((buf = Node::decryptattr(tslot ? tslot->transfer->transfercipher() : &key,
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
                                        if (s >= 0 && s != tslot->transfer->size)
                                        {
                                            tslot->transfer->size = s;
                                            for (file_list::iterator it = tslot->transfer->files.begin(); it != tslot->transfer->files.end(); it++)
                                            {
                                                (*it)->size = s;
                                            }

                                            if (priv)
                                            {
                                                Node *n = client->nodebyhandle(ph);
                                                if (n)
                                                {
                                                    n->size = s;
                                                    client->notifynode(n);
                                                }
                                            }

                                            int creqtag = client->reqtag;
                                            client->reqtag = 0;
                                            client->sendevent(99411, "Node size mismatch");
                                            client->reqtag = creqtag;
                                        }

                                        tslot->starttime = tslot->lastdata = client->waiter->ds;

                                        if (tempurls.size() == 1 && s >= 0)
                                        {
                                            tslot->transferbuf.setIsRaid(false, tslot->transfer, tempurls, tslot->transfer->pos, tslot->maxDownloadRequestSize);
                                            return tslot->progress();
                                        }
                                        else if (tempurls.size() == RAIDPARTS && s >= 0)
                                        {
                                            tslot->transferbuf.setIsRaid(true, tslot->transfer, tempurls, tslot->transfer->pos, tslot->maxDownloadRequestSize);  // starting raid download
                                            return tslot->progress();   
                                        }

                                        if (e == API_EOVERQUOTA && !tl)
                                        {
                                            // default retry interval
                                            tl = MegaClient::DEFAULT_BW_OVERQUOTA_BACKOFF_SECS;
                                        }

                                        return tslot->transfer->failed(e, e == API_EOVERQUOTA ? tl * 10 : 0);
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

        if (nn[i].type == FILENODE && !ISUNDEF(nn[i].ovhandle))
        {
            arg("ov", (byte*)&nn[i].ovhandle, MegaClient::NODEHANDLE);
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
                    case NEW_PUBLIC:
                    case NEW_NODE:
                        snk.add((NodeCore*)(nn + i), tn, 0);
                        break;

                    case NEW_UPLOAD:
                        snk.add((NodeCore*)(nn + i), tn, 0, nn[i].uploadtoken, (int)sizeof nn->uploadtoken);
                        break;
                }
            }

            snk.get(this, true);
        }
    }

    tag = ctag;
}

// add new nodes and handle->node handle mapping
void CommandPutNodes::procresult()
{
    error e;

    pendingdbid_map::iterator it = client->pendingtcids.find(tag);
    if (it != client->pendingtcids.end())
    {
        if (client->tctable)
        {
            client->tctable->begin();
            vector<uint32_t> &ids = it->second;
            for (unsigned int i = 0; i < ids.size(); i++)
            {
                if (ids[i])
                {
                    client->tctable->del(ids[i]);
                }
            }
            client->tctable->commit();
        }
        client->pendingtcids.erase(it);
    }
    pendingfiles_map::iterator pit = client->pendingfiles.find(tag);
    if (pit != client->pendingfiles.end())
    {
        vector<string> &pfs = pit->second;
        for (unsigned int i = 0; i < pfs.size(); i++)
        {
            client->fsaccess->unlinklocal(&pfs[i]);
        }
        client->pendingfiles.erase(pit);
    }

    if (client->json.isnumeric())
    {
        e = (error)client->json.getint();
        LOG_debug << "Putnodes error " << e;

#ifdef ENABLE_SYNC
        if (source == PUTNODES_SYNC)
        {
            if (e == API_EACCESS)
            {
                int creqtag = client->reqtag;
                client->reqtag = 0;
                client->sendevent(99402, "API_EACCESS putting node in sync transfer");
                client->reqtag = creqtag;
            }

            client->app->putnodes_result(e, type, NULL);

            for (int i=0; i < nnsize; i++)
            {
                if (nn[i].localnode)
                {
                    nn[i].localnode->newnode = NULL;
                }
            }

            return client->putnodes_sync_result(e, nn, nnsize);
        }
        else
        {
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
        }
#endif
    }

    e = API_EINTERNAL;

    bool noexit = true;
    while (noexit)
    {
        switch (client->json.getnameid())
        {
            case 'f':
                if (client->readnodes(&client->json, 1, source, nn, nnsize, tag))
                {
                    e = API_OK;
                }
                else
                {
                    LOG_err << "Parse error (readnodes)";
                    e = API_EINTERNAL;
                    noexit = false;
                }
                break;

            case MAKENAMEID2('f', '2'):
                if (!client->readnodes(&client->json, 1))
                {
                    LOG_err << "Parse error (readversions)";
                    e = API_EINTERNAL;
                    noexit = false;
                }
                break;

            default:
                if (client->json.storeobject())
                {
                    continue;
                }

                e = API_EINTERNAL;
                LOG_err << "Parse error (PutNodes)";

                // fall through
            case EOO:
                noexit = false;
                break;
        }
    }

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
}

CommandMoveNode::CommandMoveNode(MegaClient* client, Node* n, Node* t, syncdel_t csyncdel, handle prevparent)
{
    h = n->nodehandle;
    syncdel = csyncdel;
    np = t->nodehandle;
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
                    Node *tn = NULL;
                    if (syncdel == SYNCDEL_BIN || syncdel == SYNCDEL_FAILED
                            || !(tn = client->nodebyhandle(client->rootnodes[RUBBISHNODE - ROOTNODE])))
                    {
                        LOG_err << "Error moving node to the Rubbish Bin";
                        syncn->syncdeleted = SYNCDEL_NONE;
                        client->todebris.erase(syncn->todebris_it);
                        syncn->todebris_it = client->todebris.end();
                    }
                    else
                    {
                        int creqtag = client->reqtag;
                        client->reqtag = syncn->tag;
                        LOG_warn << "Move to Syncdebris failed. Moving to the Rubbish Bin instead.";
                        client->rename(syncn, tn, SYNCDEL_FAILED, pp);
                        client->reqtag = creqtag;
                    }
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
        // Movement of shares and pending shares into Rubbish should remove them
        Node *n = client->nodebyhandle(h);
        if (n && (n->pendingshares || n->outshares))
        {
            Node *rootnode = client->nodebyhandle(np);
            while (rootnode)
            {
                if (!rootnode->parent)
                {
                    break;
                }
                rootnode = rootnode->parent;
            }
            if (rootnode && rootnode->type == RUBBISHNODE)
            {
                share_map::iterator it;
                if (n->pendingshares)
                {
                    for (it = n->pendingshares->begin(); it != n->pendingshares->end(); it++)
                    {
                        client->newshares.push_back(new NewShare(
                                                        n->nodehandle, 1, n->owner, ACCESS_UNKNOWN,
                                                        0, NULL, NULL, it->first, false));
                    }
                }

                if (n->outshares)
                {
                    for (it = n->outshares->begin(); it != n->outshares->end(); it++)
                    {
                        client->newshares.push_back(new NewShare(
                                                        n->nodehandle, 1, it->first, ACCESS_UNKNOWN,
                                                        0, NULL, NULL, UNDEF, false));
                    }
                }

                client->mergenewshares(1);
            }
        }

        client->app->rename_result(h, e);
    }
    else
    {
        client->json.storeobject();
        client->app->rename_result(h, API_EINTERNAL);
    }
}

CommandDelNode::CommandDelNode(MegaClient* client, handle th, bool keepversions)
{
    cmd("d");
    notself(client);

    arg("n", (byte*)&th, MegaClient::NODEHANDLE);

    if (keepversions)
    {
        arg("v", 1);
    }

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

CommandDelVersions::CommandDelVersions(MegaClient* client)
{
    cmd("dv");
    tag = client->reqtag;
}

void CommandDelVersions::procresult()
{
    error e = API_EINTERNAL;
    if (client->json.isnumeric())
    {
        e = (error)client->json.getint();
    }
    client->app->unlinkversions_result(e);
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
        client->removecaches();
        client->locallogout();
    }
    app->logout_result(e);
}

// login request with user e-mail address and user hash
CommandLogin::CommandLogin(MegaClient* client, const char* email, uint64_t emailhash, const byte *sessionkey, int csessionversion)
{
    cmd("us");

    // are we just performing a session validation?
    checksession = !email;
    sessionversion = csessionversion;

    if (!checksession)
    {
        arg("user", email);
        arg("uh", (byte*)&emailhash, sizeof emailhash);
    }
    else
    {
        if (client->sctable && client->dbaccess->currentDbVersion == DbAccess::LEGACY_DB_VERSION)
        {
            LOG_debug << "Requesting a local cache upgrade";
            arg("fa", 1);
        }
    }

    if (sessionkey)
    {
        arg("sek", sessionkey, SymmCipher::KEYLENGTH);
    }

    if (client->cachedscsn != UNDEF)
    {
        arg("sn", (byte*)&client->cachedscsn, sizeof client->cachedscsn);
    }

    string id;
    if (!MegaClient::statsid)
    {
        client->fsaccess->statsid(&id);
        if (id.size())
        {
            int len = id.size() + 1;
            char *buff = new char[len];
            memcpy(buff, id.c_str(), len);
            MegaClient::statsid = buff;
        }
    }
    else
    {
        id = MegaClient::statsid;
    }

    if (id.size())
    {
        string hash;
        HashSHA256 hasher;
        hasher.add((const byte*)id.data(), id.size());
        hasher.get(&hash);
        arg("si", (const byte*)hash.data(), hash.size());
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
    byte sek[SymmCipher::KEYLENGTH];
    int len_k = 0, len_privk = 0, len_csid = 0, len_tsid = 0, len_sek = 0;
    handle me = UNDEF;
    bool fa = false;
    bool ach = false;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'k':
                len_k = client->json.storebinary(hash, sizeof hash);
                break;

            case 'u':
                me = client->json.gethandle(MegaClient::USERHANDLE);
                break;

            case MAKENAMEID3('s', 'e', 'k'):
                len_sek = client->json.storebinary(sek, sizeof sek);
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

            case MAKENAMEID2('f', 'a'):
                fa = client->json.getint();
                break;

            case MAKENAMEID3('a', 'c', 'h'):
                ach = client->json.getint();
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
                else
                {
                    if (fa && client->sctable)
                    {
                        client->sctable->remove();
                        delete client->sctable;
                        client->sctable = NULL;
                        client->pendingsccommit = false;
                        client->cachedscsn = UNDEF;
                        client->dbaccess->currentDbVersion = DbAccess::DB_VERSION;

                        int creqtag = client->reqtag;
                        client->reqtag = 0;
                        client->sendevent(99404, "Local DB upgrade granted");
                        client->reqtag = creqtag;
                    }
                }

                if (len_sek)
                {
                    if (len_sek != SymmCipher::KEYLENGTH)
                    {
                        return client->app->login_result(API_EINTERNAL);
                    }

                    if (checksession && sessionversion)
                    {
                        byte k[SymmCipher::KEYLENGTH];
                        memcpy(k, client->key.key, sizeof(k));

                        client->key.setkey(sek);
                        client->key.ecb_decrypt(k);
                        client->key.setkey(k);
                    }
                }

                if (len_tsid)
                {
                    client->setsid(sidbuf, MegaClient::SIDLEN);

                    // account does not have an RSA keypair set: verify
                    // password using symmetric challenge
                    if (!client->checktsid(sidbuf, len_tsid))
                    {
                        LOG_warn << "Error checking tsid";
                        return client->app->login_result(API_ENOENT);
                    }

                    // add missing RSA keypair
                    LOG_info << "Generating and adding missing RSA keypair";
                    client->setkeypair();
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
                        LOG_warn << "Error checking private key";
                        return client->app->login_result(API_ENOENT);
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
                client->achievements_enabled = ach;

                if (len_sek)
                {
                    client->sessionkey.assign((const char *)sek, sizeof(sek));
                }

                return client->app->login_result(API_OK);

            default:
                if (!client->json.storeobject())
                {
                    return client->app->login_result(API_EINTERNAL);
                }
        }
    }
}

CommandShareKeyUpdate::CommandShareKeyUpdate(MegaClient*, handle sh, const char* uid, const byte* key, int len)
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
            element(client->me, MegaClient::USERHANDLE);
            element(sharekey, SymmCipher::KEYLENGTH);
        }
    }

    endarray();
}

// add/remove share; include node share keys if new share
CommandSetShare::CommandSetShare(MegaClient* client, Node* n, User* u, accesslevel_t a, int newshare, const char* msg, const char* personal_representation)
{
    byte auth[SymmCipher::BLOCKSIZE];
    byte key[SymmCipher::KEYLENGTH];
    byte asymmkey[AsymmCipher::MAXKEYLENGTH];
    int t = 0;

    tag = client->restag;

    sh = n->nodehandle;
    user = u;
    access = a;

    cmd("s2");
    arg("n", (byte*)&sh, MegaClient::NODEHANDLE);

    // Only for inviting non-contacts
    if (personal_representation && personal_representation[0])
    {
        this->personal_representation = personal_representation;
        arg("e", personal_representation);
    }

    if (msg && msg[0])
    {
        this->msg = msg;
        arg("msg", msg);
    }

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

    arg("u", u ? ((u->show == VISIBLE) ? u->uid.c_str() : u->email.c_str()) : MegaClient::EXPORTEDLINK);
    // if the email is registered, the pubk request has returned the userhandle -->
    // sending the userhandle instead of the email makes the API to assume the user is already a contact

    if (a != ACCESS_UNKNOWN)
    {
        arg("r", a);

        if (u && u->pubk.isvalid() && t)
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
                        client->reqs.add(new CommandSetShare(client, n, user, access, 0, msg.c_str(), personal_representation.c_str()));
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


CommandSetPendingContact::CommandSetPendingContact(MegaClient* client, const char* temail, opcactions_t action, const char* msg, const char* oemail)
{
    cmd("upc");

    if (oemail != NULL)
    {
        arg("e", oemail);
    }

    arg("u", temail);
    switch (action)     
    {   
        case OPCA_DELETE:
            arg("aa", "d");
            break;
        case OPCA_REMIND:
            arg("aa", "r");
            break;
        case OPCA_ADD:
            arg("aa", "a");
            break;
    }

    if (msg != NULL)
    {
        arg("msg", msg);
    }

    if (action != OPCA_REMIND)  // for reminders, need the actionpacket to update `uts`
    {
        notself(client);
    }

    tag = client->reqtag;
    this->action = action;
    this->temail = temail;
}

void CommandSetPendingContact::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();

        handle pcrhandle = UNDEF;
        if (!e) // response for delete & remind actions is always numeric
        {
            // find the PCR by email
            PendingContactRequest *pcr = NULL;
            for (handlepcr_map::iterator it = client->pcrindex.begin();
                 it != client->pcrindex.end(); it++)
            {
                if (it->second->targetemail == temail)
                {
                    pcr = it->second;
                    pcrhandle = pcr->id;
                    break;
                }
            }

            if (!pcr)
            {
                LOG_err << "Reminded/deleted PCR not found";
            }
            else if (action == OPCA_DELETE)
            {
                pcr->changed.deleted = true;
                client->notifypcr(pcr);

                // remove pending shares related to the deleted PCR
                Node *n;
                for (node_map::iterator it = client->nodes.begin(); it != client->nodes.end(); it++)
                {
                    n = it->second;
                    if (n->pendingshares && n->pendingshares->find(pcr->id) != n->pendingshares->end())
                    {
                        client->newshares.push_back(
                                    new NewShare(n->nodehandle, 1, n->owner, ACCESS_UNKNOWN,
                                                 0, NULL, NULL, pcr->id, false));
                    }
                }

                client->mergenewshares(1);
            }
        }

        return client->app->setpcr_result(pcrhandle, e, this->action);
    }

    // if the PCR has been added, the response contains full details
    handle p = UNDEF;    
    m_time_t ts = 0;
    m_time_t uts = 0;
    m_time_t rts = 0;
    m_time_t dts = 0;
    const char *e = NULL;
    const char *m = NULL;
    const char *msg = NULL;
    PendingContactRequest *pcr = NULL;
    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'p':
                p = client->json.gethandle(MegaClient::PCRHANDLE);  
                break;
            case 'm':
                m = client->json.getvalue();
                break;
            case 'e':
                e = client->json.getvalue();
                break;
            case MAKENAMEID3('m', 's', 'g'):
                msg = client->json.getvalue();
                break;
            case MAKENAMEID2('t', 's'):
                ts = client->json.getint();
                break;
            case MAKENAMEID3('u', 't', 's'):
                uts = client->json.getint();
                break;
            case MAKENAMEID3('r', 't', 's'):
                rts = client->json.getint();
                break;
            case MAKENAMEID3('d', 't', 's'):
                dts = client->json.getint();
                break;
            case EOO:
                if (ISUNDEF(p))
                {
                    LOG_err << "Error in CommandSetPendingContact. Undefined handle";
                    client->app->setpcr_result(UNDEF, API_EINTERNAL, this->action);                    
                    return;
                }

                if (action != OPCA_ADD || !e || !m || ts == 0 || uts == 0)
                {
                    LOG_err << "Error in CommandSetPendingContact. Wrong parameters";
                    client->app->setpcr_result(UNDEF, API_EINTERNAL, this->action);
                    return;
                }

                pcr = new PendingContactRequest(p, e, m, ts, uts, msg, true);
                client->mappcr(p, pcr);

                client->notifypcr(pcr);
                client->app->setpcr_result(p, API_OK, this->action);
                return;

            default:
                if (!client->json.storeobject())
                {
                    LOG_err << "Error in CommandSetPendingContact. Parse error";
                    client->app->setpcr_result(UNDEF, API_EINTERNAL, this->action);
                    return;
                }
        }
    }
}

CommandUpdatePendingContact::CommandUpdatePendingContact(MegaClient* client, handle p, ipcactions_t action)
{
    cmd("upca");   

    arg("p", (byte*)&p, MegaClient::PCRHANDLE);
    switch (action)     
    {   
        case IPCA_ACCEPT:
            arg("aa", "a");
            break;
        case IPCA_DENY:
            arg("aa", "d");
            break;
        case IPCA_IGNORE:          
        default:
            arg("aa", "i");
            break;
    }

    tag = client->reqtag;
    this->action = action;
}

void CommandUpdatePendingContact::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->updatepcr_result((error)client->json.getint(), this->action);
    }
   
    LOG_err << "Unexpected response for CommandUpdatePendingContact";
    client->app->updatepcr_result(API_EINTERNAL, this->action);
    return;    
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
                                               const char* currency, unsigned /*tax*/,
                                               const char* /*country*/, const char* affiliate)
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
        return client->app->checkout_result(NULL, (error)client->json.getint());
    }

    //Expected response: "EUR":{"res":X,"code":Y}}
    client->json.getnameid();
    if (!client->json.enterobject())
    {
        LOG_err << "Parse error (CommandPurchaseCheckout)";
        client->app->checkout_result(NULL, API_EINTERNAL);
        return;
    }

    error e = API_EINTERNAL;
    string errortype;
    for (;;)
    {
        switch (client->json.getnameid())
        {
            case MAKENAMEID3('r', 'e', 's'):
                if (client->json.isnumeric())
                {
                    e = (error)client->json.getint();
                }
                else
                {
                    client->json.storeobject(&errortype);
                    if (errortype == "S")
                    {
                        errortype.clear();
                        e = API_OK;
                    }
                }
                break;

            case MAKENAMEID4('c', 'o', 'd', 'e'):
                if (client->json.isnumeric())
                {
                    e = (error)client->json.getint();
                }
                else
                {
                    LOG_err << "Parse error in CommandPurchaseCheckout (code)";
                }
                break;
            case EOO:
                client->json.leaveobject();
                if (!errortype.size() || errortype == "FI" || e == API_OK)
                {
                    client->app->checkout_result(NULL, e);
                }
                else
                {
                    client->app->checkout_result(errortype.c_str(), e);
                }
                return;
            default:
                if (!client->json.storeobject())
                {
                    client->app->checkout_result(NULL, API_EINTERNAL);
                    return;
                }
        }
    }
}

CommandRemoveContact::CommandRemoveContact(MegaClient* client, const char* m, visibility_t show)
{
    this->email = m ? m : "";
    this->v = show;

    cmd("ur2");
    arg("u", m);
    arg("l", (int)show);

    tag = client->reqtag;
}

void CommandRemoveContact::procresult()
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

        User *u = client->finduser(email.c_str());
        if (u)
        {
            u->show = v;
        }
    }

    client->app->removecontact_result(e);
}


CommandPutMultipleUAVer::CommandPutMultipleUAVer(MegaClient *client, const userattr_map *attrs, int ctag)
{
    this->attrs = *attrs;

    cmd("upv");

    for (userattr_map::const_iterator it = attrs->begin(); it != attrs->end(); it++)
    {
        attr_t type = it->first;

        beginarray(User::attr2string(type).c_str());

        element((const byte *) it->second.data(), it->second.size());

        const string *attrv = client->ownuser()->getattrversion(type);
        if (attrv)
        {
            element(attrv->c_str());
        }

        endarray();
    }

    notself(client);

    tag = ctag;
}

void CommandPutMultipleUAVer::procresult()
{
    if (client->json.isnumeric())
    {
        int creqtag = client->reqtag;
        client->reqtag = 0;
        client->sendevent(99419, "Error attaching keys");
        client->reqtag = creqtag;

        return client->app->putua_result((error)client->json.getint());
    }

    User *u = client->ownuser();
    for(;;)   // while there are more attrs to read...
    {
        const char* ptr;
        const char* end;

        if (!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            break;
        }
        attr_t type = User::string2attr(string(ptr, (end-ptr)).c_str());

        if (!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            return client->app->putua_result(API_EINTERNAL);
        }
        string version = string(ptr, (end-ptr));

        userattr_map::iterator it = this->attrs.find(type);
        if (type == ATTR_UNKNOWN || version.empty() || (it == this->attrs.end()))
        {
            LOG_err << "Error in CommandPutUA. Undefined attribute or version";
            return client->app->putua_result(API_EINTERNAL);
        }
        else
        {
            u->setattr(type, &it->second, &version);
            u->setTag(tag ? tag : -1);

#ifdef ENABLE_CHAT
            if (type == ATTR_KEYRING)
            {
                TLVstore *tlvRecords = TLVstore::containerToTLVrecords(&attrs[type], &client->key);
                if (tlvRecords)
                {
                    if (tlvRecords->find(EdDSA::TLV_KEY))
                    {
                        string prEd255 = tlvRecords->get(EdDSA::TLV_KEY);
                        if (prEd255.size() == EdDSA::SEED_KEY_LENGTH)
                        {
                            client->signkey = new EdDSA((unsigned char *) prEd255.data());
                        }
                    }

                    if (tlvRecords->find(ECDH::TLV_KEY))
                    {
                        string prCu255 = tlvRecords->get(ECDH::TLV_KEY);
                        if (prCu255.size() == ECDH::PRIVATE_KEY_LENGTH)
                        {
                            client->chatkey = new ECDH((unsigned char *) prCu255.data());
                        }
                    }

                    if (!client->chatkey || !client->chatkey->initializationOK ||
                            !client->signkey || !client->signkey->initializationOK)
                    {
                        client->resetKeyring();

                        int creqtag = client->reqtag;
                        client->reqtag = 0;
                        client->sendevent(99418, "Failed to load attached keys");
                        client->reqtag = creqtag;
                    }
                    else
                    {
                        int creqtag = client->reqtag;
                        client->reqtag = 0;
                        client->sendevent(99420, "Signing and chat keys attached OK");
                        client->reqtag = creqtag;
                    }

                    delete tlvRecords;
                }
                else
                {
                    LOG_warn << "Failed to decrypt keyring after putua";
                }
            }
#endif
        }
    }

    client->notifyuser(u);
    client->app->putua_result(API_OK);
}

CommandPutUAVer::CommandPutUAVer(MegaClient* client, attr_t at, const byte* av, unsigned avl, int ctag)
{
    this->at = at;
    this->av.assign((const char*)av, avl);

    cmd("upv");

    beginarray(User::attr2string(at).c_str());

    // if removing avatar, do not Base64 encode the attribute value
    if (at == ATTR_AVATAR && !strcmp((const char *)av, "none"))
    {
        element((const char*)av);
    }
    else
    {
        element(av, avl);
    }

    const string *attrv = client->ownuser()->getattrversion(at);
    if (attrv)
    {
        element(attrv->c_str());
    }

    endarray();

    notself(client);

    tag = ctag;
}

void CommandPutUAVer::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->putua_result((error)client->json.getint());
    }
    else
    {
        const char* ptr;
        const char* end;

        if (!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            client->app->putua_result(API_EINTERNAL);
            return;
        }
        attr_t at = User::string2attr(string(ptr, (end-ptr)).c_str());

        if (!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            client->app->putua_result(API_EINTERNAL);
            return;
        }
        string v = string(ptr, (end-ptr));

        if (at == ATTR_UNKNOWN || v.empty() || (this->at != at))
        {
            LOG_err << "Error in CommandPutUA. Undefined attribute or version";
            client->app->putua_result(API_EINTERNAL);
        }
        else
        {
            User *u = client->ownuser();
            u->setattr(at, &av, &v);
            u->setTag(tag ? tag : -1);

            client->notifyuser(u);
            client->app->putua_result(API_OK);
        }
    }
}

CommandPutUA::CommandPutUA(MegaClient* client, attr_t at, const byte* av, unsigned avl)
{
    this->at = at;
    this->av.assign((const char*)av, avl);

    cmd("up");

    string an = User::attr2string(at);

    // if removing avatar, do not Base64 encode the attribute value
    if (at == ATTR_AVATAR && !strcmp((const char *)av, "none"))
    {
        arg(an.c_str(),(const char *)av, avl);
    }
    else
    {
        arg(an.c_str(), av, avl);
    }

    notself(client);

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
        client->json.storeobject(); // [<uh>]
        e = API_OK;

        User *u = client->ownuser();
        assert(u);
        if (!u)
        {
            LOG_err << "Own user not found when attempting to set user attributes";
            client->app->putua_result(API_EACCESS);
            return;
        }
        u->setattr(at, &av, NULL);
        u->setTag(tag ? tag : -1);
        client->notifyuser(u);
    }

    client->app->putua_result(e);
}

CommandGetUA::CommandGetUA(MegaClient* client, const char* uid, attr_t at, int ctag)
{
    this->uid = uid;
    this->at = at;

    cmd("uga");
    arg("u", uid);
    arg("ua", User::attr2string(at).c_str());
    arg("v", 1);

    tag = ctag;
}

void CommandGetUA::procresult()
{
    User *u = client->finduser(uid.c_str());

    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();
        client->app->getua_result(e);

#ifdef  ENABLE_CHAT
        if (client->fetchingkeys && u->userhandle == client->me && at == ATTR_SIG_RSA_PUBK)
        {
            client->initializekeys(); // we have now all the required data
        }
#endif
        return;
    }
    else
    {
        const char* ptr;
        const char* end;
        string value, version, buf;
        for (;;)
        {
            switch (client->json.getnameid())
            {
                case MAKENAMEID2('a','v'):
                {
                    if (!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
                    {
                        client->app->getua_result(API_EINTERNAL);
#ifdef ENABLE_CHAT
                        if (client->fetchingkeys && u->userhandle == client->me && at == ATTR_SIG_RSA_PUBK)
                        {
                            client->initializekeys(); // we have now all the required data
                        }
#endif
                        return;
                    }
                    buf.assign(ptr, (end-ptr));
                    break;
                }
                case 'v':
                {
                    if (!(ptr = client->json.getvalue()) || !(end = strchr(ptr, '"')))
                    {
                        client->app->getua_result(API_EINTERNAL);
#ifdef ENABLE_CHAT
                        if (client->fetchingkeys && u->userhandle == client->me && at == ATTR_SIG_RSA_PUBK)
                        {
                            client->initializekeys(); // we have now all the required data
                        }
#endif
                        return;
                    }
                    version.assign(ptr, (end-ptr));
                    break;
                }
                case EOO:
                {
                    // if there's no avatar, the value is "none" (not Base64 encoded)
                    if (u && at == ATTR_AVATAR && buf == "none")
                    {
                        u->setattr(at, NULL, &version);
                        u->setTag(tag ? tag : -1);
                        client->app->getua_result(API_ENOENT);
                        client->notifyuser(u);
                        return;
                    }

                    // convert from ASCII to binary the received data
                    value.resize(buf.size() / 4 * 3 + 3);
                    value.resize(Base64::atob(buf.data(), (byte *)value.data(), value.size()));

                    // Some attributes don't keep historic records, ie. *!authring or *!lstint
                    // (none of those attributes are used by the SDK yet)
                    // bool nonHistoric = (attributename.at(1) == '!');

                    // handle the attribute data depending on the scope
                    char scope = User::scope(at);

                    if (!u) // retrieval of attributes without contact-relationship
                    {
                        client->app->getua_result((byte*) value.data(), value.size());
                        return;
                    }

                    switch (scope)
                    {
                        case '*':   // private, encrypted
                        {
                            // decrypt the data and build the TLV records
                            TLVstore *tlvRecords = TLVstore::containerToTLVrecords(&value, &client->key);
                            if (!tlvRecords)
                            {
                                LOG_err << "Cannot extract TLV records for private attribute " << User::attr2string(at);
                                client->app->getua_result(API_EINTERNAL);
                                return;
                            }

                            // store the value for private user attributes (decrypted version of serialized TLV)
                            string *tlvString = tlvRecords->tlvRecordsToContainer(&client->key);
                            u->setattr(at, tlvString, &version);
                            delete tlvString;
                            client->app->getua_result(tlvRecords);
                            delete tlvRecords;
                        }
                            break;

                        case '+':   // public

                            u->setattr(at, &value, &version);
                            client->app->getua_result((byte*) value.data(), value.size());
#ifdef  ENABLE_CHAT
                            if (client->fetchingkeys && u->userhandle == client->me && at == ATTR_SIG_RSA_PUBK)
                            {
                                client->initializekeys(); // we have now all the required data
                            }
#endif
                            break;

                        case '#':   // protected

                            u->setattr(at, &value, &version);
                            client->app->getua_result((byte*) value.data(), value.size());
                            break;

                        case '^': // private, non-encrypted

                            // store the value in cache in binary format
                            u->setattr(at, &value, &version);
                            client->app->getua_result((byte*) value.data(), value.size());
                            break;

                        default:    // legacy attributes or unknown attribute
                            if (at != ATTR_FIRSTNAME &&           // protected
                                    at != ATTR_LASTNAME &&        // protected
                                    at != ATTR_COUNTRY  &&        // private
                                    at != ATTR_BIRTHDAY &&        // private
                                    at != ATTR_BIRTHMONTH &&      // private
                                    at != ATTR_BIRTHYEAR)         // private
                            {
                                LOG_err << "Unknown received attribute: " << User::attr2string(at);
                                client->app->getua_result(API_EINTERNAL);
                                return;
                            }

                            u->setattr(at, &value, &version);
                            client->app->getua_result((byte*) value.data(), value.size());
                            break;
                    }

                    u->setTag(tag ? tag : -1);
                    client->notifyuser(u);
                    return;
                }

                default:
                    if (!client->json.storeobject())
                    {
                        LOG_err << "Error in CommandPutUA. Parse error";
                        client->app->getua_result(API_EINTERNAL);
#ifdef  ENABLE_CHAT
                        if (client->fetchingkeys && u->userhandle == client->me && at == ATTR_SIG_RSA_PUBK)
                        {
                            client->initializekeys(); // we have now all the required data
                        }
#endif
                        return;
                    }
            }
        }
    }
}

#ifdef DEBUG
CommandDelUA::CommandDelUA(MegaClient *client, const char *an)
{
    this->an = an;

    cmd("upr");
    arg("ua", an);

    tag = client->reqtag;
}

void CommandDelUA::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();
        if (e == API_OK)
        {
            User *u = client->ownuser();
            attr_t at = User::string2attr(an.c_str());
            u->invalidateattr(at);

#ifdef ENABLE_CHAT
            if (at == ATTR_KEYRING)
            {
                client->resetKeyring();
            }
#endif
            client->notifyuser(u);
        }

        client->app->delua_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->delua_result(API_EINTERNAL);
    }
}

#endif

CommandGetUserEmail::CommandGetUserEmail(MegaClient *client, const char *uid)
{
    cmd("uge");
    arg("u", uid);

    tag = client->reqtag;
}

void CommandGetUserEmail::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->getuseremail_result(NULL, (error)client->json.getint());
    }

    string email;
    if (!client->json.storeobject(&email))
    {
        return client->app->getuseremail_result(NULL, API_EINTERNAL);
    }
    else
    {
        return client->app->getuseremail_result(&email, API_OK);
    }
}

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

CommandKeyCR::CommandKeyCR(MegaClient* /*client*/, node_vector* rshares, node_vector* rnodes, const char* keys)
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

    u = user;
    tag = client->reqtag;
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
    else
    {
        bool finished = false;
        while (!finished)
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
                    if (!u) // user has cancelled the account
                    {
                        return;
                    }

                    if (!ISUNDEF(uh))
                    {
                        client->mapuser(uh, u->email.c_str());
                    }

    #ifdef ENABLE_CHAT
                    if (client->fetchingkeys && u->userhandle == client->me && len_pubk)
                    {
                        client->pubk.setkey(AsymmCipher::PUBKEY, pubkbuf, len_pubk);
                        return;
                    }
    #endif

                    if (len_pubk && !u->pubk.setkey(AsymmCipher::PUBKEY, pubkbuf, len_pubk))
                    {
                        len_pubk = 0;
                    }
                    finished = true;
                    break;

                default:
                    if (client->json.storeobject())
                    {
                        continue;
                    }
                    len_pubk = 0;
                    finished = true;
                    break;
            }
        }
    }

    // satisfy all pending PubKeyAction requests for this user
    while (u->pkrs.size())
    {
        client->restag = tag;
        u->pkrs[0]->proc(client, u);
        delete u->pkrs[0];
        u->pkrs.pop_front();
    }

    if (len_pubk && !u->isTemporary)
    {
        client->notifyuser(u);
    }

    if (u->isTemporary)
    {
        delete u;
        u = NULL;
    }

    return;
}

void CommandPubKeyRequest::invalidateUser()
{
    u = NULL;
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
            client->app->userdata_result(&name, &pubk, &privk, jid, API_OK);
            return;

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

    arg("v", 1);

    tag = client->reqtag;
}

void CommandGetUserQuota::procresult()
{
    m_off_t td;
    bool got_storage = false;
    bool got_transfer = false;
    bool got_pro = false;

    if (client->json.isnumeric())
    {
        return client->app->account_details(details, (error)client->json.getint());
    }

    details->pro_level = 0;
    details->subscription_type = 'O';
    details->subscription_renew = 0;

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
    details->transfer_hist_valid = true;
    details->transfer_hist.clear();

    details->transfer_reserved = 0;

    details->transfer_limit = 0;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case MAKENAMEID2('b', 't'):                  // age of transfer
                                                         // window start
                td = client->json.getint();
                if (td != -1)
                {
                    details->transfer_hist_starttime = time(NULL) - td;
                }
                break;

            case MAKENAMEID3('b', 't', 'i'):
                details->transfer_hist_interval = client->json.getint();
                break;

            case MAKENAMEID3('t', 'a', 'h'):
                if (client->json.enterarray())
                {
                    m_off_t t;

                    while (client->json.isnumeric() && (t = client->json.getint()) != -1)
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
                        ns->version_bytes = client->json.getint();
                        ns->version_files = client->json.getint();

                        while(client->json.storeobject());
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

            case MAKENAMEID6('s', 'c', 'y', 'c', 'l', 'e'):
                const char* scycle;
                if ((scycle = client->json.getvalue()))
                {
                    memcpy(details->subscription_cycle, scycle, 3);
                    details->subscription_cycle[3] = 0;
                }
                break;

            case MAKENAMEID6('s', 'r', 'e', 'n', 'e', 'w'):
                if (client->json.enterarray())
                {
                    details->subscription_renew = client->json.getint();
                    while(!client->json.leavearray())
                    {
                        client->json.storeobject();
                    }
                }
                break;

            case MAKENAMEID3('s', 'g', 'w'):
                if (client->json.enterarray())
                {
                    client->json.storeobject(&details->subscription_method);
                    while(!client->json.leavearray())
                    {
                        client->json.storeobject();
                    }
                }
                break;

            case MAKENAMEID3('r', 't', 't'):
                details->transfer_hist_valid = !client->json.getint();
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

CommandQueryTransferQuota::CommandQueryTransferQuota(MegaClient* client, m_off_t size)
{
    cmd("qbq");
    arg("s", size);

    tag = client->reqtag;
}

void CommandQueryTransferQuota::procresult()
{
    if (!client->json.isnumeric())
    {
        LOG_err << "Unexpected response: " << client->json.pos;
        client->json.storeobject();

        // Returns 0 to not alarm apps and don't show overquota pre-warnings
        // if something unexpected is received, following the same approach as
        // in the webclient
        return client->app->querytransferquota_result(0);
    }

    return client->app->querytransferquota_result(client->json.getint());
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

CommandSetPH::CommandSetPH(MegaClient* client, Node* n, int del, m_time_t ets)
{
    cmd("l");
    arg("n", (byte*)&n->nodehandle, MegaClient::NODEHANDLE);

    if (del)
    {
        arg("d", 1);
    }

    if (ets)
    {
        arg("ets", ets);
    }

    this->h = n->nodehandle;
    this->ets = ets;
    this->tag = client->reqtag;
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

    Node *n = client->nodebyhandle(h);
    if (n)
    {
        n->setpubliclink(ph, ets, false);
        n->changed.publiclink = true;
        client->notifynode(n);
    }

    client->app->exportnode_result(h, ph);
}

CommandGetPH::CommandGetPH(MegaClient* client, handle cph, const byte* ckey, int cop)
{
    cmd("g");
    arg("p", (byte*)&cph, MegaClient::NODEHANDLE);

    ph = cph;
    havekey = ckey ? true : false;
    if (havekey)
    {
        memcpy(key, ckey, sizeof key);
    }
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
                    if (havekey)
                    {
                        client->app->openfilelink_result(ph, key, s, &a, &fa, op);
                    }
                    else
                    {
                        client->app->openfilelink_result(ph, NULL, s, &a, &fa, op);
                    }
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
        client->me = client->json.gethandle(MegaClient::USERHANDLE);
        client->resumeephemeral(client->me, pw, tag);
    }
}

CommandResumeEphemeralSession::CommandResumeEphemeralSession(MegaClient*, handle cuh, const byte* cpw, int ctag)
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

    notself(client);

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
                                     unsigned pubklen)
{
    cmd("up");
    arg("privk", privk, privklen);
    arg("pubk", pubk, pubklen);

    tag = client->reqtag;
}

void CommandSetKeyPair::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->setkeypair_result((error)client->json.getint());
    }

    client->json.storeobject();

    client->app->setkeypair_result(API_OK);
}

// fetch full node tree
CommandFetchNodes::CommandFetchNodes(MegaClient* client, bool nocache)
{
    cmd("f");
    arg("c", 1);
    arg("r", 1);

    if (!nocache)
    {
        arg("ca", 1);
    }

    tag = client->reqtag;
}

// purge and rebuild node/user tree
void CommandFetchNodes::procresult()
{
    WAIT_CLASS::bumpds();
    client->fnstats.timeToLastByte = Waiter::ds - client->fnstats.startTime;

    client->purgenodesusersabortsc();

    if (client->json.isnumeric())
    {
        client->fetchingnodes = false;
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
                    client->fetchingnodes = false;
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }
                break;

            case MAKENAMEID2('f', '2'):
                // old versions
                if (!client->readnodes(&client->json, 0))
                {
                    client->fetchingnodes = false;
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }
                break;

            case MAKENAMEID2('o', 'k'):
                // outgoing sharekeys
                client->readok(&client->json);
                break;

            case 's':
                // Fall through
            case MAKENAMEID2('p', 's'):
                // outgoing or pending shares
                client->readoutshares(&client->json);
                break;

            case 'u':
                // users/contacts
                if (!client->readusers(&client->json))
                {
                    client->fetchingnodes = false;
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
                // sequence number
                if (!client->setscsn(&client->json))
                {
                    client->fetchingnodes = false;
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }
                break;

            case MAKENAMEID3('i', 'p', 'c'):
                // Incoming pending contact
                client->readipc(&client->json);
                break;

            case MAKENAMEID3('o', 'p', 'c'):
                // Outgoing pending contact
                client->readopc(&client->json);
                break;

            case MAKENAMEID2('p', 'h'):
                // Public links handles
                client->procph(&client->json);
                break;

#ifdef ENABLE_CHAT
            case MAKENAMEID3('m', 'c', 'f'):
                // List of chatrooms
                client->procmcf(&client->json);
                break;

            case MAKENAMEID4('m', 'c', 'n', 'a'):
                // nodes shared in chatrooms
                client->procmcna(&client->json);
                break;
#endif
            case EOO:
            {
                if (!*client->scsn)
                {
                    client->fetchingnodes = false;
                    return client->app->fetchnodes_result(API_EINTERNAL);
                }

                client->mergenewshares(0);
                client->applykeys();
                client->initsc();
                client->pendingsccommit = false;
                client->fetchnodestag = tag;

                WAIT_CLASS::bumpds();
                client->fnstats.timeToCached = Waiter::ds - client->fnstats.startTime;
                client->fnstats.nodesCached = client->nodes.size();
                return;
            }
            default:
                if (!client->json.storeobject())
                {
                    client->fetchingnodes = false;
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

CommandSubmitPurchaseReceipt::CommandSubmitPurchaseReceipt(MegaClient *client, int type, const char *receipt)
{
    cmd("vpay");
    arg("t", type);

    if(receipt)
    {
        arg("receipt", receipt);
    }

    if(type == 2 && client->loggedin() == FULLACCOUNT)
    {
        arg("user", client->finduser(client->me)->uid.c_str());
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

// Credit Card Store
CommandCreditCardStore::CommandCreditCardStore(MegaClient* client, const char *cc, const char *last4, const char *expm, const char *expy, const char *hash)
{
    cmd("ccs");
    arg("cc", cc);
    arg("last4", last4);
    arg("expm", expm);
    arg("expy", expy);
    arg("hash", hash);

    tag = client->reqtag;
}

void CommandCreditCardStore::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->creditcardstore_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->creditcardstore_result(API_EINTERNAL);
    }
}

CommandCreditCardQuerySubscriptions::CommandCreditCardQuerySubscriptions(MegaClient* client)
{
    cmd("ccqns");

    tag = client->reqtag;
}

void CommandCreditCardQuerySubscriptions::procresult()
{
    int number = 0;
    if (client->json.isnumeric())
    {
        number = client->json.getint();
        if(number >= 0)
        {
            client->app->creditcardquerysubscriptions_result(number, API_OK);
        }
        else
        {
            client->app->creditcardquerysubscriptions_result(0, (error)number);
        }
    }
    else
    {
        client->json.storeobject();
        client->app->creditcardquerysubscriptions_result(0, API_EINTERNAL);
    }
}

CommandCreditCardCancelSubscriptions::CommandCreditCardCancelSubscriptions(MegaClient* client, const char* reason)
{
    cmd("cccs");

    if (reason)
    {
        arg("r", reason);
    }

    tag = client->reqtag;
}

void CommandCreditCardCancelSubscriptions::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->creditcardcancelsubscriptions_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->creditcardcancelsubscriptions_result(API_EINTERNAL);
    }
}

CommandCopySession::CommandCopySession(MegaClient *client)
{
    cmd("us");
    arg("c", 1);
    tag = client->reqtag;
}

void CommandCopySession::procresult()
{
    string session;
    byte sidbuf[AsymmCipher::MAXKEYLENGTH];
    int len_csid = 0;

    if (client->json.isnumeric())
    {
        client->app->copysession_result(NULL, (error)client->json.getint());
        return;
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case MAKENAMEID4('c', 's', 'i', 'd'):
                len_csid = client->json.storebinary(sidbuf, sizeof sidbuf);
                break;

            case EOO:
                if (len_csid < 32)
                {
                    return client->app->copysession_result(NULL, API_EINTERNAL);
                }

                if (!client->asymkey.decrypt(sidbuf, len_csid, sidbuf, MegaClient::SIDLEN))
                {
                    return client->app->copysession_result(NULL, API_EINTERNAL);
                }

                session.resize(MegaClient::SIDLEN * 4 / 3 + 4);
                session.resize(Base64::btoa(sidbuf, MegaClient::SIDLEN, (char *)session.data()));
                return client->app->copysession_result(&session, API_OK);

            default:
                if (!client->json.storeobject())
                {
                    return client->app->copysession_result(NULL, API_EINTERNAL);
                }
        }
    }
}

CommandGetPaymentMethods::CommandGetPaymentMethods(MegaClient *client)
{
    cmd("ufpq");
    tag = client->reqtag;
}

void CommandGetPaymentMethods::procresult()
{
    int methods = 0;

    if(!client->json.isnumeric())
    {
        LOG_err << "Parse error in ufpq";
        client->app->getpaymentmethods_result(methods, API_EINTERNAL);
        return;
    }

    do
    {
        int value = client->json.getint();
        if(value < 0)
        {
            client->app->getpaymentmethods_result(methods, (error)value);

            //Consume remaining values if they exist
            while(client->json.isnumeric())
            {
                client->json.getint();
            }
            return;
        }

        methods |= 1 << value;
    } while(client->json.isnumeric());

    client->app->getpaymentmethods_result(methods, API_OK);
}

CommandUserFeedbackStore::CommandUserFeedbackStore(MegaClient *client, const char *type, const char *blob, const char *uid)
{
    cmd("clog");

    arg("t", type);

    if (blob)
    {
        arg("d", blob);
    }

    if (uid)
    {
        arg("id", uid);
    }

    tag = client->reqtag;
}

void CommandUserFeedbackStore::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->userfeedbackstore_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->userfeedbackstore_result(API_EINTERNAL);
    }
}

CommandSendEvent::CommandSendEvent(MegaClient *client, int type, const char *desc)
{
    cmd("log");
    arg("e", type);
    arg("m", desc);

    tag = client->reqtag;
}

void CommandSendEvent::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->sendevent_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->sendevent_result(API_EINTERNAL);
    }
}

CommandCleanRubbishBin::CommandCleanRubbishBin(MegaClient *client)
{
    cmd("dr");

    tag = client->reqtag;
}

void CommandCleanRubbishBin::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->cleanrubbishbin_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->cleanrubbishbin_result(API_EINTERNAL);
    }
}

CommandGetRecoveryLink::CommandGetRecoveryLink(MegaClient *client, const char *email, int type)
{
    cmd("erm");
    arg("m", email);
    arg("t", type);

    tag = client->reqtag;
}

void CommandGetRecoveryLink::procresult()
{    
    if (client->json.isnumeric())
    {
        client->app->getrecoverylink_result((error)client->json.getint());
    }
    else    // error
    {
        client->json.storeobject();
        client->app->getrecoverylink_result(API_EINTERNAL);
    }
}

CommandQueryRecoveryLink::CommandQueryRecoveryLink(MegaClient *client, const char *linkcode)
{
    cmd("erv");
    arg("c", linkcode);

    tag = client->reqtag;
}

void CommandQueryRecoveryLink::procresult()
{
    // [<code>,"<email>","<ip_address>",<timestamp>,"<user_handle>",["<email>"]]

    client->json.enterarray();

    int type = API_EINTERNAL;
    string email;
    string ip;
    time_t ts;
    handle uh;

    if (!client->json.isnumeric() || ((type = client->json.getint()) < 0))   // error
    {
        return client->app->queryrecoverylink_result((error)type);
    }

    if ( !client->json.storeobject(&email)  ||
         !client->json.storeobject(&ip)     ||
         ((ts = client->json.getint()) == -1) ||
         !(uh = client->json.gethandle(MegaClient::USERHANDLE)) )
    {
        return client->app->queryrecoverylink_result(API_EINTERNAL);
    }

    string tmp;
    vector<string> emails;

    // read emails registered for this account
    client->json.enterarray();
    while (client->json.storeobject(&tmp))
    {
        emails.push_back(tmp);
        if (*client->json.pos == ']')
        {
            break;
        }
    }
    client->json.leavearray();  // emails array
    client->json.leavearray();  // response array

    if (!emails.size()) // there should be at least one email
    {
        return client->app->queryrecoverylink_result(API_EINTERNAL);
    }

    return client->app->queryrecoverylink_result(type, email.c_str(), ip.c_str(), ts, uh, &emails);
}

CommandGetPrivateKey::CommandGetPrivateKey(MegaClient *client, const char *code)
{
    cmd("erx");
    arg("r", "gk");
    arg("c", code);

    tag = client->reqtag;
}

void CommandGetPrivateKey::procresult()
{
    if (client->json.isnumeric())   // error
    {
        return client->app->getprivatekey_result((error)client->json.getint());
    }
    else
    {
        byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
        int len_privk = client->json.storebinary(privkbuf, sizeof privkbuf);

        // account has RSA keypair: decrypt server-provided session ID
        if (len_privk < 256)
        {
            return client->app->getprivatekey_result(API_EINTERNAL);
        }
        else
        {
            return client->app->getprivatekey_result((error)API_OK, privkbuf, len_privk);
        }
    }
}

CommandConfirmRecoveryLink::CommandConfirmRecoveryLink(MegaClient *client, const char *code, uint64_t newLoginHash, const byte *encMasterKey, const byte *initialSession)
{
    cmd("erx");

    if (!initialSession)
    {
        arg("r", "sk");
    }

    arg("c", code);

    arg("x", encMasterKey, SymmCipher::KEYLENGTH);
    arg("y", (byte*)&newLoginHash, sizeof newLoginHash);

    if (initialSession)
    {
        arg("z", initialSession, 2 * SymmCipher::KEYLENGTH);
    }

    tag = client->reqtag;
}

void CommandConfirmRecoveryLink::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->confirmrecoverylink_result((error)client->json.getint());
    }
    else   // error
    {
        client->json.storeobject();
        return client->app->confirmrecoverylink_result((error)API_EINTERNAL);
    }
}

CommandConfirmCancelLink::CommandConfirmCancelLink(MegaClient *client, const char *code)
{
    cmd("erx");
    arg("c", code);

    tag = client->reqtag;
}

void CommandConfirmCancelLink::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();
        MegaApp *app = client->app;
        app->confirmcancellink_result(e);
        if (!e)
        {
            app->request_error(API_ESID);
        }
        return;
    }
    else   // error
    {
        client->json.storeobject();
        return client->app->confirmcancellink_result((error)API_EINTERNAL);
    }
}

CommandValidatePassword::CommandValidatePassword(MegaClient *client, const char *email, uint64_t emailhash)
{
    cmd("us");
    arg("user", email);
    arg("uh", (byte*)&emailhash, sizeof emailhash);

    tag = client->reqtag;
}

void CommandValidatePassword::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->validatepassword_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        return client->app->validatepassword_result((error)API_OK);
    }
}

CommandGetEmailLink::CommandGetEmailLink(MegaClient *client, const char *email, int add)
{
    cmd("se");

    if (add)
    {
        arg("aa", "a");     // add
    }
    else
    {
        arg("aa", "r");     // remove
    }
    arg("e", email);    
    notself(client);

    tag = client->reqtag;
}

void CommandGetEmailLink::procresult()
{
    if (client->json.isnumeric())
    {
        return client->app->getemaillink_result((error)client->json.getint());
    }
    else    // error
    {
        client->json.storeobject();
        return client->app->getemaillink_result((error)API_EINTERNAL);
    }
}

CommandConfirmEmailLink::CommandConfirmEmailLink(MegaClient *client, const char *code, const char *email, uint64_t newLoginHash, bool replace)
{
    this->email = email;
    this->replace = replace;

    cmd("sec");

    arg("c", code);
    arg("e", email);
    arg("uh", (byte*)&newLoginHash, sizeof newLoginHash);
    if (replace)
    {
        arg("r", 1);    // replace the current email address by this one
    }
    notself(client);

    tag = client->reqtag;
}

void CommandConfirmEmailLink::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error)client->json.getint();

        if (!e)
        {
            User *u = client->finduser(client->me);

            if (replace)
            {
                LOG_debug << "Email changed from `" << u->email << "` to `" << email << "`";

                client->mapuser(u->userhandle, email.c_str()); // update email used as index for user's map
                u->changed.email = true;
                client->notifyuser(u);
            }
            // TODO: once we manage multiple emails, add the new email to the list of emails
        }

        return client->app->confirmemaillink_result(e);
    }
    else   // error
    {
        client->json.storeobject();
        return client->app->confirmemaillink_result((error)API_EINTERNAL);
    }
}

CommandGetVersion::CommandGetVersion(MegaClient *client, const char *appKey)
{
    this->client = client;
    cmd("lv");
    arg("a", appKey);
    tag = client->reqtag;
}

void CommandGetVersion::procresult()
{
    int versioncode = 0;
    string versionstring;

    if (client->json.isnumeric())
    {
        client->app->getversion_result(0, NULL, (error)client->json.getint());
        return;
    }

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 'c':
                versioncode = client->json.getint();
                break;

            case 's':
                client->json.storeobject(&versionstring);
                break;

            case EOO:
                return client->app->getversion_result(versioncode, versionstring.c_str(), API_OK);

            default:
                if (!client->json.storeobject())
                {
                    return client->app->getversion_result(0, NULL, API_EINTERNAL);
                }
        }
    }
}

CommandGetLocalSSLCertificate::CommandGetLocalSSLCertificate(MegaClient *client)
{
    this->client = client;
    cmd("lc");

    tag = client->reqtag;
}

void CommandGetLocalSSLCertificate::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->getlocalsslcertificate_result(0, NULL, (error)client->json.getint());
        return;
    }

    string certdata;
    m_time_t ts = 0;
    int numelements = 0;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 't':
            {
                ts = client->json.getint();
                break;
            }
            case 'd':
            {
                string data;
                client->json.enterarray();
                while (client->json.storeobject(&data))
                {
                    if (numelements)
                    {
                        certdata.append(";");
                    }
                    numelements++;
                    certdata.append(data);
                }
                client->json.leavearray();
                break;
            }
            case EOO:
            {
                if (numelements < 2)
                {
                    return client->app->getlocalsslcertificate_result(0, NULL, API_EINTERNAL);
                }
                return client->app->getlocalsslcertificate_result(ts, &certdata, API_OK);
            }

            default:
                if (!client->json.storeobject())
                {
                    return client->app->getlocalsslcertificate_result(0, NULL, API_EINTERNAL);
                }
        }
    }
}

#ifdef ENABLE_CHAT
CommandChatCreate::CommandChatCreate(MegaClient *client, bool group, const userpriv_vector *upl)
{
    this->client = client;
    this->chatPeers = new userpriv_vector(*upl);

    cmd("mcc");
    arg("g", (group) ? 1 : 0);

    beginarray("u");

    userpriv_vector::iterator itupl;
    for (itupl = chatPeers->begin(); itupl != chatPeers->end(); itupl++)
    {
        beginobject();

        handle uh = itupl->first;
        char uid[12];
        Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
        uid[11] = 0;

        privilege_t priv = itupl->second;

        arg("u", uid);
        arg("p", priv);

        endobject();
    }

    endarray();

    arg("v", 1);
    notself(client);

    tag = client->reqtag;
}

void CommandChatCreate::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->chatcreate_result(NULL, (error)client->json.getint());
        delete chatPeers;
    }
    else
    {
        handle chatid = UNDEF;
        int shard = -1;
        bool group = false;
        m_time_t ts = -1;

        for (;;)
        {
            switch (client->json.getnameid())
            {
                case MAKENAMEID2('i','d'):
                    chatid = client->json.gethandle(MegaClient::CHATHANDLE);
                    break;

                case MAKENAMEID2('c','s'):
                    shard = client->json.getint();
                    break;

                case 'g':
                    group = client->json.getint();
                    break;

                case MAKENAMEID2('t', 's'):  // actual creation timestamp
                    ts = client->json.getint();
                    break;

                case EOO:
                    if (chatid != UNDEF && shard != -1)
                    {
                        if (client->chats.find(chatid) == client->chats.end())
                        {
                            client->chats[chatid] = new TextChat();
                        }

                        TextChat *chat = client->chats[chatid];
                        chat->id = chatid;
                        chat->priv = PRIV_MODERATOR;
                        chat->shard = shard;
                        delete chat->userpriv;  // discard any existing `userpriv`
                        chat->userpriv = this->chatPeers;
                        chat->group = group;
                        chat->ts = (ts != -1) ? ts : 0;

                        chat->setTag(tag ? tag : -1);
                        client->notifychat(chat);

                        client->app->chatcreate_result(chat, API_OK);
                    }
                    else
                    {
                        client->app->chatcreate_result(NULL, API_EINTERNAL);
                        delete chatPeers;   // unused, but might be set at creation
                    }
                    return;

                default:
                    if (!client->json.storeobject())
                    {
                        client->app->chatcreate_result(NULL, API_EINTERNAL);
                        delete chatPeers;   // unused, but might be set at creation
                        return;
                    }
            }
        }
    }
}

CommandChatInvite::CommandChatInvite(MegaClient *client, handle chatid, handle uh, privilege_t priv, const char* title)
{
    this->client = client;
    this->chatid = chatid;
    this->uh = uh;
    this->priv = priv;
    this->title = title ? string(title) : "";

    char uid[12];
    Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
    uid[11] = 0;

    cmd("mci");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("u", uid);
    arg("p", priv);
    arg("v", 1);

    if (title != NULL)
    {
        arg("ct", title);
    }
    notself(client);

    tag = client->reqtag;
}

void CommandChatInvite::procresult()
{
    if (client->json.isnumeric())
    {        
        error e = (error) client->json.getint();
        if (e == API_OK)
        {
            if (client->chats.find(chatid) == client->chats.end())
            {
                // the invitation succeed for a non-existing chatroom
                client->app->chatinvite_result(API_EINTERNAL);
                return;
            }

            TextChat *chat = client->chats[chatid];
            if (!chat->userpriv)
            {
                chat->userpriv = new userpriv_vector();
            }

            chat->userpriv->push_back(userpriv_pair(uh, priv));

            if (!title.empty())  // only if title was set for this chatroom, update it
            {
                chat->title = title;
            }

            chat->setTag(tag ? tag : -1);
            client->notifychat(chat);
        }

        client->app->chatinvite_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->chatinvite_result(API_EINTERNAL);
    }
}

CommandChatRemove::CommandChatRemove(MegaClient *client, handle chatid, handle uh)
{
    this->client = client;
    this->chatid = chatid;
    this->uh = uh;

    cmd("mcr");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);

    if (uh != client->me)
    {
        char uid[12];
        Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
        uid[11] = 0;

        arg("u", uid);
    }
    arg("v", 1);
    notself(client);

    tag = client->reqtag;
}

void CommandChatRemove::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error) client->json.getint();
        if (e == API_OK)
        {
            if (client->chats.find(chatid) == client->chats.end())
            {
                // the invitation succeed for a non-existing chatroom
                client->app->chatremove_result(API_EINTERNAL);
                return;
            }

            TextChat *chat = client->chats[chatid];
            if (chat->userpriv)
            {
                userpriv_vector::iterator upvit;
                for (upvit = chat->userpriv->begin(); upvit != chat->userpriv->end(); upvit++)
                {
                    if (upvit->first == uh)
                    {
                        chat->userpriv->erase(upvit);
                        if (chat->userpriv->empty())
                        {
                            delete chat->userpriv;
                            chat->userpriv = NULL;
                        }
                        break;
                    }
                }
            }
            else
            {
                if (uh != client->me)
                {
                    // the removal succeed, but the list of peers is empty
                    client->app->chatremove_result(API_EINTERNAL);
                    return;
                }
            }

            if (uh == client->me)
            {
                chat->priv = PRIV_RM;
            }

            chat->setTag(tag ? tag : -1);
            client->notifychat(chat);
        }

        client->app->chatremove_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->chatremove_result(API_EINTERNAL);
    }
}

CommandChatURL::CommandChatURL(MegaClient *client, handle chatid)
{
    this->client = client;

    cmd("mcurl");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("v", 1);
    notself(client);

    tag = client->reqtag;
}

void CommandChatURL::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->chaturl_result(NULL, (error)client->json.getint());
    }
    else
    {
        string url;
        if (!client->json.storeobject(&url))
        {
            client->app->chaturl_result(NULL, API_EINTERNAL);
        }
        else
        {
            client->app->chaturl_result(&url, API_OK);
        }
    }
}

CommandChatGrantAccess::CommandChatGrantAccess(MegaClient *client, handle chatid, handle h, const char *uid)
{
    this->client = client;
    this->chatid = chatid;
    this->h = h;
    Base64::atob(uid, (byte*)&uh, MegaClient::USERHANDLE);

    cmd("mcga");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("n", (byte*)&h, MegaClient::NODEHANDLE);
    arg("u", uid);
    arg("v", 1);
    notself(client);

    tag = client->reqtag;
}

void CommandChatGrantAccess::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error) client->json.getint();
        if (e == API_OK)
        {
            if (client->chats.find(chatid) == client->chats.end())
            {
                // the action succeed for a non-existing chatroom??
                client->app->chatgrantaccess_result(API_EINTERNAL);
                return;
            }

            TextChat *chat = client->chats[chatid];
            chat->setNodeUserAccess(h, uh);

            chat->setTag(tag ? tag : -1);
            client->notifychat(chat);
        }

        client->app->chatgrantaccess_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->chatgrantaccess_result(API_EINTERNAL);
    }
}

CommandChatRemoveAccess::CommandChatRemoveAccess(MegaClient *client, handle chatid, handle h, const char *uid)
{
    this->client = client;
    this->chatid = chatid;
    this->h = h;
    Base64::atob(uid, (byte*)&uh, MegaClient::USERHANDLE);

    cmd("mcra");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("n", (byte*)&h, MegaClient::NODEHANDLE);
    arg("u", uid);
    arg("v", 1);
    notself(client);

    tag = client->reqtag;
}

void CommandChatRemoveAccess::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error) client->json.getint();
        if (e == API_OK)
        {
            if (client->chats.find(chatid) == client->chats.end())
            {
                // the action succeed for a non-existing chatroom??
                client->app->chatremoveaccess_result(API_EINTERNAL);
                return;
            }

            TextChat *chat = client->chats[chatid];
            chat->setNodeUserAccess(h, uh, true);

            chat->setTag(tag ? tag : -1);
            client->notifychat(chat);
        }

        client->app->chatremoveaccess_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->chatremoveaccess_result(API_EINTERNAL);
    }
}

CommandChatUpdatePermissions::CommandChatUpdatePermissions(MegaClient *client, handle chatid, handle uh, privilege_t priv)
{
    this->client = client;
    this->chatid = chatid;
    this->uh = uh;
    this->priv = priv;

    char uid[12];
    Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
    uid[11] = 0;

    cmd("mcup");
    arg("v", 1);

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("u", uid);
    arg("p", priv);
    notself(client);

    tag = client->reqtag;
}

void CommandChatUpdatePermissions::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error) client->json.getint();
        if (e == API_OK)
        {
            if (client->chats.find(chatid) == client->chats.end())
            {
                // the invitation succeed for a non-existing chatroom
                client->app->chatupdatepermissions_result(API_EINTERNAL);
                return;
            }

            TextChat *chat = client->chats[chatid];
            if (uh != client->me)
            {
                if (!chat->userpriv)
                {
                    // the update succeed, but that peer is not included in the chatroom
                    client->app->chatupdatepermissions_result(API_EINTERNAL);
                    return;
                }

                bool found = false;
                userpriv_vector::iterator upvit;
                for (upvit = chat->userpriv->begin(); upvit != chat->userpriv->end(); upvit++)
                {
                    if (upvit->first == uh)
                    {
                        chat->userpriv->erase(upvit);
                        chat->userpriv->push_back(userpriv_pair(uh, priv));
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    // the update succeed, but that peer is not included in the chatroom
                    client->app->chatupdatepermissions_result(API_EINTERNAL);
                    return;
                }
            }
            else
            {
                chat->priv = priv;
            }

            chat->setTag(tag ? tag : -1);
            client->notifychat(chat);
        }

        client->app->chatupdatepermissions_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->chatupdatepermissions_result(API_EINTERNAL);
    }
}


CommandChatTruncate::CommandChatTruncate(MegaClient *client, handle chatid, handle messageid)
{
    this->client = client;
    this->chatid = chatid;

    cmd("mct");
    arg("v", 1);

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("m", (byte*)&messageid, MegaClient::CHATHANDLE);
    notself(client);

    tag = client->reqtag;
}

void CommandChatTruncate::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error) client->json.getint();
        if (e == API_OK)
        {
            if (client->chats.find(chatid) == client->chats.end())
            {
                // the truncation succeed for a non-existing chatroom
                client->app->chattruncate_result(API_EINTERNAL);
                return;
            }

            TextChat *chat = client->chats[chatid];
            chat->setTag(tag ? tag : -1);
            client->notifychat(chat);
        }

        client->app->chattruncate_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->chattruncate_result(API_EINTERNAL);
    }
}

CommandChatSetTitle::CommandChatSetTitle(MegaClient *client, handle chatid, const char *title)
{
    this->client = client;
    this->chatid = chatid;
    this->title = title ? string(title) : "";

    cmd("mcst");
    arg("v", 1);

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("ct", title);
    notself(client);

    tag = client->reqtag;
}

void CommandChatSetTitle::procresult()
{
    if (client->json.isnumeric())
    {
        error e = (error) client->json.getint();
        if (e == API_OK)
        {
            if (client->chats.find(chatid) == client->chats.end())
            {
                // the invitation succeed for a non-existing chatroom
                client->app->chatsettitle_result(API_EINTERNAL);
                return;
            }

            TextChat *chat = client->chats[chatid];
            chat->title = title;

            chat->setTag(tag ? tag : -1);
            client->notifychat(chat);
        }

        client->app->chatsettitle_result(e);
    }
    else
    {
        client->json.storeobject();
        client->app->chatsettitle_result(API_EINTERNAL);
    }
}

CommandChatPresenceURL::CommandChatPresenceURL(MegaClient *client)
{
    this->client = client;
    cmd("pu");
    notself(client);
    tag = client->reqtag;
}

void CommandChatPresenceURL::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->chatpresenceurl_result(NULL, (error)client->json.getint());
    }
    else
    {
        string url;
        if (!client->json.storeobject(&url))
        {
            client->app->chatpresenceurl_result(NULL, API_EINTERNAL);
        }
        else
        {
            client->app->chatpresenceurl_result(&url, API_OK);
        }
    }
}

CommandRegisterPushNotification::CommandRegisterPushNotification(MegaClient *client, int deviceType, const char *token)
{
    this->client = client;
    cmd("spt");
    arg("p", deviceType);
    arg("t", token);

    tag = client->reqtag;
}

void CommandRegisterPushNotification::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->registerpushnotification_result((error)client->json.getint());
    }
    else
    {
        client->json.storeobject();
        client->app->registerpushnotification_result(API_EINTERNAL);
    }
}
#endif

CommandGetMegaAchievements::CommandGetMegaAchievements(MegaClient *client, AchievementsDetails *details, bool registered_user)
{
    this->details = details;

    if (registered_user)
    {
        cmd("maf");
    }
    else
    {
        cmd("mafu");
    }

    arg("v", (m_off_t)0);

    tag = client->reqtag;
}

void CommandGetMegaAchievements::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->getmegaachievements_result(details, (error)client->json.getint());
        return;
    }

    details->permanent_size = 0;
    details->achievements.clear();
    details->awards.clear();
    details->rewards.clear();

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case 's':
                details->permanent_size = client->json.getint();
                break;

            case 'u':
                if (client->json.enterobject())
                {
                    for (;;)
                    {
                        achievement_class_id id = client->json.getnameid();
                        if (id == EOO)
                        {
                            break;
                        }
                        id -= '0';   // convert to number

                        if (client->json.enterarray())
                        {
                            Achievement achievement;
                            achievement.storage = client->json.getint();
                            achievement.transfer = client->json.getint();
                            const char *exp_ts = client->json.getvalue();
                            char *pEnd = NULL;
                            achievement.expire = strtol(exp_ts, &pEnd, 10);
                            if (*pEnd == 'm')
                            {
                                achievement.expire *= 30;
                            }
                            else if (*pEnd == 'y')
                            {
                                achievement.expire *= 365;
                            }

                            details->achievements[id] = achievement;

                            while(client->json.storeobject());
                            client->json.leavearray();
                        }
                    }

                    client->json.leaveobject();
                }
                else
                {
                    LOG_err << "Failed to parse Achievements of MEGA achievements";
                    client->json.storeobject();
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return;
                }
                break;

            case 'a':
                if (client->json.enterarray())
                {
                    while (client->json.enterobject())
                    {
                        Award award;
                        award.achievement_class = 0;
                        award.award_id = 0;
                        award.ts = 0;
                        award.expire = 0;

                        bool finished = false;
                        while (!finished)
                        {
                            switch (client->json.getnameid())
                            {
                            case 'a':
                                award.achievement_class = client->json.getint();
                                break;
                            case 'r':
                                award.award_id = client->json.getint();
                                break;
                            case MAKENAMEID2('t', 's'):
                                award.ts = client->json.getint();
                                break;
                            case 'e':
                                award.expire = client->json.getint();
                                break;
                            case 'm':
                                if (client->json.enterarray())
                                {
                                    string email;
                                    while(client->json.storeobject(&email))
                                    {
                                        award.emails_invited.push_back(email);
                                    }

                                    client->json.leavearray();
                                }
                                break;
                            case EOO:
                                finished = true;
                                break;
                            default:
                                client->json.storeobject();
                                break;
                            }
                        }

                        details->awards.push_back(award);

                        client->json.leaveobject();
                    }

                    client->json.leavearray();
                }
                else
                {
                    LOG_err << "Failed to parse Awards of MEGA achievements";
                    client->json.storeobject();
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return;
                }
                break;

            case 'r':
                if (client->json.enterobject())
                {
                    for (;;)
                    {
                        nameid id = client->json.getnameid();
                        if (id == EOO)
                        {
                            break;
                        }

                        Reward reward;
                        reward.award_id = id - '0';   // convert to number

                        client->json.enterarray();

                        reward.storage = client->json.getint();
                        reward.transfer = client->json.getint();
                        const char *exp_ts = client->json.getvalue();
                        char *pEnd = NULL;
                        reward.expire = strtol(exp_ts, &pEnd, 10);
                        if (*pEnd == 'm')
                        {
                            reward.expire *= 30;
                        }
                        else if (*pEnd == 'y')
                        {
                            reward.expire *= 365;
                        }

                        while(client->json.storeobject());
                        client->json.leavearray();

                        details->rewards.push_back(reward);
                    }

                    client->json.leaveobject();
                }
                else
                {
                    LOG_err << "Failed to parse Rewards of MEGA achievements";
                    client->json.storeobject();
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return;
                }
                break;

            case EOO:
                client->app->getmegaachievements_result(details, API_OK);
                return;

            default:
                if (!client->json.storeobject())
                {
                    LOG_err << "Failed to parse MEGA achievements";
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return;
                }
                break;
        }
    }
}

CommandGetWelcomePDF::CommandGetWelcomePDF(MegaClient *client)
{
    cmd("wpdf");

    tag = client->reqtag;
}

void CommandGetWelcomePDF::procresult()
{
    if (client->json.isnumeric())
    {
        client->app->getwelcomepdf_result(UNDEF, NULL, (error)client->json.getint());
        return;
    }

    handle ph = UNDEF;
    byte keybuf[FILENODEKEYLENGTH];
    int len_key = 0;
    string key;

    for (;;)
    {
        switch (client->json.getnameid())
        {
            case MAKENAMEID2('p', 'h'):
                ph = client->json.gethandle(MegaClient::NODEHANDLE);
                break;

            case 'k':
                len_key = client->json.storebinary(keybuf, sizeof keybuf);
                break;

            case EOO:
                if (ISUNDEF(ph) || len_key != FILENODEKEYLENGTH)
                {
                    return client->app->getwelcomepdf_result(UNDEF, NULL, API_EINTERNAL);
                }
                key.assign((const char *) keybuf, len_key);
                return client->app->getwelcomepdf_result(ph, &key, API_OK);

            default:
                if (!client->json.storeobject())
                {
                    LOG_err << "Failed to parse welcome PDF response";
                    return client->app->getwelcomepdf_result(UNDEF, NULL, API_EINTERNAL);
                }
                break;
        }
    }
}

} // namespace
