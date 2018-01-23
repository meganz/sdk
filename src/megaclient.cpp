/**
 * @file megaclient.cpp
 * @brief Client access engine core logic
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
#include "mega/mediafileattribute.h"

namespace mega {

// FIXME: generate cr element for file imports
// FIXME: support invite links (including responding to sharekey requests)
// FIXME: instead of copying nodes, move if the source is in the rubbish to reduce node creation load on the servers
// FIXME: prevent synced folder from being moved into another synced folder

bool MegaClient::disablepkp = false;

// root URL for API access
string MegaClient::APIURL = "https://g.api.mega.co.nz/";

// root URL for GeLB requests
string MegaClient::GELBURL = "https://gelb.karere.mega.nz/";

// root URL for chat stats
string MegaClient::CHATSTATSURL = "https://stats.karere.mega.nz/";

// maximum number of concurrent transfers (uploads + downloads)
const unsigned MegaClient::MAXTOTALTRANSFERS = 30;

// maximum number of concurrent transfers (uploads or downloads)
const unsigned MegaClient::MAXTRANSFERS = 20;

// maximum number of queued putfa before halting the upload queue
const int MegaClient::MAXQUEUEDFA = 24;

// maximum number of concurrent putfa
const int MegaClient::MAXPUTFA = 8;

#ifdef ENABLE_SYNC
// //bin/SyncDebris/yyyy-mm-dd base folder name
const char* const MegaClient::SYNCDEBRISFOLDERNAME = "SyncDebris";
#endif

// exported link marker
const char* const MegaClient::EXPORTEDLINK = "EXP";

// public key to send payment details
const char MegaClient::PAYMENT_PUBKEY[] =
        "CADB-9t4WSMCs6we8CNcAmq97_bP-eXa9pn7SwGPxXpTuScijDrLf_ooneCQnnRBDvE"
        "MNqTK3ULj1Q3bt757SQKDZ0snjbwlU2_D-rkBBbjWCs-S61R0Vlg8AI5q6oizH0pjpD"
        "eOhpsv2DUlvCa4Hjgy_bRpX8v9fJvbKI2bT3GXJWE7tu8nlKHgz8Q7NE3Ycj5XuUfCW"
        "GgOvPGBC-8qPOyg98Vloy53vja2mBjw4ycodx-ZFCt8i8b9Z8KongRMROmvoB4jY8ge"
        "ym1mA5iSSsMroGLypv9PueOTfZlG3UTpD83v6F3w8uGHY9phFZ-k2JbCd_-s-7gyfBE"
        "TpPvuz-oZABEBAAE";

// default number of seconds to wait after a bandwidth overquota
dstime MegaClient::DEFAULT_BW_OVERQUOTA_BACKOFF_SECS = 3600;

// stats id
char* MegaClient::statsid = NULL;

// decrypt key (symmetric or asymmetric), rewrite asymmetric to symmetric key
bool MegaClient::decryptkey(const char* sk, byte* tk, int tl, SymmCipher* sc, int type, handle node)
{
    int sl;
    const char* ptr = sk;

    // measure key length
    while (*ptr && *ptr != '"' && *ptr != '/')
    {
        ptr++;
    }

    sl = ptr - sk;

    if (sl > 4 * FILENODEKEYLENGTH / 3 + 1)
    {
        // RSA-encrypted key - decrypt and update on the server to save space & client CPU time
        sl = sl / 4 * 3 + 3;

        if (sl > 4096)
        {
            return false;
        }

        byte* buf = new byte[sl];

        sl = Base64::atob(sk, buf, sl);

        // decrypt and set session ID for subsequent API communication
        if (!asymkey.decrypt(buf, sl, tk, tl))
        {
            delete[] buf;
            LOG_warn << "Corrupt or invalid RSA node key";
            return false;
        }

        delete[] buf;

        if (!ISUNDEF(node))
        {
            if (type)
            {
                sharekeyrewrite.push_back(node);
            }
            else
            {
                nodekeyrewrite.push_back(node);
            }
        }
    }
    else
    {
        if (Base64::atob(sk, tk, tl) != tl)
        {
            LOG_warn << "Corrupt or invalid symmetric node key";
            return false;
        }

        sc->ecb_decrypt(tk, tl);
    }

    return true;
}

// apply queued new shares
void MegaClient::mergenewshares(bool notify)
{
    newshare_list::iterator it;

    for (it = newshares.begin(); it != newshares.end(); )
    {
        NewShare* s = *it;

        mergenewshare(s, notify);

        delete s;
        newshares.erase(it++);
    }
}

void MegaClient::mergenewshare(NewShare *s, bool notify)
{
    bool skreceived = false;
    Node* n;

    if ((n = nodebyhandle(s->h)))
    {
        if (s->have_key && (!n->sharekey || memcmp(s->key, n->sharekey->key, SymmCipher::KEYLENGTH)))
        {
            // setting an outbound sharekey requires node authentication
            // unless coming from a trusted source (the local cache)
            bool auth = true;

            if (s->outgoing > 0)
            {
                if (!checkaccess(n, OWNERPRELOGIN))
                {
                    LOG_warn << "Attempt to create dislocated outbound share foiled";
                    auth = false;
                }
                else
                {
                    byte buf[SymmCipher::KEYLENGTH];

                    handleauth(s->h, buf);

                    if (memcmp(buf, s->auth, sizeof buf))
                    {
                        LOG_warn << "Attempt to create forged outbound share foiled";
                        auth = false;
                    }
                }
            }

            if (auth)
            {
                if (n->sharekey)
                {
                    if (!fetchingnodes)
                    {
                        int creqtag = reqtag;
                        reqtag = 0;
                        sendevent(99428,"Replacing share key");
                        reqtag = creqtag;
                    }
                    delete n->sharekey;
                }
                n->sharekey = new SymmCipher(s->key);
                skreceived = true;
            }
        }

        if (s->access == ACCESS_UNKNOWN && !s->have_key)
        {
            // share was deleted
            if (s->outgoing)
            {
                bool found = false;
                if (n->outshares)
                {
                    // outgoing share to user u deleted
                    share_map::iterator shareit = n->outshares->find(s->peer);
                    if (shareit != n->outshares->end())
                    {
                        Share *delshare = shareit->second;
                        n->outshares->erase(shareit);
                        found = true;
                        if (notify)
                        {
                            n->changed.outshares = true;
                            notifynode(n);
                        }
                        delete delshare;
                    }

                    if (!n->outshares->size())
                    {
                        delete n->outshares;
                        n->outshares = NULL;
                    }
                }
                if (n->pendingshares && !found && s->pending)
                {
                    // delete the pending share
                    share_map::iterator shareit = n->pendingshares->find(s->pending);
                    if (shareit != n->pendingshares->end())
                    {
                        Share *delshare = shareit->second;
                        n->pendingshares->erase(shareit);
                        found = true;
                        if (notify)
                        {
                            n->changed.pendingshares = true;
                            notifynode(n);
                        }
                        delete delshare;
                    }

                    if (!n->pendingshares->size())
                    {
                        delete n->pendingshares;
                        n->pendingshares = NULL;
                    }
                }

                // Erase sharekey if no outgoing shares (incl pending) exist
                if (s->remove_key && !n->outshares && !n->pendingshares)
                {
                    rewriteforeignkeys(n);

                    delete n->sharekey;
                    n->sharekey = NULL;
                }
            }
            else
            {
                // incoming share deleted - remove tree
                if (!n->parent)
                {
                    TreeProcDel td;
                    proctree(n, &td, true);
                }
                else
                {
                    if (n->inshare)
                    {
                        n->inshare->user->sharing.erase(n->nodehandle);
                        notifyuser(n->inshare->user);
                        n->inshare = NULL;
                    }
                }
            }
        }
        else
        {
            if (s->outgoing)
            {
                if ((!s->upgrade_pending_to_full && (!ISUNDEF(s->peer) || !ISUNDEF(s->pending)))
                    || (s->upgrade_pending_to_full && !ISUNDEF(s->peer) && !ISUNDEF(s->pending)))
                {
                    // perform mandatory verification of outgoing shares:
                    // only on own nodes and signed unless read from cache
                    if (checkaccess(n, OWNERPRELOGIN))
                    {
                        Share** sharep;
                        if (!ISUNDEF(s->pending))
                        {
                            // Pending share
                            if (!n->pendingshares)
                            {
                                n->pendingshares = new share_map();
                            }

                            if (s->upgrade_pending_to_full)
                            {
                                share_map::iterator shareit = n->pendingshares->find(s->pending);
                                if (shareit != n->pendingshares->end())
                                {
                                    // This is currently a pending share that needs to be upgraded to a full share
                                    // erase from pending shares & delete the pending share list if needed
                                    Share *delshare = shareit->second;
                                    n->pendingshares->erase(shareit);
                                    if (notify)
                                    {
                                        n->changed.pendingshares = true;
                                        notifynode(n);
                                    }
                                    delete delshare;
                                }

                                if (!n->pendingshares->size())
                                {
                                    delete n->pendingshares;
                                    n->pendingshares = NULL;
                                }

                                // clear this so we can fall through to below and have it re-create the share in
                                // the outshares list
                                s->pending = UNDEF;

                                // create the outshares list if needed
                                if (!n->outshares)
                                {
                                    n->outshares = new share_map();
                                }

                                sharep = &((*n->outshares)[s->peer]);
                            }
                            else
                            {
                                sharep = &((*n->pendingshares)[s->pending]);
                            }
                        }
                        else
                        {
                            // Normal outshare
                            if (!n->outshares)
                            {
                                n->outshares = new share_map();
                            }

                            sharep = &((*n->outshares)[s->peer]);
                        }

                        // modification of existing share or new share
                        if (*sharep)
                        {
                            (*sharep)->update(s->access, s->ts, findpcr(s->pending));
                        }
                        else
                        {
                            *sharep = new Share(ISUNDEF(s->peer) ? NULL : finduser(s->peer, 1), s->access, s->ts, findpcr(s->pending));
                        }

                        if (notify)
                        {
                            if (!ISUNDEF(s->pending))
                            {
                                n->changed.pendingshares = true;
                            }
                            else
                            {
                                n->changed.outshares = true;
                            }
                            notifynode(n);
                        }
                    }
                }
                else
                {
                    LOG_debug << "Merging share without peer information.";
                    // Outgoing shares received during fetchnodes are merged in two steps:
                    // 1. From readok(), a NewShare is created with the 'sharekey'
                    // 2. From readoutshares(), a NewShare is created with the 'peer' information
                }
            }
            else
            {
                if (!ISUNDEF(s->peer))
                {
                    if (s->peer)
                    {
                        if (!checkaccess(n, OWNERPRELOGIN))
                        {
                            // modification of existing share or new share
                            if (n->inshare)
                            {
                                n->inshare->update(s->access, s->ts);
                            }
                            else
                            {
                                n->inshare = new Share(finduser(s->peer, 1), s->access, s->ts, NULL);
                                n->inshare->user->sharing.insert(n->nodehandle);
                            }

                            if (notify)
                            {
                                n->changed.inshare = true;
                                notifynode(n);
                            }
                        }
                        else
                        {
                            LOG_warn << "Invalid inbound share location";
                        }
                    }
                    else
                    {
                        LOG_warn << "Invalid null peer on inbound share";
                    }
                }
                else
                {
                    if (skreceived && notify)
                    {
                        TreeProcApplyKey td;
                        proctree(n, &td);
                    }
                }
            }
        }
#ifdef ENABLE_SYNC
        if (n->inshare && s->access != FULL)
        {
            // check if the low(ered) access level is affecting any syncs
            // a) have we just cut off full access to a subtree of a sync?
            do {
                if (n->localnode && (n->localnode->sync->state == SYNC_ACTIVE || n->localnode->sync->state == SYNC_INITIALSCAN))
                {
                    LOG_warn << "Existing inbound share sync or part thereof lost full access";
                    n->localnode->sync->errorcode = API_EACCESS;
                    n->localnode->sync->changestate(SYNC_FAILED);
                }
            } while ((n = n->parent));

            // b) have we just lost full access to the subtree a sync is in?
            for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
            {
                if ((*it)->inshare && ((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN) && !checkaccess((*it)->localroot.node, FULL))
                {
                    LOG_warn << "Existing inbound share sync lost full access";
                    (*it)->errorcode = API_EACCESS;
                    (*it)->changestate(SYNC_FAILED);
                }
            }

        }
#endif
    }
}

// configure for full account session access
void MegaClient::setsid(const byte* newsid, unsigned len)
{
    auth = "&sid=";

    int t = auth.size();
    auth.resize(t + len * 4 / 3 + 4);
    auth.resize(t + Base64::btoa(newsid, len, (char*)(auth.c_str() + t)));
    
    sid.assign((const char*)newsid, len);
}

// configure for exported folder links access
void MegaClient::setrootnode(handle h)
{
    char buf[12];

    Base64::btoa((byte*)&h, NODEHANDLE, buf);

    auth = "&n=";
    auth.append(buf);
    publichandle = h;

    if (accountauth.size())
    {
        auth.append("&sid=");
        auth.append(accountauth);
    }
}

bool MegaClient::setlang(string *code)
{
    if (code && code->size() == 2)
    {
        lang = "&lang=";
        lang.append(*code);
        return true;
    }

    lang.clear();
    LOG_err << "Invalid language code: " << (code ? *code : "(null)");
    return false;
}

handle MegaClient::getrootpublicfolder()
{
    // if we logged into a folder...
    if (auth.find("&n=") != auth.npos)
    {
        return rootnodes[0];
    }
    else
    {
        return UNDEF;
    }
}

handle MegaClient::getpublicfolderhandle()
{
    return publichandle;
}

// set server-client sequence number
bool MegaClient::setscsn(JSON* j)
{
    handle t;

    if (j->storebinary((byte*)&t, sizeof t) != sizeof t)
    {
        return false;
    }

    Base64::btoa((byte*)&t, sizeof t, scsn);

    return true;
}

int MegaClient::nextreqtag()
{
    return ++reqtag;
}

int MegaClient::hexval(char c)
{
    return c > '9' ? c - 'a' + 10 : c - '0';
}

void MegaClient::exportDatabase(string filename)
{
    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "w");
    if (!fp)
    {
        LOG_warn << "Cannot export DB to file \"" << filename << "\"";
        return;
    }

    LOG_info << "Exporting database...";

    sctable->rewind();

    uint32_t id;
    string data;

    std::map<uint32_t, string> entries;
    while (sctable->next(&id, &data, &key))
    {
        entries.insert(std::pair<uint32_t, string>(id,data));
    }

    for (map<uint32_t, string>::iterator it = entries.begin(); it != entries.end(); it++)
    {
        fprintf(fp, "%8.d\t%s\n", it->first, it->second.c_str());
    }

    fclose(fp);

    LOG_info << "Database exported successfully to \"" << filename << "\"";
}

bool MegaClient::compareDatabases(string filename1, string filename2)
{
    LOG_info << "Comparing databases: \"" << filename1 << "\" and \"" << filename2 << "\"";
    FILE *fp1 = fopen(filename1.data(), "r");
    if (!fp1)
    {
        LOG_info << "Cannot open " << filename1;
        return false;
    }

    FILE *fp2 = fopen(filename2.data(), "r");
    if (!fp2)
    {
        fclose(fp1);

        LOG_info << "Cannot open " << filename2;
        return false;
    }

    const int N = 8192;
    char buf1[N];
    char buf2[N];

    do
    {
        size_t r1 = fread(buf1, 1, N, fp1);
        size_t r2 = fread(buf2, 1, N, fp2);

        if (r1 != r2 || memcmp(buf1, buf2, r1))
        {
            fclose(fp1);
            fclose(fp2);

            LOG_info << "Databases are different";
            return false;
        }
    }
    while (!feof(fp1) || !feof(fp2));

    fclose(fp1);
    fclose(fp2);

    LOG_info << "Databases are equal";
    return true;
}

void MegaClient::getrecoverylink(const char *email, bool hasMasterkey)
{
    reqs.add(new CommandGetRecoveryLink(this, email,
                hasMasterkey ? RECOVER_WITH_MASTERKEY : RECOVER_WITHOUT_MASTERKEY));
}

void MegaClient::queryrecoverylink(const char *code)
{
    reqs.add(new CommandQueryRecoveryLink(this, code));
}

void MegaClient::getprivatekey(const char *code)
{
    reqs.add(new CommandGetPrivateKey(this, code));
}

void MegaClient::confirmrecoverylink(const char *code, const char *email, const byte *pwkey, const byte *masterkey)
{
    SymmCipher pwcipher(pwkey);

    string emailstr = email;
    uint64_t loginHash = stringhash64(&emailstr, &pwcipher);

    if (masterkey)
    {
        // encrypt provided masterkey using the new password
        byte encryptedMasterKey[SymmCipher::KEYLENGTH];
        memcpy(encryptedMasterKey, masterkey, sizeof encryptedMasterKey);
        pwcipher.ecb_encrypt(encryptedMasterKey);

        reqs.add(new CommandConfirmRecoveryLink(this, code, loginHash, encryptedMasterKey, NULL));
    }
    else
    {
        // create a new masterkey
        byte masterkey[SymmCipher::KEYLENGTH];
        PrnGen::genblock(masterkey, sizeof masterkey);

        // generate a new session
        byte initialSession[2 * SymmCipher::KEYLENGTH];
        PrnGen::genblock(initialSession, sizeof initialSession);
        key.setkey(masterkey);
        key.ecb_encrypt(initialSession, initialSession + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH);

        // and encrypt the master key to the new password
        pwcipher.ecb_encrypt(masterkey);

        reqs.add(new CommandConfirmRecoveryLink(this, code, loginHash, masterkey, initialSession));
    }
}

void MegaClient::getcancellink(const char *email)
{
    reqs.add(new CommandGetRecoveryLink(this, email, CANCEL_ACCOUNT));
}

void MegaClient::confirmcancellink(const char *code)
{
    reqs.add(new CommandConfirmCancelLink(this, code));
}

void MegaClient::getemaillink(const char *email)
{
    reqs.add(new CommandGetEmailLink(this, email, 1));
}

void MegaClient::confirmemaillink(const char *code, const char *email, const byte *pwkey)
{
    SymmCipher pwcipher(pwkey);

    string emailstr = email;
    uint64_t loginHash = stringhash64(&emailstr, &pwcipher);

    reqs.add(new CommandConfirmEmailLink(this, code, email, loginHash, true));
}

// set warn level
void MegaClient::warn(const char* msg)
{
    LOG_warn << msg;
    warned = true;
}

// reset and return warnlevel
bool MegaClient::warnlevel()
{
    return warned ? (warned = false) | true : false;
}

// returns a matching child node by UTF-8 name (does not resolve name clashes)
// folder nodes take precedence over file nodes
Node* MegaClient::childnodebyname(Node* p, const char* name, bool skipfolders)
{
    string nname = name;
    Node *found = NULL;

    if (!p || p->type == FILENODE)
    {
        return NULL;
    }

    fsaccess->normalize(&nname);

    for (node_list::iterator it = p->children.begin(); it != p->children.end(); it++)
    {
        if (!strcmp(nname.c_str(), (*it)->displayname()))
        {
            if ((*it)->type != FILENODE && !skipfolders)
            {
                return *it;
            }

            found = *it;
            if (skipfolders)
            {
                return found;
            }
        }
    }

    return found;
}

void MegaClient::init()
{
    warned = false;
    csretrying = false;
    chunkfailed = false;
    statecurrent = false;
    totalNodes = 0;

#ifdef ENABLE_SYNC
    syncactivity = false;
    syncops = false;
    syncdebrisadding = false;
    syncdebrisminute = 0;
    syncscanfailed = false;
    syncfslockretry = false;
    syncfsopsfailed = false;
    syncdownretry = false;
    syncnagleretry = false;
    syncextraretry = false;
    faretrying = false;
    syncsup = true;
    syncdownrequired = false;
    syncuprequired = false;

    if (syncscanstate)
    {
        app->syncupdate_scanning(false);
        syncscanstate = false;
    }
#endif

    for (int i = sizeof rootnodes / sizeof *rootnodes; i--; )
    {
        rootnodes[i] = UNDEF;
    }

    delete pendingsc;
    pendingsc = NULL;

    btcs.reset();
    btsc.reset();
    btpfa.reset();
    btbadhost.reset();

    abortlockrequest();

    jsonsc.pos = NULL;
    insca = false;
    scnotifyurl.clear();
    *scsn = 0;
}

MegaClient::MegaClient(MegaApp* a, Waiter* w, HttpIO* h, FileSystemAccess* f, DbAccess* d, GfxProc* g, const char* k, const char* u)
{
    sctable = NULL;
    pendingsccommit = false;
    tctable = NULL;
    me = UNDEF;
    publichandle = UNDEF;
    followsymlinks = false;
    usealtdownport = false;
    usealtupport = false;
    retryessl = false;
    workinglockcs = NULL;
    scpaused = false;
    asyncfopens = 0;
    achievements_enabled = false;
    tsLogin = false;
    versions_disabled = false;

#ifndef EMSCRIPTEN
    autodownport = true;
    autoupport = true;
    usehttps = false;
    orderdownloadedchunks = false;
#else
    autodownport = false;
    autoupport = false;
    usehttps = true;
    orderdownloadedchunks = true;
#endif
    
    fetchingnodes = false;
    fetchnodestag = 0;

#ifdef ENABLE_SYNC
    syncscanstate = false;
    syncadding = 0;
    currsyncid = 0;
    totalLocalNodes = 0;
#endif

    pendingcs = NULL;
    pendingsc = NULL;

    xferpaused[PUT] = false;
    xferpaused[GET] = false;
    putmbpscap = 0;
    overquotauntil = 0;
    looprequested = false;

#ifdef ENABLE_CHAT
    fetchingkeys = false;
    signkey = NULL;
    chatkey = NULL;
#endif

    init();

    f->client = this;
    f->waiter = w;
    transferlist.client = this;

    if ((app = a))
    {
        a->client = this;
    }

    waiter = w;
    httpio = h;
    fsaccess = f;
    dbaccess = d;

    if ((gfx = g))
    {
        g->client = this;
    }

    slotit = tslots.end();

    userid = 0;

    connections[PUT] = 3;
    connections[GET] = 4;

    int i;

    // initialize random client application instance ID (for detecting own
    // actions in server-client stream)
    for (i = sizeof sessionid; i--; )
    {
        sessionid[i] = 'a' + PrnGen::genuint32(26);
    }

    // initialize random API request sequence ID (server API is idempotent)
    for (i = sizeof reqid; i--; )
    {
        reqid[i] = 'a' + PrnGen::genuint32(26);
    }

    nextuh = 0;  
    reqtag = 0;

    badhostcs = NULL;

    scsn[sizeof scsn - 1] = 0;
    cachedscsn = UNDEF;

    snprintf(appkey, sizeof appkey, "&ak=%s", k);

    // initialize useragent
    useragent = u;

    useragent.append(" (");
    fsaccess->osversion(&useragent);

    useragent.append(") MegaClient/" TOSTRING(MEGA_MAJOR_VERSION)
                     "." TOSTRING(MEGA_MINOR_VERSION)
                     "." TOSTRING(MEGA_MICRO_VERSION));

    LOG_debug << "User-Agent: " << useragent;
    LOG_debug << "Cryptopp version: " << CRYPTOPP_VERSION;

    h->setuseragent(&useragent);
    h->setmaxdownloadspeed(0);
    h->setmaxuploadspeed(0);
}

MegaClient::~MegaClient()
{
    locallogout();

    delete pendingcs;
    delete pendingsc;
    delete badhostcs;
    delete workinglockcs;
    delete sctable;
    delete tctable;
    delete dbaccess;
}

// nonblocking state machine executing all operations currently in progress
void MegaClient::exec()
{
    WAIT_CLASS::bumpds();

    if (overquotauntil && overquotauntil < Waiter::ds)
    {
        overquotauntil = 0;
    }

    if (httpio->inetisback())
    {
        LOG_info << "Internet connectivity returned - resetting all backoff timers";
        abortbackoff(overquotauntil <= Waiter::ds);
    }

    if (EVER(httpio->lastdata) && Waiter::ds >= httpio->lastdata + HttpIO::NETWORKTIMEOUT
            && !pendingcs)
    {
        LOG_debug << "Network timeout. Reconnecting";
        disconnect();
    }
    else if (EVER(disconnecttimestamp))
    {
        if (disconnecttimestamp <= Waiter::ds)
        {
            int creqtag = reqtag;
            reqtag = 0;
            sendevent(99427, "Timeout (server idle)");
            reqtag = creqtag;

            disconnect();
        }
    }
    else if (pendingcs && EVER(pendingcs->lastdata) && !requestLock && !fetchingnodes
            &&  Waiter::ds >= pendingcs->lastdata + HttpIO::REQUESTTIMEOUT)
    {
        LOG_debug << "Request timeout. Triggering a lock request";
        requestLock = true;
    }

    // successful network operation with a failed transfer chunk: increment error count
    // and continue transfers
    if (httpio->success && chunkfailed)
    {
        chunkfailed = false;

        for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); it++)
        {
            if ((*it)->failure)
            {
                (*it)->lasterror = API_EFAILED;
                (*it)->errorcount++;
                (*it)->failure = false;
                (*it)->lastdata = Waiter::ds;
                LOG_warn << "Transfer error count raised: " << (*it)->errorcount;
            }
        }
    }

    bool first = true;
    do
    {
        if (!first)
        {
            WAIT_CLASS::bumpds();
        }
        first = false;

        looprequested = false;

        if (pendinghttp.size())
        {
            pendinghttp_map::iterator it = pendinghttp.begin();
            while (it != pendinghttp.end())
            {
                GenericHttpReq *req = it->second;
                switch (req->status)
                {
                case REQ_FAILURE:
                    if (!req->httpstatus && (!req->maxretries || (req->numretry + 1) < req->maxretries))
                    {
                        req->numretry++;
                        req->status = REQ_PREPARED;
                        req->bt.backoff();
                        req->isbtactive = true;
                        LOG_warn << "Request failed (" << req->posturl << ") retrying ("
                                 << (req->numretry + 1) << " of " << req->maxretries << ")";
                        it++;
                        break;
                    }
                    // no retry -> fall through
                case REQ_SUCCESS:
                    restag = it->first;
                    app->http_result(req->httpstatus ? API_OK : API_EFAILED,
                                     req->httpstatus,
                                     req->buf ? (byte *)req->buf : (byte *)req->in.data(),
                                     req->buf ? req->bufpos : req->in.size());
                    delete req;
                    pendinghttp.erase(it++);
                    break;
                case REQ_PREPARED:
                    if (req->bt.armed())
                    {
                        req->isbtactive = false;
                        LOG_debug << "Sending retry for " << req->posturl;
                        switch (req->method)
                        {
                            case METHOD_GET:
                                req->get(this);
                                break;
                            case METHOD_POST:
                                req->post(this);
                                break;
                            case METHOD_NONE:
                                req->dns(this);
                                break;
                        }
                        it++;
                        break;
                    }
                    // no retry -> fall through
                case REQ_INFLIGHT:
                    if (req->maxbt.nextset() && req->maxbt.armed())
                    {
                        LOG_debug << "Max total time exceeded for request: " << req->posturl;
                        restag = it->first;
                        app->http_result(API_EFAILED, 0, NULL, 0);
                        delete req;
                        pendinghttp.erase(it++);
                        break;
                    }
                default:
                    it++;
                }
            }
        }

        // file attribute puts (handled sequentially as a FIFO)
        if (activefa.size())
        {
            putfa_list::iterator curfa = activefa.begin();
            while (curfa != activefa.end())
            {
                HttpReqCommandPutFA* fa = *curfa;
                m_off_t p = fa->transferred(this);
                if (fa->progressreported < p)
                {
                    httpio->updateuploadspeed(p - fa->progressreported);
                    fa->progressreported = p;
                }

                switch (fa->status)
                {
                    case REQ_SUCCESS:
                        if (fa->in.size() == sizeof(handle))
                        {
                            LOG_debug << "File attribute uploaded OK - " << fa->th;

                            // successfully wrote file attribute - store handle &
                            // remove from list
                            handle fah = MemAccess::get<handle>(fa->in.data());

                            Node* n;
                            handle h;
                            handlepair_set::iterator it;

                            // do we have a valid upload handle?
                            h = fa->th;

                            it = uhnh.lower_bound(pair<handle, handle>(h, 0));

                            if (it != uhnh.end() && it->first == h)
                            {
                                h = it->second;
                            }

                            // are we updating a live node? issue command directly.
                            // otherwise, queue for processing upon upload
                            // completion.
                            if ((n = nodebyhandle(h)) || (n = nodebyhandle(fa->th)))
                            {
                                LOG_debug << "Attaching file attribute";
                                reqs.add(new CommandAttachFA(n->nodehandle, fa->type, fah, fa->tag));
                            }
                            else
                            {
                                pendingfa[pair<handle, fatype>(fa->th, fa->type)] = pair<handle, int>(fah, fa->tag);
                                LOG_debug << "Queueing pending file attribute. Total: " << pendingfa.size();
                                checkfacompletion(fa->th);
                            }
                        }
                        else
                        {
                            LOG_warn << "Error attaching attribute";

                            // check if the failed attribute belongs to an active upload
                            for (transfer_map::iterator it = transfers[PUT].begin(); it != transfers[PUT].end(); it++)
                            {
                                Transfer *transfer = it->second;
                                if (transfer->uploadhandle == fa->th)
                                {
                                    // reduce the number of required attributes to let the upload continue
                                    transfer->minfa--;
                                    checkfacompletion(fa->th);
                                    int creqtag = reqtag;
                                    reqtag = 0;
                                    sendevent(99407,"Attribute attach failed during active upload");
                                    reqtag = creqtag;
                                    break;
                                }
                            }
                        }

                        delete fa;
                        curfa = activefa.erase(curfa);
                        LOG_debug << "Remaining file attributes: " << activefa.size() << " active, " << queuedfa.size() << " queued";
                        btpfa.reset();
                        faretrying = false;
                        break;

                    case REQ_FAILURE:
                        // repeat request with exponential backoff
                        LOG_warn << "Error setting file attribute";
                        curfa = activefa.erase(curfa);
                        fa->status = REQ_READY;
                        queuedfa.push_back(fa);
                        btpfa.backoff();
                        faretrying = true;
                        break;

                    default:
                        curfa++;
                }
            }
        }

        if (btpfa.armed())
        {
            faretrying = false;
            while (queuedfa.size() && activefa.size() < MAXPUTFA)
            {
                // dispatch most recent file attribute put
                putfa_list::iterator curfa = queuedfa.begin();
                HttpReqCommandPutFA* fa = *curfa;
                queuedfa.erase(curfa);
                activefa.push_back(fa);

                LOG_debug << "Adding file attribute to the request queue";
                fa->status = REQ_INFLIGHT;
                reqs.add(fa);
            }
        }

        if (fafcs.size())
        {
            // file attribute fetching (handled in parallel on a per-cluster basis)
            // cluster channels are never purged
            fafc_map::iterator cit;
            FileAttributeFetchChannel* fc;

            for (cit = fafcs.begin(); cit != fafcs.end(); cit++)
            {
                fc = cit->second;

                // is this request currently in flight?
                switch (fc->req.status)
                {
                    case REQ_SUCCESS:
                        if (fc->req.contenttype.find("text/html") != string::npos
                            && !memcmp(fc->req.posturl.c_str(), "http:", 5))
                        {
                            LOG_warn << "Invalid Content-Type detected downloading file attr: " << fc->req.contenttype;
                            fc->urltime = 0;
                            usehttps = true;
                            app->notify_change_to_https();

                            int creqtag = reqtag;
                            reqtag = 0;
                            sendevent(99436, "Automatic change to HTTPS");
                            reqtag = creqtag;
                        }
                        else
                        {
                            fc->parse(this, cit->first, true);
                        }

                        // notify app in case some attributes were not returned, then redispatch
                        fc->failed(this);
                        fc->req.disconnect();
                        fc->req.status = REQ_PREPARED;
                        fc->timeout.reset();
                        fc->bt.reset();
                        break;

                    case REQ_INFLIGHT:
                        if (!fc->req.httpio)
                        {
                            break;
                        }

                        if (fc->inbytes != fc->req.in.size())
                        {
                            httpio->lock();
                            fc->parse(this, cit->first, false);
                            httpio->unlock();

                            fc->timeout.backoff(100);

                            fc->inbytes = fc->req.in.size();
                        }

                        if (!fc->timeout.armed()) break;

                        LOG_warn << "Timeout getting file attr";
                        // timeout! fall through...
                    case REQ_FAILURE:
                        LOG_warn << "Error getting file attr";

                        if (fc->req.httpstatus && fc->req.contenttype.find("text/html") != string::npos
                                && !memcmp(fc->req.posturl.c_str(), "http:", 5))
                        {
                            LOG_warn << "Invalid Content-Type detected on failed file attr: " << fc->req.contenttype;
                            usehttps = true;
                            app->notify_change_to_https();

                            int creqtag = reqtag;
                            reqtag = 0;
                            sendevent(99436, "Automatic change to HTTPS");
                            reqtag = creqtag;
                        }

                        fc->failed(this);
                        fc->timeout.reset();
                        fc->bt.backoff();
                        fc->urltime = 0;
                        fc->req.disconnect();
                        fc->req.status = REQ_PREPARED;
                    default:
                        ;
                }

                if (fc->req.status != REQ_INFLIGHT && fc->bt.armed() && (fc->fafs[1].size() || fc->fafs[0].size()))
                {
                    fc->req.in.clear();

                    if (!fc->urltime || (Waiter::ds - fc->urltime) > 600)
                    {
                        // fetches pending for this unconnected channel - dispatch fresh connection
                        LOG_debug << "Getting fresh download URL";
                        fc->timeout.reset();
                        reqs.add(new CommandGetFA(this, cit->first, fc->fahref));
                        fc->req.status = REQ_INFLIGHT;
                    }
                    else
                    {
                        // redispatch cached URL if not older than one minute
                        LOG_debug << "Using cached download URL";
                        fc->dispatch(this);
                    }
                }
            }
        }

        // handle API client-server requests
        for (;;)
        {
            // do we have an API request outstanding?
            if (pendingcs)
            {
                switch (pendingcs->status)
                {
                    case REQ_READY:
                        break;

                    case REQ_INFLIGHT:
                        if (pendingcs->contentlength > 0)
                        {
                            if (fetchingnodes && fnstats.timeToFirstByte == NEVER
                                    && pendingcs->bufpos > 10)
                            {
								WAIT_CLASS::bumpds();
                                fnstats.timeToFirstByte = WAIT_CLASS::ds - fnstats.startTime;
                            }

                            if (pendingcs->bufpos > pendingcs->notifiedbufpos)
                            {
                                abortlockrequest();
                                app->request_response_progress(pendingcs->bufpos, pendingcs->contentlength);
                                pendingcs->notifiedbufpos = pendingcs->bufpos;
                            }
                        }
                        break;

                    case REQ_SUCCESS:
                        abortlockrequest();
                        app->request_response_progress(pendingcs->bufpos, -1);

                        if (pendingcs->in != "-3" && pendingcs->in != "-4")
                        {
                            if (*pendingcs->in.c_str() == '[')
                            {
                                if (fetchingnodes && fnstats.timeToFirstByte == NEVER)
                                {
									WAIT_CLASS::bumpds();
                                    fnstats.timeToFirstByte = WAIT_CLASS::ds - fnstats.startTime;
                                }

                                if (csretrying)
                                {
                                    app->notify_retry(0);
                                    csretrying = false;
                                }

                                // request succeeded, process result array
                                json.begin(pendingcs->in.c_str());
                                reqs.procresult(this);

                                WAIT_CLASS::bumpds();

                                delete pendingcs;
                                pendingcs = NULL;

                                if (sctable && pendingsccommit && !reqs.cmdspending())
                                {
                                    LOG_debug << "Executing postponed DB commit";
                                    sctable->commit();
                                    sctable->begin();
                                    app->notify_dbcommit();
                                    pendingsccommit = false;
                                }

                                // increment unique request ID
                                for (int i = sizeof reqid; i--; )
                                {
                                    if (reqid[i]++ < 'z')
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        reqid[i] = 'a';
                                    }
                                }
                            }
                            else
                            {
                                // request failed
                                error e = (error)atoi(pendingcs->in.c_str());

                                if (!e)
                                {
                                    e = API_EINTERNAL;
                                }

                                app->request_error(e);
                                delete pendingcs;
                                pendingcs = NULL;
                                csretrying = false;
                                break;
                            }

                            btcs.reset();
                            break;
                        }
                        else
                        {
                            if (fetchingnodes)
                            {
                                fnstats.eAgainCount++;
                            }
                        }

                    // fall through
                    case REQ_FAILURE:
                        if (fetchingnodes && pendingcs->httpstatus != 200)
                        {
                            if (pendingcs->httpstatus == 500)
                            {
                                fnstats.e500Count++;
                            }
                            else
                            {
                                fnstats.eOthersCount++;
                            }
                        }

                        abortlockrequest();
                        if (pendingcs->sslcheckfailed)
                        {
                            sslfakeissuer = pendingcs->sslfakeissuer;
                            app->request_error(API_ESSL);
                            sslfakeissuer.clear();

                            if (!retryessl)
                            {
                                delete pendingcs;
                                pendingcs = NULL;
                                csretrying = false;
                                break;
                            }
                        }

                        // failure, repeat with capped exponential backoff
                        app->request_response_progress(pendingcs->bufpos, -1);

                        delete pendingcs;
                        pendingcs = NULL;

                        btcs.backoff();
                        app->notify_retry(btcs.retryin());
                        csretrying = true;

                    default:
                        ;
                }

                if (pendingcs)
                {
                    break;
                }
            }

            if (btcs.armed())
            {
                if (btcs.nextset())
                {
                    reqs.nextRequest();
                }

                if (reqs.cmdspending())
                {
                    pendingcs = new HttpReq();
                    pendingcs->protect = true;

                    reqs.get(pendingcs->out);

                    pendingcs->posturl = APIURL;

                    pendingcs->posturl.append("cs?id=");
                    pendingcs->posturl.append(reqid, sizeof reqid);
                    pendingcs->posturl.append(auth);
                    pendingcs->posturl.append(appkey);
                    if (lang.size())
                    {
                        pendingcs->posturl.append(lang);
                    }
                    pendingcs->type = REQ_JSON;

                    pendingcs->post(this);

                    reqs.nextRequest();
                    continue;
                }
                else
                {
                    btcs.reset();
                }
            }
            break;
        }

        // handle API server-client requests
        if (!jsonsc.pos && pendingsc)
        {
            if (scnotifyurl.size())
            {
                // pendingsc is a scnotifyurl connection
                if (pendingsc->status == REQ_SUCCESS || pendingsc->status == REQ_FAILURE)
                {
                    delete pendingsc;
                    pendingsc = NULL;

                    scnotifyurl.clear();
                }
            }
            else
            {
                // pendingsc is a server-client API request
                switch (pendingsc->status)
                {
                    case REQ_SUCCESS:
                        if (*pendingsc->in.c_str() == '{')
                        {
                            jsonsc.begin(pendingsc->in.c_str());
                            jsonsc.enterobject();
                            break;
                        }
                        else
                        {
                            error e = (error)atoi(pendingsc->in.c_str());
                            if (e == API_ESID)
                            {
                                app->request_error(API_ESID);
                                *scsn = 0;
                            }
                            else if (e == API_ETOOMANY)
                            {
                                LOG_warn << "Too many pending updates - reloading local state";
                                int creqtag = reqtag;
                                reqtag = fetchnodestag; // associate with ongoing request, if any
                                fetchingnodes = false;
                                fetchnodestag = 0;
                                fetchnodes(true);
                                reqtag = creqtag;
                            }
                            else if (e == API_EAGAIN || e == API_ERATELIMIT)
                            {
                                if (!statecurrent)
                                {
                                    fnstats.eAgainCount++;
                                }
                            }
                        }
                        // fall through
                    case REQ_FAILURE:
                        if (pendingsc && !statecurrent && pendingsc->httpstatus != 200)
                        {
                            if (pendingsc->httpstatus == 500)
                            {
                                fnstats.e500Count++;
                            }
                            else
                            {
                                fnstats.eOthersCount++;
                            }
                        }

                        if (pendingsc && pendingsc->sslcheckfailed)
                        {
                            sslfakeissuer = pendingsc->sslfakeissuer;
                            app->request_error(API_ESSL);
                            sslfakeissuer.clear();

                            if (!retryessl)
                            {
                                *scsn = 0;
                            }
                        }

                        // failure, repeat with capped exponential backoff
                        delete pendingsc;
                        pendingsc = NULL;

                        btsc.backoff();

                    default:
                        ;
                }
            }
        }

#ifdef ENABLE_SYNC
        if (syncactivity)
        {
            syncops = true;
        }
        syncactivity = false;

        // do not process the SC result until all preconfigured syncs are up and running
        // except if SC packets are required to complete a fetchnodes
        if (!scpaused && jsonsc.pos && (syncsup || !statecurrent) && !syncdownrequired && !syncdownretry)
#else
        if (!scpaused && jsonsc.pos)
#endif
        {
            // FIXME: reload in case of bad JSON
            if (procsc())
            {
                // completed - initiate next SC request
                delete pendingsc;
                pendingsc = NULL;

                btsc.reset();
            }
#ifdef ENABLE_SYNC
            else
            {
                // remote changes require immediate attention of syncdown()
                syncdownrequired = true;
                syncactivity = true;
            }
#endif
        }

        if (!pendingsc && *scsn && btsc.armed())
        {
            pendingsc = new HttpReq();

            if (scnotifyurl.size())
            {
                pendingsc->posturl = scnotifyurl;
            }
            else
            {
                pendingsc->protect = true;
                pendingsc->posturl = APIURL;
                pendingsc->posturl.append("sc?sn=");
                pendingsc->posturl.append(scsn);
                pendingsc->posturl.append(auth);

                if (usehttps)
                {
                    pendingsc->posturl.append("&ssl=1");
                }
            }

            pendingsc->type = REQ_JSON;
            pendingsc->post(this);

            jsonsc.pos = NULL;
        }

        if (badhostcs)
        {
            if (badhostcs->status == REQ_SUCCESS)
            {
                LOG_debug << "Successful badhost report";
                btbadhost.reset();
                delete badhostcs;
                badhostcs = NULL;
            }
            else if(badhostcs->status == REQ_FAILURE)
            {
                LOG_debug << "Failed badhost report. Retrying...";
                btbadhost.backoff();
                badhosts = badhostcs->outbuf;
                delete badhostcs;
                badhostcs = NULL;
            }
        }

        if (workinglockcs)
        {
            if (workinglockcs->status == REQ_SUCCESS)
            {
                LOG_debug << "Successful lock request";
                btworkinglock.reset();

                if (workinglockcs->in == "1")
                {
                    LOG_warn << "Timeout (server idle)";
                    disconnecttimestamp = Waiter::ds + HttpIO::CONNECTTIMEOUT;
                }
                else if (workinglockcs->in == "0")
                {
                    int creqtag = reqtag;
                    reqtag = 0;
                    sendevent(99425, "Timeout (server busy)");
                    reqtag = creqtag;

                    pendingcs->lastdata = Waiter::ds;
                }
                else
                {
                    LOG_err << "Error in lock request: " << workinglockcs->in;
                }

                delete workinglockcs;
                workinglockcs = NULL;
                requestLock = false;
            }
            else if (workinglockcs->status == REQ_FAILURE
                     || (workinglockcs->status == REQ_INFLIGHT && Waiter::ds >= workinglockcs->lastdata + HttpIO::REQUESTTIMEOUT))
            {
                LOG_warn << "Failed lock request. Retrying...";
                btworkinglock.backoff();
                delete workinglockcs;
                workinglockcs = NULL;
            }
        }

        // fill transfer slots from the queue
        dispatchmore(PUT);
        dispatchmore(GET);

#ifndef EMSCRIPTEN
        assert(!asyncfopens);
#endif

        slotit = tslots.begin();

        // handle active unpaused transfers
        while (slotit != tslots.end())
        {
            transferslot_list::iterator it = slotit;

            slotit++;

            if (!xferpaused[(*it)->transfer->type] && (!(*it)->retrying || (*it)->retrybt.armed()))
            {
                (*it)->doio(this);
            }
        }

#ifdef ENABLE_SYNC
        // verify filesystem fingerprints, disable deviating syncs
        // (this covers mountovers, some device removals and some failures)
        sync_list::iterator it;
        for (it = syncs.begin(); it != syncs.end(); it++)
        {
            if ((*it)->fsfp)
            {
                fsfp_t current = (*it)->dirnotify->fsfingerprint();
                if ((*it)->fsfp != current)
                {
                    LOG_err << "Local fingerprint mismatch. Previous: " << (*it)->fsfp
                            << "  Current: " << current;
                    (*it)->errorcode = API_EFAILED;
                    (*it)->changestate(SYNC_FAILED);
                }
            }
        }

        if (!syncsup)
        {
            // set syncsup if there are no initializing syncs
            // this will allow incoming server-client commands to trigger the filesystem
            // actions that have occurred while the sync app was not running
            for (it = syncs.begin(); it != syncs.end(); it++)
            {
                if ((*it)->state == SYNC_INITIALSCAN)
                {
                    break;
                }
            }

            if (it == syncs.end())
            {
                syncsup = true;
                syncactivity = true;
                syncdownrequired = true;
            }
        }

        // process active syncs
        // sync timer: full rescan in case of filesystem notification failures
        if (syncscanfailed && syncscanbt.armed())
        {
            syncscanfailed = false;
            syncops = true;
        }

        // sync timer: file change upload delay timeouts (Nagle algorithm)
        if (syncnagleretry && syncnaglebt.armed())
        {
            syncnagleretry = false;
            syncops = true;
        }

        if (syncextraretry && syncextrabt.armed())
        {
            syncextraretry = false;
            syncops = true;
        }

        // sync timer: read lock retry
        if (syncfslockretry && syncfslockretrybt.armed())
        {
            syncfslockretrybt.backoff(Sync::SCANNING_DELAY_DS);
        }

        // halt all syncing while the local filesystem is pending a lock-blocked operation
        // or while we are fetching nodes
        // FIXME: indicate by callback
        if (!syncdownretry && !syncadding && statecurrent && !syncdownrequired && !fetchingnodes)
        {
            // process active syncs, stop doing so while transient local fs ops are pending
            if (syncs.size() || syncactivity)
            {
                bool prevpending = false;
                for (int q = syncfslockretry ? DirNotify::RETRY : DirNotify::DIREVENTS; q >= DirNotify::DIREVENTS; q--)
                {
                    for (it = syncs.begin(); it != syncs.end(); )
                    {
                        Sync* sync = *it++;
                        prevpending |= sync->dirnotify->notifyq[q].size();
                        if (prevpending)
                        {
                            break;
                        }
                    }
                    if (prevpending)
                    {
                        break;
                    }
                }

                dstime nds = NEVER;
                dstime mindelay = NEVER;
                for (it = syncs.begin(); it != syncs.end(); )
                {
                    Sync* sync = *it++;
                    if (sync->isnetwork && (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN))
                    {
                        while (sync->dirnotify->notifyq[DirNotify::EXTRA].size())
                        {
                            dstime dsmin = Waiter::ds - Sync::EXTRA_SCANNING_DELAY_DS;
                            Notification &notification = sync->dirnotify->notifyq[DirNotify::EXTRA].front();
                            if (notification.timestamp <= dsmin)
                            {
                                LOG_debug << "Processing extra fs notification";
                                sync->dirnotify->notify(DirNotify::DIREVENTS, notification.localnode,
                                                        notification.path.data(), notification.path.size());
                                sync->dirnotify->notifyq[DirNotify::EXTRA].pop_front();
                            }
                            else
                            {
                                dstime delay = (notification.timestamp - dsmin) + 1;
                                if (delay < mindelay)
                                {
                                    mindelay = delay;
                                }
                                break;
                            }
                        }
                    }
                }
                if (EVER(mindelay))
                {
                    syncextrabt.backoff(mindelay);
                    syncextraretry = true;
                }
                else
                {
                    syncextraretry = false;
                }

                for (int q = syncfslockretry ? DirNotify::RETRY : DirNotify::DIREVENTS; q >= DirNotify::DIREVENTS; q--)
                {
                    if (!syncfsopsfailed)
                    {
                        syncfslockretry = false;

                        // not retrying local operations: process pending notifyqs
                        for (it = syncs.begin(); it != syncs.end(); )
                        {
                            Sync* sync = *it++;

                            if (sync->state == SYNC_CANCELED || sync->state == SYNC_FAILED)
                            {
                                delete sync;
                                continue;
                            }
                            else if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
                            {
                                // process items from the notifyq until depleted
                                if (sync->dirnotify->notifyq[q].size())
                                {
                                    dstime dsretry;

                                    syncops = true;

                                    if ((dsretry = sync->procscanq(q)))
                                    {
                                        // we resume processing after dsretry has elapsed
                                        // (to avoid open-after-creation races with e.g. MS Office)
                                        if (EVER(dsretry))
                                        {
                                            if (!syncnagleretry || (dsretry + 1) < syncnaglebt.backoffdelta())
                                            {
                                                syncnaglebt.backoff(dsretry + 1);
                                            }

                                            syncnagleretry = true;
                                        }
                                        else
                                        {
                                            if (syncnagleretry)
                                            {
                                                syncnaglebt.arm();
                                            }
                                            syncactivity = true;
                                        }

                                        if (syncadding)
                                        {
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        LOG_debug << "Pending MEGA nodes: " << synccreate.size();
                                        if (!syncadding)
                                        {
                                            LOG_debug << "Running syncup to create missing folders";
                                            syncup(&sync->localroot, &nds);
                                            sync->cachenodes();
                                        }

                                        // we interrupt processing the notifyq if the completion
                                        // of a node creation is required to continue
                                        break;
                                    }
                                }

                                if (sync->state == SYNC_INITIALSCAN && q == DirNotify::DIREVENTS && !sync->dirnotify->notifyq[q].size())
                                {
                                    sync->changestate(SYNC_ACTIVE);

                                    // scan for items that were deleted while the sync was stopped
                                    // FIXME: defer this until RETRY queue is processed
                                    sync->scanseqno++;
                                    sync->deletemissing(&sync->localroot);
                                }
                            }
                        }

                        if (syncadding)
                        {
                            break;
                        }
                    }
                }

                unsigned totalpending = 0;
                unsigned scanningpending = 0;
                for (int q = DirNotify::RETRY; q >= DirNotify::DIREVENTS; q--)
                {
                    for (it = syncs.begin(); it != syncs.end(); )
                    {
                        Sync* sync = *it++;
                        sync->cachenodes();

                        totalpending += sync->dirnotify->notifyq[q].size();
                        if (q == DirNotify::DIREVENTS)
                        {
                            scanningpending += sync->dirnotify->notifyq[q].size();
                        }
                        else if (!syncfslockretry && sync->dirnotify->notifyq[DirNotify::RETRY].size())
                        {
                            syncfslockretrybt.backoff(Sync::SCANNING_DELAY_DS);
                            syncfslockretry = true;
                        }
                    }
                }

                if (!syncfslockretry && !syncfsopsfailed)
                {
                    blockedfile.clear();
                }

                if (syncadding)
                {
                    // do not continue processing syncs while adding nodes
                    // just go to evaluate the main do-while loop
                    notifypurge();
                    continue;
                }

                // delete files that were overwritten by folders in checkpath()
                execsyncdeletions();  

                if (synccreate.size())
                {
                    syncupdate();
                }

                // notify the app of the length of the pending scan queue
                if (scanningpending < 4)
                {
                    if (syncscanstate)
                    {
                        LOG_debug << "Scanning finished";
                        app->syncupdate_scanning(false);
                        syncscanstate = false;
                    }
                }
                else if (scanningpending > 10)
                {
                    if (!syncscanstate)
                    {
                        LOG_debug << "Scanning started";
                        app->syncupdate_scanning(true);
                        syncscanstate = true;
                    }
                }

                if (prevpending && !totalpending)
                {
                    LOG_debug << "Scan queue processed, triggering a scan";
                    syncdownrequired = true;
                }

                notifypurge();

                if (!syncadding && (syncactivity || syncops))
                {
                    for (it = syncs.begin(); it != syncs.end(); it++)
                    {
                        // make sure that the remote synced folder still exists
                        if (!(*it)->localroot.node)
                        {
                            LOG_err << "The remote root node doesn't exist";
                            (*it)->errorcode = API_ENOENT;
                            (*it)->changestate(SYNC_FAILED);
                        }
                    }

                    // perform aggregate ops that require all scanqs to be fully processed
                    for (it = syncs.begin(); it != syncs.end(); it++)
                    {
                        if ((*it)->dirnotify->notifyq[DirNotify::DIREVENTS].size()
                          || (*it)->dirnotify->notifyq[DirNotify::RETRY].size())
                        {
                            if (!syncnagleretry && !syncfslockretry)
                            {
                                syncactivity = true;
                            }

                            break;
                        }
                    }

                    if (it == syncs.end())
                    {
                        // execution of notified deletions - these are held in localsyncnotseen and
                        // kept pending until all creations (that might reference them for the purpose of
                        // copying) have completed and all notification queues have run empty (to ensure
                        // that moves are not executed as deletions+additions.
                        if (localsyncnotseen.size() && !synccreate.size())
                        {
                            // ... execute all pending deletions
                            while (localsyncnotseen.size())
                            {
                                delete *localsyncnotseen.begin();
                            }
                        }

                        // process filesystem notifications for active syncs unless we
                        // are retrying local fs writes
                        if (!syncfsopsfailed)
                        {
                            LOG_verbose << "syncops: " << syncactivity << syncnagleretry
                                        << syncfslockretry << synccreate.size();
                            syncops = false;

                            // FIXME: only syncup for subtrees that were actually
                            // updated to reduce CPU load
                            for (it = syncs.begin(); it != syncs.end(); it++)
                            {
                                if (((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN)
                                 && !syncadding && syncuprequired && !syncnagleretry)
                                {
                                    LOG_debug << "Running syncup on demand";
                                    syncuprequired |= !syncup(&(*it)->localroot, &nds);
                                    (*it)->cachenodes();
                                }
                            }

                            if (EVER(nds))
                            {
                                if (!syncnagleretry || (nds - Waiter::ds) < syncnaglebt.backoffdelta())
                                {
                                    syncnaglebt.backoff(nds - Waiter::ds);
                                }

                                syncnagleretry = true;
                                syncuprequired = true;
                            }

                            // delete files that were overwritten by folders in syncup()
                            execsyncdeletions();  

                            if (synccreate.size())
                            {
                                syncupdate();
                            }

                            unsigned totalnodes = 0;

                            // we have no sync-related operations pending - trigger processing if at least one
                            // filesystem item is notified or initiate a full rescan if there has been
                            // an event notification failure (or event notification is unavailable)
                            bool scanfailed = false;
                            for (it = syncs.begin(); it != syncs.end(); it++)
                            {
                                Sync* sync = *it;

                                totalnodes += sync->localnodes[FILENODE] + sync->localnodes[FOLDERNODE];

                                if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
                                {
                                    if (sync->dirnotify->notifyq[DirNotify::DIREVENTS].size()
                                     || sync->dirnotify->notifyq[DirNotify::RETRY].size())
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        if (sync->fullscan)
                                        {
                                            // recursively delete all LocalNodes that were deleted (not moved or renamed!)
                                            sync->deletemissing(&sync->localroot);
                                            sync->cachenodes();
                                        }

                                        // if the directory events notification subsystem is permanently unavailable or
                                        // has signaled a temporary error, initiate a full rescan
                                        if (sync->state == SYNC_ACTIVE)
                                        {
                                            sync->fullscan = false;

                                            if (syncscanbt.armed()
                                                    && (sync->dirnotify->failed || fsaccess->notifyfailed
                                                        || sync->dirnotify->error || fsaccess->notifyerr))
                                            {
                                                LOG_warn << "Sync scan failed";
                                                syncscanfailed = true;
                                                scanfailed = true;

                                                sync->scan(&sync->localroot.localname, NULL);
                                                sync->dirnotify->error = false;
                                                sync->fullscan = true;
                                                sync->scanseqno++;
                                            }
                                        }
                                    }
                                }
                            }

                            if (scanfailed)
                            {
                                fsaccess->notifyerr = false;
                                syncscanbt.backoff(50 + totalnodes / 128);
                            }

                            // clear pending global notification error flag if all syncs were marked
                            // to be rescanned
                            if (fsaccess->notifyerr && it == syncs.end())
                            {
                                fsaccess->notifyerr = false;
                            }

                            execsyncdeletions();  
                        }
                    }
                }
            }
        }
        else
        {
            notifypurge();

            // sync timer: retry syncdown() ops in case of local filesystem lock clashes
            if (syncdownretry && syncdownbt.armed())
            {
                syncdownretry = false;
                syncdownrequired = true;
            }

            if (syncdownrequired)
            {
                syncdownrequired = false;
                if (!fetchingnodes)
                {
                    LOG_verbose << "Running syncdown";
                    bool success = true;
                    for (it = syncs.begin(); it != syncs.end(); it++)
                    {
                        // make sure that the remote synced folder still exists
                        if (!(*it)->localroot.node)
                        {
                            LOG_err << "The remote root node doesn't exist";
                            (*it)->errorcode = API_ENOENT;
                            (*it)->changestate(SYNC_FAILED);
                        }
                        else
                        {
                            string localpath = (*it)->localroot.localname;
                            if ((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN)
                            {
                                LOG_debug << "Running syncdown on demand";
                                if (!syncdown(&(*it)->localroot, &localpath, true))
                                {
                                    // a local filesystem item was locked - schedule periodic retry
                                    // and force a full rescan afterwards as the local item may
                                    // be subject to changes that are notified with obsolete paths
                                    success = false;
                                    (*it)->dirnotify->error = true;
                                }

                                (*it)->cachenodes();
                            }
                        }
                    }

                    // notify the app if a lock is being retried
                    if (success)
                    {
                        syncuprequired = true;
                        syncdownretry = false;
                        syncactivity = true;

                        if (syncfsopsfailed)
                        {
                            syncfsopsfailed = false;
                            blockedfile.clear();
                            app->syncupdate_local_lockretry(false);
                        }
                    }
                    else
                    {
                        if (!syncfsopsfailed)
                        {
                            syncfsopsfailed = true;
                            app->syncupdate_local_lockretry(true);
                        }

                        syncdownretry = true;
                        syncdownbt.backoff(50);
                    }
                }
                else
                {
                    LOG_err << "Syncdown requested while fetchingnodes is set";
                }
            }
        }
#endif

        notifypurge();

        if (!badhostcs && badhosts.size() && btbadhost.armed())
        {
            // report hosts affected by failed requests
            LOG_debug << "Sending badhost report: " << badhosts;
            badhostcs = new HttpReq();
            badhostcs->posturl = APIURL;
            badhostcs->posturl.append("pf?h");
            badhostcs->outbuf = badhosts;
            badhostcs->type = REQ_JSON;
            badhostcs->post(this);
            badhosts.clear();
        }

        if (!workinglockcs && requestLock && btworkinglock.armed())
        {
            LOG_debug << "Sending lock request";
            workinglockcs = new HttpReq();
            workinglockcs->posturl = APIURL;
            workinglockcs->posturl.append("cs?");
            workinglockcs->posturl.append(auth);
            workinglockcs->posturl.append("&wlt=1");
            workinglockcs->type = REQ_JSON;
            workinglockcs->post(this);
        }

        httpio->updatedownloadspeed();
        httpio->updateuploadspeed();
    } while (httpio->doio() || execdirectreads() || (!pendingcs && reqs.cmdspending() && btcs.armed()) || looprequested);
}

// get next event time from all subsystems, then invoke the waiter if needed
// returns true if an engine-relevant event has occurred, false otherwise
int MegaClient::wait()
{
    int r = preparewait();
    if (r)
    {
        return r;
    }
    r |= dowait();
    r |= checkevents();
    return r;
}

int MegaClient::preparewait()
{
    dstime nds;

    // get current dstime and clear wait events
    WAIT_CLASS::bumpds();

#ifdef ENABLE_SYNC
    // sync directory scans in progress or still processing sc packet without having
    // encountered a locally locked item? don't wait.
    if (syncactivity || syncdownrequired || (!scpaused && jsonsc.pos && (syncsup || !statecurrent) && !syncdownretry))
    {
        nds = Waiter::ds;
    }
    else
#endif
    {
        // next retry of a failed transfer
        nds = NEVER;

        if (httpio->success && chunkfailed)
        {
            // there is a pending transfer retry, don't wait
            nds = Waiter::ds;
        }

        nexttransferretry(PUT, &nds);
        nexttransferretry(GET, &nds);

        // retry transferslots
        for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); it++)
        {
            if (!(*it)->retrybt.armed())
            {
                (*it)->retrybt.update(&nds);
            }
        }

        for (pendinghttp_map::iterator it = pendinghttp.begin(); it != pendinghttp.end(); it++)
        {
            if (it->second->isbtactive)
            {
                it->second->bt.update(&nds);
            }

            if (it->second->maxbt.nextset())
            {
                it->second->maxbt.update(&nds);
            }
        }

        // retry failed client-server requests
        if (!pendingcs)
        {
            btcs.update(&nds);
        }

        // retry failed server-client requests
        if (!pendingsc && *scsn)
        {
            btsc.update(&nds);
        }

        // retry failed badhost requests
        if (!badhostcs && badhosts.size())
        {
            btbadhost.update(&nds);
        }

        if (!workinglockcs && requestLock)
        {
            btworkinglock.update(&nds);
        }

        // retry failed file attribute puts
        if (faretrying)
        {
            btpfa.update(&nds);
        }

        // retry failed file attribute gets
        for (fafc_map::iterator cit = fafcs.begin(); cit != fafcs.end(); cit++)
        {
            if (cit->second->req.status == REQ_INFLIGHT)
            {
                cit->second->timeout.update(&nds);
            }
            else if (cit->second->fafs[1].size() || cit->second->fafs[0].size())
            {
                cit->second->bt.update(&nds);
            }
        }

        // next pending pread event
        if (!dsdrns.empty())
        {
            if (dsdrns.begin()->first < nds)
            {
                if (dsdrns.begin()->first <= Waiter::ds)
                {
                    nds = Waiter::ds;
                }
                else
                {
                    nds = dsdrns.begin()->first;
                }
            }
        }

#ifdef ENABLE_SYNC
        // sync rescan
        if (syncscanfailed)
        {
            syncscanbt.update(&nds);
        }

        // retrying of transient failed read ops
        if (syncfslockretry && !syncdownretry && !syncadding
                && statecurrent && !syncdownrequired && !syncfsopsfailed)
        {
            LOG_debug << "Waiting for a temporary error checking filesystem notification";
            syncfslockretrybt.update(&nds);
        }

        // retrying of transiently failed syncdown() updates
        if (syncdownretry)
        {
            syncdownbt.update(&nds);
        }

        // triggering of Nagle-delayed sync PUTs
        if (syncnagleretry)
        {
            syncnaglebt.update(&nds);
        }

        if (syncextraretry)
        {
            syncextrabt.update(&nds);
        }
#endif

        // detect stuck network
        if (EVER(httpio->lastdata) && !pendingcs)
        {
            dstime timeout = httpio->lastdata + HttpIO::NETWORKTIMEOUT;

            if (timeout > Waiter::ds && timeout < nds)
            {
                nds = timeout;
            }
            else if (timeout <= Waiter::ds)
            {
                nds = 0;
            }
        }

        if (pendingcs && EVER(pendingcs->lastdata))
        {
            if (EVER(disconnecttimestamp))
            {
                if (disconnecttimestamp > Waiter::ds && disconnecttimestamp < nds)
                {
                    nds = disconnecttimestamp;
                }
                else if (disconnecttimestamp <= Waiter::ds)
                {
                    nds = 0;
                }
            }
            else if (!requestLock && !fetchingnodes)
            {
                dstime timeout = pendingcs->lastdata + HttpIO::REQUESTTIMEOUT;
                if (timeout > Waiter::ds && timeout < nds)
                {
                    nds = timeout;
                }
                else if (timeout <= Waiter::ds)
                {
                    nds = 0;
                }
            }
            else if (workinglockcs && EVER(workinglockcs->lastdata)
                     && workinglockcs->status == REQ_INFLIGHT)
            {
                dstime timeout = workinglockcs->lastdata + HttpIO::REQUESTTIMEOUT;
                if (timeout > Waiter::ds && timeout < nds)
                {
                    nds = timeout;
                }
                else if (timeout <= Waiter::ds)
                {
                    nds = 0;
                }
            }
        }
    }

    // immediate action required?
    if (!nds)
    {
        return Waiter::NEEDEXEC;
    }

    // nds is either MAX_INT (== no pending events) or > Waiter::ds
    if (EVER(nds))
    {
        nds -= Waiter::ds;
    }

    waiter->init(nds);

    // set subsystem wakeup criteria (WinWaiter assumes httpio to be set first!)
    waiter->wakeupby(httpio, Waiter::NEEDEXEC);
    waiter->wakeupby(fsaccess, Waiter::NEEDEXEC);

    return 0;
}

int MegaClient::dowait()
{
    return waiter->wait();
}

int MegaClient::checkevents()
{
    int r =  httpio->checkevents(waiter);
    r |= fsaccess->checkevents(waiter);
    return r;
}

// reset all backoff timers and transfer retry counters
bool MegaClient::abortbackoff(bool includexfers)
{
    bool r = false;

    WAIT_CLASS::bumpds();

    if (includexfers)
    {
        overquotauntil = 0;
        for (int d = GET; d == GET || d == PUT; d += PUT - GET)
        {
            for (transfer_map::iterator it = transfers[d].begin(); it != transfers[d].end(); it++)
            {
                if (it->second->bt.arm())
                {
                    r = true;
                }

                if (it->second->slot && it->second->slot->retrying)
                {
                    if (it->second->slot->retrybt.arm())
                    {
                        r = true;
                    }
                }
            }
        }

        for (handledrn_map::iterator it = hdrns.begin(); it != hdrns.end();)
        {
            (it++)->second->retry(API_OK);
        }
    }

    for (pendinghttp_map::iterator it = pendinghttp.begin(); it != pendinghttp.end(); it++)
    {
        if (it->second->bt.arm())
        {
            r = true;
        }
    }

    if (btcs.arm())
    {
        r = true;
    }

    if (btbadhost.arm())
    {
        r = true;
    }

    if (btworkinglock.arm())
    {
        r = true;
    }

    if (!pendingsc && btsc.arm())
    {
        r = true;
    }

    if (activefa.size() < MAXPUTFA && btpfa.arm())
    {
        r = true;
    }

    for (fafc_map::iterator it = fafcs.begin(); it != fafcs.end(); it++)
    {
        if (it->second->req.status != REQ_INFLIGHT && it->second->bt.arm())
        {
            r = true;
        }
    }

    return r;
}

// this will dispatch the next queued transfer unless one is already in
// progress and force isn't set
// returns true if dispatch occurred, false otherwise
bool MegaClient::dispatch(direction_t d)
{
    // do we have any transfer slots available?
    if (!slotavail())
    {
        LOG_verbose << "No slots available";
        return false;
    }

    // file attribute jam? halt uploads.
    if (d == PUT && queuedfa.size() > MAXQUEUEDFA)
    {
        LOG_warn << "Attribute queue full: " << queuedfa.size();
        return false;
    }

    Transfer *nexttransfer;
    TransferSlot *ts = NULL;

    for (;;)
    {
        nexttransfer = transferlist.nexttransfer(d);

        // no inactive transfers ready?
        if (!nexttransfer)
        {
            return false;
        }

        if (!nexttransfer->localfilename.size())
        {
            // this is a fresh transfer rather than the resumption of a partly
            // completed and deferred one
            if (d == PUT)
            {
                // generate fresh random encryption key/CTR IV for this file
                byte keyctriv[SymmCipher::KEYLENGTH + sizeof(int64_t)];
                PrnGen::genblock(keyctriv, sizeof keyctriv);
                memcpy(nexttransfer->transferkey, keyctriv, SymmCipher::KEYLENGTH);
                nexttransfer->ctriv = MemAccess::get<uint64_t>((const char*)keyctriv + SymmCipher::KEYLENGTH);
            }
            else
            {
                // set up keys for the decryption of this file (k == NULL => private node)
                Node* n;
                const byte* k = NULL;

                // locate suitable template file
                for (file_list::iterator it = nexttransfer->files.begin(); it != nexttransfer->files.end(); it++)
                {
                    if ((*it)->hprivate && !(*it)->hforeign)
                    {
                        // the size field must be valid right away for
                        // MegaClient::moretransfers()
                        if ((n = nodebyhandle((*it)->h)) && n->type == FILENODE)
                        {
                            k = (const byte*)n->nodekey.data();
                            nexttransfer->size = n->size;
                        }
                    }
                    else
                    {
                        k = (*it)->filekey;
                        nexttransfer->size = (*it)->size;
                    }

                    if (k)
                    {
                        memcpy(nexttransfer->transferkey, k, SymmCipher::KEYLENGTH);
                        SymmCipher::xorblock(k + SymmCipher::KEYLENGTH, nexttransfer->transferkey);
                        nexttransfer->ctriv = MemAccess::get<int64_t>((const char*)k + SymmCipher::KEYLENGTH);
                        nexttransfer->metamac = MemAccess::get<int64_t>((const char*)k + SymmCipher::KEYLENGTH + sizeof(int64_t));
                        break;
                    }
                }

                if (!k)
                {
                    return false;
                }
            }

            nexttransfer->localfilename.clear();

            // set file localnames (ultimate target) and one transfer-wide temp
            // localname
            for (file_list::iterator it = nexttransfer->files.begin();
                 !nexttransfer->localfilename.size() && it != nexttransfer->files.end(); it++)
            {
                (*it)->prepare();
            }

            // app-side transfer preparations (populate localname, create thumbnail...)
            app->transfer_prepare(nexttransfer);
        }

        bool openok;
        bool openfinished = false;

        // verify that a local path was given and start/resume transfer
        if (nexttransfer->localfilename.size())
        {
            if (!nexttransfer->slot)
            {
                // allocate transfer slot
                ts = new TransferSlot(nexttransfer);
            }
            else
            {
                ts = nexttransfer->slot;
            }

            if (ts->fa->asyncavailable())
            {
                if (!nexttransfer->asyncopencontext)
                {
                    LOG_debug << "Starting async open";

                    // try to open file (PUT transfers: open in nonblocking mode)
                    nexttransfer->asyncopencontext = (d == PUT)
                        ? ts->fa->asyncfopen(&nexttransfer->localfilename)
                        : ts->fa->asyncfopen(&nexttransfer->localfilename, false, true, nexttransfer->size);
                    asyncfopens++;
                }

                if (nexttransfer->asyncopencontext->finished)
                {
                    LOG_debug << "Async open finished";
                    openok = !nexttransfer->asyncopencontext->failed;
                    openfinished = true;
                    delete nexttransfer->asyncopencontext;
                    nexttransfer->asyncopencontext = NULL;
                    asyncfopens--;
                }

                assert(!asyncfopens);
                //FIXME: Improve the management of asynchronous fopen when they can
                //be really asynchronous. All transfers could open its file in this
                //stage (not good) and, if we limit it, the transfer queue could hang because
                //it's full of transfers in that state. Transfer moves also complicates
                //the management because transfers that haven't been opened could be
                //placed over transfers that are already being opened.
                //Probably, the best approach is to add the slot of these transfers to
                //the queue and ensure that all operations (transfer moves, pauses)
                //are correctly cancelled when needed
            }
            else
            {
                // try to open file (PUT transfers: open in nonblocking mode)
                openok = (d == PUT)
                        ? ts->fa->fopen(&nexttransfer->localfilename)
                        : ts->fa->fopen(&nexttransfer->localfilename, false, true);
                openfinished = true;
            }

            if (openfinished && openok)
            {
                handle h = UNDEF;
                bool hprivate = true;
                const char *privauth = NULL;
                const char *pubauth = NULL;

                nexttransfer->pos = 0;
                nexttransfer->progresscompleted = 0;

                if (d == GET || nexttransfer->cachedtempurl.size())
                {
                    m_off_t p = 0;

                    // resume at the end of the last contiguous completed block
                    for (chunkmac_map::iterator it = nexttransfer->chunkmacs.begin();
                         it != nexttransfer->chunkmacs.end(); it++)
                    {
                        m_off_t chunkceil = ChunkedHash::chunkceil(it->first, nexttransfer->size);

                        if (nexttransfer->pos == it->first && it->second.finished)
                        {
                            nexttransfer->pos = chunkceil;
                            nexttransfer->progresscompleted = chunkceil;
                        }
                        else if (it->second.finished)
                        {
                            m_off_t chunksize = chunkceil - ChunkedHash::chunkfloor(it->first);
                            nexttransfer->progresscompleted += chunksize;
                        }
                        else
                        {
                            nexttransfer->progresscompleted += it->second.offset;
                            p += it->second.offset;
                        }
                    }

                    if (nexttransfer->progresscompleted > nexttransfer->size)
                    {
                        LOG_err << "Invalid transfer progress!";
                        nexttransfer->pos = nexttransfer->size;
                        nexttransfer->progresscompleted = nexttransfer->size;
                    }

                    LOG_debug << "Resuming transfer at " << nexttransfer->pos
                              << " Completed: " << nexttransfer->progresscompleted
                              << " Partial: " << p << " Size: " << nexttransfer->size
                              << " ultoken: " << (nexttransfer->ultoken != NULL);
                }
                else
                {
                    nexttransfer->chunkmacs.clear();
                }

                ts->progressreported = nexttransfer->progresscompleted;

                if (d == PUT)
                {
                    if (ts->fa->mtime != nexttransfer->mtime || ts->fa->size != nexttransfer->size)
                    {
                        LOG_warn << "Modification detected starting upload";
                        nexttransfer->failed(API_EREAD);
                        continue;
                    }

                    // create thumbnail/preview imagery, if applicable (FIXME: do not re-create upon restart)
                    if (nexttransfer->localfilename.size() && !nexttransfer->uploadhandle)
                    {
                        nexttransfer->uploadhandle = getuploadhandle();

                        if (gfx && gfx->isgfx(&nexttransfer->localfilename))
                        {
                            // we want all imagery to be safely tucked away before completing the upload, so we bump minfa
                            nexttransfer->minfa += gfx->gendimensionsputfa(ts->fa, &nexttransfer->localfilename, nexttransfer->uploadhandle, nexttransfer->transfercipher(), -1, false);
                        }
                    }
                }
                else
                {
                    for (file_list::iterator it = nexttransfer->files.begin();
                         it != nexttransfer->files.end(); it++)
                    {
                        if (!(*it)->hprivate || (*it)->hforeign || nodebyhandle((*it)->h))
                        {
                            h = (*it)->h;
                            hprivate = (*it)->hprivate;
                            privauth = (*it)->privauth.size() ? (*it)->privauth.c_str() : NULL;
                            pubauth = (*it)->pubauth.size() ? (*it)->pubauth.c_str() : NULL;
                            break;
                        }
                        else
                        {
                            LOG_err << "Unexpected node ownership";
                        }
                    }
                }

                // dispatch request for temporary source/target URL
                if (nexttransfer->cachedtempurl.size())
                {
                    app->transfer_prepare(nexttransfer);
                    ts->tempurl =  nexttransfer->cachedtempurl;
                    nexttransfer->cachedtempurl.clear();
                }
                else
                {
                    reqs.add((ts->pendingcmd = (d == PUT)
                          ? (Command*)new CommandPutFile(this, ts, putmbpscap)
                          : (Command*)new CommandGetFile(this, ts, NULL, h, hprivate, privauth, pubauth)));
                }

                LOG_debug << "Activating transfer";
                ts->slots_it = tslots.insert(tslots.begin(), ts);

                // notify the app about the starting transfer
                for (file_list::iterator it = nexttransfer->files.begin();
                     it != nexttransfer->files.end(); it++)
                {
                    (*it)->start();
                }
                app->transfer_update(nexttransfer);

                return true;
            }
            else if (openfinished)
            {
                string utf8path;
                fsaccess->local2path(&nexttransfer->localfilename, &utf8path);
                if (d == GET)
                {
                    LOG_err << "Error dispatching transfer. Temporary file not writable: " << utf8path;
                    nexttransfer->failed(API_EWRITE);
                }
                else if (!ts->fa->retry)
                {
                    LOG_err << "Error dispatching transfer. Local file permanently unavailable: " << utf8path;
                    nexttransfer->failed(API_EREAD);
                }
                else
                {
                    LOG_warn << "Error dispatching transfer. Local file temporarily unavailable: " << utf8path;
                    nexttransfer->failed(API_EREAD);
                }
            }
        }
        else
        {
            LOG_err << "Error preparing transfer. No localfilename";
            nexttransfer->failed(API_EREAD);
        }
    }
}

// generate upload handle for this upload
// (after 65536 uploads, a node handle clash is possible, but far too unlikely
// to be of real-world concern)
handle MegaClient::getuploadhandle()
{
    byte* ptr = (byte*)(&nextuh + 1);

    while (!++*--ptr);

    return nextuh;
}

// do we have an upload that is still waiting for file attributes before being completed?
void MegaClient::checkfacompletion(handle th, Transfer* t)
{
    if (th)
    {
        bool delayedcompletion;
        handletransfer_map::iterator htit;

        if ((delayedcompletion = !t))
        {
            // abort if upload still running
            if ((htit = faputcompletion.find(th)) == faputcompletion.end())
            {
                LOG_debug << "Upload still running checking a file attribute - " << th;
                return;
            }

            t = htit->second;
        }

        int facount = 0;

        // do we have the pre-set threshold number of file attributes available? complete upload.
        for (fa_map::iterator it = pendingfa.lower_bound(pair<handle, fatype>(th, 0));
             it != pendingfa.end() && it->first.first == th; it++)
        {
            facount++;
        }

        if (facount < t->minfa)
        {
            LOG_debug << "Pending file attributes for upload - " << th <<  " : " << (t->minfa < facount);
            if (!delayedcompletion)
            {
                // we have insufficient file attributes available: remove transfer and put on hold
                t->faputcompletion_it = faputcompletion.insert(pair<handle, Transfer*>(th, t)).first;

                transfers[t->type].erase(t->transfers_it);
                t->transfers_it = transfers[t->type].end();

                delete t->slot;
                t->slot = NULL;

                LOG_debug << "Transfer put on hold. Total: " << faputcompletion.size();
            }

            return;
        }
    }
    else
    {
        LOG_warn << "NULL file attribute handle";
    }

    LOG_debug << "Transfer finished, sending callbacks - " << th;    
    t->state = TRANSFERSTATE_COMPLETED;
    t->completefiles();
    looprequested = true;
    app->transfer_complete(t);
    delete t;
}

// clear transfer queue
void MegaClient::freeq(direction_t d)
{
    for (transfer_map::iterator it = transfers[d].begin(); it != transfers[d].end(); )
    {
        delete it++->second;
    }
}

// determine next scheduled transfer retry
// FIXME: make this an ordered set and only check the first element instead of
// scanning the full map!
void MegaClient::nexttransferretry(direction_t d, dstime* dsmin)
{
    for (transfer_map::iterator it = transfers[d].begin(); it != transfers[d].end(); it++)
    {
        if ((!it->second->slot || !it->second->slot->fa)
         && it->second->bt.nextset())
        {
            it->second->bt.update(dsmin);
            if (it->second->bt.armed())
            {
                // fire the timer only once but keeping it armed
                it->second->bt.set(0);
                LOG_debug << "Disabling armed transfer backoff";
            }
        }
    }
}

// disconnect all HTTP connections (slows down operations, but is semantically neutral)
void MegaClient::disconnect()
{
    if (pendingcs)
    {
        app->request_response_progress(-1, -1);
        pendingcs->disconnect();
    }

    if (pendingsc)
    {
        pendingsc->disconnect();
    }

    abortlockrequest();

    for (pendinghttp_map::iterator it = pendinghttp.begin(); it != pendinghttp.end(); it++)
    {
        it->second->disconnect();
    }

    for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); it++)
    {
        (*it)->disconnect();
    }

    for (handledrn_map::iterator it = hdrns.begin(); it != hdrns.end();)
    {
        (it++)->second->retry(API_OK);
    }

    for (putfa_list::iterator it = activefa.begin(); it != activefa.end(); it++)
    {
        (*it)->disconnect();
    }

    for (fafc_map::iterator it = fafcs.begin(); it != fafcs.end(); it++)
    {
        it->second->req.disconnect();
    }

    for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); it++)
    {
        (*it)->errorcount = 0;
    }

    if (badhostcs)
    {
        badhostcs->disconnect();
    }

    httpio->lastdata = NEVER;
    httpio->disconnect();

    app->notify_disconnect();
}

void MegaClient::abortlockrequest()
{
    delete workinglockcs;
    workinglockcs = NULL;
    btworkinglock.reset();
    requestLock = false;
    disconnecttimestamp = NEVER;
}

void MegaClient::logout()
{
    if (loggedin() != FULLACCOUNT)
    {
        removecaches();
        locallogout();

        restag = reqtag;
        app->logout_result(API_OK);
        return;
    }

    reqs.add(new CommandLogout(this));
}

void MegaClient::locallogout()
{
    int i;

    delete sctable;
    sctable = NULL;
    pendingsccommit = false;

    me = UNDEF;
    publichandle = UNDEF;
    cachedscsn = UNDEF;
    achievements_enabled = false;
    tsLogin = false;
    versions_disabled = false;

    freeq(GET);
    freeq(PUT);

    disconnect();
    closetc();

    purgenodesusersabortsc();

    reqs.clear();

    delete pendingcs;
    pendingcs = NULL;

    for (putfa_list::iterator it = queuedfa.begin(); it != queuedfa.end(); it++)
    {
        delete *it;
    }

    for (putfa_list::iterator it = activefa.begin(); it != activefa.end(); it++)
    {
        delete *it;
    }

    for (pendinghttp_map::iterator it = pendinghttp.begin(); it != pendinghttp.end(); it++)
    {
        delete it->second;
    }

    queuedfa.clear();
    activefa.clear();
    pendinghttp.clear();
    xferpaused[PUT] = false;
    xferpaused[GET] = false;
    putmbpscap = 0;
    fetchingnodes = false;
    fetchnodestag = 0;
    overquotauntil = 0;
    scpaused = false;

    for (fafc_map::iterator cit = fafcs.begin(); cit != fafcs.end(); cit++)
    {
        for (i = 2; i--; )
        {
    	    for (faf_map::iterator it = cit->second->fafs[i].begin(); it != cit->second->fafs[i].end(); it++)
    	    {
                delete it->second;
    	    }
        }

        delete cit->second;
    }

    fafcs.clear();

    pendingfa.clear();

    // erase keys & session ID
#ifdef ENABLE_CHAT
    resetKeyring();
#endif

    key.setkey(SymmCipher::zeroiv);
    asymkey.resetkey();
    memset((char*)auth.c_str(), 0, auth.size());
    auth.clear();
    sessionkey.clear();
    sid.clear();

    init();

    if (dbaccess)
    {
        dbaccess->currentDbVersion = DbAccess::LEGACY_DB_VERSION;
    }

#ifdef ENABLE_SYNC
    syncadding = 0;
    totalLocalNodes = 0;
#endif

#ifdef ENABLE_CHAT
    fetchingkeys = false;
#endif
}

void MegaClient::removecaches()
{
    if (sctable)
    {
        sctable->remove();
        delete sctable;
        sctable = NULL;
        pendingsccommit = false;
    }

#ifdef ENABLE_SYNC
    for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
    {
        if ((*it)->statecachetable)
        {
            (*it)->statecachetable->remove();
            delete (*it)->statecachetable;
            (*it)->statecachetable = NULL;
        }
    }
#endif

    disabletransferresumption();
}

const char *MegaClient::version()
{
    return TOSTRING(MEGA_MAJOR_VERSION)
            "." TOSTRING(MEGA_MINOR_VERSION)
            "." TOSTRING(MEGA_MICRO_VERSION);
}

void MegaClient::getlastversion(const char *appKey)
{
    reqs.add(new CommandGetVersion(this, appKey));
}

void MegaClient::getlocalsslcertificate()
{
    reqs.add(new CommandGetLocalSSLCertificate(this));
}

void MegaClient::dnsrequest(const char *hostname)
{
    GenericHttpReq *req = new GenericHttpReq();
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = string("http://") + hostname;
    req->dns(this);
}

void MegaClient::gelbrequest(const char *service, int timeoutms, int retries)
{
    GenericHttpReq *req = new GenericHttpReq();
    req->tag = reqtag;
    req->maxretries = retries;
    req->maxbt.backoff(timeoutms);
    pendinghttp[reqtag] = req;
    req->posturl = GELBURL;
    req->posturl.append("?service=");
    req->posturl.append(service);
    req->protect = true;
    req->get(this);
}

void MegaClient::sendchatstats(const char *json)
{
    GenericHttpReq *req = new GenericHttpReq();
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = CHATSTATSURL;
    req->posturl.append("stats");
    req->protect = true;
    req->out->assign(json);
    req->post(this);
}

void MegaClient::sendchatlogs(const char *json, const char *aid)
{
    GenericHttpReq *req = new GenericHttpReq();
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = CHATSTATSURL;
    req->posturl.append("msglog?aid=");
    req->posturl.append(aid);
    req->posturl.append("&t=e");
    req->protect = true;
    req->out->assign(json);
    req->post(this);
}

void MegaClient::httprequest(const char *url, int method, bool binary, const char *json, int retries)
{
    GenericHttpReq *req = new GenericHttpReq(binary);
    req->tag = reqtag;
    req->maxretries = retries;
    pendinghttp[reqtag] = req;
    if (method == METHOD_GET)
    {
        req->posturl = url;
        req->get(this);
    }
    else
    {
        req->posturl = url;
        if (json)
        {
            req->out->assign(json);
        }
        req->post(this);
    }
}

// process server-client request
bool MegaClient::procsc()
{
    nameid name;

#ifdef ENABLE_SYNC
    char test[] = "},{\"a\":\"t\",\"i\":\"";
    char test2[32] = "\",\"t\":{\"f\":[{\"h\":\"";
    bool stop = false;
    bool newnodes = false;
#endif
    Node* dn = NULL;

    for (;;)
    {
        if (!insca)
        {
            switch (jsonsc.getnameid())
            {
                case 'w':
                    if (!statecurrent)
                    {
                        if (fetchingnodes)
                        {
                            notifypurge();
                            if (sctable)
                            {
                                sctable->commit();
                                sctable->begin();
                                pendingsccommit = false;
                            }

                            WAIT_CLASS::bumpds();
                            fnstats.timeToResult = Waiter::ds - fnstats.startTime;
                            fnstats.timeToCurrent = fnstats.timeToResult;

                            fetchingnodes = false;
                            restag = fetchnodestag;
                            fetchnodestag = 0;
                            app->fetchnodes_result(API_OK);
                            app->notify_dbcommit();

                            WAIT_CLASS::bumpds();
                            fnstats.timeToSyncsResumed = Waiter::ds - fnstats.startTime;
                        }
                        else
                        {
                            WAIT_CLASS::bumpds();
                            fnstats.timeToCurrent = Waiter::ds - fnstats.startTime;
                        }
                        fnstats.nodesCurrent = nodes.size();

                        statecurrent = true;
                        app->nodes_current();
                        LOG_debug << "Local filesystem up to date";

                        if (tctable && cachedfiles.size())
                        {
                            tctable->begin();
                            for (unsigned int i = 0; i < cachedfiles.size(); i++)
                            {
                                direction_t type = NONE;
                                File *file = app->file_resume(&cachedfiles.at(i), &type);
                                if (!file || (type != GET && type != PUT))
                                {
                                    tctable->del(cachedfilesdbids.at(i));
                                    continue;
                                }
                                nextreqtag();
                                file->dbid = cachedfilesdbids.at(i);
                                if (!startxfer(type, file))
                                {
                                    tctable->del(cachedfilesdbids.at(i));
                                    continue;
                                }
                            }
                            cachedfiles.clear();
                            cachedfilesdbids.clear();
                            tctable->commit();
                        }

                        WAIT_CLASS::bumpds();
                        fnstats.timeToTransfersResumed = Waiter::ds - fnstats.startTime;

                        string report;
                        fnstats.toJsonArray(&report);

                        int creqtag = reqtag;
                        reqtag = 0;
                        sendevent(99426, report.c_str());
                        reqtag = creqtag;

                        // NULL vector: "notify all elements"
                        app->nodes_updated(NULL, nodes.size());
                        app->users_updated(NULL, users.size());
                        app->pcrs_updated(NULL, pcrindex.size());
#ifdef ENABLE_CHAT
                        app->chats_updated(NULL, chats.size());
#endif
                        for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++)
                        {
                            memset(&(it->second->changed), 0, sizeof it->second->changed);
                        }
                    }
                
                    jsonsc.storeobject(&scnotifyurl);
                    break;

                case MAKENAMEID2('s', 'n'):
                    // the sn element is guaranteed to be the last in sequence
                    setscsn(&jsonsc);
                    notifypurge();
                    if (sctable)
                    {
                        if (!pendingcs && !csretrying && !reqs.cmdspending())
                        {
                            sctable->commit();
                            sctable->begin();
                            app->notify_dbcommit();
                            pendingsccommit = false;
                        }
                        else
                        {
                            LOG_debug << "Postponing DB commit until cs requests finish";
                            pendingsccommit = true;
                        }
                    }
                    break;
                    
                case EOO:
                    mergenewshares(1);
                    applykeys();
                    return true;

                case 'a':
                    if (jsonsc.enterarray())
                    {
                        insca = true;
                        break;
                    }
                    // fall through
                default:
                    if (!jsonsc.storeobject())
                    {
                        LOG_err << "Error parsing sc request";
                        return true;
                    }
            }
        }

        if (insca)
        {
            if (jsonsc.enterobject())
            {
                // the "a" attribute is guaranteed to be the first in the object
                if (jsonsc.getnameid() == 'a')
                {
                    if (!statecurrent)
                    {
                        fnstats.actionPackets++;
                    }

                    name = jsonsc.getnameid();

                    // only process server-client request if not marked as
                    // self-originating ("i" marker element guaranteed to be following
                    // "a" element if present)
                    if (fetchingnodes || memcmp(jsonsc.pos, "\"i\":\"", 5)
                     || memcmp(jsonsc.pos + 5, sessionid, sizeof sessionid)
                     || jsonsc.pos[5 + sizeof sessionid] != '"')
                    {
                        switch (name)
                        {
                            case 'u':
                                // node update
                                sc_updatenode();
#ifdef ENABLE_SYNC
                                if (!fetchingnodes)
                                {
                                    // run syncdown() before continuing
                                    applykeys();
                                    return false;
                                }
#endif
                                break;

                            case 't':
#ifdef ENABLE_SYNC
                                if (!fetchingnodes && !stop)
                                {
                                    for (int i=4; jsonsc.pos[i] && jsonsc.pos[i] != ']'; i++)
                                    {
                                        if (!memcmp(&jsonsc.pos[i-4], "\"t\":1", 5))
                                        {
                                            stop = true;
                                            break;
                                        }
                                    }
                                }
#endif

                                // node addition
                                sc_newnodes();
                                mergenewshares(1);

#ifdef ENABLE_SYNC
                                if (!fetchingnodes)
                                {
                                    if (stop)
                                    {
                                        // run syncdown() before continuing
                                        applykeys();
                                        return false;
                                    }
                                    else
                                    {
                                        newnodes = true;
                                    }
                                }
#endif
                                break;

                            case 'd':
                                // node deletion
                                dn = sc_deltree();

#ifdef ENABLE_SYNC
                                if (fetchingnodes)
                                {
                                    break;
                                }

                                if (dn && !memcmp(jsonsc.pos, test, 16))
                                {
                                    Base64::btoa((byte *)&dn->nodehandle, sizeof(dn->nodehandle), &test2[18]);
                                    if (!memcmp(&jsonsc.pos[26], test2, 26))
                                    {
                                        // it's a move operation, stop parsing after completing it
                                        stop = true;
                                        break;
                                    }
                                }

                                // run syncdown() to process the deletion before continuing
                                applykeys();
                                return false;
#endif
                                break;

                            case 's':
                            case MAKENAMEID2('s', '2'):
                                // share addition/update/revocation
                                if (sc_shares())
                                {
                                    int creqtag = reqtag;
                                    reqtag = 0;
                                    mergenewshares(1);
                                    reqtag = creqtag;
                                }
                                break;

                            case 'c':
                                // contact addition/update
                                sc_contacts();
                                break;

                            case 'k':
                                // crypto key request
                                sc_keys();
                                break;

                            case MAKENAMEID2('f', 'a'):
                                // file attribute update
                                sc_fileattr();
                                break;

                            case MAKENAMEID2('u', 'a'):
                                // user attribute update
                                sc_userattr();
                                break;

                            case MAKENAMEID4('p', 's', 't', 's'):
                                if (sc_upgrade())
                                {
                                    app->account_updated();
                                    abortbackoff(true);
                                }
                                break;

                            case MAKENAMEID3('i', 'p', 'c'):
                                // incoming pending contact request (to us)
                                sc_ipc();
                                break;

                            case MAKENAMEID3('o', 'p', 'c'):
                                // outgoing pending contact request (from us)
                                sc_opc();
                                break;

                            case MAKENAMEID4('u', 'p', 'c', 'i'):
                                // incoming pending contact request update (accept/deny/ignore)
                                // fall through
                            case MAKENAMEID4('u', 'p', 'c', 'o'):
                                // outgoing pending contact request update (from them, accept/deny/ignore)
                                sc_upc();
                                break;

                            case MAKENAMEID2('p','h'):
                                // public links handles
                                sc_ph();
                                break;

                            case MAKENAMEID2('s','e'):
                                // set email
                                sc_se();
                                break;
#ifdef ENABLE_CHAT
                            case MAKENAMEID3('m', 'c', 'c'):
                                // chat creation / peer's invitation / peer's removal
                                sc_chatupdate();
                                break;

                            case MAKENAMEID4('m', 'c', 'n', 'a'):
                                // granted / revoked access to a node
                                sc_chatnode();
                                break;
#endif
                            case MAKENAMEID3('u', 'a', 'c'):
                                sc_uac();
                                break;
                        }
                    }
                }

                jsonsc.leaveobject();
            }
            else
            {
                jsonsc.leavearray();
                insca = false;

#ifdef ENABLE_SYNC
                if (!fetchingnodes && newnodes)
                {
                    applykeys();
                    return false;
                }
#endif
            }
        }
    }
}

// update the user's local state cache
// (note that if immediate-completion commands have been issued in the
// meantime, the state of the affected nodes
// may be ahead of the recorded scsn - their consistency will be checked by
// subsequent server-client commands.)
// initsc() is called after all initial decryption has been performed, so we
// are tolerant towards incomplete/faulty nodes.
void MegaClient::initsc()
{
    if (sctable)
    {
        bool complete;

        sctable->begin();
        sctable->truncate();

        // 1. write current scsn
        handle tscsn;
        Base64::atob(scsn, (byte*)&tscsn, sizeof tscsn);
        complete = sctable->put(CACHEDSCSN, (char*)&tscsn, sizeof tscsn);

        if (complete)
        {
            // 2. write all users
            for (user_map::iterator it = users.begin(); it != users.end(); it++)
            {
                if (!(complete = sctable->put(CACHEDUSER, &it->second, &key)))
                {
                    break;
                }
            }
        }

        if (complete)
        {
            // 3. write new or modified nodes, purge deleted nodes
            for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++)
            {
                if (!(complete = sctable->put(CACHEDNODE, it->second, &key)))
                {
                    break;
                }
            }
        }

        if (complete)
        {
            // 4. write new or modified pcrs, purge deleted pcrs
            for (handlepcr_map::iterator it = pcrindex.begin(); it != pcrindex.end(); it++)
            {
                if (!(complete = sctable->put(CACHEDPCR, it->second, &key)))
                {
                    break;
                }
            }
        }

#ifdef ENABLE_CHAT
        if (complete)
        {
            // 5. write new or modified chats
            for (textchat_map::iterator it = chats.begin(); it != chats.end(); it++)
            {
                if (!(complete = sctable->put(CACHEDCHAT, it->second, &key)))
                {
                    break;
                }
            }
        }
        LOG_debug << "Saving SCSN " << scsn << " with " << nodes.size() << " nodes, " << users.size() << " users, " << pcrindex.size() << " pcrs and " << chats.size() << " chats to local cache (" << complete << ")";
#else

        LOG_debug << "Saving SCSN " << scsn << " with " << nodes.size() << " nodes and " << users.size() << " users and " << pcrindex.size() << " pcrs to local cache (" << complete << ")";
 #endif
        finalizesc(complete);
    }
}

// erase and and fill user's local state cache
void MegaClient::updatesc()
{
    if (sctable)
    {
        string t;

        sctable->get(CACHEDSCSN, &t);

        if (t.size() != sizeof cachedscsn)
        {
            if (t.size())
            {
                LOG_err << "Invalid scsn size";
            }
            return;
        }

        bool complete;

        // 1. update associated scsn
        handle tscsn;
        Base64::atob(scsn, (byte*)&tscsn, sizeof tscsn);
        complete = sctable->put(CACHEDSCSN, (char*)&tscsn, sizeof tscsn);

        if (complete)
        {
            // 2. write new or update modified users
            for (user_vector::iterator it = usernotify.begin(); it != usernotify.end(); it++)
            {
                char base64[12];
                if ((*it)->show == INACTIVE && (*it)->userhandle != me)
                {
                    if ((*it)->dbid)
                    {
                        LOG_verbose << "Removing inactive user from database: " << (Base64::btoa((byte*)&((*it)->userhandle),MegaClient::USERHANDLE,base64) ? base64 : "");
                        if (!(complete = sctable->del((*it)->dbid)))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    LOG_verbose << "Adding/updating user to database: " << (Base64::btoa((byte*)&((*it)->userhandle),MegaClient::USERHANDLE,base64) ? base64 : "");
                    if (!(complete = sctable->put(CACHEDUSER, *it, &key)))
                    {
                        break;
                    }
                }
            }
        }

        if (complete)
        {
            // 3. write new or modified nodes, purge deleted nodes
            for (node_vector::iterator it = nodenotify.begin(); it != nodenotify.end(); it++)
            {
                char base64[12];
                if ((*it)->changed.removed)
                {
                    if ((*it)->dbid)
                    {
                        LOG_verbose << "Removing node from database: " << (Base64::btoa((byte*)&((*it)->nodehandle),MegaClient::NODEHANDLE,base64) ? base64 : "");
                        if (!(complete = sctable->del((*it)->dbid)))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    LOG_verbose << "Adding node to database: " << (Base64::btoa((byte*)&((*it)->nodehandle),MegaClient::NODEHANDLE,base64) ? base64 : "");
                    if (!(complete = sctable->put(CACHEDNODE, *it, &key)))
                    {
                        break;
                    }
                }
            }
        }

        if (complete)
        {
            // 4. write new or modified pcrs, purge deleted pcrs
            for (pcr_vector::iterator it = pcrnotify.begin(); it != pcrnotify.end(); it++)
            {
                char base64[12];
                if ((*it)->removed())
                {
                    if ((*it)->dbid)
                    {
                        LOG_verbose << "Removing pcr from database: " << (Base64::btoa((byte*)&((*it)->id),MegaClient::PCRHANDLE,base64) ? base64 : "");
                        if (!(complete = sctable->del((*it)->dbid)))
                        {
                            break;
                        }
                    }
                }
                else if (!(*it)->removed())
                {
                    LOG_verbose << "Adding pcr to database: " << (Base64::btoa((byte*)&((*it)->id),MegaClient::PCRHANDLE,base64) ? base64 : "");
                    if (!(complete = sctable->put(CACHEDPCR, *it, &key)))
                    {
                        break;
                    }
                }
            }
        }

#ifdef ENABLE_CHAT
        if (complete)
        {
            // 5. write new or modified chats
            for (textchat_map::iterator it = chatnotify.begin(); it != chatnotify.end(); it++)
            {
                char base64[12];
                LOG_verbose << "Adding chat to database: " << (Base64::btoa((byte*)&(it->second->id),MegaClient::CHATHANDLE,base64) ? base64 : "");
                if (!(complete = sctable->put(CACHEDCHAT, it->second, &key)))
                {
                    break;
                }
            }
        }
        LOG_debug << "Saving SCSN " << scsn << " with " << nodenotify.size() << " modified nodes, " << usernotify.size() << " users, " << pcrnotify.size() << " pcrs and " << chatnotify.size() << " chats to local cache (" << complete << ")";
#else
        LOG_debug << "Saving SCSN " << scsn << " with " << nodenotify.size() << " modified nodes, " << usernotify.size() << " users and " << pcrnotify.size() << " pcrs to local cache (" << complete << ")";
#endif
        finalizesc(complete);
    }
}

// commit or purge local state cache
void MegaClient::finalizesc(bool complete)
{
    if (complete)
    {
        Base64::atob(scsn, (byte*)&cachedscsn, sizeof cachedscsn);
    }
    else
    {
        sctable->remove();

        LOG_err << "Cache update DB write error - disabling caching";

        delete sctable;
        sctable = NULL;
        pendingsccommit = false;
    }
}

// queue node file attribute for retrieval or cancel retrieval
error MegaClient::getfa(handle h, string *fileattrstring, string *nodekey, fatype t, int cancel)
{
    // locate this file attribute type in the nodes's attribute string
    handle fah;
    int p, pp;

    // find position of file attribute or 0 if not present
    if (!(p = Node::hasfileattribute(fileattrstring, t)))
    {
        return API_ENOENT;
    }

    pp = p - 1;

    while (pp && fileattrstring->at(pp - 1) >= '0' && fileattrstring->at(pp - 1) <= '9')
    {
        pp--;
    }

    if (p == pp)
    {
        return API_ENOENT;
    }

    if (Base64::atob(strchr(fileattrstring->c_str() + p, '*') + 1, (byte*)&fah, sizeof(fah)) != sizeof(fah))
    {
        return API_ENOENT;
    }

    int c = atoi(fileattrstring->c_str() + pp);

    if (cancel)
    {
        // cancel pending request
        fafc_map::iterator cit;

        if ((cit = fafcs.find(c)) != fafcs.end())
        {
            faf_map::iterator it;

            for (int i = 2; i--; )
            {
                if ((it = cit->second->fafs[i].find(fah)) != cit->second->fafs[i].end())
                {
                    delete it->second;
                    cit->second->fafs[i].erase(it);

                    // none left: tear down connection
                    if (!cit->second->fafs[1].size() && cit->second->req.status == REQ_INFLIGHT)
                    {
                        cit->second->req.disconnect();
                    }

                    return API_OK;
                }
            }
        }

        return API_ENOENT;
    }
    else
    {
        // add file attribute cluster channel and set cluster reference node handle
        FileAttributeFetchChannel** fafcp = &fafcs[c];

        if (!*fafcp)
        {
            *fafcp = new FileAttributeFetchChannel();
        }

        if (!(*fafcp)->fafs[1].count(fah))
        {
            (*fafcp)->fahref = fah;

            // map returned handle to type/node upon retrieval response
            FileAttributeFetch** fafp = &(*fafcp)->fafs[0][fah];

            if (!*fafp)
            {
                *fafp = new FileAttributeFetch(h, *nodekey, t, reqtag);
            }
            else
            {
                restag = (*fafp)->tag;
                return API_EEXIST;
            }
        }
        else
        {
            FileAttributeFetch** fafp = &(*fafcp)->fafs[1][fah];
            restag = (*fafp)->tag;
            return API_EEXIST;
        }

        return API_OK;
    }
}

// build pending attribute string for this handle and remove
void MegaClient::pendingattrstring(handle h, string* fa)
{
    char buf[128];

    for (fa_map::iterator it = pendingfa.lower_bound(pair<handle, fatype>(h, 0));
         it != pendingfa.end() && it->first.first == h; )
    {
        if (it->first.second != fa_media)
        {
            sprintf(buf, "/%u*", (unsigned)it->first.second);
            Base64::btoa((byte*)&it->second.first, sizeof(it->second.first), strchr(buf + 3, 0));
            fa->append(buf + !fa->size());
            LOG_debug << "Added file attribute to putnodes. Remaining: " << pendingfa.size()-1;
        }
        pendingfa.erase(it++);
    }
}

// attach file attribute to a file (th can be upload or node handle)
// FIXME: to avoid unnecessary roundtrips to the attribute servers, also cache locally
void MegaClient::putfa(handle th, fatype t, SymmCipher* key, string* data, bool checkAccess)
{
    // CBC-encrypt attribute data (padded to next multiple of BLOCKSIZE)
    data->resize((data->size() + SymmCipher::BLOCKSIZE - 1) & -SymmCipher::BLOCKSIZE);
    key->cbc_encrypt((byte*)data->data(), data->size());

    queuedfa.push_back(new HttpReqCommandPutFA(this, th, t, data, checkAccess));
    LOG_debug << "File attribute added to queue - " << th << " : " << queuedfa.size() << " queued, " << activefa.size() << " active";

    // no other file attribute storage request currently in progress? POST this one.
    while (activefa.size() < MAXPUTFA && queuedfa.size())
    {
        putfa_list::iterator curfa = queuedfa.begin();
        HttpReqCommandPutFA *fa = *curfa;
        queuedfa.erase(curfa);
        activefa.push_back(fa);
        fa->status = REQ_INFLIGHT;
        reqs.add(fa);
    }
}

// has the limit of concurrent transfer tslots been reached?
bool MegaClient::slotavail() const
{
    return tslots.size() < MAXTOTALTRANSFERS;
}

// returns 1 if more transfers of the requested type can be dispatched
// (back-to-back overlap pipelining)
// FIXME: support overlapped partial reads (and support partial reads in the
// first place)
bool MegaClient::moretransfers(direction_t d)
{
    m_off_t r = 0;
    unsigned int total = 0;

    // don't dispatch if all tslots busy
    if (!slotavail())
    {
        return false;
    }

    // determine average speed and total amount of data remaining for the given
    // direction
    for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); it++)
    {
        if ((*it)->transfer->type == d)
        {
            r += (*it)->transfer->size - (*it)->progressreported;
            total++;
        }
    }

    if (total >= MAXTRANSFERS)
    {
        return false;
    }

    m_off_t speed = (d == GET) ? httpio->downloadSpeed : httpio->uploadSpeed;

    // always blindly dispatch transfers up to MINPIPELINE
    // dispatch more transfers if only a a little chunk per transfer left
    // dispatch more if only two seconds of transfers left
    if (r < MINPIPELINE || r < total * 131072 || (speed > 1024 && (r / speed) <= 2))
    {
        return true;
    }

    // otherwise, don't allow more than two concurrent transfers
    if (total >= 2)
    {
        return false;
    }

    // dispatch a second transfer if less than 512KB left
    if (r < 524288)
    {
        return true;
    }
    return false;
}

void MegaClient::dispatchmore(direction_t d)
{
    // keep pipeline full by dispatching additional queued transfers, if
    // appropriate and available
    while (moretransfers(d) && dispatch(d));
}

// server-client node update processing
void MegaClient::sc_updatenode()
{
    handle h = UNDEF;
    handle u = 0;
    const char* a = NULL;
    m_time_t ts = -1;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'n':
                h = jsonsc.gethandle();
                break;

            case 'u':
                u = jsonsc.gethandle(USERHANDLE);
                break;

            case MAKENAMEID2('a', 't'):
                a = jsonsc.getvalue();
                break;

            case MAKENAMEID2('t', 's'):
                ts = jsonsc.getint();
                break;

            case EOO:
                if (!ISUNDEF(h))
                {
                    Node* n;

                    if ((n = nodebyhandle(h)))
                    {
                        if (u)
                        {
                            n->owner = u;
                            n->changed.owner = true;
                        }

                        if (a)
                        {
                            if (!n->attrstring)
                            {
                                n->attrstring = new string;
                            }
                            Node::copystring(n->attrstring, a);
                            n->changed.attrs = true;
                        }

                        if (ts + 1)
                        {
                            n->ctime = ts;
                            n->changed.ctime = true;
                        }

                        n->applykey();
                        n->setattr();

                        notifynode(n);
                    }
                }
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// read tree object (nodes and users)
void MegaClient::readtree(JSON* j)
{
    if (j->enterobject())
    {
        for (;;)
        {
            switch (jsonsc.getnameid())
            {
                case 'f':
                    readnodes(j, 1);
                    break;

                case MAKENAMEID2('f', '2'):
                    readnodes(j, 1);
                    break;

                case 'u':
                    readusers(j);
                    break;

                case EOO:
                    j->leaveobject();
                    return;

                default:
                    if (!jsonsc.storeobject())
                    {
                        return;
                    }
            }
        }
    }
}

// server-client newnodes processing
void MegaClient::sc_newnodes()
{
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 't':
                readtree(&jsonsc);
                break;

            case 'u':
                readusers(&jsonsc);
                break;

            case EOO:
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// share requests come in the following flavours:
// - n/k (set share key) (always symmetric)
// - n/o/u[/okd] (share deletion)
// - n/o/u/k/r/ts[/ok][/ha] (share addition) (k can be asymmetric)
// returns 0 in case of a share addition or error, 1 otherwise
bool MegaClient::sc_shares()
{
    handle h = UNDEF;
    handle oh = UNDEF;
    handle uh = UNDEF;
    handle p = UNDEF;
    bool upgrade_pending_to_full = false;
    const char* k = NULL;
    const char* ok = NULL;
    bool okremoved = false;
    byte ha[SymmCipher::BLOCKSIZE];
    byte sharekey[SymmCipher::BLOCKSIZE];
    int have_ha = 0;
    accesslevel_t r = ACCESS_UNKNOWN;
    m_time_t ts = 0;
    int outbound;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'p':  // Pending contact request handle for an s2 packet
                p = jsonsc.gethandle(PCRHANDLE);
                break;

            case MAKENAMEID2('o', 'p'):
                upgrade_pending_to_full = true;
                break;

            case 'n':   // share node
                h = jsonsc.gethandle();
                break;

            case 'o':   // owner user
                oh = jsonsc.gethandle(USERHANDLE);
                break;

            case 'u':   // target user
                uh = jsonsc.is(EXPORTEDLINK) ? 0 : jsonsc.gethandle(USERHANDLE);
                break;

            case MAKENAMEID2('o', 'k'):  // owner key
                ok = jsonsc.getvalue();
                break;

            case MAKENAMEID3('o', 'k', 'd'):
                okremoved = (jsonsc.getint() == 1); // owner key removed
                break;

            case MAKENAMEID2('h', 'a'):  // outgoing share signature
                have_ha = Base64::atob(jsonsc.getvalue(), ha, sizeof ha) == sizeof ha;
                break;

            case 'r':   // share access level
                r = (accesslevel_t)jsonsc.getint();
                break;

            case MAKENAMEID2('t', 's'):  // share timestamp
                ts = jsonsc.getint();
                break;

            case 'k':   // share key
                k = jsonsc.getvalue();
                break;

            case EOO:
                // we do not process share commands unless logged into a full
                // account
                if (loggedin() < FULLACCOUNT)
                {
                    return false;
                }

                // need a share node
                if (ISUNDEF(h))
                {
                    return false;
                }

                // ignore unrelated share packets (should never be triggered)
                if (!ISUNDEF(oh) && !(outbound = (oh == me)) && (uh != me))
                {
                    return false;
                }

                // am I the owner of the share? use ok, otherwise k.
                if (ok && oh == me)
                {
                    k = ok;
                }

                if (k)
                {
                    if (!decryptkey(k, sharekey, sizeof sharekey, &key, 1, h))
                    {
                        return false;
                    }

                    if (ISUNDEF(oh) && ISUNDEF(uh))
                    {
                        // share key update on inbound share
                        newshares.push_back(new NewShare(h, 0, UNDEF, ACCESS_UNKNOWN, 0, sharekey));
                        return true;
                    }

                    if (!ISUNDEF(oh) && (!ISUNDEF(uh) || !ISUNDEF(p)))
                    {
                        // new share - can be inbound or outbound
                        newshares.push_back(new NewShare(h, outbound,
                                                         outbound ? uh : oh,
                                                         r, ts, sharekey,
                                                         have_ha ? ha : NULL, 
                                                         p, upgrade_pending_to_full));

                        //Returns false because as this is a new share, the node
                        //could not have been received yet
                        return false;
                    }
                }
                else
                {
                    if (!ISUNDEF(oh) && (!ISUNDEF(uh) || !ISUNDEF(p)))
                    {
                        // share revocation or share without key
                        newshares.push_back(new NewShare(h, outbound,
                                                         outbound ? uh : oh, r, 0, NULL, NULL, p, false, okremoved));
                        return r == ACCESS_UNKNOWN;
                    }
                }

                return false;

            default:
                if (!jsonsc.storeobject())
                {
                    return false;
                }
        }
    }
}

bool MegaClient::sc_upgrade()
{
    string result;
    bool success = false;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('i', 't'):
                jsonsc.getint(); // itemclass. For now, it's always 0.
                break;

            case 'p':
                jsonsc.getint(); //pro type
                break;

            case 'r':
                jsonsc.storeobject(&result);
                if (result == "s")
                {
                   success = true;
                }
                break;

            case EOO:
                return success;

            default:
                if (!jsonsc.storeobject())
                {
                    return false;
                }
        }
    }
}

// user/contact updates come in the following format:
// u:[{c/m/ts}*] - Add/modify user/contact
void MegaClient::sc_contacts()
{
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'u':
                readusers(&jsonsc);
                break;

            case EOO:
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// server-client key requests/responses
void MegaClient::sc_keys()
{
    handle h;
    Node* n = NULL;
    node_vector kshares;
    node_vector knodes;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('s', 'r'):
                procsr(&jsonsc);
                break;

            case 'h':
                // security feature: we only distribute node keys for our own
                // outgoing shares
                if (!ISUNDEF(h = jsonsc.gethandle()) && (n = nodebyhandle(h)) && n->sharekey && !n->inshare)
                {
                    kshares.push_back(n);
                }
                break;

            case 'n':
                if (jsonsc.enterarray())
                {
                    while (!ISUNDEF(h = jsonsc.gethandle()) && (n = nodebyhandle(h)))
                    {
                        knodes.push_back(n);
                    }

                    jsonsc.leavearray();
                }
                break;

            case MAKENAMEID2('c', 'r'):
                proccr(&jsonsc);
                break;

            case EOO:
                cr_response(&kshares, &knodes, NULL);
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// server-client file attribute update
void MegaClient::sc_fileattr()
{
    Node* n = NULL;
    const char* fa = NULL;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('f', 'a'):
                fa = jsonsc.getvalue();
                break;

            case 'n':
                handle h;
                if (!ISUNDEF(h = jsonsc.gethandle()))
                {
                    n = nodebyhandle(h);
                }
                break;

            case EOO:
                if (fa && n)
                {
                    Node::copystring(&n->fileattrstring, fa);
                    n->changed.fileattrstring = true;
                    notifynode(n);
                }
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// server-client user attribute update notification
void MegaClient::sc_userattr()
{
    handle uh = UNDEF;
    User *u = NULL;

    string ua, uav;
    string_vector ualist;    // stores attribute names
    string_vector uavlist;   // stores attribute versions
    string_vector::const_iterator itua, ituav;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'u':
                uh = jsonsc.gethandle(USERHANDLE);
                break;

            case MAKENAMEID2('u', 'a'):
                if (jsonsc.enterarray())
                {
                    while (jsonsc.storeobject(&ua))
                    {
                        ualist.push_back(ua);
                    }
                    jsonsc.leavearray();
                }
                break;

            case 'v':
                if (jsonsc.enterarray())
                {
                    while (jsonsc.storeobject(&uav))
                    {
                        uavlist.push_back(uav);
                    }
                    jsonsc.leavearray();
                }
                break;

            case EOO:
                if (ISUNDEF(uh))
                {
                    LOG_err << "Failed to parse the user :" << uh;
                }
                else if (!(u = finduser(uh)))
                {
                    LOG_debug << "User attributes update for non-existing user";
                }
                // if no version received (very old actionpacket)...
                else if ( !uavlist.size() )
                {
                    // ...invalidate all of the notified user attributes
                    for (itua = ualist.begin(); itua != ualist.end(); itua++)
                    {
                        attr_t type = User::string2attr(itua->c_str());
                        u->invalidateattr(type);
#ifdef ENABLE_CHAT
                        if (type == ATTR_KEYRING)
                        {
                            resetKeyring();
                        }
#endif
                    }
                    u->setTag(0);
                    notifyuser(u);
                }
                else if (ualist.size() == uavlist.size())
                {
                    // invalidate only out-of-date attributes
                    const string *cacheduav;
                    for (itua = ualist.begin(), ituav = uavlist.begin();
                         itua != ualist.end();
                         itua++, ituav++)
                    {
                        attr_t type = User::string2attr(itua->c_str());
                        cacheduav = u->getattrversion(type);
                        if (cacheduav)
                        {
                            if (*cacheduav != *ituav)
                            {
                                u->invalidateattr(type);
#ifdef ENABLE_CHAT
                                if (type == ATTR_KEYRING)
                                {
                                    resetKeyring();
                                }
#endif
                            }
                        }
                        else
                        {
                            u->setChanged(type);

                            // if this attr was just created, add it to cache with empty value and set it as invalid
                            // (it will allow to detect if the attr exists upon resumption from cache, in case the value wasn't received yet)
                            if (type == ATTR_DISABLE_VERSIONS && !u->getattr(type))
                            {
                                string emptyStr;
                                u->setattr(type, &emptyStr, &emptyStr);
                                u->invalidateattr(type);
                            }
                        }

                        // silently fetch-upon-update this critical attribute
                        if (type == ATTR_DISABLE_VERSIONS)
                        {
                            getua(u, type, 0);
                        }
                    }
                    u->setTag(0);
                    notifyuser(u);
                }
                else    // different number of attributes than versions --> error
                {
                    LOG_err << "Unpaired user attributes and versions";
                }
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// Incoming pending contact additions or updates, always triggered by the creator (reminders, deletes, etc)
void MegaClient::sc_ipc()
{
    // fields: m, ts, uts, rts, dts, msg, p, ps
    m_time_t ts = 0;
    m_time_t uts = 0;
    m_time_t rts = 0;
    m_time_t dts = 0;
    int ps = 0;
    const char *m = NULL;
    const char *msg = NULL;
    handle p = UNDEF;
    PendingContactRequest *pcr;

    bool done = false;
    while (!done)
    {
        switch (jsonsc.getnameid())
        {
            case 'm':
                m = jsonsc.getvalue();
                break;
            case MAKENAMEID2('p', 's'):
                ps = jsonsc.getint();
                break;
            case MAKENAMEID2('t', 's'):
                ts = jsonsc.getint();
                break;
            case MAKENAMEID3('u', 't', 's'):
                uts = jsonsc.getint();
                break;
            case MAKENAMEID3('r', 't', 's'):
                rts = jsonsc.getint();
                break;
            case MAKENAMEID3('d', 't', 's'):
                dts = jsonsc.getint();
                break;
            case MAKENAMEID3('m', 's', 'g'):
                msg = jsonsc.getvalue();
                break;
            case 'p':
                p = jsonsc.gethandle(MegaClient::PCRHANDLE);
                break;
            case EOO:
                done = true;
                if (ISUNDEF(p))
                {
                    LOG_err << "p element not provided";
                    break;
                }

                pcr = pcrindex.count(p) ? pcrindex[p] : (PendingContactRequest *) NULL;

                if (dts != 0)
                {
                    //Trying to remove an ignored request
                    if (pcr)
                    {
                        // this is a delete, find the existing object in state
                        pcr->uts = dts;
                        pcr->changed.deleted = true;
                    }
                }
                else if (pcr && rts != 0)
                {
                    // reminder
                    if (uts == 0)
                    {
                        LOG_err << "uts element not provided";
                        break;
                    }

                    pcr->uts = uts;
                    pcr->changed.reminded = true;
                }
                else
                {
                    // new
                    if (!m)
                    {
                        LOG_err << "m element not provided";
                        break;
                    }
                    if (ts == 0)
                    {
                        LOG_err << "ts element not provided";
                        break;
                    }
                    if (uts == 0)
                    {
                        LOG_err << "uts element not provided";
                        break;
                    }

                    pcr = new PendingContactRequest(p, m, NULL, ts, uts, msg, false);
                    mappcr(p, pcr);
                }
                notifypcr(pcr);

                break;
            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// Outgoing pending contact additions or updates, always triggered by the creator (reminders, deletes, etc)
void MegaClient::sc_opc()
{
    // fields: e, m, ts, uts, rts, dts, msg, p
    m_time_t ts = 0;
    m_time_t uts = 0;
    m_time_t rts = 0;
    m_time_t dts = 0;
    const char *e = NULL;
    const char *m = NULL;
    const char *msg = NULL;
    handle p = UNDEF;
    PendingContactRequest *pcr;

    bool done = false;
    while (!done)
    {
        switch (jsonsc.getnameid())
        {
            case 'e':
                e = jsonsc.getvalue();
                break;
            case 'm':
                m = jsonsc.getvalue();
                break;
            case MAKENAMEID2('t', 's'):
                ts = jsonsc.getint();
                break;
            case MAKENAMEID3('u', 't', 's'):
                uts = jsonsc.getint();
                break;
            case MAKENAMEID3('r', 't', 's'):
                rts = jsonsc.getint();
                break;
            case MAKENAMEID3('d', 't', 's'):
                dts = jsonsc.getint();
                break;
            case MAKENAMEID3('m', 's', 'g'):
                msg = jsonsc.getvalue();
                break;
            case 'p':
                p = jsonsc.gethandle(MegaClient::PCRHANDLE);
                break;
            case EOO:
                done = true;
                if (ISUNDEF(p))
                {
                    LOG_err << "p element not provided";
                    break;
                }

                pcr = pcrindex.count(p) ? pcrindex[p] : (PendingContactRequest *) NULL;

                if (dts != 0) // delete PCR
                {
                    // this is a delete, find the existing object in state
                    if (pcr)
                    {
                        pcr->uts = dts;
                        pcr->changed.deleted = true;
                    }
                }
                else if (!e || !m || ts == 0 || uts == 0)
                {
                    LOG_err << "Pending Contact Request is incomplete.";
                    break;
                }
                else if (ts == uts) // add PCR
                {
                    pcr = new PendingContactRequest(p, e, m, ts, uts, msg, true);
                    mappcr(p, pcr);
                }
                else    // remind PCR
                {
                    if (rts == 0)
                    {
                        LOG_err << "Pending Contact Request is incomplete (rts element).";
                        break;
                    }

                    if (pcr)
                    {
                        pcr->uts = rts;
                        pcr->changed.reminded = true;
                    }
                }
                notifypcr(pcr);

                break;
            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

// Incoming pending contact request updates, always triggered by the receiver of the request (accepts, denies, etc)
void MegaClient::sc_upc()
{
    // fields: p, uts, s, m
    m_time_t uts = 0;
    int s = 0;
    const char *m = NULL;
    handle p = UNDEF;
    PendingContactRequest *pcr;

    bool done = false;
    while (!done)
    {
        switch (jsonsc.getnameid())
        {
            case 'm':
                m = jsonsc.getvalue();
                break;
            case MAKENAMEID3('u', 't', 's'):
                uts = jsonsc.getint();
                break; 
            case 's':
                s = jsonsc.getint();
                break;
            case 'p':
                p = jsonsc.gethandle(MegaClient::PCRHANDLE);
                break;
            case EOO:
                done = true;
                if (ISUNDEF(p))
                {
                    LOG_err << "p element not provided";
                    break;
                }

                pcr = pcrindex.count(p) ? pcrindex[p] : (PendingContactRequest *) NULL;

                if (!pcr)
                {
                    // As this was an update triggered by us, on an object we must know about, this is kinda a problem.                    
                    LOG_err << "upci PCR not found, huge massive problem";
                    break;
                }
                else
                {                    
                    if (!m)
                    {
                        LOG_err << "m element not provided";
                        break;
                    }
                    if (s == 0)
                    {
                        LOG_err << "s element not provided";
                        break;
                    }
                    if (uts == 0)
                    {
                        LOG_err << "uts element not provided";
                        break;
                    }

                    switch (s)
                    {
                        case 1:
                            // ignored
                            pcr->changed.ignored = true;
                            break;
                        case 2:
                            // accepted
                            pcr->changed.accepted = true;
                            break;
                        case 3:
                            // denied
                            pcr->changed.denied = true;
                            break;
                    }
                    pcr->uts = uts;
                }
                notifypcr(pcr);

                break;
            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}
// Public links updates
void MegaClient::sc_ph()
{
    // fields: h, ph, d, n, ets
    handle h = UNDEF;
    handle ph = UNDEF;
    bool deleted = false;
    bool created = false;
    bool updated = false;
    bool takendown = false;
    m_time_t ets = 0;
    Node *n;

    bool done = false;
    while (!done)
    {
        switch (jsonsc.getnameid())
        {
        case 'h':
            h = jsonsc.gethandle(MegaClient::NODEHANDLE);
            break;
        case MAKENAMEID2('p','h'):
            ph = jsonsc.gethandle(MegaClient::NODEHANDLE);
            break;
        case 'd':
            deleted = (jsonsc.getint() == 1);
            break;
        case 'n':
            created = (jsonsc.getint() == 1);
            break;
        case 'u':
            updated = (jsonsc.getint() == 1);
            break;
        case MAKENAMEID4('d','o','w','n'):
            takendown = (jsonsc.getint() == 1);
            break;
        case MAKENAMEID3('e', 't', 's'):
            ets = jsonsc.getint();
            break;
        case EOO:
            done = true;
            if (ISUNDEF(h))
            {
                LOG_err << "h element not provided";
                break;
            }
            if (ISUNDEF(ph))
            {
                LOG_err << "ph element not provided";
                break;
            }
            if (!deleted && !created && !updated && !takendown)
            {
                LOG_err << "d/n/u/down element not provided";
                break;
            }

            n = nodebyhandle(h);
            if (n)
            {
                if (deleted)        // deletion
                {
                    if (n->plink)
                    {
                        delete n->plink;
                        n->plink = NULL;
                    }
                }
                else
                {
                    n->setpubliclink(ph, ets, takendown);
                }

                n->changed.publiclink = true;
                notifynode(n);
            }
            else
            {
                LOG_warn << "node for public link not found";
            }

            break;
        default:
            if (!jsonsc.storeobject())
            {
                return;
            }
        }
    }
}

void MegaClient::sc_se()
{
    // fields: e, s
    string email;
    int status = -1;
    handle uh = UNDEF;
    User *u;

    bool done = false;
    while (!done)
    {
        switch (jsonsc.getnameid())
        {
        case 'e':
            jsonsc.storeobject(&email);
            break;
        case 'u':
            uh = jsonsc.gethandle(USERHANDLE);
            break;
        case 's':
            status = jsonsc.getint();
            break;
        case EOO:
            done = true;
            if (email.empty())
            {
                LOG_err << "e element not provided";
                break;
            }
            if (uh == UNDEF)
            {
                LOG_err << "u element not provided";
                break;
            }
            if (status == -1)
            {
                LOG_err << "s element not provided";
                break;
            }
            if (status != EMAIL_REMOVED &&
                    status != EMAIL_PENDING_REMOVED &&
                    status != EMAIL_PENDING_ADDED &&
                    status != EMAIL_FULLY_ACCEPTED)
            {
                LOG_err << "unknown value for s element: " << status;
                break;
            }

            u = finduser(uh);
            if (!u)
            {
                LOG_warn << "user for email change not found. Not a contact?";
            }
            else if (status == EMAIL_FULLY_ACCEPTED)
            {
                LOG_debug << "Email changed from `" << u->email << "` to `" << email << "`";

                mapuser(uh, email.c_str()); // update email used as index for user's map
                u->changed.email = true;               
                notifyuser(u);
            }
            // TODO: manage different status once multiple-emails is supported

            break;
        default:
            if (!jsonsc.storeobject())
            {
                return;
            }
        }
    }
}

#ifdef ENABLE_CHAT
void MegaClient::sc_chatupdate()
{
    // fields: id, u, cs, n, g, ou
    handle chatid = UNDEF;
    userpriv_vector *userpriv = NULL;
    int shard = -1;
    userpriv_vector *upnotif = NULL;
    bool group = false;
    handle ou = UNDEF;
    string title;
    m_time_t ts = -1;

    bool done = false;
    while (!done)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('i','d'):
                chatid = jsonsc.gethandle(MegaClient::CHATHANDLE);
                break;

            case 'u':   // list of users participating in the chat (+privileges)
                userpriv = readuserpriv(&jsonsc);
                break;

            case MAKENAMEID2('c','s'):
                shard = jsonsc.getint();
                break;

            case 'n':   // the new user, for notification purposes (not used)
                upnotif = readuserpriv(&jsonsc);
                break;

            case 'g':
                group = jsonsc.getint();
                break;

            case MAKENAMEID2('o','u'):
                ou = jsonsc.gethandle(MegaClient::USERHANDLE);
                break;

            case MAKENAMEID2('c','t'):
                jsonsc.storeobject(&title);
                break;

            case MAKENAMEID2('t', 's'):  // actual creation timestamp
                ts = jsonsc.getint();
                break;

            case EOO:
                done = true;

                if (ISUNDEF(chatid))
                {
                    LOG_err << "Cannot read handle of the chat";
                }
                else if (ISUNDEF(ou))
                {
                    LOG_err << "Cannot read originating user of action packet";
                }
                else if (shard == -1)
                {
                    LOG_err << "Cannot read chat shard";
                }
                else
                {
                    if (chats.find(chatid) == chats.end())
                    {
                        chats[chatid] = new TextChat();
                    }

                    TextChat *chat = chats[chatid];
                    chat->id = chatid;
                    chat->shard = shard;
                    chat->group = group;
                    chat->priv = PRIV_UNKNOWN;
                    chat->ou = ou;
                    chat->title = title;
                    if (ts != -1)
                    {
                        chat->ts = ts;  // only in APs related to chat creation or when you're added to
                    }

                    bool found = false;
                    userpriv_vector::iterator upvit;
                    if (userpriv)
                    {
                        // find 'me' in the list of participants, get my privilege and remove from peer's list
                        for (upvit = userpriv->begin(); upvit != userpriv->end(); upvit++)
                        {
                            if (upvit->first == me)
                            {
                                found = true;
                                chat->priv = upvit->second;
                                userpriv->erase(upvit);
                                if (userpriv->empty())
                                {
                                    delete userpriv;
                                    userpriv = NULL;
                                }
                                break;
                            }
                        }
                    }
                    // if `me` is not found among participants list and there's a notification list...
                    if (!found && upnotif)
                    {
                        // ...then `me` may have been removed from the chat: get the privilege level=PRIV_RM
                        for (upvit = upnotif->begin(); upvit != upnotif->end(); upvit++)
                        {
                            if (upvit->first == me)
                            {
                                chat->priv = upvit->second;
                                break;
                            }
                        }
                    }
                    delete chat->userpriv;  // discard any existing `userpriv`
                    chat->userpriv = userpriv;

                    chat->setTag(0);    // external change
                    notifychat(chat);
                }

                delete upnotif;
                break;

            default:
                if (!jsonsc.storeobject())
                {                    
                    delete upnotif;
                    return;
                }
        }
    }
}

void MegaClient::sc_chatnode()
{
    handle chatid = UNDEF;
    handle h = UNDEF;
    handle uh = UNDEF;
    bool r = false;
    bool g = false;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'g':
                // access granted
                g = jsonsc.getint();
                break;

            case 'r':
                // access revoked
                r = jsonsc.getint();
                break;

            case MAKENAMEID2('i','d'):
                chatid = jsonsc.gethandle(MegaClient::CHATHANDLE);
                break;

            case 'n':
                h = jsonsc.gethandle(MegaClient::NODEHANDLE);
                break;

            case 'u':
                uh = jsonsc.gethandle(MegaClient::USERHANDLE);
                break;

            case EOO:
                if (chatid != UNDEF && h != UNDEF && uh != UNDEF && (r || g))
                {
                    if (chats.find(chatid) == chats.end())
                    {
                        chats[chatid] = new TextChat();
                    }

                    TextChat *chat = chats[chatid];
                    if (r)  // access revoked
                    {
                        if(!chat->setNodeUserAccess(h, uh, true))
                        {
                            LOG_err << "Unknown user/node at revoke access to attachment";
                        }
                    }
                    else    // access granted
                    {
                        chat->setNodeUserAccess(h, uh);
                    }

                    chat->setTag(0);    // external change
                    notifychat(chat);
                }
                else
                {
                    LOG_err << "Failed to parse attached node information";
                }
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
        }
    }
}

#endif

void MegaClient::sc_uac()
{
    string email;
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'm':
                jsonsc.storeobject(&email);
                break;

            case EOO:
                if (email.empty())
                {
                    LOG_warn << "Missing email address in `uac` action packet";
                }
                app->account_updated();
                app->notify_confirmation(email.c_str());
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    LOG_warn << "Failed to parse `uac` action packet";
                    return;
                }
        }
    }
}

// scan notified nodes for
// - name differences with an existing LocalNode
// - appearance of new folders
// - (re)appearance of files
// - deletions
// purge removed nodes after notification
void MegaClient::notifypurge(void)
{
    int i, t;

    handle tscsn = cachedscsn;

    if (*scsn) Base64::atob(scsn, (byte*)&tscsn, sizeof tscsn);

    if (nodenotify.size() || usernotify.size() || pcrnotify.size()
#ifdef ENABLE_CHAT
            || chatnotify.size()
#endif
            || cachedscsn != tscsn)
    {
        updatesc();

#ifdef ENABLE_SYNC
        // update LocalNode <-> Node associations
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            (*it)->cachenodes();
        }
#endif
    }

    if ((t = nodenotify.size()))
    {
#ifdef ENABLE_SYNC
        // check for deleted syncs
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            if (((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN)
             && (*it)->localroot.node->changed.removed)
            {
                delsync(*it);
            }
        }
#endif
        applykeys();

        if (!fetchingnodes)
        {
            app->nodes_updated(&nodenotify[0], t);
        }

        // check all notified nodes for removed status and purge
        for (i = 0; i < t; i++)
        {
            Node* n = nodenotify[i];
            if (n->attrstring)
            {
                LOG_err << "NO_KEY node: " << n->type << " " << n->size << " " << n->nodehandle << " " << n->nodekey.size();
#ifdef ENABLE_SYNC
                if (n->localnode)
                {
                    LOG_err << "LocalNode: " << n->localnode->name << " " << n->localnode->type << " " << n->localnode->size;
                }
#endif
            }

            if (n->changed.removed)
            {
                // remove inbound share
                if (n->inshare)
                {
                    n->inshare->user->sharing.erase(n->nodehandle);
                    notifyuser(n->inshare->user);
                }

                nodes.erase(n->nodehandle);
                delete n;
            }
            else
            {
                n->notified = false;
                memset(&(n->changed), 0, sizeof(n->changed));
                n->tag = 0;
            }
        }

        nodenotify.clear();
    }

    if ((t = pcrnotify.size()))
    {
        if (!fetchingnodes)
        {
            app->pcrs_updated(&pcrnotify[0], t);
        }

        // check all notified nodes for removed status and purge
        for (i = 0; i < t; i++)
        {
            PendingContactRequest* pcr = pcrnotify[i];

            if (pcr->removed())
            {
                pcrindex.erase(pcr->id);
                delete pcr;
            }
            else
            {
                pcr->notified = false;
                memset(&(pcr->changed), 0, sizeof(pcr->changed));
            }
        }

        pcrnotify.clear();
    }

    // users are never deleted (except at account cancellation)
    if ((t = usernotify.size()))
    {
        if (!fetchingnodes)
        {
            app->users_updated(&usernotify[0], t);
        }

        for (i = 0; i < t; i++)
        {
            User *u = usernotify[i];

            u->notified = false;
            u->resetTag();
            memset(&(u->changed), 0, sizeof(u->changed));

            if (u->show == INACTIVE && u->userhandle != me)
            {
                // delete any remaining shares with this user
                for (handle_set::iterator it = u->sharing.begin(); it != u->sharing.end(); it++)
                {
                    Node *n = nodebyhandle(*it);
                    if (n && !n->changed.removed)
                    {
                        int creqtag = reqtag;
                        reqtag = 0;
                        sendevent(99435, "Orphan incoming share");
                        reqtag = creqtag;
                    }
                }
                u->sharing.clear();

                discarduser(u->userhandle);
            }
        }

        usernotify.clear();
    }

#ifdef ENABLE_CHAT
    if ((t = chatnotify.size()))
    {
        if (!fetchingnodes)
        {
            app->chats_updated(&chatnotify, t);
        }

        for (textchat_map::iterator it = chatnotify.begin(); it != chatnotify.end(); it++)
        {
            TextChat *chat = it->second;

            chat->notified = false;
            chat->resetTag();
            memset(&(chat->changed), 0, sizeof(chat->changed));
        }

        chatnotify.clear();
    }
#endif

    totalNodes = nodes.size();
}

// return node pointer derived from node handle
Node* MegaClient::nodebyhandle(handle h)
{
    node_map::iterator it;

    if ((it = nodes.find(h)) != nodes.end())
    {
        return it->second;
    }

    return NULL;
}

// server-client deletion
Node* MegaClient::sc_deltree()
{
    Node* n = NULL;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'n':
                handle h;

                if (!ISUNDEF((h = jsonsc.gethandle())))
                {
                    n = nodebyhandle(h);
                }
                break;

            case EOO:
                if (n)
                {
                    TreeProcDel td;

                    int creqtag = reqtag;
                    reqtag = 0;
                    proctree(n, &td);
                    reqtag = creqtag;
                }
                return n;

            default:
                if (!jsonsc.storeobject())
                {
                    return NULL;
                }
        }
    }
}

// generate handle authentication token
void MegaClient::handleauth(handle h, byte* auth)
{
    Base64::btoa((byte*)&h, NODEHANDLE, (char*)auth);
    memcpy(auth + sizeof h, auth, sizeof h);
    key.ecb_encrypt(auth);
}

// make attribute string; add magic number prefix
void MegaClient::makeattr(SymmCipher* key, string* attrstring, const char* json, int l) const
{
    if (l < 0)
    {
        l = strlen(json);
    }
    int ll = (l + 6 + SymmCipher::KEYLENGTH - 1) & - SymmCipher::KEYLENGTH;
    byte* buf = new byte[ll];

    memcpy(buf, "MEGA{", 5); // check for the presence of the magic number "MEGA"
    memcpy(buf + 5, json, l);
    buf[l + 5] = '}';
    memset(buf + 6 + l, 0, ll - l - 6);

    key->cbc_encrypt(buf, ll);

    attrstring->assign((char*)buf, ll);

    delete[] buf;
}

// update node attributes
// (with speculative instant completion)
error MegaClient::setattr(Node* n, const char *prevattr)
{
    if (!checkaccess(n, FULL))
    {
        return API_EACCESS;
    }

    SymmCipher* cipher;

    if (!(cipher = n->nodecipher()))
    {
        return API_EKEY;
    }

    n->changed.attrs = true;
    n->tag = reqtag;
    notifynode(n);

    reqs.add(new CommandSetAttr(this, n, cipher, prevattr));

    return API_OK;
}

// send new nodes to API for processing
void MegaClient::putnodes(handle h, NewNode* newnodes, int numnodes)
{
    reqs.add(new CommandPutNodes(this, h, NULL, newnodes, numnodes, reqtag));
}

// drop nodes into a user's inbox (must have RSA keypair)
void MegaClient::putnodes(const char* user, NewNode* newnodes, int numnodes)
{
    User* u;

    restag = reqtag;

    if (!(u = finduser(user, 0)) && !user)
    {
        return app->putnodes_result(API_EARGS, USER_HANDLE, newnodes);
    }

    queuepubkeyreq(user, new PubKeyActionPutNodes(newnodes, numnodes, reqtag));
}

// returns 1 if node has accesslevel a or better, 0 otherwise
int MegaClient::checkaccess(Node* n, accesslevel_t a)
{
    // folder link access is always read-only - ignore login status during
    // initial tree fetch
    if ((a < OWNERPRELOGIN) && !loggedin())
    {
        return a == RDONLY;
    }

    // trace back to root node (always full access) or share node
    while (n)
    {
        if (n->inshare)
        {
            return n->inshare->access >= a;
        }

        if (!n->parent)
        {
            return n->type > FOLDERNODE;
        }

        n = n->parent;
    }

    return 0;
}

// returns API_OK if a move operation is permitted, API_EACCESS or
// API_ECIRCULAR otherwise
error MegaClient::checkmove(Node* fn, Node* tn)
{
    // condition #1: cannot move top-level node, must have full access to fn's
    // parent
    if (!fn->parent || !checkaccess(fn->parent, FULL))
    {
        return API_EACCESS;
    }

    // condition #2: target must be folder
    if (tn->type == FILENODE)
    {
        return API_EACCESS;
    }

    // condition #3: must have write access to target
    if (!checkaccess(tn, RDWR))
    {
        return API_EACCESS;
    }

    // condition #4: tn must not be below fn (would create circular linkage)
    for (;;)
    {
        if (tn == fn)
        {
            return API_ECIRCULAR;
        }

        if (tn->inshare || !tn->parent)
        {
            break;
        }

        tn = tn->parent;
    }

    // condition #5: fn and tn must be in the same tree (same ultimate parent
    // node or shared by the same user)
    for (;;)
    {
        if (fn->inshare || !fn->parent)
        {
            break;
        }

        fn = fn->parent;
    }

    // moves within the same tree or between the user's own trees are permitted
    if (fn == tn || (!fn->inshare && !tn->inshare))
    {
        return API_OK;
    }

    // moves between inbound shares from the same user are permitted
    if (fn->inshare && tn->inshare && fn->inshare->user == tn->inshare->user)
    {
        return API_OK;
    }

    return API_EACCESS;
}

// move node to new parent node (for changing the filename, use setattr and
// modify the 'n' attribute)
error MegaClient::rename(Node* n, Node* p, syncdel_t syncdel, handle prevparent)
{
    error e;

    if ((e = checkmove(n, p)))
    {
        return e;
    }

    if (n->setparent(p))
    {
        n->changed.parent = true;
        n->tag = reqtag;
        notifynode(n);

        // rewrite keys of foreign nodes that are moved out of an outbound share
        rewriteforeignkeys(n);

        reqs.add(new CommandMoveNode(this, n, p, syncdel, prevparent));
    }

    return API_OK;
}

// delete node tree
error MegaClient::unlink(Node* n, bool keepversions)
{
    if (!n->inshare && !checkaccess(n, FULL))
    {
        return API_EACCESS;
    }

    bool kv = (keepversions && n->type == FILENODE);
    reqs.add(new CommandDelNode(this, n->nodehandle, kv));

    mergenewshares(1);

    if (kv)
    {
        Node *newerversion = n->parent;
        if (n->children.size())
        {
            Node *olderversion = n->children.back();
            olderversion->setparent(newerversion);
            olderversion->changed.parent = true;
            olderversion->tag = reqtag;
            notifynode(olderversion);
        }
    }

    TreeProcDel td;
    proctree(n, &td);

    return API_OK;
}

void MegaClient::unlinkversions()
{
    reqs.add(new CommandDelVersions(this));
}

// emulates the semantics of its JavaScript counterpart
// (returns NULL if the input is invalid UTF-8)
// unfortunately, discards bits 8-31 of multibyte characters for backwards compatibility
char* MegaClient::str_to_a32(const char* str, int* len)
{
    if (!str)
    {
        return NULL;
    }

    int t = strlen(str);
    int t2 = 4 * ((t + 3) >> 2);
    char* result = new char[t2]();
    uint32_t* a32 = (uint32_t*)result;
    uint32_t unicode;

    int i = 0;
    int j = 0;

    while (i < t)
    {
        char c = str[i++] & 0xff;

        if (!(c & 0x80))
        {
            unicode = c & 0xff;
        }
        else if ((c & 0xe0) == 0xc0)
        {
            if (i >= t || (str[i] & 0xc0) != 0x80)
            {
                delete[] result;
                return NULL;
            }

            unicode = (c & 0x1f) << 6;
            unicode |= str[i++] & 0x3f;
        }
        else if ((c & 0xf0) == 0xe0)
        {
            if (i + 2 > t || (str[i] & 0xc0) != 0x80 || (str[i + 1] & 0xc0) != 0x80)
            {
                delete[] result;
                return NULL;
            }

            unicode = (c & 0x0f) << 12;
            unicode |= (str[i++] & 0x3f) << 6;
            unicode |= str[i++] & 0x3f;
        }
        else if ((c & 0xf8) == 0xf0)
        {
            if (i + 3 > t
            || (str[i] & 0xc0) != 0x80
            || (str[i + 1] & 0xc0) != 0x80
            || (str[i + 2] & 0xc0) != 0x80)
            {
                delete[] result;
                return NULL;
            }

            unicode = (c & 0x07) << 18;
            unicode |= (str[i++] & 0x3f) << 12;
            unicode |= (str[i++] & 0x3f) << 6;
            unicode |= str[i++] & 0x3f;

            // management of surrogate pairs like the JavaScript code
            uint32_t hi = 0xd800 | ((unicode >> 10) & 0x3F) | (((unicode >> 16) - 1) << 6);
            uint32_t low = 0xdc00 | (unicode & 0x3ff);

            a32[j >> 2] |= htonl(hi << (24 - (j & 3) * 8));
            j++;

            unicode = low;
        }
        else
        {
            delete[] result;
            return NULL;
        }

        a32[j >> 2] |= htonl(unicode << (24 - (j & 3) * 8));
        j++;
    }

    *len = j;
    return result;
}

// compute UTF-8 password hash
error MegaClient::pw_key(const char* utf8pw, byte* key) const
{
    int t;
    char* pw;

    if (!(pw = str_to_a32(utf8pw, &t)))
    {
        return API_EARGS;
    }

    int n = (t + 15) / 16;
    SymmCipher* keys = new SymmCipher[n];

    for (int i = 0; i < n; i++)
    {
        int valid = (i != (n - 1)) ? SymmCipher::BLOCKSIZE : (t - SymmCipher::BLOCKSIZE * i);
        memcpy(key, pw + i * SymmCipher::BLOCKSIZE, valid);
        memset(key + valid, 0, SymmCipher::BLOCKSIZE - valid);
        keys[i].setkey(key);
    }

    memcpy(key, "\x93\xC4\x67\xE3\x7D\xB0\xC7\xA4\xD1\xBE\x3F\x81\x01\x52\xCB\x56", SymmCipher::BLOCKSIZE);

    for (int r = 65536; r--; )
    {
        for (int i = 0; i < n; i++)
        {
            keys[i].ecb_encrypt(key);
        }
    }

    delete[] keys;
    delete[] pw;

    return API_OK;
}

// compute generic string hash
void MegaClient::stringhash(const char* s, byte* hash, SymmCipher* cipher)
{
    int t;

    t = strlen(s) & - SymmCipher::BLOCKSIZE;

    strncpy((char*)hash, s + t, SymmCipher::BLOCKSIZE);

    while (t)
    {
        t -= SymmCipher::BLOCKSIZE;
        SymmCipher::xorblock((byte*)s + t, hash);
    }

    for (t = 16384; t--; )
    {
        cipher->ecb_encrypt(hash);
    }

    memcpy(hash + 4, hash + 8, 4);
}

// (transforms s to lowercase)
uint64_t MegaClient::stringhash64(string* s, SymmCipher* c)
{
    byte hash[SymmCipher::KEYLENGTH];

    transform(s->begin(), s->end(), s->begin(), ::tolower);
    stringhash(s->c_str(), hash, c);

    return MemAccess::get<uint64_t>((const char*)hash);
}

// read and add/verify node array
int MegaClient::readnodes(JSON* j, int notify, putsource_t source, NewNode* nn, int nnsize, int tag)
{
    if (!j->enterarray())
    {
        return 0;
    }

    node_vector dp;
    Node* n;

    while (j->enterobject())
    {
        handle h = UNDEF, ph = UNDEF;
        handle u = 0, su = UNDEF;
        nodetype_t t = TYPE_UNKNOWN;
        const char* a = NULL;
        const char* k = NULL;
        const char* fa = NULL;
        const char *sk = NULL;
        accesslevel_t rl = ACCESS_UNKNOWN;
        m_off_t s = NEVER;
        m_time_t ts = -1, sts = -1;
        nameid name;
        int nni = -1;

        while ((name = j->getnameid()) != EOO)
        {
            switch (name)
            {
                case 'h':   // new node: handle
                    h = j->gethandle();
                    break;

                case 'p':   // parent node
                    ph = j->gethandle();
                    break;

                case 'u':   // owner user
                    u = j->gethandle(USERHANDLE);
                    break;

                case 't':   // type
                    t = (nodetype_t)j->getint();
                    break;

                case 'a':   // attributes
                    a = j->getvalue();
                    break;

                case 'k':   // key(s)
                    k = j->getvalue();
                    break;

                case 's':   // file size
                    s = j->getint();
                    break;

                case 'i':   // related source NewNode index
                    nni = j->getint();
                    break;

                case MAKENAMEID2('t', 's'):  // actual creation timestamp
                    ts = j->getint();
                    break;

                case MAKENAMEID2('f', 'a'):  // file attributes
                    fa = j->getvalue();
                    break;

                    // inbound share attributes
                case 'r':   // share access level
                    rl = (accesslevel_t)j->getint();
                    break;

                case MAKENAMEID2('s', 'k'):  // share key
                    sk = j->getvalue();
                    break;

                case MAKENAMEID2('s', 'u'):  // sharing user
                    su = j->gethandle(USERHANDLE);
                    break;

                case MAKENAMEID3('s', 't', 's'):  // share timestamp
                    sts = j->getint();
                    break;

                default:
                    if (!j->storeobject())
                    {
                        return 0;
                    }
            }
        }

        if (ISUNDEF(h))
        {
            warn("Missing node handle");
        }
        else
        {
            if (t == TYPE_UNKNOWN)
            {
                warn("Unknown node type");
            }
            else if (t == FILENODE || t == FOLDERNODE)
            {
                if (ISUNDEF(ph))
                {
                    warn("Missing parent");
                }
                else if (!a)
                {
                    warn("Missing node attributes");
                }
                else if (!k)
                {
                    warn("Missing node key");
                }

                if (t == FILENODE && ISUNDEF(s))
                {
                    warn("File node without file size");
                }
            }
        }

        if (fa && t != FILENODE)
        {
            warn("Spurious file attributes");
        }

        if (!warnlevel())
        {
            if ((n = nodebyhandle(h)))
            {
                Node* p = NULL;
                if (!ISUNDEF(ph))
                {
                    p = nodebyhandle(ph);
                }

                if (n->changed.removed)
                {
                    // node marked for deletion is being resurrected, possibly
                    // with a new parent (server-client move operation)
                    n->changed.removed = false;
                }
                else
                {
                    // node already present - check for race condition
                    if ((n->parent && ph != n->parent->nodehandle && p &&  p->type != FILENODE) || n->type != t)
                    {
                        app->reload("Node inconsistency");

                        static bool reloadnotified = false;
                        if (!reloadnotified)
                        {
                            int creqtag = reqtag;
                            reqtag = 0;
                            sendevent(99437, "Node inconsistency");
                            reqtag = creqtag;
                            reloadnotified = true;
                        }
                    }
                }

                if (!ISUNDEF(ph))
                {
                    if (p)
                    {
                        n->setparent(p);
                        n->changed.parent = true;
                    }
                    else
                    {
                        n->setparent(NULL);
                        n->parenthandle = ph;
                        dp.push_back(n);
                    }
                }

                if (a && k && n->attrstring)
                {
                    LOG_warn << "Updating the key of a NO_KEY node";
                    Node::copystring(n->attrstring, a);
                    Node::copystring(&n->nodekey, k);
                }
            }
            else
            {
                byte buf[SymmCipher::KEYLENGTH];

                if (!ISUNDEF(su))
                {
                    if (t != FOLDERNODE)
                    {
                        warn("Invalid share node type");
                    }

                    if (rl == ACCESS_UNKNOWN)
                    {
                        warn("Missing access level");
                    }

                    if (!sk)
                    {
                        LOG_warn << "Missing share key for inbound share";
                    }

                    if (warnlevel())
                    {
                        su = UNDEF;
                    }
                    else
                    {
                        if (sk)
                        {
                            decryptkey(sk, buf, sizeof buf, &key, 1, h);
                        }
                    }
                }

                string fas;

                Node::copystring(&fas, fa);

                // fallback timestamps
                if (!(ts + 1))
                {
                    ts = time(NULL);
                }

                if (!(sts + 1))
                {
                    sts = ts;
                }

                n = new Node(this, &dp, h, ph, t, s, u, fas.c_str(), ts);

                n->tag = tag;

                n->attrstring = new string;
                Node::copystring(n->attrstring, a);
                Node::copystring(&n->nodekey, k);

                if (!ISUNDEF(su))
                {
                    newshares.push_back(new NewShare(h, 0, su, rl, sts, sk ? buf : NULL));
                }

                if (nn && nni >= 0 && nni < nnsize)
                {
                    nn[nni].added = true;

#ifdef ENABLE_SYNC
                    if (source == PUTNODES_SYNC)
                    {
                        if (nn[nni].localnode)
                        {
                            // overwrites/updates: associate LocalNode with newly created Node
                            nn[nni].localnode->setnode(n);
                            nn[nni].localnode->newnode = NULL;
                            nn[nni].localnode->treestate(TREESTATE_SYNCED);

                            // updates cache with the new node associated
                            nn[nni].localnode->sync->statecacheadd(nn[nni].localnode);
                        }
                    }
#endif

                    if (nn[nni].source == NEW_UPLOAD)
                    {
                        handle uh = nn[nni].uploadhandle;

                        // do we have pending file attributes for this upload? set them.
                        for (fa_map::iterator it = pendingfa.lower_bound(pair<handle, fatype>(uh, 0));
                             it != pendingfa.end() && it->first.first == uh; )
                        {
                            reqs.add(new CommandAttachFA(h, it->first.second, it->second.first, it->second.second));
                            pendingfa.erase(it++);
                        }

                        // FIXME: only do this for in-flight FA writes
                        uhnh.insert(pair<handle, handle>(uh, h));
                    }
                }
            }

            if (notify)
            {
                notifynode(n);
            }
        }
    }

    // any child nodes that arrived before their parents?
    for (int i = dp.size(); i--; )
    {
        if ((n = nodebyhandle(dp[i]->parenthandle)))
        {
            dp[i]->setparent(n);
        }
    }

    return j->leavearray();
}

// decrypt and set encrypted sharekey
void MegaClient::setkey(SymmCipher* c, const char* k)
{
    byte newkey[SymmCipher::KEYLENGTH];

    if (Base64::atob(k, newkey, sizeof newkey) == sizeof newkey)
    {
        key.ecb_decrypt(newkey);
        c->setkey(newkey);
    }
}

// read outbound share keys
void MegaClient::readok(JSON* j)
{
    if (j->enterarray())
    {
        while (j->enterobject())
        {
            readokelement(j);
        }

        j->leavearray();

        mergenewshares(0);
    }
}

// - h/ha/k (outbound sharekeys, always symmetric)
void MegaClient::readokelement(JSON* j)
{
    handle h = UNDEF;
    byte ha[SymmCipher::BLOCKSIZE];
    byte buf[SymmCipher::BLOCKSIZE];
    int have_ha = 0;
    const char* k = NULL;

    for (;;)
    {
        switch (j->getnameid())
        {
            case 'h':
                h = j->gethandle();
                break;

            case MAKENAMEID2('h', 'a'):      // share authentication tag
                have_ha = Base64::atob(j->getvalue(), ha, sizeof ha) == sizeof ha;
                break;

            case 'k':           // share key(s)
                k = j->getvalue();
                break;

            case EOO:
                if (ISUNDEF(h))
                {
                    LOG_warn << "Missing outgoing share handle in ok element";
                    return;
                }

                if (!k)
                {
                    LOG_warn << "Missing outgoing share key in ok element";
                    return;
                }

                if (!have_ha)
                {
                    LOG_warn << "Missing outbound share signature";
                    return;
                }

                if (decryptkey(k, buf, SymmCipher::KEYLENGTH, &key, 1, h))
                {
                    newshares.push_back(new NewShare(h, 1, UNDEF, ACCESS_UNKNOWN, 0, buf, ha));
                }
                return;

            default:
                if (!j->storeobject())
                {
                    return;
                }
        }
    }
}

// read outbound shares and pending shares
void MegaClient::readoutshares(JSON* j)
{
    if (j->enterarray())
    {
        while (j->enterobject())
        {
            readoutshareelement(j);
        }

        j->leavearray();

        mergenewshares(0);
    }
}

// - h/u/r/ts/p (outbound share or pending share)
void MegaClient::readoutshareelement(JSON* j)
{
    handle h = UNDEF;
    handle uh = UNDEF;
    handle p = UNDEF;
    accesslevel_t r = ACCESS_UNKNOWN;
    m_time_t ts = 0;

    for (;;)
    {
        switch (j->getnameid())
        {
            case 'h':
                h = j->gethandle();
                break;

            case 'p':
                p = j->gethandle(PCRHANDLE);
                break;

            case 'u':           // share target user
                uh = j->is(EXPORTEDLINK) ? 0 : j->gethandle(USERHANDLE);
                break;

            case 'r':           // access
                r = (accesslevel_t)j->getint();
                break;

            case MAKENAMEID2('t', 's'):      // timestamp
                ts = j->getint();
                break;

            case EOO:
                if (ISUNDEF(h))
                {
                    LOG_warn << "Missing outgoing share node";
                    return;
                }

                if (ISUNDEF(uh) && ISUNDEF(p))
                {
                    LOG_warn << "Missing outgoing share user";
                    return;
                }

                if (r == ACCESS_UNKNOWN)
                {
                    LOG_warn << "Missing outgoing share access";
                    return;
                }

                newshares.push_back(new NewShare(h, 1, uh, r, ts, NULL, NULL, p));
                return;

            default:
                if (!j->storeobject())
                {
                    return;
                }
        }
    }
}

void MegaClient::readipc(JSON *j)
{
    // fields: ps, m, ts, uts, msg, p
    if (j->enterarray())
    {
        while (j->enterobject())
        {
            m_time_t ts = 0;
            m_time_t uts = 0;
            int ps = 0;
            const char *m = NULL;
            const char *msg = NULL;
            handle p = UNDEF;

            bool done = false;
            while (!done)
            {
                switch (j->getnameid()) {
                    case MAKENAMEID2('p', 's'):
                        ps = j->getint();
                        break;
                    case 'm':
                        m = j->getvalue();
                        break;
                    case MAKENAMEID2('t', 's'):
                        ts = j->getint();
                        break;
                    case MAKENAMEID3('u', 't', 's'):
                        uts = j->getint();
                        break;
                    case MAKENAMEID3('m', 's', 'g'):
                        msg = j->getvalue();
                        break;
                    case 'p':
                        p = j->gethandle(MegaClient::PCRHANDLE);
                        break;
                    case EOO:
                        done = true;
                        if (ISUNDEF(p))
                        {
                            LOG_err << "p element not provided";
                            break;
                        }
                        if (!m)
                        {
                            LOG_err << "m element not provided";
                            break;
                        }
                        if (ts == 0)
                        {
                            LOG_err << "ts element not provided";
                            break;
                        }
                        if (uts == 0)
                        {
                            LOG_err << "uts element not provided";
                            break;
                        }

                        if (pcrindex[p] != NULL)
                        {
                            pcrindex[p]->update(m, NULL, ts, uts, msg, false);                        
                        } 
                        else
                        {
                            pcrindex[p] = new PendingContactRequest(p, m, NULL, ts, uts, msg, false);
                        }                       

                        break;
                    default:
                       if (!j->storeobject())
                       {
                            return;
                       }
                }
            }
        }

        j->leavearray();
    }
}

void MegaClient::readopc(JSON *j)
{
    // fields: e, m, ts, uts, rts, msg, p
    if (j->enterarray())
    {
        while (j->enterobject())
        {
            m_time_t ts = 0;
            m_time_t uts = 0;
            m_time_t rts = 0;
            const char *e = NULL;
            const char *m = NULL;
            const char *msg = NULL;
            handle p = UNDEF;

            bool done = false;
            while (!done)
            {
                switch (j->getnameid())
                {
                    case 'e':
                        e = j->getvalue();
                        break;
                    case 'm':
                        m = j->getvalue();
                        break;
                    case MAKENAMEID2('t', 's'):
                        ts = j->getint();
                        break;
                    case MAKENAMEID3('u', 't', 's'):
                        uts = j->getint();
                        break;
                    case MAKENAMEID3('r', 't', 's'):
                        rts = j->getint();
                        break;
                    case MAKENAMEID3('m', 's', 'g'):
                        msg = j->getvalue();
                        break;
                    case 'p':
                        p = j->gethandle(MegaClient::PCRHANDLE);
                        break;
                    case EOO:
                        done = true;
                        if (!e)
                        {
                            LOG_err << "e element not provided";
                            break;
                        }
                        if (!m)
                        {
                            LOG_err << "m element not provided";
                            break;
                        }
                        if (ts == 0)
                        {
                            LOG_err << "ts element not provided";
                            break;
                        }
                        if (uts == 0)
                        {
                            LOG_err << "uts element not provided";
                            break;
                        }

                        if (pcrindex[p] != NULL)
                        {
                            pcrindex[p]->update(e, m, ts, uts, msg, true);                        
                        } 
                        else
                        {
                            pcrindex[p] = new PendingContactRequest(p, e, m, ts, uts, msg, true);
                        }

                        break;
                    default:
                       if (!j->storeobject())
                       {
                            return;
                       }
                }
            }
        }

        j->leavearray();
    }
}

void MegaClient::procph(JSON *j)
{
    // fields: h, ph, ets
    if (j->enterarray())
    {
        while (j->enterobject())
        {
            handle h = UNDEF;
            handle ph = UNDEF;
            m_time_t ets = 0;
            Node *n = NULL;
            bool takendown = false;

            bool done = false;
            while (!done)
            {
                switch (j->getnameid())
                {
                    case 'h':
                        h = j->gethandle(MegaClient::NODEHANDLE);
                        break;
                    case MAKENAMEID2('p','h'):
                        ph = j->gethandle(MegaClient::NODEHANDLE);
                        break;
                    case MAKENAMEID3('e', 't', 's'):
                        ets = j->getint();
                        break;
                    case MAKENAMEID4('d','o','w','n'):
                        takendown = (j->getint() == 1);
                        break;
                    case EOO:
                        done = true;
                        if (ISUNDEF(h))
                        {
                            LOG_err << "h element not provided";
                            break;
                        }
                        if (ISUNDEF(ph))
                        {
                            LOG_err << "ph element not provided";
                            break;
                        }

                        n = nodebyhandle(h);
                        if (n)
                        {
                            n->setpubliclink(ph, ets, takendown);
                        }
                        else
                        {
                            LOG_warn << "node for public link not found";
                        }

                        break;
                    default:
                       if (!j->storeobject())
                       {
                            return;
                       }
                }
            }
        }

        j->leavearray();
    }
}

int MegaClient::applykeys()
{
    int t = 0;

    // FIXME: rather than iterating through the whole node set, maintain subset
    // with missing keys
    for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++)
    {
        if (it->second->applykey())
        {
            t++;
        }
    }

    if (sharekeyrewrite.size())
    {
        reqs.add(new CommandShareKeyUpdate(this, &sharekeyrewrite));
        sharekeyrewrite.clear();
    }

    if (nodekeyrewrite.size())
    {
        reqs.add(new CommandNodeKeyUpdate(this, &nodekeyrewrite));
        nodekeyrewrite.clear();
    }

    return t;
}

// user/contact list
bool MegaClient::readusers(JSON* j)
{
    if (!j->enterarray())
    {
        return 0;
    }

    while (j->enterobject())
    {
        handle uh = 0;
        visibility_t v = VISIBILITY_UNKNOWN;    // new share objects do not override existing visibility
        m_time_t ts = 0;
        const char* m = NULL;
        nameid name;

        while ((name = j->getnameid()) != EOO)
        {
            switch (name)
            {
                case 'u':   // new node: handle
                    uh = j->gethandle(USERHANDLE);
                    break;

                case 'c':   // visibility
                    v = (visibility_t)j->getint();
                    break;

                case 'm':   // attributes
                    m = j->getvalue();
                    break;

                case MAKENAMEID2('t', 's'):
                    ts = j->getint();
                    break;

                default:
                    if (!j->storeobject())
                    {
                        return false;
                    }
            }
        }

        if (ISUNDEF(uh))
        {
            warn("Missing contact user handle");
        }

        if (!m)
        {
            warn("Unknown contact user e-mail address");
        }

        if (!warnlevel())
        {
            User* u = finduser(uh, 0);
            bool notify = !u;
            if (u || (u = finduser(uh, 1)))
            {
                mapuser(uh, m);

                if (v != VISIBILITY_UNKNOWN)
                {
                    if (u->show != v || u->ctime != ts)
                    {
                        u->set(v, ts);
                        notify = true;
                    }
                }

                if (notify)
                {
                    notifyuser(u);
                }
            }
        }
    }

    return j->leavearray();
}

error MegaClient::folderaccess(const char *folderlink)
{
    // structure of public folder links: https://mega.nz/#F!<handle>!<key>

    const char* ptr;
    if (!((ptr = strstr(folderlink, "#F!")) && (strlen(ptr)>=11)))
    {
        return API_EARGS;
    }

    const char *f = ptr + 3;
    ptr += 11;

    if (*ptr == '\0')    // no key provided, link is incomplete
    {
        return API_EINCOMPLETE;
    }
    else if (*ptr != '!')
    {
        return API_EARGS;
    }

    const char *k = ptr + 1;

    handle h = 0;
    byte folderkey[SymmCipher::KEYLENGTH];

    locallogout();

    if (Base64::atob(f, (byte*)&h, NODEHANDLE) != NODEHANDLE)
    {
        return API_EARGS;
    }

    if (Base64::atob(k, folderkey, sizeof folderkey) != sizeof folderkey)
    {
        return API_EARGS;
    }

    setrootnode(h);
    key.setkey(folderkey);

    return API_OK;
}

// create new session
void MegaClient::login(const char* email, const byte* pwkey)
{
    locallogout();

    string lcemail(email);

    key.setkey((byte*)pwkey);

    uint64_t emailhash = stringhash64(&lcemail, &key);

    byte sek[SymmCipher::KEYLENGTH];
    PrnGen::genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, email, emailhash, sek));
}

void MegaClient::fastlogin(const char* email, const byte* pwkey, uint64_t emailhash)
{
    locallogout();

    key.setkey((byte*)pwkey);

    byte sek[SymmCipher::KEYLENGTH];
    PrnGen::genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, email, emailhash, sek));
}

void MegaClient::getuserdata()
{
    reqs.add(new CommandGetUserData(this));
}

void MegaClient::getpubkey(const char *user)
{
    queuepubkeyreq(user, new PubKeyActionNotifyApp(reqtag));
}

// resume session - load state from local cache, if available
void MegaClient::login(const byte* session, int size)
{
    locallogout();
   
    int sessionversion = 0;
    if (size == sizeof key.key + SIDLEN + 1)
    {
        sessionversion = session[0];

        if (sessionversion != 1)
        {
            restag = reqtag;
            app->login_result(API_EARGS);
            return;
        }

        session++;
        size--;
    }

    if (size == sizeof key.key + SIDLEN)
    {
        string t;

        key.setkey(session);
        setsid(session + sizeof key.key, size - sizeof key.key);

        opensctable();

        if (sctable && sctable->get(CACHEDSCSN, &t) && t.size() == sizeof cachedscsn)
        {
            cachedscsn = MemAccess::get<handle>(t.data());
        }

        byte sek[SymmCipher::KEYLENGTH];
        PrnGen::genblock(sek, sizeof sek);

        reqs.add(new CommandLogin(this, NULL, UNDEF, sek, sessionversion));
    }
    else
    {
        restag = reqtag;
        app->login_result(API_EARGS);
    }
}

// check password's integrity
error MegaClient::validatepwd(const byte *pwkey)
{
    User *u = finduser(me);
    if (!u)
    {
        return API_EACCESS;
    }

    SymmCipher pwcipher(pwkey);
    pwcipher.setkey((byte*)pwkey);

    string lcemail(u->email.c_str());
    uint64_t emailhash = stringhash64(&lcemail, &pwcipher);

    reqs.add(new CommandValidatePassword(this, lcemail.c_str(), emailhash));

    return API_OK;
}

int MegaClient::dumpsession(byte* session, size_t size)
{
    if (loggedin() == NOTLOGGEDIN)
    {
        return 0;
    }

    if (size < sid.size() + sizeof key.key)
    {
        return API_ERANGE;
    }

    if (sessionkey.size())
    {
        if (size < sid.size() + sizeof key.key + 1)
        {
            return API_ERANGE;
        }

        size = sid.size() + sizeof key.key + 1;

        session[0] = 1;
        session++;

        byte k[SymmCipher::KEYLENGTH];
        SymmCipher cipher;
        cipher.setkey((const byte *)sessionkey.data(), sessionkey.size());
        cipher.ecb_encrypt(key.key, k);
        memcpy(session, k, sizeof k);
    }
    else
    {
        size = sid.size() + sizeof key.key;
        memcpy(session, key.key, sizeof key.key);
    }

    memcpy(session + sizeof key.key, sid.data(), sid.size());
    
    return size;
}

void MegaClient::copysession()
{
    reqs.add(new CommandCopySession(this));
}

string *MegaClient::sessiontransferdata(const char *url, string *session)
{
    if (loggedin() != FULLACCOUNT)
    {
        return NULL;
    }

    std::stringstream ss;

    // open array
    ss << "[";

    // add AES key
    string aeskey;
    key.serializekeyforjs(&aeskey);
    ss << aeskey << ",\"";

    // add session ID
    if (session)
    {
        ss << *session;
    }
    else
    {
        string sids;
        sids.resize(sid.size() * 4 / 3 + 4);
        sids.resize(Base64::btoa((byte *)sid.data(), sid.size(), (char *)sids.data()));
        ss << sids;
    }
    ss << "\",\"";

    // add URL
    if (url)
    {
        ss << url;
    }
    ss << "\",false]";

    // standard Base64 encoding
    string json = ss.str();
    string *base64 = new string;
    base64->resize(json.size() * 4 / 3 + 4);
    base64->resize(Base64::btoa((byte *)json.data(), json.size(), (char *)base64->data()));
    std::replace(base64->begin(), base64->end(), '-', '+');
    std::replace(base64->begin(), base64->end(), '_', '/');
    return base64;
}

void MegaClient::killsession(handle session)
{
    reqs.add(new CommandKillSessions(this, session));
}

// Kill all sessions (except current)
void MegaClient::killallsessions()
{
    reqs.add(new CommandKillSessions(this));
}

void MegaClient::opensctable()
{
    if (dbaccess && !sctable)
    {
        string dbname;

        if (sid.size() >= SIDLEN)
        {
            dbname.resize((SIDLEN - sizeof key.key) * 4 / 3 + 3);
            dbname.resize(Base64::btoa((const byte*)sid.data() + sizeof key.key, SIDLEN - sizeof key.key, (char*)dbname.c_str()));
        }
        else if (publichandle != UNDEF)
        {
            dbname.resize(NODEHANDLE * 4 / 3 + 3);
            dbname.resize(Base64::btoa((const byte*)&publichandle, NODEHANDLE, (char*)dbname.c_str()));
        }

        if (dbname.size())
        {
            sctable = dbaccess->open(fsaccess, &dbname);
            pendingsccommit = false;
        }
    }
}

// verify a static symmetric password challenge
int MegaClient::checktsid(byte* sidbuf, unsigned len)
{
    if (len != SIDLEN)
    {
        return 0;
    }

    key.ecb_encrypt(sidbuf);

    return !memcmp(sidbuf, sidbuf + SIDLEN - SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH);
}

// locate user by e-mail address or ASCII handle
User* MegaClient::finduser(const char* uid, int add)
{
    // null user for folder links?
    if (!uid || !*uid)
    {
        return NULL;
    }

    if (!strchr(uid, '@'))
    {
        // not an e-mail address: must be ASCII handle
        handle uh;

        if (Base64::atob(uid, (byte*)&uh, sizeof uh) == sizeof uh)
        {
            return finduser(uh, add);
        }

        return NULL;
    }

    string nuid;
    User* u;

    // convert e-mail address to lowercase (ASCII only)
    Node::copystring(&nuid, uid);
    transform(nuid.begin(), nuid.end(), nuid.begin(), ::tolower);

    um_map::iterator it = umindex.find(nuid);

    if (it == umindex.end())
    {
        if (!add)
        {
            return NULL;
        }

        // add user by lowercase e-mail address
        u = &users[++userid];
        u->uid = nuid;
        Node::copystring(&u->email, nuid.c_str());
        umindex[nuid] = userid;

        return u;
    }
    else
    {
        return &users[it->second];
    }
}

// locate user by binary handle
User* MegaClient::finduser(handle uh, int add)
{
    if (!uh)
    {
        return NULL;
    }

    User* u;
    uh_map::iterator it = uhindex.find(uh);

    if (it == uhindex.end())
    {
        if (!add)
        {
            return NULL;
        }

        // add user by binary handle
        u = &users[++userid];

        char uid[12];
        Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
        u->uid.assign(uid, 11);

        uhindex[uh] = userid;
        u->userhandle = uh;

        return u;
    }
    else
    {
        return &users[it->second];
    }
}

User *MegaClient::ownuser()
{
    return finduser(me);
}

// add missing mapping (handle or email)
// reduce uid to ASCII uh if only known by email
void MegaClient::mapuser(handle uh, const char* email)
{
    if (!email || !*email)
    {
        return;
    }

    User* u;
    string nuid;

    Node::copystring(&nuid, email);
    transform(nuid.begin(), nuid.end(), nuid.begin(), ::tolower);

    // does user uh exist?
    uh_map::iterator hit = uhindex.find(uh);

    if (hit != uhindex.end())
    {
        // yes: add email reference
        u = &users[hit->second];

        um_map::iterator mit = umindex.find(nuid);
        if (mit != umindex.end() && mit->second != hit->second && (users[mit->second].show != INACTIVE || users[mit->second].userhandle == me))
        {
            // duplicated user: one by email, one by handle
            discardnotifieduser(&users[mit->second]);
            assert(!users[mit->second].sharing.size());
            users.erase(mit->second);
        }

        // if mapping a different email, remove old index
        if (strcmp(u->email.c_str(), nuid.c_str()))
        {
            if (u->email.size())
            {
                umindex.erase(u->email);
            }

            Node::copystring(&u->email, nuid.c_str());
        }

        umindex[nuid] = hit->second;

        return;
    }

    // does user email exist?
    um_map::iterator mit = umindex.find(nuid);

    if (mit != umindex.end())
    {
        // yes: add uh reference
        u = &users[mit->second];

        uhindex[uh] = mit->second;
        u->userhandle = uh;

        char uid[12];
        Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
        u->uid.assign(uid, 11);
    }
}

void MegaClient::discarduser(handle uh)
{
    User *u = finduser(uh);
    if (!u)
    {
        return;
    }

    while (u->pkrs.size())  // protect any pending pubKey request
    {
        PubKeyAction *pka = u->pkrs[0];
        if(pka->cmd)
        {
            pka->cmd->invalidateUser();
        }
        pka->proc(this, u);
        delete pka;
        u->pkrs.pop_front();
    }

    discardnotifieduser(u);

    umindex.erase(u->email);
    users.erase(uhindex[uh]);
    uhindex.erase(uh);
}

void MegaClient::discarduser(const char *email)
{
    User *u = finduser(email);
    if (!u)
    {
        return;
    }

    while (u->pkrs.size())  // protect any pending pubKey request
    {
        PubKeyAction *pka = u->pkrs[0];
        if(pka->cmd)
        {
            pka->cmd->invalidateUser();
        }
        pka->proc(this, u);
        delete pka;
        u->pkrs.pop_front();
    }

    discardnotifieduser(u);

    uhindex.erase(u->userhandle);
    users.erase(umindex[email]);
    umindex.erase(email);
}

PendingContactRequest* MegaClient::findpcr(handle p)
{
    if (ISUNDEF(p))
    {
        return NULL;
    }

    PendingContactRequest* pcr = pcrindex[p];
    if (!pcr)
    {
        pcr = new PendingContactRequest(p);
        pcrindex[p] = pcr;
    }

    return pcrindex[p];
}

void MegaClient::mappcr(handle id, PendingContactRequest *pcr)
{
    delete pcrindex[id];
    pcrindex[id] = pcr;
}

bool MegaClient::discardnotifieduser(User *u)
{
    for (user_vector::iterator it = usernotify.begin(); it != usernotify.end(); it++)
    {
        if (*it == u)
        {
            usernotify.erase(it);
            return true;  // no duplicated users in the notify vector
        }
    }
    return false;
}

// sharekey distribution request - walk array consisting of {node,user+}+ handle tuples
// and submit public key requests
void MegaClient::procsr(JSON* j)
{
    User* u;
    handle sh, uh;

    if (!j->enterarray())
    {
        return;
    }

    while (j->ishandle() && (sh = j->gethandle()))
    {
        if (nodebyhandle(sh))
        {
            // process pending requests
            while (j->ishandle(USERHANDLE) && (uh = j->gethandle(USERHANDLE)))
            {
                if ((u = finduser(uh)))
                {
                    queuepubkeyreq(u, new PubKeyActionSendShareKey(sh));
                }
            }
        }
        else
        {
            // unknown node: skip
            while (j->ishandle(USERHANDLE) && (uh = j->gethandle(USERHANDLE)));
        }
    }

    j->leavearray();
}

#ifdef ENABLE_CHAT
void MegaClient::clearKeys()
{
    User *u = finduser(me);

    u->invalidateattr(ATTR_KEYRING);
    u->invalidateattr(ATTR_ED25519_PUBK);
    u->invalidateattr(ATTR_CU25519_PUBK);
    u->invalidateattr(ATTR_SIG_RSA_PUBK);
    u->invalidateattr(ATTR_SIG_CU255_PUBK);

    fetchingkeys = false;
}

void MegaClient::resetKeyring()
{
    delete signkey;
    signkey = NULL;

    delete chatkey;
    chatkey = NULL;
}
#endif

// process node tree (bottom up)
void MegaClient::proctree(Node* n, TreeProc* tp, bool skipinshares, bool skipversions)
{
    if (!skipversions || n->type != FILENODE)
    {
        for (node_list::iterator it = n->children.begin(); it != n->children.end(); )
        {
            Node *child = *it++;
            if (!(skipinshares && child->inshare))
            {
                proctree(child, tp, skipinshares);
            }
        }
    }

    tp->proc(this, n);
}

// queue PubKeyAction request to be triggered upon availability of the user's
// public key
void MegaClient::queuepubkeyreq(User* u, PubKeyAction* pka)
{
    if (!u || u->pubk.isvalid())
    {
        restag = pka->tag;
        pka->proc(this, u);
        delete pka;
    }
    else
    {
        u->pkrs.push_back(pka);

        if (!u->pubkrequested)
        {
            pka->cmd = new CommandPubKeyRequest(this, u);
            reqs.add(pka->cmd);
            u->pubkrequested = true;
        }
    }
}

void MegaClient::queuepubkeyreq(const char *uid, PubKeyAction *pka)
{
    User *u = finduser(uid, 0);
    if (!u && uid)
    {
        if (strchr(uid, '@'))   // uid is an e-mail address
        {
            string nuid;
            Node::copystring(&nuid, uid);
            transform(nuid.begin(), nuid.end(), nuid.begin(), ::tolower);

            u = new User(nuid.c_str());
            u->uid = nuid;
            u->isTemporary = true;
        }
        else    // not an e-mail address: must be ASCII handle
        {
            handle uh;
            if (Base64::atob(uid, (byte*)&uh, sizeof uh) == sizeof uh)
            {
                u = new User(NULL);
                u->userhandle = uh;
                u->uid = uid;
                u->isTemporary = true;
            }
        }
    }

    queuepubkeyreq(u, pka);
}

// rewrite keys of foreign nodes due to loss of underlying shareufskey
void MegaClient::rewriteforeignkeys(Node* n)
{
    TreeProcForeignKeys rewrite;
    proctree(n, &rewrite);

    if (nodekeyrewrite.size())
    {
        reqs.add(new CommandNodeKeyUpdate(this, &nodekeyrewrite));
        nodekeyrewrite.clear();
    }
}

// if user has a known public key, complete instantly
// otherwise, queue and request public key if not already pending
void MegaClient::setshare(Node* n, const char* user, accesslevel_t a, const char* personal_representation)
{
    int total = n->outshares ? n->outshares->size() : 0;
    total += n->pendingshares ? n->pendingshares->size() : 0;
    if (a == ACCESS_UNKNOWN && total == 1)
    {
        // rewrite keys of foreign nodes located in the outbound share that is getting canceled
        // FIXME: verify that it is really getting canceled to prevent benign premature rewrite
        rewriteforeignkeys(n);
    }

    queuepubkeyreq(user, new PubKeyActionCreateShare(n->nodehandle, a, reqtag, personal_representation));
}

// Add/delete/remind outgoing pending contact request
void MegaClient::setpcr(const char* temail, opcactions_t action, const char* msg, const char* oemail)
{
    reqs.add(new CommandSetPendingContact(this, temail, action, msg, oemail));
}

void MegaClient::updatepcr(handle p, ipcactions_t action)
{
    reqs.add(new CommandUpdatePendingContact(this, p, action));
}

// enumerate Pro account purchase options (not fully implemented)
void MegaClient::purchase_enumeratequotaitems()
{
    reqs.add(new CommandEnumerateQuotaItems(this));
}

// begin a new purchase (FIXME: not fully implemented)
void MegaClient::purchase_begin()
{
    purchase_basket.clear();
}

// submit purchased product for payment
void MegaClient::purchase_additem(int itemclass, handle item, unsigned price,
                                  const char* currency, unsigned tax, const char* country,
                                  const char* affiliate)
{
    reqs.add(new CommandPurchaseAddItem(this, itemclass, item, price, currency, tax, country, affiliate));
}

// obtain payment URL for given provider
void MegaClient::purchase_checkout(int gateway)
{
    reqs.add(new CommandPurchaseCheckout(this, gateway));
}

void MegaClient::submitpurchasereceipt(int type, const char *receipt)
{
    reqs.add(new CommandSubmitPurchaseReceipt(this, type, receipt));
}

error MegaClient::creditcardstore(const char *ccplain)
{
    if (!ccplain)
    {
        return API_EARGS;
    }

    string ccnumber, expm, expy, cv2, ccode;
    if (!JSON::extractstringvalue(ccplain, "card_number", &ccnumber)
        || (ccnumber.size() < 10)
        || !JSON::extractstringvalue(ccplain, "expiry_date_month", &expm)
        || (expm.size() != 2)
        || !JSON::extractstringvalue(ccplain, "expiry_date_year", &expy)
        || (expy.size() != 4)
        || !JSON::extractstringvalue(ccplain, "cv2", &cv2)
        || (cv2.size() != 3)
        || !JSON::extractstringvalue(ccplain, "country_code", &ccode)
        || (ccode.size() != 2))
    {
        return API_EARGS;
    }

    string::iterator it = find_if(ccnumber.begin(), ccnumber.end(), not1(ptr_fun(static_cast<int(*)(int)>(isdigit))));
    if (it != ccnumber.end())
    {
        return API_EARGS;
    }

    it = find_if(expm.begin(), expm.end(), not1(ptr_fun(static_cast<int(*)(int)>(isdigit))));
    if (it != expm.end() || atol(expm.c_str()) > 12)
    {
        return API_EARGS;
    }

    it = find_if(expy.begin(), expy.end(), not1(ptr_fun(static_cast<int(*)(int)>(isdigit))));
    if (it != expy.end() || atol(expy.c_str()) < 2015)
    {
        return API_EARGS;
    }

    it = find_if(cv2.begin(), cv2.end(), not1(ptr_fun(static_cast<int(*)(int)>(isdigit))));
    if (it != cv2.end())
    {
        return API_EARGS;
    }


    //Luhn algorithm
    int odd = 1, sum = 0;
    for (int i = ccnumber.size(); i--; odd = !odd)
    {
        int digit = ccnumber[i] - '0';
        sum += odd ? digit : ((digit < 5) ? 2 * digit : 2 * (digit - 5) + 1);
    }

    if (sum % 10)
    {
        return API_EARGS;
    }

    byte pubkdata[sizeof(PAYMENT_PUBKEY) * 3 / 4 + 3];
    int pubkdatalen = Base64::atob(PAYMENT_PUBKEY, (byte *)pubkdata, sizeof(pubkdata));

    string ccenc;
    string ccplain1 = ccplain;
    PayCrypter payCrypter;
    if (!payCrypter.hybridEncrypt(&ccplain1, pubkdata, pubkdatalen, &ccenc))
    {
        return API_EARGS;
    }

    string last4 = ccnumber.substr(ccnumber.size() - 4);

    char hashstring[256];
    int ret = snprintf(hashstring, sizeof(hashstring), "{\"card_number\":\"%s\","
            "\"expiry_date_month\":\"%s\","
            "\"expiry_date_year\":\"%s\","
            "\"cv2\":\"%s\"}", ccnumber.c_str(), expm.c_str(), expy.c_str(), cv2.c_str());

    if (ret < 0 || ret >= (int)sizeof(hashstring))
    {
        return API_EARGS;
    }

    HashSHA256 hash;
    string binaryhash;
    hash.add((byte *)hashstring, strlen(hashstring));
    hash.get(&binaryhash);

    static const char hexchars[] = "0123456789abcdef";
    ostringstream oss;
    string hexHash;
    for (size_t i=0;i<binaryhash.size();++i)
    {
        oss.put(hexchars[(binaryhash[i] >> 4) & 0x0F]);
        oss.put(hexchars[binaryhash[i] & 0x0F]);
    }
    hexHash = oss.str();

    string base64cc;
    base64cc.resize(ccenc.size()*4/3+4);
    base64cc.resize(Base64::btoa((byte *)ccenc.data(), ccenc.size(), (char *)base64cc.data()));
    std::replace( base64cc.begin(), base64cc.end(), '-', '+');
    std::replace( base64cc.begin(), base64cc.end(), '_', '/');

    reqs.add(new CommandCreditCardStore(this, base64cc.data(), last4.c_str(), expm.c_str(), expy.c_str(), hexHash.data()));
    return API_OK;
}

void MegaClient::creditcardquerysubscriptions()
{
    reqs.add(new CommandCreditCardQuerySubscriptions(this));
}

void MegaClient::creditcardcancelsubscriptions(const char* reason)
{
    reqs.add(new CommandCreditCardCancelSubscriptions(this, reason));
}

void MegaClient::getpaymentmethods()
{
    reqs.add(new CommandGetPaymentMethods(this));
}

// delete or block an existing contact
error MegaClient::removecontact(const char* email, visibility_t show)
{
    if (!strchr(email, '@') || (show != HIDDEN && show != BLOCKED))
    {
        return API_EARGS;
    }

    reqs.add(new CommandRemoveContact(this, email, show));

    return API_OK;
}

/**
 * @brief Attach/update/delete a user attribute.
 *
 * Attributes are stored as base64-encoded binary blobs. They use internal
 * attribute name prefixes:
 *
 * "*" - Private and encrypted. Use a TLV container (key-value)
 * "#" - Protected and plain text, accessible only by contacts.
 * "+" - Public and plain text, accessible by anyone knowing userhandle
 * "^" - Private and non-encrypted.
 *
 * @param an Attribute name.
 * @param av Attribute value.
 * @param avl Attribute value length.
 * @param ctag Tag to identify the request at intermediate layer
 * @return Void.
 */
