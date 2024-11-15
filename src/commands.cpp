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

#include "mega.h"
#include "mega/base64.h"
#include "mega/command.h"
#include "mega/fileattributefetch.h"
#include "mega/heartbeats.h"
#include "mega/mediafileattribute.h"
#include "mega/megaapp.h"
#include "mega/transfer.h"
#include "mega/transferslot.h"
#include "mega/types.h"
#include "mega/user.h"
#include "mega/user_attribute.h"
#include "mega/utils.h"

#include <charconv>
#include <optional>

namespace mega {

CommandPutFA::CommandPutFA(NodeOrUploadHandle cth,
                           fatype /*ctype*/,
                           bool usehttps,
                           int ctag,
                           size_t size,
                           bool getIP,
                           CommandPutFA::Cb&& completion):
    mCompletion(std::move(completion))
{
    cmd("ufa");
    arg("s", size);

    if (cth.isNodeHandle())
    {
        arg("h", cth.nodeHandle());
    }

    if (usehttps)
    {
        arg("ssl", 2);
    }

    if (getIP)
    {
        arg("v", 3);
    }

    tag = ctag;
}

HttpReqFA::HttpReqFA(NodeOrUploadHandle cth, fatype ctype, bool usehttps, int ctag, std::unique_ptr<string> cdata, bool getIP, MegaClient* client)
    : data(std::move(cdata))
{
    tag = ctag;
    progressreported = 0;

    th = cth;
    type = ctype;

    binary = true;

    getURLForFACmd = [this, cth, ctype, usehttps, ctag, getIP, client](){

        std::weak_ptr<HttpReqFA> weakSelf(shared_from_this());

        return new CommandPutFA(cth, ctype, usehttps, ctag, data->size(), getIP,
            [weakSelf, client](Error e, const std::string & url, const vector<std::string> & /*ips*/)
            {
                auto self = weakSelf.lock();
                if (!self) return;

                if (!self->data || self->data->empty())
                {
                    e = API_EARGS;
                    LOG_err << "Data object is " << (!self->data ? "nullptr" : "empty");
                }

                if (e == API_OK)
                {
                    LOG_debug << "Sending file attribute data";
                    self->progressreported = 0;
                    self->HttpReq::type = REQ_BINARY;
                    self->posturl = url;

                    // post sets the status for http processing state machine
                    self->post(client, self->data->data(), static_cast<unsigned>(self->data->size()));
                }
                else
                {
                    // jumping to REQ_SUCCESS, but with no handle in `in`, means we failed overall (and don't retry)
                    self->status = REQ_SUCCESS;
                    client->app->putfa_result(self->th.nodeHandle().as8byte(), self->type, e);
                }
            });
    };
}

bool CommandPutFA::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        assert(!r.wasError(API_EAGAIN)); // these would not occur here, we would retry after backoff
        assert(!r.wasError(API_ERATELIMIT));

        if (r.wasError(API_EACCESS))
        {
            // create a custom attribute indicating thumbnail can't be restored from this account
            shared_ptr<Node> n = client->nodeByHandle(th.nodeHandle());

            char me64[12];
            Base64::btoa((const byte*)&client->me, MegaClient::USERHANDLE, me64);

            if (n && client->checkaccess(n.get(), FULL) &&
                    (n->attrs.map.find('f') == n->attrs.map.end() || n->attrs.map['f'] != me64) )
            {
                LOG_debug << "Restoration of file attributes is not allowed for current user (" << me64 << ").";

                // 'canChangeVault' is false here because restoration of file attributes is triggered by
                // downloads, so it cannot be triggered by a Backup operation
                bool canChangeVault = false;
                client->setattr(n, attr_map('f', me64), nullptr, canChangeVault);
            }
        }

        mCompletion(r.errorOrOK(), {}, {});
        return true;
    }
    else
    {
        const char* p = NULL;
        std::vector<string> ips;

        for (;;)
        {
            switch (json.getnameid())
            {
                case 'p':
                    p = json.getvalue();
                    break;

                case MAKENAMEID2('i', 'p'):
                    loadIpsFromJson(ips, json);
                    break;

                case EOO:
                    if (!p)
                    {
                        mCompletion(API_EINTERNAL, {}, {});
                    }
                    else
                    {
                        string posturl;
                        JSON::copystring(&posturl, p);

                        // cache resolved URLs if received
                        std::vector<string> urls(1, posturl);
                        std::vector<string> ipsCopy = ips;

                        if(!cacheresolvedurls(urls, std::move(ips)))
                        {
                            LOG_err << "Unpaired IPs received for URLs in `ufa` command. URLs: " << urls.size() << " IPs: " << ips.size();
                        }

                        mCompletion(API_OK, posturl, ipsCopy);
                    }
                    return true;

                default:
                    if (!json.storeobject())
                    {
                        mCompletion(API_EINTERNAL, {}, {});
                        return false;
                    }
            }
        }
    }
}

m_off_t HttpReqFA::transferred(MegaClient *client)
{
    if (httpiohandle)
    {
        client->httpio->postpos(httpiohandle);
        return true;
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

bool CommandGetFA::procresult(Result r, JSON& json)
{
    fafc_map::iterator it = client->fafcs.find(part);

    if (r.wasErrorOrOK())
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

            it->second->e = r.errorOrOK();
            it->second->req.status = REQ_FAILURE;
        }

        return true;
    }

    const char* p = NULL;

    for (;;)
    {
        switch (json.getnameid())
        {
            case 'p':
                p = json.getvalue();
                break;

            case EOO:
                if (it != client->fafcs.end())
                {
                    if (p)
                    {
                        JSON::copystring(&it->second->posturl, p);
                        it->second->urltime = Waiter::ds;
                        it->second->dispatch();
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

                return true;

            default:
                if (!json.storeobject())
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
                    return false;
                }
        }
    }
}

CommandAttachFA::CommandAttachFA(MegaClient*, handle nh, fatype t, handle ah, int ctag)
{
    mSeqtagArray = true;
    cmd("pfa");

    arg("n", (byte*)&nh, MegaClient::NODEHANDLE);

    char buf[64];

    snprintf(buf, sizeof(buf), "%u*", t);
    Base64::btoa((byte*)&ah, sizeof(ah), strchr(buf + 2, 0));
    arg("fa", buf);

    h = nh;
    type = t;
    tag = ctag;
}

CommandAttachFA::CommandAttachFA(MegaClient*,
                                 handle nh,
                                 fatype t,
                                 const std::string& encryptedAttributes,
                                 int ctag)
{
    mSeqtagArray = true;
    cmd("pfa");

    arg("n", (byte*)&nh, MegaClient::NODEHANDLE);

    arg("fa", encryptedAttributes.c_str());

    h = nh;
    type = t;
    tag = ctag;
}

bool CommandAttachFA::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->putfa_result(h, type, r.errorOrOK());
        return true;
    }
    else
    {
        string fa;
        if (json.storeobject(&fa))
        {
#ifdef DEBUG
            shared_ptr<Node> n = client->nodebyhandle(h);
            assert(!n || n->fileattrstring == fa);
#endif
            client->app->putfa_result(h, type, API_OK);
            return true;
        }
    }
    client->app->putfa_result(h, type, API_EINTERNAL);
    return false;
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

    arg("v", 3);
    arg("s", tslot->fa->size);
    arg("ms", ms);

    // send minimum set of different tree's roots for API to check overquota
    set<handle> targetRoots;
    bool begun = false;
    for (auto &file : tslot->transfer->files)
    {
        if (!file->h.isUndef())
        {
            shared_ptr<Node> node = client->nodeByHandle(file->h);
            if (node)
            {
                assert(node->type != FILENODE);
                assert(!node->parent || node->parent->type != FILENODE);

                handle rootnode = client->getrootnode(node)->nodehandle;
                if (targetRoots.find(rootnode) != targetRoots.end())
                {
                    continue;
                }

                targetRoots.insert(rootnode);
            }
            if (!begun)
            {
                beginarray("t");
                begun = true;
            }

            element((byte*)&file->h, MegaClient::NODEHANDLE);
        }
    }

    if (begun)
    {
        endarray();
    }
    else
    {
        // Target user goes alone, not inside an array. Note: we are skipping this if a)more than two b)the array had been created for node handles
        for (auto &file : tslot->transfer->files)
        {
            if (file->h.isUndef() && file->targetuser.size())
            {
                arg("t", file->targetuser.c_str());
                break;
            }
        }
    }
}

void CommandPutFile::cancel()
{
    Command::cancel();
    tslot = NULL;
}

// set up file transfer with returned target URL
bool CommandPutFile::procresult(Result r, JSON& json)
{
    if (tslot)
    {
        tslot->pendingcmd = NULL;
    }
    else
    {
        canceled = true;
    }

    if (r.wasErrorOrOK())
    {
        if (!canceled)
        {
            tslot->transfer->failed(r.errorOrOK(), *client->mTctableRequestCommitter);
        }

        return true;
    }

    std::vector<std::string> tempurls;
    std::vector<std::string> tempips;
    for (;;)
    {
        switch (json.getnameid())
        {
            case 'p':
                tempurls.push_back("");
                json.storeobject(canceled ? NULL : &tempurls.back());
                break;

            case MAKENAMEID2('i', 'p'):
                loadIpsFromJson(tempips, json);
                break;
            case EOO:
                if (canceled) return true;

                if (tempurls.size() == 1)
                {
                    if(!cacheresolvedurls(tempurls, std::move(tempips)))
                    {
                        LOG_err << "Unpaired IPs received for URLs in `u` command. URLs: " << tempurls.size() << " IPs: " << tempips.size();
                    }

                    tslot->transfer->tempurls = tempurls;
                    tslot->transferbuf.setIsRaid(tslot->transfer, tempurls, tslot->transfer->pos, tslot->maxRequestSize);
                    tslot->starttime = tslot->lastdata = client->waiter->ds;
                    tslot->progress();
                }
                else
                {
                    tslot->transfer->failed(API_EINTERNAL, *client->mTctableRequestCommitter);
                }
                return true;

            default:
                if (!json.storeobject())
                {
                    if (!canceled)
                    {
                        tslot->transfer->failed(API_EINTERNAL, *client->mTctableRequestCommitter);
                    }

                    return false;
                }
        }
    }
}

// request upload target URL
CommandGetPutUrl::CommandGetPutUrl(m_off_t size, int putmbpscap, bool forceSSL, bool getIP, CommandGetPutUrl::Cb completion)
    : mCompletion(completion)
{
    cmd("u");
    if (forceSSL)
    {
        arg("ssl", 2);
    }
    if (getIP)
    {
        arg("v", 3);
    }
    else
    {
        arg("v", 2);
    }
    arg("s", size);
    arg("ms", putmbpscap);
}


// set up file transfer with returned target URL
bool CommandGetPutUrl::procresult(Result r, JSON& json)
{
    string url;
    std::vector<string> ips;

    if (r.wasErrorOrOK())
    {
        if (!canceled)
        {
            mCompletion(r.errorOrOK(), url, ips);
        }
        return true;
    }

    for (;;)
    {
        switch (json.getnameid())
        {
            case 'p':
                json.storeobject(canceled ? nullptr : &url);
                break;
            case MAKENAMEID2('i', 'p'):
                loadIpsFromJson(ips, json);
                break;
            case EOO:
                if (canceled) return true;
                mCompletion(API_OK, url, ips);
                return true;

            default:
                if (!json.storeobject())
                {
                    if (!canceled)
                    {
                        mCompletion(API_EINTERNAL, string(), {});
                    }
                    return false;
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
    arg("g", 1); // server will provide download URL(s)/token(s) (if skipped, only information about the file)
    arg("v", 2);  // version 2: server can supply details for cloudraid files

    if (drn->privateauth.size())
    {
        arg("esid", drn->privateauth.c_str());
    }

    if (drn->publicauth.size())
    {
        arg("en", drn->publicauth.c_str());
    }

    if (drn->chatauth.size())
    {
        arg("cauth", drn->chatauth.c_str());
    }

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

bool CommandDirectRead::procresult(Result r, JSON& json)
{
    if (drn)
    {
        drn->pendingcmd = NULL;
    }

    if (r.wasErrorOrOK())
    {
        if (!canceled && drn)
        {
            drn->cmdresult(r.errorOrOK());
        }
        return true;
    }
    else
    {
        Error e(API_EINTERNAL);
        dstime tl = 0;
        std::vector<std::string> tempurls;

        for (;;)
        {
            switch (json.getnameid())
            {
                case 'g':
                    if (json.enterarray())   // now that we are requesting v2, the reply will be an array of 6 URLs for a raid download, or a single URL for the original direct download
                    {
                        for (;;)
                        {
                            std::string tu;
                            if (!json.storeobject(&tu))
                            {
                                break;
                            }
                            tempurls.push_back(tu);
                        }
                        json.leavearray();
                    }
                    else
                    {
                        std::string tu;
                        if (json.storeobject(&tu))
                        {
                            tempurls.push_back(tu);
                        }
                    }
                    if (tempurls.size() == 1 || tempurls.size() == RAIDPARTS)
                    {
                        if (drn)
                        {
                            drn->tempurls.swap(tempurls);
                            e.setErrorCode(API_OK);
                        }
                    }
                    else
                    {
                        e.setErrorCode(API_EINCOMPLETE);
                    }
                    break;

                case 's':
                    if (drn)
                    {
                        drn->size = json.getint();
                    }
                    break;

                case 'd':
                    e = API_EBLOCKED;
                    break;

                case 'e':
                    e = (error)json.getint();
                    break;

                case MAKENAMEID2('t', 'l'):
                    tl = dstime(json.getint());
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

                    return true;

                default:
                    if (!json.storeobject())
                    {
                        if (!canceled && drn)
                        {
                            drn->cmdresult(e);
                        }

                        return false;
                    }
            }
        }
    }
}

// request temporary source URL for full-file access (p == private node)
CommandGetFile::CommandGetFile(MegaClient *client, const byte* key, size_t keySize, bool undelete,
                               handle h, bool p, const char *privateauth,
                               const char *publicauth, const char *chatauth,
                               bool singleUrl, Cb &&completion)
{
    cmd(undelete ? "gd" : "g");
    arg(p ? "n" : "p", (byte*)&h, MegaClient::NODEHANDLE);
    arg("g", 1); // server will provide download URL(s)/token(s) (if skipped, only information about the file)
    if (!singleUrl)
    {
        arg("v", 2);  // version 2: server can supply details for cloudraid files
    }

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

    if (chatauth)
    {
        arg("cauth", chatauth);
    }

    assert(key && "no key provided!");
    if (key && keySize != SymmCipher::KEYLENGTH)
    {
        assert (keySize <= FILENODEKEYLENGTH);
        memcpy(filekey, key, keySize);
        mFileKeyType = FILENODE;
    }
    else if (key && keySize == SymmCipher::KEYLENGTH)
    {
        memcpy(filekey, key, SymmCipher::KEYLENGTH);
        mFileKeyType = 1;
    }

    mCompletion = std::move(completion);
}

void CommandGetFile::cancel()
{
    Command::cancel();
}


void CommandGetFile::callFailedCompletion(const Error &e)
{
    assert(mCompletion);
    if (mCompletion)
    {
        mCompletion(e, -1, 0, nullptr, nullptr, nullptr, {}, {}, {});
    }
}

// process file credentials
bool CommandGetFile::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (!canceled)
        {
            callFailedCompletion(r.errorOrOK());
        }
        return true;
    }

    const char* at = nullptr;
    Error e(API_EINTERNAL);
    m_off_t s = -1;
    dstime tl = 0;
    std::unique_ptr<byte[]> buf;

    // credentials relevant to a non-TransferSlot scenario (node query)
    string fileattrstring;
    string filenamestring;
    string filefingerprint;
    vector<string> tempurls;
    vector<string> tempips;
    string fileHandle;

    for (;;)
    {
        switch (json.getnameid())
        {
            case 'g':
                if (json.enterarray())   // now that we are requesting v2, the reply will be an array of 6 URLs for a raid download, or a single URL for the original direct download
                {
                    for (;;)
                    {
                        std::string tu;
                        if (!json.storeobject(&tu))
                        {
                            break;
                        }
                        tempurls.push_back(tu);
                    }
                    json.leavearray();
                }
                else
                {
                    std::string tu;
                    if (json.storeobject(&tu))
                    {
                        tempurls.push_back(tu);
                    }
                }
                e.setErrorCode(API_OK);
                break;

            case MAKENAMEID2('i', 'p'):
                loadIpsFromJson(tempips, json);
                break;

            case 's':
                s = json.getint();
                break;

            case MAKENAMEID2('a', 't'):
                at = json.getvalue();
                break;

            case MAKENAMEID2('f', 'a'):
                json.storeobject(&fileattrstring);
                break;

            case 'e':
                e = (error)json.getint();
                break;

            case MAKENAMEID2('t', 'l'):
                tl = dstime(json.getint());
                break;

            case MAKENAMEID2('f', 'h'):
            {
                json.storeobject(&fileHandle);
                break;
            }

            case EOO:
            {
                // defer code that steals the ips <move(tempips)> and stores them in the cache
                // thus we can use them before going out of scope
                std::shared_ptr<void> deferThis(nullptr, [this, &tempurls, &tempips](...)
                {
                    if(!cacheresolvedurls(tempurls, std::move(tempips)))
                    {
                        LOG_err << "Unpaired IPs received for URLs in `g` command. URLs: " << tempurls.size() << " IPs: " << tempips.size();
                    }
                });

                if (canceled) //do not proceed: SymmCipher may no longer exist
                {
                    return true;
                }

                if (!at)
                {
                    callFailedCompletion(API_EINTERNAL);
                    return true;
                }

                // decrypt at and set filename
                SymmCipher * cipherer = client->getRecycledTemporaryTransferCipher(filekey, mFileKeyType);
                const char* eos = strchr(at, '"');
                buf.reset(Node::decryptattr(cipherer, at, eos ? eos - at : strlen(at)));
                if (!buf)
                {
                    callFailedCompletion(API_EKEY);
                    return true;
                }

                // all good, lets parse the attribute string
                JSON attrJson;
                attrJson.begin((char*)buf.get() + 5);

                for (;;)
                {
                    switch (attrJson.getnameid())
                    {
                        case 'c':
                            if (!attrJson.storeobject(&filefingerprint))
                            {
                                callFailedCompletion(API_EINTERNAL);
                                return true;
                            }
                            break;

                        case 'n':
                            if (!attrJson.storeobject(&filenamestring))
                            {
                                callFailedCompletion(API_EINTERNAL);
                                return true;
                            }
                            break;

                        case EOO:
                            // success, call completion function!
                            return mCompletion ? mCompletion(e,
                                                             s,
                                                             tl,
                                                             &filenamestring,
                                                             &filefingerprint,
                                                             &fileattrstring,
                                                             tempurls,
                                                             tempips,
                                                             fileHandle) :
                                                 false;

                        default:
                            if (!attrJson.storeobject())
                            {
                                callFailedCompletion(API_EINTERNAL);
                                return false;
                            }
                    }
                }
            }
            default:
                if (!json.storeobject())
                {
                    if (!canceled)
                    {
                        callFailedCompletion(API_EINTERNAL);
                    }
                    return false;
                }
        }
    }
}

CommandSetAttr::CommandSetAttr(MegaClient*,
                               std::shared_ptr<Node> n,
                               attr_map&& attrMapUpdates,
                               Completion&& c,
                               bool canChangeVault):
    mAttrMapUpdates(attrMapUpdates),
    mCanChangeVault(canChangeVault)
{
    h = n->nodeHandle();
    mNode = n;
    generationError = API_OK;
    completion = c;

    addToNodePendingCommands(n.get());
}

const char* CommandSetAttr::getJSON(MegaClient* client)
{
    // We generate the command just before sending, so it's up to date for any external changes that occured in the meantime
    // And we can also take into account any changes we have sent for this node that have not yet been applied by actionpackets
    jsonWriter.clear();
    generationError = API_OK;

    cmd("a");

    string at;
    if (shared_ptr<Node> n = client->nodeByHandle(h))
    {
        assert(n == mNode);
        AttrMap m = n->attrs;

        // apply these changes for sending, but also any earlier changes that are ahead in the queue
        assert(!n->mPendingChanges.empty());
        n->mPendingChanges.forEachCommand([&m, this](Command* cmd)
        {
            if (cmd == this) return;
            if (auto cmdSetAttr = dynamic_cast<CommandSetAttr*>(cmd))
            {
                cmdSetAttr->applyUpdatesTo(m);
            }
        });

        m.applyUpdates(mAttrMapUpdates);

        if (SymmCipher* cipher = n->nodecipher())
        {
            m.getjson(&at);
            client->makeattr(cipher, &at, at.c_str(), int(at.size()));
        }
        else
        {
            h.setUndef();  // dummy command to generate an error, with no effect
            mNode.reset();
            generationError = API_EKEY;
        }

        if (at.size() > MAX_NODE_ATTRIBUTE_SIZE)
        {
            client->sendevent(99484, "Node attribute exceed maximun size");
            LOG_err << "Node attribute exceed maximun size";
            h.setUndef();  // dummy command to generate an error, with no effect
            mNode.reset();
            generationError =  API_EARGS;
        }
    }
    else
    {
        h.setUndef();  // dummy command to generate an error, with no effect
        mNode.reset();
        generationError = API_ENOENT;
    }

    arg("n", (byte*)&h, MegaClient::NODEHANDLE);
    arg("at", (byte*)at.c_str(), int(at.size()));

    if (mCanChangeVault)
    {
        arg("vw", 1);
    }

    return jsonWriter.getstring().c_str();
}

bool CommandSetAttr::procresult(Result r, JSON&)
{
    removeFromNodePendingCommands(h, client);
    if (completion) completion(h, generationError ? Error(generationError) : r.errorOrOK());
    return r.wasErrorOrOK();
}

void CommandSetAttr::applyUpdatesTo(AttrMap& attrMap) const
{
    attrMap.applyUpdates(mAttrMapUpdates);
}

// (the result is not processed directly - we rely on the server-client
// response)
CommandPutNodes::CommandPutNodes(MegaClient* client,
                                 NodeHandle th,
                                 const char* userhandle,
                                 VersioningOption vo,
                                 vector<NewNode>&& newnodes,
                                 int ctag,
                                 putsource_t csource,
                                 const char* cauth,
                                 Completion&& resultFunction,
                                 bool canChangeVault,
                                 const string& customerIpPort):
    mResultFunction(resultFunction)
{
    byte key[FILENODEKEYLENGTH];

#ifndef NDEBUG
    assert(newnodes.size() > 0);
    for (auto& n : newnodes) assert(n.canChangeVault == canChangeVault);
#endif

    nn = std::move(newnodes);
    type = userhandle ? USER_HANDLE : NODE_HANDLE;
    source = csource;
    mSeqtagArray = true;
    cmd("p");

    arg("v", 4); // include file IDs/handles

    if (userhandle)
    {
        arg("t", userhandle);
        targethandle.setUndef();
    }
    else
    {
        arg("t", (byte*)&th, MegaClient::NODEHANDLE);
        targethandle = th;
    }

    arg("sm",1);

    if (cauth)
    {
        arg("cauth", cauth);
    }

    if (canChangeVault)
    {
        arg("vw", 1);
    }

    // "vb": when provided, it force to override the account-wide versioning behavior by the value indicated by client
    //     vb:1 to force it on
    //     vb:0 to force it off
    // Dont provide it at all to rely on the account-wide setting (as of the moment the command is processed).

    if (vo == UseLocalVersioningFlag && client->loggedIntoWritableFolder())
    {   // do not rely on local versioning flag when logged into writable folders
        //  as the owner's ATTR_DISABLE_VERSIONS attribute is not received/updated in that case
        // and MegaClient::versions_disabled will alwas be false.
        // Instead, let the API server act according to user's settings
        vo =  UseServerVersioningFlag;
    }

    switch (vo)
    {
        case NoVersioning:
            break;

        case ClaimOldVersion:
            arg("vb", 1);
            break;

        case ReplaceOldVersion:
            arg("vb", m_off_t(0));
            break;

        case UseLocalVersioningFlag:
            arg("vb", !client->versions_disabled);
            vo = !client->versions_disabled ? ClaimOldVersion : ReplaceOldVersion;
            break;

        case UseServerVersioningFlag:
            break;
    }

    beginarray("n");

    for (unsigned i = 0; i < nn.size(); i++)
    {
        beginobject();

        NewNode* nni = &nn[i];
        switch (nni->source)
        {
            case NEW_NODE:
                arg("h", (byte*)&nni->nodehandle, MegaClient::NODEHANDLE);
                break;

            case NEW_PUBLIC:
                arg("ph", (byte*)&nni->nodehandle, MegaClient::NODEHANDLE);
                break;

            case NEW_UPLOAD:
                arg("h", nni->uploadtoken.data(), sizeof nn[0].uploadtoken);

                // include pending file attributes for this upload
                string s;

                if (nni->fileattributes)
                {
                    // if attributes are set on the newnode then the app is not using the pendingattr mechanism
                    s.swap(*nni->fileattributes);
                    nni->fileattributes.reset();
                }
                else
                {
                    client->pendingattrstring(nn[i].uploadhandle, &s);

#ifdef USE_MEDIAINFO
                    client->mediaFileInfo.addUploadMediaFileAttributes(nn[i].uploadhandle, &s);
#endif
                }

                if (s.size())
                {
                    arg("fa", s.c_str(), 1);
                }
        }

        if (!ISUNDEF(nn[i].parenthandle))
        {
            arg("p", (byte*)&nn[i].parenthandle, MegaClient::NODEHANDLE);
        }

        if (vo != NoVersioning &&
            nn[i].type == FILENODE && !nn[i].ovhandle.isUndef())
        {
            arg("ov", (byte*)&nn[i].ovhandle, MegaClient::NODEHANDLE);
        }
        nn[i].mVersioningOption = vo;

        arg("t", nn[i].type);
        arg("a", (byte*)nn[i].attrstring->data(), int(nn[i].attrstring->size()));

        if (!client->loggedIntoWritableFolder())
        {
            assert(!nn[i].hasZeroKey()); // Add this assert here to avoid extra checks in production -> we will have a check and logs if this fails within CommandPutNode::procresult
            if (nn[i].nodekey.size() <= sizeof key)
            {
                client->key.ecb_encrypt((byte*)nn[i].nodekey.data(), key, nn[i].nodekey.size());
                assert(!SymmCipher::isZeroKey(key, FILENODEKEYLENGTH));
                arg("k", key, int(nn[i].nodekey.size()));
            }
            else
            {
                arg("k", (const byte*)nn[i].nodekey.data(), int(nn[i].nodekey.size()));
            }
        }
        endobject();
    }

    endarray();

    if (!customerIpPort.empty())
    {
        arg("cip", customerIpPort.c_str()); // "IPv4:port" or "[IPv6]:port"
    }

    // add cr element for new nodes, if applicable
    if (type == NODE_HANDLE)
    {
        shared_ptr<Node> tn;
        if ((tn = client->nodeByHandle(th)))
        {
            assert(tn->type != FILENODE);

            ShareNodeKeys snk;

            for (unsigned i = 0; i < nn.size(); i++)
            {
                switch (nn[i].source)
                {
                    case NEW_PUBLIC:
                    case NEW_NODE:
                        snk.add(nn[i].nodekey, nn[i].nodehandle, tn, true);
                        break;

                    case NEW_UPLOAD:
                        snk.add(nn[i].nodekey, nn[i].nodehandle, tn, true, nn[i].uploadtoken.data(), (int)sizeof nn[i].uploadtoken);
                        break;
                }
            }

            snk.get(this, true);
        }
    }

    tag = ctag;
}

// add new nodes and handle->node handle mapping
void CommandPutNodes::removePendingDBRecordsAndTempFiles()
{
    pendingdbid_map::iterator it = client->pendingtcids.find(tag);
    if (it != client->pendingtcids.end())
    {
        if (client->tctable)
        {
            client->mTctableRequestCommitter->beginOnce();
            vector<uint32_t> &ids = it->second;
            for (unsigned int i = 0; i < ids.size(); i++)
            {
                if (ids[i])
                {
                    client->tctable->del(ids[i]);
                }
            }
        }
        client->pendingtcids.erase(it);
    }
    pendingfiles_map::iterator pit = client->pendingfiles.find(tag);
    if (pit != client->pendingfiles.end())
    {
        vector<LocalPath> &pfs = pit->second;
        for (unsigned int i = 0; i < pfs.size(); i++)
        {
            client->fsaccess->unlinklocal(pfs[i]);
        }
        client->pendingfiles.erase(pit);
    }
}

void CommandPutNodes::performAppCallback(Error e,
                                         vector<NewNode>& newnodes,
                                         bool targetOverride,
                                         const map<string, string>& fileHandles)
{
    if (mResultFunction)
        mResultFunction(e, type, newnodes, targetOverride, tag, fileHandles);
    else
        client->app->putnodes_result(e, type, newnodes, targetOverride, tag, fileHandles);
}

bool CommandPutNodes::procresult(Result r, JSON& json)
{
    removePendingDBRecordsAndTempFiles();

    if (r.wasErrorOrOK())
    {
        LOG_debug << "Putnodes error " << r.errorOrOK();
        error e = r.errorOrOK();
        if (e == API_EOVERQUOTA)
        {
            if (client->isPrivateNode(targethandle))
            {
                client->activateoverquota(0, false);
            }
        }

        performAppCallback((e ? e : API_EINTERNAL), nn, false, {});
        return true;
    }

    Error newNodeError(API_OK);
    map<string, string> fileHandles;

    for (;;)
    {
        switch (json.getnameid())
        {
            case 'e':
            {
                // This element is a sparse array indicating the nodes that failed, and the
                // corresponding error codes.
                // If the first three nodes failed, the response would be e.g. [-9,-9,-9].
                // Success is [].
                // If the second and third node failed, the response would change to
                // {"1":-9,"2":-9}.

                bool hasJsonArray = json.enterarray();
                if (!hasJsonArray && !json.enterobject())
                {
                    performAppCallback(API_EINTERNAL, nn, false, fileHandles);
                    return false;
                }

                unsigned arrayIndex = 0;
                for (;;)
                {
                    if (hasJsonArray)
                    {
                        if (*json.pos == ']')
                        {
                            json.leavearray();
                            break;
                        }

                        if (!json.isnumeric())
                        {
                            performAppCallback(API_EINTERNAL, nn, false, fileHandles);
                            return false;
                        }

                        assert(arrayIndex < nn.size());
                        if (arrayIndex < nn.size())
                        {
                            nn[arrayIndex].mError = error(json.getint());
                            if (nn[arrayIndex].mError != API_OK)
                            {
                                newNodeError = nn[arrayIndex].mError;
                                LOG_debug << "[CommandPutNodes] New Node failed with "
                                          << newNodeError << " [newnode index = " << arrayIndex
                                          << ", NodeHandle = " << nn[arrayIndex].nodeHandle()
                                          << "]";
                                assert(((nn[arrayIndex].mError != API_EKEY) ||
                                        !nn[arrayIndex].hasZeroKey()) &&
                                       "New Node which failed with API_EKEY has a zerokey!!!!");
                            }
                            arrayIndex++;
                        }
                    }
                    else
                    {
                        string index, errorCode;
                        if (json.storeobject(&index) && *json.pos == ':')
                        {
                            ++json.pos;
                            if (json.storeobject(&errorCode))
                            {
                                arrayIndex = unsigned(atoi(index.c_str()));
                                if (arrayIndex < nn.size())
                                {
                                    nn[arrayIndex].mError = error(atoi(errorCode.c_str()));
                                    continue;
                                }
                            }
                        }

                        if (!json.leaveobject())
                        {
                            performAppCallback(API_EINTERNAL, nn, false, fileHandles);
                            return false;
                        }
                        break;
                    }
                }

                break;
            }

            case MAKENAMEID2('f', 'h'): // ["drEyXKKB:C6-OsdmLX2U","<nodehandle>:<fileid>"]
                if (!json.enterarray())
                {
                    performAppCallback(API_EINTERNAL, nn, false, fileHandles);
                    return false;
                }

                for (std::string temp; json.storeobject(&temp);)
                {
                    auto separator = temp.find(':');
                    if (separator != string::npos)
                    {
                        fileHandles[temp.substr(0, separator)] = temp.substr(separator + 1);
                    }
                }

                if (!json.leavearray())
                {
                    performAppCallback(API_EINTERNAL, nn, false, fileHandles);
                    return false;
                }
                break;

            default:
                if (!json.storeobject())
                {
                    performAppCallback(API_EINTERNAL, nn, false, fileHandles);
                    return false;
                }
                break;

            case EOO:
#ifdef DEBUG
                if (type != USER_HANDLE)
                {
                    for (auto& n: nn)
                    {
                        // double check we got a node, or know the error why it didn't get created
                        if (!((n.added && n.mAddedHandle != UNDEF && !n.mError) ||
                              (!n.added && n.mAddedHandle == UNDEF && n.mError)))
                        {
                            assert(false);
                        }
                    }
                }
#endif

                // when the target has been removed, the API automatically adds the new node/s
                // into the rubbish bin
                shared_ptr<Node> tempNode =
                    !nn.empty() ? client->nodebyhandle(nn.front().mAddedHandle) : nullptr;
                bool targetOverride =
                    (tempNode.get() &&
                     NodeHandle().set6byte(tempNode->parenthandle) != targethandle);

                const Error& finalStatus =
                    emptyResponse ?
                        ((newNodeError != API_OK) ?
                             // Add last new node error if there is any, otherwise API_ENOENT
                             newNodeError :
                             Error(API_ENOENT)) :
                        Error(API_OK);

                performAppCallback(finalStatus, nn, targetOverride, fileHandles);
                return true;
        }
    }
}


CommandMoveNode::CommandMoveNode(MegaClient* client, std::shared_ptr<Node> n, std::shared_ptr<Node> t, syncdel_t csyncdel, NodeHandle prevparent, Completion&& c, bool canChangeVault)
{
    h = n->nodeHandle();
    syncdel = csyncdel;
    np = t->nodeHandle();
    pp = prevparent;
    syncop = !pp.isUndef();
    mCanChangeVault = canChangeVault;

    cmd("m");

    // Special case for Move, we do set the 'i' field.
    // This is needed for backward compatibility, old versions used memcmp to detect if a 'd' actionpacket was followed by a 't'  actionpacket with the same 'i' (ie, a move)
    // Additionally the servers can't deliver `st` in that packet for the same reason.  And of course we will not ignore this `t` packet, despite setting 'i'.
    notself(client);

    if (mCanChangeVault)
    {
        arg("vw", 1);
    }

    arg("n", h);
    arg("t", t->nodeHandle());
    assert(t->type != FILENODE);

    TreeProcShareKeys tpsk(t, true);
    client->proctree(n, &tpsk);
    tpsk.get(this);

    tag = client->reqtag;
    completion = move(c);
}

bool CommandMoveNode::procresult(Result r, JSON&)
{
    if (r.wasErrorOrOK())
    {
        if (r.wasError(API_EOVERQUOTA))
        {
            client->activateoverquota(0, false);
        }

        // Movement of shares and pending shares into Rubbish should remove them
        if (r.wasStrictlyError() && syncdel == SYNCDEL_NONE)
        {
            client->sendevent(99439, "Unexpected move error", 0);
        }
    }

    if (completion) completion(h, r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandDelNode::CommandDelNode(MegaClient*,
                               NodeHandle th,
                               bool keepversions,
                               int cmdtag,
                               std::function<void(NodeHandle, Error)>&& f,
                               bool canChangeVault):
    mResultFunction(std::move(f))
{
    cmd("d");

    arg("n", (byte*)&th, MegaClient::NODEHANDLE);

    if (keepversions)
    {
        arg("v", 1);
    }

    if (canChangeVault)
    {
        arg("vw", 1);
    }

    h = th;
    tag = cmdtag;
}

bool CommandDelNode::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (mResultFunction)    mResultFunction(h, r.errorOrOK());
        else         client->app->unlink_result(h.as8byte(), r.errorOrOK());
        return true;
    }
    else
    {
        error e = API_OK;

        for (;;)
        {
            switch (json.getnameid())
            {
                case 'r':
                    if (json.enterarray())
                    {
                        if(json.isnumeric())
                        {
                            e = (error)json.getint();
                        }

                        json.leavearray();
                    }
                    break;

                case EOO:
                    if (mResultFunction)    mResultFunction(h, e);
                    else         client->app->unlink_result(h.as8byte(), e);
                    return true;

                default:
                    if (!json.storeobject())
                    {
                        if (mResultFunction)    mResultFunction(h, API_EINTERNAL);
                        else         client->app->unlink_result(h.as8byte(), API_EINTERNAL);
                        return false;
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

bool CommandDelVersions::procresult(Result r, JSON&)
{
    client->app->unlinkversions_result(r.errorOrOK());
    return r.wasErrorOrOK();
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

bool CommandKillSessions::procresult(Result r, JSON&)
{
    client->app->sessions_killed(h, r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandLogout::CommandLogout(MegaClient *client, Completion completion, bool keepSyncConfigsFile)
  : mCompletion(std::move(completion))
  , mKeepSyncConfigsFile(keepSyncConfigsFile)
{
    cmd("sml");

    batchSeparately = true;

    tag = client->reqtag;
}

const char* CommandLogout::getJSON(MegaClient* client)
{
    if (!incrementedCount)
    {
        // only set this once we are about to send the command, in case there are others ahead of it in the queue
        client->loggingout++;
        // only set it once in case of retries due to -3.
        incrementedCount = true;
    }
    return jsonWriter.getstring().c_str();
}

bool CommandLogout::procresult(Result r, JSON&)
{
    assert(r.wasErrorOrOK());
    if (client->loggingout > 0)
    {
        client->loggingout--;
    }
    if(r.wasError(API_OK))
    {
        // We are logged out, but we mustn't call locallogout until we exit this call
        // stack for processing CS batches, as it deletes data currently in use.
        Completion completion = std::move(mCompletion);
        bool keepSyncConfigsFile = mKeepSyncConfigsFile;
        LOG_debug << "setting mOnCSCompletion for final logout processing";  // track possible lack of logout callbacks
        client->mOnCSCompletion = [=](MegaClient* client){
            client->locallogout(true, keepSyncConfigsFile);
            completion(API_OK);
        };
    }
    else
    {
        mCompletion(r.errorOrOK());
    }
    return true;
}

CommandPrelogin::CommandPrelogin(MegaClient* client, Completion completion, const char* email)
  : mCompletion(std::move(completion))
  , email(email)
{
    // Sanity.
    assert(mCompletion);

    cmd("us0");
    arg("user", email);
    batchSeparately = true;  // in case the account is blocked (we need to get a sid so we can issue whyamiblocked)

    tag = client->reqtag;
}

bool CommandPrelogin::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        mCompletion(0, NULL, NULL, r.errorOrOK());
        return true;
    }

    assert(r.hasJsonObject());
    int v = 0;
    string salt;
    for (;;)
    {
        switch (json.getnameid())
        {
            case 'v':
                v = int(json.getint());
                break;
            case 's':
                json.storeobject(&salt);
                break;
            case EOO:
                if (v == 0)
                {
                    LOG_err << "No version returned";
                    mCompletion(0, NULL, NULL, API_EINTERNAL);
                }
                else if (v > 2)
                {
                    LOG_err << "Version of account not supported";
                    mCompletion(0, NULL, NULL, API_EINTERNAL);
                }
                else if (v == 2 && !salt.size())
                {
                    LOG_err << "No salt returned";
                    mCompletion(0, NULL, NULL, API_EINTERNAL);
                }
                else
                {
                    client->accountversion = v;
                    Base64::atob(salt, client->accountsalt);
                    mCompletion(v, &email, &salt, API_OK);
                }
                return true;
            default:
                if (!json.storeobject())
                {
                    mCompletion(0, NULL, NULL, API_EINTERNAL);
                    return false;
                }
        }
    }
}

// login request with user e-mail address and user hash
CommandLogin::CommandLogin(MegaClient* client,
                           Completion completion,
                           const char* email,
                           const byte *emailhash,
                           int emailhashsize,
                           const byte *sessionkey,
                           int csessionversion,
                           const char *pin)
  : mCompletion(std::move(completion))
{
    // Sanity.
    assert(mCompletion);

    cmd("us");
    batchSeparately = true;  // in case the account is blocked (we need to get a sid so we can issue whyamiblocked)

    // are we just performing a session validation?
    checksession = !email;
    sessionversion = csessionversion;

    if (!checksession)
    {
        arg("user", email);
        arg("uh", emailhash, emailhashsize);
        if (pin)
        {
            arg("mfa", pin);
        }
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

    string deviceIdHash = client->getDeviceidHash();
    if (!deviceIdHash.empty())
    {
        arg("si", deviceIdHash.c_str());
    }
    else
    {
        client->sendevent(99454, "Device-id not available at login");
    }

    tag = client->reqtag;
}

// process login result
bool CommandLogin::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->loginResult(std::move(mCompletion), r.errorOrOK());
        return true;
    }

    assert(r.hasJsonObject());
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
        switch (json.getnameid())
        {
            case 'k':
                len_k = json.storebinary(hash, sizeof hash);
                break;

            case 'u':
                me = json.gethandle(MegaClient::USERHANDLE);
                break;

            case MAKENAMEID3('s', 'e', 'k'):
                len_sek = json.storebinary(sek, sizeof sek);
                break;

            case MAKENAMEID4('t', 's', 'i', 'd'):
                len_tsid = json.storebinary(sidbuf, sizeof sidbuf);
                break;

            case MAKENAMEID4('c', 's', 'i', 'd'):
                len_csid = json.storebinary(sidbuf, sizeof sidbuf);
                break;

            case MAKENAMEID5('p', 'r', 'i', 'v', 'k'):
                len_privk = json.storebinary(privkbuf, sizeof privkbuf);
                break;

            case MAKENAMEID2('f', 'a'):
                fa = json.getbool();
                break;

            case MAKENAMEID3('a', 'c', 'h'):
                ach = json.getbool();
                break;

            case MAKENAMEID2('s', 'n'):
                if (!json.getint())
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
                        client->loginResult(std::move(mCompletion), API_EINTERNAL);
                        return true;
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
                        client->sctable.reset();
                        client->mNodeManager.reset();
                        client->pendingsccommit = false;
                        client->cachedscsn = UNDEF;
                        client->dbaccess->currentDbVersion = DbAccess::DB_VERSION;

                        client->sendevent(99404, "Local DB upgrade granted", 0);
                    }
                }

                if (len_sek)
                {
                    if (len_sek != SymmCipher::KEYLENGTH)
                    {
                        client->loginResult(std::move(mCompletion), API_EINTERNAL);
                        return true;
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
                    client->sid.assign((const char *)sidbuf, MegaClient::SIDLEN);

                    // account does not have an RSA keypair set: verify
                    // password using symmetric challenge
                    if (!client->checktsid(sidbuf, len_tsid))
                    {
                        LOG_warn << "Error checking tsid";
                        client->loginResult(std::move(mCompletion), API_ENOENT);
                        return true;
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
                        if (!checksession)
                        {
                            client->loginResult(std::move(mCompletion), API_EINTERNAL);
                            return true;
                        }
                        else if (!client->ephemeralSessionPlusPlus && !client->ephemeralSession)
                        {
                            // logging in with tsid to an account without a RSA keypair
                            LOG_info << "Generating and adding missing RSA keypair";
                            client->setkeypair();
                        }
                    }
                    else
                    {
                        // decrypt and set private key
                        client->key.ecb_decrypt(privkbuf, len_privk);
                        client->mPrivKey.resize(AsymmCipher::MAXKEYLENGTH * 2);
                        client->mPrivKey.resize(Base64::btoa(privkbuf, len_privk, (char *)client->mPrivKey.data()));

                        if (!client->asymkey.setkey(AsymmCipher::PRIVKEY, privkbuf, len_privk))
                        {
                            LOG_warn << "Error checking private key";
                            client->loginResult(std::move(mCompletion), API_ENOENT);
                            return true;
                        }
                    }

                    if (!checksession)
                    {
                        if (len_csid < 32)
                        {
                            client->loginResult(std::move(mCompletion), API_EINTERNAL);
                            return true;
                        }

                        byte buf[sizeof me];

                        // decrypt and set session ID for subsequent API communication
                        if (!client->asymkey.decrypt(sidbuf, len_csid, sidbuf, MegaClient::SIDLEN)
                                // additionally, check that the user's handle included in the session matches the own user's handle (me)
                                || (Base64::atob((char*)sidbuf + SymmCipher::KEYLENGTH, buf, sizeof buf) != sizeof buf)
                                || (me != MemAccess::get<handle>((const char*)buf)))
                        {
                            client->loginResult(std::move(mCompletion), API_EINTERNAL);
                            return true;
                        }

                        client->sid.assign((const char *)sidbuf, MegaClient::SIDLEN);
                    }
                }

                client->me = me;
                client->uid = Base64Str<MegaClient::USERHANDLE>(client->me);
                client->achievements_enabled = ach;
                // Force to create own user
                client->finduser(me, 1);

                if (len_sek)
                {
                    client->sessionkey.assign((const char *)sek, sizeof(sek));
                }

                // Initialize FUSE subsystem.
                client->mFuseClientAdapter.initialize();
                client->mFuseService.initialize();

                client->openStatusTable(true);
                client->loadJourneyIdCacheValues();

                { // scope for local variable
                    MegaClient* cl = client; // make a copy, because 'this' will be gone by the time lambda will execute
                    client->loginResult(std::move(mCompletion), API_OK, [cl]()
                        {
                            cl->getaccountdetails(std::make_shared<AccountDetails>(), false, false, true, false, false, false);
                        }
                    );
                }

                return true;

            default:
                if (!json.storeobject())
                {
                    client->loginResult(std::move(mCompletion), API_EINTERNAL);
                    return false;
                }
        }
    }
}

// add/remove share; include node share keys if new share
CommandSetShare::CommandSetShare(MegaClient* client, std::shared_ptr<Node> n, User* u, accesslevel_t a, bool newshare, const char* msg, bool writable, const char* personal_representation, int ctag, std::function<void(Error, bool writable)> f)
{
    byte auth[SymmCipher::BLOCKSIZE];
    byte key[SymmCipher::KEYLENGTH];

    tag = ctag;

    sh = n->nodehandle;
    access = a;
    mWritable = writable;

    completion = std::move(f);
    assert(completion);

    mSeqtagArray = true;
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
        // TODO: dummy key/handleauth - FIXME: remove when the server allows it
        memset(key, 0, sizeof key);
        memset(auth, 0, sizeof auth);
        arg("ok", key, sizeof key);
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
    }

    endobject();
    endarray();

    // only for a fresh share: add cr element with all node keys encrypted to
    // the share key
    if (newshare)
    {
        // the new share's nodekeys for this user: generate node list
        TreeProcShareKeys tpsk(n, false);
        client->proctree(n, &tpsk);
        tpsk.get(this);
    }
}

// process user element (email/handle pairs)
bool CommandSetShare::procuserresult(MegaClient* client, JSON& json)
{
    while (json.enterobject())
    {
        handle uh = UNDEF;
        const char* m = NULL;

        for (;;)
        {
            switch (json.getnameid())
            {
                case 'u':
                    uh = json.gethandle(MegaClient::USERHANDLE);
                    break;

                case 'm':
                    m = json.getvalue();
                    break;

                case EOO:
                    if (!ISUNDEF(uh) && m)
                    {
                        client->mapuser(uh, m);
                    }
                    return true;

                default:
                    if (!json.storeobject())
                    {
                        return false;
                    }
            }
        }
    }

    return false;
}

// process result of share addition/modification
bool CommandSetShare::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        completion(r.errorOrOK(), mWritable);
        return true;
    }

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID2('o', 'k'):  // an owner key response will only
                                         // occur if the same share was created
                                         // with a different key
            {
                // if the API has a different key, the only legit scenario is that
                // such owner key is invalid (ie. "AAAAA..."), set by a client with
                // secure=true
                completion(API_EKEY, mWritable);
                return true;
            }

            case 'u':   // user/handle confirmation
                if (json.enterarray())
                {
                    while (procuserresult(client, json))
                    {}
                    json.leavearray();
                }
                break;

            case 'r':
                if (json.enterarray())
                {
                    while (json.isnumeric())
                    {
                        // intermediate result updates, not final completion
                        // we used to call share_result but it wasn't used
                        json.getint();
                    }

                    json.leavearray();
                }
                break;

            case MAKENAMEID3('s', 'n', 'k'):
                client->procsnk(&json);
                break;

            case MAKENAMEID3('s', 'u', 'k'):
                client->procsuk(&json);
                break;

            case MAKENAMEID2('c', 'r'):
                client->proccr(&json);
                break;

            case EOO:
                completion(API_OK, mWritable);
                return true;

            default:
                if (!json.storeobject())
                {
                    completion(API_EINTERNAL, mWritable);
                    return false;
                }
        }
    }
}