void MegaClient::putua(attr_t at, const byte* av, unsigned avl, int ctag)
{
    string data;

    if (!av)
    {
        if (at == ATTR_AVATAR)  // remove avatar
        {
            data = "none";
        }

        av = (const byte*) data.data();
        avl = data.size();
    }

    int tag = (ctag != -1) ? ctag : reqtag;
    User *u = ownuser();
    assert(u);
    if (!u)
    {
        LOG_err << "Own user not found when attempting to set user attributes";
        restag = tag;
        app->putua_result(API_EACCESS);
        return;
    }
    int needversion = u->needversioning(at);
    if (needversion == -1)
    {
        restag = tag;
        app->putua_result(API_EARGS);   // attribute not recognized
        return;
    }

    if (!needversion)
    {
        reqs.add(new CommandPutUA(this, at, av, avl, tag));
    }
    else
    {
        // if the cached value is outdated, first need to fetch the latest version
        if (u->getattr(at) && !u->isattrvalid(at))
        {
            restag = tag;
            app->putua_result(API_EEXPIRED);
            return;
        }
        reqs.add(new CommandPutUAVer(this, at, av, avl, tag));
    }
}

void MegaClient::putua(userattr_map *attrs, int ctag)
{
    int tag = (ctag != -1) ? ctag : reqtag;
    User *u = ownuser();

    if (!u || !attrs || !attrs->size())
    {
        restag = tag;
        return app->putua_result(API_EARGS);
    }

    for (userattr_map::iterator it = attrs->begin(); it != attrs->end(); it++)
    {
        attr_t type = it->first;;

        if (!User::needversioning(type))
        {
            restag = tag;
            return app->putua_result(API_EARGS);
        }

        // if the cached value is outdated, first need to fetch the latest version
        if (u->getattr(type) && !u->isattrvalid(type))
        {
            restag = tag;
            return app->putua_result(API_EEXPIRED);
        }
    }

    reqs.add(new CommandPutMultipleUAVer(this, attrs, tag));
}