CommandPendingKeys::CommandPendingKeys(MegaClient *client, CommandPendingKeysReadCompletion completion)
{
    // Assume we've been passed a completion function.
    mReadCompletion = std::move(completion);

    cmd("pk");

    tag = client->reqtag;
}

CommandPendingKeys::CommandPendingKeys(MegaClient *client, std::string lastcompleted, std::function<void (Error)> completion)
{
    // Assume we've been passed a completion function.
    mCompletion = std::move(completion);

    cmd("pk");
    arg("d", lastcompleted.c_str());

    tag = client->reqtag;
}

CommandPendingKeys::CommandPendingKeys(MegaClient *client, handle user, handle share, byte *key, std::function<void (Error)> completion)
{
    // Assume we've been passed a completion function.
    mCompletion = std::move(completion);

    cmd("pk");
    arg("u", (byte*)&user, MegaClient::USERHANDLE);
    arg("h", (byte*)&share, MegaClient::NODEHANDLE);
    arg("k", key, SymmCipher::KEYLENGTH);

    tag = client->reqtag;
}

bool CommandPendingKeys::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (mReadCompletion)
        {
            mReadCompletion(r.errorOrOK(), std::string(), nullptr);
            return true;
        }

        mCompletion(r.errorOrOK());
        return true;
    }

    if (mCompletion)
    {
        mCompletion(API_EINTERNAL);
        return false;
    }

    // Response format:
    // {"peeruserhandle1":{"sharehandle1":"key1","sharehandle2":"key2"},
    //  "peeruserhandle2":{"sharehandle3":"key3"},...
    //  "d":"lastcompleted"} (lastcompleted is a base64 string like oMl7nfj67Jw)

    // maps user's handles to a map of share's handles : share's keys
    std::shared_ptr<map<handle, map<handle, string>>> keys = std::make_shared<map<handle, map<handle, string>>>();
    std::string lastcompleted;

    std::string name;
    name = json.getname();
    while (name.size())
    {
        if (name == "d")
        {
            json.storeobject(&lastcompleted);
            name = json.getname();
            continue;
        }

        handle userhandle = 0;
        Base64::atob(name.c_str(), (byte*)&userhandle, MegaClient::USERHANDLE);
        if (!json.enterobject())
        {
            mReadCompletion(API_EINTERNAL, std::string(), nullptr);
            return false;
        }

        handle sharehandle;
        while (!ISUNDEF(sharehandle = json.gethandle()))
        {
            string sharekey;
            JSON::copystring(&sharekey, json.getvalue());
            (*keys)[userhandle][sharehandle] = Base64::atob(sharekey);
        }

        json.leaveobject();
        name = json.getname();
    }

    mReadCompletion(API_OK, lastcompleted, keys);
    return true;
}

CommandSetPendingContact::CommandSetPendingContact(MegaClient* client, const char* temail, opcactions_t action, const char* msg, const char* oemail, handle contactLink, Completion completion)
{
    mSeqtagArray = true;
    
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
            if (!ISUNDEF(contactLink))
            {
                arg("cl", (byte*)&contactLink, MegaClient::CONTACTLINKHANDLE);
            }
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

    // Assume we've been passed a completion function.
    mCompletion = std::move(completion);
}

bool CommandSetPendingContact::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        handle pcrhandle = UNDEF;
        if (r.wasError(API_OK)) // response for delete & remind actions is always numeric
        {
            // find the PCR by email
            PendingContactRequest *pcr = NULL;
            for (handlepcr_map::iterator it = client->pcrindex.begin();
                 it != client->pcrindex.end(); it++)
            {
                if (it->second->targetemail == temail)
                {
                    pcr = it->second.get();
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
                sharedNode_vector nodes = client->mNodeManager.getNodesWithPendingOutShares();
                for (auto& n : nodes)
                {
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

        doComplete(pcrhandle, r.errorOrOK(), this->action);
        return true;
    }

    // if the PCR has been added, the response contains full details
    handle p = UNDEF;
    m_time_t ts = 0;
    m_time_t uts = 0;
    const char *eValue = NULL;
    const char *m = NULL;
    const char *msg = NULL;
    PendingContactRequest *pcr = NULL;
    for (;;)
    {
        switch (json.getnameid())
        {
            case 'p':
                p = json.gethandle(MegaClient::PCRHANDLE);
                break;
            case 'm':
                m = json.getvalue();
                break;
            case 'e':
                eValue = json.getvalue();
                break;
            case MAKENAMEID3('m', 's', 'g'):
                msg = json.getvalue();
                break;
            case MAKENAMEID2('t', 's'):
                ts = json.getint();
                break;
            case MAKENAMEID3('u', 't', 's'):
                uts = json.getint();
                break;
            case EOO:
                if (ISUNDEF(p))
                {
                    LOG_err << "Error in CommandSetPendingContact. Undefined handle";
                    doComplete(UNDEF, API_EINTERNAL, this->action);
                    return true;
                }

                if (action != OPCA_ADD || !eValue || !m || ts == 0 || uts == 0)
                {
                    LOG_err << "Error in CommandSetPendingContact. Wrong parameters";
                    doComplete(UNDEF, API_EINTERNAL, this->action);
                    return true;
                }

                pcr = new PendingContactRequest(p, eValue, m, ts, uts, msg, true);
                client->mappcr(p, unique_ptr<PendingContactRequest>(pcr));

                client->notifypcr(pcr);
                doComplete(p, API_OK, this->action);
                return true;

            default:
                if (!json.storeobject())
                {
                    LOG_err << "Error in CommandSetPendingContact. Parse error";
                    doComplete(UNDEF, API_EINTERNAL, this->action);
                    return false;
                }
        }
    }
}

void CommandSetPendingContact::doComplete(handle handle, error result, opcactions_t actions)
{
    if (!mCompletion)
        return client->app->setpcr_result(handle, result, actions);

    mCompletion(handle, result, actions);
}

CommandUpdatePendingContact::CommandUpdatePendingContact(MegaClient* client, handle p, ipcactions_t action, Completion completion)
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

    // Assume we've been provided a completion function.
    mCompletion = std::move(completion);
}

bool CommandUpdatePendingContact::procresult(Result r, JSON&)
{
    doComplete(r.errorOrOK(), this->action);

    return r.wasErrorOrOK();
}


void CommandUpdatePendingContact::doComplete(error result, ipcactions_t actions)
{
    if (!mCompletion)
        return client->app->updatepcr_result(result, actions);

    mCompletion(result, actions);
}

CommandEnumerateQuotaItems::CommandEnumerateQuotaItems(MegaClient* client)
{
    cmd("utqa");
    arg("nf", 3);
    arg("b", 1);    // support for Business accounts
    arg("p", 1);    // support for Pro Flexi
    arg("ft", 1);   // support for Feature plans
    tag = client->reqtag;
}

bool CommandEnumerateQuotaItems::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->enumeratequotaitems_result(r.errorOrOK());
        return true;
    }

    string currency; // common for all plans, populated from `l` object

    while (json.enterobject())
    {
        handle product = UNDEF;
        int prolevel = -1, gbstorage = -1, gbtransfer = -1, months = -1, type = -1;
        unsigned amount = 0, amountMonth = 0, localPrice = 0;
        unsigned int testCategory = CommandEnumerateQuotaItems::INVALID_TEST_CATEGORY; // Bitmap. Bit 0 set (int value 1) is standard plan, other bits are defined by API.
        unsigned int trialDays = CommandEnumerateQuotaItems::NO_TRIAL_DAYS;
        string description;
        map<string, uint32_t> features;
        string ios_id;
        string android_id;

        unique_ptr<BusinessPlan> bizPlan;
        unique_ptr<CurrencyData> currencyData;

        bool finished = false;
        bool readingL = false;
        const char* buf = nullptr;
        while (!finished)
        {
            buf = nullptr;

            switch (json.getnameid())
            {
                case MAKENAMEID1('l'):  // currency localization
                {
                    if (!json.enterobject())
                    {
                        LOG_err << "Failed to parse Enumerate-quota-items response, `l` object";
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return false;
                    }

                    currencyData = std::make_unique<CurrencyData>();
                    readingL = true;

                    while (!finished)
                    {
                        buf = nullptr;

                        switch(json.getnameid())
                        {
                            case MAKENAMEID1('c'):  // currency, ie. EUR
                                buf = json.getvalue();
                                JSON::copystring(&currencyData->currencyName, buf);
                                currency = currencyData->currencyName;
                                break;
                            case MAKENAMEID2('c', 's'): // currency symbol, ie. €
                                buf = json.getvalue();
                                JSON::copystring(&currencyData->currencySymbol, buf);
                                break;
                            case MAKENAMEID2('l', 'c'):  // local currency, ie. NZD
                                buf = json.getvalue();
                                JSON::copystring(&currencyData->localCurrencyName, buf);
                                break;
                            case MAKENAMEID3('l', 'c', 's'):    // local currency symbol, ie. $
                                buf = json.getvalue();
                                JSON::copystring(&currencyData->localCurrencySymbol, buf);
                                break;
                            case EOO:
                                // sanity checks for received data
                                if (currencyData->currencyName.empty() || currencyData->currencySymbol.empty())
                                {
                                    LOG_err << "Failed to parse Enumerate-quota-items response, `l` data";
                                    client->app->enumeratequotaitems_result(API_EINTERNAL);
                                    return true;
                                }

                                finished = true;    // exits from the outer loop too
                                json.leaveobject(); // 'l' object
                                break;
                            default:
                                if (!json.storeobject())
                                {
                                    LOG_err << "Failed to parse Enumerate-quota-items response, store `l` data";
                                    client->app->enumeratequotaitems_result(API_EINTERNAL);
                                    return false;
                                }
                                break;
                        }
                    }
                    break;
                }
                case MAKENAMEID2('i', 't'): // 0 -> for all Pro level plans; 1 -> for Business plan; 2 -> for Feature plan
                    type = static_cast<int>(json.getint());
                    break;
//                case MAKENAMEID2('i', 'b'): // for "it":1 (business plans), 0 -> Pro Flexi; 1 -> Business plan
//                    {
//                        bool isProFlexi = json.getbool();
//                    }
//                    break;
                case MAKENAMEID2('i', 'd'):
                    product = json.gethandle(8);
                    break;
                case MAKENAMEID2('a', 'l'):
                    prolevel = static_cast<int>(json.getint());
                    break;
                case 's':
                    gbstorage = static_cast<int>(json.getint());
                    break;
                case 't':
                    gbtransfer = static_cast<int>(json.getint());
                    break;
                case 'm':
                    months = static_cast<int>(json.getint());
                    break;
                case 'p':   // price (in cents)
                    amount = static_cast<unsigned>(json.getint());
                    break;
                case 'd':
                    buf = json.getvalue();
                    JSON::copystring(&description, buf);
                    break;
                case 'f': // e.g. "f": { "vpn": 1 }
                {
                    if (!json.enterobject())
                    {
                        LOG_err << "Failed to parse Enumerate-quota-items response, enter `f` object";
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return false;
                    }
                    string key, value;
                    while (json.storeKeyValueFromObject(key, value))
                    {
                        features[key] = static_cast<unsigned>(std::stoul(value));
                    }
                    if (!json.leaveobject())
                    {
                        LOG_err << "Failed to parse Enumerate-quota-items response, leave `f` object";
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return false;
                    }
                    break;
                }
                case MAKENAMEID3('i', 'o', 's'):
                    buf = json.getvalue();
                    JSON::copystring(&ios_id, buf);
                    break;
                case MAKENAMEID6('g', 'o', 'o', 'g', 'l', 'e'):
                    buf = json.getvalue();
                    JSON::copystring(&android_id, buf);
                    break;
                case MAKENAMEID3('m', 'b', 'p'):    // monthly price (in cents)
                    amountMonth = static_cast<unsigned>(json.getint());
                    break;
                case MAKENAMEID2('l', 'p'): // local price (in cents)
                    localPrice = static_cast<unsigned>(json.getint());
                    break;
                case MAKENAMEID2('b', 'd'): // BusinessPlan
                {
                    if (!json.enterobject())
                    {
                        LOG_err << "Failed to parse Enumerate-quota-items response, `bd` object";
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return false;
                    }

                    bizPlan = std::make_unique<BusinessPlan>();

                    bool readingBd = true;
                    while (readingBd)
                    {
                        switch (json.getnameid())
                        {
                            case MAKENAMEID2('b', 'a'): // base (-1 means unlimited storage or transfer)
                            {
                                if (!json.enterobject())
                                {
                                    LOG_err << "Failed to parse Enumerate-quota-items response, `ba` object";
                                    client->app->enumeratequotaitems_result(API_EINTERNAL);
                                    return false;
                                }

                                bool readingBa = true;
                                while (readingBa)
                                {
                                    switch (json.getnameid())
                                    {
                                        case 's':
                                            bizPlan->gbStoragePerUser = static_cast<int>(json.getint());
                                            break;
                                        case 't':
                                            bizPlan->gbTransferPerUser = static_cast<int>(json.getint());
                                            break;
                                        case EOO:
                                            readingBa = false;
                                            break;
                                        default:
                                            if (!json.storeobject())
                                            {
                                                LOG_err << "Failed to parse Enumerate-quota-items response, `ba` data";
                                                client->app->enumeratequotaitems_result(API_EINTERNAL);
                                                return false;
                                            }
                                            break;
                                    }
                                }
                                json.leaveobject();
                                break;
                            }
                            case MAKENAMEID2('u', 's'):   // price per user
                            {
                                if (!json.enterobject())
                                {
                                    LOG_err << "Failed to parse Enumerate-quota-items response, `us` object";
                                    client->app->enumeratequotaitems_result(API_EINTERNAL);
                                    return false;
                                }

                                bool readingUs = true;
                                while (readingUs)
                                {
                                    switch (json.getnameid())
                                    {
                                        case 'p':
                                            bizPlan->pricePerUser = static_cast<unsigned>(json.getint());
                                            break;
                                        case MAKENAMEID2('l', 'p'):
                                            bizPlan->localPricePerUser = static_cast<unsigned>(json.getint());
                                            break;
                                        case EOO:
                                            readingUs = false;
                                            break;
                                        default:
                                            if (!json.storeobject())
                                            {
                                                LOG_err << "Failed to parse Enumerate-quota-items response, `us` data";
                                                client->app->enumeratequotaitems_result(API_EINTERNAL);
                                                return false;
                                            }
                                            break;
                                    }
                                }
                                json.leaveobject();
                                break;
                            }
                            case MAKENAMEID3('s', 't', 'o'):   // storage block
                            {
                                if (!json.enterobject())
                                {
                                    LOG_err << "Failed to parse Enumerate-quota-items response, `sto` object";
                                    client->app->enumeratequotaitems_result(API_EINTERNAL);
                                    return false;
                                }

                                bool readingSto = true;
                                while (readingSto)
                                {
                                    switch (json.getnameid())
                                    {
                                        case 's':
                                            bizPlan->gbPerStorage = static_cast<int>(json.getint());
                                            break;
                                        case 'p':
                                            bizPlan->pricePerStorage = static_cast<unsigned>(json.getint());
                                            break;
                                        case MAKENAMEID2('l', 'p'):
                                            bizPlan->localPricePerStorage = static_cast<unsigned>(json.getint());
                                            break;
                                        case EOO:
                                            readingSto = false;
                                            break;
                                        default:
                                            if (!json.storeobject())
                                            {
                                                LOG_err << "Failed to parse Enumerate-quota-items response, `sto` data";
                                                client->app->enumeratequotaitems_result(API_EINTERNAL);
                                                return false;
                                            }
                                            break;
                                    }
                                }
                                json.leaveobject();
                                break;
                            }
                            case MAKENAMEID4('t', 'r', 'n', 's'):   // transfer block
                            {
                                if (!json.enterobject())
                                {
                                    LOG_err << "Failed to parse Enumerate-quota-items response, `trns` object";
                                    client->app->enumeratequotaitems_result(API_EINTERNAL);
                                    return false;
                                }

                                bool readingTrns = true;
                                while (readingTrns)
                                {
                                    switch (json.getnameid())
                                    {
                                        case 't':
                                            bizPlan->gbPerTransfer = static_cast<int>(json.getint());
                                            break;
                                        case 'p':
                                            bizPlan->pricePerTransfer = static_cast<unsigned>(json.getint());
                                            break;
                                        case MAKENAMEID2('l', 'p'):
                                            bizPlan->localPricePerTransfer = static_cast<unsigned>(json.getint());
                                            break;
                                        case EOO:
                                            readingTrns = false;
                                            break;
                                        default:
                                            if (!json.storeobject())
                                            {
                                                LOG_err << "Failed to parse Enumerate-quota-items response, `sto` data";
                                                client->app->enumeratequotaitems_result(API_EINTERNAL);
                                                return false;
                                            }
                                            break;
                                    }
                                }
                                json.leaveobject();
                                break;
                            }
                            case MAKENAMEID4('m', 'i', 'n', 'u'):   // minimum number of user required to purchase
                                bizPlan->minUsers = static_cast<int>(json.getint());
                                break;
                            case EOO:
                                readingBd = false;
                                break;
                            default:
                                if (!json.storeobject())
                                {
                                    LOG_err << "Failed to parse Enumerate-quota-items response, `bd` object";
                                    client->app->enumeratequotaitems_result(API_EINTERNAL);
                                    return false;
                                }
                                break;
                        }
                    }
                    json.leaveobject();
                    break;
                }
                case MAKENAMEID2('t', 'c'):
                    testCategory = json.getuint32();
                    break;
                case MAKENAMEID5('t', 'r', 'i', 'a', 'l'):
                {
                    if (!json.enterobject())
                    {
                        LOG_err << "Failed to parse Enumerate-quota-items response,"
                                << "entering `trials` object";
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return false;
                    }
                    [[maybe_unused]] string key = json.getname();
                    assert(key == "days");
                    trialDays = json.getuint32();
                    if (!json.leaveobject())
                    {
                        LOG_err << "Failed to parse Enumerate-quota-items response,"
                                << "leaving `trials` object";
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return false;
                    }
                }
                break;
                case EOO:
                    if (type < 0
                            || ISUNDEF(product)
                            || (prolevel < 0)
                            || (months < 0)
                            || currency.empty()
                            || description.empty()
                            || testCategory == CommandEnumerateQuotaItems::INVALID_TEST_CATEGORY
                            // only available for Pro plans, not for Business
                            || (!type && gbstorage < 0)
                            || (!type && gbtransfer < 0)
                            || (!type && !amount)
                            || (!type && !amountMonth)
                            || (!type && ios_id.empty())
                            || (!type && android_id.empty())
                            // only available for Business plan(s)
                            || (type == 1 && !bizPlan))
                    {
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return true;
                    }

                    finished = true;
                    break;
                default:
                    if (!json.storeobject())
                    {
                        LOG_err << "Failed to parse Enumerate-quota-items response";
                        client->app->enumeratequotaitems_result(API_EINTERNAL);
                        return false;
                    }
                    break;
            }
        }   // end while(!finished)

        json.leaveobject();

        if (readingL)
        {
            // just read currency data, keep reading objects for each pro/business plan
            readingL = false;
            client->app->enumeratequotaitems_result(std::move(currencyData));
            continue;
        }
        else
        {
            const Product productData = {static_cast<unsigned int>(type),
                                         product,
                                         static_cast<unsigned int>(prolevel),
                                         gbstorage,
                                         gbtransfer,
                                         static_cast<unsigned int>(months),
                                         amount,
                                         amountMonth,
                                         localPrice,
                                         description.c_str(),
                                         std::move(features),
                                         ios_id.c_str(),
                                         android_id.c_str(),
                                         testCategory,
                                         std::move(bizPlan),
                                         trialDays};
            client->app->enumeratequotaitems_result(productData);
        }
    }

    client->app->enumeratequotaitems_result(API_OK);
    return true;
}

CommandPurchaseAddItem::CommandPurchaseAddItem(MegaClient* client, int itemclass,
                                               handle item, unsigned price,
                                               const char* currency, unsigned /*tax*/,
                                               const char* /*country*/, handle lph,
                                               int phtype, int64_t ts)
{
    string sprice;
    sprice.resize(128);
    snprintf(const_cast<char*>(sprice.data()), sprice.length(), "%.2f", price/100.0);
    replace( sprice.begin(), sprice.end(), ',', '.');
    cmd("uts");
    arg("it", itemclass);
    arg("si", (byte*)&item, 8);
    arg("p", sprice.c_str());
    arg("c", currency);
    if (!ISUNDEF(lph))
    {
        if (phtype == 0) // legacy mode
        {
            arg("aff", (byte*)&lph, MegaClient::NODEHANDLE);
        }
        else
        {
            beginobject("aff");
            arg("id", (byte*)&lph, MegaClient::NODEHANDLE);
            arg("ts", ts);
            arg("t", phtype);   // 1=affiliate id, 2=file/folder link, 3=chat link, 4=contact link
            endobject();
        }
    }

    tag = client->reqtag;

    //TODO: Complete this (tax? country?)
}

bool CommandPurchaseAddItem::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->additem_result(r.errorOrOK());
        return true;
    }

    handle item = json.gethandle(8);
    if (item != UNDEF)
    {
        client->purchase_basket.push_back(item);
        client->app->additem_result(API_OK);
        return true;
    }
    else
    {
        json.storeobject();
        client->app->additem_result(API_EINTERNAL);
        return false;
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

bool CommandPurchaseCheckout::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->checkout_result(NULL, r.errorOrOK());
        return true;
    }

    //Expected response: "EUR":{"res":X,"code":Y}}
    json.getnameid();
    if (!json.enterobject())
    {
        LOG_err << "Parse error (CommandPurchaseCheckout)";
        client->app->checkout_result(NULL, API_EINTERNAL);
        return false;
    }

    string errortype;
    Error e;
    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID3('r', 'e', 's'):
                if (json.isnumeric())
                {
                    e = (error)json.getint();
                }
                else
                {
                    json.storeobject(&errortype);
                    if (errortype == "S")
                    {
                        errortype.clear();
                        e = API_OK;
                    }
                }
                break;

            case MAKENAMEID4('c', 'o', 'd', 'e'):
                if (json.isnumeric())
                {
                    e = (error)json.getint();
                }
                else
                {
                    LOG_err << "Parse error in CommandPurchaseCheckout (code)";
                }
                break;
            case EOO:
                json.leaveobject();
                if (!errortype.size() || errortype == "FI" || e == API_OK)
                {
                    client->app->checkout_result(NULL, e);
                }
                else
                {
                    client->app->checkout_result(errortype.c_str(), e);
                }
                return true;
            default:
                if (!json.storeobject())
                {
                    client->app->checkout_result(NULL, API_EINTERNAL);
                    return false;
                }
        }
    }
}

CommandRemoveContact::CommandRemoveContact(MegaClient* client, const char* m, visibility_t show, Completion completion)
{
    mSeqtagArray = true;
    
    this->email = m ? m : "";
    this->v = show;

    cmd("ur2");
    arg("u", m);
    arg("l", (int)show);

    tag = client->reqtag;

    // Assume we've been given a completion function.
    mCompletion = std::move(completion);
}

bool CommandRemoveContact::procresult(Result r, JSON&)
{
    assert(r.hasJsonObject() || r.wasStrictlyError());

    if (r.hasJsonObject())
    {
        // the object contains (userhandle + email string) - caller will leaveobject() automatically

        if (User *u = client->finduser(email.c_str()))
        {
            u->show = v;
        }

        doComplete(API_OK);
        return true;
    }

    doComplete(r.errorOrOK());
    return r.wasErrorOrOK();
}

void CommandRemoveContact::doComplete(error result)
{
    if (!mCompletion)
        return client->app->removecontact_result(result);

    mCompletion(result);
}

CommandPutMultipleUAVer::CommandPutMultipleUAVer(MegaClient *client, const userattr_map *attrs, int ctag, std::function<void (Error)> completion)
{
    mSeqtagArray = true;

    this->attrs = *attrs;

    mCompletion = completion ? std::move(completion) :
        [this](Error e) {
            this->client->app->putua_result(e);
        };

    cmd("upv");

    for (userattr_map::const_iterator it = attrs->begin(); it != attrs->end(); it++)
    {
        attr_t type = it->first;

        beginarray(User::attr2string(type).c_str());

        element((const byte *) it->second.data(), int(it->second.size()));

        const UserAttribute* attribute = client->ownuser()->getAttribute(type);
        if (attribute && !attribute->version().empty())
        {
            element(attribute->version().c_str());
        }

        endarray();
    }

    tag = ctag;
}

bool CommandPutMultipleUAVer::procresult(Result r, JSON& json)
{
    if (r.hasJsonObject())
    {
        User *u = client->ownuser();
        for(;;)   // while there are more attrs to read...
        {

            if (*json.pos == '}')
            {
                client->notifyuser(u);
                mCompletion(API_OK);
                return true;
            }

            string key, value;
            if (!json.storeKeyValueFromObject(key, value))
            {
                break;
            }

            attr_t type = User::string2attr(key.c_str());
            userattr_map::iterator it = this->attrs.find(type);
            if (type == ATTR_UNKNOWN || value.empty() || (it == this->attrs.end()))
            {
                LOG_err << "Error in CommandPutMultipleUAVer. Undefined attribute or version: " << key;
                for (auto a : this->attrs) { LOG_err << " expected one of: " << User::attr2string(a.first); }
                break;
            }
            else
            {
                u->setAttribute(type, it->second, value);
                u->setTag(tag ? tag : -1);

                if (type == ATTR_KEYRING)
                {
                    TLVstore *tlvRecords = TLVstore::containerToTLVrecords(&attrs[type], &client->key);
                    if (tlvRecords)
                    {
                        string prEd255;
                        if (tlvRecords->get(EdDSA::TLV_KEY, prEd255) && prEd255.size() == EdDSA::SEED_KEY_LENGTH)
                        {
                            client->signkey = new EdDSA(client->rng, (unsigned char *) prEd255.data());
                        }

                        string prCu255;
                        if (tlvRecords->get(ECDH::TLV_KEY, prCu255) && prCu255.size() == ECDH::PRIVATE_KEY_LENGTH)
                        {
                            client->chatkey = new ECDH(prCu255);
                        }

                        if (!client->chatkey || !client->chatkey->initializationOK ||
                                !client->signkey || !client->signkey->initializationOK)
                        {
                            client->resetKeyring();
                            client->sendevent(99418, "Failed to load attached keys", 0);
                        }
                        else
                        {
                            client->sendevent(99420, "Signing and chat keys attached OK", 0);
                        }

                        delete tlvRecords;
                    }
                    else
                    {
                        LOG_warn << "Failed to decrypt keyring after putua";
                    }
                }
                else if (type == ATTR_KEYS)
                {
                    if (!client->mKeyManager.fromKeysContainer(it->second))
                    {
                        LOG_err << "Error processing new established value for the Key Manager (CommandPutMultipleUAVer)";
                        // We can't use a previous value here because CommandPutMultipleUAVer is only used to update ^!keys
                        // during initialization
                    }
                }
            }
        }
    }
    else if (r.wasErrorOrOK())
    {
        mCompletion(r.errorOrOK());
        return true;
    }

    mCompletion(API_EINTERNAL);
    return false;
}

CommandPutUAVer::CommandPutUAVer(MegaClient* client, attr_t at, const byte* av, unsigned avl, int ctag,
                                 std::function<void(Error)> completion)
{
    mSeqtagArray = true;

    this->at = at;
    this->av.assign((const char*)av, avl);

    mCompletion = completion ? std::move(completion) :
        [this](Error e) {
            this->client->app->putua_result(e);
        };

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

    const UserAttribute* attribute = client->ownuser()->getAttribute(at);
    if (attribute && attribute->isValid() && !attribute->version().empty())
    {
        element(attribute->version().c_str());
    }

    endarray();

    tag = ctag;
}

bool CommandPutUAVer::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (r.wasError(API_EEXPIRED))
        {
            User *u = client->ownuser();
            u->setAttributeExpired(at);
        }

        mCompletion(r.errorOrOK());
    }
    else
    {
        const char* ptr;
        const char* end;

        if (!(ptr = json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            mCompletion(API_EINTERNAL);
            return false;
        }
        attr_t at = User::string2attr(string(ptr, (end-ptr)).c_str());

        if (!(ptr = json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            mCompletion(API_EINTERNAL);
            return false;
        }
        string v = string(ptr, (end-ptr));

        if (at == ATTR_UNKNOWN || v.empty() || (this->at != at))
        {
            LOG_err << "Error in CommandPutUAVer. Undefined attribute or version";
            mCompletion(API_EINTERNAL);
            return false;
        }
        else
        {
            User *u = client->ownuser();

            if (at == ATTR_KEYS && !client->mKeyManager.fromKeysContainer(av))
            {
                LOG_err << "Error processing new established value for the Key Manager";

                // if there's a previous version, better keep that value in cache
                const UserAttribute* attribute = client->ownuser()->getAttribute(at);
                if (attribute && !attribute->isNotExisting() && !attribute->version().empty())
                {
                    LOG_warn << "Replacing ^!keys value by previous version "
                             << attribute->version() << ", current: " << v;
                    assert(!attribute->value().empty());
                    av = attribute->value();
                }
            }

            u->setAttribute(at, av, v);
            u->setTag(tag ? tag : -1);

            if (at == ATTR_UNSHAREABLE_KEY)
            {
                LOG_info << "Unshareable key successfully created";
                client->unshareablekey.swap(av);
            }

            client->notifyuser(u);
            mCompletion(API_OK);
        }
    }
    return true;
}

CommandPutUA::CommandPutUA(MegaClient* /*client*/, attr_t at, const byte* av, unsigned avl, int ctag, handle lph, int phtype, int64_t ts,
                           std::function<void(Error)> completion)
{
    mV3 = false;

    this->at = at;
    this->av.assign((const char*)av, avl);

    mCompletion = completion ? std::move(completion) :
                  [this](Error e){
                        client->app->putua_result(e);
                  };

    cmd("up2");

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

    if (!ISUNDEF(lph))
    {
        beginobject("aff");
        arg("id", (byte*)&lph, MegaClient::NODEHANDLE);
        arg("ts", ts);
        arg("t", phtype);   // 1=affiliate id, 2=file/folder link, 3=chat link, 4=contact link
        endobject();
    }

    tag = ctag;
}

bool CommandPutUA::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        mCompletion(r.errorOrOK());
    }
    else
    {
        const char* ptr;
        const char* end;

        if (!(ptr = json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            mCompletion(API_EINTERNAL);
            return false;
        }
        attr_t at = User::string2attr(string(ptr, (end - ptr)).c_str());

        if (!(ptr = json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            mCompletion(API_EINTERNAL);
            return false;
        }
        string v = string(ptr, (end - ptr));

        if (at == ATTR_UNKNOWN || v.empty() || (this->at != at))
        {
            LOG_err << "Error in CommandPutUA. Undefined attribute or version";
            mCompletion(API_EINTERNAL);
            return false;
        }

        User *u = client->ownuser();
        assert(u);
        if (!u)
        {
            LOG_err << "Own user not found when attempting to set user attributes";
            mCompletion(API_EACCESS);
            return true;
        }
        u->setAttribute(at, av, v);
        u->setTag(tag ? tag : -1);
        client->notifyuser(u);

        if (at == ATTR_DISABLE_VERSIONS)
        {
            client->versions_disabled = (av == "1");
            if (client->versions_disabled)
            {
                LOG_info << "File versioning is disabled";
            }
            else
            {
                LOG_info << "File versioning is enabled";
            }
        }
        else if (at == ATTR_NO_CALLKIT)
        {
            LOG_info << "CallKit is " << ((av == "1") ? "disabled" : "enabled");
        }

        mCompletion(API_OK);
    }

    return true;
}

CommandGetUA::CommandGetUA(MegaClient* /*client*/, const char* uid, attr_t at, const char* ph, int ctag,
                           CompletionErr completionErr, CompletionBytes completionBytes, CompletionTLV compltionTLV)
{
    mV3 = true;

    this->uid = uid;
    this->at = at;
    this->ph = ph ? string(ph) : "";

    mCompletionErr = completionErr ? std::move(completionErr) :
        [this](error e) {
            client->app->getua_result(e);
        };

    mCompletionBytes = completionBytes ? std::move(completionBytes) :
        [this](byte* b, unsigned l, attr_t e) {
            client->app->getua_result(b, l, e);
        };

    mCompletionTLV = compltionTLV ? std::move(compltionTLV) :
        [this](TLVstore* t, attr_t e) {
            client->app->getua_result(t, e);
        };

    if (ph && ph[0])
    {
        cmd("mcuga");
        arg("ph", ph);
    }
    else
    {
        cmd("uga");
    }

    arg("u", uid);
    arg("ua", User::attr2string(at).c_str());
    arg("v", 1);
    tag = ctag;
}

bool CommandGetUA::procresult(Result r, JSON& json)
{
    User *u = client->finduser(uid.c_str());

    if (r.wasErrorOrOK())
    {
        if (r.wasError(API_ENOENT) && u)
        {
            u->removeAttribute(at);
        }

        mCompletionErr(r.errorOrOK());

        if (isFromChatPreview())    // if `mcuga` was sent, no need to do anything else
        {
            return true;
        }

        if (u && !u->isTemporary && u->userhandle != client->me && r.wasError(API_ENOENT))
        {
            if (at == ATTR_ED25519_PUBK || at == ATTR_CU25519_PUBK)
            {
                LOG_warn << "Missing public key " << User::attr2string(at) << " for user " << u->uid;
                attr_t authringType = AuthRing::keyTypeToAuthringType(at);
                auto it = client->mAuthRingsTemp.find(authringType);
                bool temporalAuthring = it != client->mAuthRingsTemp.end();
                if (temporalAuthring)
                {
                    client->updateAuthring(&it->second, authringType, true, u->userhandle);
                }
            }
            else if (at == ATTR_SIG_CU255_PUBK)
            {
                LOG_warn << "Missing signature " << User::attr2string(at) << " for user " << u->uid;
                attr_t authringType = AuthRing::signatureTypeToAuthringType(at);
                auto it = client->mAuthRingsTemp.find(authringType);
                bool temporalAuthring = it != client->mAuthRingsTemp.end();
                if (temporalAuthring)
                {
                    client->updateAuthring(&it->second, authringType, true, u->userhandle);
                }
            }
        }

        // if the attr does not exist, initialize it
        if (at == ATTR_DISABLE_VERSIONS && r.wasError(API_ENOENT))
        {
            LOG_info << "File versioning is enabled";
            client->versions_disabled = false;
        }
        else if (at == ATTR_NO_CALLKIT && r.wasError(API_ENOENT))
        {
            LOG_info << "CallKit is enabled";
        }

        return true;
    }
    else
    {
        const char* ptr;
        const char* end;
        string value, version, buf;

        for (;;)
        {
            switch (json.getnameid())
            {
                case MAKENAMEID2('a','v'):
                {
                    if (!(ptr = json.getvalue()) || !(end = strchr(ptr, '"')))
                    {
                        mCompletionErr(API_EINTERNAL);
                        return false;
                    }
                    buf.assign(ptr, (end-ptr));
                    break;
                }
                case 'v':
                {
                    if (!(ptr = json.getvalue()) || !(end = strchr(ptr, '"')))
                    {
                        mCompletionErr(API_EINTERNAL);
                        return false;
                    }
                    version.assign(ptr, (end-ptr));
                    break;
                }
                case EOO:
                {
                    // if there's no avatar, the value is "none" (not Base64 encoded)
                    if (u && at == ATTR_AVATAR && buf == "none")
                    {
                        u->setAttribute(ATTR_AVATAR,
                                        buf, // actual value will be ignored
                                        version);
                        u->setTag(tag ? tag : -1);
                        mCompletionErr(API_ENOENT);
                        client->notifyuser(u);
                        return true;
                    }

                    // convert from ASCII to binary the received data
                    value.resize(buf.size() / 4 * 3 + 3);
                    value.resize(Base64::atob(buf.data(), (byte *)value.data(), int(value.size())));

                    // Some attributes don't keep historic records, ie. *!authring or *!lstint
                    // (none of those attributes are used by the SDK yet)
                    // bool nonHistoric = (attributename.at(1) == '!');

                    if (!u) // retrieval of attributes without contact-relationship
                    {
                        if (at == ATTR_AVATAR && buf == "none")
                        {
                            mCompletionErr(API_ENOENT);
                        }
                        else
                        {
                            mCompletionBytes((byte*) value.data(), unsigned(value.size()), at);
                        }
                        return true;
                    }

                    // handle attribute data depending on the scope
                    switch (User::scope(at))
                    {
                        case ATTR_SCOPE_PRIVATE_ENCRYPTED:
                        {
                            // decrypt the data and build the TLV records
                            std::unique_ptr<TLVstore> tlvRecords { TLVstore::containerToTLVrecords(&value, &client->key) };
                            if (!tlvRecords)
                            {
                                LOG_err << "Cannot extract TLV records for private attribute " << User::attr2string(at);
                                mCompletionErr(API_EINTERNAL);
                                return false;
                            }

                            // store the value for private user attributes (re-encrypted version of serialized TLV)
                            u->setAttribute(at, value, version);
                            mCompletionTLV(tlvRecords.get(), at);

                            break;
                        }
                        case ATTR_SCOPE_PUBLIC_UNENCRYPTED:
                        {
                            u->setAttribute(at, value, version);
                            mCompletionBytes((byte*) value.data(), unsigned(value.size()), at);

                            if (!u->isTemporary && u->userhandle != client->me)
                            {
                                if (at == ATTR_ED25519_PUBK || at == ATTR_CU25519_PUBK)
                                {
                                    client->trackKey(at, u->userhandle, value);
                                }
                                else if (at == ATTR_SIG_CU255_PUBK)
                                {
                                    client->trackSignature(at, u->userhandle, value);
                                }
                            }
                            break;
                        }
                        case ATTR_SCOPE_PROTECTED_UNENCRYPTED:
                        {
                            u->setAttribute(at, value, version);
                            mCompletionBytes((byte*) value.data(), unsigned(value.size()), at);
                            break;
                        }
                        case ATTR_SCOPE_PRIVATE_UNENCRYPTED:
                        {
                            if (at == ATTR_KEYS && !client->mKeyManager.fromKeysContainer(value))
                            {
                                LOG_err << "Error processing new established value for the Key Manager upon init";

                                // if there's a previous version, better keep that value in cache
                                const UserAttribute* attribute =
                                    client->ownuser()->getAttribute(at);
                                if (attribute && !attribute->isNotExisting() &&
                                    !attribute->version().empty())
                                {
                                    LOG_warn << "Replacing ^!keys value by previous version "
                                             << attribute->version() << " current: " << version;
                                    assert(!attribute->value().empty());
                                    value = attribute->value();
                                }
                            }

                            // store the value in cache in binary format
                            u->setAttribute(at, value, version);

                            mCompletionBytes((byte*) value.data(), unsigned(value.size()), at);

                            if (at == ATTR_DISABLE_VERSIONS)
                            {
                                client->versions_disabled = !strcmp(value.data(), "1");
                                if (client->versions_disabled)
                                {
                                    LOG_info << "File versioning is disabled";
                                }
                                else
                                {
                                    LOG_info << "File versioning is enabled";
                                }
                            }
                            else if (at == ATTR_NO_CALLKIT)
                            {
                                LOG_info << "CallKit is " << ((!strcmp(value.data(), "1")) ? "disabled" : "enabled");
                            }
                            break;
                        }
                        default: // legacy attributes without explicit scope or unknown attribute
                        {
                            LOG_err << "Unknown received attribute: " << User::attr2string(at);
                            mCompletionErr(API_EINTERNAL);
                            return false;
                        }

                    }   // switch (scope)

                    u->setTag(tag ? tag : -1);
                    client->notifyuser(u);
                    return true;
                }
                default:
                {
                    if (!json.storeobject())
                    {
                        LOG_err << "Error in CommandGetUA. Parse error";
                        mCompletionErr(API_EINTERNAL);
                        return false;
                    }
                }

            }   // switch (nameid)
        }
    }
#ifndef WIN32
    return false;  // unreachable code
#endif
}

#ifdef DEBUG
CommandDelUA::CommandDelUA(MegaClient *client, const char *an)
{
    this->an = an;
    mSeqtagArray = true;

    cmd("upr");
    arg("ua", an);

    arg("v", 1);    // returns the new version for the (removed) null value

    tag = client->reqtag;
}

bool CommandDelUA::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->delua_result(r.errorOrOK());
    }
    else
    {
        const char* ptr;
        const char* end;
        if (!(ptr = json.getvalue()) || !(end = strchr(ptr, '"')))
        {
            client->app->delua_result(API_EINTERNAL);
            return false;
        }

        User *u = client->ownuser();
        attr_t at = User::string2attr(an.c_str());
        string version(ptr, (end-ptr));

        // store version in order to avoid double users update from corresponding AP
        u->removeAttributeUpdateVersion(at, version);

        if (at == ATTR_KEYRING)
        {
            client->resetKeyring();
        }

        client->notifyuser(u);
        client->app->delua_result(API_OK);
    }
    return true;
}

CommandSendDevCommand::CommandSendDevCommand(MegaClient* client,
                                             const char* command,
                                             const char* email,
                                             long long q,
                                             int bs,
                                             int us,
                                             const char* cp)
{
    cmd("dev");

    arg("aa", command);
    if (email)
    {
        arg("t", email);
    }

    if ((strcmp(command, "tq") == 0))
    {
        arg("q", q);
    }
    else if ((strcmp(command, "bs") == 0))
    {
        arg("s", bs);
    }
    else if ((strcmp(command, "us") == 0))
    {
        arg("s", us);
    }
    else if ((strcmp(command, "abs") == 0))
    {
        assert(cp);

        if (cp) arg("c", cp);
        arg("g", us);
    }
    tag = client->reqtag;
}

bool CommandSendDevCommand::procresult(Result r, JSON&)
{
    client->app->senddevcommand_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

#endif  // #ifdef DEBUG

CommandGetUserEmail::CommandGetUserEmail(MegaClient *client, const char *uid)
{
    mSeqtagArray = true;

    cmd("uge");
    arg("u", uid);

    tag = client->reqtag;
}

bool CommandGetUserEmail::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        string email;
        if (json.storeobject(&email))
        {
            client->app->getuseremail_result(&email, API_OK);
            return true;
        }
    }
    else if (r.wasErrorOrOK())
    {
        assert(r.wasStrictlyError());
        client->app->getuseremail_result(NULL, r.errorOrOK());
        return true;
    }

    client->app->getuseremail_result(NULL, API_EINTERNAL);
    return false;
}

// set node keys (e.g. to convert asymmetric keys to symmetric ones)
CommandNodeKeyUpdate::CommandNodeKeyUpdate(MegaClient* client, handle_vector* v)
{
    byte nodekey[FILENODEKEYLENGTH];

    cmd("k");
    beginarray("nk");

    for (size_t i = v->size(); i--;)
    {
        handle h = (*v)[i];

        shared_ptr<Node> n;

        if ((n = client->nodebyhandle(h)))
        {
            client->key.ecb_encrypt((byte*)n->nodekey().data(), nodekey, n->nodekey().size());
            assert(!n->hasZeroKey());

            element(h, MegaClient::NODEHANDLE);
            element(nodekey, int(n->nodekey().size()));
        }
    }

    endarray();
}

CommandSingleKeyCR::CommandSingleKeyCR(handle sh, handle nh, const byte* key, size_t keylen)
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
    element(key, static_cast<int>(keylen));
    endarray();

    endarray();
}

CommandKeyCR::CommandKeyCR(MegaClient* /*client*/, sharedNode_vector* rshares, sharedNode_vector* rnodes, const char* keys)
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

bool CommandPubKeyRequest::procresult(Result r, JSON& json)
{
    byte pubkbuf[AsymmCipher::MAXKEYLENGTH];
    int len_pubk = 0;
    handle uh = UNDEF;

    unique_ptr<User> cleanup(u && u->isTemporary ? u : nullptr);

    if (r.wasErrorOrOK())
    {
        if (!r.wasError(API_ENOENT)) //API_ENOENT = unregistered users or accounts without a public key yet
        {
            LOG_err << "Unexpected error in CommandPubKeyRequest: " << error(r.errorOrOK());
        }
    }
    else
    {
        bool finished = false;
        while (!finished)
        {
            switch (json.getnameid())
            {
                case 'u':
                    uh = json.gethandle(MegaClient::USERHANDLE);
                    break;

                case MAKENAMEID4('p', 'u', 'b', 'k'):
                    len_pubk = json.storebinary(pubkbuf, sizeof pubkbuf);
                    break;

                case EOO:
                    if (!u) // user has cancelled the account
                    {
                        return true;
                    }

                    if (!ISUNDEF(uh))
                    {
                        client->mapuser(uh, u->email.c_str());
                        if (u->isTemporary && u->uid == u->email) //update uid with the received USERHANDLE (will be used as target for putnodes)
                        {
                            u->uid = Base64Str<MegaClient::USERHANDLE>(uh);
                        }
                    }

                    if (len_pubk && !u->pubk.setkey(AsymmCipher::PUBKEY, pubkbuf, len_pubk))
                    {
                        len_pubk = 0;
                    }

                    finished = true;
                    break;

                default:
                    if (json.storeobject())
                    {
                        continue;
                    }
                    len_pubk = 0;
                    finished = true;
                    break;
            }
        }
    }

    if (!u) // user has cancelled the account, or HIDDEN user was removed
    {
        return true;
    }

    // satisfy all pending PubKeyAction requests for this user
    while (u->pkrs.size())
    {
        client->restag = tag;
        u->pkrs[0]->proc(client, u);
        u->pkrs.pop_front();
    }

    if (len_pubk && !u->isTemporary)
    {
        client->notifyuser(u);
    }

    return true;
}

void CommandPubKeyRequest::invalidateUser()
{
    u = NULL;
}

CommandGetUserData::CommandGetUserData(
    MegaClient*,
    int tag,
    std::function<void(string*, string*, string*, error)> completion)
{
    cmd("ug");
    arg("v", 1);

    this->tag = tag;

    mCompletion = completion ? std::move(completion) :
        [this](string* name, string* pubk, string* privk, error e) {
            this->client->app->userdata_result(name, pubk, privk, e);
        };

}

bool CommandGetUserData::procresult(Result r, JSON& json)
{
    string name;
    string pubk;
    string privk;
    string k;
    byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
    int len_privk = 0;
    byte pubkbuf[AsymmCipher::MAXKEYLENGTH];
    int len_pubk = 0;
    m_time_t since = 0;
    int v = 0;
    string salt;
    string smsv;
    string lastname;
    string versionLastname;
    string firstname;
    string versionFirstname;
    string language;
    string versionLanguage;
    string pwdReminderDialog;
    string versionPwdReminderDialog;
    string pushSetting;
    string versionPushSetting;
    string contactLinkVerification;
    string versionContactLinkVerification;
#ifndef NDEBUG
    handle me = UNDEF;
#endif
    string chatFolder;
    string versionChatFolder;
    string cameraUploadFolder;
    string versionCameraUploadFolder;
    string aliases;
    string versionAliases;
    string disableVersions;
    string versionDisableVersions;
    string noCallKit;
    string versionNoCallKit;
    string country;
    string versionCountry;
    string birthday;
    string versionBirthday;
    string birthmonth;
    string versionBirthmonth;
    string birthyear;
    string versionBirthyear;
    string email;
    string unshareableKey;
    string versionUnshareableKey;
    string deviceNames;
    string versionDeviceNames;
    string versionDriveNames;
    string myBackupsFolder;
    string versionMyBackupsFolder;
    string versionBackupNames;
    string cookieSettings;
    string versionCookieSettings;
    string appPrefs;
    string versionAppPrefs;
    string ccPrefs;
    string versionCcPrefs;
    string enabledTestNotifications, versionEnabledTestNotifications;
    string lastReadNotification, versionLastReadNotification;
    string lastActionedBanner, versionLastActionedBanner;
    string enabledTestSurveys, versionEnabledTestSurveys;
#ifdef ENABLE_SYNC
    string jsonSyncConfigData;
    string jsonSyncConfigDataVersion;
#endif
    string keys, keysVersion;
    string keyring, versionKeyring;
    string pubEd255, versionPubEd255;
    string pubCu255, versionPubCu255;
    string sigPubk, versionSigPubk;
    string sigCu255, versionSigCu255;
    string authringEd255, versionAuthringEd255;
    string authringCu255, versionAuthringCu255;
    string visibleWelcomeDialog;
    string versionVisibleWelcomeDialog;
    string visibleTermsOfService;
    string versionVisibleTermsOfService;
    string pwmh, pwmhVersion;
    vector<uint32_t> notifs;

    bool uspw = false;
    vector<m_time_t> warningTs;
    m_time_t deadlineTs = -1;

    bool b = false;
    BizMode m = BIZ_MODE_UNKNOWN;
    BizStatus s = BIZ_STATUS_UNKNOWN;
    std::set<handle> masters;
    std::vector<std::pair<BizStatus, m_time_t>> sts;

    if (r.wasErrorOrOK())
    {
        mCompletion(NULL, NULL, NULL, r.wasError(API_OK) ? Error(API_ENOENT) : r.errorOrOK());
        return true;
    }

    for (;;)
    {
        string attributeName = json.getnameWithoutAdvance();
        switch (json.getnameid())
        {
        case MAKENAMEID3('a', 'a', 'v'):    // account authentication version
            v = (int)json.getint();
            break;

        case MAKENAMEID3('a', 'a', 's'):    // account authentication salt
            json.storeobject(&salt);
            break;

        case MAKENAMEID4('n', 'a', 'm', 'e'):
            json.storeobject(&name);
            break;

        case 'k':   // master key
            k.resize(SymmCipher::KEYLENGTH);
            json.storebinary((byte *)k.data(), int(k.size()));
            break;

        case MAKENAMEID5('s', 'i', 'n', 'c', 'e'):
            since = json.getint();
            break;

        case MAKENAMEID4('p', 'u', 'b', 'k'):   // RSA public key
            json.storeobject(&pubk);
            len_pubk = Base64::atob(pubk.c_str(), pubkbuf, sizeof pubkbuf);
            break;

        case MAKENAMEID5('p', 'r', 'i', 'v', 'k'):  // RSA private key (encrypted to MK)
            len_privk = json.storebinary(privkbuf, sizeof privkbuf);
            break;

        case MAKENAMEID5('f', 'l', 'a', 'g', 's'):
            if (json.enterobject())
            {
                if (client->readmiscflags(&json) != API_OK)
                {
                    mCompletion(NULL, NULL, NULL, API_EINTERNAL);
                    return false;
                }
                json.leaveobject();
            }
            break;

        case MAKENAMEID2('n', 'a'):
            client->accountIsNew = bool(json.getint());
            break;

        case 'u':
#ifndef NDEBUG
            me =
#endif
                 json.gethandle(MegaClient::USERHANDLE);
            break;

        case MAKENAMEID8('l', 'a', 's', 't', 'n', 'a', 'm', 'e'):
            parseUserAttribute(json, lastname, versionLastname);
            break;

        case MAKENAMEID6('^', '!', 'l', 'a', 'n', 'g'):
            parseUserAttribute(json, language, versionLanguage);
            break;

        case MAKENAMEID8('b', 'i', 'r', 't', 'h', 'd', 'a', 'y'):
            parseUserAttribute(json, birthday, versionBirthday);
            break;

        case MAKENAMEID7('c', 'o', 'u', 'n', 't', 'r', 'y'):
            parseUserAttribute(json, country, versionCountry);
            break;

        case MAKENAMEID4('^', '!', 'p', 's'):
            parseUserAttribute(json, pushSetting, versionPushSetting);
            break;

        case MAKENAMEID5('^', '!', 'p', 'r', 'd'):
            parseUserAttribute(json, pwdReminderDialog, versionPwdReminderDialog);
            break;

        case MAKENAMEID4('^', 'c', 'l', 'v'):
            parseUserAttribute(json, contactLinkVerification, versionContactLinkVerification);
            break;

        case MAKENAMEID4('^', '!', 'd', 'v'):
            parseUserAttribute(json, disableVersions, versionDisableVersions);
            break;

        case MAKENAMEID7('^', '!', 'n', 'o', 'k', 'i', 't'):
            parseUserAttribute(json, noCallKit, versionNoCallKit);
            break;

        case MAKENAMEID4('*', '!', 'c', 'f'):
            parseUserAttribute(json, chatFolder, versionChatFolder);
            break;

        case MAKENAMEID5('*', '!', 'c', 'a', 'm'):
            parseUserAttribute(json, cameraUploadFolder, versionCameraUploadFolder);
            break;

        case MAKENAMEID8('*', '!', '>', 'a', 'l', 'i', 'a', 's'):
            parseUserAttribute(json, aliases, versionAliases);
            break;

        case MAKENAMEID5('e', 'm', 'a', 'i', 'l'):
            json.storeobject(&email);
            break;

        case MAKENAMEID5('*', '~', 'u', 's', 'k'):
            parseUserAttribute(json, unshareableKey, versionUnshareableKey, false);
            break;

        case MAKENAMEID4('*', '!', 'd', 'n'):
            parseUserAttribute(json, deviceNames, versionDeviceNames);
            break;

        case MAKENAMEID5('^', '!', 'b', 'a', 'k'):
            parseUserAttribute(json, myBackupsFolder, versionMyBackupsFolder);
            break;

        case MAKENAMEID8('*', '!', 'a', 'P', 'r', 'e', 'f', 's'):
            parseUserAttribute(json, appPrefs, versionAppPrefs);
            break;

        case MAKENAMEID8('*', '!', 'c', 'c', 'P', 'r', 'e', 'f'):
            parseUserAttribute(json, ccPrefs, versionCcPrefs);
            break;

#ifdef ENABLE_SYNC
        case MAKENAMEID6('*', '~', 'j', 's', 'c', 'd'):
            parseUserAttribute(json, jsonSyncConfigData, jsonSyncConfigDataVersion);
            break;
#endif
        case MAKENAMEID6('^', '!', 'k', 'e', 'y', 's'):
            parseUserAttribute(json, keys, keysVersion);
            break;
        case MAKENAMEID8('*', 'k', 'e', 'y', 'r', 'i', 'n', 'g'):
            parseUserAttribute(json, keyring, versionKeyring);
            break;
        case MAKENAMEID8('+', 'p', 'u', 'E', 'd', '2', '5', '5'):
            parseUserAttribute(json, pubEd255, versionPubEd255);
            break;
        case MAKENAMEID8('+', 'p', 'u', 'C', 'u', '2', '5', '5'):
            parseUserAttribute(json, pubCu255, versionPubCu255);
            break;
        case MAKENAMEID8('+', 's', 'i', 'g', 'P', 'u', 'b', 'k'):
            parseUserAttribute(json, sigPubk, versionSigPubk);
            break;

        case MAKENAMEID2('p', 'f'):  // Pro Flexi plan (similar to business)
            client->setProFlexi(true);
            [[fallthrough]];
        case 'b':   // business account's info
            assert(!b);
            b = true;
            if (json.enterobject())
            {
                bool endobject = false;
                while (!endobject)
                {
                    switch (json.getnameid())
                    {
                        case 's':   // status
                            // -1: expired, 1: active, 2: grace-period
                            s = BizStatus(json.getint32());
                            break;

                        case 'm':   // mode
                            m = BizMode(json.getint32());
                            break;

                        case MAKENAMEID2('m', 'u'):
                            if (json.enterarray())
                            {
                                for (;;)
                                {
                                    handle uh = json.gethandle(MegaClient::USERHANDLE);
                                    if (!ISUNDEF(uh))
                                    {
                                        masters.emplace(uh);
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }
                                json.leavearray();
                            }
                            break;

                        case MAKENAMEID3('s', 't', 's'):    // status timestamps
                            // ie. "sts":[{"s":-1,"ts":1566182227},{"s":1,"ts":1563590227}]
                            json.enterarray();
                            while (json.enterobject())
                            {
                                BizStatus status = BIZ_STATUS_UNKNOWN;
                                m_time_t ts = 0;

                                bool exit = false;
                                while (!exit)
                                {
                                    switch (json.getnameid())
                                    {
                                        case 's':
                                           status = BizStatus(json.getint());
                                           break;

                                        case MAKENAMEID2('t', 's'):
                                           ts = json.getint();
                                           break;

                                        case EOO:
                                            if (status != BIZ_STATUS_UNKNOWN && isValidTimeStamp(ts))
                                            {
                                                sts.push_back(std::make_pair(status, ts));
                                            }
                                            else
                                            {
                                                LOG_warn << "Unpaired/missing business status-ts in b.sts";
                                            }
                                            exit = true;
                                            break;

                                        default:
                                            if (!json.storeobject())
                                            {
                                                mCompletion(NULL, NULL, NULL, API_EINTERNAL);
                                                json.leavearray();
                                                return false;
                                            }
                                    }
                                }
                                json.leaveobject();
                            }
                            json.leavearray();
                            break;

                        case EOO:
                            endobject = true;
                            break;

                        default:
                            if (!json.storeobject())
                            {
                                mCompletion(NULL, NULL, NULL, API_EINTERNAL);
                                return false;
                            }
                    }
                }
                json.leaveobject();
            }
            break;

        case MAKENAMEID4('s', 'm', 's', 'v'):   // SMS verified phone number
            if (!json.storeobject(&smsv))
            {
                LOG_err << "Invalid verified phone number (smsv)";
                assert(false);
            }
            break;

        case MAKENAMEID4('u', 's', 'p', 'w'):   // user paywall data
        {
            uspw = true;

            if (json.enterobject())
            {
                bool endobject = false;
                while (!endobject)
                {
                    switch (json.getnameid())
                    {
                        case MAKENAMEID2('d', 'l'): // deadline timestamp
                            deadlineTs = json.getint();
                            break;

                        case MAKENAMEID3('w', 't', 's'):    // warning timestamps
                            // ie. "wts":[1591803600,1591813600,1591823600

                            if (json.enterarray())
                            {
                                m_time_t ts;
                                while (json.isnumeric() && (ts = json.getint()) != -1)
                                {
                                    warningTs.push_back(ts);
                                }

                                json.leavearray();
                            }
                            break;

                        case EOO:
                            endobject = true;
                            break;

                        default:
                            if (!json.storeobject())
                            {
                                mCompletion(NULL, NULL, NULL, API_EINTERNAL);
                                return false;
                            }
                    }
                }
                json.leaveobject();
            }
            break;
        }

        case MAKENAMEID5('^', '!', 'c', 's', 'p'):
            parseUserAttribute(json, cookieSettings, versionCookieSettings);
            break;

//        case MAKENAMEID1('p'):  // plan: 101 for Pro Flexi
//            {
//                int proPlan = json.getint32();
//            }
//            break;
        case MAKENAMEID8('^', '!', 'w', 'e', 'l', 'd', 'l', 'g'):
        {
            parseUserAttribute(json, visibleWelcomeDialog, versionVisibleWelcomeDialog);
            break;
        }

        case MAKENAMEID5('^', '!', 't', 'o', 's'):
        {
            parseUserAttribute(json, visibleTermsOfService, versionVisibleTermsOfService);
            break;
        }

        case MAKENAMEID4('p', 'w', 'm', 'h'):
            parseUserAttribute(json, pwmh, pwmhVersion);
            break;

        case MAKENAMEID6('n', 'o', 't', 'i', 'f', 's'):
        {
            if (json.enterarray())
            {
                while (json.isnumeric())
                {
                    notifs.push_back(json.getuint32());
                }
                json.leavearray();
            }
            break;
        }

        case MAKENAMEID8('^', '!', 't', 'n', 'o', 't', 'i', 'f'):
        {
            parseUserAttribute(json, enabledTestNotifications, versionEnabledTestNotifications);
            break;
        }

        case MAKENAMEID8('^', '!', 'l', 'n', 'o', 't', 'i', 'f'):
        {
            parseUserAttribute(json, lastReadNotification, versionLastReadNotification);
            break;
        }

        case MAKENAMEID8('^', '!', 'l', 'b', 'a', 'n', 'n', 'r'):
        {
            parseUserAttribute(json, lastActionedBanner, versionLastActionedBanner);
            break;
        }

        case MAKENAMEID6('^', '!', 't', 's', 'u', 'r'):
        {
            parseUserAttribute(json, enabledTestSurveys, versionEnabledTestSurveys);
            break;
        }

        case EOO:
        {
            assert(me == client->me);

            if (len_privk)
            {
                client->key.ecb_decrypt(privkbuf, len_privk);
                privk.resize(AsymmCipher::MAXKEYLENGTH * 2);
                privk.resize(Base64::btoa(privkbuf, len_privk, (char *)privk.data()));

                // RSA private key should be already assigned at login
                assert(privk == client->mPrivKey);
                if (client->mPrivKey.empty())
                {
                    client->mPrivKey = privk;
                    LOG_warn << "Private key not set by login, setting at `ug` response...";
                    if (!client->asymkey.setkey(AsymmCipher::PRIVKEY, privkbuf, len_privk))
                    {
                        LOG_warn << "Error checking private key at `ug` response";
                    }
                }
            }

            if (len_pubk)
            {
                client->pubk.setkey(AsymmCipher::PUBKEY, pubkbuf, len_pubk);
            }

            if (v)
            {
                client->accountversion = v;
            }

            if (salt.size())
            {
                Base64::atob(salt, client->accountsalt);
            }

            client->accountsince = since;
            client->mSmsVerifiedPhone = smsv;

            client->k = k;

            client->btugexpiration.backoff(MegaClient::USER_DATA_EXPIRATION_BACKOFF_SECS * 10);
            client->cachedug = true;

            // pre-load received user attributes into cache
            User* u = client->ownuser();
            if (u)
            {
                bool changes = false;
                if (email.size())
                {
                    client->setEmail(u, email);
                }

                if (firstname.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_FIRSTNAME,
                                                                    firstname,
                                                                    versionFirstname);
                }

                if (lastname.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_LASTNAME,
                                                                    lastname,
                                                                    versionLastname);
                }

                if (language.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_LANGUAGE,
                                                                    language,
                                                                    versionLanguage);
                }
                else
                {
                    u->removeAttribute(ATTR_LANGUAGE);
                }

                if (birthday.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_BIRTHDAY,
                                                                    birthday,
                                                                    versionBirthday);
                }
                else
                {
                    u->removeAttribute(ATTR_BIRTHDAY);
                }

                if (birthmonth.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_BIRTHMONTH,
                                                                    birthmonth,
                                                                    versionBirthmonth);
                }
                else
                {
                    u->removeAttribute(ATTR_BIRTHMONTH);
                }

                if (birthyear.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_BIRTHYEAR,
                                                                    birthyear,
                                                                    versionBirthyear);
                }
                else
                {
                    u->removeAttribute(ATTR_BIRTHYEAR);
                }

                if (country.size())
                {
                    changes |=
                        u->updateAttributeIfDifferentVersion(ATTR_COUNTRY, country, versionCountry);
                }
                else
                {
                    u->removeAttribute(ATTR_COUNTRY);
                }

                if (pwdReminderDialog.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_PWD_REMINDER,
                                                                    pwdReminderDialog,
                                                                    versionPwdReminderDialog);
                }
                else
                {
                    u->removeAttribute(ATTR_PWD_REMINDER);
                }

                if (pushSetting.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_PUSH_SETTINGS,
                                                                    pushSetting,
                                                                    versionPushSetting);
                }
                else
                {
                    u->removeAttribute(ATTR_PUSH_SETTINGS);
                }

                if (contactLinkVerification.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_CONTACT_LINK_VERIFICATION,
                                                                    contactLinkVerification,
                                                                    versionContactLinkVerification);
                }
                else
                {
                    u->removeAttribute(ATTR_CONTACT_LINK_VERIFICATION);
                }

                if (disableVersions.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_DISABLE_VERSIONS,
                                                                    disableVersions,
                                                                    versionDisableVersions);

                    // initialize the status of file-versioning for the client
                    client->versions_disabled = (disableVersions == "1");
                    if (client->versions_disabled)
                    {
                        LOG_info << "File versioning is disabled";
                    }
                    else
                    {
                        LOG_info << "File versioning is enabled";
                    }
                }
                else    // attribute does not exists
                {
                    LOG_info << "File versioning is enabled";
                    client->versions_disabled = false;
                    u->removeAttribute(ATTR_DISABLE_VERSIONS);
                }

                if (noCallKit.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_NO_CALLKIT,
                                                                    noCallKit,
                                                                    versionNoCallKit);
                    LOG_info << "CallKit is " << ((noCallKit == "1") ? "disabled" : "enabled");
                }
                else
                {
                    LOG_info << "CallKit is enabled [noCallKit.size() == 0]";
                    u->removeAttribute(ATTR_NO_CALLKIT);
                }

                if (chatFolder.size())
                {
                    unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(&chatFolder, &client->key));
                    if (tlvRecords)
                    {
                        // store the value for private user attributes (decrypted version of serialized TLV)
                        unique_ptr<string> tlvString(tlvRecords->tlvRecordsToContainer(client->rng, &client->key));
                        changes |= u->updateAttributeIfDifferentVersion(ATTR_MY_CHAT_FILES_FOLDER,
                                                                        *tlvString,
                                                                        versionChatFolder);
                    }
                    else
                    {
                        LOG_err << "Cannot extract TLV records for ATTR_MY_CHAT_FILES_FOLDER";
                    }
                }
                else
                {
                    u->removeAttribute(ATTR_MY_CHAT_FILES_FOLDER);
                }

                if (cameraUploadFolder.size())
                {
                    unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(&cameraUploadFolder, &client->key));
                    if (tlvRecords)
                    {
                        // store the value for private user attributes (decrypted version of serialized TLV)
                        unique_ptr<string> tlvString(tlvRecords->tlvRecordsToContainer(client->rng, &client->key));
                        changes |= u->updateAttributeIfDifferentVersion(ATTR_CAMERA_UPLOADS_FOLDER,
                                                                        *tlvString,
                                                                        versionCameraUploadFolder);
                    }
                    else
                    {
                        LOG_err << "Cannot extract TLV records for ATTR_CAMERA_UPLOADS_FOLDER";
                    }
                }
                else
                {
                    u->removeAttribute(ATTR_CAMERA_UPLOADS_FOLDER);
                }

                if (!myBackupsFolder.empty())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_MY_BACKUPS_FOLDER,
                                                                    myBackupsFolder,
                                                                    versionMyBackupsFolder);
                }
                else
                {
                    u->removeAttribute(ATTR_MY_BACKUPS_FOLDER);
                }

                if (!appPrefs.empty())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_APPS_PREFS,
                                                                    appPrefs,
                                                                    versionAppPrefs);
                }
                else
                {
                    u->removeAttribute(ATTR_APPS_PREFS);
                }

                if (!ccPrefs.empty())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_CC_PREFS,
                                                                    ccPrefs,
                                                                    versionCcPrefs);
                }
                else
                {
                    u->removeAttribute(ATTR_CC_PREFS);
                }

                if (aliases.size())
                {
                    unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(&aliases, &client->key));
                    if (tlvRecords)
                    {
                        // store the value for private user attributes (decrypted version of serialized TLV)
                        unique_ptr<string> tlvString(tlvRecords->tlvRecordsToContainer(client->rng, &client->key));
                        changes |= u->updateAttributeIfDifferentVersion(ATTR_ALIAS,
                                                                        *tlvString,
                                                                        versionAliases);
                    }
                    else
                    {
                        LOG_err << "Cannot extract TLV records for ATTR_ALIAS";
                    }
                }
                else
                {
                    u->removeAttribute(ATTR_ALIAS);
                }

                if (unshareableKey.size() == Base64Str<SymmCipher::BLOCKSIZE>::STRLEN)
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_UNSHAREABLE_KEY,
                                                                    unshareableKey,
                                                                    versionUnshareableKey);
                    client->unshareablekey.swap(unshareableKey);
                }
                else if (client->loggedin() == EPHEMERALACCOUNTPLUSPLUS)
                {
                    // cannot configure CameraUploads, so it's not needed at this stage.
                    // It will be created when the account gets confirmed.
                    // (motivation: speed up the E++ account's setup)
                    LOG_info << "Skip creation of unshareable key for E++ account";
                }
                else if (unshareableKey.empty())    // it has not been created yet
                {
                    LOG_info << "Creating unshareable key...";
                    byte newunshareablekey[SymmCipher::BLOCKSIZE];
                    client->rng.genblock(newunshareablekey, sizeof(newunshareablekey));
                    client->putua(ATTR_UNSHAREABLE_KEY, newunshareablekey, sizeof(newunshareablekey), 0);
                }
                else
                {
                    LOG_err << "Unshareable key wrong length";
                }

                if (deviceNames.size())
                {
                    unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(&deviceNames, &client->key));
                    if (tlvRecords)
                    {
                        // store the value for private user attributes (decrypted version of serialized TLV)
                        unique_ptr<string> tlvString(tlvRecords->tlvRecordsToContainer(client->rng, &client->key));
                        changes |= u->updateAttributeIfDifferentVersion(ATTR_DEVICE_NAMES,
                                                                        *tlvString,
                                                                        versionDeviceNames);
                    }
                    else
                    {
                        LOG_err << "Cannot extract TLV records for ATTR_DEVICE_NAMES";
                    }
                }
                else
                {
                    u->removeAttribute(ATTR_DEVICE_NAMES);
                }

                if (!cookieSettings.empty())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_COOKIE_SETTINGS,
                                                                    cookieSettings,
                                                                    versionCookieSettings);
                }
                else
                {
                    u->removeAttribute(ATTR_COOKIE_SETTINGS);
                }

                client->setEnabledNotifications(std::move(notifs));

                if (!enabledTestNotifications.empty() || !versionEnabledTestNotifications.empty())
                {
                    changes |=
                        u->updateAttributeIfDifferentVersion(ATTR_ENABLE_TEST_NOTIFICATIONS,
                                                             enabledTestNotifications,
                                                             versionEnabledTestNotifications);
                }
                else
                {
                    u->removeAttribute(ATTR_ENABLE_TEST_NOTIFICATIONS);
                }

                if (!lastReadNotification.empty() || !versionLastReadNotification.empty())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_LAST_READ_NOTIFICATION,
                                                                    lastReadNotification,
                                                                    versionLastReadNotification);
                }
                else
                {
                    u->removeAttribute(ATTR_LAST_READ_NOTIFICATION);
                }

                if (!lastActionedBanner.empty() || !versionLastActionedBanner.empty())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_LAST_ACTIONED_BANNER,
                                                                    lastActionedBanner,
                                                                    versionLastActionedBanner);
                }
                else
                {
                    u->removeAttribute(ATTR_LAST_ACTIONED_BANNER);
                }

                if (!enabledTestSurveys.empty() || !versionEnabledTestSurveys.empty())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_ENABLE_TEST_SURVEYS,
                                                                    enabledTestSurveys,
                                                                    versionEnabledTestSurveys);
                }
                else
                {
                    u->removeAttribute(ATTR_ENABLE_TEST_SURVEYS);
                }