/**
 * @brief Queue a user attribute retrieval.
 *
 * @param u User.
 * @param an Attribute name.
 * @param ctag Tag to identify the request at intermediate layer
 * @return Void.
 */
void MegaClient::getua(User* u, const attr_t at, int ctag)
{
    if (at != ATTR_UNKNOWN)
    {
        // if we can solve those requests locally (cached values)...
        const string *cachedav = u->getattr(at);
        int tag = (ctag != -1) ? ctag : reqtag;

#ifdef ENABLE_CHAT
        if (!fetchingkeys && cachedav && u->isattrvalid(at))
#else
        if (cachedav && u->isattrvalid(at))
#endif
        {
            if (User::scope(at) == '*') // private attribute, TLV encoding
            {
                TLVstore *tlv = TLVstore::containerToTLVrecords(cachedav, &key);
                restag = tag;
                app->getua_result(tlv);
                delete tlv;
                return;
            }
            else
            {
                restag = tag;
                app->getua_result((byte*) cachedav->data(), cachedav->size());
                return;
            }
        }
        else
        {
            reqs.add(new CommandGetUA(this, u->uid.c_str(), at, tag));
        }
    }
}

void MegaClient::getua(const char *email_handle, const attr_t at, int ctag)
{
    if (email_handle && at != ATTR_UNKNOWN)
    {
        reqs.add(new CommandGetUA(this, email_handle, at, (ctag != -1) ? ctag : reqtag));
    }
}