#ifdef ENABLE_SYNC
                if (!jsonSyncConfigData.empty())
                {
                    // Tell the rest of the SDK that the attribute's changed.
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_JSON_SYNC_CONFIG_DATA,
                                                                    jsonSyncConfigData,
                                                                    jsonSyncConfigDataVersion);
                }
                else
                {
                    u->removeAttribute(ATTR_JSON_SYNC_CONFIG_DATA);
                }
#endif // ENABLE_SYNC

                if (keys.size())
                {
                    client->mKeyManager.setKey(client->key);
                    if (!client->mKeyManager.fromKeysContainer(keys))
                    {
                        LOG_err << "Error processing new received values for the Key Manager (ug command)";

                        // if there's a previous version, better keep that value in cache
                        const UserAttribute* attribute = client->ownuser()->getAttribute(ATTR_KEYS);
                        if (attribute && !attribute->isNotExisting() &&
                            !attribute->version().empty())
                        {
                            LOG_warn << "Replacing ^!keys value by previous version "
                                     << attribute->version() << " current: " << keysVersion;
                            assert(!attribute->value().empty());
                            keys = attribute->value();
                        }
                    }

                    changes |= u->updateAttributeIfDifferentVersion(ATTR_KEYS, keys, keysVersion);
                }
                else if (client->mKeyManager.generation())
                {
                    // once the KeyManager is initialized, a future `ug` should always
                    // include the user's attribute
                    client->sendevent(99465, "KeyMgr / Setup failure");
                }
                else
                {
                    // Process the following ones only when there is no ^!keys yet in the account.
                    // If ^!keys exists, they are all already in it.
                    if (keyring.size()) // priv Ed255 and Cu255 keys
                    {
                        changes |= u->updateAttributeIfDifferentVersion(ATTR_KEYRING,
                                                                        keyring,
                                                                        versionKeyring);
                    }

                    if (authringEd255.size())
                    {
                        changes |= u->updateAttributeIfDifferentVersion(ATTR_AUTHRING,
                                                                        authringEd255,
                                                                        versionAuthringEd255);
                    }

                    if (authringCu255.size())
                    {
                        changes |= u->updateAttributeIfDifferentVersion(ATTR_AUTHCU255,
                                                                        authringCu255,
                                                                        versionAuthringCu255);
                    }
                }

                if (pubEd255.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_ED25519_PUBK,
                                                                    pubEd255,
                                                                    versionPubEd255);
                }

                if (pubCu255.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_CU25519_PUBK,
                                                                    pubCu255,
                                                                    versionPubCu255);
                }

                if (sigPubk.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_SIG_RSA_PUBK,
                                                                    sigPubk,
                                                                    versionSigPubk);
                }

                if (sigCu255.size())
                {
                    changes |= u->updateAttributeIfDifferentVersion(ATTR_SIG_CU255_PUBK,
                                                                    sigCu255,
                                                                    versionSigCu255);
                }

                if (!pwmh.empty())
                {
                    changes |=
                        u->updateAttributeIfDifferentVersion(ATTR_PWM_BASE, pwmh, pwmhVersion);
                }
                else
                {
                    u->removeAttribute(ATTR_PWM_BASE);
                }

                if (changes)
                {
                    u->setTag(tag ? tag : -1);
                    client->notifyuser(u);
                }
            }

            if (b)  // business account
            {
                // integrity checks
                if ((s < BIZ_STATUS_EXPIRED || s > BIZ_STATUS_GRACE_PERIOD)  // status not received or invalid
                        || (m == BIZ_MODE_UNKNOWN && !client->isProFlexi()))  // master flag not received or invalid (or Pro Flexi, not business)
                {
                    std::string err = "GetUserData: invalid business status / account mode";
                    LOG_err << err;
                    client->sendevent(99450, err.c_str(), 0);
                    client->mBizMode = BIZ_MODE_SUBUSER;
                    client->mBizExpirationTs = client->mBizGracePeriodTs = 0;
                    client->setBusinessStatus(BIZ_STATUS_EXPIRED);
                }
                else
                {
                    for (auto it : sts)
                    {
                        BizStatus status = it.first;
                        m_time_t ts = it.second;
                        if (status == BIZ_STATUS_EXPIRED)
                        {
                            client->mBizExpirationTs = ts;
                        }
                        else if (status == BIZ_STATUS_GRACE_PERIOD)
                        {
                            client->mBizGracePeriodTs = ts;
                        }
                        else
                        {
                            LOG_warn << "Unexpected status in b.sts. Status: " << status << "ts: " << ts;
                        }
                    }

                    client->mBizMode = m;
                    // subusers must receive the list of master users
                    assert(m != BIZ_MODE_SUBUSER || !masters.empty());
                    client->mBizMasters = masters;

                    client->setBusinessStatus(s);

                    // if current business status will expire sooner than the scheduled `ug`, update the
                    // backoff to a shorter one in order to refresh the business status asap
                    m_time_t auxts = 0;
                    m_time_t now = m_time(nullptr);
                    if (client->mBizGracePeriodTs && client->mBizGracePeriodTs > now)
                    {
                        auxts = client->mBizGracePeriodTs;
                    }
                    else if (client->mBizExpirationTs && client->mBizExpirationTs > now)
                    {
                        auxts = client->mBizExpirationTs;
                    }
                    if (auxts)
                    {
                        dstime diff = static_cast<dstime>((auxts - now) * 10);
                        dstime current = client->btugexpiration.backoffdelta();
                        // diff < 0 grace period has already expired, ug update is requested one per
                        // day
                        if (diff > 0 && current > diff)
                        {
                            client->btugexpiration.backoff(diff);
                        }
                    }
                    // TODO: check if type of account has changed and notify with new event (not yet supported by API)
                }
            }
            else
            {
                client->mBizMode = BIZ_MODE_UNKNOWN;
                client->mBizMasters.clear();
                client->mBizExpirationTs = client->mBizGracePeriodTs = 0;
                client->setBusinessStatus(BIZ_STATUS_INACTIVE);
            }

            if (uspw)
            {
                if (deadlineTs == -1 || warningTs.empty())
                {
                    LOG_err << "uspw received with missing timestamps";
                }
                else
                {
                    client->mOverquotaWarningTs = std::move(warningTs);
                    client->mOverquotaDeadlineTs = deadlineTs;
                    client->activateoverquota(0, true);
                }

            }

            mCompletion(&name, &pubk, &privk, API_OK);
            return true;
        }
        default:
            switch (User::string2attr(attributeName.c_str()))
            {
                case ATTR_FIRSTNAME:
                    parseUserAttribute(json, firstname, versionFirstname);
                    break;

                case ATTR_BIRTHMONTH:
                    parseUserAttribute(json, birthmonth, versionBirthmonth);
                    break;

                case ATTR_BIRTHYEAR:
                    parseUserAttribute(json, birthyear, versionBirthyear);
                    break;

                case ATTR_SIG_CU255_PUBK:
                    parseUserAttribute(json, sigCu255, versionSigCu255);
                    break;

                case ATTR_AUTHRING:
                    parseUserAttribute(json, authringEd255, versionAuthringEd255);
                    break;

                case ATTR_AUTHCU255:
                    parseUserAttribute(json, authringCu255, versionAuthringCu255);
                    break;

                default:
                    if (!json.storeobject())
                    {
                        mCompletion(NULL, NULL, NULL, API_EINTERNAL);
                        return false;
                    }
                    break;
            }

            break;
        }
    }
}

void CommandGetUserData::parseUserAttribute(JSON& json, std::string &value, std::string &version, bool asciiToBinary)
{
    string info;
    if (!json.storeobject(&info))
    {
        LOG_err << "Failed to parse user attribute from the array";
        return;
    }

    string buf;
    JSON infoJson;
    infoJson.pos = info.c_str() + 1;
    for (;;)
    {
        switch (infoJson.getnameid())
        {
            case MAKENAMEID2('a','v'):  // value
            {
                infoJson.storeobject(&buf);
                break;
            }
            case 'v':   // version
            {
                infoJson.storeobject(&version);
                break;
            }
            case EOO:
            {
                value = asciiToBinary ? Base64::atob(buf) : buf;
                return;
            }
            default:
            {
                if (!infoJson.storeobject())
                {
                    version.clear();
                    LOG_err << "Failed to parse user attribute inside the array";
                    return;
                }
            }
        }
    }
}

CommandGetMiscFlags::CommandGetMiscFlags(MegaClient *client)
{
    cmd("gmf");

    // this one can get the smsve flag when the account is blocked (if it's in a batch by itself)
    batchSeparately = true;

    tag = client->reqtag;
}

bool CommandGetMiscFlags::procresult(Result r, JSON& json)
{
    Error e;
    if (r.wasErrorOrOK())
    {
        e = r.errorOrOK();
        if (!e)
        {
            LOG_err << "Unexpected response for gmf: no flags, but no error";
            e = API_ENOENT;
        }
        LOG_err << "gmf failed: " << e;
    }
    else
    {
        e = client->readmiscflags(&json);
    }

    client->app->getmiscflags_result(e);
    return error(e) != API_EINTERNAL;
}

CommandABTestActive::CommandABTestActive(MegaClient *client, const string& flag, Completion completion)
    : mCompletion(completion)
{
    cmd("abta");
    arg("c", flag.c_str());

    tag = client->reqtag;
}

bool CommandABTestActive::procresult(Result r, JSON&)
{
    assert(r.wasErrorOrOK());
    if (mCompletion)
    {
        mCompletion(r.errorOrOK());
    }

    return r.wasErrorOrOK();
}


CommandGetUserQuota::CommandGetUserQuota(MegaClient* client, std::shared_ptr<AccountDetails> ad, bool storage, bool transfer, bool pro, int source, std::function<void(std::shared_ptr<AccountDetails>, Error)> completion)
  : details(ad), mStorage(storage), mTransfer(transfer), mPro(pro), mCompletion(std::move(completion))
{
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

    arg("src", source);

    arg("v", 2);

    tag = client->reqtag;
}

bool CommandGetUserQuota::procresult(Result r, JSON& json)
{
    m_off_t td;
#ifndef NDEBUG
    bool got_storage = false;
    bool got_storage_used = false;
#endif
    int uslw = -1;

    if (r.wasErrorOrOK())
    {
        client->app->account_details(details.get(), r.errorOrOK());
        if(mCompletion)
        {
            mCompletion(details, r.errorOrOK());
        }
        return true;
    }

    details->subscriptions.clear();
    details->plans.clear();

    details->storage_used = 0;
    details->storage_max = 0;

    details->transfer_max = 0;
    details->transfer_own_used = 0;
    details->transfer_srv_used = 0;
    details->srv_ratio = 0;

    details->transfer_hist_starttime = 0;
    details->transfer_hist_interval = 3600;
    details->transfer_hist.clear();
    details->transfer_hist_valid = true;

    details->transfer_reserved = 0;
    details->transfer_own_reserved = 0;
    details->transfer_srv_reserved = 0;

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID2('b', 't'):
            // "Base time age", this is number of seconds since the start of the current quota buckets
                // age of transfer
                // window start
                td = json.getint();
                if (td != -1)
                {
                    details->transfer_hist_starttime = m_time() - td;
                }
                break;

            case MAKENAMEID3('t', 'a', 'h'):
            // The free IP-based quota buckets, 6 entries for 6 hours
                if (json.enterarray())
                {
                    m_off_t t;

                    while (json.isnumeric() && (t = json.getint()) != -1)
                    {
                        details->transfer_hist.push_back(t);
                    }

                    json.leavearray();
                }
                break;

            case MAKENAMEID3('t', 'a', 'r'):
            // IP transfer reserved
                details->transfer_reserved = json.getint();
                break;

            case MAKENAMEID3('r', 'u', 'a'):
            // Actor reserved quota
                details->transfer_own_reserved += json.getint();
                break;

            case MAKENAMEID3('r', 'u', 'o'):
            // Owner reserved quota
                details->transfer_srv_reserved += json.getint();
                break;

            case MAKENAMEID5('c', 's', 't', 'r', 'g'):
            // Your total account storage usage
                details->storage_used = json.getint();
#ifndef NDEBUG
                got_storage_used = true;
#endif
                break;

            case MAKENAMEID6('c', 's', 't', 'r', 'g', 'n'):
            // Storage breakdown of root nodes and shares for your account
            // [bytes, numFiles, numFolders, versionedBytes, numVersionedFiles]
                if (json.enterobject())
                {
                    handle h;
                    NodeStorage* ns;

                    while (!ISUNDEF(h = json.gethandle()) && json.enterarray())
                    {
                        ns = &details->storage[h];

                        ns->bytes = json.getint();
                        ns->files = uint32_t(json.getint());
                        ns->folders = uint32_t(json.getint());
                        ns->version_bytes = json.getint();
                        ns->version_files = json.getint32();

#ifdef _DEBUG
                        // TODO: remove this debugging block once local count is confirmed to work correctly 100%
                        // verify the new local storage counters per root match server side (could fail if actionpackets are pending)
                        shared_ptr<Node> node = client->nodebyhandle(h);
                        if (node)
                        {
                            NodeCounter counter = node->getCounter();
                            LOG_debug << node->displaypath() << " " << counter.storage << " " << ns->bytes << " " << counter.files << " " << ns->files << " " << counter.folders << " " << ns->folders << " "
                                      << counter.versionStorage << " " << ns->version_bytes << " " << counter.versions << " " << ns->version_files
                                      << (counter.storage == ns->bytes && counter.files == ns->files && counter.folders == ns->folders && counter.versionStorage == ns->version_bytes && counter.versions == ns->version_files
                                          ? "" : " ******************************************* mismatch *******************************************");
                        }
#endif

                        while(json.storeobject());
                        json.leavearray();
                    }

                    json.leaveobject();
                }
                break;

            case MAKENAMEID5('m', 's', 't', 'r', 'g'):
            // maximum storage allowance
                details->storage_max = json.getint();
#ifndef NDEBUG
                got_storage = true;
#endif
                break;

            case MAKENAMEID6('c', 'a', 'x', 'f', 'e', 'r'):
            // PRO transfer quota consumed by yourself
                details->transfer_own_used += json.getint();
                break;

            case MAKENAMEID3('t', 'u', 'o'):
            // Transfer usage by the owner on quotad which hasn't yet been committed back to the API DB. Supplements caxfer
                details->transfer_own_used += json.getint();
                break;

            case MAKENAMEID6('c', 's', 'x', 'f', 'e', 'r'):
            // PRO transfer quota served to others
                details->transfer_srv_used += json.getint();
                break;

            case MAKENAMEID3('t', 'u', 'a'):
            // Transfer usage served to other users which hasn't yet been committed back to the API DB. Supplements csxfer
                details->transfer_srv_used += json.getint();
                break;

            case MAKENAMEID5('m', 'x', 'f', 'e', 'r'):
            // maximum transfer allowance
                details->transfer_max = json.getint();
                break;

            case MAKENAMEID8('s', 'r', 'v', 'r', 'a', 't', 'i', 'o'):
            // The ratio of your PRO transfer quota that is able to be served to others
                details->srv_ratio = json.getfloat();
                break;

            case MAKENAMEID3('r', 't', 't'):
                details->transfer_hist_valid = !json.getint();
                break;

            case MAKENAMEID7('b', 'a', 'l', 'a', 'n', 'c', 'e'):
            // Balance of your account
                if (json.enterarray())
                {
                    const char* cur;
                    const char* amount;

                    while (json.enterarray())
                    {
                        if ((amount = json.getvalue()) && (cur = json.getvalue()))
                        {
                            size_t t = details->balances.size();
                            details->balances.resize(t + 1);
                            details->balances[t].amount = atof(amount);
                            memcpy(details->balances[t].currency, cur, 3);
                            details->balances[t].currency[3] = 0;
                        }

                        json.leavearray();
                    }

                    json.leavearray();
                }
                break;

            case MAKENAMEID4('u', 's', 'l', 'w'):
            // The percentage (in 1000s) indicating the limit at which you are 'nearly' over. Currently 98% for PRO, 90% for free.
                uslw = int(json.getint());
                break;

            case MAKENAMEID8('f', 'e', 'a', 't', 'u', 'r', 'e', 's'):
                if (!json.enterarray())
                {
                    LOG_err << "Failed to parse GetUserQuota response, enter `features` object";
                    client->app->account_details(details.get(), API_EINTERNAL);
                    return false;
                }

                while (json.enterarray())
                {
                    int64_t expiryTimestamp = json.getint();
                    string featureId;
                    json.storeobject(&featureId);
                    details->activeFeatures.push_back({expiryTimestamp, featureId});

                    json.leavearray();
                }

                if (!json.leavearray())
                {
                    LOG_err << "Failed to parse GetUserQuota response, leave `features` object";
                    client->app->account_details(details.get(), API_EINTERNAL);
                    return false;
                }
                break;

            case MAKENAMEID4('s', 'u', 'b', 's'):
            {
                if (!readSubscriptions(&json))
                {
                    LOG_err << "Failed to parse `subs` array in GetUserQuota response";
                    client->app->account_details(details.get(), API_EINTERNAL);
                    return false;
                }
            }
            break;

            case MAKENAMEID5('p', 'l', 'a', 'n', 's'):
            {
                if (!readPlans(&json))
                {
                    LOG_err << "Failed to parse `plans` array in GetUserQuota response";
                    client->app->account_details(details.get(), API_EINTERNAL);
                    return false;
                }
            }
            break;

            case EOO:
                assert(!mStorage || (got_storage && got_storage_used) || client->loggedIntoFolder());

                if (mStorage)
                {
                    if (uslw <= 0)
                    {
                        uslw = 9000;
                        LOG_warn << "Using default almost overstorage threshold";
                    }

                    if (details->storage_used >= details->storage_max)
                    {
                        LOG_debug << "Account full";
                        bool isPaywall = (client->ststatus == STORAGE_PAYWALL);
                        client->activateoverquota(0, isPaywall);
                    }
                    else if (details->storage_used >= (details->storage_max / 10000 * uslw))
                    {
                        LOG_debug << "Few storage space available";
                        client->setstoragestatus(STORAGE_ORANGE);
                    }
                    else
                    {
                        LOG_debug << "There are no storage problems";
                        client->setstoragestatus(STORAGE_GREEN);
                    }
                }

                if (mPro)
                {
                    processPlans();
                }

                client->app->account_details(details.get(), mStorage, mTransfer, mPro, false, false, false);
                if(mCompletion)
                {
                    mCompletion(details, API_OK);
                }
                return true;

            default:
                if (!json.storeobject())
                {
                    client->app->account_details(details.get(), API_EINTERNAL);
                    if(mCompletion)
                    {
                        mCompletion(details, API_EINTERNAL);
                    }
                    return false;
                }
        }
    }
}

bool CommandGetUserQuota::readSubscriptions(JSON* j)
{
    vector<AccountSubscription>& subs = details->subscriptions;

    if (!j->enterarray())
    {
        return false;
    }

    while (j->enterobject())
    {
        AccountSubscription sub;

        bool finishedSubscription = false;
        while (!finishedSubscription)
        {
            switch (j->getnameid())
            {
                case MAKENAMEID2('i', 'd'):
                    // Encrypted subscription ID
                    if (!j->storeobject(&sub.id))
                    {
                        return false;
                    }
                    break;

                case MAKENAMEID4('t', 'y', 'p', 'e'):
                    // 'S' for active payment provider, 'R' otherwise
                    {
                        const char* ptr;
                        if ((ptr = j->getvalue()))
                        {
                            sub.type = *ptr;
                        }
                    }
                    break;

                case MAKENAMEID5('c', 'y', 'c', 'l', 'e'):
                    // Subscription billing period
                    if (!j->storeobject(&sub.cycle))
                    {
                        return false;
                    }
                    break;

                case MAKENAMEID2('g', 'w'):
                    // Payment provider name
                    if (!j->storeobject(&sub.paymentMethod))
                    {
                        return false;
                    }
                    break;

                case MAKENAMEID4('g', 'w', 'i', 'd'):
                    // Payment provider ID
                    sub.paymentMethodId = j->getint32();
                    break;

                case MAKENAMEID4('n', 'e', 'x', 't'):
                    // Renewal time
                    sub.renew = j->getint();
                    break;

                case MAKENAMEID2('a', 'l'):
                    // Account level
                    sub.level = j->getint32();
                    break;

                case MAKENAMEID8('f', 'e', 'a', 't', 'u', 'r', 'e', 's'):
                    // List of features the subscription grants
                    {
                        if (!j->enterobject())
                        {
                            return false;
                        }
                        string key, value;
                        while (j->storeKeyValueFromObject(key, value))
                        {
                            // Check if enabled (value = 1). Disabled features are usually not
                            // present.
                            if (std::stoi(value))
                            {
                                sub.features.push_back(std::move(key));
                            }
                        }
                        if (!j->leaveobject())
                        {
                            return false;
                        }
                    }
                    break;

                case MAKENAMEID8('i', 's', '_', 't', 'r', 'i', 'a', 'l'):
                    // Is an active trial
                    sub.isTrial = j->getbool();
                    break;

                case EOO:
                    subs.push_back(std::move(sub));
                    finishedSubscription = true;
                    break;

                default:
                    if (!j->storeobject())
                    {
                        return false;
                    }
            }
        }
    }

    return j->leavearray();
}

bool CommandGetUserQuota::readPlans(JSON* j)
{
    vector<AccountPlan>& plans = details->plans;

    if (!j->enterarray())
    {
        return false;
    }

    while (j->enterobject())
    {
        AccountPlan plan;

        bool finishedPlan = false;
        while (!finishedPlan)
        {
            switch (j->getnameid())
            {
                case MAKENAMEID2('a', 'l'):
                    // Account level
                    plan.level = j->getint32();
                    break;

                case MAKENAMEID8('f', 'e', 'a', 't', 'u', 'r', 'e', 's'):
                    // List of features the plan grants
                    {
                        if (!j->enterobject())
                        {
                            return false;
                        }
                        string key, value;
                        while (j->storeKeyValueFromObject(key, value))
                        {
                            // Check if enabled (value = 1).
                            // Disabled features are usually not present.
                            if (std::stoi(value))
                            {
                                plan.features.push_back(std::move(key));
                            }
                        }
                        if (!j->leaveobject())
                        {
                            return false;
                        }
                    }
                    break;

                case MAKENAMEID7('e', 'x', 'p', 'i', 'r', 'e', 's'):
                    // The time the plan expires
                    plan.expiration = j->getint();
                    break;

                case MAKENAMEID4('t', 'y', 'p', 'e'):
                    // Why the plan was granted: payment, achievement, etc.
                    // Not included for Bussiness/Pro Flexi
                    plan.type = j->getint32();
                    break;

                // Encrypted subscription ID
                case MAKENAMEID5('s', 'u', 'b', 'i', 'd'):
                    if (!j->storeobject(&plan.subscriptionId))
                    {
                        return false;
                    }
                    break;

                case MAKENAMEID8('i', 's', '_', 't', 'r', 'i', 'a', 'l'):
                    // Is an active trial
                    plan.isTrial = j->getbool();
                    break;

                case EOO:
                    plans.push_back(std::move(plan));
                    finishedPlan = true;
                    break;

                default:
                    if (!j->storeobject())
                    {
                        return false;
                    }
            }
        }
    }

    return j->leavearray();
}

void CommandGetUserQuota::processPlans()
{
    // Inspect plans to detect changes in the account.
    bool proPlanReceived = false;
    bool featurePlanReceived = false;
    bool changed = false;
    for (const auto& plan: details->plans)
    {
        if (plan.isProPlan())
        {
            changed |=
                client->mCachedStatus.addOrUpdate(CacheableStatus::STATUS_PRO_LEVEL, plan.level);
            client->mMyAccount.setProLevel(static_cast<AccountType>(plan.level));
            client->mMyAccount.setProUntil(static_cast<m_time_t>(plan.expiration));
            proPlanReceived = true;
        }
        else // Feature plans
        {
            changed |= client->mCachedStatus.addOrUpdate(CacheableStatus::STATUS_FEATURE_LEVEL,
                                                         plan.level);
            featurePlanReceived = true;
        }
    }

    if (!proPlanReceived)
    {
        // Check if the PRO plan is no longer active.
        changed |= client->mCachedStatus.addOrUpdate(CacheableStatus::STATUS_PRO_LEVEL,
                                                     AccountType::ACCOUNT_TYPE_FREE);
        if (client->mMyAccount.getProLevel() != AccountType::ACCOUNT_TYPE_FREE)
        {
            client->mMyAccount.setProLevel(AccountType::ACCOUNT_TYPE_FREE);
            client->mMyAccount.setProUntil(-1);
        }
    }

    if (!featurePlanReceived)
    {
        // Check if the feature plan is no longer active.
        changed |= client->mCachedStatus.addOrUpdate(CacheableStatus::STATUS_FEATURE_LEVEL,
                                                     ACCOUNT_TYPE_UNKNOWN);
    }

    // Account level (PRO and features) can change without a payment (ie. with
    // coupons or by helpdesk) and in those cases, the `psts` packages
    // are not triggered. However, the SDK should notify the app and resume
    // transfers, etc.
    if (changed)
    {
        client->app->account_updated();
        client->abortbackoff(true);
    }
}

CommandQueryTransferQuota::CommandQueryTransferQuota(MegaClient* client, m_off_t size)
{
    cmd("qbq");
    arg("s", size);

    tag = client->reqtag;
}

bool CommandQueryTransferQuota::procresult(Result r, JSON& json)
{
    if (!r.wasErrorOrOK())
    {
        LOG_err << "Unexpected response: " << json.pos;
        json.storeobject();

        // Returns 0 to not alarm apps and don't show overquota pre-warnings
        // if something unexpected is received, following the same approach as
        // in the webclient
        client->app->querytransferquota_result(0);
        return false;
    }

    client->app->querytransferquota_result(r.errorOrOK());
    return true;
}

CommandGetUserTransactions::CommandGetUserTransactions(MegaClient* client, std::shared_ptr<AccountDetails> ad)
{
    cmd("utt");

    details = ad;
    tag = client->reqtag;
}

bool CommandGetUserTransactions::procresult(Result, JSON& json)
{
    details->transactions.clear();

    while (json.enterarray())
    {
        const char* handle = json.getvalue();
        m_time_t ts = json.getint();
        const char* delta = json.getvalue();
        const char* cur = json.getvalue();

        if (handle && (ts > 0) && delta && cur)
        {
            size_t t = details->transactions.size();
            details->transactions.resize(t + 1);
            memcpy(details->transactions[t].handle, handle, 11);
            details->transactions[t].handle[11] = 0;
            details->transactions[t].timestamp = ts;
            details->transactions[t].delta = atof(delta);
            memcpy(details->transactions[t].currency, cur, 3);
            details->transactions[t].currency[3] = 0;
        }

        if (!json.leavearray())
        {
            client->app->account_details(details.get(), API_EINTERNAL);
            return false;
        }
    }

    client->app->account_details(details.get(), false, false, false, false, true, false);
    return true;
}

CommandGetUserPurchases::CommandGetUserPurchases(MegaClient* client, std::shared_ptr<AccountDetails> ad)
{
    cmd("utp");

    details = ad;
    tag = client->reqtag;
}

bool CommandGetUserPurchases::procresult(Result, JSON& json)
{
    client->restag = tag;

    details->purchases.clear();

    while (json.enterarray())
    {
        const char* handle = json.getvalue();
        const m_time_t ts = json.getint();
        const char* amount = json.getvalue();
        const char* cur = json.getvalue();
        int method = (int)json.getint();

        if (handle && (ts > 0) && amount && cur && (method >= 0))
        {
            size_t t = details->purchases.size();
            details->purchases.resize(t + 1);
            memcpy(details->purchases[t].handle, handle, 11);
            details->purchases[t].handle[11] = 0;
            details->purchases[t].timestamp = ts;
            details->purchases[t].amount = atof(amount);
            memcpy(details->purchases[t].currency, cur, 3);
            details->purchases[t].currency[3] = 0;
            details->purchases[t].method = method;
        }

        if (!json.leavearray())
        {
            client->app->account_details(details.get(), API_EINTERNAL);
            return false;
        }
    }

    client->app->account_details(details.get(), false, false, false, true, false, false);
    return true;
}

CommandGetUserSessions::CommandGetUserSessions(MegaClient* client, std::shared_ptr<AccountDetails> ad)
{
    cmd("usl");
    arg("x", 1); // Request the additional id and alive information
    arg("d", 1); // Request the additional device-id

    details = ad;
    tag = client->reqtag;
}

bool CommandGetUserSessions::procresult(Result, JSON& json)
{
    details->sessions.clear();

    while (json.enterarray())
    {
        size_t t = details->sessions.size();
        details->sessions.resize(t + 1);

        details->sessions[t].timestamp = json.getint();
        details->sessions[t].mru = json.getint();
        json.storeobject(&details->sessions[t].useragent);
        json.storeobject(&details->sessions[t].ip);

        const char* country = json.getvalue();
        memcpy(details->sessions[t].country, country ? country : "\0\0", 2);
        details->sessions[t].country[2] = 0;

        details->sessions[t].current = (int)json.getint();

        details->sessions[t].id = json.gethandle(8);
        details->sessions[t].alive = (int)json.getint();
        json.storeobject(&details->sessions[t].deviceid);

        if (!json.leavearray())
        {
            client->app->account_details(details.get(), API_EINTERNAL);
            return false;
        }
    }

    client->app->account_details(details.get(), false, false, false, false, false, true);
    return true;
}

CommandSetPH::CommandSetPH(MegaClient* client,
                           Node* n,
                           int del,
                           m_time_t cets,
                           bool writable,
                           bool megaHosted,
                           int ctag,
                           CompletionType f)
{
    mSeqtagArray = true;

    h = n->nodehandle;
    ets = cets;
    tag = ctag;
    mCompletion = std::move(f);
    assert(mCompletion);

    cmd("l");
    arg("n", (byte*)&n->nodehandle, MegaClient::NODEHANDLE);

    if (del)
    {
        mDeleting = true;
        arg("d", 1);
    }

    if (ets)
    {
        arg("ets", ets);
    }

    if (writable)
    {
        mWritable = true;
        arg("w", "1");

        if (megaHosted)
        {
            assert(n->sharekey && "attempting to share a key that was not set");

            // generate AES-128 encryption key
            byte encryptionKeyForShareKey[SymmCipher::KEYLENGTH];
            client->rng.genblock(encryptionKeyForShareKey, SymmCipher::KEYLENGTH);

            // encrypt share key with it
            SymmCipher* encrypter =
                client->getRecycledTemporaryNodeCipher(encryptionKeyForShareKey);
            byte encryptedShareKey[SymmCipher::KEYLENGTH] = {};
            encrypter->ecb_encrypt(n->sharekey->key, encryptedShareKey, SymmCipher::KEYLENGTH);

            // send encrypted share key
            arg("sk", encryptedShareKey, SymmCipher::KEYLENGTH);

            // keep the encryption key until the command has succeeded
            mEncryptionKeyForShareKey =
                Base64::btoa(std::string(reinterpret_cast<char*>(encryptionKeyForShareKey),
                                         SymmCipher::KEYLENGTH));
        }
    }
}

void CommandSetPH::completion(Error error, handle nodeHandle, handle publicHandle)
{
    mCompletion(error, nodeHandle, publicHandle, std::move(mEncryptionKeyForShareKey));
}

bool CommandSetPH::procresult(Result r, JSON& json)
{
    // depending on 'w', the response can be [{"ph":"XXXXXXXX","w":"YYYYYYYYYYYYYYYYYYYYYY"}] or simply [XXXXXXXX]
    if (r.hasJsonObject())
    {
        assert(mWritable);
        assert(!mDeleting);
        handle ph = UNDEF;
        std::string authKey;

        bool exit = false;
        while (!exit)
        {
            switch (json.getnameid())
            {
            case 'w':
                json.storeobject(&authKey);
                break;

            case MAKENAMEID2('p', 'h'):
                ph = json.gethandle();
                break;

            case EOO:
            {
                if (!authKey.empty() && !ISUNDEF(ph))
                {
                    completion(API_OK, h, ph);
                    return true;
                }
                exit = true;
                break;
            }
            default:
                if (!json.storeobject())
                {
                    exit = true;
                    break;
                }
            }
        }
    }
    else if (r.hasJsonItem())   // format: [XXXXXXXX]
    {
        assert(!mWritable);
        assert(!mDeleting);
        handle ph = json.gethandle();
        if (!ISUNDEF(ph))
        {
            completion(API_OK, h, ph);
            return true;
        }
    }
    else if (r.wasError(API_OK))
    {
        assert(mDeleting);
        // link removal is done by actionpacket in this case
        completion(r.errorOrOK(), h, UNDEF);
        return true;
    }
    else if (r.wasStrictlyError())
    {
        completion(r.errorOrOK(), h, UNDEF);
        return true;
    }

    completion(API_EINTERNAL, UNDEF, UNDEF);
    return false;
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

bool CommandGetPH::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->openfilelink_result(r.errorOrOK());
        return true;
    }

    m_off_t s = -1;
    string a, fa;

    for (;;)
    {
        switch (json.getnameid())
        {
            case 's':
                s = json.getint();
                break;

            case MAKENAMEID2('a', 't'):
                json.storeobject(&a);
                break;

            case MAKENAMEID2('f', 'a'):
                json.storeobject(&fa);
                break;

            case EOO:
                // we want at least the attributes
                if (s >= 0)
                {
                    a.resize(Base64::atob(a.c_str(), (byte*)a.data(), int(a.size())));

                    if (op == 2)    // importing WelcomePDF for new account
                    {
                        assert(havekey);

                        vector<NewNode> newnodes(1);
                        auto newnode = &newnodes[0];

                        // set up new node
                        newnode->source = NEW_PUBLIC;
                        newnode->type = FILENODE;
                        newnode->nodehandle = ph;
                        newnode->parenthandle = UNDEF;
                        newnode->nodekey.assign((char*)key, FILENODEKEYLENGTH);
                        newnode->attrstring.reset(new string(a));

                        client->putnodes(client->mNodeManager.getRootNodeFiles(), NoVersioning, std::move(newnodes), nullptr, 0, false);
                    }
                    else if (havekey)
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
                return true;

            default:
                if (!json.storeobject())
                {
                    client->app->openfilelink_result(API_EINTERNAL);
                    return false;
                }
        }
    }
}

CommandSetMasterKey::CommandSetMasterKey(MegaClient* client, const byte* newkey, const byte *hash, int hashsize, const byte *clientrandomvalue, const char *pin, string *salt)
{
    mSeqtagArray = true;

    memcpy(this->newkey, newkey, SymmCipher::KEYLENGTH);

    cmd("up");
    arg("k", newkey, SymmCipher::KEYLENGTH);
    if (clientrandomvalue)
    {
        arg("crv", clientrandomvalue, SymmCipher::KEYLENGTH);
    }
    arg("uh", hash, hashsize);
    if (pin)
    {
        arg("mfa", pin);
    }

    if (salt)
    {
        this->salt = *salt;
    }

    tag = client->reqtag;
}

bool CommandSetMasterKey::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        // update encrypted MK and salt for further checkups
        client->k.assign((const char *) newkey, SymmCipher::KEYLENGTH);
        client->accountsalt = salt;

        json.storeobject();
        client->app->changepw_result(API_OK);
        return true;
    }
    else if (r.wasErrorOrOK())
    {
        client->app->changepw_result(r.errorOrOK());
        return true;
    }

    client->app->changepw_result(API_EINTERNAL);
    return false;
}

CommandAccountVersionUpgrade::CommandAccountVersionUpgrade(vector<byte>&& clRandValue, vector<byte>&& encMKey, string&& hashedAuthKey, string&& salt, int ctag,
    std::function<void(error e)> completion)
    : mEncryptedMasterKey(std::move(encMKey)), mSalt(std::move(salt)), mCompletion(completion)
{
    cmd("avu");

    arg("emk", mEncryptedMasterKey.data(), static_cast<int>(mEncryptedMasterKey.size()));
    arg("hak", reinterpret_cast<const byte*>(hashedAuthKey.c_str()), static_cast<int>(hashedAuthKey.size()));
    arg("crv", clRandValue.data(), static_cast<int>(clRandValue.size()));

    tag = ctag;
}

bool CommandAccountVersionUpgrade::procresult(Result r, JSON&)
{
    bool goodJson = r.wasErrorOrOK();
    error e = goodJson ? error(r.errorOrOK()) : API_EINTERNAL;

    if (goodJson)
    {
        if (r.errorOrOK() == API_OK)
        {
            client->accountversion = 2;
            client->k.assign(reinterpret_cast<const char*>(mEncryptedMasterKey.data()), mEncryptedMasterKey.size());
            client->accountsalt = std::move(mSalt);
        }
    }

    if (e == API_OK)
    {
        client->sendevent(99473, "Account successfully upgraded to v2");
    }
    else
    {
        const string& msg = "Account upgrade to v2 has failed (" + std::to_string(e) + ')';
        client->sendevent(99474, msg.c_str());
    }

    if (mCompletion)
    {
        mCompletion(e);
    }

    return goodJson;
}

CommandCreateEphemeralSession::CommandCreateEphemeralSession(MegaClient* client,
                                                             const byte* key,
                                                             const byte* cpw,
                                                             const byte* ssc)
{
    mSeqtagArray = true;

    memcpy(pw, cpw, sizeof pw);

    cmd("up");
    arg("k", key, SymmCipher::KEYLENGTH);
    arg("ts", ssc, 2 * SymmCipher::KEYLENGTH);

    tag = client->reqtag;
}

bool CommandCreateEphemeralSession::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        client->me = json.gethandle(MegaClient::USERHANDLE);
        client->uid = Base64Str<MegaClient::USERHANDLE>(client->me);
        client->resumeephemeral(client->me, pw, tag);
        return true;
    }
    else if (r.wasErrorOrOK())
    {
        client->ephemeralSession = false;
        client->ephemeralSessionPlusPlus = false;
        client->app->ephemeral_result(r.errorOrOK());
        return true;
    }

    client->app->ephemeral_result(API_EINTERNAL);
    return false;
}

CommandResumeEphemeralSession::CommandResumeEphemeralSession(MegaClient*, handle cuh, const byte* cpw, int ctag)
{
    memcpy(pw, cpw, sizeof pw);

    uh = cuh;

    cmd("us");
    arg("user", (byte*)&uh, MegaClient::USERHANDLE);

    tag = ctag;
}

bool CommandResumeEphemeralSession::procresult(Result r, JSON& json)
{
    byte keybuf[SymmCipher::KEYLENGTH];
    byte sidbuf[MegaClient::SIDLEN];
    int havek = 0, havecsid = 0;

    if (r.wasErrorOrOK())
    {
        client->app->ephemeral_result(r.errorOrOK());
        return true;
    }

    for (;;)
    {
        switch (json.getnameid())
        {
            case 'k':
                havek = json.storebinary(keybuf, sizeof keybuf) == sizeof keybuf;
                break;

            case MAKENAMEID4('t', 's', 'i', 'd'):
                havecsid = json.storebinary(sidbuf, sizeof sidbuf) == sizeof sidbuf;
                break;

            case EOO:
                if (!havek || !havecsid)
                {
                    client->app->ephemeral_result(API_EINTERNAL);
                    return false;
                }

                client->sid.assign((const char *)sidbuf, sizeof sidbuf);

                client->key.setkey(pw);
                client->key.ecb_decrypt(keybuf);

                client->key.setkey(keybuf);

                if (!client->checktsid(sidbuf, sizeof sidbuf))
                {
                    client->app->ephemeral_result(API_EKEY);
                    return true;
                }

                client->me = uh;
                client->uid = Base64Str<MegaClient::USERHANDLE>(client->me);

                client->openStatusTable(true);
                client->loadJourneyIdCacheValues();
                client->app->ephemeral_result(uh, pw);
                return true;

            default:
                if (!json.storeobject())
                {
                    client->app->ephemeral_result(API_EINTERNAL);
                    return false;
                }
        }
    }
}

CommandCancelSignup::CommandCancelSignup(MegaClient *client)
{
    cmd("ucr");

    tag = client->reqtag;
}

bool CommandCancelSignup::procresult(Result r, JSON&)
{
    client->app->cancelsignup_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandWhyAmIblocked::CommandWhyAmIblocked(MegaClient *client)
{
    cmd("whyamiblocked");
    batchSeparately = true;  // don't let any other commands that might get batched with it cause the whole batch to fail

    tag = client->reqtag;
}

bool CommandWhyAmIblocked::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (r.wasError(API_OK)) //unblocked
        {
            client->unblock();
        }

        client->app->whyamiblocked_result(r.errorOrOK());
        return true;
    }
    else if (json.isnumeric())
    {
         int response = int(json.getint());
         client->app->whyamiblocked_result(response);
         return true;
    }

    json.storeobject();
    client->app->whyamiblocked_result(API_EINTERNAL);
	return false;
}

CommandSendSignupLink2::CommandSendSignupLink2(MegaClient* client, const char* email, const char* name)
{
    cmd("uc2");
    arg("n", (byte*)name, int(strlen(name)));
    arg("m", (byte*)email, int(strlen(email)));
    arg("v", 2);
    tag = client->reqtag;
}

CommandSendSignupLink2::CommandSendSignupLink2(MegaClient*,
                                               const char* email,
                                               const char* name,
                                               byte* clientrandomvalue,
                                               byte* encmasterkey,
                                               byte* hashedauthkey,
                                               int ctag)
{
    cmd("uc2");
    arg("n", (byte*)name, int(strlen(name)));
    arg("m", (byte*)email, int(strlen(email)));
    arg("crv", clientrandomvalue, SymmCipher::KEYLENGTH);
    arg("hak", hashedauthkey, SymmCipher::KEYLENGTH);
    arg("k", encmasterkey, SymmCipher::KEYLENGTH);
    arg("v", 2);

    tag = ctag;
}

bool CommandSendSignupLink2::procresult(Result r, JSON&)
{
    client->app->sendsignuplink_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandConfirmSignupLink2::CommandConfirmSignupLink2(MegaClient* client,
                                                   const byte* code,
                                                   unsigned len)
{
    mSeqtagArray = true;

    cmd("ud2");
    arg("c", code, len);

    tag = client->reqtag;
}

bool CommandConfirmSignupLink2::procresult(Result r, JSON& json)
{
    string name;
    string email;
    handle uh = UNDEF;
    int version = 0;

    if (r.wasErrorOrOK())
    {
        client->app->confirmsignuplink2_result(UNDEF, NULL, NULL, r.errorOrOK());
        return true;
    }

    assert(r.hasJsonArray());
    if (json.storebinary(&email) && json.storebinary(&name))
    {
        uh = json.gethandle(MegaClient::USERHANDLE);
        version = int(json.getint());
    }
    while (json.storeobject());

    if (!ISUNDEF(uh) && version == 2)
    {
        client->ephemeralSession = false;
        client->app->confirmsignuplink2_result(uh, name.c_str(), email.c_str(), API_OK);
        return true;
    }
    else
    {
        client->app->confirmsignuplink2_result(UNDEF, NULL, NULL, API_EINTERNAL);
        return false;
    }
}

CommandSetKeyPair::CommandSetKeyPair(MegaClient* client, const byte* privk,
                                     unsigned privklen, const byte* pubk,
                                     unsigned pubklen)
{
    mSeqtagArray = true;

    cmd("up");
    arg("privk", privk, privklen);
    arg("pubk", pubk, pubklen);

    tag = client->reqtag;

    len = privklen;
    privkBuffer.reset(new byte[privklen]);
    memcpy(privkBuffer.get(), privk, len);
}

bool CommandSetKeyPair::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        json.storeobject();

        client->key.ecb_decrypt(privkBuffer.get(), len);
        client->mPrivKey.resize(AsymmCipher::MAXKEYLENGTH * 2);
        client->mPrivKey.resize(Base64::btoa(privkBuffer.get(), len, (char *)client->mPrivKey.data()));

        client->app->setkeypair_result(API_OK);
        return true;

    }
    else if (r.wasErrorOrOK())
    {
        client->asymkey.resetkey(); // clear local value, since it failed to set
        client->app->setkeypair_result(r.errorOrOK());
        return true;
    }

    client->app->setkeypair_result(API_EINTERNAL);
    return false;
}

// fetch full node tree
CommandFetchNodes::CommandFetchNodes(MegaClient* client,
                                     int tag,
                                     bool nocache,
                                     bool loadSyncs,
                                     const NodeHandle partialFetchRoot)
{
    assert(client);

    cmd("f");

    // The servers are more efficient with this command when it's the only one in the batch
    batchSeparately = true;

    this->tag = tag;

    if (client->isClientType(MegaClient::ClientType::VPN))
    {
        arg("mc", 1); // the only arg supported by VPN
        return;
    }

    arg("c", 1);
    arg("r", 1);

    if (!nocache)
    {
        arg("ca", 1);
    }

    if (client->isClientType(MegaClient::ClientType::PASSWORD_MANAGER))
    {
        arg("n", partialFetchRoot);
        arg("part", 1);
    }

    // Whether we should (re)load the sync config database on request completion.
    mLoadSyncs = loadSyncs;

    ///////////////////////////////////
    // Filters for parsing in streaming

    // Parsing of chunk started
    mFilters.emplace("<", [this, client](JSON *)
    {
        if (!mFirstChunkProcessed)
        {
            mScsn = 0;
            mSt.clear();
            mPreviousHandleForAlert = UNDEF;
            mMissingParentNodes.clear();

            // make sure the syncs don't see Nodes disappearing
            // they should only look at the nodes again once
            // everything is reloaded and caught up
            // (in case we are reloading mid-session)
            client->statecurrent = false;
            client->actionpacketsCurrent = false;
#ifdef ENABLE_SYNC
            // this just makes sure syncs exit any current tree iteration
            client->syncs.syncRun([]{}, "fetchnodes ready");
#endif

            assert(!mNodeTreeIsChanging.owns_lock());
            mNodeTreeIsChanging = std::unique_lock<mutex>(client->nodeTreeMutex);
            client->purgenodesusersabortsc(true);

            if (client->sctable)
            {
                // reset sc database for brand new node tree (note that we may be reloading mid-session)
                LOG_debug << "Resetting sc database";
                client->sctable->truncate();
                client->sctable->commit();
                client->sctable->begin();
                client->pendingsccommit = false;
            }

            mFirstChunkProcessed = true;
        }
        else
        {
            assert(!mNodeTreeIsChanging.owns_lock());
            mNodeTreeIsChanging = std::unique_lock<mutex>(client->nodeTreeMutex);
        }
        return true;
    });

    // Parsing of chunk finished
    mFilters.emplace(">",
                     [this](JSON*)
                     {
                         assert(mNodeTreeIsChanging.owns_lock());
                         mNodeTreeIsChanging.unlock();
                         return true;
                     });

    // Node objects (one by one)
    auto f = mFilters.emplace("{[f{", [this, client](JSON *json)
    {
        if (client->readnode(json, 0, PUTNODES_APP, nullptr, false, true,
                             mMissingParentNodes, mPreviousHandleForAlert,
                             nullptr, // allParents disabled because Syncs::triggerSync
                             // does nothing when MegaClient::fetchingnodes is true
                             nullptr, nullptr) != 1)
        {
            return false;
        }
        return json->leaveobject();
    });

    // Node versions (one by one)
    mFilters.emplace("{[f2{", f.first->second);

    // End of node array
    f = mFilters.emplace("{[f", [this, client](JSON *json)
    {
        client->mergenewshares(0);
        client->mNodeManager.checkOrphanNodes(mMissingParentNodes);

        // No need to call Syncs::triggerSync here like in MegaClient::readnodes
        // because it does nothing when MegaClient::fetchingnodes is true

        mPreviousHandleForAlert = UNDEF;
        mMissingParentNodes.clear();

        // This is intended to consume the '[' character if the array
        // was empty and an empty array arrives here "[]".
        // If the array was not empty, we receive here only the remaining
        // character ']' and this call doesn't have any effect.
        json->enterarray();

        return json->leavearray();
    });

    // End of node versions array
    mFilters.emplace("{[f2", f.first->second);

    // Legacy keys (one by one)
    mFilters.emplace("{[ok0{", [client](JSON *json)
    {
        if (!json->enterobject())
        {
            return false;
        }

        client->readokelement(json);
        return json->leaveobject();
    });

    // Outgoing shares (one by one)
    f = mFilters.emplace("{[s{", [client](JSON *json)
    {
        if (!json->enterobject())
        {
            return false;
        }

        client->readoutshareelement(json);
        return json->leaveobject();
    });

    // Pending shares (one by one)
    mFilters.emplace("{[ps{", f.first->second);

    // End of outgoing shares array
    f = mFilters.emplace("{[s", [client](JSON *json)
    {
        client->mergenewshares(0);

        json->enterarray();
        return json->leavearray();
    });

    // End of pending shares array
    mFilters.emplace("{[ps", f.first->second);

    // Users (one by one)
    mFilters.emplace("{[u{", [client](JSON *json)
    {
        if (client->readuser(json, false) != 1)
        {
            return false;
        }
        return json->leaveobject();
    });

    // Legacy node key requests (array)
    f = mFilters.emplace("{[cr", [client](JSON *json)
    {
        client->proccr(json);
        return true;
    });

    // Legacy node key requests (object)
    mFilters.emplace("{{cr", f.first->second);

    // Legacy share key requests
    mFilters.emplace("{[sr", [client](JSON *json)
    {
        client->procsr(json);
        return true;
    });

    // sn tag
    mFilters.emplace("{\"sn",
                     [this](JSON* json)
                     {
                         // Not applying the scsn until the end of the parsing
                         // because it could arrive before nodes
                         // (despite at the moment it is arriving at the end)
                         return json->storebinary((byte*)&mScsn, sizeof mScsn) == sizeof mScsn;
                     });

    // st tag
    mFilters.emplace("{\"st",
                     [this](JSON* json)
                     {
                         return json->storeobject(&mSt);
                     });

    // Incoming contact requests
    mFilters.emplace("{[ipc", [client](JSON *json)
    {
        client->readipc(json);
        return true;
    });

    // Outgoing contact requests
    mFilters.emplace("{[opc", [client](JSON *json)
    {
        client->readopc(json);
        return true;
    });

    // Public links (one by one)
    mFilters.emplace("{[ph{", [client](JSON *json)
    {
        if (client->procphelement(json) == 1)
        {
            json->leaveobject();
        }
        return true;
    });

    // Sets and Elements
    mFilters.emplace("{{aesp", [client](JSON *json)
    {
        client->procaesp(*json); // continue even if it failed, it's not critical
        return true;
    });

    // Parsing finished
    mFilters.emplace("{", [this, client](JSON *)
    {
        WAIT_CLASS::bumpds();
        client->fnstats.timeToLastByte = Waiter::ds - client->fnstats.startTime;

        assert(mScsn && "scsn must be received in response to `f` command always");
        if (mScsn)
        {
            client->scsn.setScsn(mScsn);
        }

        if (!mSt.empty())
        {
            client->app->sequencetag_update(mSt);
            client->mScDbStateRecord.seqTag = mSt;
        }

        return parsingFinished();
    });

    // Numeric error, either a number or an error object {"err":XXX}
    mFilters.emplace("#", [this, client](JSON *json)
    {
        // like CommandFetchNodes::procresult when r.wasErrorOrOK() is true but
        // parsing the specific error code here instead of directly receiving it
        WAIT_CLASS::bumpds();
        client->fnstats.timeToLastByte = Waiter::ds - client->fnstats.startTime;

        Error e;
        checkError(e, *json);
        client->fetchingnodes = false;
        client->app->fetchnodes_result(e);
        return true;
    });

    // Parsing error
    mFilters.emplace("E",
                     [client](JSON*)
                     {
                         WAIT_CLASS::bumpds();
                         client->fnstats.timeToLastByte = Waiter::ds - client->fnstats.startTime;
                         client->purgenodesusersabortsc(true);

                         client->fetchingnodes = false;
                         client->mNodeManager.cleanNodes();
                         client->app->fetchnodes_result(API_EINTERNAL);
                         return true;
                     });

#ifdef ENABLE_CHAT
    // Chat-related callbacks
    mFilters.emplace("{{mcf", [client](JSON *json)
    {
        // List of chatrooms
        client->procmcf(json);
        return true;
    });

    f = mFilters.emplace("{[mcpna", [client](JSON *json)
    {
        // nodes shared in chatrooms
        client->procmcna(json);
        return true;
    });
    mFilters.emplace("{[mcna", f.first->second);

    mFilters.emplace("{[mcsm", [client](JSON *json)
    {
        // scheduled meetings
        client->procmcsm(json);
        return true;
    });
#endif
}

CommandFetchNodes::~CommandFetchNodes()
{
    assert(!mNodeTreeIsChanging.owns_lock());
}

const char* CommandFetchNodes::getJSON(MegaClient* client)
{
    // reset all the sc channel state, prevent sending sc requests while fetchnodes is sent
    // we wait until this moment, because when `f` is queued, there may be
    // other commands queued ahead of it, and those may need sc responses in order
    // to fully complete, and so we can't reset these members at that time.
    client->resetScForFetchnodes();

    return Command::getJSON(client);
}

// purge and rebuild node/user tree
bool CommandFetchNodes::procresult(Result r, JSON& json)
{
    WAIT_CLASS::bumpds();
    client->fnstats.timeToLastByte = Waiter::ds - client->fnstats.startTime;

    if (r.wasErrorOrOK())
    {
        client->fetchingnodes = false;
        client->app->fetchnodes_result(r.errorOrOK());
        return true;
    }

    // make sure the syncs don't see Nodes disappearing
    // they should only look at the nodes again once
    // everything is reloaded and caught up
    // (in case we are reloading mid-session)
    client->statecurrent = false;
    client->actionpacketsCurrent = false;
#ifdef ENABLE_SYNC
    // this just makes sure syncs exit any current tree iteration
    client->syncs.syncRun([&](){}, "fetchnodes ready");
#endif
    std::unique_lock<mutex> nodeTreeIsChanging(client->nodeTreeMutex);
    client->purgenodesusersabortsc(true);

    if (client->sctable)
    {
        // reset sc database for brand new node tree (note that we may be reloading mid-session)
        LOG_debug << "Resetting sc database";
        client->sctable->truncate();
        client->sctable->commit();
        client->sctable->begin();
        client->pendingsccommit = false;
    }

    for (;;)
    {
        switch (json.getnameid())
        {
            case 'f':
                // nodes
                if (!client->readnodes(&json, 0, PUTNODES_APP, nullptr, false, true, nullptr, nullptr))
                {
                    client->fetchingnodes = false;
                    client->mNodeManager.cleanNodes();
                    client->app->fetchnodes_result(API_EINTERNAL);
                    return false;
                }
                break;

            case MAKENAMEID2('f', '2'):
                // old versions
                if (!client->readnodes(&json, 0, PUTNODES_APP, nullptr, false, true, nullptr, nullptr))
                {
                    client->fetchingnodes = false;
                    client->mNodeManager.cleanNodes();
                    client->app->fetchnodes_result(API_EINTERNAL);
                    return false;
                }
                break;

            case MAKENAMEID3('o', 'k', '0'):
                // outgoing sharekeys
                client->readok(&json);
                break;

            case 's':
                // Fall through
            case MAKENAMEID2('p', 's'):
                // outgoing or pending shares
                client->readoutshares(&json);
                break;

            case 'u':
                // users/contacts
                if (!client->readusers(&json, false))
                {
                    client->fetchingnodes = false;
                    client->mNodeManager.cleanNodes();
                    client->app->fetchnodes_result(API_EINTERNAL);
                    return false;
                }
                break;

            case MAKENAMEID2('c', 'r'):
                // crypto key request
                client->proccr(&json);
                break;

            case MAKENAMEID2('s', 'r'):
                // sharekey distribution request
                client->procsr(&json);
                break;

            case MAKENAMEID2('s', 'n'):
                // sequence number
                if (!client->scsn.setScsn(&json))
                {
                    client->fetchingnodes = false;
                    client->mNodeManager.cleanNodes();
                    client->app->fetchnodes_result(API_EINTERNAL);
                    return false;
                }
                break;

            case MAKENAMEID2('s', 't'):
                {
                    string st;
                    if (!json.storeobject(&st)) return false;
                    client->app->sequencetag_update(st);
                    client->mScDbStateRecord.seqTag = st;
                }
                break;

            case MAKENAMEID3('i', 'p', 'c'):
                // Incoming pending contact
                client->readipc(&json);
                break;

            case MAKENAMEID3('o', 'p', 'c'):
                // Outgoing pending contact
                client->readopc(&json);
                break;

            case MAKENAMEID2('p', 'h'):
                // Public links handles
                client->procph(&json);
                break;

            case MAKENAMEID4('a', 'e', 's', 'p'):
                // Sets and Elements
                client->procaesp(json); // continue even if it failed, it's not critical
                break;

#ifdef ENABLE_CHAT
            case MAKENAMEID3('m', 'c', 'f'):
                // List of chatrooms
                client->procmcf(&json);
                break;

            case MAKENAMEID5('m', 'c', 'p', 'n', 'a'):   // fall-through
            case MAKENAMEID4('m', 'c', 'n', 'a'):
                // nodes shared in chatrooms
                client->procmcna(&json);
                break;

            case MAKENAMEID4('m', 'c', 's', 'm'):
                // scheduled meetings
                client->procmcsm(&json);
                break;
#endif
            case EOO:
            {
                return parsingFinished();
            }
            default:
                if (!json.storeobject())
                {
                    client->fetchingnodes = false;
                    client->mNodeManager.cleanNodes();
                    client->app->fetchnodes_result(API_EINTERNAL);
                    return false;
                }
        }
    }
}

bool CommandFetchNodes::parsingFinished()
{
    if (!client->scsn.ready())
    {
        client->fetchingnodes = false;
        client->mNodeManager.cleanNodes();
        client->app->fetchnodes_result(API_EINTERNAL);
        return false;
    }

    client->mergenewshares(0);

    client->mNodeManager.initCompleted();  // (nodes already written into DB)

    client->initsc();
    client->pendingsccommit = false;
    client->fetchnodestag = tag;

    WAIT_CLASS::bumpds();
    client->fnstats.timeToCached = Waiter::ds - client->fnstats.startTime;
    client->fnstats.nodesCached = client->mNodeManager.getNodeCount();
#ifdef ENABLE_SYNC
    if (mLoadSyncs)
        client->syncs.loadSyncConfigsOnFetchnodesComplete(true);
#endif
    return true;
}

CommandSubmitPurchaseReceipt::CommandSubmitPurchaseReceipt(MegaClient *client, int type, const char *receipt, handle lph, int phtype, int64_t ts)
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

    if (!ISUNDEF(lph))
    {
        if (phtype == 0) // legacy mode
        {
            arg("aff", (byte*)&lph, MegaClient::NODEHANDLE);
        }
        else
        {
            beginobject("aff");
            arg("id", (byte*)&lph, MegaClient::NODEHANDLE);
            arg("ts", ts);
            arg("t", phtype);   // 1=affiliate id, 2=file/folder link, 3=chat link, 4=contact link
            endobject();
        }
    }

    tag = client->reqtag;
}

bool CommandSubmitPurchaseReceipt::procresult(Result r, JSON&)
{
    client->app->submitpurchasereceipt_result(r.errorOrOK());
    return r.wasErrorOrOK();
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

bool CommandCreditCardStore::procresult(Result r, JSON&)
{
    client->app->creditcardstore_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandCreditCardQuerySubscriptions::CommandCreditCardQuerySubscriptions(MegaClient* client)
{
    cmd("ccqns");

    tag = client->reqtag;
}

bool CommandCreditCardQuerySubscriptions::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->creditcardquerysubscriptions_result(0, r.errorOrOK());
        return true;
    }
    else if (json.isnumeric())
    {
        int number = int(json.getint());
        client->app->creditcardquerysubscriptions_result(number, API_OK);
        return true;
    }
    else
    {
        json.storeobject();
        client->app->creditcardquerysubscriptions_result(0, API_EINTERNAL);
        return false;
    }
}

CommandCreditCardCancelSubscriptions::CancelSubscription::CancelSubscription(const char* reason,
                                                                             const char* id,
                                                                             int canContact):
    mReasoning{reason ? reason : ""},
    mId{id ? id : ""},
    mCanContact{canContact == static_cast<int>(CanContact::Yes) ? CanContact::Yes : CanContact::No}
{}

CommandCreditCardCancelSubscriptions::CancelSubscription::CancelSubscription(
    vector<pair<string, string>>&& reasons,
    const char* id,
    int canContact):
    mReasoning{std::move(reasons)},
    mId{id ? id : ""},
    mCanContact{canContact == static_cast<int>(CanContact::Yes) ? CanContact::Yes : CanContact::No}
{}

CommandCreditCardCancelSubscriptions::CommandCreditCardCancelSubscriptions(
    MegaClient* client,
    const CancelSubscription& cancelSubscription)
{
    cmd("cccs");

    // Cancel Reason(s)
    if (const string* reason = cancelSubscription.getReasoning<string>())
    {
        if (!reason->empty())
        {
            arg("r", reason->c_str());
        }
    }
    else if (const auto* reasons = cancelSubscription.getReasoning<vector<pair<string, string>>>())
    {
        if (!reasons->empty())
        {
            beginarray("r");

            for (const auto& r: *reasons)
            {
                beginobject();
                arg("r", r.first.c_str());
                arg("p", r.second.c_str());
                endobject();
            }

            endarray();
        }
    }

    // The user can be contacted or not
    if (cancelSubscription.canContact())
    {
        arg("cc", static_cast<m_off_t>(CanContact::Yes));
    }

    // Specific subscription ID
    if (!cancelSubscription.getId().empty())
    {
        arg("sub", cancelSubscription.getId().c_str());
    }

    tag = client->reqtag;
}

bool CommandCreditCardCancelSubscriptions::procresult(Result r, JSON&)
{
    client->app->creditcardcancelsubscriptions_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandCopySession::CommandCopySession(MegaClient *client)
{
    cmd("us");
    arg("c", 1);
    batchSeparately = true;  // don't let any other commands that might get batched with it cause the whole batch to fail when blocked
    tag = client->reqtag;
}

// for ephemeral accounts, it returns "tsid" instead of "csid" -> not supported, will return API_EINTERNAL
bool CommandCopySession::procresult(Result r, JSON& json)
{
    string session;
    byte sidbuf[AsymmCipher::MAXKEYLENGTH];
    int len_csid = 0;

    if (r.wasErrorOrOK())
    {
        assert(r.errorOrOK() != API_OK); // API shouldn't return OK, but a session
        client->app->copysession_result(NULL, r.errorOrOK());
        return true;
    }

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID4('c', 's', 'i', 'd'):
                len_csid = json.storebinary(sidbuf, sizeof sidbuf);
                break;

            case EOO:
                if (len_csid < 32)
                {
                    client->app->copysession_result(NULL, API_EINTERNAL);
                    return false;
                }

                if (!client->asymkey.decrypt(sidbuf, len_csid, sidbuf, MegaClient::SIDLEN))
                {
                    client->app->copysession_result(NULL, API_EINTERNAL);
                    return false;
                }

                session.resize(MegaClient::SIDLEN * 4 / 3 + 4);
                session.resize(Base64::btoa(sidbuf, MegaClient::SIDLEN, (char *)session.data()));
                client->app->copysession_result(&session, API_OK);
                return true;

            default:
                if (!json.storeobject())
                {
                    client->app->copysession_result(NULL, API_EINTERNAL);
                    return false;
                }
        }
    }
}

CommandGetPaymentMethods::CommandGetPaymentMethods(MegaClient *client)
{
    cmd("ufpq");
    tag = client->reqtag;
}

bool CommandGetPaymentMethods::procresult(Result r, JSON& json)
{
    int methods = 0;
    int64_t value;

    if (r.wasErrorOrOK())
    {
        if (!r.wasError(API_OK))
        {
            client->app->getpaymentmethods_result(methods, r.errorOrOK());

            //Consume remaining values if they exist
            while(json.isnumeric())
            {
                json.getint();
            }
            return true;
        }

        value = static_cast<int64_t>(error(r.errorOrOK()));
    }
    else if (json.isnumeric())
    {
        value = json.getint();
    }
    else
    {
        LOG_err << "Parse error in ufpq";
        client->app->getpaymentmethods_result(methods, API_EINTERNAL);
        return false;
    }

    methods |= 1 << value;

    while (json.isnumeric())
    {
        value = json.getint();
        if (value < 0)
        {
            client->app->getpaymentmethods_result(methods, static_cast<error>(value));

            //Consume remaining values if they exist
            while(json.isnumeric())
            {
                json.getint();
            }
            return true;
        }

        methods |= 1 << value;
    }

    client->app->getpaymentmethods_result(methods, API_OK);
    return true;
}

CommandSendReport::CommandSendReport(MegaClient *client, const char *type, const char *blob, const char *uid)
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