void MegaClient::getUserEmail(const char *uid)
{
    reqs.add(new CommandGetUserEmail(this, uid));
}

#ifdef DEBUG
void MegaClient::delua(const char *an)
{
    if (an)
    {
        reqs.add(new CommandDelUA(this, an));
    }
}
#endif

// queue node for notification
void MegaClient::notifynode(Node* n)
{
    n->applykey();

    if (!fetchingnodes)
    {
        if (n->tag && !n->changed.removed && n->attrstring)
        {
            // report a "NO_KEY" event

            char* buf = new char[n->nodekey.size() * 4 / 3 + 4];
            Base64::btoa((byte *)n->nodekey.data(), n->nodekey.size(), buf);

            int changed = 0;
            changed |= (int)n->changed.removed;
            changed |= n->changed.attrs << 1;
            changed |= n->changed.owner << 2;
            changed |= n->changed.ctime << 3;
            changed |= n->changed.fileattrstring << 4;
            changed |= n->changed.inshare << 5;
            changed |= n->changed.outshares << 6;
            changed |= n->changed.parent << 7;
            changed |= n->changed.publiclink << 8;

            int attrlen = n->attrstring->size();
            string base64attrstring;
            base64attrstring.resize(attrlen * 4 / 3 + 4);
            base64attrstring.resize(Base64::btoa((byte *)n->attrstring->data(), n->attrstring->size(), (char *)base64attrstring.data()));

            char report[512];
            Base64::btoa((const byte *)&n->nodehandle, MegaClient::NODEHANDLE, report);
            sprintf(report + 8, " %d %" PRIu64 " %d %X %.200s %.200s", n->type, n->size, attrlen, changed, buf, base64attrstring.c_str());

            int creqtag = reqtag;
            reqtag = 0;
            reportevent("NK", report);
            sendevent(99400, report);
            reqtag = creqtag;

            delete [] buf;
        }

#ifdef ENABLE_SYNC
        // is this a synced node that was moved to a non-synced location? queue for
        // deletion from LocalNodes.
        if (n->localnode && n->localnode->parent && n->parent && !n->parent->localnode)
        {
            if (n->changed.removed || n->changed.parent)
            {
                if (n->type == FOLDERNODE)
                {
                    app->syncupdate_remote_folder_deletion(n->localnode->sync, n);
                }
                else
                {
                    app->syncupdate_remote_file_deletion(n->localnode->sync, n);
                }
            }

            n->localnode->deleted = true;
            n->localnode->node = NULL;
            n->localnode = NULL;
        }
        else
        {
            // is this a synced node that is not a sync root, or a new node in a
            // synced folder?
            // FIXME: aggregate subtrees!
            if (n->localnode && n->localnode->parent)
            {
                n->localnode->deleted = n->changed.removed;
            }

            if (n->parent && n->parent->localnode && (!n->localnode || (n->localnode->parent != n->parent->localnode)))
            {
                if (n->localnode)
                {
                    n->localnode->deleted = n->changed.removed;
                }

                if (!n->changed.removed && n->changed.parent)
                {
                    if (!n->localnode)
                    {
                        if (n->type == FOLDERNODE)
                        {
                            app->syncupdate_remote_folder_addition(n->parent->localnode->sync, n);
                        }
                        else
                        {
                            app->syncupdate_remote_file_addition(n->parent->localnode->sync, n);
                        }
                    }
                    else
                    {
                        app->syncupdate_remote_move(n->localnode->sync, n,
                            n->localnode->parent ? n->localnode->parent->node : NULL);
                    }
                }
            }
            else if (!n->changed.removed && n->changed.attrs && n->localnode && n->localnode->name.compare(n->displayname()))
            {
                app->syncupdate_remote_rename(n->localnode->sync, n, n->localnode->name.c_str());
            }
        }
#endif
    }

    if (!n->notified)
    {
        n->notified = true;
        nodenotify.push_back(n);
    }
}