bool CommandSendReport::procresult(Result r, JSON&)
{
    client->app->userfeedbackstore_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandSendEvent::CommandSendEvent(MegaClient *client, int type, const char *desc, bool addJourneyId, const char *viewId)
{
    cmd("log");
    arg("e", type);
    arg("m", desc);

    // Attach JourneyID
    if (addJourneyId)
    {
        string journeyId = client->getJourneyId();
        if (!journeyId.empty())
        {
            arg("j", journeyId.c_str());
            m_off_t currentms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            arg("ms", currentms);
        }
        else
        {
            LOG_warn << "[CommandSendEvent::CommandSendEvent] Add JourneyID flag is ON, but there is no JourneyID value set";
        }
    }
    // Attach ViewID (generated by the SDK for the client, handled by the client)
    if (viewId && *viewId) // Cannot be empty
    {
        arg("v", viewId);
    }

    tag = client->reqtag;
}

bool CommandSendEvent::procresult(Result r, JSON&)
{
    client->app->sendevent_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandSupportTicket::CommandSupportTicket(MegaClient *client, const char *message, int type)
{
    cmd("sse");
    arg("t", type);
    arg("b", 1);    // base64 encoding for `msg`
    arg("m", (const byte*)message, int(strlen(message)));

    tag = client->reqtag;
}

bool CommandSupportTicket::procresult(Result r, JSON&)
{
    client->app->supportticket_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandCleanRubbishBin::CommandCleanRubbishBin(MegaClient *client)
{
    cmd("dr");

    tag = client->reqtag;
}

bool CommandCleanRubbishBin::procresult(Result r, JSON&)
{
    client->app->cleanrubbishbin_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandGetRecoveryLink::CommandGetRecoveryLink(MegaClient *client, const char *email, int type, const char *pin)
{
    cmd("erm");
    arg("m", email);
    arg("t", type);

    if (type == CANCEL_ACCOUNT && pin)
    {
        arg("mfa", pin);
    }

    tag = client->reqtag;
}

bool CommandGetRecoveryLink::procresult(Result r, JSON&)
{
    client->app->getrecoverylink_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandQueryRecoveryLink::CommandQueryRecoveryLink(MegaClient *client, const char *linkcode)
{
    cmd("erv");
    arg("c", linkcode);

    tag = client->reqtag;
}

bool CommandQueryRecoveryLink::procresult(Result r, JSON& json)
{
    // [<code>,"<email>","<ip_address>",<timestamp>,"<user_handle>",["<email>"]]   (and we are already in the array)
    string email;
    string ip;
    m_time_t ts;
    handle uh;

    if (r.wasStrictlyError())
    {
        client->app->queryrecoverylink_result(r.errorOrOK());
        return true;
    }

    if (!json.isnumeric())
    {
        client->app->queryrecoverylink_result(API_EINTERNAL);
        return false;
    }

    int type = static_cast<int>(json.getint());

    if ( !json.storeobject(&email)  ||
         !json.storeobject(&ip)     ||
         ((ts = json.getint()) == -1) ||
         !(uh = json.gethandle(MegaClient::USERHANDLE)) )
    {
        client->app->queryrecoverylink_result(API_EINTERNAL);
        return false;
    }

    string tmp;
    vector<string> emails;

    // read emails registered for this account
    json.enterarray();
    while (json.storeobject(&tmp))
    {
        emails.push_back(tmp);
        if (*json.pos == ']')
        {
            break;
        }
    }
    json.leavearray();  // emails array

    if (!emails.size()) // there should be at least one email
    {
        client->app->queryrecoverylink_result(API_EINTERNAL);
        return false;
    }

    if (client->loggedin() == FULLACCOUNT && uh != client->me)
    {
        client->app->queryrecoverylink_result(API_EACCESS);
        return true;
    }

    client->app->queryrecoverylink_result(type, email.c_str(), ip.c_str(), time_t(ts), uh, &emails);
    return true;
}

CommandGetPrivateKey::CommandGetPrivateKey(MegaClient *client, const char *code)
{
    mSeqtagArray = true;
    cmd("erx");
    arg("r", "gk");
    arg("c", code);

    tag = client->reqtag;
}

bool CommandGetPrivateKey::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())   // error
    {
        client->app->getprivatekey_result(r.errorOrOK());
        return true;
    }
    else
    {
        byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
        int len_privk = json.storebinary(privkbuf, sizeof privkbuf);

        // account has RSA keypair: decrypt server-provided session ID
        if (len_privk < 256)
        {
            client->app->getprivatekey_result(API_EINTERNAL);
            return false;
        }
        else
        {
            client->app->getprivatekey_result((error)API_OK, privkbuf, len_privk);
            return true;
        }
    }
}

CommandConfirmRecoveryLink::CommandConfirmRecoveryLink(MegaClient *client, const char *code, const byte *hash, int hashsize, const byte *clientrandomvalue, const byte *encMasterKey, const byte *initialSession)
{
    cmd("erx");
    mSeqtagArray = true;

    if (!initialSession)
    {
        arg("r", "sk");
    }

    arg("c", code);

    arg("x", encMasterKey, SymmCipher::KEYLENGTH);
    if (!clientrandomvalue)
    {
        arg("y", hash, hashsize);
    }
    else
    {
        beginobject("y");
        arg("crv", clientrandomvalue, SymmCipher::KEYLENGTH);
        arg("hak", hash, hashsize); //hashed authentication key
        endobject();
    }

    if (initialSession)
    {
        arg("z", initialSession, 2 * SymmCipher::KEYLENGTH);
    }

    tag = client->reqtag;
}

bool CommandConfirmRecoveryLink::procresult(Result r, JSON&)
{
    client->app->confirmrecoverylink_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandConfirmCancelLink::CommandConfirmCancelLink(MegaClient *client, const char *code)
{
    cmd("erx");
    arg("c", code);

    tag = client->reqtag;
}

bool CommandConfirmCancelLink::procresult(Result r, JSON&)
{
    MegaApp *app = client->app;
    app->confirmcancellink_result(r.errorOrOK());
    if (r.wasError(API_OK))
    {
        app->request_error(API_ESID);
    }
    return r.wasErrorOrOK();
}

CommandResendVerificationEmail::CommandResendVerificationEmail(MegaClient *client)
{
    cmd("era");
    batchSeparately = true;  // don't let any other commands that might get batched with it cause the whole batch to fail

    tag = client->reqtag;
}

bool CommandResendVerificationEmail::procresult(Result r, JSON&)
{
    client->app->resendverificationemail_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandResetSmsVerifiedPhoneNumber::CommandResetSmsVerifiedPhoneNumber(MegaClient *client)
{
    cmd("smsr");
    tag = client->reqtag;
}

bool CommandResetSmsVerifiedPhoneNumber::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        client->mSmsVerifiedPhone.clear();
    }
    client->app->resetSmsVerifiedPhoneNumber_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandValidatePassword::CommandValidatePassword(MegaClient *client, const char *email, const vector<byte>& authKey)
{
    cmd("us");
    arg("user", email);
    arg("uh", authKey.data(), (int)authKey.size());

    tag = client->reqtag;
}

bool CommandValidatePassword::procresult(Result r, JSON&)
{
    if (r.wasErrorOrOK())
    {
        client->app->validatepassword_result(r.errorOrOK());
        return true;
    }
    else
    {
        assert(r.hasJsonObject());  // we don't use the object contents, and will exit the object automatically
        client->app->validatepassword_result(API_OK);
        return r.hasJsonObject();
    }
}

CommandGetEmailLink::CommandGetEmailLink(MegaClient *client, const char *email, int add, const char *pin)
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
    if (pin)
    {
        arg("mfa", pin);
    }

    tag = client->reqtag;
}

bool CommandGetEmailLink::procresult(Result r, JSON&)
{
    client->app->getemaillink_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandConfirmEmailLink::CommandConfirmEmailLink(MegaClient *client, const char *code, const char *email, const byte *newLoginHash, bool replace)
{
    this->email = email;
    this->replace = replace;

    cmd("sec");

    arg("c", code);
    arg("e", email);
    if (newLoginHash)
    {
        arg("uh", newLoginHash, sizeof(uint64_t));
    }
    if (replace)
    {
        arg("r", 1);    // replace the current email address by this one
    }
    notself(client);

    tag = client->reqtag;
}

bool CommandConfirmEmailLink::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        User *u = client->finduser(client->me);

        if (replace)
        {
            LOG_debug << "Email changed from `" << u->email << "` to `" << email << "`";
            client->setEmail(u, email);
        }
        // TODO: once we manage multiple emails, add the new email to the list of emails
    }

    client->app->confirmemaillink_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandGetVersion::CommandGetVersion(MegaClient *client, const char *appKey)
{
    this->client = client;
    cmd("lv");
    arg("a", appKey);
    tag = client->reqtag;
}

bool CommandGetVersion::procresult(Result r, JSON& json)
{
    int versioncode = 0;
    string versionstring;

    if (r.wasErrorOrOK())
    {
        client->app->getversion_result(0, NULL, r.errorOrOK());
        return r.wasErrorOrOK();
    }

    assert(r.hasJsonObject());
    for (;;)
    {
        switch (json.getnameid())
        {
            case 'c':
                versioncode = int(json.getint());
                break;

            case 's':
                json.storeobject(&versionstring);
                break;

            case EOO:
                client->app->getversion_result(versioncode, versionstring.c_str(), API_OK);
                return true;

            default:
                if (!json.storeobject())
                {
                    client->app->getversion_result(0, NULL, API_EINTERNAL);
                    return false;
                }
        }
    }
}

CommandGetLocalSSLCertificate::CommandGetLocalSSLCertificate(MegaClient *client)
{
    this->client = client;
    cmd("lc");
    arg("v", 1);

    tag = client->reqtag;
}

bool CommandGetLocalSSLCertificate::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->getlocalsslcertificate_result(0, NULL, r.errorOrOK());
        return true;
    }

    assert(r.hasJsonObject());
    string certdata;
    m_time_t ts = 0;
    int numelements = 0;

    for (;;)
    {
        switch (json.getnameid())
        {
            case 't':
            {
                ts = json.getint();
                break;
            }
            case 'd':
            {
                string data;
                json.enterarray();
                while (json.storeobject(&data))
                {
                    if (numelements)
                    {
                        certdata.append(";");
                    }
                    numelements++;
                    certdata.append(data);
                }
                json.leavearray();
                break;
            }
            case EOO:
            {
                if (numelements < 2)
                {
                    client->app->getlocalsslcertificate_result(0, NULL, API_EINTERNAL);
                    return false;
                }
                client->app->getlocalsslcertificate_result(ts, &certdata, API_OK);
                return true;
            }

            default:
                if (!json.storeobject())
                {
                    client->app->getlocalsslcertificate_result(0, NULL, API_EINTERNAL);
                    return false;
                }
        }
    }
}

#ifdef ENABLE_CHAT
CommandChatCreate::CommandChatCreate(MegaClient* client, bool group, bool publicchat, const userpriv_vector* upl, const string_map* ukm, const char* title, bool meetingRoom, int chatOptions, const ScheduledMeeting* schedMeeting)
{
    this->client = client;
    this->chatPeers = new userpriv_vector(*upl);
    this->mPublicChat = publicchat;
    this->mTitle = title ? string(title) : "";
    this->mUnifiedKey = "";
    mMeeting = meetingRoom;


    cmd("mcc");
    arg("g", (group) ? 1 : 0);

    if (group && title)
    {
        arg("ct", title);
    }

    if (publicchat)
    {
        arg("m", 1);

        char ownHandleB64[12];
        Base64::btoa((byte *)&client->me, MegaClient::USERHANDLE, ownHandleB64);
        ownHandleB64[11] = '\0';

        string_map::const_iterator it = ukm->find(ownHandleB64);
        if (it != ukm->end())
        {
            mUnifiedKey = it->second;
            arg("ck", mUnifiedKey.c_str());
        }
    }

    if (meetingRoom)
    {
        arg("mr", 1);
    }

    if (group)
    {
        mChatOptions.set(static_cast<ChatOptions_t>(chatOptions));
        if (mChatOptions.speakRequest()) {arg("sr", 1);}
        if (mChatOptions.waitingRoom())  {arg("w", 1);}
        if (mChatOptions.openInvite())   {arg("oi", 1);}
    }

    beginarray("u");

    userpriv_vector::iterator itupl;
    for (itupl = chatPeers->begin(); itupl != chatPeers->end(); itupl++)
    {
        beginobject();

        handle uh = itupl->first;
        privilege_t priv = itupl->second;

        arg("u", (byte *)&uh, MegaClient::USERHANDLE);
        arg("p", priv);

        if (publicchat)
        {
            char uid[12];
            Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
            uid[11] = '\0';

            string_map::const_iterator ituk = ukm->find(uid);
            if(ituk != ukm->end())
            {
                arg("ck", ituk->second.c_str());
            }
        }
        endobject();
    }

    endarray();

    // create a scheduled meeting along with chatroom
    if (schedMeeting)
    {
        mSchedMeeting.reset(schedMeeting->copy()); // can avoid copy by mooving check from where we are calling
        beginobject("sm");
        arg("a", "mcsmp");
        createSchedMeetingJson(mSchedMeeting.get());
        endobject();
    }

    arg("v", 1);
    notself(client);

    tag = client->reqtag;
}

bool CommandChatCreate::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->chatcreate_result(NULL, r.errorOrOK());
        delete chatPeers;
        return true;
    }
    else
    {
        handle chatid = UNDEF;
        handle schedId = UNDEF;
        int shard = -1;
        bool group = false;
        m_time_t ts = -1;
        std::vector<std::unique_ptr<ScheduledMeeting>> schedMeetings;
        UserAlert::UpdatedScheduledMeeting::Changeset cs;
        bool exit = false;
        bool addSchedMeeting = false;

        while (!exit)
        {
            switch (json.getnameid())
            {
                case MAKENAMEID2('i','d'):
                    chatid = json.gethandle(MegaClient::CHATHANDLE);
                    break;

                case MAKENAMEID2('c','s'):
                    shard = int(json.getint());
                    break;

                case 'g':
                    group = json.getbool();
                    break;

                case MAKENAMEID2('t', 's'):  // actual creation timestamp
                    ts = json.getint();
                    break;

                case MAKENAMEID2('s', 'm'):
                {
                    addSchedMeeting = !json.isnumeric();
                    if (addSchedMeeting)
                    {
                        schedId = json.gethandle(MegaClient::CHATHANDLE);
                    }
                    else
                    {
                        LOG_err << "Error creating a scheduled meeting along with chat. chatId [" <<  Base64Str<MegaClient::CHATHANDLE>(chatid) << "]";
                        assert(false);
                    }
                    break;
                }
                case EOO:
                    exit = true;
                    break;

                default:
                    if (!json.storeobject())
                    {
                        client->app->chatcreate_result(NULL, API_EINTERNAL);
                        delete chatPeers;   // unused, but might be set at creation
                        return false;
                    }
            }
        }

        if (chatid != UNDEF && shard != -1)
        {
            if (addSchedMeeting)
            {
                if (mSchedMeeting)
                {
                    mSchedMeeting->setSchedId(schedId);
                    mSchedMeeting->setChatid(chatid);
                    if (!mSchedMeeting->isValid())
                    {
                        client->reportInvalidSchedMeeting(mSchedMeeting.get());
                        addSchedMeeting = false;
                    }
                }
                else
                {
                    LOG_err << "Scheduled meeting id received upon mcc command, but there's no local "
                               "scheduled meeting data. chatId [" << toHandle(chatid) << "]";
                    addSchedMeeting = false;
                    assert(false);
                }
            }

            TextChat* chat = nullptr;
            if (client->chats.find(chatid) == client->chats.end())
            {
                chat = new TextChat(mPublicChat);
                client->chats[chatid] = chat;
            }
            else
            {
                chat = client->chats[chatid];
                client->setChatMode(chat, mPublicChat);
            }

            chat->setChatId(chatid);
            chat->setOwnPrivileges(PRIV_MODERATOR);
            chat->setShard(shard);
            chat->setUserPrivileges(chatPeers);
            chat->setGroup(group);
            chat->setTs(ts != -1 ? ts : 0);
            chat->setMeeting(mMeeting);
            // no need to fetch scheduled meetings as we have just created the chat, so it doesn't have any

            if (group) // we are creating a chat, so we need to initialize all chat options enabled/disabled
            {
                chat->addOrUpdateChatOptions(mChatOptions.speakRequest(), mChatOptions.waitingRoom(), mChatOptions.openInvite());
            }

            chat->setTag(tag ? tag : -1);
            if (chat->getGroup() && !mTitle.empty())
            {
                chat->setTitle(mTitle);
            }
            if (mPublicChat)
            {
                chat->setUnifiedKey(mUnifiedKey);
            }

            if (addSchedMeeting && !chat->addOrUpdateSchedMeeting(std::move(mSchedMeeting)))
            {
                LOG_err << "Error adding a new scheduled meeting with schedId [" <<  Base64Str<MegaClient::CHATHANDLE>(schedId) << "]";
            }

            client->notifychat(chat);
            client->app->chatcreate_result(chat, API_OK);
        }
        else
        {
            client->app->chatcreate_result(NULL, API_EINTERNAL);
            delete chatPeers;   // unused, but might be set at creation
        }
        return true;
    }
}

CommandSetChatOptions::CommandSetChatOptions(MegaClient* client, handle chatid, int option, bool enabled, CommandSetChatOptionsCompletion completion)
    : mCompletion(completion)
{
    this->client = client;
    mChatid = chatid;
    mOption = option;
    mEnabled = enabled;

    cmd("mco");
    arg("cid", (byte*)&chatid, MegaClient::CHATHANDLE);
    switch (option)
    {
        case ChatOptions::kOpenInvite:      arg("oi", enabled);  break;
        case ChatOptions::kSpeakRequest:    arg("sr", enabled);  break;
        case ChatOptions::kWaitingRoom:     arg("w", enabled);   break;
        default:                                                 break;
    }

    notself(client); // set i param to ignore action packet generated by our own action
    tag = client->reqtag;
}

bool CommandSetChatOptions::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        auto it = client->chats.find(mChatid);
        if (it == client->chats.end())
        {
            mCompletion(API_EINTERNAL);
            return true;
        }

        // chat options: [-1 (not updated) | 0 (remove) | 1 (add)]
        int speakRequest = mOption == ChatOptions::kSpeakRequest ? mEnabled : -1;
        int waitingRoom  = mOption == ChatOptions::kWaitingRoom  ? mEnabled : -1;
        int openInvite   = mOption == ChatOptions::kOpenInvite   ? mEnabled : -1;

        TextChat* chat = it->second;
        chat->addOrUpdateChatOptions(speakRequest, waitingRoom, openInvite);
        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    mCompletion(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandChatInvite::CommandChatInvite(MegaClient *client, handle chatid, handle uh, privilege_t priv, const char *unifiedkey, const char* title)
{
    this->client = client;
    this->chatid = chatid;
    this->uh = uh;
    this->priv = priv;
    this->title = title ? string(title) : "";

    cmd("mci");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("u", (byte *)&uh, MegaClient::USERHANDLE);
    arg("p", priv);
    arg("v", 1);

    if (title)
    {
        arg("ct", title);
    }

    if (unifiedkey)
    {
        arg("ck", unifiedkey);
    }

    notself(client);

    tag = client->reqtag;
}

bool CommandChatInvite::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        if (client->chats.find(chatid) == client->chats.end())
        {
            // the invitation succeed for a non-existing chatroom
            client->app->chatinvite_result(API_EINTERNAL);
            return true;
        }

        TextChat *chat = client->chats[chatid];
        chat->addUserPrivileges(uh, priv);

        if (!title.empty())  // only if title was set for this chatroom, update it
        {
            chat->setTitle(title);
        }

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chatinvite_result(r.errorOrOK());
    return r.wasErrorOrOK();
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
        arg("u", (byte *)&uh, MegaClient::USERHANDLE);
    }
    arg("v", 1);
    notself(client);

    tag = client->reqtag;
}

bool CommandChatRemove::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        if (client->chats.find(chatid) == client->chats.end())
        {
            // the invitation succeed for a non-existing chatroom
            client->app->chatremove_result(API_EINTERNAL);
            return true;
        }

        TextChat *chat = client->chats[chatid];
        if (!chat->removeUserPrivileges(uh))
        {
            if (uh != client->me)
            {
                // the removal succeed, but the list of peers is empty
                client->app->chatremove_result(API_EINTERNAL);
                return true;
            }
        }

        if (uh == client->me)
        {
            chat->setOwnPrivileges(PRIV_RM);

            // clear the list of peers (if re-invited, peers will be re-added)
            chat->setUserPrivileges(nullptr);
        }

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chatremove_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandChatURL::CommandChatURL(MegaClient *client, handle chatid)
{
    mSeqtagArray = true;

    this->client = client;

    cmd("mcurl");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("v", 1);

    tag = client->reqtag;
}

bool CommandChatURL::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        string url;
        if (json.storeobject(&url))
        {
            client->app->chaturl_result(&url, API_OK);
            return true;
        }
    }
    else if (r.wasErrorOrOK())
    {
        client->app->chaturl_result(NULL, r.errorOrOK());
        return true;
    }

    client->app->chaturl_result(NULL, API_EINTERNAL);
    return false;
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

bool CommandChatGrantAccess::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        if (client->chats.find(chatid) == client->chats.end())
        {
            // the action succeed for a non-existing chatroom??
            client->app->chatgrantaccess_result(API_EINTERNAL);
            return true;
        }

        TextChat *chat = client->chats[chatid];
        chat->setNodeUserAccess(h, uh);

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chatgrantaccess_result(r.errorOrOK());
    return r.wasErrorOrOK();
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

bool CommandChatRemoveAccess::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        if (client->chats.find(chatid) == client->chats.end())
        {
            client->app->chatremoveaccess_result(API_EINTERNAL);
            return true;
        }

        TextChat *chat = client->chats[chatid];
        chat->setNodeUserAccess(h, uh, true);

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chatremoveaccess_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandChatUpdatePermissions::CommandChatUpdatePermissions(MegaClient *client, handle chatid, handle uh, privilege_t priv)
{
    this->client = client;
    this->chatid = chatid;
    this->uh = uh;
    this->priv = priv;

    cmd("mcup");
    arg("v", 1);

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("u", (byte *)&uh, MegaClient::USERHANDLE);
    arg("p", priv);
    notself(client);

    tag = client->reqtag;
}

bool CommandChatUpdatePermissions::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        if (client->chats.find(chatid) == client->chats.end())
        {
            client->app->chatupdatepermissions_result(API_EINTERNAL);
            return true;
        }

        TextChat *chat = client->chats[chatid];
        if (uh != client->me)
        {
            if (!chat->updateUserPrivileges(uh, priv))
            {
                // the update succeed, but that peer is not included in the chatroom
                client->app->chatupdatepermissions_result(API_EINTERNAL);
                return true;
            }
        }
        else
        {
            chat->setOwnPrivileges(priv);
        }

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chatupdatepermissions_result(r.errorOrOK());
    return r.wasErrorOrOK();
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

bool CommandChatTruncate::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        if (client->chats.find(chatid) == client->chats.end())
        {
            // the truncation succeed for a non-existing chatroom
            client->app->chattruncate_result(API_EINTERNAL);
            return true;
        }

        TextChat *chat = client->chats[chatid];
        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chattruncate_result(r.errorOrOK());
    return r.wasErrorOrOK();
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

bool CommandChatSetTitle::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        if (client->chats.find(chatid) == client->chats.end())
        {
            // the invitation succeed for a non-existing chatroom
            client->app->chatsettitle_result(API_EINTERNAL);
            return true;
        }

        TextChat *chat = client->chats[chatid];
        chat->setTitle(title);

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chatsettitle_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandChatPresenceURL::CommandChatPresenceURL(MegaClient *client)
{
    mSeqtagArray = true;
    this->client = client;
    cmd("pu");
    tag = client->reqtag;
}

bool CommandChatPresenceURL::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        string url;
        if (json.storeobject(&url))
        {
            client->app->chatpresenceurl_result(&url, API_OK);
            return true;
        }
    }
    else if (r.wasErrorOrOK())
    {
        client->app->chatpresenceurl_result(NULL, r.errorOrOK());
        return true;
    }

    client->app->chatpresenceurl_result(NULL, API_EINTERNAL);
    return false;
}

CommandRegisterPushNotification::CommandRegisterPushNotification(MegaClient *client, int deviceType, const char *token)
{
    this->client = client;
    cmd("spt");
    arg("p", deviceType);
    arg("t", token);

    tag = client->reqtag;
}

bool CommandRegisterPushNotification::procresult(Result r, JSON&)
{
    client->app->registerpushnotification_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandArchiveChat::CommandArchiveChat(MegaClient *client, handle chatid, bool archive)
{
    this->mChatid = chatid;
    this->mArchive = archive;

    cmd("mcsf");

    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("m", 1);
    arg("f", archive);

    notself(client);

    tag = client->reqtag;
}

bool CommandArchiveChat::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        textchat_map::iterator it = client->chats.find(mChatid);
        if (it == client->chats.end())
        {
            LOG_err << "Archive chat succeeded for a non-existing chatroom";
            client->app->archivechat_result(API_ENOENT);
            return true;
        }

        TextChat *chat = it->second;
        chat->setFlag(mArchive, TextChat::FLAG_OFFSET_ARCHIVE);

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->archivechat_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandSetChatRetentionTime::CommandSetChatRetentionTime(MegaClient *client, handle chatid, unsigned period)
{
    mChatid = chatid;

    cmd("mcsr");
    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("d", period);
    arg("ds", 1);
    tag = client->reqtag;
}

bool CommandSetChatRetentionTime::procresult(Result r, JSON&)
{
    client->app->setchatretentiontime_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandRichLink::CommandRichLink(MegaClient *client, const char *url)
{
    cmd("erlsd");

    arg("url", url);

    tag = client->reqtag;
}

bool CommandRichLink::procresult(Result r, JSON& json)
{
    // error format: [{"error":<code>}]
    // result format: [{"result":{
    //                      "url":"<url>",
    //                      "t":"<title>",
    //                      "d":"<description>",
    //                      "ic":"<format>:<icon_B64>",
    //                      "i":"<format>:<image>"}}]

    if (r.wasErrorOrOK())
    {
        client->app->richlinkrequest_result(NULL, r.errorOrOK());
        return true;
    }


    string res;
    int errCode = 0;
    string metadata;
    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID5('e', 'r', 'r', 'o', 'r'):
                errCode = int(json.getint());
                break;

            case MAKENAMEID6('r', 'e', 's', 'u', 'l', 't'):
                json.storeobject(&metadata);
                break;

            case EOO:
            {
                error e = API_EINTERNAL;
                if (!metadata.empty())
                {
                    client->app->richlinkrequest_result(&metadata, API_OK);
                    return true;
                }
                else if (errCode)
                {
                    switch(errCode)
                    {
                        case 403:
                            e = API_EACCESS;
                            break;

                        case 404:
                            e = API_ENOENT;
                            break;

                        default:
                            e = API_EINTERNAL;
                            break;
                    }
                }

                client->app->richlinkrequest_result(NULL, e);
                return true;
            }

            default:
                if (!json.storeobject())
                {
                    client->app->richlinkrequest_result(NULL, API_EINTERNAL);
                    return false;
                }
        }
    }
}

CommandChatLink::CommandChatLink(MegaClient *client, handle chatid, bool del, bool createifmissing)
{
    mSeqtagArray = true;

    mDelete = del;

    cmd("mcph");
    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);

    if (del)
    {
        arg("d", 1);
    }

    if (!createifmissing)
    {
        arg("cim", (m_off_t)0);
    }

    tag = client->reqtag;
}

bool CommandChatLink::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        assert(!mDelete);
        handle h = json.gethandle(MegaClient::CHATLINKHANDLE);
        if (!ISUNDEF(h))
        {
            client->app->chatlink_result(h, API_OK);
            return true;
        }
    }
    else if (r.wasErrorOrOK())
    {
        client->app->chatlink_result(UNDEF, r.errorOrOK());
        return true;
    }

    LOG_err << "Unexpected response for create/get chatlink";
    client->app->chatlink_result(UNDEF, API_EINTERNAL);
    return false;
}

CommandChatLinkURL::CommandChatLinkURL(MegaClient *client, handle publichandle)
{
    cmd("mcphurl");
    arg("ph", (byte*)&publichandle, MegaClient::CHATLINKHANDLE);

    tag = client->reqtag;
}

bool CommandChatLinkURL::procresult(Result r, JSON& json)
{
    if (r.wasStrictlyError())
    {
        client->app->chatlinkurl_result(UNDEF, -1, NULL, NULL, -1, 0, false, ChatOptions::kEmpty, nullptr, UNDEF, r.errorOrOK());
        return true;
    }
    else
    {
        handle chatid = UNDEF;
        int shard = -1;
        int numPeers = -1;
        string url;
        string ct;
        m_time_t ts = 0;
        bool meetingRoom = false;
        bool waitingRoom = false;
        bool openInvite = false;
        bool speakRequest = false;

        std::vector<std::unique_ptr<ScheduledMeeting>> schedMeetings;
        handle callid = UNDEF;

        for (;;)
        {
            switch (json.getnameid())
            {
                case MAKENAMEID2('i','d'): // chatid
                    chatid = json.gethandle(MegaClient::CHATHANDLE);
                    break;

                case MAKENAMEID2('c','s'): // shard
                    shard = int(json.getint());
                    break;

                case MAKENAMEID2('c','t'):  // chat-title
                    json.storeobject(&ct);
                    break;

                case MAKENAMEID3('u','r','l'): // chaturl
                    json.storeobject(&url);
                    break;

                case MAKENAMEID3('n','c','m'): // number of members in the chat
                    numPeers = int(json.getint());
                    break;

                case MAKENAMEID2('t', 's'): // chat creation timestamp
                    ts = json.getint();
                    break;

                case MAKENAMEID6('c', 'a', 'l', 'l', 'I', 'd'): //callId if there is an active call (just if mr == 1)
                    callid = json.gethandle(MegaClient::CHATHANDLE);
                    break;

                case MAKENAMEID2('m', 'r'): // meeting room
                    meetingRoom = json.getbool();
                    break;

                case MAKENAMEID1('w'): // waiting room
                    waitingRoom = json.getbool();
                    break;

                case MAKENAMEID2('s','r'):
                    speakRequest = json.getbool();
                    break;

                case MAKENAMEID2('o','i'):
                    openInvite = json.getbool();
                    break;

                case MAKENAMEID2('s', 'm'): // scheduled meetings
                {
                    if (json.enterarray())
                    {
                        error err = client->parseScheduledMeetings(schedMeetings, false, &json);
                        if (!json.leavearray() || err)
                        {
                            LOG_err << "Failed to parse mcphurl respone. Error: " << err;
                            client->app->chatlinkurl_result(UNDEF, -1, NULL, NULL, -1, 0, false, false, nullptr, UNDEF, API_EINTERNAL);
                            return false;
                        }
                    }
                    break;
                }
                case EOO:
                    if (chatid != UNDEF && shard != -1 && !url.empty() && !ct.empty() && numPeers != -1)
                    {
                        client->app->chatlinkurl_result(chatid, shard, &url, &ct, numPeers, ts, meetingRoom,
							ChatOptions(speakRequest, waitingRoom, openInvite).value(),
							&schedMeetings, callid, API_OK);
                    }
                    else
                    {
                        client->app->chatlinkurl_result(UNDEF, -1, NULL, NULL, -1, 0, false, ChatOptions::kEmpty, nullptr, UNDEF, API_EINTERNAL);
                    }
                    return true;

                default:
                    if (!json.storeobject())
                    {
                        client->app->chatlinkurl_result(UNDEF, -1, NULL, NULL, -1, 0, false, ChatOptions::kEmpty, nullptr, UNDEF, API_EINTERNAL);
                        return false;
                    }
            }
        }
    }
}

CommandChatLinkClose::CommandChatLinkClose(MegaClient *client, handle chatid, const char *title)
{
    mChatid = chatid;
    mTitle = title ? string(title) : "";

    cmd("mcscm");
    arg("id", (byte*)&chatid, MegaClient::CHATHANDLE);

    if (title)
    {
        arg("ct", title);
    }

    notself(client);
    tag = client->reqtag;
}

bool CommandChatLinkClose::procresult(Result r, JSON&)
{
    if (r.wasError(API_OK))
    {
        textchat_map::iterator it = client->chats.find(mChatid);
        if (it == client->chats.end())
        {
            LOG_err << "Chat link close succeeded for a non-existing chatroom";
            client->app->chatlinkclose_result(API_ENOENT);
            return true;
        }

        TextChat *chat = it->second;
        client->setChatMode(chat, false);
        if (!mTitle.empty())
        {
            chat->setTitle(mTitle);
        }

        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    client->app->chatlinkclose_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandChatLinkJoin::CommandChatLinkJoin(MegaClient *client, handle publichandle, const char *unifiedkey)
{
    cmd("mciph");
    arg("ph", (byte*)&publichandle, MegaClient::CHATLINKHANDLE);
    arg("ck", unifiedkey);
    tag = client->reqtag;
}

bool CommandChatLinkJoin::procresult(Result r, JSON&)
{
    client->app->chatlinkjoin_result(r.errorOrOK());
    return r.wasErrorOrOK();
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

bool CommandGetMegaAchievements::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->getmegaachievements_result(details, r.errorOrOK());
        return true;
    }

    details->permanent_size = 0;
    details->achievements.clear();
    details->awards.clear();
    details->rewards.clear();

    for (;;)
    {
        switch (json.getnameid())
        {
            case 's':
                details->permanent_size = json.getint();
                break;

            case 'u':
                if (json.enterobject())
                {
                    for (;;)
                    {
                        achievement_class_id id = achievement_class_id(json.getnameid());
                        if (id == EOO)
                        {
                            break;
                        }
                        id -= '0';   // convert to number

                        if (json.enterarray())
                        {
                            Achievement achievement;
                            achievement.storage = json.getint();
                            achievement.transfer = json.getint();
                            const char *exp_ts = json.getvalue();
                            char *pEnd = NULL;
                            achievement.expire = int(strtol(exp_ts, &pEnd, 10));
                            if (*pEnd == 'm')
                            {
                                achievement.expire *= 30;
                            }
                            else if (*pEnd == 'y')
                            {
                                achievement.expire *= 365;
                            }

                            details->achievements[id] = achievement;

                            while(json.storeobject());
                            json.leavearray();
                        }
                    }

                    json.leaveobject();
                }
                else
                {
                    LOG_err << "Failed to parse Achievements of MEGA achievements";
                    json.storeobject();
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return false;
                }
                break;

            case 'a':
                if (json.enterarray())
                {
                    while (json.enterobject())
                    {
                        Award award;
                        award.achievement_class = 0;
                        award.award_id = 0;
                        award.ts = 0;
                        award.expire = 0;

                        bool finished = false;
                        while (!finished)
                        {
                            switch (json.getnameid())
                            {
                            case 'a':
                                award.achievement_class = achievement_class_id(json.getint());
                                break;
                            case 'r':
                                award.award_id = int(json.getint());
                                break;
                            case MAKENAMEID2('t', 's'):
                                award.ts = json.getint();
                                break;
                            case 'e':
                                award.expire = json.getint();
                                break;
                            case 'm':
                                if (json.enterarray())
                                {
                                    string email;
                                    while(json.storeobject(&email))
                                    {
                                        award.emails_invited.push_back(email);
                                    }

                                    json.leavearray();
                                }
                                break;
                            case EOO:
                                finished = true;
                                break;
                            default:
                                json.storeobject();
                                break;
                            }
                        }

                        details->awards.push_back(award);

                        json.leaveobject();
                    }

                    json.leavearray();
                }
                else
                {
                    LOG_err << "Failed to parse Awards of MEGA achievements";
                    json.storeobject();
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return false;
                }
                break;

            case 'r':
                if (json.enterobject())
                {
                    for (;;)
                    {
                        nameid id = json.getnameid();
                        if (id == EOO)
                        {
                            break;
                        }

                        Reward reward;
                        reward.award_id = int(id - '0');   // convert to number

                        json.enterarray();

                        reward.storage = json.getint();
                        reward.transfer = json.getint();
                        const char *exp_ts = json.getvalue();
                        char *pEnd = NULL;
                        reward.expire = int(strtol(exp_ts, &pEnd, 10));
                        if (*pEnd == 'm')
                        {
                            reward.expire *= 30;
                        }
                        else if (*pEnd == 'y')
                        {
                            reward.expire *= 365;
                        }

                        while(json.storeobject());
                        json.leavearray();

                        details->rewards.push_back(reward);
                    }

                    json.leaveobject();
                }
                else
                {
                    LOG_err << "Failed to parse Rewards of MEGA achievements";
                    json.storeobject();
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return false;
                }
                break;

            case EOO:
                client->app->getmegaachievements_result(details, API_OK);
                return true;

            default:
                if (!json.storeobject())
                {
                    LOG_err << "Failed to parse MEGA achievements";
                    client->app->getmegaachievements_result(details, API_EINTERNAL);
                    return false;
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

bool CommandGetWelcomePDF::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        LOG_err << "Unexpected response of 'wpdf' command: missing 'ph' and 'k'";
        return true;
    }

    handle ph = UNDEF;
    byte keybuf[FILENODEKEYLENGTH];
    int len_key = 0;
    string key;

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID2('p', 'h'):
                ph = json.gethandle(MegaClient::NODEHANDLE);
                break;

            case 'k':
                len_key = json.storebinary(keybuf, sizeof keybuf);
                break;

            case EOO:
                if (ISUNDEF(ph) || len_key != FILENODEKEYLENGTH)
                {
                    LOG_err << "Failed to import welcome PDF: invalid response";
                    return false;
                }
                key.assign((const char *) keybuf, len_key);
                client->reqs.add(new CommandGetPH(client, ph, (const byte*) key.data(), 2));
                if (client->wasWelcomePdfImportDelayed())
                {
                    client->setWelcomePdfNeedsDelayedImport(false);
                }
                return true;

            default:
                if (!json.storeobject())
                {
                    LOG_err << "Failed to parse welcome PDF response";
                    return false;
                }
                break;
        }
    }
}


CommandMediaCodecs::CommandMediaCodecs(MegaClient* c, Callback cb)
{
    cmd("mc");

    client = c;
    callback = cb;
}

bool CommandMediaCodecs::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        LOG_err << "mc result: " << error(r.errorOrOK());
        return true;
    }

    if (!json.isnumeric())
    {
        // It's wrongly formatted, consume this one so the next command can be processed.
        LOG_err << "mc response badly formatted";
        return false;
    }

    int version = static_cast<int>(json.getint());
    callback(client, json, version);
    return true;
}

CommandContactLinkCreate::CommandContactLinkCreate(MegaClient *client, bool renew)
{
    mSeqtagArray = true;

    if (renew)
    {
        cmd("clr");
    }
    else
    {
        cmd("clc");
    }

    tag = client->reqtag;
}

bool CommandContactLinkCreate::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        handle h = json.gethandle(MegaClient::CONTACTLINKHANDLE);
        client->app->contactlinkcreate_result(API_OK, h);
        return true;
    }
    else if (r.wasErrorOrOK())
    {
        client->app->contactlinkcreate_result(r.errorOrOK(), UNDEF);
        return true;
    }

    client->app->contactlinkcreate_result(API_EINTERNAL, UNDEF);
    return false;
}

CommandContactLinkQuery::CommandContactLinkQuery(MegaClient *client, handle h)
{
    cmd("clg");
    arg("cl", (byte*)&h, MegaClient::CONTACTLINKHANDLE);

    arg("b", 1);    // return firstname/lastname in B64

    tag = client->reqtag;
}

bool CommandContactLinkQuery::procresult(Result r, JSON& json)
{
    handle h = UNDEF;
    string email;
    string firstname;
    string lastname;
    string avatar;

    if (r.wasErrorOrOK())
    {
        client->app->contactlinkquery_result(r.errorOrOK(), h, &email, &firstname, &lastname, &avatar);
        return true;
    }

    for (;;)
    {
        switch (json.getnameid())
        {
            case 'h':
                h = json.gethandle(MegaClient::USERHANDLE);
                break;
            case 'e':
                json.storeobject(&email);
                break;
            case MAKENAMEID2('f', 'n'):
                json.storeobject(&firstname);
                break;
            case MAKENAMEID2('l', 'n'):
                json.storeobject(&lastname);
                break;
            case MAKENAMEID2('+', 'a'):
                json.storeobject(&avatar);
                break;
            case EOO:
                client->app->contactlinkquery_result(API_OK, h, &email, &firstname, &lastname, &avatar);
                return true;
            default:
                if (!json.storeobject())
                {
                    LOG_err << "Failed to parse query contact link response";
                    client->app->contactlinkquery_result(API_EINTERNAL, h, &email, &firstname, &lastname, &avatar);
                    return false;
                }
                break;
        }
    }
}

CommandContactLinkDelete::CommandContactLinkDelete(MegaClient *client, handle h)
{
    cmd("cld");
    if (!ISUNDEF(h))
    {
        arg("cl", (byte*)&h, MegaClient::CONTACTLINKHANDLE);
    }
    tag = client->reqtag;
}

bool CommandContactLinkDelete::procresult(Result r, JSON&)
{
    client->app->contactlinkdelete_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandKeepMeAlive::CommandKeepMeAlive(MegaClient *client, int type, bool enable)
{
    if (enable)
    {
        cmd("kma");
    }
    else
    {
        cmd("kmac");
    }
    arg("t", type);

    tag = client->reqtag;
}

bool CommandKeepMeAlive::procresult(Result r, JSON&)
{
    client->app->keepmealive_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandMultiFactorAuthSetup::CommandMultiFactorAuthSetup(MegaClient *client, const char *pin)
{
    mSeqtagArray = true;

    cmd("mfas");
    if (pin)
    {
        arg("mfa", pin);
    }
    tag = client->reqtag;
}

bool CommandMultiFactorAuthSetup::procresult(Result r, JSON& json)
{
    // don't call storeobject unless we are sure we should, as it could consume a top level `,`
    if (r.hasJsonItem())
    {
        // code string is returned when mfa is not supplied in the request
        string code;
        if (json.storeobject(&code))
        {
            client->app->multifactorauthsetup_result(&code, API_OK);
            return true;
        }
    }
    else if (r.wasErrorOrOK())  //[0] is valid response (returned when mfa is supplied in the request)
    {
        client->app->multifactorauthsetup_result(NULL, r.errorOrOK());
        return true;
    }

    // if anything went wrong
    client->app->multifactorauthsetup_result(NULL, API_EINTERNAL);
    return false;  // caller will reevaluate json to get to the next command
}

CommandMultiFactorAuthCheck::CommandMultiFactorAuthCheck(MegaClient *client, const char *email)
{
    cmd("mfag");
    arg("e", email);

    tag = client->reqtag;
}

bool CommandMultiFactorAuthCheck::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->multifactorauthcheck_result(r.errorOrOK());
        return true;
    }

    if (json.isnumeric())
    {
        client->app->multifactorauthcheck_result(static_cast<int>(json.getint()));
        return true;
    }
    else
    {
        client->app->multifactorauthcheck_result(API_EINTERNAL);
        return false;
    }
}

CommandMultiFactorAuthDisable::CommandMultiFactorAuthDisable(MegaClient *client, const char *pin)
{
    cmd("mfad");
    arg("mfa", pin);

    tag = client->reqtag;
}

bool CommandMultiFactorAuthDisable::procresult(Result r, JSON&)
{
    client->app->multifactorauthdisable_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandGetPSA::CommandGetPSA(bool urlSupport, MegaClient *client)
{
    cmd("gpsa");

    if (urlSupport)
    {
        arg("w", 1);
    }

    tag = client->reqtag;
}

bool CommandGetPSA::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->getpsa_result(r.errorOrOK(), 0, NULL, NULL, NULL, NULL, NULL, NULL);
        return true;
    }

    int id = 0;
    string temp;
    string title, text, imagename, imagepath;
    string buttonlink, buttontext, url;

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID2('i', 'd'):
                id = int(json.getint());
                break;
            case 't':
                json.storeobject(&temp);
                Base64::atob(temp, title);
                break;
            case 'd':
                json.storeobject(&temp);
                Base64::atob(temp, text);
                break;
            case MAKENAMEID3('i', 'm', 'g'):
                json.storeobject(&imagename);
                break;
            case 'l':
                json.storeobject(&buttonlink);
                break;
            case MAKENAMEID3('u', 'r', 'l'):
                json.storeobject(&url);
                break;
            case 'b':
                json.storeobject(&temp);
                Base64::atob(temp, buttontext);
                break;
            case MAKENAMEID3('d', 's', 'p'):
                json.storeobject(&imagepath);
                break;
            case EOO:
                imagepath.append(imagename);
                imagepath.append(".png");
                client->app->getpsa_result(API_OK, id, &title, &text, &imagepath, &buttontext, &buttonlink, &url);
                return true;
            default:
                if (!json.storeobject())
                {
                    LOG_err << "Failed to parse get PSA response";
                    client->app->getpsa_result(API_EINTERNAL, 0, NULL, NULL, NULL, NULL, NULL, NULL);
                    return false;
                }
                break;
        }
    }
}

CommandFetchTimeZone::CommandFetchTimeZone(MegaClient *client, const char *timezone, const char* timeoffset)
{
    cmd("ftz");
    arg("utz", timezone);
    arg("uo", timeoffset);

    tag = client->reqtag;
}

bool CommandFetchTimeZone::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->fetchtimezone_result(r.errorOrOK(), NULL, NULL, -1);
        return true;
    }

    string currenttz;
    int currentto;
    vector<string> timezones;
    vector<int> timeoffsets;
    string defaulttz;
    int defaulttzindex = -1;

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID7('c', 'h', 'o', 'i', 'c', 'e', 's'):
                if (json.enterobject())
                {
                    while (json.storeobject(&currenttz))
                    {
                        currentto = int(json.getint());
                        timezones.push_back(currenttz);
                        timeoffsets.push_back(currentto);
                    }
                    json.leaveobject();
                }
                else if (!json.storeobject())
                {
                    LOG_err << "Failed to parse fetch time zone response";
                    client->app->fetchtimezone_result(API_EINTERNAL, NULL, NULL, -1);
                    return false;
                }
                break;

            case MAKENAMEID7('d', 'e', 'f', 'a', 'u', 'l', 't'):
                if (json.isnumeric())
                {
                    json.getint();
                }
                else
                {
                    json.storeobject(&defaulttz);
                }
                break;

            case EOO:
                if (!defaulttz.empty())    // default received as string
                {
                    for (int i = 0; i < (int)timezones.size(); i++)
                    {
                        if (timezones[i] == defaulttz)
                        {
                            defaulttzindex = i;
                            break;
                        }
                    }
                }
                client->app->fetchtimezone_result(API_OK, &timezones, &timeoffsets, defaulttzindex);
                return true;

            default:
                if (!json.storeobject())
                {
                    LOG_err << "Failed to parse fetch time zone response";
                    client->app->fetchtimezone_result(API_EINTERNAL, NULL, NULL, -1);
                    return false;
                }
                break;
        }
    }
}

CommandSetLastAcknowledged::CommandSetLastAcknowledged(MegaClient* client)
{
    cmd("sla");
    tag = client->reqtag;
}

bool CommandSetLastAcknowledged::procresult(Result r, JSON&)
{
    if (r.succeeded())
    {
        client->useralerts.acknowledgeAllSucceeded();
    }

    client->app->acknowledgeuseralerts_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandSMSVerificationSend::CommandSMSVerificationSend(MegaClient* client, const string& phoneNumber, bool reVerifyingWhitelisted)
{
    cmd("smss");
    batchSeparately = true;  // don't let any other commands that might get batched with it cause the whole batch to fail

    assert(isPhoneNumber(phoneNumber));
    arg("n", phoneNumber.c_str());

    if (reVerifyingWhitelisted)
    {
        arg("to", 1);   // test override
    }

    tag = client->reqtag;
}

bool CommandSMSVerificationSend::isPhoneNumber(const string& s)
{
    for (auto i = s.size(); i--; )
    {
        if (!(is_digit(s[i]) || (i == 0 && s[i] == '+')))
        {
            return false;
        }
    }
    return s.size() > 6;
}

bool CommandSMSVerificationSend::procresult(Result r, JSON&)
{
    client->app->smsverificationsend_result(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandSMSVerificationCheck::CommandSMSVerificationCheck(MegaClient* client, const string& verificationcode)
{
    mSeqtagArray = true;

    cmd("smsv");
    batchSeparately = true;  // don't let any other commands that might get batched with it cause the whole batch to fail

    if (isVerificationCode(verificationcode))
    {
        arg("c", verificationcode.c_str());
    }

    tag = client->reqtag;
}

bool CommandSMSVerificationCheck::isVerificationCode(const string& s)
{
    for (const char c : s)
    {
        if (!is_digit(c))
        {
            return false;
        }
    }
    return s.size() == 6;
}

bool CommandSMSVerificationCheck::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        string phoneNumber;
        if (json.storeobject(&phoneNumber))
        {
            assert(CommandSMSVerificationSend::isPhoneNumber(phoneNumber));
            client->mSmsVerifiedPhone = phoneNumber;
            client->app->smsverificationcheck_result(API_OK, &phoneNumber);
            return true;
        }
    }
    else if (r.wasErrorOrOK())
    {
        client->app->smsverificationcheck_result(r.errorOrOK(), nullptr);
        return true;
    }

    client->app->smsverificationcheck_result(API_EINTERNAL, nullptr);
    return false;
}

CommandGetCountryCallingCodes::CommandGetCountryCallingCodes(MegaClient* client)
{
    cmd("smslc");

    batchSeparately = true;
    tag = client->reqtag;
}

bool CommandGetCountryCallingCodes::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->getcountrycallingcodes_result(r.errorOrOK(), nullptr);
        return true;
    }

    map<string, vector<string>> countryCallingCodes;

    bool success = true;
    while (json.enterobject())
    {
        bool exit = false;
        string countryCode;
        vector<string> callingCodes;
        while (!exit)
        {
            switch (json.getnameid())
            {
                case MAKENAMEID2('c', 'c'):
                {
                    json.storeobject(&countryCode);
                    break;
                }
                case MAKENAMEID1('l'):
                {
                    if (json.enterarray())
                    {
                        std::string code;
                        while (json.storeobject(&code))
                        {
                            callingCodes.emplace_back(std::move(code));
                        }
                        json.leavearray();
                    }
                    break;
                }
                case EOO:
                {
                    if (countryCode.empty() || callingCodes.empty())
                    {
                        LOG_err << "Missing or empty fields when parsing 'get country calling codes' response";
                        success = false;
                    }
                    else
                    {
                        countryCallingCodes.emplace(make_pair(std::move(countryCode), std::move(callingCodes)));
                    }
                    exit = true;
                    break;
                }
                default:
                {
                    if (!json.storeobject())
                    {
                        LOG_err << "Failed to parse 'get country calling codes' response";
                        client->app->getcountrycallingcodes_result(API_EINTERNAL, nullptr);
                        return false;
                    }
                }
            }
        }
        json.leaveobject();
    }
    if (success)
    {
        client->app->getcountrycallingcodes_result(API_OK, &countryCallingCodes);
        return true;
    }
    else
    {
        client->app->getcountrycallingcodes_result(API_EINTERNAL, nullptr);
        return false;
    }
}

CommandFolderLinkInfo::CommandFolderLinkInfo(MegaClient* client, handle publichandle)
{
    ph = publichandle;

    cmd("pli");
    arg("ph", (byte*)&publichandle, MegaClient::NODEHANDLE);

    tag = client->reqtag;
}

bool CommandFolderLinkInfo::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->folderlinkinfo_result(r.errorOrOK(), UNDEF, UNDEF, NULL, NULL, 0, 0, 0, 0, 0);
        return true;
    }
    string attr;
    string key;
    handle owner = UNDEF;
    handle ph = 0;
    m_off_t currentSize = 0;
    m_off_t versionsSize  = 0;
    int numFolders = 0;
    int numFiles = 0;
    int numVersions = 0;

    for (;;)
    {
        switch (json.getnameid())
        {
        case MAKENAMEID5('a','t','t','r','s'):
            json.storeobject(&attr);
            break;

        case MAKENAMEID2('p','h'):
            ph = json.gethandle(MegaClient::NODEHANDLE);
            break;

        case 'u':
            owner = json.gethandle(MegaClient::USERHANDLE);
            break;

        case 's':
            if (json.enterarray())
            {
                currentSize = json.getint();
                numFiles = int(json.getint());
                numFolders = int(json.getint());
                versionsSize  = json.getint();
                numVersions = int(json.getint());
                json.leavearray();
            }
            break;

        case 'k':
            json.storeobject(&key);
            break;

        case EOO:
            if (attr.empty())
            {
                LOG_err << "The folder link information doesn't contain the attr string";
                client->app->folderlinkinfo_result(API_EINCOMPLETE, UNDEF, UNDEF, NULL, NULL, 0, 0, 0, 0, 0);
                return false;
            }
            if (key.size() <= 9 || key.find(":") == string::npos)
            {
                LOG_err << "The folder link information doesn't contain a valid decryption key";
                client->app->folderlinkinfo_result(API_EKEY, UNDEF, UNDEF, NULL, NULL, 0, 0, 0, 0, 0);
                return false;
            }
            if (ph != this->ph)
            {
                LOG_err << "Folder link information: public handle doesn't match";
                client->app->folderlinkinfo_result(API_EINTERNAL, UNDEF, UNDEF, NULL, NULL, 0, 0, 0, 0, 0);
                return false;
            }

            client->app->folderlinkinfo_result(API_OK, owner, ph, &attr, &key, currentSize, numFiles, numFolders, versionsSize, numVersions);
            return true;

        default:
            if (!json.storeobject())
            {
                LOG_err << "Failed to parse folder link information response";
                client->app->folderlinkinfo_result(API_EINTERNAL, UNDEF, UNDEF, NULL, NULL, 0, 0, 0, 0, 0);
                return false;
            }
            break;
        }
    }
}

CommandBackupPut::CommandBackupPut(MegaClient* client, const BackupInfo& fields, std::function<void(Error, handle /*backup id*/)> completion)
    : mCompletion(completion)
{
    mSeqtagArray = true;

    cmd("sp");

    if (!ISUNDEF(fields.backupId))
    {
        arg("id", (byte*)&fields.backupId, MegaClient::BACKUPHANDLE);
    }

    if (fields.type != BackupType::INVALID)
    {
        arg("t", fields.type);
    }

    if (!fields.nodeHandle.isUndef())
    {
        arg("h", fields.nodeHandle);
    }

    if (!fields.localFolder.empty())
    {
        string localFolderEncrypted(client->cypherTLVTextWithMasterKey("lf", fields.localFolder.toPath(false)));
        arg("l", localFolderEncrypted.c_str());
    }

    if (!fields.deviceId.empty())
    {
        arg("d", fields.deviceId.c_str());
    }

    if (!ISUNDEF(fields.driveId))
    {
        arg("dr",  (byte*)&fields.driveId, MegaClient::DRIVEHANDLE);
    }

    if (fields.state >= 0)
    {
        arg("s", fields.state);
    }

    if (fields.subState >= 0)
    {
        arg("ss", fields.subState);
    }

    if (!fields.backupName.empty())
    {
        string edEncrypted(client->cypherTLVTextWithMasterKey("bn", fields.backupName));
        arg("e", edEncrypted.c_str());
    }

    tag = client->reqtag;
}

bool CommandBackupPut::procresult(Result r, JSON& json)
{
    if (r.hasJsonItem())
    {
        handle backupId = json.gethandle(MegaClient::BACKUPHANDLE);

        if (mCompletion) mCompletion(API_OK, backupId);
        client->app->backupput_result(API_OK, backupId);
        return true;
    }
    else if (r.wasErrorOrOK())
    {
        assert(r.errorOrOK() != API_EARGS);  // if this happens, the API rejected the request because it wants more fields supplied
        if (mCompletion) mCompletion(r.errorOrOK(), UNDEF);
        client->app->backupput_result(r.errorOrOK(), UNDEF);
        return true;
    }

    if (mCompletion) mCompletion(API_EINTERNAL, UNDEF);
    client->app->backupput_result(API_EINTERNAL, UNDEF);
    return false;
}

CommandBackupPutHeartBeat::CommandBackupPutHeartBeat(MegaClient* client, handle backupId, SPHBStatus status, int8_t progress, uint32_t uploads, uint32_t downloads, m_time_t ts, handle lastNode, std::function<void(Error)> f)
    : mCompletion(f)
{
    cmd("sphb");

    arg("id", (byte*)&backupId, MegaClient::BACKUPHANDLE);
    arg("s", uint8_t(status));
    if (status == SPHBStatus::SYNCING || status == SPHBStatus::UPTODATE)
    {
        // so don't send 0 out of 0 0% initially
        assert(progress >= 0);
        assert(progress <= 100);
        arg("p", progress);
    }
    arg("qu", uploads);
    arg("qd", downloads);
    if (ts != -1)
    {
        arg("lts", ts);
    }
    if (!ISUNDEF(lastNode))
    {
        arg("lh", (byte*)&lastNode, MegaClient::NODEHANDLE);
    }

    tag = client->reqtag;
}

bool CommandBackupPutHeartBeat::procresult(Result r, JSON&)
{
    if (mCompletion) mCompletion(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandBackupRemove::CommandBackupRemove(MegaClient* client,
                                         handle backupId,
                                         std::function<void(Error)> completion)
{
    cmd("sr");
    arg("id", (byte*)&backupId, MegaClient::BACKUPHANDLE);

    tag = client->reqtag;
    mCompletion = completion;
}

bool CommandBackupRemove::procresult(Result r, JSON&)
{
    if (mCompletion)
    {
        mCompletion(r.errorOrOK());
    }
    return r.wasErrorOrOK();
}

CommandBackupSyncFetch::CommandBackupSyncFetch(std::function<void(const Error&, const vector<Data>&)> f)
    : completion(std::move(f))
{
    cmd("sf");
}

bool CommandBackupSyncFetch::procresult(Result r, JSON& json)
{
    vector<Data> data;
    if (!r.hasJsonArray())
    {
        completion(r.errorOrOK(), data);
    }
    else
    {
        auto skipUnknownField = [&]() -> bool {
            if (!json.storeobject())
            {
                completion(API_EINTERNAL, data);
                return false;
            }
            return true;
        };

        auto cantLeaveObject = [&]() -> bool {
            if (!json.leaveobject())
            {
                completion(API_EINTERNAL, data);
                return true;
            }
            return false;
        };

        while (json.enterobject())
        {
            data.push_back(Data());
            for (;;)
            {
                auto& d = data.back();
                auto nid = json.getnameid();
                if (nid == EOO) break;
                switch (nid)
                {
                case MAKENAMEID2('i', 'd'):     d.backupId = json.gethandle(sizeof(handle)); break;
                case MAKENAMEID1('t'):          d.backupType = static_cast<BackupType>(json.getint32()); break;
                case MAKENAMEID1('h'):          d.rootNode = json.gethandle(MegaClient::NODEHANDLE); break;
                case MAKENAMEID1('l'):          json.storeobject(&d.localFolder);
                                                d.localFolder = client->decypherTLVTextWithMasterKey("lf", d.localFolder);
                                                break;
                case MAKENAMEID1('d'):          json.storeobject(&d.deviceId); break;
                case MAKENAMEID3('d', 'u', 'a'):json.storeobject(&d.deviceUserAgent); break;
                case MAKENAMEID1('s'):          d.syncState = json.getint32(); break;
                case MAKENAMEID2('s', 's'):     d.syncSubstate = json.getint32(); break;
                case MAKENAMEID1('e'):          json.storeobject(&d.extra);
                                                d.backupName = client->decypherTLVTextWithMasterKey("bn", d.extra);
                                                break;
                case MAKENAMEID2('h', 'b'):
                {

                    if (json.enterobject())
                    {
                        for (;;)
                        {
                            nid = json.getnameid();
                            if (nid == EOO) break;
                            switch (nid)
                            {
                            case MAKENAMEID2('t', 's'):     d.hbTimestamp = json.getint(); break;
                            case MAKENAMEID1('s'):          d.hbStatus = json.getint32(); break;
                            case MAKENAMEID1('p'):          d.hbProgress = json.getint32(); break;
                            case MAKENAMEID2('q', 'u'):     d.uploads = json.getint32(); break;
                            case MAKENAMEID2('q', 'd'):     d.downloads = json.getint32(); break;
                            case MAKENAMEID3('l', 't', 's'):d.lastActivityTs = json.getint32(); break;
                            case MAKENAMEID2('l', 'h'):     d.lastSyncedNodeHandle = json.gethandle(MegaClient::NODEHANDLE); break;
                            default: if (!skipUnknownField()) return false;
                            }
                        }
                        if (cantLeaveObject()) return false;
                    }
                }
                break;

                default: if (!skipUnknownField()) return false;
                }
            }
            if (cantLeaveObject()) return false;
        }

        completion(API_OK, data);
    }
    return true;
}


CommandGetBanners::CommandGetBanners(MegaClient* client)
{
    cmd("gban");

    tag = client->reqtag;
}

bool CommandGetBanners::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        client->app->getbanners_result(r.errorOrOK());
        return true; // because parsing didn't fail
    }

    /*
        {
            "id": 2, ///The banner id
            "t": "R2V0IFZlcmlmaWVk", ///Banner title
            "d": "TWFrZSBpdCBlYXNpZXIgZm9yIHlvdXIgY29udGFjdHMgdG8gZmluZCB5b3Ugb24gTUVHQS4", ///Banner description.
            "img": "Verified_image.png", ///Image name.
            "l": "", ///URL
            "bimg": "Verified_BG.png", ///background image name.
            "dsp": "https://domain/path" ///Where to get the image.
        }, {"id":3, ...}, ... ]
    */

    vector< tuple<int, string, string, string, string, string, string> > banners;

    // loop array elements
    while (json.enterobject())
    {
        int id = 0;
        string title, description, img, url, bimg, dsp;
        bool exit = false;

        // loop and read object members
        while (!exit)
        {
            switch (json.getnameid())
            {
            case MAKENAMEID2('i', 'd'):
                id = json.getint32();
                break;

            case MAKENAMEID1('t'):
                json.storeobject(&title);
                title = Base64::atob(title);
                break;

            case MAKENAMEID1('d'):
                json.storeobject(&description);
                description = Base64::atob(description);
                break;

            case MAKENAMEID3('i', 'm', 'g'):
                json.storeobject(&img);
                break;

            case MAKENAMEID1('l'):
                json.storeobject(&url);
                break;

            case MAKENAMEID4('b', 'i', 'm', 'g'):
                json.storeobject(&bimg);
                break;

            case MAKENAMEID3('d', 's', 'p'):
                json.storeobject(&dsp);
                break;

            case EOO:
                if (!id || title.empty() || description.empty())
                {
                    LOG_err << "Missing id, title or description in response to gban";
                    client->app->getbanners_result(API_EINTERNAL);
                    return false;
                }
                exit = true;
                break;

            default:
                if (!json.storeobject()) // skip unknown member
                {
                    LOG_err << "Failed to parse banners response";
                    client->app->getbanners_result(API_EINTERNAL);
                    return false;
                }
                break;
            }
        }

        banners.emplace_back(make_tuple(id, std::move(title), std::move(description), std::move(img), std::move(url), std::move(bimg), std::move(dsp)));

        json.leaveobject();
    }

    client->app->getbanners_result(std::move(banners));

    return true;
}

CommandDismissBanner::CommandDismissBanner(MegaClient* client, int id, m_time_t timestamp)
{
    cmd("dban");
    arg("id", id); // id of the Smart Banner
    arg("ts", timestamp);

    tag = client->reqtag;
}

bool CommandDismissBanner::procresult(Result r, JSON&)
{
    client->app->dismissbanner_result(r.errorOrOK());
    return r.wasErrorOrOK();
}


//
// Sets and Elements
//

bool CommandSE::procjsonobject(JSON& json, handle& id, m_time_t& ts, handle* u, m_time_t* cts,
                               handle* s, int64_t* o, handle* ph, uint8_t* t) const
{
    for (;;)
    {
        switch (json.getnameid())
        {
        case MAKENAMEID2('i', 'd'):
            id = json.gethandle(MegaClient::SETHANDLE);
            break;

        case MAKENAMEID1('u'):
            {
                const auto buf = json.gethandle(MegaClient::USERHANDLE);
                if (u) *u = buf;
            }
            break;

        case MAKENAMEID1('s'):
            {
                const auto buf = json.gethandle(MegaClient::SETHANDLE);
                if (s) *s = buf;
            }
            break;

        case MAKENAMEID2('t', 's'):
            ts = json.getint();
            break;

        case MAKENAMEID3('c', 't', 's'):
            {
                const auto buf = json.getint();
                if (cts) *cts = buf;
            }
            break;

        case MAKENAMEID1('o'):
            {
                const auto buf = json.getint();
                if (o) *o = buf;
            }
            break;

        case MAKENAMEID2('p', 'h'):
            {
                const auto buf = json.gethandle(MegaClient::PUBLICSETHANDLE);
                if (ph) *ph = buf;
            }
            break;

        case MAKENAMEID1('t'):
            {
                const auto setType = static_cast<uint8_t>(json.getint());
                if (t) *t = setType;
            }
            break;

        default:
            if (!json.storeobject())
            {
                return false;
            }
            break;

        case EOO:
            return true;
        }
    }
}

bool CommandSE::procresultid(JSON& json, const Result& r, handle& id, m_time_t& ts, handle* u,
                             m_time_t* cts, handle* s, int64_t* o, handle* ph, uint8_t* t) const
{
    return r.hasJsonObject() && procjsonobject(json, id, ts, u, cts, s, o, ph, t);
}

bool CommandSE::procerrorcode(const Result& r, Error& e) const
{
    if (r.wasErrorOrOK())
    {
        e = r.errorOrOK();
        return true;
    }

    return false;
}

bool CommandSE::procExtendedError(JSON& json, int64_t& errCode, handle& eid) const
{
    int maxJsonAttrToCheck = 2; // shortcut to avoid processing the whole json object
    bool isErr = false;
    while (maxJsonAttrToCheck--)
    {
        switch (json.getnameid())
        {
        case MAKENAMEID3('e', 'r', 'r'):
        {
            isErr = true;
            errCode = json.getint();
            break;
        }

        case MAKENAMEID3('e', 'i', 'd'):
        {
            eid = json.gethandle(MegaClient::SETELEMENTHANDLE);
            break;
        }

        default:
            return false;
        }
    }
    return isErr;
}

CommandPutSet::CommandPutSet(MegaClient* cl, Set&& s, unique_ptr<string> encrAttrs, string&& encrKey,
                             std::function<void(Error, const Set*)> completion)
    : mSet(new Set(std::move(s))), mCompletion(completion)
{
    mSeqtagArray = true;
    cmd("asp");

    if (mSet->id() == UNDEF) // create new
    {
        arg("k", (byte*)encrKey.c_str(), (int)encrKey.size());
        arg("t", static_cast<m_off_t>(mSet->type()));
    }
    else // update
    {
        arg("id", (byte*)&mSet->id(), MegaClient::SETHANDLE);
    }

    if (encrAttrs)
    {
        arg("at", (byte*)encrAttrs->c_str(), (int)encrAttrs->size());
    }

    notself(cl); // don't process its Action Packet after sending this
}

bool CommandPutSet::procresult(Result r, JSON& json)
{
    handle sId = 0;
    handle user = 0;
    m_time_t ts = 0;
    m_time_t cts = 0;
    const Set* s = nullptr;
    Error e = API_OK;
    bool parsedOk = procerrorcode(r, e) || procresultid(json, r, sId, ts, &user, &cts);

    if (!parsedOk || (mSet->id() == UNDEF && !user))
    {
        e = API_EINTERNAL;
    }
    else if (e == API_OK)
    {
        mSet->setTs(ts);
        if (mSet->id() == UNDEF) // add new
        {
            mSet->setId(sId);
            mSet->setUser(user);
            mSet->setCTs(cts);
            mSet->setChanged(Set::CH_NEW);
            s = client->addSet(std::move(*mSet));
        }
        else // update existing
        {
            assert(mSet->id() == sId);

            if (!client->updateSet(std::move(*mSet)))
            {
                LOG_warn << "Sets: command 'asp' succeed, but Set was not found";
                e = API_ENOENT;
            }
        }
    }

    if (mCompletion)
    {
        mCompletion(e, s);
    }

    return parsedOk;
}

CommandRemoveSet::CommandRemoveSet(MegaClient* cl, handle id, std::function<void(Error)> completion)
    : mSetId(id), mCompletion(completion)
{
    cmd("asr");
    arg("id", (byte*)&id, MegaClient::SETHANDLE);

    notself(cl); // don't process its Action Packet after sending this
}

bool CommandRemoveSet::procresult(Result r, JSON&)
{
    Error e = API_OK;
    bool parsedOk = procerrorcode(r, e);

    if (parsedOk && e == API_OK)
    {
        if (!client->deleteSet(mSetId))
        {
            LOG_err << "Sets: Failed to remove Set in `asr` command response";
            e = API_ENOENT;
        }
    }

    if (mCompletion)
    {
        mCompletion(e);
    }

    return parsedOk;
}

CommandFetchSet::CommandFetchSet(MegaClient* cl,
    std::function<void(Error, Set*, elementsmap_t*)> completion)
    : mCompletion(completion)
{
    cmd("aft");
    arg("v", 2);  // version 2: server can supply node metadata
    if(!cl->inPublicSetPreview())
    {
        LOG_err << "Sets: CommandFetchSet only available for Public Set in Preview Mode";
        assert(cl->inPublicSetPreview());
    }
}

bool CommandFetchSet::procresult(Result r, JSON& json)
{
    Error e = API_OK;
    if (procerrorcode(r, e))
    {
        if (mCompletion)
        {
            mCompletion(e, nullptr, nullptr);
        }
        return true;
    }

    map<handle, Set> sets;
    map<handle, elementsmap_t> elements;
    e = client->readSetsAndElements(json, sets, elements);
    if (e != API_OK)
    {
        LOG_err << "Sets: Failed to parse \"aft\" response";
        if (mCompletion)
        {
            mCompletion(e, nullptr, nullptr);
        }
        return false;
    }

    assert(sets.size() <= 1);

    if (mCompletion)
    {
        if (sets.empty())
        {
            LOG_err << "Sets: Failed to decrypt data from \"aft\" response";
            mCompletion(API_EKEY, nullptr, nullptr);
        }

        else
        {
            Set* s = new Set(std::move(sets.begin()->second));
            elementsmap_t* els = elements.empty()
                                 ? new elementsmap_t()
                                 : new elementsmap_t(std::move(elements.begin()->second));
            mCompletion(API_OK, s, els);
        }
    }

    return true;
}

CommandPutSetElements::CommandPutSetElements(MegaClient* cl, vector<SetElement>&& els, vector<StringPair>&& encrDetails,
                                               std::function<void(Error, const vector<const SetElement*>*, const vector<int64_t>*)> completion)
    : mElements(new vector<SetElement>(std::move(els))), mCompletion(completion)
{
    mSeqtagArray = true;
    cmd("aepb");

    const byte* setHandleBytes = reinterpret_cast<const byte*>(&mElements->front().set());
    arg("s", setHandleBytes, MegaClient::SETHANDLE);

    beginarray("e");

    for (size_t i = 0; i < mElements->size(); ++i)
    {
        beginobject();

        const byte* nodeHandleBytes = reinterpret_cast<const byte*>(&mElements->at(i).node());
        arg("h", nodeHandleBytes, MegaClient::NODEHANDLE);

        auto& ed = encrDetails[i];
        const byte* keyBytes = reinterpret_cast<const byte*>(ed.second.c_str());
        arg("k", keyBytes, static_cast<int>(ed.second.size()));

        if (!ed.first.empty())
        {
            const byte* attrBytes = reinterpret_cast<const byte*>(ed.first.c_str());
            arg("at", attrBytes, static_cast<int>(ed.first.size()));
        }
        endobject();
    }

    endarray();

    notself(cl); // don't process its Action Packets after sending this
}

bool CommandPutSetElements::procresult(Result r, JSON& json)
{
    Error e = API_OK;
    if (procerrorcode(r, e))
    {
        if (mCompletion)
        {
            mCompletion(e, nullptr, nullptr);
        }
        return true;
    }
    else if (!r.hasJsonArray())
    {
        LOG_err << "Sets: failed to parse `aepb` response";
        if (mCompletion)
        {
            mCompletion(API_EINTERNAL, nullptr, nullptr);
        }
        return false;
    }

    bool allOk = true;
    vector<const SetElement*> addedEls;
    vector<int64_t> errs(mElements->size(), API_OK);
    for (size_t elCount = 0u; elCount < mElements->size(); ++elCount)
    {
        if (json.isnumeric())
        {
            // there was an error while adding this element
            errs[elCount] = json.getint();
        }
        else if (json.enterobject())
        {
            const auto posAux = json.pos;
            handle errEid = UNDEF;
            if (procExtendedError(json, errs[elCount], errEid))
            {
                if (errEid == UNDEF) LOG_warn << "Sets: Extended error missing Element id";
            }
            else
            {
                json.pos = posAux;
                handle elementId = 0;
                m_time_t ts = 0;
                int64_t order = 0;
                if (!procjsonobject(json, elementId, ts, nullptr, nullptr, nullptr, &order))
                {
                    LOG_err << "Sets: failed to parse Element object in `aepb` response";
                    allOk = false;
                    break;
                }

                SetElement& el = mElements->at(elCount);
                el.setId(elementId);
                el.setTs(ts);
                el.setOrder(order);
                addedEls.push_back(client->addOrUpdateSetElement(std::move(el)));
            }

            if (!json.leaveobject())
            {
                LOG_err << "Sets: failed to leave Element object in `aepb` response";
                allOk = false;
                break;
            }
        }
        else
        {
            LOG_err << "Sets: failed to parse Element array in `aepb` response";
            allOk = false;
            break;
        }
    }

    if (mCompletion)
    {
        mCompletion(e, &addedEls, &errs);
    }

    return allOk;
}

CommandPutSetElement::CommandPutSetElement(MegaClient* cl, SetElement&& el, unique_ptr<string> encrAttrs, string&& encrKey,
                                               std::function<void(Error, const SetElement*)> completion)
    : mElement(new SetElement(std::move(el))), mCompletion(completion)
{
    mSeqtagArray = true;
    cmd("aep");

    bool createNew = mElement->id() == UNDEF;

    if (createNew)
    {
        arg("s", (byte*)&mElement->set(), MegaClient::SETHANDLE);
        arg("h", (byte*)&mElement->node(), MegaClient::NODEHANDLE);
        arg("k", (byte*)encrKey.c_str(), (int)encrKey.size());
    }

    else // update
    {
        arg("id", (byte*)&mElement->id(), MegaClient::SETELEMENTHANDLE);
    }

    // optionals
    if (mElement->hasOrder())
    {
        arg("o", mElement->order());
    }

    if (encrAttrs)
    {
        arg("at", (byte*)encrAttrs->c_str(), (int)encrAttrs->size());
    }

    notself(cl); // don't process its Action Packet after sending this
}

bool CommandPutSetElement::procresult(Result r, JSON& json)
{
    handle elementId = 0;
    m_time_t ts = 0;
    int64_t order = 0;
    Error e = API_OK;
#ifndef NDEBUG
    bool isNew = mElement->id() == UNDEF;
#endif
    const SetElement* el = nullptr;
    bool parsedOk = procerrorcode(r, e) || procresultid(json, r, elementId, ts, nullptr, nullptr, nullptr, &order); // 'aep' does not return 's'

    if (!parsedOk)
    {
        e = API_EINTERNAL;
    }
    else if (e == API_OK)
    {
        mElement->setTs(ts);
        mElement->setOrder(order); // this is now present in all 'aep' responses
        assert(isNew || mElement->id() == elementId);
        mElement->setId(elementId);
        el = client->addOrUpdateSetElement(std::move(*mElement));
    }

    if (mCompletion)
    {
        mCompletion(e, el);
    }

    return parsedOk;
}

CommandRemoveSetElements::CommandRemoveSetElements(MegaClient* cl, handle sid, vector<handle>&& eids,
                                                   std::function<void(Error, const vector<int64_t>*)> completion)
    : mSetId(sid), mElemIds(std::move(eids)), mCompletion(completion)
{
    cmd("aerb");

    arg("s", reinterpret_cast<const byte*>(&sid), MegaClient::SETHANDLE);

    beginarray("e");

    for (auto& eh : mElemIds)
    {
        element(reinterpret_cast<const byte*>(&eh), MegaClient::SETELEMENTHANDLE);
    }

    endarray();

    notself(cl); // don't process its Action Packet after sending this
}

bool CommandRemoveSetElements::procresult(Result r, JSON& json)
{
    Error e = API_OK;
    if (procerrorcode(r, e))
    {
        if (mCompletion)
        {
            mCompletion(e, nullptr);
        }
        return true;
    }
    else if (!r.hasJsonArray())
    {
        LOG_err << "Sets: failed to parse `aerb` response";
        if (mCompletion)
        {
            mCompletion(API_EINTERNAL, nullptr);
        }
        return false;
    }

    bool jsonOk = true;
    vector<int64_t> errs(mElemIds.size());
    for (size_t elCount = 0u; elCount < mElemIds.size(); ++elCount)
    {
        if (json.isnumeric())
        {
            errs[elCount] = json.getint();
        }
        else if (json.enterobject())
        {
            handle errEid = UNDEF;
            if (procExtendedError(json, errs[elCount], errEid))
            {
                if (errEid == UNDEF) LOG_warn << "Sets: Extended error missing Element id in `aerb`";
            }
            else
            {
                jsonOk = false;
            }

            if (!json.leaveobject())
            {
                LOG_err << "Sets: failed to parse Element object in `aerb` response";
                jsonOk = false;
            }
        }
        else
        {
            LOG_err << "Sets: failed to parse Element removal response in `aerb` command response";
            jsonOk = false;
        }

        if (!jsonOk) break;

        if (errs[elCount] == API_OK && !client->deleteSetElement(mSetId, mElemIds[elCount]))
        {
            LOG_err << "Sets: Failed to remove Element in `aerb` command response";
            errs[elCount] = API_ENOENT;
        }
    }

    if (mCompletion)
    {
        mCompletion(e, &errs);
    }

    return jsonOk;
}

CommandRemoveSetElement::CommandRemoveSetElement(MegaClient* cl, handle sid, handle eid, std::function<void(Error)> completion)
    : mSetId(sid), mElementId(eid), mCompletion(completion)
{
    mSeqtagArray = true;
    cmd("aer");
    arg("id", (byte*)&eid, MegaClient::SETELEMENTHANDLE);

    notself(cl); // don't process its Action Packet after sending this
}

bool CommandRemoveSetElement::procresult(Result r, JSON& json)
{
    handle elementId = 0;
    m_time_t ts = 0;
    Error e = API_OK;
    bool parsedOk = procerrorcode(r, e) || procresultid(json, r, elementId, ts, nullptr);

    if (parsedOk && e == API_OK)
    {
        if (!client->deleteSetElement(mSetId, mElementId))
        {
            LOG_err << "Sets: Failed to remove Element in `aer` command response";
            e = API_ENOENT;
        }
    }

    if (mCompletion)
    {
        mCompletion(e);
    }

    return parsedOk;
}

CommandExportSet::CommandExportSet(MegaClient* cl, Set&& s, bool makePublic, std::function<void(Error)> completion)
    : mSet(new Set(std::move(s))), mCompletion(completion)
{
    mSeqtagArray = true;
    cmd("ass");
    arg("id", (byte*)&mSet->id(), MegaClient::SETHANDLE);
    if (!makePublic) arg("d", 1);

    notself(cl);
}

bool CommandExportSet::procresult(Result r, JSON& json)
{
    handle sid = mSet->id();
    handle publicId = UNDEF;
    m_time_t ts = m_time(nullptr); // made it up for case that API returns [0] (like for "d":1)
    Error e = API_OK;
    const bool parsedOk = procerrorcode(r, e) || procresultid(json, r, sid, ts, nullptr, nullptr, nullptr, nullptr, &publicId);

    if (sid != mSet->id())
    {
        LOG_err << "Sets: command 'ass' in processing result. Received Set id " << toHandle(sid)
                << " expected Set id " << toHandle(mSet->id());
        assert(false);
    }

    if ((parsedOk) && e == API_OK)
    {
        mSet->setPublicId(publicId);
        mSet->setTs(ts);
        mSet->setChanged(Set::CH_EXPORTED);
        if (!client->updateSet(std::move(*mSet)))
        {
            LOG_warn << "Sets: comand 'ass' succeeded, but Set was not found";
            e = API_ENOENT;
        }
    }

    if (mCompletion) mCompletion(e);

    return parsedOk;
}

// -------- end of Sets and Elements


#ifdef ENABLE_CHAT

bool CommandMeetingStart::procresult(Command::Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        mCompletion(r.errorOrOK(), "", UNDEF);
        return true;
    }

    handle callid = UNDEF;
    string sfuUrl;

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID6('c', 'a', 'l', 'l', 'I', 'd'):
                callid = json.gethandle(MegaClient::CHATHANDLE);
                break;

            case MAKENAMEID3('s', 'f', 'u'):
                json.storeobject(&sfuUrl);
                break;

            case EOO:
                mCompletion(API_OK, sfuUrl, callid);
                return true;
                break;

            default:
                if (!json.storeobject())
                {
                    mCompletion(API_EINTERNAL, "", UNDEF);
                    return false;
                }
        }
    }
}

CommandMeetingStart::CommandMeetingStart(MegaClient* client, const handle chatid, const bool notRinging, CommandMeetingStartCompletion completion)
    : mCompletion(completion)
{
    cmd("mcms");
    arg("cid", (byte*)&chatid, MegaClient::CHATHANDLE);

    if (client->mSfuid != sfu_invalid_id)
    {
        arg("sfu", client->mSfuid);
    }

    if (notRinging)
    {
        arg("nr", 1);
    }
    tag = client->reqtag;
}