void MegaClient::transfercacheadd(Transfer *transfer)
{
    if (tctable)
    {
        LOG_debug << "Caching transfer";
        tctable->put(MegaClient::CACHEDTRANSFER, transfer, &tckey);
    }
}

void MegaClient::transfercachedel(Transfer *transfer)
{
    if (tctable && transfer->dbid)
    {
        LOG_debug << "Removing cached transfer";
        tctable->del(transfer->dbid);
    }
}

void MegaClient::filecacheadd(File *file)
{
    if (tctable && !file->syncxfer)
    {
        LOG_debug << "Caching file";
        tctable->put(MegaClient::CACHEDFILE, file, &tckey);
    }
}

void MegaClient::filecachedel(File *file)
{
    if (tctable && !file->syncxfer)
    {
        LOG_debug << "Removing cached file";
        tctable->del(file->dbid);
    }

    if (file->temporaryfile)
    {
        LOG_debug << "Removing temporary file";
        fsaccess->unlinklocal(&file->localname);
    }
}

// queue user for notification
void MegaClient::notifyuser(User* u)
{
    if (!u->notified)
    {
        u->notified = true;
        usernotify.push_back(u);
    }
}

// queue pcr for notification
void MegaClient::notifypcr(PendingContactRequest* pcr)
{
    if (pcr && !pcr->notified)
    {
        pcr->notified = true;
        pcrnotify.push_back(pcr);
    }
}

#ifdef ENABLE_CHAT
void MegaClient::notifychat(TextChat *chat)
{
    if (!chat->notified)
    {
        chat->notified = true;
        chatnotify[chat->id] = chat;
    }
}
#endif

// process request for share node keys
// builds & emits k/cr command
// returns 1 in case of a valid response, 0 otherwise
void MegaClient::proccr(JSON* j)
{
    node_vector shares, nodes;
    handle h;

    if (j->enterobject())
    {
        for (;;)
        {
            switch (j->getnameid())
            {
                case MAKENAMEID3('s', 'n', 'k'):
                    procsnk(j);
                    break;

                case MAKENAMEID3('s', 'u', 'k'):
                    procsuk(j);
                    break;

                case EOO:
                    j->leaveobject();
                    return;

                default:
                    if (!j->storeobject())
                    {
                        return;
                    }
            }
        }

        return;
    }

    if (!j->enterarray())
    {
        LOG_err << "Malformed CR - outer array";
        return;
    }

    if (j->enterarray())
    {
        while (!ISUNDEF(h = j->gethandle()))
        {
            shares.push_back(nodebyhandle(h));
        }

        j->leavearray();

        if (j->enterarray())
        {
            while (!ISUNDEF(h = j->gethandle()))
            {
                nodes.push_back(nodebyhandle(h));
            }

            j->leavearray();
        }
        else
        {
            LOG_err << "Malformed SNK CR - nodes part";
            return;
        }

        if (j->enterarray())
        {
            cr_response(&shares, &nodes, j);
            j->leavearray();
        }
        else
        {
            LOG_err << "Malformed CR - linkage part";
            return;
        }
    }

    j->leavearray();
}

// share nodekey delivery
void MegaClient::procsnk(JSON* j)
{
    if (j->enterarray())
    {
        handle sh, nh;

        while (j->enterarray())
        {
            if (ISUNDEF((sh = j->gethandle())))
            {
                return;
            }

            if (ISUNDEF((nh = j->gethandle())))
            {
                return;
            }

            Node* sn = nodebyhandle(sh);

            if (sn && sn->sharekey && checkaccess(sn, OWNER))
            {
                Node* n = nodebyhandle(nh);

                if (n && n->isbelow(sn))
                {
                    byte keybuf[FILENODEKEYLENGTH];

                    sn->sharekey->ecb_encrypt((byte*)n->nodekey.data(), keybuf, n->nodekey.size());

                    reqs.add(new CommandSingleKeyCR(sh, nh, keybuf, n->nodekey.size()));
                }
            }

            j->leavearray();
        }

        j->leavearray();
    }
}

// share userkey delivery
void MegaClient::procsuk(JSON* j)
{
    if (j->enterarray())
    {
        while (j->enterarray())
        {
            handle sh, uh;

            sh = j->gethandle();

            if (!ISUNDEF(sh))
            {
                uh = j->gethandle();

                if (!ISUNDEF(uh))
                {
                    // FIXME: add support for share user key delivery
                }
            }

            j->leavearray();
        }

        j->leavearray();
    }
}

#ifdef ENABLE_CHAT
void MegaClient::procmcf(JSON *j)
{
    if (j->enterobject() && j->getnameid() == 'c' && j->enterarray())
    {
        while(j->enterobject())   // while there are more chats to read...
        {
            handle chatid = UNDEF;
            privilege_t priv = PRIV_UNKNOWN;
            int shard = -1;
            userpriv_vector *userpriv = NULL;
            bool group = false;
            string title;
            m_time_t ts = -1;

            bool readingChat = true;
            while(readingChat) // read the chat information
            {
                switch (j->getnameid())
                {
                case MAKENAMEID2('i','d'):
                    chatid = j->gethandle(MegaClient::CHATHANDLE);
                    break;

                case 'p':
                    priv = (privilege_t) j->getint();
                    break;

                case MAKENAMEID2('c','s'):
                    shard = j->getint();
                    break;

                case 'u':   // list of users participating in the chat (+privileges)
                    userpriv = readuserpriv(j);
                    break;

                case 'g':
                    group = j->getint();
                    break;

                case MAKENAMEID2('c','t'):
                    j->storeobject(&title);
                    break;

                case MAKENAMEID2('t', 's'):  // actual creation timestamp
                    ts = j->getint();
                    break;

                case EOO:
                    if (chatid != UNDEF && priv != PRIV_UNKNOWN && shard != -1)
                    {
                        if (chats.find(chatid) == chats.end())
                        {
                            chats[chatid] = new TextChat();
                        }

                        TextChat *chat = chats[chatid];
                        chat->id = chatid;
                        chat->priv = priv;
                        chat->shard = shard;
                        chat->group = group;
                        chat->title = title;
                        chat->ts = (ts != -1) ? ts : 0;

                        // remove yourself from the list of users (only peers matter)
                        if (userpriv)
                        {
                            userpriv_vector::iterator upvit;
                            for (upvit = userpriv->begin(); upvit != userpriv->end(); upvit++)
                            {
                                if (upvit->first == me)
                                {
                                    userpriv->erase(upvit);
                                    if (userpriv->empty())
                                    {
                                        delete userpriv;
                                        userpriv = NULL;
                                    }
                                    break;
                                }
                            }
                        }
                        delete chat->userpriv;  // discard any existing `userpriv`
                        chat->userpriv = userpriv;
                    }
                    else
                    {
                        LOG_err << "Failed to parse chat information";
                    }
                    readingChat = false;
                    break;

                default:
                    if (!j->storeobject())
                    {
                        LOG_err << "Failed to parse chat information";
                        readingChat = false;
                        delete userpriv;
                        userpriv = NULL;
                    }
                    break;
                }
            }
            j->leaveobject();
        }
    }

    j->leavearray();
    j->leaveobject();
}

void MegaClient::procmcna(JSON *j)
{
    if (j->enterarray())
    {
        while(j->enterobject())   // while there are more nodes to read...
        {
            handle chatid = UNDEF;
            handle h = UNDEF;
            handle uh = UNDEF;

            bool readingNode = true;
            while(readingNode) // read the attached node information
            {
                switch (j->getnameid())
                {
                case MAKENAMEID2('i','d'):
                    chatid = j->gethandle(MegaClient::CHATHANDLE);
                    break;

                case 'n':
                    h = j->gethandle(MegaClient::NODEHANDLE);
                    break;

                case 'u':
                    uh = j->gethandle(MegaClient::USERHANDLE);
                    break;

                case EOO:
                    if (chatid != UNDEF && h != UNDEF && uh != UNDEF)
                    {
                        if (chats.find(chatid) == chats.end())
                        {
                            chats[chatid] = new TextChat();
                        }

                        chats[chatid]->setNodeUserAccess(h, uh);
                    }
                    else
                    {
                        LOG_err << "Failed to parse attached node information";
                    }
                    readingNode = false;
                    break;

                default:
                    if (!j->storeobject())
                    {
                        LOG_err << "Failed to parse attached node information";
                        readingNode = false;
                    }
                    break;
                }
            }
            j->leaveobject();
        }        
        j->leavearray();
    }
}
#endif

// add node to vector, return position, deduplicate
unsigned MegaClient::addnode(node_vector* v, Node* n) const
{
    // linear search not particularly scalable, but fine for the relatively
    // small real-world requests
    for (int i = v->size(); i--; )
    {
        if ((*v)[i] == n)
        {
            return i;
        }
    }

    v->push_back(n);
    return v->size() - 1;
}

// generate crypto key response
// if !selector, generate all shares*nodes tuples
void MegaClient::cr_response(node_vector* shares, node_vector* nodes, JSON* selector)
{
    node_vector rshares, rnodes;
    unsigned si, ni;
    Node* sn;
    Node* n;
    string crkeys;
    byte keybuf[FILENODEKEYLENGTH];
    char buf[128];
    int setkey = -1;

    // for security reasons, we only respond to key requests affecting our own
    // shares
    for (si = shares->size(); si--; )
    {
        if ((*shares)[si] && ((*shares)[si]->inshare || !(*shares)[si]->sharekey))
        {
            LOG_warn << "Attempt to obtain node key for invalid/third-party share foiled";
            (*shares)[si] = NULL;
        }
    }

    if (!selector)
    {
        si = 0;
        ni = 0;
    }

    // estimate required size for requested keys
    // for each node: ",<index>,<index>,"<nodekey>
    crkeys.reserve(nodes->size() * ((5 + 4 * 2) + (FILENODEKEYLENGTH * 4 / 3 + 4)) + 1);
    // we reserve for indexes up to 4 digits per index

    for (;;)
    {
        if (selector)
        {
            if (!selector->isnumeric())
            {
                break;
            }

            si = (unsigned)selector->getint();
            ni = (unsigned)selector->getint();

            if (si >= shares->size())
            {
                LOG_err << "Share index out of range";
                return;
            }

            if (ni >= nodes->size())
            {
                LOG_err << "Node index out of range";
                return;
            }

            if (selector->pos[1] == '"')
            {
                setkey = selector->storebinary(keybuf, sizeof keybuf);
            }
        }
        else
        {
            // no selector supplied
            ni++;

            if (ni >= nodes->size())
            {
                ni = 0;
                if (++si >= shares->size())
                {
                    break;
                }
            }
        }

        if ((sn = (*shares)[si]) && (n = (*nodes)[ni]))
        {
            if (n->isbelow(sn))
            {
                if (setkey >= 0)
                {
                    if (setkey == (int)n->nodekey.size())
                    {
                        sn->sharekey->ecb_decrypt(keybuf, n->nodekey.size());
                        n->setkey(keybuf);
                        setkey = -1;
                    }
                }
                else
                {
                    n->applykey();
                    if (sn->sharekey && n->nodekey.size() ==
                            (unsigned)((n->type == FILENODE) ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH))
                    {
                        unsigned nsi, nni;

                        nsi = addnode(&rshares, sn);
                        nni = addnode(&rnodes, n);

                        sprintf(buf, "\",%u,%u,\"", nsi, nni);

                        // generate & queue share nodekey
                        sn->sharekey->ecb_encrypt((byte*)n->nodekey.data(), keybuf, n->nodekey.size());
                        Base64::btoa(keybuf, n->nodekey.size(), strchr(buf + 7, 0));
                        crkeys.append(buf);
                    }
                    else
                    {
                        LOG_warn << "Skipping node due to an unavailable key";
                    }
                }
            }
            else
            {
                LOG_warn << "Attempt to obtain key of node outside share foiled";
            }
        }
    }

    if (crkeys.size())
    {
        crkeys.append("\"");
        reqs.add(new CommandKeyCR(this, &rshares, &rnodes, crkeys.c_str() + 2));
    }
}

void MegaClient::getaccountdetails(AccountDetails* ad, bool storage,
                                   bool transfer, bool pro, bool transactions,
                                   bool purchases, bool sessions)
{
    reqs.add(new CommandGetUserQuota(this, ad, storage, transfer, pro));

    if (transactions)
    {
        reqs.add(new CommandGetUserTransactions(this, ad));
    }

    if (purchases)
    {
        reqs.add(new CommandGetUserPurchases(this, ad));
    }

    if (sessions)
    {
        reqs.add(new CommandGetUserSessions(this, ad));
    }
}

void MegaClient::querytransferquota(m_off_t size)
{
    reqs.add(new CommandQueryTransferQuota(this, size));
}

// export node link
error MegaClient::exportnode(Node* n, int del, m_time_t ets)
{
    if (n->plink && !del && !n->plink->takendown
            && (ets == n->plink->ets) && !n->plink->isExpired())
    {
        restag = reqtag;
        app->exportnode_result(n->nodehandle, n->plink->ph);
        return API_OK;
    }

    if (!checkaccess(n, OWNER))
    {
        return API_EACCESS;
    }

    // export node
    switch (n->type)
    {
    case FILENODE:
        getpubliclink(n, del, ets);
        break;

    case FOLDERNODE:
        if (del)
        {
            // deletion of outgoing share also deletes the link automatically
            // need to first remove the link and then the share
            getpubliclink(n, del, ets);
            setshare(n, NULL, ACCESS_UNKNOWN);
        }
        else
        {
            // exporting folder - need to create share first
            setshare(n, NULL, RDONLY);
            // getpubliclink() is called as _result() of the share
        }

        break;

    default:
        return API_EACCESS;
    }

    return API_OK;
}

void MegaClient::getpubliclink(Node* n, int del, m_time_t ets)
{
    reqs.add(new CommandSetPH(this, n, del, ets));
}

// open exported file link
// formats supported: ...#!publichandle!key or publichandle!key
error MegaClient::openfilelink(const char* link, int op)
{
    const char* ptr = NULL;
    handle ph = 0;
    byte key[FILENODEKEYLENGTH];

    if ((ptr = strstr(link, "#!")))
    {
        ptr += 2;
    }
    else    // legacy format without '#'
    {
        ptr = link;
    }

    if (Base64::atob(ptr, (byte*)&ph, NODEHANDLE) == NODEHANDLE)
    {
        ptr += 8;

        if (*ptr == '!')
        {
            ptr++;

            if (Base64::atob(ptr, key, sizeof key) == sizeof key)
            {
                if (op)
                {
                    reqs.add(new CommandGetPH(this, ph, key, op));
                }
                else
                {
                    reqs.add(new CommandGetFile(this, NULL, key, ph, false));
                }

                return API_OK;
            }
        }
        else if (*ptr == '\0')    // no key provided, check only the existence of the node
        {
            if (op)
            {
                reqs.add(new CommandGetPH(this, ph, NULL, op));
                return API_OK;
            }
        }
    }

    return API_EARGS;
}

/* Format of password-protected links
 *
 * algorithm        = 1 byte - A byte to identify which algorithm was used (for future upgradability), initially is set to 0
 * file/folder      = 1 byte - A byte to identify if the link is a file or folder link (0 = folder, 1 = file)
 * public handle    = 6 bytes - The public folder/file handle
 * salt             = 32 bytes - A 256 bit randomly generated salt
 * encrypted key    = 16 or 32 bytes - The encrypted actual folder or file key
 * MAC tag          = 32 bytes - The MAC of all the previous data to ensure integrity of the link i.e. calculated as:
 *                      HMAC-SHA256(MAC key, (algorithm || file/folder || public handle || salt || encrypted key))
 */
error MegaClient::decryptlink(const char *link, const char *pwd, string* decryptedLink)
{
    if (!pwd || !link)
    {
        LOG_err << "Empty link or empty password to decrypt link";
        return API_EARGS;
    }

    const char* ptr = NULL;
    const char* end = NULL;
    if (!(ptr = strstr(link, "#P!")))
    {
        LOG_err << "This link is not password protected";
        return API_EARGS;
    }
    ptr += 3;

    // Decode the link
    int linkLen = 1 + 1 + 6 + 32 + 32 + 32;   // maximum size in binary, for file links
    string linkBin;
    linkBin.resize(linkLen);
    linkLen = Base64::atob(ptr, (byte*)linkBin.data(), linkLen);

    ptr = (char *)linkBin.data();
    end = ptr + linkLen;

    if ((ptr + 2) >= end)
    {
        LOG_err << "This link is too short";
        return API_EINCOMPLETE;
    }

    int algorithm = *ptr++;
    if (algorithm != 1 && algorithm != 2)
    {
        LOG_err << "The algorithm used to encrypt this link is not supported";
        return API_EINTERNAL;
    }

    int isFolder = !(*ptr++);
    if (isFolder > 1)
    {
        LOG_err << "This link doesn't reference any folder or file";
        return API_EARGS;
    }

    size_t encKeyLen = isFolder ? FOLDERNODEKEYLENGTH : FILENODEKEYLENGTH;
    if ((ptr + 38 + encKeyLen + 32) > end)
    {
        LOG_err << "This link is too short";
        return API_EINCOMPLETE;
    }

    handle ph = MemAccess::get<handle>(ptr);
    ptr += 6;

    byte salt[32];
    memcpy((char*)salt, ptr, 32);
    ptr += sizeof salt;

    string encKey;
    encKey.resize(encKeyLen);
    memcpy((byte *)encKey.data(), ptr, encKeyLen);
    ptr += encKeyLen;

    byte hmac[32];
    memcpy((char*)&hmac, ptr, 32);
    ptr += 32;

    // Derive MAC key with salt+pwd
    byte derivedKey[64];
    unsigned int iterations = 100000;
    PBKDF2_HMAC_SHA512 pbkdf2;
    pbkdf2.deriveKey(derivedKey, sizeof derivedKey,
                     (byte*) pwd, strlen(pwd),
                     salt, sizeof salt,
                     iterations);

    byte hmacComputed[32];
    if (algorithm == 1)
    {
        // verify HMAC with macKey(alg, f/F, ph, salt, encKey)
        HMACSHA256 hmacsha256((byte *)linkBin.data(), 40 + encKeyLen);
        hmacsha256.add(derivedKey + 32, 32);
        hmacsha256.get(hmacComputed);
    }
    else // algorithm == 2 (fix legacy Webclient bug: swap data and key
    {
        // verify HMAC with macKey(alg, f/F, ph, salt, encKey)
        HMACSHA256 hmacsha256(derivedKey + 32, 32);
        hmacsha256.add((byte *)linkBin.data(), 40 + encKeyLen);
        hmacsha256.get(hmacComputed);
    }
    if (memcmp(hmac, hmacComputed, 32))
    {
        LOG_err << "HMAC verification failed. Possible tampered or corrupted link";
        return API_EKEY;
    }

    if (decryptedLink)
    {
        // Decrypt encKey using X-OR with first 16/32 bytes of derivedKey
        byte key[FILENODEKEYLENGTH];
        for (unsigned int i = 0; i < encKeyLen; i++)
        {
            key[i] = encKey[i] ^ derivedKey[i];
        }

        // generate plain link
        char phStr[9];
        char keyStr[FILENODEKEYLENGTH*4/3+3];

        Base64::btoa((byte*) &ph, MegaClient::NODEHANDLE, phStr);
        Base64::btoa(key, encKeyLen, keyStr);

        decryptedLink->clear();
        decryptedLink->append("https://mega.nz/#");
        decryptedLink->append(isFolder ? "F!" : "!");
        decryptedLink->append(phStr);
        decryptedLink->append("!");
        decryptedLink->append(keyStr);
    }

    return API_OK;
}

error MegaClient::encryptlink(const char *link, const char *pwd, string *encryptedLink)
{
    if (!pwd || !link)
    {
        LOG_err << "Empty link or empty password to encrypt link";
        return API_EARGS;
    }

    const char* ptr = NULL;
    const char* end = link + strlen(link);

    if (!(ptr = strstr(link, "#")) || ptr >= end)
    {
        LOG_err << "Invalid format of public link or incomplete";
        return API_EARGS;
    }
    ptr++;  // skip '#'

    int isFolder;
    if (*ptr == 'F')
    {
        isFolder = true;
        ptr++;  // skip 'F'
    }
    else if (*ptr == '!')
    {
        isFolder = false;
    }
    else
    {
        LOG_err << "Invalid format of public link";
        return API_EARGS;
    }
    ptr++;  // skip '!' separator

    if (ptr + 8 >= end)
    {
        LOG_err << "Incomplete public link";
        return API_EINCOMPLETE;
    }

    handle ph;
    if (Base64::atob(ptr, (byte*)&ph, NODEHANDLE) != NODEHANDLE)
    {
        LOG_err << "Invalid format of public link";
        return API_EARGS;
    }
    ptr += 8;   // skip public handle

    if (ptr + 1 >= end || *ptr != '!')
    {
        LOG_err << "Invalid format of public link";
        return API_EARGS;
    }
    ptr++;  // skip '!' separator

    size_t linkKeySize = isFolder ? FOLDERNODEKEYLENGTH : FILENODEKEYLENGTH;
    string linkKey;
    linkKey.resize(linkKeySize);
    if ((size_t) Base64::atob(ptr, (byte *)linkKey.data(), linkKey.size()) != linkKeySize)
    {
        LOG_err << "Invalid encryption key in the public link";
        return API_EKEY;
    }

    if (encryptedLink)
    {
        // Derive MAC key with salt+pwd
        byte derivedKey[64];
        byte salt[32];
        PrnGen::genblock(salt, 32);
        unsigned int iterations = 100000;
        PBKDF2_HMAC_SHA512 pbkdf2;
        pbkdf2.deriveKey(derivedKey, sizeof derivedKey,
                         (byte*) pwd, strlen(pwd),
                         salt, sizeof salt,
                         iterations);

        // Prepare encryption key
        string encKey;
        encKey.resize(linkKeySize);
        for (unsigned int i = 0; i < linkKeySize; i++)
        {
            encKey[i] = derivedKey[i] ^ linkKey[i];
        }

        // Preapare payload to derive encryption key
        byte algorithm = 1;
        byte type = isFolder ? 0 : 1;
        string payload;
        payload.append((char*) &algorithm, sizeof algorithm);
        payload.append((char*) &type, sizeof type);
        payload.append((char*) &ph, NODEHANDLE);
        payload.append((char*) salt, sizeof salt);
        payload.append(encKey);


        // Prepare HMAC
        byte hmac[32];
        if (algorithm == 1)
        {
            HMACSHA256 hmacsha256((byte *)payload.data(), payload.size());
            hmacsha256.add(derivedKey + 32, 32);
            hmacsha256.get(hmac);
        }
        else if (algorithm == 2) // fix legacy Webclient bug: swap data and key
        {
            HMACSHA256 hmacsha256(derivedKey + 32, 32);
            hmacsha256.add((byte *)payload.data(), payload.size());
            hmacsha256.get(hmac);
        }
        else
        {
            LOG_err << "Invalid algorithm to encrypt link";
            return API_EINTERNAL;
        }

        // Prepare encrypted link
        string encLinkBytes;
        encLinkBytes.append((char*) &algorithm, sizeof algorithm);
        encLinkBytes.append((char*) &type, sizeof type);
        encLinkBytes.append((char*) &ph, NODEHANDLE);
        encLinkBytes.append((char*) salt, sizeof salt);
        encLinkBytes.append(encKey);
        encLinkBytes.append((char*) hmac, sizeof hmac);

        string encLink;
        Base64::btoa(encLinkBytes, encLink);

        encryptedLink->clear();
        encryptedLink->append("https://mega.nz/#P!");
        encryptedLink->append(encLink);
    }

    return API_OK;
}

sessiontype_t MegaClient::loggedin()
{
    if (ISUNDEF(me))
    {
        return NOTLOGGEDIN;
    }

    User* u = finduser(me);

    if (u && !u->email.size())
    {
        return EPHEMERALACCOUNT;
    }

    if (!asymkey.isvalid())
    {
        return CONFIRMEDACCOUNT;
    }

    return FULLACCOUNT;
}

error MegaClient::changepw(const byte* oldpwkey, const byte* newpwkey)
{
    User* u;

    if (!loggedin() || !(u = finduser(me)))
    {
        return API_EACCESS;
    }

    byte oldkey[SymmCipher::KEYLENGTH];
    byte newkey[SymmCipher::KEYLENGTH];

    SymmCipher pwcipher;

    memcpy(oldkey, key.key, sizeof oldkey);
    memcpy(newkey, oldkey,  sizeof newkey);

    pwcipher.setkey(oldpwkey);
    pwcipher.ecb_encrypt(oldkey);

    pwcipher.setkey(newpwkey);
    pwcipher.ecb_encrypt(newkey);

    string email = u->email;

    reqs.add(new CommandSetMasterKey(this, oldkey, newkey, stringhash64(&email, &pwcipher)));

    return API_OK;
}

// create ephemeral session
void MegaClient::createephemeral()
{
    byte keybuf[SymmCipher::KEYLENGTH];
    byte pwbuf[SymmCipher::KEYLENGTH];
    byte sscbuf[2 * SymmCipher::KEYLENGTH];

    locallogout();

    PrnGen::genblock(keybuf, sizeof keybuf);
    PrnGen::genblock(pwbuf, sizeof pwbuf);
    PrnGen::genblock(sscbuf, sizeof sscbuf);

    key.setkey(keybuf);
    key.ecb_encrypt(sscbuf, sscbuf + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH);

    key.setkey(pwbuf);
    key.ecb_encrypt(keybuf);

    reqs.add(new CommandCreateEphemeralSession(this, keybuf, pwbuf, sscbuf));
}

void MegaClient::resumeephemeral(handle uh, const byte* pw, int ctag)
{
    reqs.add(new CommandResumeEphemeralSession(this, uh, pw, ctag ? ctag : reqtag));
}

void MegaClient::sendsignuplink(const char* email, const char* name, const byte* pwhash)
{
    SymmCipher pwcipher(pwhash);
    byte c[2 * SymmCipher::KEYLENGTH];

    memcpy(c, key.key, sizeof key.key);
    PrnGen::genblock(c + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH / 4);
    memset(c + SymmCipher::KEYLENGTH + SymmCipher::KEYLENGTH / 4, 0, SymmCipher::KEYLENGTH / 2);
    PrnGen::genblock(c + 2 * SymmCipher::KEYLENGTH - SymmCipher::KEYLENGTH / 4, SymmCipher::KEYLENGTH / 4);

    pwcipher.ecb_encrypt(c, c, sizeof c);

    reqs.add(new CommandSendSignupLink(this, email, name, c));
}

// if query is 0, actually confirm account; just decode/query signup link
// details otherwise
void MegaClient::querysignuplink(const byte* code, unsigned len)
{
    reqs.add(new CommandQuerySignupLink(this, code, len));
}

void MegaClient::confirmsignuplink(const byte* code, unsigned len, uint64_t emailhash)
{
    reqs.add(new CommandConfirmSignupLink(this, code, len, emailhash));
}

// generate and configure encrypted private key, plaintext public key
void MegaClient::setkeypair()
{
    CryptoPP::Integer pubk[AsymmCipher::PUBKEY];

    string privks, pubks;

    asymkey.genkeypair(asymkey.key, pubk, 2048);

    AsymmCipher::serializeintarray(pubk, AsymmCipher::PUBKEY, &pubks);
    AsymmCipher::serializeintarray(asymkey.key, AsymmCipher::PRIVKEY, &privks);

    // add random padding and ECB-encrypt with master key
    unsigned t = privks.size();

    privks.resize((t + SymmCipher::BLOCKSIZE - 1) & - SymmCipher::BLOCKSIZE);
    PrnGen::genblock((byte*)(privks.data() + t), privks.size() - t);

    key.ecb_encrypt((byte*)privks.data(), (byte*)privks.data(), (unsigned)privks.size());

    reqs.add(new CommandSetKeyPair(this,
                                      (const byte*)privks.data(),
                                      privks.size(),
                                      (const byte*)pubks.data(),
                                      pubks.size()));
}

bool MegaClient::fetchsc(DbTable* sctable)
{
    uint32_t id;
    string data;
    Node* n;
    User* u;
    PendingContactRequest* pcr;
    node_vector dp;

    LOG_info << "Loading session from local cache";

    sctable->rewind();

    bool hasNext = sctable->next(&id, &data, &key);
    WAIT_CLASS::bumpds();
    fnstats.timeToFirstByte = Waiter::ds - fnstats.startTime;

    while (hasNext)
    {
        switch (id & 15)
        {
            case CACHEDSCSN:
                if (data.size() != sizeof cachedscsn)
                {
                    return false;
                }
                break;

            case CACHEDNODE:
                if ((n = Node::unserialize(this, &data, &dp)))
                {
                    n->dbid = id;
                }
                else
                {
                    LOG_err << "Failed - node record read error";
                    return false;
                }
                break;

            case CACHEDPCR:
                if ((pcr = PendingContactRequest::unserialize(this, &data)))
                {
                    pcr->dbid = id;
                }
                else
                {
                    LOG_err << "Failed - pcr record read error";
                    return false;
                }
                break;

            case CACHEDUSER:
                if ((u = User::unserialize(this, &data)))
                {
                    u->dbid = id;
                }
                else
                {
                    LOG_err << "Failed - user record read error";
                    return false;
                }
                break;

            case CACHEDCHAT:
#ifdef ENABLE_CHAT
                {
                    TextChat *chat;
                    if ((chat = TextChat::unserialize(this, &data)))
                    {
                        chat->dbid = id;
                    }
                    else
                    {
                        LOG_err << "Failed - chat record read error";
                        return false;
                    }
                }
#endif
                break;
        }
        hasNext = sctable->next(&id, &data, &key);
    }

    WAIT_CLASS::bumpds();
    fnstats.timeToLastByte = Waiter::ds - fnstats.startTime;

    // any child nodes arrived before their parents?
    for (int i = dp.size(); i--; )
    {
        if ((n = nodebyhandle(dp[i]->parenthandle)))
        {
            dp[i]->setparent(n);
        }
    }

    mergenewshares(0);

    return true;
}

void MegaClient::closetc(bool remove)
{
    bool purgeOrphanTransfers = statecurrent;

#ifdef ENABLE_SYNC
    if (purgeOrphanTransfers && !remove)
    {
        if (!syncsup)
        {
            purgeOrphanTransfers = false;
        }
        else
        {
            for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
            {
                if ((*it)->state != SYNC_ACTIVE)
                {
                    purgeOrphanTransfers = false;
                    break;
                }
            }
        }
    }
#endif

    for (int d = GET; d == GET || d == PUT; d += PUT - GET)
    {
        while (cachedtransfers[d].size())
        {
            transfer_map::iterator it = cachedtransfers[d].begin();
            Transfer *transfer = it->second;
            if (remove || (purgeOrphanTransfers && (time(NULL) - transfer->lastaccesstime) >= 172500))
            {
                LOG_warn << "Purging orphan transfer";
                transfer->finished = true;
            }

            delete transfer;
            cachedtransfers[d].erase(it);
        }
    }

    pendingtcids.clear();
    cachedfiles.clear();
    cachedfilesdbids.clear();

    if (remove && tctable)
    {
        tctable->remove();
    }
    delete tctable;
    tctable = NULL;
}

void MegaClient::enabletransferresumption(const char *loggedoutid)
{
    if (!dbaccess || tctable)
    {
        return;
    }

    string dbname;
    if (sid.size() >= SIDLEN)
    {
        dbname.resize((SIDLEN - sizeof key.key) * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)sid.data() + sizeof key.key, SIDLEN - sizeof key.key, (char*)dbname.c_str()));
        tckey = key;
    }
    else if (publichandle != UNDEF)
    {
        dbname.resize(NODEHANDLE * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)&publichandle, NODEHANDLE, (char*)dbname.c_str()));
        tckey = key;
    }
    else
    {
        dbname = loggedoutid ? loggedoutid : "default";

        string lok;
        Hash hash;
        hash.add((const byte *)dbname.c_str(), dbname.size() + 1);
        hash.get(&lok);
        tckey.setkey((const byte*)lok.data());
    }

    dbname.insert(0, "transfers_");

    tctable = dbaccess->open(fsaccess, &dbname, true);
    if (!tctable)
    {
        return;
    }

    uint32_t id;
    string data;
    Transfer* t;

    LOG_info << "Loading transfers from local cache";
    tctable->rewind();
    while (tctable->next(&id, &data, &tckey))
    {
        switch (id & 15)
        {
            case CACHEDTRANSFER:
                if ((t = Transfer::unserialize(this, &data, cachedtransfers)))
                {
                    t->dbid = id;
                    if (t->priority > transferlist.currentpriority)
                    {
                        transferlist.currentpriority = t->priority;
                    }
                    LOG_debug << "Cached transfer loaded";
                }
                else
                {
                    tctable->del(id);
                    LOG_err << "Failed - transfer record read error";
                }
                break;
            case CACHEDFILE:
                cachedfiles.push_back(data);
                cachedfilesdbids.push_back(id);
                LOG_debug << "Cached file loaded";
                break;
        }
    }

    // if we are logged in but the filesystem is not current yet
    // postpone the resumption until the filesystem is updated
    if ((!sid.size() && publichandle == UNDEF) || statecurrent)
    {
        tctable->begin();
        for (unsigned int i = 0; i < cachedfiles.size(); i++)
        {
            direction_t type = NONE;
            File *file = app->file_resume(&cachedfiles.at(i), &type);
            if (!file || (type != GET && type != PUT))
            {
                tctable->del(cachedfilesdbids.at(i));
                continue;
            }
            nextreqtag();
            file->dbid = cachedfilesdbids.at(i);
            if (!startxfer(type, file))
            {
                tctable->del(cachedfilesdbids.at(i));
                continue;
            }
        }
        cachedfiles.clear();
        cachedfilesdbids.clear();
        tctable->commit();
    }
}

void MegaClient::disabletransferresumption(const char *loggedoutid)
{
    if (!dbaccess)
    {
        return;
    }
    closetc(true);

    string dbname;
    if (sid.size() >= SIDLEN)
    {
        dbname.resize((SIDLEN - sizeof key.key) * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)sid.data() + sizeof key.key, SIDLEN - sizeof key.key, (char*)dbname.c_str()));

    }
    else if (publichandle != UNDEF)
    {
        dbname.resize(NODEHANDLE * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)&publichandle, NODEHANDLE, (char*)dbname.c_str()));
    }
    else
    {
        dbname = loggedoutid ? loggedoutid : "default";
    }
    dbname.insert(0, "transfers_");

    tctable = dbaccess->open(fsaccess, &dbname, true);
    if (!tctable)
    {
        return;
    }

    closetc(true);
}