bool CommandMeetingJoin::procresult(Command::Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        mCompletion(r.errorOrOK(), "");
        return true;
    }

    string sfuUrl;

    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID3('u', 'r', 'l'):
                json.storeobject(&sfuUrl);
                break;

            case EOO:
                mCompletion(API_OK, sfuUrl);
                return true;
                break;

            default:
                if (!json.storeobject())
                {
                    mCompletion(API_EINTERNAL, "");
                    return false;
                }
        }
    }
}

CommandMeetingJoin::CommandMeetingJoin(MegaClient *client, handle chatid, handle callid, CommandMeetingJoinCompletion completion)
    : mCompletion(completion)
{
    cmd("mcmj");
    arg("cid", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("mid", (byte*)&callid, MegaClient::CHATHANDLE);

    tag = client->reqtag;
}

bool CommandMeetingEnd::procresult(Command::Result r, JSON&)
{
    mCompletion(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandMeetingEnd::CommandMeetingEnd(MegaClient *client, handle chatid, handle callid, int reason, CommandMeetingEndCompletion completion)
    : mCompletion(completion)
{
    cmd("mcme");
    arg("cid", (byte*)&chatid, MegaClient::CHATHANDLE);
    arg("mid", (byte*)&callid, MegaClient::CHATHANDLE);
    // At meeting first version, only valid reason is 0x02 (REJECTED)
    arg("r", reason);

    tag = client->reqtag;
}

bool CommandRingUser::procresult(Command::Result r, JSON&)
{
    mCompletion(r.errorOrOK());
    return r.wasErrorOrOK();
}

CommandRingUser::CommandRingUser(MegaClient* client, handle chatid, handle userid, CommandRingUserCompletion completion)
    : mCompletion(completion)
{
    cmd("mcru");
    arg("u", reinterpret_cast<byte*>(&userid), MegaClient::CHATHANDLE);
    arg("cid", reinterpret_cast<byte*>(&chatid), MegaClient::CHATHANDLE);
    tag = client->reqtag;
}

CommandScheduledMeetingAddOrUpdate::CommandScheduledMeetingAddOrUpdate(MegaClient* client, const ScheduledMeeting *schedMeeting, const char* chatTitle, CommandScheduledMeetingAddOrUpdateCompletion completion)
    : mScheduledMeeting(schedMeeting->copy()), mCompletion(completion)
{
    assert(schedMeeting);
    cmd("mcsmp");
    arg("v", 1); // add version to receive cmd array

    // this one does produce an `st`, with a json {object} after
    mSeqtagArray = true;

    if (chatTitle && strlen(chatTitle))
    {
        // update chatroom title along with sm title
        mChatTitle.assign(chatTitle, strlen(chatTitle));
        arg("ct", mChatTitle.c_str());
    }

    createSchedMeetingJson(mScheduledMeeting.get());
    notself(client); // set i param to ignore action packet generated by our own action
    tag = client->reqtag;
}

bool CommandScheduledMeetingAddOrUpdate::procresult(Command::Result r, JSON& json)
{
    assert(mScheduledMeeting);
    if (r.wasErrorOrOK())
    {
        if (mCompletion) { mCompletion(r.errorOrOK(), nullptr); }
        return true;
    }

    bool exit = false;
    handle schedId = UNDEF;
    handle_set childMeetingsDeleted;
    while (!exit)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID3('c', 'm', 'd'):
            {
                if (json.enterarray())
                {
                    while(json.ishandle(MegaClient::CHATHANDLE))
                    {
                        childMeetingsDeleted.insert(json.gethandle());
                    }
                    json.leavearray();
                }
                else
                {
                    if (mCompletion) { mCompletion(API_EINTERNAL, nullptr); }
                    return false;
                }
                break;
            }
            case MAKENAMEID2('i', 'd'):
                schedId = json.gethandle(MegaClient::CHATHANDLE);
                mScheduledMeeting->setSchedId(schedId);
                break;

            case EOO:
                exit = true;
                break;

            default:
                if (!json.storeobject())
                {
                    if (mCompletion) { mCompletion(API_EINTERNAL, nullptr); }
                    return false;
                }
        }
    }

    // sanity checks for scheduled meeting
    if (!mScheduledMeeting || !mScheduledMeeting->isValid())
    {
        if (mScheduledMeeting) { client->reportInvalidSchedMeeting(mScheduledMeeting.get()); }
        if (mCompletion)       { mCompletion(API_EINTERNAL, nullptr); }
        return true;
    }

    auto it = client->chats.find(mScheduledMeeting->chatid());
    if (it == client->chats.end())
    {
        if (mCompletion) { mCompletion(API_EINTERNAL, nullptr); }
        return true;
    }
    TextChat* chat = it->second;

    // remove child scheduled meetings in cmd (child meetings deleted) array
    chat->removeSchedMeetingsList(childMeetingsDeleted);

    // clear scheduled meeting occurrences for the chat
    client->clearSchedOccurrences(*chat);

    // update chat title
    if (!mChatTitle.empty())
    {
        chat->setTitle(mChatTitle.c_str());
    }

    // add scheduled meeting
    const bool added = chat->addOrUpdateSchedMeeting(std::unique_ptr<ScheduledMeeting>(mScheduledMeeting->copy())); // add or update scheduled meeting if already exists

    // notify chat
    chat->setTag(tag ? tag : -1);
    client->notifychat(chat);

    if (mCompletion) { mCompletion(added ? API_OK : API_EINTERNAL, mScheduledMeeting.get()); }
    return true;
}

CommandScheduledMeetingRemove::CommandScheduledMeetingRemove(MegaClient* client, handle chatid, handle schedMeeting, CommandScheduledMeetingRemoveCompletion completion)
    : mChatId(chatid), mSchedId(schedMeeting), mCompletion(completion)
{
    cmd("mcsmr");
    arg("id", (byte*) &schedMeeting, MegaClient::CHATHANDLE); // scheduled meeting handle
    notself(client); // set i param to ignore action packet generated by our own action
    tag = client->reqtag;
}

bool CommandScheduledMeetingRemove::procresult(Command::Result r, JSON&)
{
    if (!r.wasErrorOrOK())
    {
        if (mCompletion) { mCompletion(r.errorOrOK()); }
        return false;
    }

    if (r.wasError(API_OK))
    {
        auto it = client->chats.find(mChatId);
        if (it == client->chats.end())
        {
            if (mCompletion) { mCompletion(API_EINTERNAL); }
            return true;
        }

        // remove scheduled meeting and all it's children
        TextChat* chat = it->second;
        if (chat->removeSchedMeeting(mSchedId))
        {
            // remove children scheduled meetings (API requirement)
            chat->removeChildSchedMeetings(mSchedId);
        }

        client->clearSchedOccurrences(*chat);
        chat->setTag(tag ? tag : -1);
        client->notifychat(chat);
    }

    if (mCompletion) { mCompletion(r.errorOrOK()); }
    return true;
}

CommandScheduledMeetingFetch::CommandScheduledMeetingFetch(
    MegaClient* client,
    handle chatid,
    handle schedMeeting,
    CommandScheduledMeetingFetchCompletion completion):
    mCompletion(completion)
{
    cmd("mcsmf");
    if (schedMeeting != UNDEF) { arg("id", (byte*) &schedMeeting, MegaClient::CHATHANDLE); }
    if (chatid != UNDEF)       { arg("cid", (byte*) &chatid, MegaClient::CHATHANDLE); }
    tag = client->reqtag;
}

bool CommandScheduledMeetingFetch::procresult(Command::Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (mCompletion) { mCompletion(r.errorOrOK(), nullptr); }
        return true;
    }

    std::vector<std::unique_ptr<ScheduledMeeting>> schedMeetings;
    error err = client->parseScheduledMeetings(schedMeetings, false /*parsingOccurrences*/, &json);
    if (mCompletion) { mCompletion(err, err == API_OK ? &schedMeetings : nullptr); }
    return err == API_OK;
}

CommandScheduledMeetingFetchEvents::CommandScheduledMeetingFetchEvents(MegaClient* client, handle chatid, m_time_t since, m_time_t until, unsigned int count, bool byDemand, CommandScheduledMeetingFetchEventsCompletion completion)
 : mChatId(chatid),
   mByDemand(byDemand),
   mCompletion(completion ? completion : [](Error, const std::vector<std::unique_ptr<ScheduledMeeting>>*){})
{
    cmd("mcsmfo");
    arg("cid", (byte*) &chatid, MegaClient::CHATHANDLE);
    if (isValidTimeStamp(since))      { arg("cf", since); }
    if (isValidTimeStamp(until))      { arg("ct", until); }
    if (count)                        { arg("cc", count); }
    tag = client->reqtag;
}

bool CommandScheduledMeetingFetchEvents::procresult(Command::Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (mCompletion) { mCompletion(r.errorOrOK(), nullptr); }
        return true;
    }

    std::vector<std::unique_ptr<ScheduledMeeting>> schedMeetings;
    error err = client->parseScheduledMeetings(schedMeetings, true /*parsingOccurrences*/, &json);
    if (err)
    {
        if (mCompletion) { mCompletion(err, nullptr); }
        return false;
    }

    auto it = client->chats.find(mChatId);
    if (it == client->chats.end())
    {
        if (mCompletion) { mCompletion(API_EINTERNAL, nullptr); }
        return true;
    }
    TextChat* chat = it->second;
    // clear list in case it contains any element
    chat->clearUpdatedSchedMeetingOccurrences();

    // add received scheduled meetings occurrences from API into mUpdatedOcurrences to be notified
    for (auto& schedMeeting: schedMeetings)
    {
        chat->addUpdatedSchedMeetingOccurrence(std::unique_ptr<ScheduledMeeting>(schedMeeting->copy()));
    }

    // set the change type although we haven't received any occurrences (but there's no error and json proccessing has been succesfull)
    if (mByDemand) { chat->changed.schedOcurrAppend = true; }
    else           { chat->changed.schedOcurrReplace = true; }

    // just notify once, for all ocurrences received for the same chat
    chat->setTag(tag ? tag : -1);
    client->notifychat(chat);
    if (mCompletion) { mCompletion(API_OK, &schedMeetings); }
    return true;
}

#endif

bool CommandFetchAds::procresult(Command::Result r, JSON& json)
{
    string_map result;
    if (r.wasStrictlyError())
    {
        mCompletion(r.errorOrOK(), result);
        return true;
    }

    bool error = false;
    for (auto adUnit = std::begin(mAdUnits); adUnit != std::end(mAdUnits) && !error; ++adUnit)
    {
        if (json.isnumeric())
        {
            // -9 or any other error for the provided ad unit (error results order must match)
            result[*adUnit] = std::to_string(json.getint());
        }
        else if (json.enterobject())
        {
            std::string id;
            std::string iu;
            bool exit = false;
            while (!exit)
            {
                switch (json.getnameid())
                {
                    case MAKENAMEID2('i', 'd'):
                        json.storeobject(&id);
                        break;

                    case MAKENAMEID3('s', 'r', 'c'):
                        json.storeobject(&iu);
                        break;

                    case EOO:
                        exit = true;
                        if (!id.empty() && !iu.empty())
                        {
                            assert(id == *adUnit);
                            result[id] = iu;
                        }
                        else
                        {
                            error = true;
                            result.clear();
                        }
                        break;

                    default:
                        if (!json.storeobject())
                        {
                            result.clear();
                            mCompletion(API_EINTERNAL, result);
                            return false;
                        }
                        break;
                }
            }
            json.leaveobject();
        }
        else
        {
            result.clear();
            error = true;
        }
    }

    mCompletion((error ? API_EINTERNAL : API_OK), result);

    return !error;
}

CommandFetchAds::CommandFetchAds(MegaClient* client, int adFlags, const std::vector<std::string> &adUnits, handle publicHandle, CommandFetchAdsCompletion completion)
    : mCompletion(completion), mAdUnits(adUnits)
{
    cmd("adf");
    arg("ad", adFlags);
    arg("af", 1);   // ad format: URL

    if (!ISUNDEF(publicHandle))
    {
        arg("p", publicHandle);
    }

    beginarray("au");
    for (const std::string& adUnit : adUnits)
    {
        element(adUnit.c_str());
    }
    endarray();

    tag = client->reqtag;
}

bool CommandQueryAds::procresult(Command::Result r, JSON &json)
{
    if (r.wasErrorOrOK())
    {
        mCompletion(r.errorOrOK(), 0);
        return true;
    }

    if (!json.isnumeric())
    {
        // It's wrongly formatted, consume this one so the next command can be processed.
        LOG_err << "Command response badly formatted";
        mCompletion(API_EINTERNAL, 0);
        return false;
    }

    int value = json.getint32();
    mCompletion(API_OK, value);
    return true;
}

CommandQueryAds::CommandQueryAds(MegaClient* client, int adFlags, handle publicHandle, CommandQueryAdsCompletion completion)
    : mCompletion(completion)
{
    cmd("ads");
    arg("ad", adFlags);
    if (!ISUNDEF(publicHandle))
    {
        arg("ph", publicHandle);
    }

    tag = client->reqtag;
}

/* MegaVPN Commands BEGIN */
CommandGetVpnRegions::CommandGetVpnRegions(MegaClient* client, Cb&& completion)
:
    mCompletion(std::move(completion))
{
    cmd("vpnr");
    arg("v", 4); // include the DNS targets for each cluster in each region in response

    tag = client->reqtag;
}

bool CommandGetVpnRegions::parseRegions(JSON& json, vector<VpnRegion>* vpnRegions)
{
    bool storeData = vpnRegions != nullptr;
    string buffer;
    string* pBuffer = storeData ? &buffer : nullptr;
    for (; json.storeobject(pBuffer);) // region name
    {
        if (*json.pos == ':') // work around lack of functionality in enterobject()
            ++json.pos;
        if (!json.enterobject())
            return false;

        std::optional<VpnRegion> region{storeData ? std::make_optional(std::move(buffer)) :
                                                    std::nullopt};
        buffer.clear();

        for (; json.storeobject(pBuffer);) // cluster ID
        {
            int clusterID{};
            if (storeData)
            {
                auto [ptr, ec] =
                    std::from_chars(buffer.c_str(), buffer.c_str() + buffer.size(), clusterID);
                if (ec != std::errc())
                    return false;
            }

            if (*json.pos == ':')
                ++json.pos;
            if (!json.enterobject())
                return false;

            std::optional<string> host{storeData ? std::make_optional<string>() : std::nullopt};
            std::optional<vector<string>> dns{storeData ? std::make_optional<vector<string>>() :
                                                          std::nullopt};

            for (bool hasData = true; hasData;) // host, dns
            {
                switch (json.getnameid())
                {
                    case 'h':
                        if (!json.storeobject(pBuffer))
                            return false;
                        if (storeData)
                        {
                            host = std::move(buffer);
                            buffer.clear();
                        }
                        break;

                    case MAKENAMEID3('d', 'n', 's'):
                        if (!json.enterarray())
                            return false;

                        while (json.storeobject(pBuffer))
                        {
                            if (storeData)
                            {
                                dns->emplace_back(std::move(buffer));
                                buffer.clear();
                            }
                        }

                        if (!json.leavearray())
                            return false;
                        break;

                    case EOO:
                        hasData = false;
                        break;

                    default:
                        if (!json.storeobject())
                            return false;
                }
            }
            if (!json.leaveobject())
                return false;

            if (storeData)
            {
                region->addCluster(clusterID, {std::move(host.value()), std::move(dns.value())});
            }

            if (*json.pos == '}')
            {
                json.leaveobject();
                break;
            }
        }

        if (storeData)
        {
            vpnRegions->emplace_back(std::move(region.value()));
        }
    }

    return true;
}

bool CommandGetVpnRegions::procresult(Command::Result r, JSON& json)
{
    if (!r.hasJsonObject())
    {
        if (mCompletion) { mCompletion(API_EINTERNAL, {}); }
        return false;
    }

    // Parse regions
    vector<VpnRegion> vpnRegions;
    if (parseRegions(json, &vpnRegions))
    {
        mCompletion(API_OK, std::move(vpnRegions));
        return true;
    }
    else
    {
        mCompletion(API_EINTERNAL, {});
        return false;
    }
}

CommandGetVpnCredentials::CommandGetVpnCredentials(MegaClient* client, Cb&& completion)
:
    mCompletion(std::move(completion))
{
    cmd("vpng");
    arg("v", 4); // include the DNS targets for each cluster in each region in response

    tag = client->reqtag;
}

bool CommandGetVpnCredentials::procresult(Command::Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (mCompletion) { mCompletion(r.errorOrOK(), {}, {}, {}); }
        return true;
    }

    Error e(API_EINTERNAL);
    MapSlotIDToCredentialInfo mapSlotIDToCredentialInfo;
    MapClusterPublicKeys mapClusterPubKeys;
    {
        // Parse ClusterID and IPs
        if (json.enterobject())
        {
            string slotIDStr;
            bool parsedOk = true;
            while (parsedOk)
            {
                slotIDStr = json.getname();
                if (slotIDStr.empty())
                {
                    break;
                }

                int slotID = -1;
                try
                {
                    slotID = std::stoi(slotIDStr);
                }
                catch (std::exception const &ex)
                {
                    LOG_err << "[CommandGetVpnCredentials] Could not convert param SlotID(" << slotIDStr << ") to integer. Exception: " << ex.what();
                    parsedOk = false;
                }

                if (parsedOk && json.enterarray())
                {
                    CredentialInfo credentialInfo;
                    credentialInfo.clusterID = static_cast<int>(json.getint());
                    parsedOk = credentialInfo.clusterID != -1;
                    parsedOk = parsedOk && json.storeobject(&credentialInfo.ipv4);
                    parsedOk = parsedOk && json.storeobject(&credentialInfo.ipv6);
                    parsedOk = parsedOk && json.storeobject(&credentialInfo.deviceID);
                    if (parsedOk)
                    {
                        mapSlotIDToCredentialInfo.emplace(std::make_pair(slotID, std::move(credentialInfo)));
                    }
                    json.leavearray();
                }
            }
            if (!parsedOk)
            {
                // There were credentials, but something was wrong with the JSON
                if (mCompletion) { mCompletion(e, {}, {}, {}); }
                return false;
            }
            json.leaveobject();
        }
        else
        {
            // There should be a valid object at this point
            if (mCompletion) { mCompletion(e, {}, {}, {}); }
            return false;
        }

        // Parse Cluster Public Keys
        if (json.enterobject())
        {
            bool parsedOk = true;
            while (parsedOk)
            {
                std::string clusterIDStr = json.getname();
                if (clusterIDStr.empty())
                {
                    break;
                }

                int clusterID = -1;
                try
                {
                    clusterID = std::stoi(clusterIDStr);
                }
                catch (std::exception const &ex)
                {
                    LOG_err << "[CommandGetVpnCredentials] Could not convert param ClusterID(" << clusterIDStr << ") to integer. Exception: " << ex.what();
                    parsedOk = false;
                }

                if (parsedOk)
                {
                    std::string clusterPubKey;
                    if (!json.storeobject(&clusterPubKey))
                    {
                        parsedOk = false;
                        break;
                    }
                    mapClusterPubKeys.emplace(std::make_pair(clusterID, clusterPubKey));
                }
            }
            if (!parsedOk)
            {
                // There were credentials and a valid ClusterID, but something was wrong with the Cluster Public Key value
                if (mCompletion) { mCompletion(e, {}, {}, {}); }
                return false;
            }
            json.leaveobject();
        }
        else
        {
            // There were credentials, but there were no information regarding the Cluster Public Key(s)
            if (mCompletion) { mCompletion(e, {}, {}, {}); }
            return false;
        }
    }

    // Finally, parse VPN regions
    vector<VpnRegion> vpnRegions;
    if (json.enterobject() && CommandGetVpnRegions::parseRegions(json, &vpnRegions) &&
        json.leaveobject())
    {
        mCompletion(API_OK,
                    std::move(mapSlotIDToCredentialInfo),
                    std::move(mapClusterPubKeys),
                    std::move(vpnRegions));
        return true;
    }
    else
    {
        mCompletion(API_EINTERNAL, {}, {}, {});
        return false;
    }
}

CommandPutVpnCredential::CommandPutVpnCredential(MegaClient* client,
                                                std::string&& region,
                                                StringKeyPair&& userKeyPair,
                                                Cb&& completion)
:
    mRegion(std::move(region)),
    mUserKeyPair(std::move(userKeyPair)),
    mCompletion(std::move(completion))
{
    cmd("vpnp");
    arg("k", (byte*)mUserKeyPair.pubKey.c_str(), static_cast<int>(mUserKeyPair.pubKey.size()));
    arg("v", 4); // include the DNS targets for each cluster in each region in response

    tag = client->reqtag;
}

bool CommandPutVpnCredential::procresult(Command::Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (mCompletion) { mCompletion(r.errorOrOK(), -1, {}, {}); }
        return true;
    }

    if (!r.hasJsonArray())
    {
        if (mCompletion) { mCompletion(API_EINTERNAL, -1, {}, {}); }
        return false;
    }

    // We receive directly one array here (like in CommandGetVpnRegions), so we are inside the array already

    // Parse SlotID
    int slotID = static_cast<int>(json.getint());

    // Parse ClusterID
    int clusterID = static_cast<int>(json.getint());

    // Parse IPv4
    std::string ipv4;
    if (!json.storeobject(&ipv4))
    {
        if (mCompletion) { mCompletion(API_EINTERNAL, -1, {}, {}); }
        return false;
    }

    // Parse IPv6
    std::string ipv6;
    if (!json.storeobject(&ipv6))
    {
        if (mCompletion) { mCompletion(API_EINTERNAL, -1, {}, {}); }
        return false;
    }

    // Parse Cluster Public Key
    std::string clusterPubKey;
    if (!json.storeobject(&clusterPubKey))
    {
        if (mCompletion) { mCompletion(API_EINTERNAL, -1, {}, {}); }
        return false;
    }

    // Parse VPN regions
    vector<VpnRegion> vpnRegions;
    if (!json.enterobject() || !CommandGetVpnRegions::parseRegions(json, &vpnRegions) ||
        !json.leaveobject())
    {
        if (mCompletion)
        {
            mCompletion(API_EINTERNAL, -1, {}, {});
        }
        return false;
    }

    if (mCompletion)
    {
        std::string userPubKey = Base64::btoa(mUserKeyPair.pubKey);
        std::string newCredential;
        const auto itRegion = std::find_if(vpnRegions.begin(),
                                           vpnRegions.end(),
                                           [&name = mRegion](const VpnRegion& r)
                                           {
                                               return r.getName() == name;
                                           });
        if (itRegion != vpnRegions.end())
        {
            const auto& clusters = itRegion->getClusters();
            const auto itCluster = clusters.find(clusterID);
            if (itCluster != clusters.end())
            {
                auto peerKeyPair =
                    StringKeyPair(std::move(mUserKeyPair.privKey), std::move(clusterPubKey));
                newCredential = client->generateVpnCredentialString(itCluster->second.getHost(),
                                                                    itCluster->second.getDns(),
                                                                    std::move(ipv4),
                                                                    std::move(ipv6),
                                                                    std::move(peerKeyPair));
            }
        }

        if (newCredential.empty())
        {
            LOG_err << "[CommandPutVpnCredentials] Could not generate VPN credential string";
            mCompletion(API_ENOENT, -1, {}, {});
        }
        else
        {
            mCompletion(API_OK, slotID, std::move(userPubKey), std::move(newCredential));
        }
    }
    return true;
}

CommandDelVpnCredential::CommandDelVpnCredential(MegaClient* client, int slotID, Cb&& completion)
:
    mCompletion(std::move(completion))
{
    cmd("vpnd");
    arg("s", slotID); // SlotID to remove the credentials

    tag = client->reqtag;
}

bool CommandDelVpnCredential::procresult(Command::Result r, JSON&)
{
    if (mCompletion)
    {
        mCompletion(r.errorOrOK());
    }
    return r.wasErrorOrOK();
}

CommandCheckVpnCredential::CommandCheckVpnCredential(MegaClient* client, string&& userPubKey, Cb&& completion)
{
    cmd("vpnc");
    arg("k", userPubKey.c_str()); // User Public Key is already in B64 format
    tag = client->reqtag;

    mCompletion = std::move(completion);
}

bool CommandCheckVpnCredential::procresult(Command::Result r, JSON&)
{
    if (mCompletion)
    {
        mCompletion(r.errorOrOK());
    }
    return r.wasErrorOrOK();
}
/* MegaVPN Commands END*/

CommandFetchCreditCard::CommandFetchCreditCard(MegaClient* client, CommandFetchCreditCardCompletion completion)
    : mCompletion(std::move(completion))
{
    assert(mCompletion);
    cmd("cci");
    tag = client->reqtag;
}

bool CommandFetchCreditCard::procresult(Command::Result r, JSON& json)
{
    string_map creditCardInfo;

    if (r.wasStrictlyError())
    {
        mCompletion(r.errorOrOK(), creditCardInfo);
        return true;
    }

    if (r.hasJsonObject())
    {
        for (;;)
        {
            string name = json.getnameWithoutAdvance();
            switch (json.getnameid())
            {
            case MAKENAMEID2('g', 'w'):
                creditCardInfo[name] = std::to_string(json.getint());
                break;

            case MAKENAMEID5('b', 'r', 'a', 'n', 'd'):
                creditCardInfo[name] = json.getname();
                break;

            case MAKENAMEID5('l', 'a', 's', 't', '4'):
                creditCardInfo[name] = json.getname();
                break;

            case MAKENAMEID8('e', 'x', 'p', '_', 'y', 'e', 'a', 'r'):
                creditCardInfo[name] = std::to_string(json.getint());
                break;

            case EOO:
                assert(creditCardInfo.size() == 5);
                mCompletion(API_OK, creditCardInfo);
                return true;

            default:
                if (name == "exp_month")
                {
                    creditCardInfo[name] = std::to_string(json.getint());
                }
                else if (!json.storeobject())
                {
                    creditCardInfo.clear();
                    mCompletion(API_EINTERNAL, creditCardInfo);
                    return false;
                }

                break;
            }
        }
    }
    else
    {
        mCompletion(API_EINTERNAL, creditCardInfo);
    }

    return false;
}

CommandCreatePasswordManagerBase::CommandCreatePasswordManagerBase(MegaClient* cl, std::unique_ptr<NewNode> nn, int ctag,
                                                                   CommandCreatePasswordManagerBase::Completion&& cb)
    : mNewNode(std::move(nn)), mCompletion(std::move(cb))
{
    mSeqtagArray = true;

    cmd("pwmp");
    // APs "t" (for the new node/folder) and "ua" (for the new user attribute) triggered

    assert(mNewNode);

    arg("k", reinterpret_cast<const byte*>(mNewNode->nodekey.data()),
        static_cast<int>(mNewNode->nodekey.size()));
    if (mNewNode->attrstring)
    {
        arg("at", reinterpret_cast<const byte*>(mNewNode->attrstring->data()),
            static_cast<int>(mNewNode->attrstring->size()));
    }

    // although these won't be used, they are updated for integrity
    tag = ctag;
    client = cl;
};

bool CommandCreatePasswordManagerBase::procresult(Result r, JSON &json)
{
    // APs will update user data (in v3: wait for APs before returning)

    if (r.wasErrorOrOK())
    {
        if (mCompletion) mCompletion(r.errorOrOK(), nullptr);
        return true;
    }

    NodeHandle folderHandle;
    std::string key;
    std::unique_ptr<std::string> attrString;  // optionals are C++17
    m_off_t t = 0;
    for (;;)
    {
        // not interested in already-known "k" (user:key), "t", "at", "u", "ts"
        switch (json.getnameid())
        {
        case 'h':
            folderHandle.set6byte(json.gethandle(MegaClient::NODEHANDLE));
            break;
        case 'k':
            json.storeobject(&key);
            break;
        case 'a':
            attrString = std::make_unique<std::string>();
            json.storeobject(attrString.get());
            break;
        case 't':
        {
            t = json.getint();
            break;
        }
        case EOO:
        {
            bool sanityChecksFailed = false;
            const std::string msg {"Password Manager: wrong node type received in command response. Received "};
            if (FOLDERNODE != static_cast<nodetype_t>(t))
            {
                LOG_err << msg << "type " << t << " expected " << FOLDERNODE;
                sanityChecksFailed = true;
            }

            const auto keySeparatorPos = key.find(":");
            auto keyBeginning = keySeparatorPos + 1;
            if (keySeparatorPos == std::string::npos)
            {
                LOG_warn << msg << "unexpected key field value |" << key << "| missing separator ':'."
                         << " Attempting key value format without separator ':'";
                keyBeginning = 0;
            }

            const std::string aux {Base64::btoa(mNewNode->nodekey)};
            key = key.substr(keyBeginning);
            if (key != aux)
            {
                LOG_err << "node key value |" << key << "| different than expected |" << aux << "|";
                sanityChecksFailed = true;
            }

            const auto& at = mNewNode->attrstring;
            const std::string atAux = at ? Base64::btoa(*(at.get())) : "";
            if ((!at && attrString) || (at && !attrString) ||  // if only 1 exists
                (at && attrString && atAux != *attrString))    // or both exist and are different
            {
                LOG_err << "node attributes |" << (attrString ? *attrString : "")
                        << "| different than expected |" << atAux << "|";
                sanityChecksFailed = true;
            }

            if (sanityChecksFailed)
            {
                if (mCompletion) mCompletion(API_EINTERNAL, nullptr);
                return true;
            }


            mNewNode->nodehandle = folderHandle.as8byte();

            if (mCompletion) mCompletion(API_OK, std::move(mNewNode));
            return true;
        }
        default:
            if (!json.storeobject())
            {
                LOG_err << "Password Manager: error parsing param";
                if (mCompletion) mCompletion(API_EINTERNAL, nullptr);
                return false;
            }
        }
    }
}

CommandGetNotifications::CommandGetNotifications(MegaClient* client, ResultFunc onResult)
    : mOnResult(onResult)
{
    cmd("gnotif");

    tag = client->reqtag;

    if (!mOnResult)
    {
        mOnResult = [](const Error&, vector<DynamicMessageNotification>&&)
        {
            LOG_err << "The result of 'gnotif' will be lost";
        };
    }
}

bool CommandGetNotifications::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        LOG_err << "Unexpected response of 'gnotif' command";
        mOnResult(r.errorOrOK(), {});
        return true;
    }

    vector<DynamicMessageNotification> notifications;

    while (json.enterobject())
    {
        notifications.emplace_back();
        DynamicMessageNotification& notification = notifications.back();

        for (nameid nid = json.getnameid(); nid != EOO; nid = json.getnameid())
        {
            switch (nid)
            {
            case MAKENAMEID2('i', 'd'):
                notification.id = json.getint();
                break;

            case 't':
                json.storeobject(&notification.title);
                notification.title = Base64::atob(notification.title);
                break;

            case 'd':
                json.storeobject(&notification.description);
                notification.description = Base64::atob(notification.description);
                break;

            case MAKENAMEID3('i', 'm', 'g'):
                json.storeobject(&notification.imageName);
                break;

            case MAKENAMEID4('i', 'c', 'o', 'n'):
                json.storeobject(&notification.iconName);
                break;

            case MAKENAMEID3('d', 's', 'p'):
                json.storeobject(&notification.imagePath);
                break;

            case 's':
                notification.start = json.getint();
                break;

            case 'e':
                notification.end = json.getint();
                break;

            case MAKENAMEID2('s', 'b'):
                notification.showBanner = json.getbool();
                break;

            case MAKENAMEID4('c', 't', 'a', '1'):
            {
                if (!readCallToAction(json, notification.callToAction1))
                {
                    LOG_err << "Unable to read 'cta1' in 'gnotif' response";
                    mOnResult(API_EINTERNAL, {});
                    return false;
                }
                break;
            }

            case MAKENAMEID4('c', 't', 'a', '2'):
            {
                if (!readCallToAction(json, notification.callToAction2))
                {
                    LOG_err << "Unable to read 'cta2' in 'gnotif' response";
                    mOnResult(API_EINTERNAL, {});
                    return false;
                }
                break;
            }

            case 'm':
                if (!readRenderModes(json, notification.renderModes))
                {
                    LOG_err << "Unable to read 'm' in 'gnotif' response";
                    mOnResult(API_EINTERNAL, {});
                    return false;
                }
                break;

            default:
                if (!json.storeobject())
                {
                    LOG_err << "Failed to parse 'gnotif' response";
                    mOnResult(API_EINTERNAL, {});
                    return false;
                }
                break;
            }
        }

        if (!json.leaveobject())
        {
            LOG_err << "Unable to leave json object in 'gnotif' response";
            mOnResult(API_EINTERNAL, {});
            return false;
        }
    }

    mOnResult(API_OK, std::move(notifications));
    return true;
}

bool CommandGetNotifications::readCallToAction(JSON& json, map<string, string>& action)
{
    if (!json.enterobject())
    {
        return false;
    }

    for (nameid nid = json.getnameid(); nid != EOO; nid = json.getnameid())
    {
        switch (nid)
        {
        case MAKENAMEID4('l', 'i', 'n', 'k'):
        {
            json.storeobject(&action["link"]);
            break;
        }
        case MAKENAMEID4('t', 'e', 'x', 't'):
        {
            string& t = action["text"];
            json.storeobject(&t);
            t = Base64::atob(t);
            break;
        }
        default:
            if (!json.storeobject())
            {
                return false;
            }
        }
    }

    return json.leaveobject();
}

bool CommandGetNotifications::readRenderModes(JSON& json, map<string, map<string, string>>& modes)
{
    if (!json.enterobject())
    {
        return false;
    }

    for (string renderMode = json.getname(); !renderMode.empty(); renderMode = json.getname())
    {
        if (!json.enterobject())
        {
            return false;
        }

        auto& fields = modes[std::move(renderMode)];

        for (string f, v; json.storeKeyValueFromObject(f, v);)
        {
            fields.emplace(std::move(f), std::move(v));
            // Clear moved-from strings to avoid "Use of a moved from object" warning
            // (false-positive in current implementation)
            f.clear();
            v.clear();
        }

        if (!json.leaveobject())
        {
            return false;
        }
    }

    return json.leaveobject();
}

CommandGetActiveSurveyTriggerActions::CommandGetActiveSurveyTriggerActions(MegaClient* client,
                                                                           Completion&& completion)
{
    cmd("gsur");
    mCompletion = std::move(completion);
    tag = client->reqtag;
}

std::vector<uint32_t> CommandGetActiveSurveyTriggerActions::parseTriggerActionIds(JSON& json)
{
    std::vector<uint32_t> ids;

    // Trigger action ID is a small positive integer
    int id = 0;
    while (json.isnumeric() && (id = json.getint32()) > 0)
    {
        ids.push_back(static_cast<uint32_t>(id));
    }

    return ids;
}

bool CommandGetActiveSurveyTriggerActions::procresult(Result r, JSON& json)
{
    std::vector<uint32_t> ids;
    if (r.wasErrorOrOK())
    {
        // Preventive: convert API_OK to API_ENOENT
        Error e = r.wasError(API_OK) ? Error{API_ENOENT} : r.errorOrOK();
        onCompletion(e, ids);
        return true;
    }

    if (!r.hasJsonArray())
    {
        // Not expect to happen
        assert(r.hasJsonArray() && "Unexpected response for gsur command");
        onCompletion(API_EINTERNAL, ids);
        return false;
    }

    // Inside Json array and parse
    ids = parseTriggerActionIds(json);

    Error err = ids.empty() ? API_ENOENT : API_OK;

    onCompletion(err, ids);

    return true;
}

CommandGetSurvey::CommandGetSurvey(MegaClient* client,
                                   unsigned int triggerActionId,
                                   Completion&& completion)
{
    cmd("ssur");
    arg("t", static_cast<m_off_t>(triggerActionId));
    mCompletion = std::move(completion);
    tag = client->reqtag;
}

//
// Returns true if parsing was successful, false otherwise.
//
bool CommandGetSurvey::parseSurvey(JSON& json, Survey& survey)
{
    for (;;)
    {
        switch (json.getnameid())
        {
            case MAKENAMEID1('s'):
                if ((survey.h = json.gethandle(MegaClient::SURVEYHANDLE)) == UNDEF)
                    return false;
                break;

            case MAKENAMEID1('m'):
                if (auto value = json.getint32(); value < 0)
                    return false;
                else
                    survey.maxResponse = static_cast<unsigned int>(value);
                break;

            case MAKENAMEID1('i'):
                if (!json.storeobject(&survey.image))
                    return false;
                break;

            case MAKENAMEID1('c'):
                if (!json.storeobject(&survey.content))
                    return false;
                break;

            case EOO:
                return true;
                break;

            default:
                if (!json.storeobject())
                    return false;
                break;
        }
    }
}

bool CommandGetSurvey::procresult(Result r, JSON& json)
{
    Survey survey{};
    if (r.wasErrorOrOK())
    {
        // Preventive: convert API_OK to API_ENOENT
        const Error e = r.wasError(API_OK) ? Error{API_ENOENT} : r.errorOrOK();

        onCompletion(e, survey);

        return true;
    }

    const bool parsedOk = parseSurvey(json, survey);

    const Error e = parsedOk && survey.isValid() ? API_OK : API_EINTERNAL;

    onCompletion(e, survey);

    return parsedOk;
}

CommandAnswerSurvey::CommandAnswerSurvey(MegaClient* client,
                                         const Answer& answer,
                                         Completion&& completion)
{
    cmd("asur");

    arg("s", Base64Str<MegaClient::SURVEYHANDLE>(answer.mHandle));

    arg("t", static_cast<m_off_t>(answer.mTriggerActionId));

    if (!answer.mResponse.empty())
        arg("r", answer.mResponse.c_str());

    if (!answer.mComment.empty())
        arg("c", answer.mComment.c_str());

    mCompletion = std::move(completion);

    tag = client->reqtag;
}

bool CommandAnswerSurvey::procresult(Result r, JSON&)
{
    if (r.wasErrorOrOK())
    {
        onCompletion(r.errorOrOK());

        return true;
    }

    return false;
}

} // namespace