void MegaClient::fetchnodes(bool nocache)
{
    if (fetchingnodes)
    {
        return;
    }

    WAIT_CLASS::bumpds();
    fnstats.init();
    if (sid.size() >= SIDLEN)
    {
        fnstats.type = FetchNodesStats::TYPE_ACCOUNT;
    }
    else if (publichandle != UNDEF)
    {
        fnstats.type = FetchNodesStats::TYPE_FOLDER;
    }

    opensctable();

    if (sctable && cachedscsn == UNDEF)
    {
        sctable->truncate();
    }

    // only initial load from local cache
    if (loggedin() == FULLACCOUNT && !nodes.size() && sctable && !ISUNDEF(cachedscsn) && fetchsc(sctable))
    {
        WAIT_CLASS::bumpds();
        fnstats.mode = FetchNodesStats::MODE_DB;
        fnstats.cache = FetchNodesStats::API_NO_CACHE;
        fnstats.nodesCached = nodes.size();
        fnstats.timeToCached = Waiter::ds - fnstats.startTime;
        fnstats.timeToResult = fnstats.timeToCached;

        restag = reqtag;
        statecurrent = false;

        sctable->begin();
        pendingsccommit = false;

        Base64::btoa((byte*)&cachedscsn, sizeof cachedscsn, scsn);
        LOG_info << "Session loaded from local cache. SCSN: " << scsn;

        app->fetchnodes_result(API_OK);

        // if don't know fileversioning is enabled or disabled...
        // (it can happen after AP invalidates the attribute, but app is closed before current value is retrieved and cached)
        User *ownUser = finduser(me);
        const string *av = ownUser->getattr(ATTR_DISABLE_VERSIONS);
        if (av)
        {
            if (ownUser->isattrvalid((ATTR_DISABLE_VERSIONS)))
            {
                versions_disabled = !strcmp(av->c_str(), "1");
                if (versions_disabled)
                {
                    LOG_info << "File versioning is disabled";
                }
                else
                {
                    LOG_info << "File versioning is enabled";
                }
            }
            else
            {
                getua(ownUser, ATTR_DISABLE_VERSIONS, 0);
                LOG_info << "File versioning option exist but is unknown. Fetching...";
            }
        }
        else    // attribute does not exists
        {
            LOG_info << "File versioning is enabled";
            versions_disabled = false;
        }

        WAIT_CLASS::bumpds();
        fnstats.timeToSyncsResumed = Waiter::ds - fnstats.startTime;
    }
    else if (!fetchingnodes)
    {
        fnstats.mode = FetchNodesStats::MODE_API;
        fnstats.cache = nocache ? FetchNodesStats::API_NO_CACHE : FetchNodesStats::API_CACHE;
        fetchingnodes = true;
        pendingsccommit = false;

        // prevent the processing of previous sc requests
        delete pendingsc;
        pendingsc = NULL;
        jsonsc.pos = NULL;
        scnotifyurl.clear();
        insca = false;
        btsc.reset();

        // don't allow to start new sc requests yet
        *scsn = 0;

#ifdef ENABLE_SYNC
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            (*it)->changestate(SYNC_CANCELED);
        }
#endif
#ifdef ENABLE_CHAT
        if (loggedin() == FULLACCOUNT)
        {
            fetchkeys();
        }
#endif
        reqs.add(new CommandFetchNodes(this, nocache));

        char me64[12];
        Base64::btoa((const byte*)&me, MegaClient::USERHANDLE, me64);
        reqs.add(new CommandGetUA(this, me64, ATTR_DISABLE_VERSIONS, 0));
    }
}

#ifdef ENABLE_CHAT
void MegaClient::fetchkeys()
{
    fetchingkeys = true;

    resetKeyring();
    discarduser(me);
    User *u = finduser(me, 1);

    int creqtag = reqtag;
    reqtag = 0;
    reqs.add(new CommandPubKeyRequest(this, u));    // public RSA
    reqtag = creqtag;

    getua(u, ATTR_KEYRING, 0);        // private Cu25519 & private Ed25519
    getua(u, ATTR_ED25519_PUBK, 0);
    getua(u, ATTR_CU25519_PUBK, 0);
    getua(u, ATTR_SIG_CU255_PUBK, 0);
    getua(u, ATTR_SIG_RSA_PUBK, 0);   // it triggers MegaClient::initializekeys() --> must be the latest
}

void MegaClient::initializekeys()
{
    User *u = finduser(me);

    // Initialize private keys
    const string *av = (u->isattrvalid(ATTR_KEYRING)) ? u->getattr(ATTR_KEYRING) : NULL;
    if (av)
    {
        TLVstore *tlvRecords = TLVstore::containerToTLVrecords(av, &key);
        if (tlvRecords)
        {

            if (tlvRecords->find(EdDSA::TLV_KEY))
            {
                string prEd255 = tlvRecords->get(EdDSA::TLV_KEY);
                if (prEd255.size() == EdDSA::SEED_KEY_LENGTH)
                {
                    signkey = new EdDSA((unsigned char *) prEd255.data());
                    if (!signkey->initializationOK)
                    {
                        delete signkey;
                        signkey = NULL;
                        clearKeys();
                        return;
                    }
                }
            }

            if (tlvRecords->find(ECDH::TLV_KEY))
            {
                string prCu255 = tlvRecords->get(ECDH::TLV_KEY);
                if (prCu255.size() == ECDH::PRIVATE_KEY_LENGTH)
                {
                    chatkey = new ECDH((unsigned char *) prCu255.data());
                    if (!chatkey->initializationOK)
                    {
                        delete chatkey;
                        chatkey = NULL;
                        clearKeys();
                        return;
                    }
                }
            }
            delete tlvRecords;
        }
        else
        {
            LOG_warn << "Failed to decrypt keyring while initialization";
        }
    }

    string puEd255 = (u->isattrvalid(ATTR_ED25519_PUBK)) ? *u->getattr(ATTR_ED25519_PUBK) : "";
    string puCu255 = (u->isattrvalid(ATTR_CU25519_PUBK)) ? *u->getattr(ATTR_CU25519_PUBK) : "";
    string sigCu255 = (u->isattrvalid(ATTR_SIG_CU255_PUBK)) ? *u->getattr(ATTR_SIG_CU255_PUBK) : "";
    string sigPubk = (u->isattrvalid(ATTR_SIG_RSA_PUBK)) ? *u->getattr(ATTR_SIG_RSA_PUBK) : "";

    if (chatkey && signkey)    // THERE ARE KEYS
    {
        // Check Ed25519 public key against derived version
        if ((puEd255.size() != EdDSA::PUBLIC_KEY_LENGTH) || memcmp(puEd255.data(), signkey->pubKey, EdDSA::PUBLIC_KEY_LENGTH))
        {
            LOG_warn << "Public key for Ed25519 mismatch.";

            int creqtag = reqtag;
            reqtag = 0;
            sendevent(99417, "Ed25519 public key mismatch");
            reqtag = creqtag;

            clearKeys();
            resetKeyring();
            return;
        }

        // Check Cu25519 public key against derive version
        if ((puCu255.size() != ECDH::PUBLIC_KEY_LENGTH) || memcmp(puCu255.data(), chatkey->pubKey, ECDH::PUBLIC_KEY_LENGTH))
        {
            LOG_warn << "Public key for Cu25519 mismatch.";

            int creqtag = reqtag;
            reqtag = 0;
            sendevent(99412, "Cu25519 public key mismatch");
            reqtag = creqtag;

            clearKeys();
            resetKeyring();
            return;
        }

        // Verify signatures for Cu25519
        if (!sigCu255.size() ||
                !signkey->verifyKey((unsigned char*) puCu255.data(),
                                    puCu255.size(),
                                    &sigCu255,
                                    (unsigned char*) puEd255.data()))
        {
            LOG_warn << "Signature of public key for Cu25519 not found or mismatch";

            int creqtag = reqtag;
            reqtag = 0;
            sendevent(99413, "Signature of Cu25519 public key mismatch");
            reqtag = creqtag;

            clearKeys();
            resetKeyring();
            return;
        }

        // Verify signature for RSA public key
        string sigPubk = (u->isattrvalid(ATTR_SIG_RSA_PUBK)) ? *u->getattr(ATTR_SIG_RSA_PUBK) : "";
        string pubkstr;
        if (pubk.isvalid())
        {
            pubk.serializekeyforjs(pubkstr);
        }
        if (!pubkstr.size() || !sigPubk.size())
        {
            int creqtag = reqtag;
            reqtag = 0;

            if (!pubkstr.size())
            {
                LOG_warn << "Error serializing RSA public key";
                sendevent(99421, "Error serializing RSA public key");
            }
            if (!sigPubk.size())
            {
                LOG_warn << "Signature of public key for RSA not found";
                sendevent(99422, "Signature of public key for RSA not found");
            }
            reqtag = creqtag;

            clearKeys();
            resetKeyring();
            return;
        }
        if (!signkey->verifyKey((unsigned char*) pubkstr.data(),
                                    pubkstr.size(),
                                    &sigPubk,
                                    (unsigned char*) puEd255.data()))
        {
            LOG_warn << "Verification of signature of public key for RSA failed";

            int creqtag = reqtag;
            reqtag = 0;
            sendevent(99414, "Verification of signature of public key for RSA failed");
            reqtag = creqtag;

            clearKeys();
            resetKeyring();
            return;
        }

        // if we reached this point, everything is OK
        LOG_info << "Keypairs and signatures loaded successfully";
        fetchingkeys = false;
        return;
    }
    else if (!signkey && !chatkey)       // THERE ARE NO KEYS
    {
        // Check completeness of keypairs
        if (!pubk.isvalid() || puEd255.size() || puCu255.size() || sigCu255.size() || sigPubk.size())
        {
            LOG_warn << "Public keys and/or signatures found witout their respective private key.";

            int creqtag = reqtag;
            reqtag = 0;
            sendevent(99415, "Incomplete keypair detected");
            reqtag = creqtag;

            clearKeys();
            return;
        }
        else    // No keys were set --> generate keypairs and related attributes
        {
            // generate keypairs
            EdDSA *signkey = new EdDSA();
            ECDH *chatkey = new ECDH();

            if (!chatkey->initializationOK || !signkey->initializationOK)
            {
                LOG_err << "Initialization of keys Cu25519 and/or Ed25519 failed";
                clearKeys();
                delete signkey;
                delete chatkey;
                return;
            }

            // prepare the TLV for private keys
            TLVstore tlvRecords;
            tlvRecords.set(EdDSA::TLV_KEY, string((const char*)signkey->keySeed, EdDSA::SEED_KEY_LENGTH));
            tlvRecords.set(ECDH::TLV_KEY, string((const char*)chatkey->privKey, ECDH::PRIVATE_KEY_LENGTH));
            string *tlvContainer = tlvRecords.tlvRecordsToContainer(&key);

            // prepare signatures
            string pubkStr;
            pubk.serializekeyforjs(pubkStr);
            signkey->signKey((unsigned char*)pubkStr.data(), pubkStr.size(), &sigPubk);
            signkey->signKey(chatkey->pubKey, ECDH::PUBLIC_KEY_LENGTH, &sigCu255);

            // store keys into user attributes (skipping the procresult() <-- reqtag=0)
            userattr_map attrs;
            string buf;

            buf.assign(tlvContainer->data(), tlvContainer->size());
            attrs[ATTR_KEYRING] = buf;

            buf.assign((const char *) signkey->pubKey, EdDSA::PUBLIC_KEY_LENGTH);
            attrs[ATTR_ED25519_PUBK] = buf;

            buf.assign((const char *) chatkey->pubKey, ECDH::PUBLIC_KEY_LENGTH);
            attrs[ATTR_CU25519_PUBK] = buf;

            buf.assign(sigPubk.data(), sigPubk.size());
            attrs[ATTR_SIG_RSA_PUBK] = buf;

            buf.assign(sigCu255.data(), sigCu255.size());
            attrs[ATTR_SIG_CU255_PUBK] = buf;

            putua(&attrs, 0);

            delete tlvContainer;
            delete chatkey;
            delete signkey; // MegaClient::signkey & chatkey are created on putua::procresult()

            LOG_info << "Creating new keypairs and signatures";
            fetchingkeys = false;
            return;
        }
    }
    else    // there is chatkey but no signing key, or viceversa
    {
        LOG_warn << "Keyring exists, but it's incomplete.";

        int creqtag = reqtag;
        reqtag = 0;
        if (!chatkey)
        {
            sendevent(99416, "Incomplete keyring detected: private key for Cu25519 not found.");
        }
        else // !signkey
        {
            sendevent(99423, "Incomplete keyring detected: private key for Ed25519 not found.");
        }
        reqtag = creqtag;

        resetKeyring();
        clearKeys();
        return;
    }
}
#endif  // ENABLE_CHAT

void MegaClient::purgenodesusersabortsc()
{
    app->clearing();

    while (!hdrns.empty())
    {
        delete hdrns.begin()->second;
    }

#ifdef ENABLE_SYNC
    for (sync_list::iterator it = syncs.begin(); it != syncs.end(); )
    {
        (*it)->changestate(SYNC_CANCELED);
        delete *(it++);
    }

    syncs.clear();
#endif

    for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++)
    {
        delete it->second;
    }

    nodes.clear();

#ifdef ENABLE_SYNC
    todebris.clear();
    tounlink.clear();
    fingerprints.clear();
#endif

    for (fafc_map::iterator cit = fafcs.begin(); cit != fafcs.end(); cit++)
    {
        for (int i = 2; i--; )
        {
            for (faf_map::iterator it = cit->second->fafs[i].begin(); it != cit->second->fafs[i].end(); it++)
            {
                delete it->second;
            }

            cit->second->fafs[i].clear();
        }
    }

    for (newshare_list::iterator it = newshares.begin(); it != newshares.end(); it++)
    {
        delete *it;
    }

    newshares.clear();

    nodenotify.clear();
    usernotify.clear();
    pcrnotify.clear();

#ifndef ENABLE_CHAT
    users.clear();
    uhindex.clear();
    umindex.clear();
#else
    for (textchat_map::iterator it = chats.begin(); it != chats.end();)
    {
        delete it->second;
        chats.erase(it++);
    }
    chatnotify.clear();

    for (user_map::iterator it = users.begin(); it != users.end(); )
    {
        User *u = &(it->second);
        if (u->userhandle != me || u->userhandle == UNDEF)
        {
            umindex.erase(u->email);
            uhindex.erase(u->userhandle);
            users.erase(it++);
        }
        else
        {
            u->dbid = 0;
            u->notified = false;
            it++;
        }
    }

    assert(users.size() <= 1 && uhindex.size() <= 1 && umindex.size() <= 1);
#endif

    for (handlepcr_map::iterator it = pcrindex.begin(); it != pcrindex.end(); it++)
    {
        delete it->second;
    }

    pcrindex.clear();

    *scsn = 0;

    if (pendingsc)
    {
        app->request_response_progress(-1, -1);
        pendingsc->disconnect();
    }

    init();
}

// request direct read by node pointer
void MegaClient::pread(Node* n, m_off_t count, m_off_t offset, void* appdata)
{
    queueread(n->nodehandle, true, n->nodecipher(), MemAccess::get<int64_t>((const char*)n->nodekey.data() + SymmCipher::KEYLENGTH), count, offset, appdata);
}

// request direct read by exported handle / key
void MegaClient::pread(handle ph, SymmCipher* key, int64_t ctriv, m_off_t count, m_off_t offset, void* appdata, bool isforeign)
{
    queueread(ph, isforeign, key, ctriv, count, offset, appdata);
}

// since only the first six bytes of a handle are in use, we use the seventh to encode its type
void MegaClient::encodehandletype(handle* hp, bool p)
{
    if (p)
    {
        ((char*)hp)[NODEHANDLE] = 1;
    }
}

bool MegaClient::isprivatehandle(handle* hp)
{
    return ((char*)hp)[NODEHANDLE] != 0;
}

void MegaClient::queueread(handle h, bool p, SymmCipher* key, int64_t ctriv, m_off_t offset, m_off_t count, void* appdata)
{
    handledrn_map::iterator it;

    encodehandletype(&h, p);

    it = hdrns.find(h);

    if (it == hdrns.end())
    {
        // this handle is not being accessed yet: insert
        it = hdrns.insert(hdrns.end(), pair<handle, DirectReadNode*>(h, new DirectReadNode(this, h, p, key, ctriv)));
        it->second->hdrn_it = it;
        it->second->enqueue(offset, count, reqtag, appdata);

        if (overquotauntil && overquotauntil > Waiter::ds)
        {
            dstime timeleft = overquotauntil - Waiter::ds;
            app->pread_failure(API_EOVERQUOTA, 0, appdata, timeleft);
            it->second->schedule(timeleft);
        }
        else
        {
            it->second->dispatch();
        }
    }
    else
    {
        it->second->enqueue(offset, count, reqtag, appdata);
        if (overquotauntil && overquotauntil > Waiter::ds)
        {
            dstime timeleft = overquotauntil - Waiter::ds;
            app->pread_failure(API_EOVERQUOTA, 0, appdata, timeleft);
            it->second->schedule(timeleft);
        }
    }
}

// cancel direct read by node pointer / count / count
void MegaClient::preadabort(Node* n, m_off_t offset, m_off_t count)
{
    abortreads(n->nodehandle, true, offset, count);
}

// cancel direct read by exported handle / offset / count
void MegaClient::preadabort(handle ph, m_off_t offset, m_off_t count)
{
    abortreads(ph, false, offset, count);
}

void MegaClient::abortreads(handle h, bool p, m_off_t offset, m_off_t count)
{
    handledrn_map::iterator it;
    DirectReadNode* drn;

    encodehandletype(&h, p);
    
    if ((it = hdrns.find(h)) != hdrns.end())
    {
        drn = it->second;

        for (dr_list::iterator it = drn->reads.begin(); it != drn->reads.end(); )
        {
            if ((offset < 0 || offset == (*it)->offset) && (count < 0 || count == (*it)->count))
            {
                app->pread_failure(API_EINCOMPLETE, (*it)->drn->retries, (*it)->appdata, 0);

                delete *(it++);
            }
            else it++;
        }
    }
}

// execute pending directreads
bool MegaClient::execdirectreads()
{
    bool r = false;
    DirectReadSlot* drs;

    if (drq.size() < MAXDRSLOTS)
    {
        // fill slots
        for (dr_list::iterator it = drq.begin(); it != drq.end(); it++)
        {
            if (!(*it)->drs)
            {
                drs = new DirectReadSlot(*it);
                (*it)->drs = drs;
                r = true;

                if (drq.size() >= MAXDRSLOTS) break;
            }
        }
    }

    // perform slot I/O
    for (drs_list::iterator it = drss.begin(); it != drss.end(); )
    {
        if ((*(it++))->doio())
        {
            r = true;
            break;
        }
    }

    while (!dsdrns.empty() && dsdrns.begin()->first <= Waiter::ds)
    {
        if (dsdrns.begin()->second->reads.size() && (dsdrns.begin()->second->tempurl.size() || dsdrns.begin()->second->pendingcmd))
        {
            LOG_warn << "DirectRead scheduled retry";
            dsdrns.begin()->second->retry(API_EAGAIN);
        }
        else
        {
            LOG_debug << "Dispatching scheduled streaming";
            dsdrns.begin()->second->dispatch();
        }
    }

    return r;
}

// recreate filenames of active PUT transfers
void MegaClient::updateputs()
{
    for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); it++)
    {
        if ((*it)->transfer->type == PUT && (*it)->transfer->files.size())
        {
            (*it)->transfer->files.front()->prepare();
        }
    }
}

error MegaClient::isnodesyncable(Node *remotenode, bool *isinshare)
{
#ifdef ENABLE_SYNC
    // cannot sync files, rubbish bins or inboxes
    if (remotenode->type != FOLDERNODE && remotenode->type != ROOTNODE)
    {
        return API_EACCESS;
    }

    Node* n;
    bool inshare;

    // any active syncs below?
    for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
    {
        if ((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN)
        {
            n = (*it)->localroot.node;

            do {
                if (n == remotenode)
                {
                    return API_EEXIST;
                }
            } while ((n = n->parent));
        }
    }

    // any active syncs above?
    n = remotenode;
    inshare = false;

    do {
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            if (((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN)
             && n == (*it)->localroot.node)
            {
                return API_EEXIST;
            }
        }

        if (n->inshare && !inshare)
        {
            // we need FULL access to sync
            // FIXME: allow downsyncing from RDONLY and limited syncing to RDWR shares
            if (n->inshare->access != FULL) return API_EACCESS;

            inshare = true;
        }
    } while ((n = n->parent));

    if (inshare)
    {
        // this sync is located in an inbound share - make sure that there
        // are no access restrictions in place anywhere in the sync's tree
        for (user_map::iterator uit = users.begin(); uit != users.end(); uit++)
        {
            User* u = &uit->second;

            if (u->sharing.size())
            {
                for (handle_set::iterator sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
                {
                    if ((n = nodebyhandle(*sit)) && n->inshare && n->inshare->access != FULL)
                    {
                        do {
                            if (n == remotenode)
                            {
                                return API_EACCESS;
                            }
                        } while ((n = n->parent));
                    }
                }
            }
        }
    }

    if (isinshare)
    {
        *isinshare = inshare;
    }
    return API_OK;
#else
    return API_EINCOMPLETE;
#endif
}

// check sync path, add sync if folder
// disallow nested syncs (there is only one LocalNode pointer per node)
// (FIXME: perform the same check for local paths!)
error MegaClient::addsync(string* rootpath, const char* debris, string* localdebris, Node* remotenode, fsfp_t fsfp, int tag, void *appData)
{
#ifdef ENABLE_SYNC
    bool inshare = false;
    error e = isnodesyncable(remotenode, &inshare);
    if (e)
    {
        return e;
    }

    if (rootpath->size() >= fsaccess->localseparator.size()
     && !memcmp(rootpath->data() + (rootpath->size() & -fsaccess->localseparator.size()) - fsaccess->localseparator.size(),
                fsaccess->localseparator.data(),
                fsaccess->localseparator.size()))
    {
        rootpath->resize((rootpath->size() & -fsaccess->localseparator.size()) - fsaccess->localseparator.size());
    }
    
    bool isnetwork = false;
    if (!fsaccess->issyncsupported(rootpath, &isnetwork))
    {
        LOG_warn << "Unsupported filesystem";
        return API_EFAILED;
    }

    FileAccess* fa = fsaccess->newfileaccess();
    if (fa->fopen(rootpath, true, false))
    {
        if (fa->type == FOLDERNODE)
        {
            string utf8path;
            fsaccess->local2path(rootpath, &utf8path);
            LOG_debug << "Adding sync: " << utf8path;

            Sync* sync = new Sync(this, rootpath, debris, localdebris, remotenode, fsfp, inshare, tag, appData);
            sync->isnetwork = isnetwork;

            if (sync->scan(rootpath, fa))
            {
                syncsup = false;
                e = API_OK;
                sync->initializing = false;
            }
            else
            {
                LOG_err << "Initial scan failed";
                sync->changestate(SYNC_FAILED);
                delete sync;
                e = API_EFAILED;
            }

            syncactivity = true;
        }
        else
        {
            e = API_EACCESS;    // cannot sync individual files
        }
    }
    else
    {
        e = fa->retry ? API_ETEMPUNAVAIL : API_ENOENT;
    }

    delete fa;

    return e;
#else
    return API_EINCOMPLETE;
#endif
}

#ifdef ENABLE_SYNC
// syncids are usable to indicate putnodes()-local parent linkage
handle MegaClient::nextsyncid()
{
    byte* ptr = (byte*)&currsyncid;

    while (!++*ptr && ptr < (byte*)&currsyncid + NODEHANDLE)
    {
        ptr++;
    }

    return currsyncid;
}

// recursively stop all transfers
void MegaClient::stopxfers(LocalNode* l)
{
    if (l->type != FILENODE)
    {
        for (localnode_map::iterator it = l->children.begin(); it != l->children.end(); it++)
        {
            stopxfers(it->second);
        }
    }
  
    stopxfer(l);
}

// add child to nchildren hash (deterministically prefer newer/larger versions
// of identical names to avoid flapping)
// apply standard unescaping, if necessary (use *strings as ephemeral storage
// space)
void MegaClient::addchild(remotenode_map* nchildren, string* name, Node* n, list<string>* strings) const
{
    Node** npp;

    if (name->find('%') + 1)
    {
        string tmplocalname;

        // perform one round of unescaping to ensure that the resulting local
        // filename matches
        fsaccess->path2local(name, &tmplocalname);
        fsaccess->local2name(&tmplocalname);

        strings->push_back(tmplocalname);
        name = &strings->back();
    }

    npp = &(*nchildren)[name];

    if (!*npp
     || n->mtime > (*npp)->mtime
     || (n->mtime == (*npp)->mtime && n->size > (*npp)->size)
     || (n->mtime == (*npp)->mtime && n->size == (*npp)->size && memcmp(n->crc, (*npp)->crc, sizeof n->crc) > 0))
    {
        *npp = n;
    }
}

// downward sync - recursively scan for tree differences and execute them locally
// this is first called after the local node tree is complete
// actions taken:
// * create missing local folders
// * initiate GET transfers to missing local files (but only if the target
// folder was created successfully)
// * attempt to execute renames, moves and deletions (deletions require the
// rubbish flag to be set)
// returns false if any local fs op failed transiently
bool MegaClient::syncdown(LocalNode* l, string* localpath, bool rubbish)
{
    // only use for LocalNodes with a corresponding and properly linked Node
    if (l->type != FOLDERNODE || !l->node || (l->parent && l->node->parent->localnode != l->parent))
    {
        return true;
    }

    list<string> strings;
    remotenode_map nchildren;
    remotenode_map::iterator rit;

    bool success = true;

    // build array of sync-relevant (in case of clashes, the newest alias wins)
    // remote children by name
    string localname;

    // build child hash - nameclash resolution: use newest/largest version
    for (node_list::iterator it = l->node->children.begin(); it != l->node->children.end(); it++)
    {
        attr_map::iterator ait;        

        // node must be syncable, alive, decrypted and have its name defined to
        // be considered - also, prevent clashes with the local debris folder
        if (((*it)->syncdeleted == SYNCDEL_NONE
             && !(*it)->attrstring
             && (ait = (*it)->attrs.map.find('n')) != (*it)->attrs.map.end()
             && ait->second.size())
         && (l->parent || l->sync->debris != ait->second))
        {
            size_t t = localpath->size();
            string localname = ait->second;
            fsaccess->name2local(&localname);
            localpath->append(fsaccess->localseparator);
            localpath->append(localname);
            if (app->sync_syncable(l->sync, ait->second.c_str(), localpath, *it))
            {
                addchild(&nchildren, &ait->second, *it, &strings);
            }
            else
            {
                LOG_debug << "Node excluded " << LOG_NODEHANDLE((*it)->nodehandle) << "  Name: " << (*it)->displayname();
            }
            localpath->resize(t);
        }
        else
        {
            LOG_debug << "Node skipped " << LOG_NODEHANDLE((*it)->nodehandle) << "  Name: " << (*it)->displayname();
        }
    }

    // remove remote items that exist locally from hash, recurse into existing folders
    for (localnode_map::iterator lit = l->children.begin(); lit != l->children.end(); )
    {
        LocalNode* ll = lit->second;

        rit = nchildren.find(&ll->name);

        size_t t = localpath->size();

        localpath->append(fsaccess->localseparator);
        localpath->append(ll->localname);

        // do we have a corresponding remote child?
        if (rit != nchildren.end())
        {
            // corresponding remote node exists
            // local: folder, remote: file - ignore
            // local: file, remote: folder - ignore
            // local: folder, remote: folder - recurse
            // local: file, remote: file - overwrite if newer
            if (ll->type != rit->second->type)
            {
                // folder/file clash: do nothing (rather than attempting to
                // second-guess the user)
                LOG_warn << "Type changed: " << ll->name << " LNtype: " << ll->type << " Ntype: " << rit->second->type;
                nchildren.erase(rit);
            }
            else if (ll->type == FILENODE)
            {
                if (ll->node != rit->second)
                {
                    ll->sync->statecacheadd(ll);
                }

                ll->setnode(rit->second);

                // file exists on both sides - do not overwrite if local version newer or same
                if (ll->mtime > rit->second->mtime)
                {
                    // local version is newer
                    LOG_debug << "LocalNode is newer: " << ll->name << " LNmtime: " << ll->mtime << " Nmtime: " << rit->second->mtime;
                    nchildren.erase(rit);
                }
                else if (ll->mtime == rit->second->mtime
                         && (ll->size > rit->second->size
                             || (ll->size == rit->second->size && memcmp(ll->crc, rit->second->crc, sizeof ll->crc) > 0)))

                {
                    if (ll->size < rit->second->size)
                    {
                        LOG_warn << "Syncdown. Same mtime but lower size: " << ll->name
                                 << " mtime: " << ll->mtime << " LNsize: " << ll->size << " Nsize: " << rit->second->size
                                 << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);
                    }
                    else
                    {
                        LOG_warn << "Syncdown. Same mtime and size, but lower CRC: " << ll->name
                                 << " mtime: " << ll->mtime << " size: " << ll->size << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);
                    }

                    nchildren.erase(rit);
                }
                else if (*ll == *(FileFingerprint*)rit->second)
                {
                    // both files are identical
                    nchildren.erase(rit);
                }
                else
                {
                    // means that the localnode is going to be overwritten
                    if (rit->second->localnode && rit->second->localnode->transfer)
                    {
                        LOG_debug << "Stopping an unneeded upload";
                        stopxfer(rit->second->localnode);
                    }

                    rit->second->localnode = (LocalNode*)~0;
                }
            }
            else
            {
                if (ll->node != rit->second)
                {
                    ll->setnode(rit->second);
                    ll->sync->statecacheadd(ll);
                }

                // recurse into directories of equal name
                if (!syncdown(ll, localpath, rubbish) && success)
                {
                    success = false;
                }

                nchildren.erase(rit);
            }

            lit++;
        }
        else if (rubbish && ll->deleted)    // no corresponding remote node: delete local item
        {
            if (ll->type == FILENODE)
            {
                // only delete the file if it is unchanged
                string tmplocalpath;

                ll->getlocalpath(&tmplocalpath);

                FileAccess* fa = fsaccess->newfileaccess();

                if (fa->fopen(&tmplocalpath, true, false))
                {
                    FileFingerprint fp;
                    fp.genfingerprint(fa);

                    if (!(fp == *(FileFingerprint*)ll))
                    {
                        ll->deleted = false;
                    }
                }

                delete fa;
            }

            if (ll->deleted)
            {
                // attempt deletion and re-queue for retry in case of a transient failure
                ll->treestate(TREESTATE_SYNCING);

                if (l->sync->movetolocaldebris(localpath) || !fsaccess->transient_error)
                {
                    delete lit++->second;
                }
                else
                {
                    fsaccess->local2path(localpath, &blockedfile);
                    success = false;
                    lit++;
                }
            }
        }
        else
        {
            lit++;
        }

        localpath->resize(t);
    }

    // create/move missing local folders / FolderNodes, initiate downloads of
    // missing local files
    for (rit = nchildren.begin(); rit != nchildren.end(); rit++)
    {
        size_t t = localpath->size();

        localname = rit->second->attrs.map.find('n')->second;

        fsaccess->name2local(&localname);
        localpath->append(fsaccess->localseparator);
        localpath->append(localname);

        string utf8path;
        fsaccess->local2path(localpath, &utf8path);
        LOG_debug << "Unsynced remote node in syncdown: " << utf8path << " Nsize: " << rit->second->size
                  << " Nmtime: " << rit->second->mtime << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);

        // does this node already have a corresponding LocalNode under
        // a different name or elsewhere in the filesystem?
        if (rit->second->localnode && rit->second->localnode != (LocalNode*)~0)
        {
            LOG_debug << "has a previous localnode: " << rit->second->localnode->name;
            if (rit->second->localnode->parent)
            {
                LOG_debug << "with a previous parent: " << rit->second->localnode->parent->name;
                string curpath;

                rit->second->localnode->getlocalpath(&curpath);
                rit->second->localnode->treestate(TREESTATE_SYNCING);

                LOG_debug << "Renaming/moving from the previous location to the new one";
                if (fsaccess->renamelocal(&curpath, localpath))
                {
                    fsaccess->local2path(localpath, &localname);
                    app->syncupdate_local_move(rit->second->localnode->sync,
                                               rit->second->localnode, localname.c_str());

                    // update LocalNode tree to reflect the move/rename
                    rit->second->localnode->setnameparent(l, localpath);

                    rit->second->localnode->sync->statecacheadd(rit->second->localnode);

                    // update filenames so that PUT transfers can continue seamlessly
                    updateputs();
                    syncactivity = true;

                    rit->second->localnode->treestate(TREESTATE_SYNCED);
                }
                else if (success && fsaccess->transient_error)
                {
                    // schedule retry
                    fsaccess->local2path(&curpath, &blockedfile);
                    LOG_debug << "Transient error moving localnode";
                    success = false;
                }
            }
            else
            {
                LOG_debug << "without a previous parent. Skipping";
            }
        }
        else
        {
            LOG_debug << "doesn't have a previous localnode";
            // missing node is not associated with an existing LocalNode
            if (rit->second->type == FILENODE)
            {
                bool download = true;
                FileAccess *f = fsaccess->newfileaccess();
                if (rit->second->localnode != (LocalNode*)~0
                        && f->fopen(localpath, true, false))
                {
                    LOG_debug << "Skipping download over an unscanned file/folder";
                    download = false;
                }
                delete f;
                rit->second->localnode = NULL;

                // start fetching this node, unless fetch is already in progress
                // FIXME: to cover renames that occur during the
                // download, reconstruct localname in complete()
                if (download && !rit->second->syncget)
                {
                    LOG_debug << "Start fetching file node";
                    fsaccess->local2path(localpath, &localname);
                    app->syncupdate_get(l->sync, rit->second, localname.c_str());

                    rit->second->syncget = new SyncFileGet(l->sync, rit->second, localpath);
                    nextreqtag();
                    startxfer(GET, rit->second->syncget);
                    syncactivity = true;
                }
            }
            else
            {
                LOG_debug << "Creating local folder";

                // create local path, add to LocalNodes and recurse
                if (fsaccess->mkdirlocal(localpath))
                {
                    LocalNode* ll = l->sync->checkpath(l, localpath, &localname);

                    if (ll && ll != (LocalNode*)~0)
                    {
                        LOG_debug << "Local folder created, continuing syncdown";

                        ll->setnode(rit->second);
                        ll->sync->statecacheadd(ll);

                        if (!syncdown(ll, localpath, rubbish) && success)
                        {
                            LOG_debug << "Syncdown not finished";
                            success = false;
                        }
                    }
                    else
                    {
                        LOG_debug << "Checkpath() failed " << (ll == NULL);
                    }
                }
                else if (success && fsaccess->transient_error)
                {
                    fsaccess->local2path(localpath, &blockedfile);
                    LOG_debug << "Transient error creating folder";
                    success = false;
                }
                else if (!fsaccess->transient_error)
                {
                    LOG_debug << "Non transient error creating folder";
                }
            }
        }

        localpath->resize(t);
    }

    return success;
}

// recursively traverse tree of LocalNodes and match with remote Nodes
// mark nodes to be rubbished in deleted. with their nodehandle
// mark additional nodes to to rubbished (those overwritten) by accumulating
// their nodehandles in rubbish.
// nodes to be added are stored in synccreate. - with nodehandle set to parent
// if attached to an existing node
// l and n are assumed to be folders and existing on both sides or scheduled
// for creation
bool MegaClient::syncup(LocalNode* l, dstime* nds)
{
    bool insync = true;

    list<string> strings;
    remotenode_map nchildren;
    remotenode_map::iterator rit;

    // build array of sync-relevant (newest alias wins) remote children by name
    attr_map::iterator ait;

    // UTF-8 converted local name
    string localname;

    if (l->node)
    {
        // corresponding remote node present: build child hash - nameclash
        // resolution: use newest version
        for (node_list::iterator it = l->node->children.begin(); it != l->node->children.end(); it++)
        {
            // node must be alive
            if ((*it)->syncdeleted == SYNCDEL_NONE)
            {
                // check if there is a crypto key missing...
                if ((*it)->attrstring)
                {
                    if (!l->reported)
                    {
                        char* buf = new char[(*it)->nodekey.size() * 4 / 3 + 4];
                        Base64::btoa((byte *)(*it)->nodekey.data(), (*it)->nodekey.size(), buf);

                        LOG_warn << "Sync: Undecryptable child node. " << buf;

                        l->reported = true;

                        char report[256];

                        Base64::btoa((const byte *)&(*it)->nodehandle, MegaClient::NODEHANDLE, report);
                        
                        sprintf(report + 8, " %d %.200s", (*it)->type, buf);

                        // report an "undecrypted child" event
                        int creqtag = reqtag;
                        reqtag = 0;
                        reportevent("CU", report);
                        reqtag = creqtag;

                        delete [] buf;
                    }

                    continue;
                }

                // ...or a node name attribute missing
                if ((ait = (*it)->attrs.map.find('n')) == (*it)->attrs.map.end())
                {
                    LOG_warn << "Node name missing, not syncing subtree: " << l->name.c_str();

                    if (!l->reported)
                    {
                        l->reported = true;

                        // report a "no-name child" event
                        int creqtag = reqtag;
                        reqtag = 0;
                        reportevent("CN");
                        reqtag = creqtag;
                    }

                    continue;
                }

                addchild(&nchildren, &ait->second, *it, &strings);
            }
        }
    }

    // check for elements that need to be created, deleted or updated on the
    // remote side
    for (localnode_map::iterator lit = l->children.begin(); lit != l->children.end(); lit++)
    {
        LocalNode* ll = lit->second;

        if (ll->deleted)
        {
            LOG_debug << "LocalNode deleted " << ll->name;
            continue;
        }

        localname = *lit->first;
        fsaccess->local2name(&localname);
        if (!localname.size() || !ll->name.size())
        {
            if (!ll->reported)
            {
                ll->reported = true;

                char report[256];
                sprintf(report, "%d %d %d %d", (int)lit->first->size(), (int)localname.size(), (int)ll->name.size(), (int)ll->type);

                // report a "no-name localnode" event
                int creqtag = reqtag;
                reqtag = 0;
                reportevent("LN", report);
                reqtag = creqtag;
            }
            continue;
        }

        rit = nchildren.find(&localname);

        // do we have a corresponding remote child?
        if (rit != nchildren.end())
        {
            // corresponding remote node exists
            // local: folder, remote: file - overwrite
            // local: file, remote: folder - overwrite
            // local: folder, remote: folder - recurse
            // local: file, remote: file - overwrite if newer
            if (ll->type != rit->second->type)
            {
                insync = false;
                LOG_warn << "Type changed: " << localname << " LNtype: " << ll->type << " Ntype: " << rit->second->type;
                movetosyncdebris(rit->second, l->sync->inshare);
            }
            else
            {
                // file on both sides - do not overwrite if local version older or identical
                if (ll->type == FILENODE)
                {
                    // skip if remote file is newer
                    if (ll->mtime < rit->second->mtime)
                    {
                        LOG_debug << "LocalNode is older: " << ll->name << " LNmtime: " << ll->mtime << " Nmtime: " << rit->second->mtime;
                        continue;
                    }                           

                    if (ll->mtime == rit->second->mtime)
                    {
                        if (ll->size < rit->second->size)
                        {
                            LOG_warn << "Syncup. Same mtime but lower size: " << ll->name
                                     << " LNmtime: " << ll->mtime << " LNsize: " << ll->size << " Nsize: " << rit->second->size
                                     << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle) ;

                            continue;
                        }

                        if (ll->size == rit->second->size && memcmp(ll->crc, rit->second->crc, sizeof ll->crc) < 0)
                        {
                            LOG_warn << "Syncup. Same mtime and size, but lower CRC: " << ll->name
                                     << " mtime: " << ll->mtime << " size: " << ll->size << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);

                            continue;
                        }
                    }

                    if (ll->node != rit->second)
                    {
                        ll->sync->statecacheadd(ll);
                    }

                    ll->setnode(rit->second);

                    if (ll->size == rit->second->size)
                    {
                        // check if file is likely to be identical
                        if (rit->second->isvalid
                          ? *ll == *(FileFingerprint*)rit->second
                          : (ll->mtime == rit->second->mtime))
                        {
                            // files have the same size and the same mtime (or the
                            // same fingerprint, if available): no action needed
                            if (!ll->checked)
                            {                                
                                if (gfx && gfx->isgfx(&ll->localname))
                                {
                                    int missingattr = 0;

                                    // check for missing imagery
                                    if (!ll->node->hasfileattribute(GfxProc::THUMBNAIL))
                                    {
                                        missingattr |= 1 << GfxProc::THUMBNAIL;
                                    }

                                    if (!ll->node->hasfileattribute(GfxProc::PREVIEW))
                                    {
                                        missingattr |= 1 << GfxProc::PREVIEW;
                                    }

                                    if (missingattr && checkaccess(ll->node, OWNER)
                                            && !gfx->isvideo(&ll->localname))
                                    {
                                        char me64[12];
                                        Base64::btoa((const byte*)&me, MegaClient::USERHANDLE, me64);
                                        if (ll->node->attrs.map.find('f') == ll->node->attrs.map.end() || ll->node->attrs.map['f'] != me64)
                                        {
                                            LOG_debug << "Restoring missing attributes: " << ll->name;
                                            string localpath;
                                            ll->getlocalpath(&localpath);
                                            SymmCipher *symmcipher = ll->node->nodecipher();
                                            gfx->gendimensionsputfa(NULL, &localpath, ll->node->nodehandle, symmcipher, missingattr);
                                        }
                                    }
                                }

                                ll->checked = true;
                            }

                            // if this node is being fetched, but it's already synced
                            if (rit->second->syncget)
                            {
                                LOG_debug << "Stopping unneeded download";
                                delete rit->second->syncget;
                                rit->second->syncget = NULL;
                            }

                            // if this localnode is being uploaded, but it's already synced
                            if (ll->transfer)
                            {
                                LOG_debug << "Stopping unneeded upload";
                                stopxfer(ll);
                            }

                            ll->treestate(TREESTATE_SYNCED);
                            continue;
                        }
                    }

                    LOG_debug << "LocalNode change detected on syncupload: " << ll->name << " LNsize: " << ll->size << " LNmtime: " << ll->mtime
                              << " NSize: " << rit->second->size << " Nmtime: " << rit->second->mtime << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);

#ifdef WIN32
                    if(ll->size == ll->node->size && !memcmp(ll->crc, ll->node->crc, sizeof(ll->crc)))
                    {
                        LOG_debug << "Modification time changed only";
                        FileAccess *f = fsaccess->newfileaccess();
                        string lpath;
                        ll->getlocalpath(&lpath);
                        string stream = lpath;
                        stream.append((char *)L":$CmdTcID:$DATA", 30);
                        if (f->fopen(&stream))
                        {
                            LOG_warn << "COMODO detected";
                            HKEY hKey;
                            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                                            L"SYSTEM\\CurrentControlSet\\Services\\CmdAgent\\CisConfigs\\0\\HIPS\\SBSettings",
                                            0,
                                            KEY_QUERY_VALUE,
                                            &hKey ) == ERROR_SUCCESS)
                            {
                                DWORD value = 0;
                                DWORD size = sizeof(value);
                                if (RegQueryValueEx(hKey, L"EnableSourceTracking", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS)
                                {
                                    if (value == 1 && fsaccess->setmtimelocal(&lpath, ll->node->mtime))
                                    {
                                        LOG_warn << "Fixed modification time probably changed by COMODO";
                                        ll->mtime = ll->node->mtime;
                                        ll->treestate(TREESTATE_SYNCED);
                                        RegCloseKey(hKey);
                                        delete f;
                                        continue;
                                    }
                                }
                                RegCloseKey(hKey);
                            }
                        }

                        lpath.append((char *)L":OECustomProperty", 34);
                        if (f->fopen(&lpath))
                        {
                            LOG_warn << "Windows Search detected";
                            delete f;
                            continue;
                        }

                        delete f;
                    }
#endif

                    // if this node is being fetched, but has to be upsynced
                    if (rit->second->syncget)
                    {
                        LOG_debug << "Stopping unneeded download";
                        delete rit->second->syncget;
                        rit->second->syncget = NULL;
                    }
                }
                else
                {
                    insync = false;

                    if (ll->node != rit->second)
                    {
                        ll->setnode(rit->second);
                        ll->sync->statecacheadd(ll);
                    }

                    // recurse into directories of equal name
                    if (!syncup(ll, nds))
                    {
                        return false;
                    }
                    continue;
                }
            }
        }

        if (ll->type == FILENODE)
        {
            // do not begin transfer until the file size / mtime has stabilized
            insync = false;

            if (ll->transfer)
            {
                continue;
            }

            LOG_verbose << "Unsynced LocalNode (file): " << ll->name << " " << ll << " " << (ll->transfer != 0);
            ll->treestate(TREESTATE_PENDING);

            if (Waiter::ds < ll->nagleds)
            {
                LOG_debug << "Waiting for the upload delay: " << ll->name << " " << ll->nagleds;
                if (ll->nagleds < *nds)
                {
                    *nds = ll->nagleds;
                }

                continue;
            }
            else
            {
                Node *currentVersion = ll->node;
                if (currentVersion)
                {
                    m_time_t delay = 0;
                    m_time_t currentTime = time(NULL);
                    if (currentVersion->ctime > currentTime + 30)
                    {
                        // with more than 30 seconds of detecteed clock drift,
                        // we don't apply any version rate control for now
                        LOG_err << "Incorrect local time detected";
                    }
                    else
                    {
                        int recentVersions = 0;
                        m_time_t startInterval = currentTime - Sync::RECENT_VERSION_INTERVAL_SECS;
                        Node *version = currentVersion;
                        while (true)
                        {
                            if (version->ctime < startInterval)
                            {
                                break;
                            }

                            recentVersions++;
                            if (!version->children.size())
                            {
                                break;
                            }

                            version = version->children.back();
                        }

                        if (recentVersions > 10)
                        {
                            // version rate control starts with more than 10 recent versions
                            delay = 7 * (recentVersions / 10) * (recentVersions - 10);
                        }

                        LOG_debug << "Number of recent versions: " << recentVersions << " delay: " << delay
                                  << " prev: " << currentVersion->ctime << " current: " << currentTime;
                    }

                    if (delay)
                    {
                        m_time_t next = currentVersion->ctime + delay;
                        if (next > currentTime)
                        {
                            dstime backoffds = (next - currentTime) * 10;
                            ll->nagleds = waiter->ds + backoffds;
                            LOG_debug << "Waiting for the version rate limit delay during " << backoffds << " ds";

                            if (ll->nagleds < *nds)
                            {
                                *nds = ll->nagleds;
                            }
                            continue;
                        }
                        else
                        {
                            LOG_debug << "Version rate limit delay already expired";
                        }
                    }
                }

                string localpath;
                bool t;
                FileAccess* fa = fsaccess->newfileaccess();

                ll->getlocalpath(&localpath);

                if (!(t = fa->fopen(&localpath, true, false))
                 || fa->size != ll->size
                 || fa->mtime != ll->mtime)
                {
                    if (t)
                    {
                        ll->sync->localbytes -= ll->size;
                        ll->genfingerprint(fa);
                        ll->sync->localbytes += ll->size;                        

                        ll->sync->statecacheadd(ll);
                    }

                    ll->bumpnagleds();

                    LOG_debug << "Localnode not stable yet: " << ll->name << " " << t << " " << fa->size << " " << ll->size
                              << " " << fa->mtime << " " << ll->mtime << " " << ll->nagleds;

                    delete fa;

                    if (ll->nagleds < *nds)
                    {
                        *nds = ll->nagleds;
                    }

                    continue;
                }

                delete fa;
                
                ll->created = false;
            }
        }
        else
        {
            LOG_verbose << "Unsynced LocalNode (folder): " << ll->name;
        }

        if (ll->created)
        {
            if (!ll->reported)
            {
                ll->reported = true;

                // FIXME: remove created flag and associated safeguards after
                // positively verifying the absence of a related repetitive node creation bug
                LOG_err << "Internal error: Duplicate node creation: " << ll->name.c_str();

                char report[256];

                // always report LocalNode's type, name length, mtime, file size
                sprintf(report, "[%u %u %d %d %d] %d %d %d %d %d %" PRIi64,
                    (int)nchildren.size(),
                    (int)l->children.size(),
                    l->node ? (int)l->node->children.size() : -1,
                    (int)synccreate.size(),
                    syncadding,
                    ll->type,
                    (int)ll->name.size(),
                    (int)ll->mtime,
                    (int)ll->sync->state,
                    (int)ll->sync->inshare,
                    ll->size);

                if (ll->node)
                {
                    int namelen;

                    if ((ait = ll->node->attrs.map.find('n')) != ll->node->attrs.map.end())
                    {
                        namelen = ait->second.size();
                    }
                    else
                    {
                        namelen = -1;
                    }

                    // additionally, report corresponding Node's type, name length, mtime, file size and handle
                    sprintf(strchr(report, 0), " %d %d %d %" PRIi64 " %d ", ll->node->type, namelen, (int)ll->node->mtime, ll->node->size, ll->node->syncdeleted);
                    Base64::btoa((const byte *)&ll->node->nodehandle, MegaClient::NODEHANDLE, strchr(report, 0));
                }

                // report a "dupe" event
                int creqtag = reqtag;
                reqtag = 0;
                reportevent("D2", report);
                reqtag = creqtag;
            }
            else
            {
                LOG_err << "LocalNode created and reported " << ll->name;
            }
        }
        else
        {
            ll->created = true;

            // create remote folder or send file
            LOG_debug << "Adding local file to synccreate: " << ll->name << " " << synccreate.size();
            synccreate.push_back(ll);
            syncactivity = true;

            if (synccreate.size() >= MAX_NEWNODES)
            {
                LOG_warn << "Stopping syncup due to MAX_NEWNODES";
                return false;
            }
        }

        if (ll->type == FOLDERNODE)
        {
            if (!syncup(ll, nds))
            {
                return false;
            }
        }
    }

    if (insync && l->node)
    {
        l->treestate(TREESTATE_SYNCED);
    }

    return true;
}

// execute updates stored in synccreate[]
// must not be invoked while the previous creation operation is still in progress
void MegaClient::syncupdate()
{
    // split synccreate[] in separate subtrees and send off to putnodes() for
    // creation on the server
    unsigned i, start, end;
    SymmCipher tkey;
    string tattrstring;
    AttrMap tattrs;
    Node* n;
    NewNode* nn;
    NewNode* nnp;
    LocalNode* l;

    for (start = 0; start < synccreate.size(); start = end)
    {
        // determine length of distinct subtree beneath existing node
        for (end = start; end < synccreate.size(); end++)
        {
            if ((end > start) && synccreate[end]->parent->node)
            {
                break;
            }
        }

        // add nodes that can be created immediately: folders & existing files;
        // start uploads of new files
        nn = nnp = new NewNode[end - start];

        for (i = start; i < end; i++)
        {
            n = NULL;
            l = synccreate[i];

            if (l->type == FOLDERNODE || (n = nodebyfingerprint(l)))
            {
                // create remote folder or copy file if it already exists
                nnp->source = NEW_NODE;
                nnp->type = l->type;
                nnp->syncid = l->syncid;
                nnp->localnode = l;
                l->newnode = nnp;
                nnp->nodehandle = n ? n->nodehandle : l->syncid;
                nnp->parenthandle = i > start ? l->parent->syncid : UNDEF;

                if (n)
                {
                    // overwriting an existing remote node? tag it as the previous version or move to SyncDebris
                    if (l->node && l->node->parent && l->node->parent->localnode)
                    {
                        if (versions_disabled)
                        {
                            movetosyncdebris(l->node, l->sync->inshare);
                        }
                        else
                        {
                            nnp->ovhandle = l->node->nodehandle;
                        }
                    }

                    // this is a file - copy, use original key & attributes
                    // FIXME: move instead of creating a copy if it is in
                    // rubbish to reduce node creation load
                    nnp->nodekey = n->nodekey;
                    tattrs.map = n->attrs.map;

                    app->syncupdate_remote_copy(l->sync, l->name.c_str());
                }
                else
                {
                    // this is a folder - create, use fresh key & attributes
                    nnp->nodekey.resize(FOLDERNODEKEYLENGTH);
                    PrnGen::genblock((byte*)nnp->nodekey.data(), FOLDERNODEKEYLENGTH);
                    tattrs.map.clear();
                }

                // set new name, encrypt and attach attributes
                tattrs.map['n'] = l->name;
                tattrs.getjson(&tattrstring);
                tkey.setkey((const byte*)nnp->nodekey.data(), nnp->type);
                nnp->attrstring = new string;
                makeattr(&tkey, nnp->attrstring, tattrstring.c_str());

                l->treestate(TREESTATE_SYNCING);
                nnp++;
            }
            else if (l->type == FILENODE)
            {
                l->treestate(TREESTATE_PENDING);

                // the overwrite will happen upon PUT completion
                string tmppath, tmplocalpath;

                nextreqtag();
                startxfer(PUT, l);

                l->getlocalpath(&tmplocalpath, true);
                fsaccess->local2path(&tmplocalpath, &tmppath);
                app->syncupdate_put(l->sync, l, tmppath.c_str());
            }
        }

        if (nnp == nn)
        {
            delete[] nn;
        }
        else
        {
            // add nodes unless parent node has been deleted
            if (synccreate[start]->parent->node)
            {
                syncadding++;

                reqs.add(new CommandPutNodes(this,
                                                synccreate[start]->parent->node->nodehandle,
                                                NULL, nn, nnp - nn,
                                                synccreate[start]->sync->tag,
                                                PUTNODES_SYNC));

                syncactivity = true;
            }
        }
    }

    synccreate.clear();
}

void MegaClient::putnodes_sync_result(error e, NewNode* nn, int nni)
{
    // check for file nodes that failed to copy and remove them from fingerprints
    // FIXME: retrigger sync decision upload them immediately
    while (nni--)
    {
        Node* n;
        if (nn[nni].type == FILENODE && !nn[nni].added)
        {
            if ((n = nodebyhandle(nn[nni].nodehandle)))
            {
                if (n->fingerprint_it != fingerprints.end())
                {
                    fingerprints.erase(n->fingerprint_it);
                    n->fingerprint_it = fingerprints.end();
                }
            }
        }
        else if (nn[nni].localnode && (n = nn[nni].localnode->node))
        {
            if (n->type == FOLDERNODE)
            {
                app->syncupdate_remote_folder_addition(nn[nni].localnode->sync, n);
            }
            else
            {
                app->syncupdate_remote_file_addition(nn[nni].localnode->sync, n);
            }
        }

        if (e && e != API_EEXPIRED && nn[nni].localnode && nn[nni].localnode->sync)
        {
            nn[nni].localnode->sync->errorcode = e;
            nn[nni].localnode->sync->changestate(SYNC_FAILED);
        }
    }

    delete[] nn;

    syncadding--;
    syncactivity = true;
}

// move node to //bin, then on to the SyncDebris folder of the day (to prevent
// dupes)
void MegaClient::movetosyncdebris(Node* dn, bool unlink)
{
    dn->syncdeleted = SYNCDEL_DELETED;

    // detach node from LocalNode
    if (dn->localnode)
    {
        dn->tag = dn->localnode->sync->tag;
        dn->localnode->node = NULL;
        dn->localnode = NULL;
    }

    Node* n = dn;

    // at least one parent node already on the way to SyncDebris?
    while ((n = n->parent) && n->syncdeleted == SYNCDEL_NONE);

    // no: enqueue this one
    if (!n)
    {
        if (unlink)
        {
            dn->tounlink_it = tounlink.insert(dn).first;
        }
        else
        {
            dn->todebris_it = todebris.insert(dn).first;        
        }
    }
}

void MegaClient::execsyncdeletions()
{                
    if (todebris.size())
    {
        execmovetosyncdebris();
    }

    if (tounlink.size())
    {
        execsyncunlink();
    }
}

void MegaClient::proclocaltree(LocalNode* n, LocalTreeProc* tp)
{
    if (n->type != FILENODE)
    {
        for (localnode_map::iterator it = n->children.begin(); it != n->children.end(); )
        {
            LocalNode *child = it->second;
            it++;
            proclocaltree(child, tp);
        }
    }

    tp->proc(this, n);
}

void MegaClient::execsyncunlink()
{
    Node* n;
    Node* tn;
    node_set::iterator it;

    // delete tounlink nodes
    do {
        n = tn = *tounlink.begin();

        while ((n = n->parent) && n->syncdeleted == SYNCDEL_NONE);

        if (!n)
        {
            int creqtag = reqtag;
            reqtag = tn->tag;
            unlink(tn);
            reqtag = creqtag;
        }

        tn->tounlink_it = tounlink.end();
        tounlink.erase(tounlink.begin());
    } while (tounlink.size());
}

// immediately moves pending todebris items to //bin
// also deletes tounlink items directly
void MegaClient::execmovetosyncdebris()
{
    Node* n;
    Node* tn;
    node_set::iterator it;

    time_t ts;
    struct tm* ptm;
    char buf[32];
    syncdel_t target;

    // attempt to move the nodes in node_set todebris to the following
    // locations (in falling order):
    // - //bin/SyncDebris/yyyy-mm-dd
    // - //bin/SyncDebris
    // - //bin

    // (if no rubbish bin is found, we should probably reload...)
    if (!(tn = nodebyhandle(rootnodes[RUBBISHNODE - ROOTNODE])))
    {
        return;
    }

    target = SYNCDEL_BIN;

    ts = time(NULL);
    ptm = localtime(&ts);
    sprintf(buf, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
    m_time_t currentminute = ts / 60;

    // locate //bin/SyncDebris
    if ((n = childnodebyname(tn, SYNCDEBRISFOLDERNAME)) && n->type == FOLDERNODE)
    {
        tn = n;
        target = SYNCDEL_DEBRIS;

        // locate //bin/SyncDebris/yyyy-mm-dd
        if ((n = childnodebyname(tn, buf)) && n->type == FOLDERNODE)
        {
            tn = n;
            target = SYNCDEL_DEBRISDAY;
        }
    }

    // in order to reduce the API load, we move
    // - SYNCDEL_DELETED nodes to any available target
    // - SYNCDEL_BIN/SYNCDEL_DEBRIS nodes to SYNCDEL_DEBRISDAY
    // (move top-level nodes only)
    for (it = todebris.begin(); it != todebris.end(); )
    {
        n = *it;

        if (n->syncdeleted == SYNCDEL_DELETED
         || n->syncdeleted == SYNCDEL_BIN
         || n->syncdeleted == SYNCDEL_DEBRIS)
        {
            while ((n = n->parent) && n->syncdeleted == SYNCDEL_NONE);

            if (!n)
            {
                n = *it;

                if (n->syncdeleted == SYNCDEL_DELETED
                 || ((n->syncdeleted == SYNCDEL_BIN
                   || n->syncdeleted == SYNCDEL_DEBRIS)
                      && target == SYNCDEL_DEBRISDAY))
                {
                    n->syncdeleted = SYNCDEL_INFLIGHT;
                    int creqtag = reqtag;
                    reqtag = n->tag;
                    LOG_debug << "Moving to Syncdebris: " << n->displayname() << " in " << tn->displayname() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
                    rename(n, tn, target, n->parent ? n->parent->nodehandle : UNDEF);
                    reqtag = creqtag;
                    it++;
                }
                else
                {
                    LOG_debug << "SyncDebris daily folder not created. Final target: " << n->syncdeleted;
                    n->syncdeleted = SYNCDEL_NONE;
                    n->todebris_it = todebris.end();
                    todebris.erase(it++);
                }
            }
            else
            {
                it++;
            }
        }
        else if (n->syncdeleted == SYNCDEL_DEBRISDAY
                 || n->syncdeleted == SYNCDEL_FAILED)
        {
            LOG_debug << "Move to SyncDebris finished. Final target: " << n->syncdeleted;
            n->syncdeleted = SYNCDEL_NONE;
            n->todebris_it = todebris.end();
            todebris.erase(it++);
        }
        else
        {
            it++;
        }
    }

    if (target != SYNCDEL_DEBRISDAY && todebris.size() && !syncdebrisadding
            && (target == SYNCDEL_BIN || syncdebrisminute != currentminute))
    {
        syncdebrisadding = true;
        syncdebrisminute = currentminute;
        LOG_debug << "Creating daily SyncDebris folder: " << buf << " Target: " << target;

        // create missing component(s) of the sync debris folder of the day
        NewNode* nn;
        SymmCipher tkey;
        string tattrstring;
        AttrMap tattrs;
        int i = (target == SYNCDEL_DEBRIS) ? 1 : 2;

        nn = new NewNode[i] + i;

        while (i--)
        {
            nn--;

            nn->source = NEW_NODE;
            nn->type = FOLDERNODE;
            nn->nodehandle = i;
            nn->parenthandle = i ? 0 : UNDEF;

            nn->nodekey.resize(FOLDERNODEKEYLENGTH);
            PrnGen::genblock((byte*)nn->nodekey.data(), FOLDERNODEKEYLENGTH);

            // set new name, encrypt and attach attributes
            tattrs.map['n'] = (i || target == SYNCDEL_DEBRIS) ? buf : SYNCDEBRISFOLDERNAME;
            tattrs.getjson(&tattrstring);
            tkey.setkey((const byte*)nn->nodekey.data(), FOLDERNODE);
            nn->attrstring = new string;
            makeattr(&tkey, nn->attrstring, tattrstring.c_str());
        }

        reqs.add(new CommandPutNodes(this, tn->nodehandle, NULL, nn,
                                        (target == SYNCDEL_DEBRIS) ? 1 : 2, -reqtag,
                                        PUTNODES_SYNCDEBRIS));
    }
}

// we cannot delete the Sync object directly, as it might have pending
// operations on it
void MegaClient::delsync(Sync* sync, bool deletecache)
{
    sync->changestate(SYNC_CANCELED);

    if (deletecache && sync->statecachetable)
    {
        sync->statecachetable->remove();
        delete sync->statecachetable;
        sync->statecachetable = NULL;
    }

    syncactivity = true;
}

void MegaClient::putnodes_syncdebris_result(error, NewNode* nn)
{
    delete[] nn;

    syncdebrisadding = false;
}
#endif

// inject file into transfer subsystem
// if file's fingerprint is not valid, it will be obtained from the local file
// (PUT) or the file's key (GET)
bool MegaClient::startxfer(direction_t d, File* f, bool skipdupes)
{
    if (!f->transfer)
    {
        if (d == PUT)
        {
            if (!f->isvalid)    // (sync LocalNodes always have this set)
            {
                // missing FileFingerprint for local file - generate
                FileAccess* fa = fsaccess->newfileaccess();

                if (fa->fopen(&f->localname, d == PUT, d == GET))
                {
                    f->genfingerprint(fa);
                }

                delete fa;
            }

            // if we are unable to obtain a valid file FileFingerprint, don't proceed
            if (!f->isvalid)
            {
                LOG_err << "Unable to get a fingerprint " << f->name;
                return false;
            }

            #ifdef USE_MEDIAINFO
            mediaFileInfo.requestCodecMappingsOneTime(this, &f->localname);  
            #endif
        }
        else
        {
            if (!f->isvalid)
            {
                // no valid fingerprint: use filekey as its replacement
                memcpy(f->crc, f->filekey, sizeof f->crc);
            }
        }

        Transfer* t = NULL;
        transfer_map::iterator it = transfers[d].find(f);

        if (it != transfers[d].end())
        {
            t = it->second;
            if (skipdupes)
            {
                for (file_list::iterator fi = t->files.begin(); fi != t->files.end(); fi++)
                {
                    if ((d == GET && f->localname == (*fi)->localname)
                            || (d == PUT && f->h != UNDEF
                                && f->h == (*fi)->h
                                && !f->targetuser.size()
                                && !(*fi)->targetuser.size()
                                && f->name == (*fi)->name))
                    {
                        LOG_warn << "Skipping duplicated transfer";
                        return false;
                    }
                }
            }
            f->file_it = t->files.insert(t->files.end(), f);
            f->transfer = t;
            f->tag = reqtag;
            if (!f->dbid)
            {
                filecacheadd(f);
            }
            app->file_added(f);

            if (overquotauntil && overquotauntil > Waiter::ds)
            {
                dstime timeleft = overquotauntil - Waiter::ds;
                t->failed(API_EOVERQUOTA, timeleft);
            }
        }
        else
        {
            it = cachedtransfers[d].find(f);
            if (it != cachedtransfers[d].end())
            {
                LOG_debug << "Resumable transfer detected";
                t = it->second;
                if ((d == GET && !t->pos) || ((time(NULL) - t->lastaccesstime) >= 172500))
                {
                    LOG_warn << "Discarding temporary URL (" << t->pos << ", " << t->lastaccesstime << ")";
                    t->cachedtempurl.clear();

                    if (d == PUT)
                    {
                        t->chunkmacs.clear();
                        t->progresscompleted = 0;
                        delete [] t->ultoken;
                        t->ultoken = NULL;
                        t->pos = 0;
                    }
                }

                FileAccess* fa = fsaccess->newfileaccess();
                if (!fa->fopen(&t->localfilename))
                {
                    if (d == PUT)
                    {
                        LOG_warn << "Local file not found";
                        // the transfer will be retried to ensure that the file
                        // is not just just temporarily blocked
                    }
                    else
                    {
                        LOG_warn << "Temporary file not found";
                        t->localfilename.clear();
                        t->chunkmacs.clear();
                        t->progresscompleted = 0;
                        t->pos = 0;
                    }
                }
                else
                {
                    if (d == PUT)
                    {
                        if (f->genfingerprint(fa))
                        {
                            LOG_warn << "The local file has been modified";
                            t->cachedtempurl.clear();
                            t->chunkmacs.clear();
                            t->progresscompleted = 0;
                            delete [] t->ultoken;
                            t->ultoken = NULL;
                            t->pos = 0;
                        }
                    }
                    else
                    {
                        if (t->progresscompleted > fa->size)
                        {
                            LOG_warn << "Truncated temporary file";
                            t->chunkmacs.clear();
                            t->progresscompleted = 0;
                            t->pos = 0;
                        }
                    }
                }
                delete fa;
                cachedtransfers[d].erase(it);
                LOG_debug << "Transfer resumed";
            }

            if (!t)
            {
                t = new Transfer(this, d);
                *(FileFingerprint*)t = *(FileFingerprint*)f;
            }

            t->lastaccesstime = time(NULL);
            t->tag = reqtag;
            f->tag = reqtag;
            t->transfers_it = transfers[d].insert(pair<FileFingerprint*, Transfer*>((FileFingerprint*)t, t)).first;

            f->file_it = t->files.insert(t->files.end(), f);
            f->transfer = t;
            if (!f->dbid)
            {
                filecacheadd(f);
            }

            transferlist.addtransfer(t);
            app->transfer_added(t);
            app->file_added(f);
            looprequested = true;

            if (overquotauntil && overquotauntil > Waiter::ds)
            {
                dstime timeleft = overquotauntil - Waiter::ds;
                t->failed(API_EOVERQUOTA, timeleft);
            }
        }
    }

    return true;
}

// remove file from transfer subsystem
void MegaClient::stopxfer(File* f)
{
    if (f->transfer)
    {
        LOG_debug << "Stopping transfer: " << f->name;

        Transfer *transfer = f->transfer;
        transfer->files.erase(f->file_it);
        filecachedel(f);
        app->file_removed(f, API_EINCOMPLETE);
        f->transfer = NULL;
        f->terminated();

        // last file for this transfer removed? shut down transfer.
        if (!transfer->files.size())
        {
            looprequested = true;
            transfer->finished = true;
            transfer->state = TRANSFERSTATE_CANCELLED;
            app->transfer_removed(transfer);
            delete transfer;
        }
        else
        {
            if (transfer->type == PUT && transfer->localfilename.size())
            {
                LOG_debug << "Updating transfer path";
                transfer->files.front()->prepare();
            }
        }
    }
}

// pause/unpause transfers
void MegaClient::pausexfers(direction_t d, bool pause, bool hard)
{
    xferpaused[d] = pause;

    if (!pause || hard)
    {
        WAIT_CLASS::bumpds();

        for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); )
        {
            if ((*it)->transfer->type == d)
            {
                if (pause)
                {
                    if (hard)
                    {
                        (*it++)->disconnect();
                    }
                }
                else
                {
                    (*it)->lastdata = Waiter::ds;
                    (*it++)->doio(this);
                }
            }
            else
            {
                it++;
            }
        }
    }
}

void MegaClient::setmaxconnections(direction_t d, int num)
{
    if (num > 0)
    {
         if ((unsigned int) num > MegaClient::MAX_NUM_CONNECTIONS)
        {
            num = MegaClient::MAX_NUM_CONNECTIONS;
        }

        if (connections[d] != num)
        {
            connections[d] = num;
            for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); )
            {
                TransferSlot *slot = *it++;
                if (slot->transfer->type == d)
                {
                    slot->transfer->state = TRANSFERSTATE_QUEUED;
                    slot->transfer->bt.arm();
                    slot->transfer->cachedtempurl = slot->tempurl;
                    delete slot;
                }
            }
        }
    }
}

Node* MegaClient::nodebyfingerprint(FileFingerprint* fingerprint)
{
    fingerprint_set::iterator it;

    if ((it = fingerprints.find(fingerprint)) != fingerprints.end())
    {
        return (Node*)*it;
    }

    return NULL;
}

node_vector *MegaClient::nodesbyfingerprint(FileFingerprint* fingerprint)
{
    node_vector *nodes = new node_vector();
    pair<fingerprint_set::iterator, fingerprint_set::iterator> p = fingerprints.equal_range(fingerprint);
    for (fingerprint_set::iterator it = p.first; it != p.second; it++)
    {
        nodes->push_back((Node*)*it);
    }
    return nodes;
}


// a chunk transfer request failed: record failed protocol & host
void MegaClient::setchunkfailed(string* url)
{
    if (!chunkfailed && url->size() > 19)
    {
        LOG_debug << "Adding badhost report for URL " << *url;
        chunkfailed = true;
        httpio->success = false;

        // record protocol and hostname
        if (badhosts.size())
        {
            badhosts.append(",");
        }

        const char* ptr = url->c_str()+4;

        if (*ptr == 's')
        {
            badhosts.append("S");
            ptr++;
        }
        
        badhosts.append(ptr+6,7);
        btbadhost.reset();
    }
}

bool MegaClient::toggledebug()
{
     SimpleLogger::setLogLevel((SimpleLogger::logCurrentLevel >= logDebug) ? logWarning : logDebug);
     return debugstate();
}

bool MegaClient::debugstate()
{
    return SimpleLogger::logCurrentLevel >= logDebug;
}

void MegaClient::reportevent(const char* event, const char* details)
{
    LOG_err << "SERVER REPORT: " << event << " DETAILS: " << details;
    reqs.add(new CommandReportEvent(this, event, details));
}

bool MegaClient::setmaxdownloadspeed(m_off_t bpslimit)
{
    return httpio->setmaxdownloadspeed(bpslimit >= 0 ? bpslimit : 0);
}

bool MegaClient::setmaxuploadspeed(m_off_t bpslimit)
{
    return httpio->setmaxuploadspeed(bpslimit >= 0 ? bpslimit : 0);
}

m_off_t MegaClient::getmaxdownloadspeed()
{
    return httpio->getmaxdownloadspeed();
}

m_off_t MegaClient::getmaxuploadspeed()
{
    return httpio->getmaxuploadspeed();
}

handle MegaClient::getovhandle(Node *parent, string *name)
{
    handle ovhandle = UNDEF;
    if (parent && name)
    {
        Node *ovn = childnodebyname(parent, name->c_str(), true);
        if (ovn)
        {
            ovhandle = ovn->nodehandle;
        }
    }
    return ovhandle;
}

void MegaClient::userfeedbackstore(const char *message)
{
    string type = "feedback.";
    type.append(&(appkey[4]));
    type.append(".");

    string base64userAgent;
    base64userAgent.resize(useragent.size() * 4 / 3 + 4);
    Base64::btoa((byte *)useragent.data(), useragent.size(), (char *)base64userAgent.data());
    type.append(base64userAgent);

    reqs.add(new CommandUserFeedbackStore(this, type.c_str(), message, NULL));
}

void MegaClient::sendevent(int event, const char *desc)
{
    LOG_warn << "Event " << event << ": " << desc;
    reqs.add(new CommandSendEvent(this, event, desc));
}

void MegaClient::cleanrubbishbin()
{
    reqs.add(new CommandCleanRubbishBin(this));
}

#ifdef ENABLE_CHAT
void MegaClient::createChat(bool group, const userpriv_vector *userpriv)
{
    reqs.add(new CommandChatCreate(this, group, userpriv));
}

void MegaClient::inviteToChat(handle chatid, handle uh, int priv, const char *title)
{
    reqs.add(new CommandChatInvite(this, chatid, uh, (privilege_t) priv, title));
}

void MegaClient::removeFromChat(handle chatid, handle uh)
{
    reqs.add(new CommandChatRemove(this, chatid, uh));
}

void MegaClient::getUrlChat(handle chatid)
{
    reqs.add(new CommandChatURL(this, chatid));
}

userpriv_vector *MegaClient::readuserpriv(JSON *j)
{
    userpriv_vector *userpriv = NULL;

    if (j->enterarray())
    {
        while(j->enterobject())
        {
            handle uh = UNDEF;
            privilege_t priv = PRIV_UNKNOWN;

            bool readingUsers = true;
            while(readingUsers)
            {
                switch (j->getnameid())
                {
                    case 'u':
                        uh = j->gethandle(MegaClient::USERHANDLE);
                        break;

                    case 'p':
                        priv = (privilege_t) j->getint();
                        break;

                    case EOO:
                        if(uh == UNDEF || priv == PRIV_UNKNOWN)
                        {
                            delete userpriv;
                            return NULL;
                        }

                        if (!userpriv)
                        {
                            userpriv = new userpriv_vector;
                        }

                        userpriv->push_back(userpriv_pair(uh, priv));
                        readingUsers = false;
                        break;

                    default:
                        if (!j->storeobject())
                        {
                            delete userpriv;
                            return NULL;
                        }
                        break;
                    }
            }
            j->leaveobject();
        }
        j->leavearray();
    }

    return userpriv;
}

void MegaClient::grantAccessInChat(handle chatid, handle h, const char *uid)
{
    reqs.add(new CommandChatGrantAccess(this, chatid, h, uid));
}

void MegaClient::removeAccessInChat(handle chatid, handle h, const char *uid)
{
    reqs.add(new CommandChatRemoveAccess(this, chatid, h, uid));
}

void MegaClient::updateChatPermissions(handle chatid, handle uh, int priv)
{
    reqs.add(new CommandChatUpdatePermissions(this, chatid, uh, (privilege_t) priv));
}

void MegaClient::truncateChat(handle chatid, handle messageid)
{
    reqs.add(new CommandChatTruncate(this, chatid, messageid));
}

void MegaClient::setChatTitle(handle chatid, const char *title)
{
    reqs.add(new CommandChatSetTitle(this, chatid, title));
}

void MegaClient::getChatPresenceUrl()
{
    reqs.add(new CommandChatPresenceURL(this));
}

void MegaClient::registerPushNotification(int deviceType, const char *token)
{
    reqs.add(new CommandRegisterPushNotification(this, deviceType, token));
}

#endif

void MegaClient::getaccountachievements(AchievementsDetails *details)
{
    reqs.add(new CommandGetMegaAchievements(this, details));
}

void MegaClient::getmegaachievements(AchievementsDetails *details)
{
    reqs.add(new CommandGetMegaAchievements(this, details, false));
}

void MegaClient::getwelcomepdf()
{
    reqs.add(new CommandGetWelcomePDF(this));
}

FetchNodesStats::FetchNodesStats()
{
    init();
}

void FetchNodesStats::init()
{
    mode = MODE_NONE;
    type = TYPE_NONE;
    cache = API_NONE;
    nodesCached = 0;
    nodesCurrent = 0;
    actionPackets = 0;

    eAgainCount = 0;
    e500Count = 0;
    eOthersCount = 0;

    startTime = Waiter::ds;
    timeToFirstByte = NEVER;
    timeToLastByte = NEVER;
    timeToCached = NEVER;
    timeToResult = NEVER;
    timeToSyncsResumed = NEVER;
    timeToCurrent = NEVER;
    timeToTransfersResumed = NEVER;
}

void FetchNodesStats::toJsonArray(string *json)
{
    if (!json)
    {
        return;
    }

    ostringstream oss;
    oss << "[" << mode << "," << type << ","
        << nodesCached << "," << nodesCurrent << "," << actionPackets << ","
        << eAgainCount << "," << e500Count << "," << eOthersCount << ","
        << timeToFirstByte << "," << timeToLastByte << ","
        << timeToCached << "," << timeToResult << ","
        << timeToSyncsResumed << "," << timeToCurrent << ","
        << timeToTransfersResumed << "," << cache << "]";
    json->append(oss.str());
}

} // namespace
