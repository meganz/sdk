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

#ifdef USE_SODIUM
#include <sodium.h>
#endif

#include <chrono>
#include <future>

namespace mega {

// FIXME: generate cr element for file imports
// FIXME: support invite links (including responding to sharekey requests)
// FIXME: instead of copying nodes, move if the source is in the rubbish to reduce node creation load on the servers
// FIXME: prevent synced folder from being moved into another synced folder

bool MegaClient::disablepkp = false;

// root URL for API access
string MegaClient::APIURL = "https://g.api.mega.co.nz/";

// root URL for the load balancer
const char* const MegaClient::BALANCERURL =
        "https://karere-001.developers.mega.co.nz:4434/";

#ifdef ENABLE_SYNC
// //bin/SyncDebris/yyyy-mm-dd base folder name
const char* const MegaClient::SYNCDEBRISFOLDERNAME = "SyncDebris";
#endif

// exported link marker
const char* const MegaClient::EXPORTEDLINK = "EXP";

// decrypt key (symmetric or asymmetric), rewrite asymmetric to symmetric key
bool MegaClient::decryptkey(const char* sk, byte* tk, int tl, SymmCipher* sc,
        int type, handle node) {
    int sl;
    const char* ptr = sk;

    // measure key length
    while (*ptr && *ptr != '"' && *ptr != '/') {
        ptr++;
    }

    sl = ptr - sk;

    if (sl > 4 * FILENODEKEYLENGTH / 3 + 1) {
        // RSA-encrypted key - decrypt and update on the server to save space & client CPU time
        sl = sl / 4 * 3 + 3;

        if (sl > 4096) {
            return false;
        }

        byte* buf = new byte[sl];

        sl = Base64::atob(sk, buf, sl);

        // decrypt and set session ID for subsequent API communication
        if (!asymkey.decrypt(buf, sl, tk, tl)) {
            delete[] buf;
            LOG_warn << "Corrupt or invalid RSA node key";
            return false;
        }

        delete[] buf;

        if (!ISUNDEF(node)) {
            if (type) {
                sharekeyrewrite.push_back(node);
            } else {
                nodekeyrewrite.push_back(node);
            }
        }
    } else {
        if (Base64::atob(sk, tk, tl) != tl) {
            LOG_warn << "Corrupt or invalid symmetric node key";
            return false;
        }

        sc->ecb_decrypt(tk, tl);
    }

    return true;
}

// apply queued new shares
void MegaClient::mergenewshares(bool notify) {
    newshare_list::iterator it;

    for (it = newshares.begin(); it != newshares.end();) {
        Node* n;
        NewShare* s = *it;

        if ((n = nodebyhandle(s->h))) {
            if (!n->sharekey && s->have_key) {
                // setting an outbound sharekey requires node authentication
                // unless coming from a trusted source (the local cache)
                bool auth = true;

                if (s->outgoing > 0) {
                    if (!checkaccess(n, OWNERPRELOGIN)) {
                        LOG_warn
                                << "Attempt to create dislocated outbound share foiled";
                        auth = false;
                    } else {
                        byte buf[SymmCipher::KEYLENGTH];

                        handleauth(s->h, buf);

                        if (memcmp(buf, s->auth, sizeof buf)) {
                            LOG_warn
                                    << "Attempt to create forged outbound share foiled";
                            auth = false;
                        }
                    }
                }

                if (auth) {
                    n->sharekey = new SymmCipher(s->key);
                }
            }

            if (s->access == ACCESS_UNKNOWN && !s->have_key) {
                // share was deleted
                if (s->outgoing) {
                    if (n->outshares) {
                        // outgoing share to user u deleted
                        if (n->outshares->erase(s->peer) && notify) {
                            n->changed.outshares = true;
                            notifynode(n);
                        }

                        // if no other outgoing shares remain on this node, erase sharekey
                        if (!n->outshares->size()) {
                            delete n->outshares;
                            n->outshares = NULL;

                            delete n->sharekey;
                            n->sharekey = NULL;
                        }
                    }
                } else {
                    // incoming share deleted - remove tree
                    if (!n->parent) {
                        TreeProcDel td;
                        proctree(n, &td, true);
                    } else {
                        if (n->inshare) {
                            n->inshare->user->sharing.erase(n->nodehandle);
                            notifyuser(n->inshare->user);
                            n->inshare = NULL;
                        }
                    }
                }
            } else {
                if (!ISUNDEF(s->peer)) {
                    if (s->outgoing) {
                        // perform mandatory verification of outgoing shares:
                        // only on own nodes and signed unless read from cache
                        if (checkaccess(n, OWNERPRELOGIN)) {
                            if (!n->outshares) {
                                n->outshares = new share_map;
                            }

                            Share** sharep = &((*n->outshares)[s->peer]);

                            // modification of existing share or new share
                            if (*sharep) {
                                (*sharep)->update(s->access, s->ts);
                            } else {
                                *sharep = new Share(finduser(s->peer, 1),
                                        s->access, s->ts);
                            }

                            if (notify) {
                                n->changed.outshares = true;
                                notifynode(n);
                            }
                        }
                    } else {
                        if (s->peer) {
                            if (!checkaccess(n, OWNERPRELOGIN)) {
                                // modification of existing share or new share
                                if (n->inshare) {
                                    n->inshare->update(s->access, s->ts);
                                } else {
                                    n->inshare = new Share(finduser(s->peer, 1),
                                            s->access, s->ts);
                                    n->inshare->user->sharing.insert(
                                            n->nodehandle);
                                }

                                if (notify) {
                                    n->changed.inshare = true;
                                    notifynode(n);
                                }
                            } else {
                                LOG_warn << "Invalid inbound share location";
                            }
                        } else {
                            LOG_warn << "Invalid null peer on inbound share";
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
                }while ((n = n->parent));

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

        delete s;
        newshares.erase(it++);
    }
}

// configure for full account session access
void MegaClient::setsid(const byte* newsid, unsigned len) {
    auth = "&sid=";

    int t = auth.size();
    auth.resize(t + len * 4 / 3 + 4);
    auth.resize(t + Base64::btoa(newsid, len, (char*) (auth.c_str() + t)));

    sid.assign((const char*) newsid, len);
}

// configure for exported folder links access
void MegaClient::setrootnode(handle h) {
    char buf[12];

    Base64::btoa((byte*) &h, NODEHANDLE, buf);

    auth = "&n=";
    auth.append(buf);
}

// set server-client sequence number
bool MegaClient::setscsn(JSON* j) {
    handle t;

    if (j->storebinary((byte*) &t, sizeof t) != sizeof t) {
        return false;
    }

    Base64::btoa((byte*) &t, sizeof t, scsn);

    return true;
}

int MegaClient::nextreqtag() {
    return ++reqtag;
}

int MegaClient::hexval(char c) {
    return c > '9' ? c - 'a' + 10 : c - '0';
}

// set warn level
void MegaClient::warn(const char* msg) {
    LOG_warn << msg;
    warned = true;
}

// reset and return warnlevel
bool MegaClient::warnlevel() {
    return warned ? (warned = false) | true : false;
}

// returns the first matching child node by UTF-8 name (does not resolve name clashes)
Node* MegaClient::childnodebyname(Node* p, const char* name) {
    string nname = name;

    fsaccess->normalize(&nname);

    for (node_list::iterator it = p->children.begin(); it != p->children.end();
            it++) {
        if (!strcmp(nname.c_str(), (*it)->displayname())) {
            return *it;
        }
    }

    return NULL;
}

void MegaClient::init() {
    keysFetched = false;
    warned = false;
    csretrying = false;
    fetchingnodes = false;
    chunkfailed = false;
    noinetds = 0;

#ifdef ENABLE_SYNC
    syncactivity = false;
    syncadded = false;
    syncdebrisadding = false;
    syncscanfailed = false;
    syncfslockretry = false;
    syncfsopsfailed = false;
    syncdownretry = false;
    syncnagleretry = false;
    syncsup = true;
    syncdownrequired = false;

    if (syncscanstate)
    {
        app->syncupdate_scanning(false);
        syncscanstate = false;
    }
#endif

    for (int i = sizeof rootnodes / sizeof *rootnodes; i--;) {
        rootnodes[i] = UNDEF;
    }

    delete pendingsc;
    pendingsc = NULL;

    curfa = newfa.end();

    btcs.reset();
    btsc.reset();
    btpfa.reset();

    xferpaused[PUT] = false;
    xferpaused[GET] = false;

    putmbpscap = 0;

    jsonsc.pos = NULL;
    insca = false;
    scnotifyurl.clear();
    *scsn = 0;
}

MegaClient::MegaClient(MegaApp* a, Waiter* w, HttpIO* h, FileSystemAccess* f,
        DbAccess* d, GfxProc* g, const char* k, const char* u) {
    sctable = NULL;
    me = UNDEF;
    followsymlinks = false;

#ifdef ENABLE_SYNC
    syncscanstate = false;
    syncdownrequired = false;
    syncadding = 0;
    currsyncid = 0;
#endif

    pendingcs = NULL;
    pendingsc = NULL;

    init();

    f->client = this;

    if ((app = a)) {
        a->client = this;
    }

    waiter = w;
    httpio = h;
    fsaccess = f;
    dbaccess = d;

    if ((gfx = g)) {
        g->client = this;
    }

    slotit = tslots.end();

    userid = 0;

    connections[PUT] = 3;
    connections[GET] = 4;

    int i;

    // initialize random client application instance ID (for detecting own
    // actions in server-client stream)
    for (i = sizeof sessionid; i--;) {
        sessionid[i] = 'a' + PrnGen::genuint32(26);
    }

    // initialize random API request sequence ID (server API is idempotent)
    for (i = sizeof reqid; i--;) {
        reqid[i] = 'a' + PrnGen::genuint32(26);
    }

    r = 0;
    nextuh = 0;
    reqtag = 0;

    badhostcs = NULL;
    loadbalancingcs = NULL;

    scsn[sizeof scsn - 1] = 0;

    snprintf(appkey, sizeof appkey, "&ak=%s", k);

    // initialize useragent
    useragent = u;

    useragent.append(" (");
    fsaccess->osversion(&useragent);

    useragent.append(") MegaClient/" TOSTRING(MEGA_MAJOR_VERSION)
    "." TOSTRING(MEGA_MINOR_VERSION)
    "." TOSTRING(MEGA_MICRO_VERSION));

    h->setuseragent(&useragent);
}

MegaClient::~MegaClient() {
    locallogout();

    delete pendingcs;
    delete pendingsc;
    delete badhostcs;
    delete loadbalancingcs;
    delete sctable;
    delete dbaccess;
}

// nonblocking state machine executing all operations currently in progress
void MegaClient::exec() {
    WAIT_CLASS::bumpds();

    if (httpio->inetisback()) {
        LOG_info
                << "Internet connectivity returned - resetting all backoff timers";
        abortbackoff();
    }

    if (EVER(httpio->lastdata)
            && Waiter::ds >= httpio->lastdata + HttpIO::NETWORKTIMEOUT) {
        disconnect();
    }

    // successful network operation with a failed transfer chunk: increment error count
    // and continue transfers
    if (httpio->success && chunkfailed) {
        chunkfailed = false;

        for (transferslot_list::iterator it = tslots.begin();
                it != tslots.end(); it++) {
            if ((*it)->failure) {
                (*it)->errorcount++;
                (*it)->failure = false;
            }
        }
    }

    do {
        // file attribute puts (handled sequentially as a FIFO)
        if (curfa != newfa.end()) {
            HttpReqCommandPutFA* fa = *curfa;

            switch (fa->status) {
            case REQ_SUCCESS:
                if (fa->in.size() == sizeof(handle)) {
                    // successfully wrote file attribute - store handle &
                    // remove from list
                    handle fah = MemAccess::get<handle>(fa->in.data());

                    Node* n;
                    handle h;
                    handlepair_set::iterator it;

                    // do we have a valid upload handle?
                    h = fa->th;

                    it = uhnh.lower_bound(pair<handle, handle>(h, 0));

                    if (it != uhnh.end() && it->first == h) {
                        h = it->second;
                    }

                    // are we updating a live node? issue command directly.
                    // otherwise, queue for processing upon upload
                    // completion.
                    if ((n = nodebyhandle(h)) || (n = nodebyhandle(fa->th))) {
                        reqs[r].add(
                                new CommandAttachFA(n->nodehandle, fa->type,
                                        fah, fa->tag));
                    } else {
                        pendingfa[pair<handle, fatype>(fa->th, fa->type)] =
                                pair<handle, int>(fah, fa->tag);
                        checkfacompletion(fa->th);
                    }

                    delete fa;
                    newfa.erase(curfa);
                }

                btpfa.reset();
                curfa = newfa.end();
                break;

            case REQ_FAILURE:
                // repeat request with exponential backoff
                curfa = newfa.end();
                btpfa.backoff();

            default:
                ;
            }
        }

        if (newfa.size() && curfa == newfa.end() && btpfa.armed()) {
            // dispatch most recent file attribute put
            curfa = newfa.begin();

            (*curfa)->status = REQ_INFLIGHT;
            reqs[r].add(*curfa);
        }

        if (fafcs.size()) {
            // file attribute fetching (handled in parallel on a per-cluster basis)
            // cluster channels are never purged
            fafc_map::iterator cit;
            faf_map::iterator it;
            FileAttributeFetchChannel* fc;

            for (cit = fafcs.begin(); cit != fafcs.end(); cit++) {
                fc = cit->second;

                // is this request currently in flight?
                switch (fc->req.status) {
                case REQ_SUCCESS:
                    fc->parse(this, cit->first, true);

                    // notify app in case some attributes were not returned, then redispatch
                    fc->failed(this);
                    fc->bt.reset();
                    break;

                case REQ_INFLIGHT:
                    if (fc->inbytes != fc->req.in.size()) {
                        httpio->lock();
                        fc->parse(this, cit->first, false);
                        httpio->unlock();

                        fc->timeout.backoff(100);

                        fc->inbytes = fc->req.in.size();
                    }

                    if (!fc->timeout.armed())
                        break;

                    // timeout! fall through...
                    fc->req.disconnect();
                case REQ_FAILURE:
                    fc->failed(this);
                    fc->bt.backoff();
                    fc->urltime = 0;
                default:
                    ;
                }

                if (fc->req.status != REQ_INFLIGHT && fc->bt.armed()
                        && (fc->fafs[1].size() || fc->fafs[0].size())) {
                    fc->req.in.clear();

                    if (Waiter::ds - fc->urltime > 600) {
                        // fetches pending for this unconnected channel - dispatch fresh connection
                        reqs[r].add(
                                new CommandGetFA(cit->first, fc->fahref,
                                        httpio->chunkedok));
                        fc->req.status = REQ_INFLIGHT;
                    } else {
                        // redispatch cached URL if not older than one minute
                        fc->dispatch(this);
                    }
                }
            }
        }

        // handle API client-server requests
        for (;;) {
            // do we have an API request outstanding?
            if (pendingcs) {
                switch (pendingcs->status) {
                case REQ_READY:
                    break;

                case REQ_INFLIGHT:
                    if (pendingcs->contentlength > 0) {
                        app->request_response_progress(pendingcs->bufpos,
                                pendingcs->contentlength);
                    }
                    break;

                case REQ_SUCCESS:
                    app->request_response_progress(pendingcs->bufpos, -1);

                    if (pendingcs->in != "-3" && pendingcs->in != "-4") {
                        if (*pendingcs->in.c_str() == '[') {
                            if (csretrying) {
                                app->notify_retry(0);
                                csretrying = false;
                            }

                            // request succeeded, process result array
                            json.begin(pendingcs->in.c_str());
                            reqs[r ^ 1].procresult(this);

                            delete pendingcs;
                            pendingcs = NULL;

                            // increment unique request ID
                            for (int i = sizeof reqid; i--;) {
                                if (reqid[i]++ < 'z') {
                                    break;
                                } else {
                                    reqid[i] = 'a';
                                }
                            }
                        } else {
                            // request failed
                            error e = (error) atoi(pendingcs->in.c_str());

                            if (!e) {
                                e = API_EINTERNAL;
                            }

                            app->request_error(e);
                            break;
                        }

                        btcs.reset();
                        break;
                    }

                    // fall through
                case REQ_FAILURE: // failure, repeat with capped exponential backoff
                    app->request_response_progress(pendingcs->bufpos, -1);

                    delete pendingcs;
                    pendingcs = NULL;

                    btcs.backoff();
                    app->notify_retry(btcs.retryin());
                    csretrying = true;

                default:
                    ;
                }

                if (pendingcs) {
                    break;
                }
            }

            if (btcs.armed()) {
                if (btcs.nextset()) {
                    r ^= 1;
                }

                if (reqs[r].cmdspending()) {
                    pendingcs = new HttpReq();

                    reqs[r].get(pendingcs->out);

                    pendingcs->posturl = APIURL;

                    pendingcs->posturl.append("cs?id=");
                    pendingcs->posturl.append(reqid, sizeof reqid);
                    pendingcs->posturl.append(auth);
                    pendingcs->posturl.append(appkey);

                    pendingcs->type = REQ_JSON;

                    pendingcs->post(this);

                    r ^= 1;
                    continue;
                } else {
                    btcs.reset();
                }
            }
            break;
        }

        // handle API server-client requests
        if (!jsonsc.pos && pendingsc) {
            if (scnotifyurl.size()) {
                // pendingsc is a scnotifyurl connection
                if (pendingsc->status == REQ_SUCCESS
                        || pendingsc->status == REQ_FAILURE) {
                    delete pendingsc;
                    pendingsc = NULL;

                    scnotifyurl.clear();
                }
            } else {
                // pendingsc is a server-client API request
                switch (pendingsc->status) {
                case REQ_SUCCESS:
                    if (*pendingsc->in.c_str() == '{') {
#ifdef ENABLE_SYNC
                        if (syncsup)
#endif
                        {
                            jsonsc.begin(pendingsc->in.c_str());
                            jsonsc.enterobject();
                        }

                        break;
                    } else {
                        error e = (error) atoi(pendingsc->in.c_str());

                        if (e == API_ESID) {
                            app->request_error(API_ESID);
                            *scsn = 0;
                        } else if (e == API_ETOOMANY) {
                            LOG_warn
                                    << "Too many pending updates - reloading local state";
                            fetchnodes();
                        }
                    }
                    // fall through
                case REQ_FAILURE:
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
        syncactivity = false;

        // do not process the SC result until all preconfigured syncs are up and running
        if (syncsup && jsonsc.pos && !syncdownrequired)
#else
        if (jsonsc.pos)
#endif
        {
            // FIXME: reload in case of bad JSON
            if (procsc()) {
                // completed - initiate next SC request
                delete pendingsc;
                pendingsc = NULL;

                btsc.reset();
            }
#ifdef ENABLE_SYNC
            else
            {
                // a remote node move requires the immediate attention of syncdown()
                syncdownrequired = true;
                syncactivity = true;
            }
#endif
        }

        if (!pendingsc && *scsn && btsc.armed()) {
            pendingsc = new HttpReq();

            if (scnotifyurl.size()) {
                pendingsc->posturl = scnotifyurl;
            } else {
                pendingsc->posturl = APIURL;
                pendingsc->posturl.append("sc?sn=");
                pendingsc->posturl.append(scsn);
                pendingsc->posturl.append(auth);
            }

            pendingsc->type = REQ_JSON;
            pendingsc->post(this);

            jsonsc.pos = NULL;
        }

        if (badhostcs) {
            if (badhostcs->status == REQ_FAILURE
                    || badhostcs->status == REQ_SUCCESS) {
                delete badhostcs;
                badhostcs = NULL;
            }
        }

        if (loadbalancingcs) {
            if (loadbalancingcs->status == REQ_FAILURE) {
                CommandLoadBalancing *command = loadbalancingreqs.front();

                restag = command->tag;
                app->loadbalancing_result(NULL, API_EFAILED);

                delete loadbalancingcs;
                loadbalancingcs = NULL;

                delete command;
                loadbalancingreqs.pop();
            } else if (loadbalancingcs->status == REQ_SUCCESS) {
                CommandLoadBalancing *command = loadbalancingreqs.front();

                restag = command->tag;
                app->loadbalancing_result(&loadbalancingcs->in, API_OK);
                //json.begin(loadbalancingcs->in.c_str());
                //command->procresult();

                delete loadbalancingcs;
                loadbalancingcs = NULL;

                delete command;
                loadbalancingreqs.pop();
            }
        }

        if (!loadbalancingcs && loadbalancingreqs.size()) {
            CommandLoadBalancing *command = loadbalancingreqs.front();
            loadbalancingcs = new HttpReq();
            loadbalancingcs->posturl = BALANCERURL;
            loadbalancingcs->posturl.append("?service=");
            loadbalancingcs->posturl.append(command->service);
            loadbalancingcs->type = REQ_JSON;
            loadbalancingcs->post(this);
        }

        // fill transfer slots from the queue
        dispatchmore(PUT);
        dispatchmore(GET);

        slotit = tslots.begin();

        // handle active unpaused transfers
        while (slotit != tslots.end()) {
            transferslot_list::iterator it = slotit;

            slotit++;

            if (!xferpaused[(*it)->transfer->type]
                    && (!(*it)->retrying || (*it)->retrybt.armed())) {
                (*it)->doio(this);
            }
        }

#ifdef ENABLE_SYNC
        // syncops indicates that a sync-relevant tree update may be pending
        bool syncops = syncadded;
        sync_list::iterator it;

        if (syncadded)
        {
            syncadded = false;
        }

        // verify filesystem fingerprints, disable deviating syncs
        // (this covers mountovers, some device removals and some failures)
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
            }
        }

        // process active syncs
        // sync timer: full rescan in case of filesystem notification failures
        if (syncscanfailed && syncscanbt.armed())
        {
            syncscanfailed = false;
            syncops = true;
        }

        // sync timer: retry syncdown() ops in case of local filesystem lock clashes
        if (syncdownretry && syncdownbt.armed())
        {
            syncdownretry = false;
            syncops = true;
        }

        // sync timer: file change upload delay timeouts (Nagle algorithm)
        if (syncnagleretry && syncnaglebt.armed())
        {
            syncnagleretry = false;
            syncops = true;
        }

        // sync timer: read lock retry
        if (syncfslockretry && syncfslockretrybt.armed())
        {
            syncfslockretrybt.backoff(1);
        }

        // halt all syncing while the local filesystem is pending a lock-blocked operation
        // FIXME: indicate by callback
        if (!syncdownretry && !syncadding)
        {
            // process active syncs, stop doing so while transient local fs ops are pending
            if (syncs.size() || syncactivity)
            {
                unsigned totalpending = 0;
                dstime nds = NEVER;

                for (int q = syncfslockretry ? DirNotify::RETRY : DirNotify::DIREVENTS; q >= DirNotify::DIREVENTS; q--)
                {
                    syncfslockretry = false;

                    if (!syncfsopsfailed)
                    {
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
                                            syncnaglebt.backoff(dsretry + 1);
                                            syncnagleretry = true;
                                        }
                                        else
                                        {
                                            syncactivity = true;
                                        }

                                        if(syncadding)
                                        {
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        LOG_debug << "Pending MEGA nodes: " << synccreate.size();
                                        if(!syncadding)
                                        {
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

                                if (!syncfslockretry && sync->dirnotify->notifyq[DirNotify::RETRY].size())
                                {
                                    syncfslockretrybt.backoff(1);
                                    syncfslockretry = true;
                                }

                                if (q == DirNotify::DIREVENTS)
                                {
                                    totalpending += sync->dirnotify->notifyq[DirNotify::DIREVENTS].size();
                                }
                            }

                            if (sync->state == SYNC_ACTIVE)
                            {
                                sync->cachenodes();
                            }
                        }

                        if(syncadding)
                        {
                            break;
                        }
                    }
                }

                if(syncadding)
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

                // set retry interval for locked filesystem items once all
                // pending items were processed
                if (syncfslockretry)
                {
                    syncfslockretrybt.backoff(2);
                }

                // notify the app of the length of the pending scan queue
                if (totalpending < 4)
                {
                    if (syncscanstate)
                    {
                        app->syncupdate_scanning(false);
                        syncscanstate = false;
                    }
                }
                else if (totalpending > 10)
                {
                    if (!syncscanstate)
                    {
                        app->syncupdate_scanning(true);
                        syncscanstate = true;
                    }
                }

                bool success = true;
                string localpath;

                notifypurge();

                if (syncactivity || syncops)
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
                        else
                        {
                            localpath = (*it)->localroot.localname;

                            if ((*it)->state == SYNC_ACTIVE && (!syncscanstate || syncdownrequired))
                            {
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
                        syncdownrequired = false;
                        if (syncfsopsfailed)
                        {
                            syncfsopsfailed = false;
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

                    // perform aggregate ops that require all scanqs to be fully processed
                    for (it = syncs.begin(); it != syncs.end(); it++)
                    {
                        if ((*it)->dirnotify->notifyq[DirNotify::DIREVENTS].size()
                                || (*it)->dirnotify->notifyq[DirNotify::RETRY].size())
                        {
                            if (!syncnagleretry)
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
                            localnode_set::iterator it;

                            while (localsyncnotseen.size())
                            {
                                delete *localsyncnotseen.begin();
                            }
                        }

                        // process filesystem notifications for active syncs unless we
                        // are retrying local fs writes
                        if (!syncfsopsfailed)
                        {
                            // FIXME: only syncup for subtrees that were actually
                            // updated to reduce CPU load
                            for (it = syncs.begin(); it != syncs.end(); it++)
                            {
                                if (((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN)
                                        && !(*it)->dirnotify->notifyq[DirNotify::DIREVENTS].size()
                                        && !(*it)->dirnotify->notifyq[DirNotify::RETRY].size()
                                        && !syncadding)
                                {
                                    syncup(&(*it)->localroot, &nds);
                                    (*it)->cachenodes();
                                }
                            }

                            if (EVER(nds))
                            {
                                syncnaglebt.backoff(nds - Waiter::ds);
                                syncnagleretry = true;
                            }

                            // delete files that were overwritten by folders in syncup()
                            execsyncdeletions();

                            if (synccreate.size())
                            {
                                syncupdate();
                            }

                            unsigned totalnodes = 0;

                            syncscanfailed = false;

                            // we have no sync-related operations pending - trigger processing if at least one
                            // filesystem item is notified or initiate a full rescan if there has been
                            // an event notification failure (or event notification is unavailable)
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

                                            if (sync->dirnotify->failed || fsaccess->notifyfailed || sync->dirnotify->error || fsaccess->notifyerr)
                                            {
                                                syncscanfailed = true;

                                                sync->scan(&sync->localroot.localname, NULL);
                                                sync->dirnotify->error = false;
                                                fsaccess->notifyerr = false;

                                                sync->fullscan = true;
                                                sync->scanseqno++;

                                                syncscanbt.backoff(10 + totalnodes / 128);
                                            }
                                        }
                                    }
                                }
                            }

                            // clear pending global notification error flag if all syncs were marked
                            // to be rescanned
                            if (fsaccess->notifyerr && it == syncs.end())
                            {
                                fsaccess->notifyerr = false;
                            }

                            // limit rescan rate (interval depends on total tree size)
                            if (syncscanfailed)
                            {
                                syncscanbt.backoff(10 + totalnodes / 128);
                            }

                            execsyncdeletions();
                        }
                    }
                }
            }
        }
#endif

        notifypurge();
    } while (httpio->doio() || execdirectreads()
            || (!pendingcs && reqs[r].cmdspending() && btcs.armed()));

    if (!badhostcs && badhosts.size()) {
        // report hosts affected by failed requests
        badhostcs = new HttpReq();
        badhostcs->posturl = APIURL;
        badhostcs->posturl.append("pf?h");
        badhostcs->outbuf = badhosts;
        badhostcs->type = REQ_JSON;
        badhostcs->post(this);
        badhosts.clear();
    }
}

// get next event time from all subsystems, then invoke the waiter if needed
// returns true if an engine-relevant event has occurred, false otherwise
int MegaClient::wait() {
    dstime nds;

    // get current dstime and clear wait events
    WAIT_CLASS::bumpds();

#ifdef ENABLE_SYNC
    // sync directory scans in progress or still processing sc packet without having
    // encountered a locally locked item? don't wait.
    if (syncactivity || (jsonsc.pos && !syncdownretry))
    {
        nds = Waiter::ds;
    }
    else
#endif
    {
        // next retry of a failed transfer
        nds = NEVER;

        nexttransferretry(PUT, &nds);
        nexttransferretry(GET, &nds);

        // retry transferslots
        for (transferslot_list::iterator it = tslots.begin();
                it != tslots.end(); it++) {
            if (!(*it)->retrybt.armed()) {
                (*it)->retrybt.update(&nds);
            }
        }

        // retry failed client-server requests
        if (!pendingcs) {
            btcs.update(&nds);
        }

        // retry failed server-client requests
        if (!pendingsc && *scsn) {
            btsc.update(&nds);
        }

        // retry failed file attribute puts
        if (curfa == newfa.end()) {
            btpfa.update(&nds);
        }

        // retry failed file attribute gets
        for (fafc_map::iterator cit = fafcs.begin(); cit != fafcs.end();
                cit++) {
            if (cit->second->req.status == REQ_INFLIGHT) {
                cit->second->timeout.update(&nds);
            } else if (cit->second->fafs[1].size()
                    || cit->second->fafs[0].size()) {
                cit->second->bt.update(&nds);
            }
        }

        // next pending pread event
        if (!dsdrns.empty()) {
            if (dsdrns.begin()->first < nds) {
                if (dsdrns.begin()->first <= Waiter::ds) {
                    nds = Waiter::ds;
                } else {
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
        if (syncfslockretry)
        {
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
#endif

        // detect stuck network
        if (EVER(httpio->lastdata)) {
            dstime timeout = httpio->lastdata + HttpIO::NETWORKTIMEOUT;

            if (timeout >= Waiter::ds && timeout < nds) {
                nds = timeout;
            }
        }
    }

    // immediate action required?
    if (!nds) {
        return Waiter::NEEDEXEC;
    }

    // nds is either MAX_INT (== no pending events) or > Waiter::ds
    if (EVER(nds)) {
        nds -= Waiter::ds;
    }

    waiter->init(nds);

    // set subsystem wakeup criteria (WinWaiter assumes httpio to be set first!)
    waiter->wakeupby(httpio, Waiter::NEEDEXEC);
    waiter->wakeupby(fsaccess, Waiter::NEEDEXEC);

    int r = waiter->wait();

    // process results
    r |= httpio->checkevents(waiter);
    r |= fsaccess->checkevents(waiter);

    return r;
}

// reset all backoff timers and transfer retry counters
bool MegaClient::abortbackoff(bool includexfers) {
    bool r = false;

    WAIT_CLASS::bumpds();

    if (includexfers) {
        for (int d = GET; d == GET || d == PUT; d += PUT - GET) {
            for (transfer_map::iterator it = transfers[d].begin();
                    it != transfers[d].end(); it++) {
                if (it->second->failcount) {
                    if (it->second->bt.arm()) {
                        r = true;
                    }
                }
            }
        }
    }

    if (btcs.arm()) {
        r = true;
    }

    if (!pendingsc && btsc.arm()) {
        r = true;
    }

    if (curfa == newfa.end() && btpfa.arm()) {
        r = true;
    }

    for (fafc_map::iterator it = fafcs.begin(); it != fafcs.end(); it++) {
        if (it->second->req.status != REQ_INFLIGHT && it->second->bt.arm()) {
            r = true;
        }
    }

    return r;
}

// this will dispatch the next queued transfer unless one is already in
// progress and force isn't set
// returns true if dispatch occurred, false otherwise
bool MegaClient::dispatch(direction_t d) {
    // do we have any transfer slots available?
    if (!slotavail()) {
        return false;
    }

    // file attribute jam? halt uploads.
    if (d == PUT && newfa.size() > 32) {
        return false;
    }

    transfer_map::iterator nextit;
    TransferSlot *ts = NULL;

    for (;;) {
        nextit = transfers[d].end();

        for (transfer_map::iterator it = transfers[d].begin();
                it != transfers[d].end(); it++) {
            if (!it->second->slot && it->second->bt.armed()
                    && (nextit == transfers[d].end()
                            || it->second->bt.retryin()
                                    < nextit->second->bt.retryin())) {
                nextit = it;
            }
        }

        // no inactive transfers ready?
        if (nextit == transfers[d].end()) {
            return false;
        }

        if (!nextit->second->localfilename.size()) {
            // this is a fresh transfer rather than the resumption of a partly
            // completed and deferred one
            if (d == PUT) {
                // generate fresh random encryption key/CTR IV for this file
                byte keyctriv[SymmCipher::KEYLENGTH + sizeof(int64_t)];
                PrnGen::genblock(keyctriv, sizeof keyctriv);
                nextit->second->key.setkey(keyctriv);
                nextit->second->ctriv = MemAccess::get<uint64_t>(
                        (const char*) keyctriv + SymmCipher::KEYLENGTH);
            } else {
                // set up keys for the decryption of this file (k == NULL => private node)
                Node* n;
                const byte* k = NULL;

                // locate suitable template file
                for (file_list::iterator it = nextit->second->files.begin();
                        it != nextit->second->files.end(); it++) {
                    if ((*it)->hprivate) {
                        // the size field must be valid right away for
                        // MegaClient::moretransfers()
                        if ((n = nodebyhandle((*it)->h))
                                && n->type == FILENODE) {
                            k = (const byte*) n->nodekey.data();
                            nextit->second->size = n->size;
                        }
                    } else {
                        k = (*it)->filekey;
                        nextit->second->size = (*it)->size;
                    }

                    if (k) {
                        nextit->second->key.setkey(k, FILENODE);
                        nextit->second->ctriv = MemAccess::get<int64_t>(
                                (const char*) k + SymmCipher::KEYLENGTH);
                        nextit->second->metamac = MemAccess::get<int64_t>(
                                (const char*) k + SymmCipher::KEYLENGTH
                                        + sizeof(int64_t));

                        // FIXME: re-add support for partial transfers
                        break;
                    }
                }

                if (!k) {
                    return false;
                }
            }

            nextit->second->localfilename.clear();

            // set file localnames (ultimate target) and one transfer-wide temp
            // localname
            for (file_list::iterator it = nextit->second->files.begin();
                    !nextit->second->localfilename.size()
                            && it != nextit->second->files.end(); it++) {
                (*it)->prepare();
            }

            // app-side transfer preparations (populate localname, create thumbnail...)
            app->transfer_prepare(nextit->second);
        }

        // verify that a local path was given and start/resume transfer
        if (nextit->second->localfilename.size()) {
            // allocate transfer slot
            ts = new TransferSlot(nextit->second);

            // try to open file (PUT transfers: open in nonblocking mode)
            if ((d == PUT) ?
                    ts->fa->fopen(&nextit->second->localfilename) :
                    ts->fa->fopen(&nextit->second->localfilename, false,
                            true)) {
                handle h = UNDEF;
                bool hprivate = true;

                nextit->second->pos = 0;

                // always (re)start upload from scratch
                if (d == PUT) {
                    nextit->second->size = ts->fa->size;
                    nextit->second->chunkmacs.clear();

                    // create thumbnail/preview imagery, if applicable (FIXME: do not re-create upon restart)
                    if (gfx && nextit->second->localfilename.size()
                            && !nextit->second->uploadhandle) {
                        nextit->second->uploadhandle = getuploadhandle();

                        // we want all imagery to be safely tucked away before completing the upload, so we bump minfa
                        nextit->second->minfa += gfx->gendimensionsputfa(ts->fa,
                                &nextit->second->localfilename,
                                nextit->second->uploadhandle,
                                &nextit->second->key);
                    }
                } else {
                    // downloads resume at the end of the last contiguous completed block
                    for (chunkmac_map::iterator it =
                            nextit->second->chunkmacs.begin();
                            it != nextit->second->chunkmacs.end(); it++) {
                        if (nextit->second->pos != it->first) {
                            break;
                        }

                        if (nextit->second->size) {
                            nextit->second->pos = ChunkedHash::chunkceil(
                                    nextit->second->pos);
                        }
                    }

                    for (file_list::iterator it = nextit->second->files.begin();
                            it != nextit->second->files.end(); it++) {
                        if (!(*it)->hprivate || nodebyhandle((*it)->h)) {
                            h = (*it)->h;
                            hprivate = (*it)->hprivate;
                            break;
                        }
                    }
                }

                // dispatch request for temporary source/target URL
                reqs[r].add(
                        (ts->pendingcmd =
                                (d == PUT) ?
                                        (Command*) new CommandPutFile(ts,
                                                putmbpscap) :
                                        (Command*) new CommandGetFile(ts, NULL,
                                                h, hprivate)));

                ts->slots_it = tslots.insert(tslots.begin(), ts);

                // notify the app about the starting transfer
                for (file_list::iterator it = nextit->second->files.begin();
                        it != nextit->second->files.end(); it++) {
                    (*it)->start();
                }

                return true;
            }
        }

        // file didn't open - fail & defer
        nextit->second->failed(API_EREAD);
    }
}

// generate upload handle for this upload
// (after 65536 uploads, a node handle clash is possible, but far too unlikely
// to be of real-world concern)
handle MegaClient::getuploadhandle() {
    byte* ptr = (byte*) (&nextuh + 1);

    while (!++*--ptr)
        ;

    return nextuh;
}

// do we have an upload that is still waiting for file attributes before being completed?
void MegaClient::checkfacompletion(handle th, Transfer* t) {
    if (th) {
        bool delayedcompletion;
        handletransfer_map::iterator htit;

        if ((delayedcompletion = !t)) {
            // abort if upload still running
            if ((htit = faputcompletion.find(th)) == faputcompletion.end()) {
                return;
            }

            t = htit->second;
        }

        int facount = 0;

        // do we have the pre-set threshold number of file attributes available? complete upload.
        for (fa_map::iterator it = pendingfa.lower_bound(
                pair<handle, fatype>(th, 0));
                it != pendingfa.end() && it->first.first == th; it++) {
            facount++;
        }

        if (facount < t->minfa) {
            if (!delayedcompletion) {
                // we have insufficient file attributes available: remove transfer and put on hold
                t->faputcompletion_it = faputcompletion.insert(
                        pair<handle, Transfer*>(th, t)).first;

                transfers[t->type].erase(t->transfers_it);
                t->transfers_it = transfers[t->type].end();

                delete t->slot;
                t->slot = NULL;
            }

            return;
        }
    }

    t->completefiles();
    app->transfer_complete(t);
    delete t;
}

// clear transfer queue
void MegaClient::freeq(direction_t d) {
    for (transfer_map::iterator it = transfers[d].begin();
            it != transfers[d].end();) {
        delete it++->second;
    }
}

// determine next scheduled transfer retry
// FIXME: make this an ordered set and only check the first element instead of
// scanning the full map!
void MegaClient::nexttransferretry(direction_t d, dstime* dsmin) {
    for (transfer_map::iterator it = transfers[d].begin();
            it != transfers[d].end(); it++) {
        if ((!it->second->slot || !it->second->slot->fa)
                && it->second->bt.nextset()
                && it->second->bt.nextset() >= Waiter::ds
                && it->second->bt.nextset() < *dsmin) {
            *dsmin = it->second->bt.nextset();
        }
    }
}

// disconnect all HTTP connections (slows down operations, but is semantically neutral)
void MegaClient::disconnect() {
    if (pendingcs) {
        app->request_response_progress(-1, -1);
        pendingcs->disconnect();
    }

    if (pendingsc) {
        pendingsc->disconnect();
    }

    for (transferslot_list::iterator it = tslots.begin(); it != tslots.end();
            it++) {
        (*it)->disconnect();
    }

    for (putfa_list::iterator it = newfa.begin(); it != newfa.end(); it++) {
        (*it)->disconnect();
    }

    for (fafc_map::iterator it = fafcs.begin(); it != fafcs.end(); it++) {
        it->second->req.disconnect();
    }

    httpio->lastdata = NEVER;
    httpio->disconnect();
}

void MegaClient::logout() {
    if (loggedin() != FULLACCOUNT) {
        if (sctable) {
            sctable->remove();
        }

#ifdef ENABLE_SYNC
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            if((*it)->statecachetable)
            {
                (*it)->statecachetable->remove();
            }
        }
#endif
        locallogout();
        app->logout_result(API_OK);
        return;
    }

    reqs[r].add(new CommandLogout(this));
}

void MegaClient::locallogout() {
    int i;

    disconnect();

    delete sctable;
    sctable = NULL;

    me = UNDEF;

    cachedscsn = UNDEF;

    freeq(GET);
    freeq(PUT);

    purgenodesusersabortsc();

    for (i = sizeof(reqs) / sizeof(*reqs); i--;) {
        reqs[i].clear();
    }

    delete pendingcs;
    pendingcs = NULL;

    for (putfa_list::iterator it = newfa.begin(); it != newfa.end(); it++) {
        delete *it;
    }

    newfa.clear();

    for (fafc_map::iterator cit = fafcs.begin(); cit != fafcs.end(); cit++) {
        for (i = 2; i--;) {
            for (faf_map::iterator it = cit->second->fafs[i].begin();
                    it != cit->second->fafs[i].end(); it++) {
                delete it->second;
            }
        }

        delete cit->second;
    }

    fafcs.clear();

    pendingfa.clear();

    // erase master key & session ID
    key.setkey(SymmCipher::zeroiv);
    memset((char*) auth.c_str(), 0, auth.size());
    auth.clear();

    init();

#ifdef ENABLE_SYNC
    syncadding = 0;
#endif
}

const char *MegaClient::version() {
    return TOSTRING(MEGA_MAJOR_VERSION)
    "." TOSTRING(MEGA_MINOR_VERSION)
    "." TOSTRING(MEGA_MICRO_VERSION);
}

// process server-client request
bool MegaClient::procsc() {
    nameid name;

#ifdef ENABLE_SYNC
    char test[] = "},{\"a\":\"t\",\"i\":\"";
    char test2[32] = "\",\"t\":{\"f\":[{\"h\":\"";
    bool stop = false;
#endif
    Node* dn = NULL;

    for (;;) {
        if (!insca) {
            switch (jsonsc.getnameid()) {
            case 'w':
                if (!statecurrent) {
                    // NULL vector: "notify all nodes"
                    app->nodes_current();
                    statecurrent = true;
                }

                jsonsc.storeobject(&scnotifyurl);
                break;

            case MAKENAMEID2('s', 'n'):
                // the sn element is guaranteed to be the last in sequence
                setscsn(&jsonsc);
                break;

            case EOO:
                mergenewshares(1);
                applykeys();
                return true;

            case 'a':
                if (jsonsc.enterarray()) {
                    insca = true;
                    break;
                }
                // fall through
            default:
                if (!jsonsc.storeobject()) {
                    LOG_err << "Error parsing sc request";
                    return true;
                }
            }
        }

        if (insca) {
            if (jsonsc.enterobject()) {
                // the "a" attribute is guaranteed to be the first in the object
                if (jsonsc.getnameid() == 'a') {
                    name = jsonsc.getnameid();

                    // only process server-client request if not marked as
                    // self-originating ("i" marker element guaranteed to be following
                    // "a" element if present)
                    if (memcmp(jsonsc.pos, "\"i\":\"", 5)
                            || memcmp(jsonsc.pos + 5, sessionid,
                                    sizeof sessionid)
                            || jsonsc.pos[5 + sizeof sessionid] != '"') {
                        switch (name) {
                        case 'u':
                            // node update
                            sc_updatenode();
#ifdef ENABLE_SYNC
                            if (jsonsc.pos[1] != ']') // there are more packets
                            {
                                applykeys();
                                return false;
                            }
#endif
                            break;

                        case 't':
#ifdef ENABLE_SYNC
                            if (!stop)
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
                            if (stop)
                            {
                                stop = false;

                                if (jsonsc.pos[1] != ']') // there are more packets
                                {
                                    // run syncdown() before continuing
                                    applykeys();
                                    return false;
                                }
                            }
#endif
                            break;

                        case 'd':
                            // node deletion
                            dn = sc_deltree();

#ifdef ENABLE_SYNC
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

                            if (jsonsc.pos[1] != ']') // there are more packets
                            {
                                // run syncdown() to process the deletion before continuing
                                applykeys();
                                return false;
                            }
#endif
                            break;

                        case 's':
                            // share addition/update/revocation
                            if (sc_shares()) {
                                mergenewshares(1);
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
                            // user attribtue update
                            sc_userattr();
                        }
                    }
                }

                jsonsc.leaveobject();
            } else {
                jsonsc.leavearray();
                insca = false;
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
void MegaClient::initsc() {
    if (sctable) {
        bool complete;

        sctable->begin();
        sctable->truncate();

        // 1. write current scsn
        handle tscsn;
        Base64::atob(scsn, (byte*) &tscsn, sizeof tscsn);
        complete = sctable->put(CACHEDSCSN, (char*) &tscsn, sizeof tscsn);

        if (complete) {
            // 2. write all users
            for (user_map::iterator it = users.begin(); it != users.end();
                    it++) {
                if (!(complete = sctable->put(CACHEDUSER, &it->second, &key))) {
                    break;
                }
            }
        }

        if (complete) {
            // 3. write new or modified nodes, purge deleted nodes
            for (node_map::iterator it = nodes.begin(); it != nodes.end();
                    it++) {
                if (!(complete = sctable->put(CACHEDNODE, it->second, &key))) {
                    break;
                }
            }
        }

        LOG_debug << "Saving SCSN " << scsn << " with " << nodes.size()
                << " nodes and " << users.size() << " users to local cache ("
                << complete << ")";
        finalizesc(complete);
    }
}

// erase and and fill user's local state cache
void MegaClient::updatesc() {
    if (sctable) {
        string t;

        sctable->get(CACHEDSCSN, &t);

        if (t.size() != sizeof cachedscsn) {
            LOG_err << "Invalid scsn size";
            return;
        }

        sctable->begin();

        bool complete;

        // 1. update associated scsn
        handle tscsn;
        Base64::atob(scsn, (byte*) &tscsn, sizeof tscsn);
        complete = sctable->put(CACHEDSCSN, (char*) &tscsn, sizeof tscsn);

        if (complete) {
            // 2. write new or update modified users
            for (user_vector::iterator it = usernotify.begin();
                    it != usernotify.end(); it++) {
                if (!(complete = sctable->put(CACHEDUSER, *it, &key))) {
                    break;
                }
            }
        }

        if (complete) {
            // 3. write new or modified nodes, purge deleted nodes
            for (node_vector::iterator it = nodenotify.begin();
                    it != nodenotify.end(); it++) {
                char base64[12];
                if ((*it)->changed.removed && (*it)->dbid) {
                    LOG_verbose << "Removing node from database: "
                            << (Base64::btoa((byte*) &((*it)->nodehandle),
                                    MegaClient::NODEHANDLE, base64) ?
                                    base64 : "");
                    if (!(complete = sctable->del((*it)->dbid))) {
                        break;
                    }
                } else {
                    LOG_verbose << "Adding node to database: "
                            << (Base64::btoa((byte*) &((*it)->nodehandle),
                                    MegaClient::NODEHANDLE, base64) ?
                                    base64 : "");
                    if (!(complete = sctable->put(CACHEDNODE, *it, &key))) {
                        break;
                    }
                }
            }
        }

        LOG_debug << "Saving SCSN " << scsn << " with " << nodenotify.size()
                << " modified nodes and " << usernotify.size()
                << " users to local cache (" << complete << ")";
        finalizesc(complete);
    }

}

// commit or purge local state cache
void MegaClient::finalizesc(bool complete) {
    if (complete) {
        sctable->commit();
        Base64::atob(scsn, (byte*) &cachedscsn, sizeof cachedscsn);
    } else {
        sctable->abort();
        sctable->truncate();

        LOG_err << "Cache update DB write error - disabling caching";

        delete sctable;
        sctable = NULL;
    }

}

// queue node file attribute for retrieval or cancel retrieval
error MegaClient::getfa(Node* n, fatype t, int cancel) {
    // locate this file attribute type in the nodes's attribute string
    handle fah;
    int p, pp;

    if (!(p = n->hasfileattribute(t))) {
        return API_ENOENT;
    }

    pp = p - 1;

    while (pp && n->fileattrstring[pp - 1] >= '0'
            && n->fileattrstring[pp - 1] <= '9') {
        pp--;
    }

    if (p == pp) {
        return API_ENOENT;
    }

    if (Base64::atob(strchr(n->fileattrstring.c_str() + p, '*') + 1,
            (byte*) &fah, sizeof(fah)) != sizeof(fah)) {
        return API_ENOENT;
    }

    int c = atoi(n->fileattrstring.c_str() + pp);

    if (cancel) {
        // cancel pending request
        fafc_map::iterator cit;

        if ((cit = fafcs.find(c)) != fafcs.end()) {
            faf_map::iterator it;

            for (int i = 2; i--;) {
                if ((it = cit->second->fafs[i].find(fah))
                        != cit->second->fafs[i].end()) {
                    delete it->second;
                    cit->second->fafs[i].erase(it);

                    // none left: tear down connection
                    if (!cit->second->fafs[1].size()
                            && cit->second->req.status == REQ_INFLIGHT) {
                        cit->second->req.disconnect();
                    }

                    return API_OK;
                }
            }
        }

        return API_ENOENT;
    } else {
        // add file attribute cluster channel and set cluster reference node handle
        FileAttributeFetchChannel** fafcp = &fafcs[c];

        if (!*fafcp) {
            *fafcp = new FileAttributeFetchChannel();
        }

        if (!(*fafcp)->fafs[1].count(fah)) {
            (*fafcp)->fahref = fah;

            // map returned handle to type/node upon retrieval response
            FileAttributeFetch** fafp = &(*fafcp)->fafs[0][fah];

            if (!*fafp) {
                *fafp = new FileAttributeFetch(n->nodehandle, t, reqtag);
            } else {
                restag = (*fafp)->tag;
                return API_EEXIST;
            }
        } else {
            FileAttributeFetch** fafp = &(*fafcp)->fafs[1][fah];
            restag = (*fafp)->tag;
            return API_EEXIST;
        }

        return API_OK;
    }
}

// build pending attribute string for this handle and remove
void MegaClient::pendingattrstring(handle h, string* fa) {
    char buf[128];

    for (fa_map::iterator it = pendingfa.lower_bound(
            pair<handle, fatype>(h, 0));
            it != pendingfa.end() && it->first.first == h;) {
        sprintf(buf, "/%u*", (unsigned) it->first.second);
        Base64::btoa((byte*) &it->second.first, sizeof(it->second.first),
                strchr(buf + 3, 0));
        pendingfa.erase(it++);
        fa->append(buf + !fa->size());
    }
}

// attach file attribute to a file (th can be upload or node handle)
// FIXME: to avoid unnecessary roundtrips to the attribute servers, also cache locally
void MegaClient::putfa(handle th, fatype t, SymmCipher* key, string* data) {
    // CBC-encrypt attribute data (padded to next multiple of BLOCKSIZE)
    data->resize(
            (data->size() + SymmCipher::BLOCKSIZE - 1)
                    & -SymmCipher::BLOCKSIZE);
    key->cbc_encrypt((byte*) data->data(), data->size());

    newfa.push_back(new HttpReqCommandPutFA(this, th, t, data));

    // no other file attribute storage request currently in progress? POST this one.
    if (curfa == newfa.end()) {
        curfa = newfa.begin();
        reqs[r].add(*curfa);
    }
}

// has the limit of concurrent transfer tslots been reached?
bool MegaClient::slotavail() const {
    return tslots.size() < MAXTRANSFERS;
}

// returns 1 if more transfers of the requested type can be dispatched
// (back-to-back overlap pipelining)
// FIXME: support overlapped partial reads (and support partial reads in the
// first place)
bool MegaClient::moretransfers(direction_t d) {
    m_off_t c = 0, r = 0;
    dstime t = 0;
    int total = 0;

    // don't dispatch if all tslots busy
    if (!slotavail()) {
        return false;
    }

    // determine average speed and total amount of data remaining for the given
    // direction
    for (transferslot_list::iterator it = tslots.begin(); it != tslots.end();
            it++) {
        if ((*it)->transfer->type == d) {
            if ((*it)->starttime) {
                t += Waiter::ds - (*it)->starttime;
            }

            c += (*it)->progressreported;
            r += (*it)->transfer->size - (*it)->progressreported;
            total++;
        }
    }

    // always blindly dispatch transfers up to MINPIPELINE
    if (r < MINPIPELINE) {
        return true;
    }

    // otherwise, don't allow more than two concurrent transfers
    if (total >= 2) {
        return false;
    }

    // dispatch more if less than two seconds of transfers left (at least
    // 5 seconds must have elapsed for precise speed indication)
    if (t > 50) {
        int bpds = (int) (c / t);

        if (bpds > 100 && r / bpds < 20) {
            return true;
        }
    }

    return false;
}

void MegaClient::dispatchmore(direction_t d) {
    // keep pipeline full by dispatching additional queued transfers, if
    // appropriate and available
    while (moretransfers(d) && dispatch(d))
        ;
}

// server-client node update processing
void MegaClient::sc_updatenode() {
    handle h = UNDEF;
    handle u = 0;
    const char* a = NULL;
    m_time_t ts = -1;

    for (;;) {
        switch (jsonsc.getnameid()) {
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
            if (!ISUNDEF(h)) {
                Node* n;

                if ((n = nodebyhandle(h))) {
                    if (u) {
                        n->owner = u;
                        n->changed.owner = true;
                    }

                    if (a) {
                        if (!n->attrstring) {
                            n->attrstring = new string;
                        }
                        Node::copystring(n->attrstring, a);
                        n->changed.attrs = true;
                    }

                    if (ts + 1) {
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
            if (!jsonsc.storeobject()) {
                return;
            }
        }
    }
}

// read tree object (nodes and users)
void MegaClient::readtree(JSON* j) {
    if (j->enterobject()) {
        for (;;) {
            switch (jsonsc.getnameid()) {
            case 'f':
                readnodes(j, 1);
                break;

            case 'u':
                readusers(j);
                break;

            case EOO:
                j->leaveobject();
                return;

            default:
                if (!jsonsc.storeobject()) {
                    return;
                }
            }
        }
    }
}

// server-client newnodes processing
void MegaClient::sc_newnodes() {
    for (;;) {
        switch (jsonsc.getnameid()) {
        case 't':
            readtree(&jsonsc);
            break;

        case 'u':
            readusers(&jsonsc);
            break;

        case EOO:
            return;

        default:
            if (!jsonsc.storeobject()) {
                return;
            }
        }
    }
}

// share requests come in the following flavours:
// - n/k (set share key) (always symmetric)
// - n/o/u (share deletion)
// - n/o/u/k/r/ts[/ok][/ha] (share addition) (k can be asymmetric)
// returns 0 in case of a share addition or error, 1 otherwise
bool MegaClient::sc_shares() {
    handle h = UNDEF;
    handle oh = UNDEF;
    handle uh = UNDEF;
    const char* k = NULL;
    const char* ok = NULL;
    byte ha[SymmCipher::BLOCKSIZE];
    byte sharekey[SymmCipher::BLOCKSIZE];
    int have_ha = 0;
    accesslevel_t r = ACCESS_UNKNOWN;
    m_time_t ts = 0;
    int outbound;

    for (;;) {
        switch (jsonsc.getnameid()) {
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

        case MAKENAMEID2('h', 'a'):  // outgoing share signature
            have_ha = Base64::atob(jsonsc.getvalue(), ha, sizeof ha)
                    == sizeof ha;
            break;

        case 'r':   // share access level
            r = (accesslevel_t) jsonsc.getint();
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
            if (loggedin() < FULLACCOUNT) {
                return false;
            }

            // need a share node
            if (ISUNDEF(h)) {
                return false;
            }

            // ignore unrelated share packets (should never be triggered)
            if (!ISUNDEF(oh) && !(outbound = (oh == me)) && (uh != me)) {
                return false;
            }

            // am I the owner of the share? use ok, otherwise k.
            if (ok && oh == me) {
                k = ok;
            }

            if (k) {
                if (!decryptkey(k, sharekey, sizeof sharekey, &key, 1, h)) {
                    return false;
                }

                if (ISUNDEF(oh) && ISUNDEF(uh)) {
                    // share key update on inbound share
                    newshares.push_back(
                            new NewShare(h, 0, UNDEF, ACCESS_UNKNOWN, 0,
                                    sharekey));
                    return true;
                }

                if (!ISUNDEF(oh) && !ISUNDEF(uh)) {
                    // new share - can be inbound or outbound
                    newshares.push_back(
                            new NewShare(h, outbound, outbound ? uh : oh, r, ts,
                                    sharekey, have_ha ? ha : NULL));

                    //Returns false because as this is a new share, the node
                    //could not have been received yet
                    return false;
                }
            } else {
                if (!ISUNDEF(oh) && !ISUNDEF(uh)) {
                    // share revocation
                    newshares.push_back(
                            new NewShare(h, outbound, outbound ? uh : oh,
                                    ACCESS_UNKNOWN, 0, NULL));
                    return true;
                }
            }

            return false;

        default:
            if (!jsonsc.storeobject()) {
                return false;
            }
        }
    }
}

// user/contact updates come in the following format:
// u:[{c/m/ts}*] - Add/modify user/contact
void MegaClient::sc_contacts() {
    for (;;) {
        switch (jsonsc.getnameid()) {
        case 'u':
            readusers(&jsonsc);
            break;

        case EOO:
            return;

        default:
            if (!jsonsc.storeobject()) {
                return;
            }
        }
    }
}

// server-client key requests/responses
void MegaClient::sc_keys() {
    handle h;
    Node* n = NULL;
    node_vector kshares;
    node_vector knodes;

    for (;;) {
        switch (jsonsc.getnameid()) {
        case MAKENAMEID2('s', 'r'):
            procsr(&jsonsc);
            break;

        case 'h':
            // security feature: we only distribute node keys for our own
            // outgoing shares
            if (!ISUNDEF(h = jsonsc.gethandle()) && (n = nodebyhandle(h))
                    && n->sharekey && !n->inshare) {
                kshares.push_back(n);
            }
            break;

        case 'n':
            if (jsonsc.enterarray()) {
                while (!ISUNDEF(h = jsonsc.gethandle()) && (n = nodebyhandle(h))) {
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
            if (!jsonsc.storeobject()) {
                return;
            }
        }
    }
}

// server-client file attribute update
void MegaClient::sc_fileattr() {
    Node* n = NULL;
    const char* fa = NULL;

    for (;;) {
        switch (jsonsc.getnameid()) {
        case MAKENAMEID2('f', 'a'):
            fa = jsonsc.getvalue();
            break;

        case 'n':
            handle h;
            if (!ISUNDEF(h = jsonsc.gethandle())) {
                n = nodebyhandle(h);
            }
            break;

        case EOO:
            if (fa && n) {
                Node::copystring(&n->fileattrstring, fa);
                n->changed.fileattrstring = true;
                notifynode(n);
            }
            return;

        default:
            if (!jsonsc.storeobject()) {
                return;
            }
        }
    }
}

// server-client user attribute update notification
void MegaClient::sc_userattr() {
    string ua;
    handle uh = UNDEF;

    for (;;) {
        switch (jsonsc.getnameid()) {
        case 'u':
            uh = jsonsc.gethandle(USERHANDLE);
            break;

        case MAKENAMEID2('u', 'a'):
            if (!ISUNDEF(uh)) {
                User* u;

                if ((u = finduser(uh))) {
                    if (jsonsc.enterarray()) {
                        while (jsonsc.storeobject(&ua)) {
                            if (ua[0] == '+') {
                                app->userattr_update(u, 0, ua.c_str() + 1);
                            } else if (ua[0] == '*') {
                                app->userattr_update(u, 1, ua.c_str() + 1);
                            }
                        }

                        jsonsc.leavearray();
                        return;
                    }
                }
            }

            jsonsc.storeobject();
            break;

        case EOO:
            return;

        default:
            if (!jsonsc.storeobject()) {
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
void MegaClient::notifypurge(void) {
    int i, t;

    handle tscsn = cachedscsn;

    if (*scsn)
        Base64::atob(scsn, (byte*) &tscsn, sizeof tscsn);

    if (nodenotify.size() || usernotify.size() || cachedscsn != tscsn) {
        updatesc();

#ifdef ENABLE_SYNC
        // update LocalNode <-> Node associations
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            (*it)->cachenodes();
        }
#endif
    }

    if ((t = nodenotify.size())) {
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

        syncadded = true;
#endif
        applykeys();

        app->nodes_updated(&nodenotify[0], t);

        // check all notified nodes for removed status and purge
        for (i = 0; i < t; i++) {
            Node* n = nodenotify[i];

            if (n->changed.removed) {
                // remove inbound share
                if (n->inshare) {
                    n->inshare->user->sharing.erase(n->nodehandle);
                    notifyuser(n->inshare->user);
                }

                nodes.erase(n->nodehandle);
                delete n;
            } else {
                n->notified = false;
                memset(&(n->changed), 0, sizeof(n->changed));
            }
        }

        nodenotify.clear();
    }

    // users are never deleted
    if ((t = usernotify.size())) {
        app->users_updated(&usernotify[0], t);
        usernotify.clear();
    }
}

// return node pointer derived from node handle
Node* MegaClient::nodebyhandle(handle h) {
    node_map::iterator it;

    if ((it = nodes.find(h)) != nodes.end()) {
        return it->second;
    }

    return NULL;
}

// server-client deletion
Node* MegaClient::sc_deltree() {
    Node* n = NULL;

    for (;;) {
        switch (jsonsc.getnameid()) {
        case 'n':
            handle h;

            if (!ISUNDEF((h = jsonsc.gethandle()))) {
                n = nodebyhandle(h);
            }
            break;

        case EOO:
            if (n) {
                TreeProcDel td;
                proctree(n, &td);
            }
            return n;

        default:
            if (!jsonsc.storeobject()) {
                return NULL;
            }
        }
    }
}

// generate handle authentication token
void MegaClient::handleauth(handle h, byte* auth) {
    Base64::btoa((byte*) &h, NODEHANDLE, (char*) auth);
    memcpy(auth + sizeof h, auth, sizeof h);
    key.ecb_encrypt(auth);
}

// make attribute string; add magic number prefix
void MegaClient::makeattr(SymmCipher* key, string* attrstring, const char* json,
        int l) const {
    if (l < 0) {
        l = strlen(json);
    }
    int ll = (l + 6 + SymmCipher::KEYLENGTH - 1) & -SymmCipher::KEYLENGTH;
    byte* buf = new byte[ll];

    memcpy(buf, "MEGA{", 5); // check for the presence of the magic number "MEGA"
    memcpy(buf + 5, json, l);
    buf[l + 5] = '}';
    memset(buf + 6 + l, 0, ll - l - 6);

    key->cbc_encrypt(buf, ll);

    attrstring->assign((char*) buf, ll);

    delete[] buf;
}

// update node attributes - optional newattr is { name, value, name, value, ..., NULL }
// (with speculative instant completion)
error MegaClient::setattr(Node* n, const char** newattr, const char *prevattr) {
    if (!checkaccess(n, FULL)) {
        return API_EACCESS;
    }

    SymmCipher* cipher;

    if (!(cipher = n->nodecipher())) {
        return API_EKEY;
    }

    if (newattr) {
        while (*newattr) {
            n->attrs.map[nameid(*newattr)] = newattr[1];
            newattr += 2;
        }
    }

    n->changed.attrs = true;
    notifynode(n);

    reqs[r].add(new CommandSetAttr(this, n, cipher, prevattr));

    return API_OK;
}

// send new nodes to API for processing
void MegaClient::putnodes(handle h, NewNode* newnodes, int numnodes) {
    reqs[r].add(new CommandPutNodes(this, h, NULL, newnodes, numnodes, reqtag));
}

// drop nodes into a user's inbox (must have RSA keypair)
void MegaClient::putnodes(const char* user, NewNode* newnodes, int numnodes) {
    User* u;

    restag = reqtag;

    if (!(u = finduser(user, 1))) {
        return app->putnodes_result(API_EARGS, USER_HANDLE, newnodes);
    }

    queuepubkeyreq(u, new PubKeyActionPutNodes(newnodes, numnodes, reqtag));
}

// returns 1 if node has accesslevel a or better, 0 otherwise
int MegaClient::checkaccess(Node* n, accesslevel_t a) {
    // folder link access is always read-only - ignore login status during
    // initial tree fetch
    if ((a < OWNERPRELOGIN) && !loggedin()) {
        return a == RDONLY;
    }

    // trace back to root node (always full access) or share node
    while (n) {
        if (n->inshare) {
            return n->inshare->access >= a;
        }

        if (!n->parent) {
            return n->type > FOLDERNODE;
        }

        n = n->parent;
    }

    return 0;
}

// returns API_OK if a move operation is permitted, API_EACCESS or
// API_ECIRCULAR otherwise
error MegaClient::checkmove(Node* fn, Node* tn) {
    // condition #1: cannot move top-level node, must have full access to fn's
    // parent
    if (!fn->parent || !checkaccess(fn->parent, FULL)) {
        return API_EACCESS;
    }

    // condition #2: target must be folder
    if (tn->type == FILENODE) {
        return API_EACCESS;
    }

    // condition #3: must have write access to target
    if (!checkaccess(tn, RDWR)) {
        return API_EACCESS;
    }

    // condition #4: tn must not be below fn (would create circular linkage)
    for (;;) {
        if (tn == fn) {
            return API_ECIRCULAR;
        }

        if (tn->inshare || !tn->parent) {
            break;
        }

        tn = tn->parent;
    }

    // condition #5: fn and tn must be in the same tree (same ultimate parent
    // node or shared by the same user)
    for (;;) {
        if (fn->inshare || !fn->parent) {
            break;
        }

        fn = fn->parent;
    }

    // moves within the same tree or between the user's own trees are permitted
    if (fn == tn || (!fn->inshare && !tn->inshare)) {
        return API_OK;
    }

    // moves between inbound shares from the same user are permitted
    if (fn->inshare && tn->inshare && fn->inshare->user == tn->inshare->user) {
        return API_OK;
    }

    return API_EACCESS;
}

// move node to new parent node (for changing the filename, use setattr and
// modify the 'n' attribute)
error MegaClient::rename(Node* n, Node* p, syncdel_t syncdel,
        handle prevparent) {
    error e;

    if ((e = checkmove(n, p))) {
        return e;
    }

    if (n->setparent(p)) {
        n->changed.parent = true;
        notifynode(n);

        // rewrite keys of foreign nodes that are moved out of an outbound share
        rewriteforeignkeys(n);

        reqs[r].add(new CommandMoveNode(this, n, p, syncdel, prevparent));
    }

    return API_OK;
}

// delete node tree
error MegaClient::unlink(Node* n) {
    if (!n->inshare && !checkaccess(n, FULL)) {
        return API_EACCESS;
    }

    reqs[r].add(new CommandDelNode(this, n->nodehandle));

    mergenewshares(1);

    TreeProcDel td;
    proctree(n, &td);

    return API_OK;
}

// emulates the semantics of its JavaScript counterpart
// (returns NULL if the input is invalid UTF-8)
// unfortunately, discards bits 8-31 of multibyte characters for backwards compatibility
char* MegaClient::str_to_a32(const char* str, int* len) {
    if (!str) {
        return NULL;
    }

    int t = strlen(str);
    int t2 = 4 * ((t + 3) >> 2);
    char* result = new char[t2]();
    uint32_t* a32 = (uint32_t*) result;
    uint32_t unicode;

    int i = 0;
    int j = 0;

    while (i < t) {
        char c = str[i++] & 0xff;

        if (!(c & 0x80)) {
            unicode = c & 0xff;
        } else if ((c & 0xe0) == 0xc0) {
            if (i >= t || (str[i] & 0xc0) != 0x80) {
                delete[] result;
                return NULL;
            }

            unicode = (c & 0x1f) << 6;
            unicode |= str[i++] & 0x3f;
        } else if ((c & 0xf0) == 0xe0) {
            if (i + 2 > t || (str[i] & 0xc0) != 0x80
                    || (str[i + 1] & 0xc0) != 0x80) {
                delete[] result;
                return NULL;
            }

            unicode = (c & 0x0f) << 12;
            unicode |= (str[i++] & 0x3f) << 6;
            unicode |= str[i++] & 0x3f;
        } else if ((c & 0xf8) == 0xf0) {
            if (i + 3 > t || (str[i] & 0xc0) != 0x80
                    || (str[i + 1] & 0xc0) != 0x80
                    || (str[i + 2] & 0xc0) != 0x80) {
                delete[] result;
                return NULL;
            }

            unicode = (c & 0x07) << 18;
            unicode |= (str[i++] & 0x3f) << 12;
            unicode |= (str[i++] & 0x3f) << 6;
            unicode |= str[i++] & 0x3f;

            // management of surrogate pairs like the JavaScript code
            uint32_t hi = 0xd800 | ((unicode >> 10) & 0x3F)
                    | (((unicode >> 16) - 1) << 6);
            uint32_t low = 0xdc00 | (unicode & 0x3ff);

            a32[j >> 2] |= htonl(hi << (24 - (j & 3) * 8));
            j++;

            unicode = low;
        } else {
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
error MegaClient::pw_key(const char* utf8pw, byte* key) const {
    int t;
    char* pw;

    if (!(pw = str_to_a32(utf8pw, &t))) {
        return API_EARGS;
    }

    int n = (t + 15) / 16;
    SymmCipher* keys = new SymmCipher[n];

    for (int i = 0; i < n; i++) {
        int valid =
                (i != (n - 1)) ?
                        SymmCipher::BLOCKSIZE : (t - SymmCipher::BLOCKSIZE * i);
        memcpy(key, pw + i * SymmCipher::BLOCKSIZE, valid);
        memset(key + valid, 0, SymmCipher::BLOCKSIZE - valid);
        keys[i].setkey(key);
    }

    memcpy(key,
            "\x93\xC4\x67\xE3\x7D\xB0\xC7\xA4\xD1\xBE\x3F\x81\x01\x52\xCB\x56",
            SymmCipher::BLOCKSIZE);

    for (int r = 65536; r--;) {
        for (int i = 0; i < n; i++) {
            keys[i].ecb_encrypt(key);
        }
    }

    delete[] keys;
    delete[] pw;

    return API_OK;
}

void MegaClient::loadbalancing(const char* service) {
    loadbalancingreqs.push(new CommandLoadBalancing(this, service));
}

// compute generic string hash
void MegaClient::stringhash(const char* s, byte* hash, SymmCipher* cipher) {
    int t;

    t = strlen(s) & -SymmCipher::BLOCKSIZE;

    strncpy((char*) hash, s + t, SymmCipher::BLOCKSIZE);

    while (t) {
        t -= SymmCipher::BLOCKSIZE;
        SymmCipher::xorblock((byte*) s + t, hash);
    }

    for (t = 16384; t--;) {
        cipher->ecb_encrypt(hash);
    }

    memcpy(hash + 4, hash + 8, 4);
}

// (transforms s to lowercase)
uint64_t MegaClient::stringhash64(string* s, SymmCipher* c) {
    byte hash[SymmCipher::KEYLENGTH];

    transform(s->begin(), s->end(), s->begin(), ::tolower);
    stringhash(s->c_str(), hash, c);

    return MemAccess::get<uint64_t>((const char*) hash);
}

// read and add/verify node array
int MegaClient::readnodes(JSON* j, int notify, putsource_t source, NewNode* nn,
        int nnsize, int tag) {
    if (!j->enterarray()) {
        return 0;
    }

    node_vector dp;
    Node* n;

    while (j->enterobject()) {
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

        while ((name = j->getnameid()) != EOO) {
            switch (name) {
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
                t = (nodetype_t) j->getint();
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
                rl = (accesslevel_t) j->getint();
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
                if (!j->storeobject()) {
                    return 0;
                }
            }
        }

        if (ISUNDEF(h)) {
            warn("Missing node handle");
        } else {
            if (t == TYPE_UNKNOWN) {
                warn("Unknown node type");
            } else if (t == FILENODE || t == FOLDERNODE) {
                if (ISUNDEF(ph)) {
                    warn("Missing parent");
                } else if (!a) {
                    warn("Missing node attributes");
                } else if (!k) {
                    warn("Missing node key");
                }

                if (t == FILENODE && ISUNDEF(s)) {
                    warn("File node without file size");
                }
            }
        }

        if (fa && t != FILENODE) {
            warn("Spurious file attributes");
        }

        if (!warnlevel()) {
            if ((n = nodebyhandle(h))) {
                if (n->changed.removed) {
                    // node marked for deletion is being resurrected, possibly
                    // with a new parent (server-client move operation)
                    n->changed.removed = false;
                } else {
                    // node already present - check for race condition
                    if ((n->parent && ph != n->parent->nodehandle)
                            || n->type != t) {
                        app->reload("Node inconsistency (parent linkage)");
                    }
                }

                if (!ISUNDEF(ph)) {
                    Node* p;

                    if ((p = nodebyhandle(ph))) {
                        n->setparent(p);
                        n->changed.parent = true;
                    } else {
                        n->setparent(NULL);
                        n->parenthandle = ph;
                        dp.push_back(n);
                    }
                }
            } else {
                byte buf[SymmCipher::KEYLENGTH];

                if (!ISUNDEF(su)) {
                    if (t != FOLDERNODE) {
                        warn("Invalid share node type");
                    }

                    if (rl == ACCESS_UNKNOWN) {
                        warn("Missing access level");
                    }

                    if (!sk) {
                        warn("Missing share key for inbound share");
                    }

                    if (warnlevel()) {
                        su = UNDEF;
                    } else {
                        decryptkey(sk, buf, sizeof buf, &key, 1, h);
                    }
                }

                string fas;

                Node::copystring(&fas, fa);

                // fallback timestamps
                if (!(ts + 1)) {
                    ts = time(NULL);
                }

                if (!(sts + 1)) {
                    sts = ts;
                }

                n = new Node(this, &dp, h, ph, t, s, u, fas.c_str(), ts);

                n->tag = tag;

                n->attrstring = new string;
                Node::copystring(n->attrstring, a);
                Node::copystring(&n->nodekey, k);

                if (!ISUNDEF(su)) {
                    newshares.push_back(new NewShare(h, 0, su, rl, sts, buf));
                }

                if (nn && nni >= 0 && nni < nnsize) {
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

                    if (nn[nni].source == NEW_UPLOAD) {
                        handle uh = nn[nni].uploadhandle;

                        // do we have pending file attributes for this upload? set them.
                        for (fa_map::iterator it = pendingfa.lower_bound(
                                pair<handle, fatype>(uh, 0));
                                it != pendingfa.end() && it->first.first == uh;
                                ) {
                            reqs[r].add(
                                    new CommandAttachFA(h, it->first.second,
                                            it->second.first,
                                            it->second.second));
                            pendingfa.erase(it++);
                        }

                        // FIXME: only do this for in-flight FA writes
                        uhnh.insert(pair<handle, handle>(uh, h));
                    }
                }
            }

            if (notify) {
                notifynode(n);
            }
        }
    }

    // any child nodes that arrived before their parents?
    for (int i = dp.size(); i--;) {
        if ((n = nodebyhandle(dp[i]->parenthandle))) {
            dp[i]->setparent(n);
        }
    }

    return j->leavearray();
}

// decrypt and set encrypted sharekey
void MegaClient::setkey(SymmCipher* c, const char* k) {
    byte newkey[SymmCipher::KEYLENGTH];

    if (Base64::atob(k, newkey, sizeof newkey) == sizeof newkey) {
        key.ecb_decrypt(newkey);
        c->setkey(newkey);
    }
}

// read outbound share keys
void MegaClient::readok(JSON* j) {
    if (j->enterarray()) {
        while (j->enterobject()) {
            readokelement(j);
        }

        j->leavearray();

        mergenewshares(0);
    }
}

// - h/ha/k (outbound sharekeys, always symmetric)
void MegaClient::readokelement(JSON* j) {
    handle h = UNDEF;
    byte ha[SymmCipher::BLOCKSIZE];
    byte buf[SymmCipher::BLOCKSIZE];
    int have_ha = 0;
    const char* k = NULL;

    for (;;) {
        switch (j->getnameid()) {
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
            if (ISUNDEF(h)) {
                LOG_warn << "Missing outgoing share handle in ok element";
                return;
            }

            if (!k) {
                LOG_warn << "Missing outgoing share key in ok element";
                return;
            }

            if (!have_ha) {
                LOG_warn << "Missing outbound share signature";
                return;
            }

            if (decryptkey(k, buf, SymmCipher::KEYLENGTH, &key, 1, h)) {
                newshares.push_back(
                        new NewShare(h, 1, UNDEF, ACCESS_UNKNOWN, 0, buf, ha));
            }
            return;

        default:
            if (!j->storeobject()) {
                return;
            }
        }
    }
}

// read outbound shares
void MegaClient::readoutshares(JSON* j) {
    if (j->enterarray()) {
        while (j->enterobject()) {
            readoutshareelement(j);
        }

        j->leavearray();

        mergenewshares(0);
    }
}

// - h/u/r/ts (outbound share)
void MegaClient::readoutshareelement(JSON* j) {
    handle h = UNDEF;
    handle uh = UNDEF;
    accesslevel_t r = ACCESS_UNKNOWN;
    m_time_t ts = 0;

    for (;;) {
        switch (j->getnameid()) {
        case 'h':
            h = j->gethandle();
            break;

        case 'u':           // share target user
            uh = j->is(EXPORTEDLINK) ? 0 : j->gethandle(USERHANDLE);
            break;

        case 'r':           // access
            r = (accesslevel_t) j->getint();
            break;

        case MAKENAMEID2('t', 's'):      // timestamp
            ts = j->getint();
            break;

        case EOO:
            if (ISUNDEF(h)) {
                LOG_warn << "Missing outgoing share node";
                return;
            }

            if (ISUNDEF(uh)) {
                LOG_warn << "Missing outgoing share user";
                return;
            }

            if (r == ACCESS_UNKNOWN) {
                LOG_warn << "Missing outgoing share access";
                return;
            }

            newshares.push_back(new NewShare(h, 1, uh, r, ts, NULL, NULL));
            return;

        default:
            if (!j->storeobject()) {
                return;
            }
        }
    }
}

int MegaClient::applykeys() {
    int t = 0;

    // FIXME: rather than iterating through the whole node set, maintain subset
    // with missing keys
    for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++) {
        if (it->second->applykey()) {
            t++;
        }
    }

    if (sharekeyrewrite.size()) {
        reqs[r].add(new CommandShareKeyUpdate(this, &sharekeyrewrite));
        sharekeyrewrite.clear();
    }

    if (nodekeyrewrite.size()) {
        reqs[r].add(new CommandNodeKeyUpdate(this, &nodekeyrewrite));
        nodekeyrewrite.clear();
    }

    return t;
}

// user/contact list
bool MegaClient::readusers(JSON* j) {
    if (!j->enterarray()) {
        return 0;
    }

    while (j->enterobject()) {
        handle uh = 0;
        visibility_t v = VISIBILITY_UNKNOWN; // new share objects do not override existing visibility
        m_time_t ts = 0;
        const char* m = NULL;
        nameid name;

        while ((name = j->getnameid()) != EOO) {
            switch (name) {
            case 'u':   // new node: handle
                uh = j->gethandle(USERHANDLE);
                break;

            case 'c':   // visibility
                v = (visibility_t) j->getint();
                break;

            case 'm':   // attributes
                m = j->getvalue();
                break;

            case MAKENAMEID2('t', 's'):
                ts = j->getint();
                break;

            default:
                if (!j->storeobject()) {
                    return false;
                }
            }
        }

        if (ISUNDEF(uh)) {
            warn("Missing contact user handle");
        }

        if (!m) {
            warn("Unknown contact user e-mail address");
        }

        if (!warnlevel()) {
            User* u;

            if (v == ME) {
                me = uh;
            }

            if ((u = finduser(uh, 1))) {
                mapuser(uh, m);

                if (v != VISIBILITY_UNKNOWN) {
                    u->set(v, ts);
                }

                notifyuser(u);
            }
        }
    }

    return j->leavearray();
}

error MegaClient::folderaccess(const char* f, const char* k) {
    handle h = 0;
    byte folderkey[SymmCipher::KEYLENGTH];

    locallogout();

    if (Base64::atob(f, (byte*) &h, NODEHANDLE) != NODEHANDLE) {
        return API_EARGS;
    }

    if (Base64::atob(k, folderkey, sizeof folderkey) != sizeof folderkey) {
        return API_EARGS;
    }

    setrootnode(h);
    key.setkey(folderkey);

    return API_OK;
}

// create new session
void MegaClient::login(const char* email, const byte* pwkey) {
    locallogout();

    string lcemail(email);

    key.setkey((byte*) pwkey);

    uint64_t emailhash = stringhash64(&lcemail, &key);

    reqs[r].add(new CommandLogin(this, email, emailhash));
}

void MegaClient::fastlogin(const char* email, const byte* pwkey,
        uint64_t emailhash) {
    locallogout();

    string lcemail(email);

    key.setkey((byte*) pwkey);

    reqs[r].add(new CommandLogin(this, email, emailhash));
}

void MegaClient::getuserdata() {
    reqs[r].add(new CommandGetUserData(this));
}

// ATTR
void MegaClient::setuserattribute(const char *user, const char *an,
        ValueMap map, int priv, int nonhistoric) {
    int savedrt = reqtag;
    setgattribute(an, map, priv, nonhistoric, [this, savedrt](error e) {
        restag = savedrt;
        app->putguattr_result(e);
    });
}

void MegaClient::getuserattribute(const char *user, const char *an) {
    int savedrt = reqtag;
    getgattribute(user, an, [this, savedrt](ValueMap map, error e)
    {
        restag = savedrt;
        app->getguattr_result(map, e);
    });
}

void MegaClient::getgattribute(const char *user, const char *an,
        std::function<void(ValueMap, error)> funct) {
    reqs[r].add(new CommandGetUserAttr(this, user, an, 0, funct));
}

void MegaClient::setgattribute(const char *an, ValueMap map, int priv,
        int nonhistoric, std::function<void(error)> funct) {

    SharedBuffer tlvApp;
    char p = (priv) ? '*' : '+';
    int hV = (nonhistoric) ? 1 : 0;
    char h = '!';
    if (priv) {
        SharedBuffer tlv = UserAttributes::valueMapToTlv(map);
        std::string data((char*) tlv.get(), tlv.size);
        string iv;

        PaddedCBC::encrypt(&data, &key, &iv);

        // Now prepend the data with the (8 byte) IV.
        iv.append(data);
        tlvApp = SharedBuffer(iv.size() + 1 + hV);
        memcpy(tlvApp.get(), &p, 1);
        if (nonhistoric) {
            memcpy(tlvApp.get() + 1, &h, 1);
        }
        memcpy(tlvApp.get() + 1 + hV, iv.c_str(), iv.size());
    } else {
        SharedBuffer value = map->begin()->second;
        tlvApp = SharedBuffer(value.size + 1 + hV);
        memcpy(tlvApp.get(), &p, 1);
        if (nonhistoric) {
            memcpy(tlvApp.get() + 1, &h, 1);
        }
        memcpy(tlvApp.get() + 1 + hV, value.get(), value.size);
        //tlvApp = SharedBuffer(tlv.size + 1);
        //memcpy(tlvApp.get(), &p, 1);
        //memcpy(tlvApp.get() + 1, tlv.get(), tlv.size);
    }

    reqs[r].add(
            new CommandSetUserAttr(this, an, tlvApp.get(), tlvApp.size, funct));

}

void MegaClient::getgattribute(std::string &email, const char *an,
        std::function<void(ValueMap, error)> funct) {
    reqs[r].add(new CommandGetUserAttr(this, email, an, 0, funct));
}

void MegaClient::getownsigningkeys(bool reset) {
    if (signkey.keySet()) {
        LOG_info << "Using cached signing keys";
        std::pair<SecureBuffer, SecureBuffer> keyPair = signkey.getKeyPair();
        if (!(keyPair.first && keyPair.second)) {
            app->getguattr_result(ValueMap(), API_EINTERNAL);
            return;
        }

        ValueMap map = ValueMap(new std::map<std::string, SharedBuffer>);
        SharedBuffer puBuff = SharedBuffer(keyPair.first.get(),
                keyPair.first.size());
        SharedBuffer prBuff = SharedBuffer(keyPair.second.get(),
                keyPair.second.size());
        map->insert( { "puEd255", puBuff });
        map->insert( { "prEd255", prBuff });
        restag = reqtag;
        app->getguattr_result(map, API_OK);

        return;
    }

    int savedrq = reqtag;
    LOG_info << "Fetching signing keys";
    getownsigningkeys([this, savedrq](ValueMap map, error e)
    {
        restag = savedrq;
        app->getguattr_result(map, e);
    }, reset);
}

void MegaClient::getownsigningkeys(GetSigKeysCallback callback, bool reset) {
    if (reset) {
        LOG_info << "Resetting keys";
        uploadkeys(callback);
        return;
    }

    char h[12];
    Base64::btoa((byte*) &me, sizeof me, h);
    std::string hand(h, 11);
    getgattribute(hand, "keyring",
            [this, callback, hand](ValueMap map, error e) mutable
            {

                if(e == API_ENOENT)
                {
                    LOG_info << "Calling upload keys, resource not found";
                    uploadkeys(callback);
                    return;
                }
                else if(e != API_OK)
                {
                    callback(map, e);
                }

                auto i = map->find("prEd255");
                if(i == map->end())
                {
                    callback(map, API_EINTERNAL);
                    return;
                }
                SharedBuffer priKey = i->second;
                getgattribute(hand, "puEd255",
                        [this, priKey, callback](ValueMap pubMap, error e) mutable
                        {

                            if(e == API_ENOENT)
                            {
                                uploadkeys(callback);
                                return;
                            }
                            else if(e != API_OK)
                            {
                                callback(pubMap, e);
                            }

                            SecureBuffer priBuff(priKey.size);
                            if(priBuff.get() == nullptr)
                            {
                                callback(pubMap, API_EINTERNAL);
                                return;
                            }
                            this->signkey.genKeySeed(priBuff);
                            pubMap->insert( {"prEd255", priKey});
                            callback(pubMap, e);
                        });
            });
}

void MegaClient::uploadkeys(GetSigKeysCallback callback) {
    std::pair<SecureBuffer, SecureBuffer> pair = initstatickeys();
    if (pair.first.get() == nullptr) {
        LOG_err << "uploadkeys: Error with key init";
        callback(ValueMap(), API_EINTERNAL);
        return;
    }

    SharedBuffer pubBuff(pair.first.get(), pair.first.size());
    ValueMap pubMap(new std::map<std::string, SharedBuffer>());
    // pubMap->insert("puEd255");
    pubMap->insert( { "", pubBuff });
    ValueMap priMap(new std::map<std::string, SharedBuffer>());
    priMap->insert(
            { "prEd255", SharedBuffer(pair.second.get(), pair.second.size()) });

    ////////////// Create the chain of API calls /////////////////

    std::shared_ptr<std::vector<std::function<void(error)>>>fVec(
            new std::vector<std::function<void(error)>>());

    int savedrt = reqtag;
    auto finalFunc = [this, callback, savedrt](ValueMap map, error e)
    {
        restag = reqtag;
        callback(map, e);
    };

    // Set puEd255.
    fVec->push_back([priMap, pubMap, pubBuff, fVec, finalFunc, this](error e)
    {
        if(e != API_OK)
        {
            finalFunc(priMap, e);
            return;
        }
        setgattribute("keyring", priMap, 1, 0,
                (*fVec)[1]);
    });

    // Then set prEd255.
    fVec->push_back([priMap, pubBuff, fVec, finalFunc, this](error e)
    {
        if(e != API_OK)
        {
            finalFunc(priMap, e);
            return;
        }
        priMap->insert( {"puEd255", pubBuff});
        User *u = new User(nullptr);
        char h[12];
        Base64::btoa((byte*)&me, sizeof me, h);
        u->uid = std::string(h, 11);

        // Get the public key for 'this' user.
            reqs[r].add(new CommandPubKeyRequest(this, u,
                            [finalFunc, fVec, this, priMap, u](handle hd, byte *kBytes, int keyLen) mutable
                            {
                                SharedBuffer sigBuff = signRSAKey(u->pubk.getPublicKeyBytes());
                                delete u;
                                if(sigBuff.get() == nullptr)
                                {
                                    finalFunc(priMap, API_EINTERNAL);
                                    return;
                                }
                                // signedRSA = authRSA
                                ValueMap sigMap(new std::map<std::string, SharedBuffer>);
                                sigMap->insert( {"sgPubk", sigBuff});
                                setgattribute("sgPubk", sigMap, 0, 0,
                                        (*fVec)[2]);
                            }));

        });

    // Then set signedRSA.
    fVec->push_back([priMap, fVec, finalFunc, this](error e)
    {
        finalFunc(priMap, e);
    });

    setgattribute("puEd255", pubMap, 0, 0, (*fVec)[0]);
}

SharedBuffer MegaClient::signRSAKey(SharedBuffer &&keyBytes) {
    std::chrono::seconds now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch());
    long tStamp = now.count();

    std::string auth("keyauth");
    auth.append((char*) &tStamp, sizeof(tStamp));
    auth.append(keyBytes.str());

    SecureBuffer sig = signkey.signDetatched((unsigned char*) auth.c_str(),
            auth.length());
    if (sig.get() == nullptr) {
        LOG_err << "Error creating signature for RSA key";
        return SharedBuffer();
    }

    SharedBuffer retBuffer(sizeof(tStamp) + sig.size());
    memcpy(retBuffer.get(), (void*) &tStamp, sizeof(tStamp));
    memcpy(retBuffer.get() + sizeof(tStamp), sig.get(), sig.size());

    return retBuffer;
}

SharedBuffer MegaClient::createFingerPrint(SharedBuffer &keyBytes) {
    HashSHA256 hash;
    hash.add(keyBytes.get(), keyBytes.size);
    std::string keyHash;
    hash.get(&keyHash);
    keyHash.erase(20, std::string::npos);

    return SharedBuffer((byte*) keyHash.c_str(), keyHash.length());
}

void MegaClient::verifyRSAKeySignature(User *user,
        function<void(error)> funct) {
    if (user->rsaVerified) {
        funct(API_OK);
        return;
    }

    getgattribute(user->uid, "sgPubk",
            [user, funct, this](ValueMap map, error e) mutable
            {
                if(e != API_OK)
                {
                    LOG_err << "Could not get sgPubk attribute.";
                    funct(e);
                    return;
                }
                //auto i = map->find("sgPubk");
                auto i = map->find("");
                if(i == map->end())
                {
                    LOG_err << "No record for sgPubk in VaueMap";
                    funct(API_EINTERNAL);
                    return;
                }
                SharedBuffer sig = i->second;
                if(user->puEd25519)
                {
                    LOG_debug << "test rsa key from cache.";
                    user->rsaVerified = verifyRSAKeySignature_(user, sig);
                    funct((user->rsaVerified) ? API_OK : API_EKEYVERFAIL);
                    return;
                }
                else
                {
                    getgattribute(user->uid, "puEd255", [funct, user, this, sig](ValueMap map, error e) mutable
                            {
                                if(e != API_OK)
                                {
                                    funct(e);
                                    return;
                                }

                                //auto i = map->find("puEd255");
                                auto i = map->find("");
                                if(i == map->end())
                                {
                                    funct(API_EINTERNAL);
                                    return;
                                }

                                user->puEd25519 = i->second;
                                user->rsaVerified = verifyRSAKeySignature_(user, sig);
                                LOG_info << "RSA key verifed: " << user->rsaVerified;
                                funct((user->rsaVerified) ? API_OK : API_EKEYVERFAIL);
                                return;
                            });
                }
            });
}

bool MegaClient::verifyRSAKeySignature_(User *user, SharedBuffer &sig) {
    std::string auth("keyauth");
    std::string sigStr = sig.str();
    int timeStampPos = sigStr.length() - crypto_sign_ed25519_BYTES;
    std::string timeStamp = sigStr.substr(0, timeStampPos);
    sigStr = sigStr.substr(timeStampPos);
    auth.append(timeStamp);
    auth.append(user->pubk.getPublicKeyBytes().str());

    return EdDSA::verifyDetatched((unsigned char*) sigStr.c_str(),
            (unsigned char*) auth.c_str(), auth.length(), user->puEd25519.get());
}

void MegaClient::fetchKeyrings(FetchKeyringsCallback callback) {
    if (!keysFetched) {
        char h[12];
        Base64::btoa((byte*) &me, sizeof me, h);
        std::string hand(h, 11);
        getgattribute(hand, "authRSA",
                [this, callback, hand](ValueMap map, error e) mutable
                {
                    if(e != API_OK && e != API_ENOENT)
                    {
                        callback(e);
                        return;
                    }
                    if(e == API_OK)
                    {
                        if(!deserilizeMap(map, 1))
                        {
                            callback(API_EINTERNAL);
                            return;
                        }
                    }

                    getgattribute(hand, "authring",
                            [this, callback](ValueMap map, error e) mutable
                            {

                                if(e != API_OK && e != API_ENOENT) {
                                    callback(e);
                                    return;
                                }
                                if(e == API_OK)
                                {
                                    if(!deserilizeMap(map, 0))
                                    {
                                        callback(API_EINTERNAL);
                                        return;
                                    }
                                }
                                keysFetched = true;
                                callback(API_OK);
                            });
                });
    } else {
        callback(API_OK);
    }
}

void MegaClient::verifyKeyFingerPrint(const char *user, SharedBuffer &key,
        int rsa, VerifyKeyCallback callback) {

    if (!keysFetched) {
        getgattribute(finduser(me)->email.c_str(), "authRSA",
                [this, user, key, rsa, callback](ValueMap map, error e) mutable
                {
                    if(e != API_OK && e != API_ENOENT)
                    {
                        callback(e);
                        return;
                    }
                    if(e == API_OK)
                    {
                        if(!deserilizeMap(map, 1)) {
                            LOG_err << "could not deserilize rsa map.";
                            callback(API_EINTERNAL);
                            return;
                        }
                    }

                    getgattribute(finduser(me)->email.c_str(), "authring",
                            [this, user, key, rsa, callback](ValueMap map, error e) mutable
                            {
                                if(e != API_OK && e != API_ENOENT)
                                {
                                    callback(e);
                                    return;
                                }
                                if(e == API_OK)
                                {
                                    if(!deserilizeMap(map, 0))
                                    {
                                        LOG_err << "could not deserilize ed map.";
                                        callback(API_EINTERNAL);
                                        return;
                                    }
                                }
                                keysFetched = true;
                                verifyKeyFingerPrint_(user, key, rsa, callback);
                            });
                });
    } else {
        verifyKeyFingerPrint_(user, key, rsa, callback);
    }
}

void MegaClient::verifyKeyFingerPrint_(const char *user, SharedBuffer &key,
        int rsa, VerifyKeyCallback callback) {
    User *userObj = finduser(user);
    if (!userObj) {
        restag = reqtag;
        app->verifykeyfp_result(API_EEXIST);
        return;
    }

    SharedBuffer keyFp = createFingerPrint(key);

    FingerPrintRecord record;
    if (rsa) {
        auto i = rsaKeyRing.find(userObj->userhandle);
        if (i != rsaKeyRing.end()) {
            record = i->second;
        }
    } else {
        auto i = edKeyRing.find(userObj->userhandle);
        if (i != edKeyRing.end()) {
            record = i->second;
        }
    }

    if (record.fingerPrint) {
        LOG_info << "testing key fingerprint from cache";
        bool check = record.verify(keyFp);
        LOG_debug << "key test: " << check;
        callback((check) ? API_OK : API_EKEYVERFAIL);
        return;
    }

    LOG_info << "storing fingerprint and updating keyring";

    if (rsa) {
        rsaKeyRing.insert( { userObj->userhandle, FingerPrintRecord(keyFp) });
    } else {
        edKeyRing.insert( { userObj->userhandle, FingerPrintRecord(keyFp) });
    }

    ValueMap sm = serilizeMap(rsa);
    setgattribute((rsa) ? "authRSA" : "authring", sm, 1, 1,
            [this, callback](error e)
            {
                callback(e);
            });
}

void MegaClient::getPublicStaticKey(const char *user) {
    User *u = finduser(user);
    if (!u) {
        app->getguattr_result(ValueMap(), API_EINTERNAL);
        return;
    }

    if (u->puEd25519) {
        LOG_info << "Using users cached ed25519 signing key";
        ValueMap map(new std::map<std::string, SharedBuffer>());
        map->insert( { "puEd255", u->puEd25519 });
        restag = reqtag;
        app->getguattr_result(map, API_OK);
        return;
    }

    int savedrt = reqtag;
    LOG_info << "Retrieving users ed25519 public signing key";
    getgattribute(user, "puEd255",
            [this, user, u, savedrt](ValueMap map, error e)
            {
                if(e != API_OK)
                {
                    LOG_err << "Error retrieving puEd255";
                    restag = savedrt;
                    app->getguattr_result(map, e);
                    return;
                }
                //auto k = map->find("puEd255");
                auto k = map->find("");
                if(k == map->end())
                {
                    LOG_err << "puEd255 record not found";
                    restag = savedrt;
                    app->getguattr_result(map, API_EINTERNAL);
                    return;
                }
                u->puEd25519 = k->second;

                verifyKeyFingerPrint(user, k->second, 0, [this, map, savedrt](error e)
                        {
                            if(e != API_OK)
                            {
                                LOG_err << "Users ed25519 signing key not verified";
                            }
                            restag = savedrt;
                            app->getguattr_result(map, e);
                        });
            });
}

ValueMap MegaClient::serilizeMap(int rsa) {
    ValueMap keyFpMap(new std::map<std::string, SharedBuffer>());
    std::map<handle, FingerPrintRecord> *fpMap;
    std::string mapName;
    if (rsa) {
        mapName.assign("authRSA");
        fpMap = &rsaKeyRing;
    } else {
        mapName.assign("authring");
        fpMap = &edKeyRing;
    }

    SharedBuffer serBuff(fpMap->size() * (sizeof(handle) + 20 + 1));
    int count = 0;
    for (auto &i : *fpMap) {
        memcpy(serBuff.get() + count, &i.first, sizeof(handle));
        memcpy(serBuff.get() + (count += sizeof(handle)),
                i.second.fingerPrint.get(), 20);
        memcpy(serBuff.get() + (count += 20), &i.second.methodConfidence, 1);
        LOG_info << "Saved record for " << i.first;
        count++;
    }

    keyFpMap->insert( { mapName, serBuff });
    return keyFpMap;
}

bool MegaClient::deserilizeMap(ValueMap map, int rsa) {
    auto i = map->find(
            (rsa) ? std::string("authRSA") : std::string("authring"));
    if (i == map->end()) {
        return false;
    }
    SharedBuffer buff = i->second;

    if ((buff.size % (sizeof(handle) + 20 + 1)) != 0) {
        return false;
    }

    std::map<handle, FingerPrintRecord> *rMap =
            (rsa) ? &rsaKeyRing : &edKeyRing;
    rMap->clear();
    int count = 0;
    while (count < buff.size) {
        SharedBuffer fp(20);
        FingerPrintRecord record(fp);
        handle h = 0;
        memcpy(&h, buff.get() + count, sizeof(handle));
        memcpy(record.fingerPrint.get(), buff.get() + (count += sizeof(handle)),
                20);
        memcpy(&record.methodConfidence, buff.get() + (count += 20), 1);
        rMap->insert( { h, record });
        count++;
    }

    return true;
}

std::pair<SecureBuffer, SecureBuffer> MegaClient::initstatickeys() {
#ifndef USE_SODIUM
    throw std::runtime_error("Not currently supported");
#else

    signkey.init();

    // Make the new key pair and their storage arrays.
    if (signkey.genKeySeed().get() == nullptr) {
        LOG_err << "Error generating an Ed25519 key seed.";
        return std::pair<SecureBuffer, SecureBuffer>(SecureBuffer(),
                SecureBuffer());
    }

    std::pair<SecureBuffer, SecureBuffer> kPair = signkey.getKeyPair();
    if (kPair.first.get() == nullptr || kPair.second.get() == nullptr) {
        return std::pair<SecureBuffer, SecureBuffer>(SecureBuffer(),
                SecureBuffer());
    }

    return kPair;
#endif
}

void MegaClient::getpubkey(const char *user) {
    queuepubkeyreq(finduser(user, 1), new PubKeyActionNotifyApp(reqtag));
}

// resume session - load state from local cache, if available
void MegaClient::login(const byte* session, int size) {
    locallogout();

    if (size == sizeof key.key + SIDLEN) {
        string t;

        key.setkey(session);
        setsid(session + sizeof key.key, size - sizeof key.key);

        opensctable();

        if (sctable && sctable->get(CACHEDSCSN, &t)
                && t.size() == sizeof cachedscsn) {
            cachedscsn = MemAccess::get<handle>(t.data());
        }

        reqs[r].add(new CommandLogin(this, NULL, UNDEF));
    } else {
        restag = reqtag;
        app->login_result(API_EARGS);
    }
}

int MegaClient::dumpsession(byte* session, int size) {
    if (loggedin() == NOTLOGGEDIN) {
        return 0;
    }

    if (size < (int) sid.size() + (int) sizeof key.key) {
        return API_ERANGE;
    }

    memcpy(session, key.key, sizeof key.key);
    memcpy(session + sizeof key.key, sid.data(), sid.size());

    return sizeof key.key + sid.size();
}

void MegaClient::killsession(handle session) {
    reqs[r].add(new CommandKillSessions(this, session));
}

// Kill all sessions (except current)
void MegaClient::killallsessions() {
    reqs[r].add(new CommandKillSessions(this));
}

void MegaClient::opensctable() {
    if (dbaccess && !sctable) {
        string dbname;

        dbname.resize((SIDLEN - sizeof key.key) * 4 / 3 + 3);
        dbname.resize(
                Base64::btoa((const byte*) sid.data() + sizeof key.key,
                        SIDLEN - sizeof key.key, (char*) dbname.c_str()));

        sctable = dbaccess->open(fsaccess, &dbname);
    }
}

// verify a static symmetric password challenge
int MegaClient::checktsid(byte* sidbuf, unsigned len) {
    if (len != SIDLEN) {
        return 0;
    }

    key.ecb_encrypt(sidbuf);

    return !memcmp(sidbuf, sidbuf + SIDLEN - SymmCipher::KEYLENGTH,
            SymmCipher::KEYLENGTH);
}

// locate user by e-mail address or ASCII handle
User* MegaClient::finduser(const char* uid, int add) {
    // null user for folder links?
    if (!uid || !*uid) {
        return NULL;
    }

    if (!strchr(uid, '@')) {
        // not an e-mail address: must be ASCII handle
        handle uh;

        if (Base64::atob(uid, (byte*) &uh, sizeof uh) == sizeof uh) {
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

    if (it == umindex.end()) {
        if (!add) {
            return NULL;
        }

        // add user by lowercase e-mail address
        u = &users[++userid];
        u->uid = nuid;
        Node::copystring(&u->email, uid);
        umindex[nuid] = userid;

        return u;
    } else {
        return &users[it->second];
    }
}

// locate user by binary handle
User* MegaClient::finduser(handle uh, int add) {
    if (!uh) {
        return NULL;
    }

    char uid1[12];
    Base64::btoa((byte*) &uh, sizeof uh, uid1);
    uid1[11] = 0;

    User* u;
    uh_map::iterator it = uhindex.find(uh);

    if (it == uhindex.end()) {
        if (!add) {
            return NULL;
        }

        // add user by binary handle
        u = &users[++userid];

        char uid[12];
        Base64::btoa((byte*) &uh, sizeof uh, uid);
        u->uid.assign(uid, 11);

        uhindex[uh] = userid;
        u->userhandle = uh;

        return u;
    } else {
        return &users[it->second];
    }
}

// add missing mapping (handle or email)
// reduce uid to ASCII uh if only known by email
void MegaClient::mapuser(handle uh, const char* email) {
    if (!*email) {
        return;
    }

    User* u;
    string nuid;

    Node::copystring(&nuid, email);
    transform(nuid.begin(), nuid.end(), nuid.begin(), ::tolower);

    // does user uh exist?
    uh_map::iterator hit = uhindex.find(uh);

    if (hit != uhindex.end()) {
        // yes: add email reference
        u = &users[hit->second];

        if (!u->email.size()) {
            Node::copystring(&u->email, email);
            umindex[nuid] = hit->second;
        }

        return;
    }

    // does user email exist?
    um_map::iterator mit = umindex.find(nuid);

    if (mit != umindex.end()) {
        // yes: add uh reference
        u = &users[mit->second];

        uhindex[uh] = mit->second;
        u->userhandle = uh;

        char uid[12];
        Base64::btoa((byte*) &uh, sizeof uh, uid);
        u->uid.assign(uid, 11);
    }
}

// sharekey distribution request - walk array consisting of {node,user+}+ handle tuples
// and submit public key requests
void MegaClient::procsr(JSON* j) {
    User* u;
    handle sh, uh;

    if (!j->enterarray()) {
        return;
    }

    while (j->ishandle() && (sh = j->gethandle())) {
        if (nodebyhandle(sh)) {
            // process pending requests
            while (j->ishandle(USERHANDLE) && (uh = j->gethandle(USERHANDLE))) {
                if ((u = finduser(uh))) {
                    queuepubkeyreq(u, new PubKeyActionSendShareKey(sh));
                }
            }
        } else {
            // unknown node: skip
            while (j->ishandle(USERHANDLE) && (uh = j->gethandle(USERHANDLE)))
                ;
        }
    }

    j->leavearray();
}

// process node tree (bottom up)
void MegaClient::proctree(Node* n, TreeProc* tp, bool skipinshares) {
    if (n->type != FILENODE) {
        for (node_list::iterator it = n->children.begin();
                it != n->children.end();) {
            Node *child = *it++;
            if (!(skipinshares && child->inshare)) {
                proctree(child, tp, skipinshares);
            }
        }
    }

    tp->proc(this, n);
}

// queue PubKeyAction request to be triggered upon availability of the user's
// public key
void MegaClient::queuepubkeyreq(User* u, PubKeyAction* pka) {
    if (!u || u->pubk.isvalid()) {
        restag = pka->tag;
        pka->proc(this, u);
        delete pka;
    } else {
        u->pkrs.push_back(pka);

        if (!u->pubkrequested) {
            reqs[r].add(new CommandPubKeyRequest(this, u));
        }
    }
}

// rewrite keys of foreign nodes due to loss of underlying shareufskey
void MegaClient::rewriteforeignkeys(Node* n) {
    TreeProcForeignKeys rewrite;
    proctree(n, &rewrite);

    if (nodekeyrewrite.size()) {
        reqs[r].add(new CommandNodeKeyUpdate(this, &nodekeyrewrite));
    }
}

// if user has a known public key, complete instantly
// otherwise, queue and request public key if not already pending
void MegaClient::setshare(Node* n, const char* user, accesslevel_t a) {
    if (a == ACCESS_UNKNOWN && n->outshares && n->outshares->size() == 1) {
        // rewrite keys of foreign nodes located in the outbound share that is getting canceled
        // FIXME: verify that it is really getting canceled to prevent benign premature rewrite
        rewriteforeignkeys(n);
    }

    queuepubkeyreq(finduser(user, 1),
            new PubKeyActionCreateShare(n->nodehandle, a, reqtag));
}

// enumerate Pro account purchase options (not fully implemented)
void MegaClient::purchase_enumeratequotaitems() {
    reqs[r].add(new CommandEnumerateQuotaItems(this));
}

// begin a new purchase (FIXME: not fully implemented)
void MegaClient::purchase_begin() {
    purchase_basket.clear();
}

// submit purchased product for payment
void MegaClient::purchase_additem(int itemclass, handle item, unsigned price,
        const char* currency, unsigned tax, const char* country,
        const char* affiliate) {
    reqs[r].add(
            new CommandPurchaseAddItem(this, itemclass, item, price, currency,
                    tax, country, affiliate));
}

// obtain payment URL for given provider
void MegaClient::purchase_checkout(int gateway) {
    reqs[r].add(new CommandPurchaseCheckout(this, gateway));
}

void MegaClient::submitpurchasereceipt(int type, const char *receipt) {
    reqs[r].add(new CommandSubmitPurchaseReceipt(this, type, receipt));
}

// add new contact (by e-mail address)
error MegaClient::invite(const char* email, visibility_t show) {
    if (!strchr(email, '@')) {
        return API_EARGS;
    }

    reqs[r].add(new CommandUserRequest(this, email, show));

    return API_OK;
}

/**
 * @brief Attach/update/delete a user attribute.
 *
 * Attributes are stored as base64-encoded binary blobs. They use internal
 * attribute name prefixes:
 *
 * "*" - Private and CBC-encrypted.
 * "+" - Public and plain text.
 *
 * @param an Attribute name.
 * @param av Attribute value.
 * @param avl Attribute value length.
 * @param priv 1 for a private, 0 for a public attribute, 2 for a default attribute
 * @return Void.
 */
void MegaClient::putua(const char* an, const byte* av, unsigned avl, int priv) {
    string name = priv ? ((priv == 1) ? "*" : "") : "+";
    string data;

    name.append(an);

    if (priv == 1) {
        if (av) {
            data.assign((const char*) av, avl);
        }

        string iv;

        PaddedCBC::encrypt(&data, &key, &iv);

        // Now prepend the data with the (8 byte) IV.
        iv.append(data);
        data = iv;
    } else if (!av) {
        av = (const byte*) "";
    }

    reqs[r].add(
            new CommandPutUA(this, name.c_str(),
                    (priv == 1) ? (const byte*) data.data() : av,
                    (priv == 1) ? data.size() : avl));
}

/**
 * @brief Queue a user attribute retrieval.
 *
 * @param u User.
 * @param an Attribute name.
 * @param p 1 for a private, 0 for a public attribute.
 * @return Void.
 */
void MegaClient::getua(User* u, const char* an, int p) {
    if (an) {
        string name = p ? "*" : "+";

        name.append(an);

        reqs[r].add(new CommandGetUA(this, u->uid.c_str(), name.c_str(), p));
    }
}

// queue node for notification
void MegaClient::notifynode(Node* n) {
    n->applykey();

#ifdef ENABLE_SYNC
    // is this a synced node that was moved to a non-synced location? queue for
    // deletion from LocalNodes.
    if (n->localnode && n->localnode->parent && n->parent && !n->parent->localnode)
    {
        if(n->changed.removed || n->changed.parent)
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
            n->parent->localnode->deleted = n->changed.removed;
            if (!n->changed.removed && n->changed.parent)
            {
                if(!n->localnode)
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

    if (!n->notified) {
        n->notified = true;
        nodenotify.push_back(n);
    }
}

// queue user for notification
void MegaClient::notifyuser(User* u) {
    usernotify.push_back(u);
}

// process request for share node keys
// builds & emits k/cr command
// returns 1 in case of a valid response, 0 otherwise
void MegaClient::proccr(JSON* j) {
    node_vector shares, nodes;
    handle h;

    if (j->enterobject()) {
        for (;;) {
            switch (j->getnameid()) {
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
                if (!j->storeobject()) {
                    return;
                }
            }
        }

        return;
    }

    if (!j->enterarray()) {
        LOG_err << "Malformed CR - outer array";
        return;
    }

    if (j->enterarray()) {
        while (!ISUNDEF(h = j->gethandle())) {
            shares.push_back(nodebyhandle(h));
        }

        j->leavearray();

        if (j->enterarray()) {
            while (!ISUNDEF(h = j->gethandle())) {
                nodes.push_back(nodebyhandle(h));
            }

            j->leavearray();
        } else {
            LOG_err << "Malformed SNK CR - nodes part";
            return;
        }

        if (j->enterarray()) {
            cr_response(&shares, &nodes, j);
            j->leavearray();
        } else {
            LOG_err << "Malformed CR - linkage part";
            return;
        }
    }

    j->leavearray();
}

// share nodekey delivery
void MegaClient::procsnk(JSON* j) {
    if (j->enterarray()) {
        handle sh, nh;

        while (j->enterarray()) {
            if (ISUNDEF((sh = j->gethandle()))) {
                return;
            }

            if (ISUNDEF((nh = j->gethandle()))) {
                return;
            }

            Node* sn = nodebyhandle(sh);

            if (sn && sn->sharekey && checkaccess(sn, OWNER)) {
                Node* n = nodebyhandle(nh);

                if (n && n->isbelow(sn)) {
                    byte keybuf[FILENODEKEYLENGTH];

                    sn->sharekey->ecb_encrypt((byte*) n->nodekey.data(), keybuf,
                            n->nodekey.size());

                    reqs[r].add(
                            new CommandSingleKeyCR(sh, nh, keybuf,
                                    n->nodekey.size()));
                }
            }

            j->leavearray();
        }

        j->leavearray();
    }
}

// share userkey delivery
void MegaClient::procsuk(JSON* j) {
    if (j->enterarray()) {
        while (j->enterarray()) {
            handle sh, uh;

            sh = j->gethandle();

            if (!ISUNDEF(sh)) {
                uh = j->gethandle();

                if (!ISUNDEF(uh)) {
                    // FIXME: add support for share user key delivery
                }
            }

            j->leavearray();
        }

        j->leavearray();
    }
}

// add node to vector, return position, deduplicate
unsigned MegaClient::addnode(node_vector* v, Node* n) const {
    // linear search not particularly scalable, but fine for the relatively
    // small real-world requests
    for (int i = v->size(); i--;) {
        if ((*v)[i] == n) {
            return i;
        }
    }

    v->push_back(n);
    return v->size() - 1;
}

// generate crypto key response
// if !selector, generate all shares*nodes tuples
void MegaClient::cr_response(node_vector* shares, node_vector* nodes,
        JSON* selector) {
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
    for (si = shares->size(); si--;) {
        if ((*shares)[si]
                && ((*shares)[si]->inshare || !(*shares)[si]->sharekey)) {
            LOG_warn
                    << "Attempt to obtain node key for invalid/third-party share foiled";
            (*shares)[si] = NULL;
        }
    }

    if (!selector) {
        si = 0;
        ni = 0;
    }

    crkeys.reserve(shares->size() * nodes->size() * (32 * 4 / 3 + 10));

    for (;;) {
        if (selector) {
            // walk selector, detect errors/end by checking if the JSON
            // position advanced
            const char* p = selector->pos;

            si = (unsigned) selector->getint();

            if (p == selector->pos) {
                break;
            }

            ni = (unsigned) selector->getint();

            if (si >= shares->size()) {
                LOG_err << "Share index out of range";
                return;
            }

            if (ni >= nodes->size()) {
                LOG_err << "Node index out of range";
                return;
            }

            if (selector->pos[1] == '"') {
                setkey = selector->storebinary(keybuf, sizeof keybuf);
            }
        } else {
            // no selector supplied
            ni++;

            if (ni >= nodes->size()) {
                ni = 0;
                if (++si >= shares->size()) {
                    break;
                }
            }
        }

        if ((sn = (*shares)[si]) && (n = (*nodes)[ni])) {
            if (n->isbelow(sn)) {
                if (setkey >= 0) {
                    if (setkey == (int) n->nodekey.size()) {
                        sn->sharekey->ecb_decrypt(keybuf, n->nodekey.size());
                        n->setkey(keybuf);
                        setkey = -1;
                    }
                } else {
                    unsigned nsi, nni;

                    nsi = addnode(&rshares, sn);
                    nni = addnode(&rnodes, n);

                    sprintf(buf, "\",%u,%u,\"", nsi, nni);

                    // generate & queue share nodekey
                    sn->sharekey->ecb_encrypt((byte*) n->nodekey.data(), keybuf,
                            n->nodekey.size());
                    Base64::btoa(keybuf, n->nodekey.size(), strchr(buf + 7, 0));
                    crkeys.append(buf);
                }
            } else {
                LOG_warn
                        << "Attempt to obtain key of node outside share foiled";
            }
        }
    }

    if (crkeys.size()) {
        crkeys.append("\"");
        reqs[r].add(
                new CommandKeyCR(this, &rshares, &rnodes, crkeys.c_str() + 2));
    }
}

void MegaClient::getaccountdetails(AccountDetails* ad, bool storage,
bool transfer, bool pro, bool transactions,
bool purchases, bool sessions) {
    reqs[r].add(new CommandGetUserQuota(this, ad, storage, transfer, pro));

    if (transactions) {
        reqs[r].add(new CommandGetUserTransactions(this, ad));
    }

    if (purchases) {
        reqs[r].add(new CommandGetUserPurchases(this, ad));
    }

    if (sessions) {
        reqs[r].add(new CommandGetUserSessions(this, ad));
    }
}

// export node link
error MegaClient::exportnode(Node* n, int del) {
    if (!checkaccess(n, OWNER)) {
        return API_EACCESS;
    }

    // exporting folder - create share
    if (n->type == FOLDERNODE) {
        setshare(n, NULL, del ? ACCESS_UNKNOWN : RDONLY);
    }

    // export node
    if (n->type == FOLDERNODE || n->type == FILENODE) {
        reqs[r].add(new CommandSetPH(this, n, del));
    } else {
        return API_EACCESS;
    }

    return API_OK;
}

// open exported file link
// formats supported: ...#!publichandle#key or publichandle#key
error MegaClient::openfilelink(const char* link, int op) {
    const char* ptr;
    handle ph = 0;
    byte key[FILENODEKEYLENGTH];

    if ((ptr = strstr(link, "#!"))) {
        ptr += 2;
    } else {
        ptr = link;
    }

    if (Base64::atob(ptr, (byte*) &ph, NODEHANDLE) == NODEHANDLE) {
        ptr += 8;

        if (*ptr++ == '!') {
            if (Base64::atob(ptr, key, sizeof key) == sizeof key) {
                if (op) {
                    reqs[r].add(new CommandGetPH(this, ph, key, op));
                } else {
                    reqs[r].add(new CommandGetFile(NULL, key, ph, false));
                }

                return API_OK;
            }
        }
    }

    return API_EARGS;
}

sessiontype_t MegaClient::loggedin() {
    if (ISUNDEF(me)) {
        return NOTLOGGEDIN;
    }

    User* u = finduser(me);

    if (u && !u->email.size()) {
        return EPHEMERALACCOUNT;
    }

    if (!asymkey.isvalid()) {
        return CONFIRMEDACCOUNT;
    }

    return FULLACCOUNT;
}

error MegaClient::changepw(const byte* oldpwkey, const byte* newpwkey) {
    User* u;

    if (!loggedin() || !(u = finduser(me))) {
        return API_EACCESS;
    }

    byte oldkey[SymmCipher::KEYLENGTH];
    byte newkey[SymmCipher::KEYLENGTH];

    SymmCipher pwcipher;

    memcpy(oldkey, key.key, sizeof oldkey);
    memcpy(newkey, oldkey, sizeof newkey);

    pwcipher.setkey(oldpwkey);
    pwcipher.ecb_encrypt(oldkey);

    pwcipher.setkey(newpwkey);
    pwcipher.ecb_encrypt(newkey);

    string email = u->email;

    reqs[r].add(
            new CommandSetMasterKey(this, oldkey, newkey,
                    stringhash64(&email, &pwcipher)));

    return API_OK;
}

// create ephemeral session
void MegaClient::createephemeral() {
    byte keybuf[SymmCipher::KEYLENGTH];
    byte pwbuf[SymmCipher::KEYLENGTH];
    byte sscbuf[2 * SymmCipher::KEYLENGTH];

    locallogout();

    PrnGen::genblock(keybuf, sizeof keybuf);
    PrnGen::genblock(pwbuf, sizeof pwbuf);
    PrnGen::genblock(sscbuf, sizeof sscbuf);

    key.setkey(keybuf);
    key.ecb_encrypt(sscbuf, sscbuf + SymmCipher::KEYLENGTH,
            SymmCipher::KEYLENGTH);

    key.setkey(pwbuf);
    key.ecb_encrypt(keybuf);

    reqs[r].add(new CommandCreateEphemeralSession(this, keybuf, pwbuf, sscbuf));
}

void MegaClient::resumeephemeral(handle uh, const byte* pw, int ctag) {
    reqs[r].add(
            new CommandResumeEphemeralSession(this, uh, pw,
                    ctag ? ctag : reqtag));
}

void MegaClient::sendsignuplink(const char* email, const char* name,
        const byte* pwhash) {
    SymmCipher pwcipher(pwhash);
    byte c[2 * SymmCipher::KEYLENGTH];

    memcpy(c, key.key, sizeof key.key);
    PrnGen::genblock(c + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH / 4);
    memset(c + SymmCipher::KEYLENGTH + SymmCipher::KEYLENGTH / 4, 0,
            SymmCipher::KEYLENGTH / 2);
    PrnGen::genblock(c + 2 * SymmCipher::KEYLENGTH - SymmCipher::KEYLENGTH / 4,
            SymmCipher::KEYLENGTH / 4);

    pwcipher.ecb_encrypt(c, c, sizeof c);

    reqs[r].add(new CommandSendSignupLink(this, email, name, c));
}

// if query is 0, actually confirm account; just decode/query signup link
// details otherwise
void MegaClient::querysignuplink(const byte* code, unsigned len) {
    reqs[r].add(new CommandQuerySignupLink(this, code, len));
}

void MegaClient::confirmsignuplink(const byte* code, unsigned len,
        uint64_t emailhash) {
    reqs[r].add(new CommandConfirmSignupLink(this, code, len, emailhash));
}

// generate and configure encrypted private key, plaintext public key
void MegaClient::setkeypair(CreateKeypairCallback callback) {
    CryptoPP::Integer pubk[AsymmCipher::PUBKEY];

    string privks, pubks;

    asymkey.genkeypair(asymkey.key, pubk, 2048);

    AsymmCipher::serializeintarray(pubk, AsymmCipher::PUBKEY, &pubks);
    AsymmCipher::serializeintarray(asymkey.key, AsymmCipher::PRIVKEY, &privks);

    // add random padding and ECB-encrypt with master key
    unsigned t = privks.size();

    privks.resize((t + SymmCipher::BLOCKSIZE - 1) & -SymmCipher::BLOCKSIZE);
    PrnGen::genblock((byte*) (privks.data() + t), privks.size() - t);

    key.ecb_encrypt((byte*) privks.data(), (byte*) privks.data(),
            (unsigned) privks.size());

    reqs[r].add(
            new CommandSetKeyPair(this, (const byte*) privks.data(),
                    privks.size(), (const byte*) pubks.data(), pubks.size(),
                    callback));
}

#ifdef USE_SODIUM
/**
 * @brief Initialises the Ed25519 EdDSA key user properties.
 *
 * A key pair will be added, if not present, yet.
 *
 * @return Error code (default: 1 on success).
 */
int MegaClient::inited25519() {
//    signkey.init();
//
//    // Make the new key pair and their storage arrays.
//    if (!signkey.genKeySeed())
//    {
//        LOG_err << "Error generating an Ed25519 key seed.";
//        return(0);
//    }
//
//    unsigned char* pubKey = (unsigned char*)malloc(crypto_sign_PUBLICKEYBYTES);
//
//    if (!signkey.publicKey(pubKey))
//    {
//        free(pubKey);
//        LOG_err << "Error deriving the Ed25519 public key.";
//        return(0);
//    }
//
//    // Store the key -pair to user attributes.
//    putua("prEd255", (const byte*)signkey.keySeed, crypto_sign_SEEDBYTES, 1);
//    putua("puEd255", (const byte*)pubKey, crypto_sign_PUBLICKEYBYTES, 0);
//    free(pubKey);
    return (1);
}
#endif

bool MegaClient::fetchsc(DbTable* sctable) {
    uint32_t id;
    string data;
    Node* n;
    User* u;
    node_vector dp;

    LOG_info << "Loading session from local cache";

    sctable->rewind();

    while (sctable->next(&id, &data, &key)) {
        switch (id & 15) {
        case CACHEDSCSN:
            if (data.size() != sizeof cachedscsn) {
                return false;
            }
            break;

        case CACHEDNODE:
            if ((n = Node::unserialize(this, &data, &dp))) {
                n->dbid = id;
            } else {
                LOG_err << "Failed - node record read error";
                return false;
            }
            break;

        case CACHEDUSER:
            if ((u = User::unserialize(this, &data))) {
                u->dbid = id;
            } else {
                LOG_err << "Failed - user record read error";
                return false;
            }
        }
    }

    // any child nodes arrived before their parents?
    for (int i = dp.size(); i--;) {
        if ((n = nodebyhandle(dp[i]->parenthandle))) {
            dp[i]->setparent(n);
        }
    }

    mergenewshares(0);

    return true;
}

void MegaClient::fetchnodes() {
    statecurrent = false;

    opensctable();

    if (sctable && cachedscsn == UNDEF) {
        sctable->truncate();
    }

    // only initial load from local cache
    if (loggedin() == FULLACCOUNT && !nodes.size() && sctable
            && !ISUNDEF(cachedscsn) && fetchsc(sctable)) {
        restag = reqtag;
#ifdef ENABLE_SYNC
        syncsup = false;
#endif
        app->fetchnodes_result(API_OK);
        app->nodes_updated(NULL, nodes.size());
        for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++) {
            memset(&(it->second->changed), 0, sizeof it->second->changed);
        }

        Base64::btoa((byte*) &cachedscsn, sizeof cachedscsn, scsn);
        LOG_info << "Session loaded from local cache. SCSN: " << scsn;
    } else if (!fetchingnodes) {
        fetchingnodes = true;

#ifdef ENABLE_SYNC
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            (*it)->changestate(SYNC_CANCELED);
        }
#endif

        reqs[r].add(new CommandFetchNodes(this));
    }
}

void MegaClient::purgenodesusersabortsc() {
    app->clearing();

    while (!hdrns.empty()) {
        delete hdrns.begin()->second;
    }

#ifdef ENABLE_SYNC
    for (sync_list::iterator it = syncs.begin(); it != syncs.end(); )
    {
        (*it)->changestate(SYNC_CANCELED);
        delete *(it++);
    }

    todebris.clear();
    tounlink.clear();

    syncs.clear();
#endif

    for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++) {
        delete it->second;
    }

    nodes.clear();

    for (fafc_map::iterator cit = fafcs.begin(); cit != fafcs.end(); cit++) {
        for (int i = 2; i--;) {
            for (faf_map::iterator it = cit->second->fafs[i].begin();
                    it != cit->second->fafs[i].end(); it++) {
                delete it->second;
            }

            cit->second->fafs[i].clear();
        }
    }

    for (newshare_list::iterator it = newshares.begin(); it != newshares.end();
            it++) {
        delete *it;
    }

    newshares.clear();

    nodenotify.clear();
    usernotify.clear();
    users.clear();
    uhindex.clear();
    umindex.clear();

    *scsn = 0;

    if (pendingsc) {
        app->request_response_progress(-1, -1);
        pendingsc->disconnect();
    }

    init();
}

// request direct read by node pointer
void MegaClient::pread(Node* n, m_off_t count, m_off_t offset, void* appdata) {
    queueread(n->nodehandle, true, n->nodecipher(),
            MemAccess::get<int64_t>(
                    (const char*) n->nodekey.data() + SymmCipher::KEYLENGTH),
            count, offset, appdata);
}

// request direct read by exported handle / key
void MegaClient::pread(handle ph, SymmCipher* key, int64_t ctriv, m_off_t count,
        m_off_t offset, void* appdata) {
    queueread(ph, false, key, ctriv, count, offset, appdata);
}

// since only the first six bytes of a handle are in use, we use the seventh to encode its type
void MegaClient::encodehandletype(handle* hp, bool p) {
    if (p) {
        ((char*) hp)[NODEHANDLE] = 1;
    }
}

bool MegaClient::isprivatehandle(handle* hp) {
    return ((char*) hp)[NODEHANDLE] != 0;
}

void MegaClient::queueread(handle h, bool p, SymmCipher* key, int64_t ctriv,
        m_off_t offset, m_off_t count, void* appdata) {
    handledrn_map::iterator it;

    encodehandletype(&h, p);

    it = hdrns.find(h);

    if (it == hdrns.end()) {
        // this handle is not being accessed yet: insert
        it = hdrns.insert(hdrns.end(),
                pair<handle, DirectReadNode*>(h,
                        new DirectReadNode(this, h, p, key, ctriv)));
        it->second->hdrn_it = it;
        it->second->enqueue(offset, count, reqtag, appdata);
        it->second->dispatch();
    } else {
        it->second->enqueue(offset, count, reqtag, appdata);
    }
}

// cancel direct read by node pointer / count / count
void MegaClient::preadabort(Node* n, m_off_t offset, m_off_t count) {
    abortreads(n->nodehandle, true, offset, count);
}

// cancel direct read by exported handle / offset / count
void MegaClient::preadabort(handle ph, m_off_t offset, m_off_t count) {
    abortreads(ph, false, offset, count);
}

void MegaClient::abortreads(handle h, bool p, m_off_t offset, m_off_t count) {
    handledrn_map::iterator it;
    DirectReadNode* drn;

    encodehandletype(&h, p);

    if ((it = hdrns.find(h)) != hdrns.end()) {
        drn = it->second;

        for (dr_list::iterator it = drn->reads.begin(); it != drn->reads.end();
                ) {
            if ((offset < 0 || offset == (*it)->offset)
                    && (count < 0 || count == (*it)->count)) {
                app->pread_failure(API_EINCOMPLETE, (*it)->drn->retries,
                        (*it)->appdata);

                delete *(it++);
            } else
                it++;
        }
    }
}

// execute pending directreads
bool MegaClient::execdirectreads() {
    bool r = false;
    DirectReadSlot* drs;

    if (drq.size() < MAXDRSLOTS) {
        // fill slots
        for (dr_list::iterator it = drq.begin(); it != drq.end(); it++) {
            if (!(*it)->drs) {
                drs = new DirectReadSlot(*it);
                (*it)->drs = drs;
                r = true;

                if (drq.size() >= MAXDRSLOTS)
                    break;
            }
        }
    }

    // perform slot I/O
    for (drs_list::iterator it = drss.begin(); it != drss.end();) {
        if ((*(it++))->doio() && !r)
            r = true;
    }

    while (!dsdrns.empty() && dsdrns.begin()->first <= Waiter::ds) {
        dsdrns.begin()->second->dispatch();
    }

    return r;
}

// recreate filenames of active PUT transfers
void MegaClient::updateputs() {
    for (transferslot_list::iterator it = tslots.begin(); it != tslots.end();
            it++) {
        if ((*it)->transfer->type == PUT && (*it)->transfer->files.size()) {
            (*it)->transfer->files.front()->prepare();
        }
    }
}

// check sync path, add sync if folder
// disallow nested syncs (there is only one LocalNode pointer per node), return
// EEXIST otherwise
// (FIXME: perform the same check for local paths!)
error MegaClient::addsync(string* rootpath, const char* debris,
        string* localdebris, Node* remotenode, fsfp_t fsfp, int tag) {
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
            }while ((n = n->parent));
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
    }while ((n = n->parent));

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
                        }while ((n = n->parent));
                    }
                }
            }
        }
    }

    if (rootpath->size() >= fsaccess->localseparator.size()
            && !memcmp(rootpath->data() + (rootpath->size() & -fsaccess->localseparator.size()) - fsaccess->localseparator.size(),
                    fsaccess->localseparator.data(),
                    fsaccess->localseparator.size()))
    {
        rootpath->resize((rootpath->size() & -fsaccess->localseparator.size()) - fsaccess->localseparator.size());
    }

    FileAccess* fa = fsaccess->newfileaccess();
    error e;

    if (fa->fopen(rootpath, true, false))
    {
        if (fa->type == FOLDERNODE)
        {
            string utf8path;
            fsaccess->local2path(rootpath, &utf8path);
            LOG_debug << "Adding sync: " << utf8path;

            Sync* sync = new Sync(this, rootpath, debris, localdebris, remotenode, fsfp, inshare, tag);

            if (sync->scan(rootpath, fa))
            {
                e = API_OK;
            }
            else
            {
                delete sync;
                e = API_ENOENT;
            }

            syncadded = true;
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
    attr_map::iterator ait;

    string localname;

    // build child hash - nameclash resolution: use newest/largest version
    for (node_list::iterator it = l->node->children.begin(); it != l->node->children.end(); it++)
    {
        // node must be syncable, alive, decrypted and have its name defined to
        // be considered - also, prevent clashes with the local debris folder
        if ((app->sync_syncable(*it)
                        && (*it)->syncdeleted == SYNCDEL_NONE
                        && !(*it)->attrstring
                        && (ait = (*it)->attrs.map.find('n')) != (*it)->attrs.map.end())
                && (l->parent || l->sync->debris != ait->second))
        {
            addchild(&nchildren, &ait->second, *it, &strings);
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
                    nchildren.erase(rit);
                }
                else if (*ll == *(FileFingerprint*)rit->second)
                {
                    // both files are identical
                    nchildren.erase(rit);
                }
                else
                {
                    rit->second->localnode = NULL;
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
        else if (rubbish && ll->deleted) // no corresponding remote node: delete local item
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
                    l->treestate();
                }
                else
                {
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
        if (app->sync_syncable(rit->second))
        {
            if ((ait = rit->second->attrs.map.find('n')) != rit->second->attrs.map.end())
            {
                size_t t = localpath->size();

                localname = ait->second;
                fsaccess->name2local(&localname);
                localpath->append(fsaccess->localseparator);
                localpath->append(localname);

                // does this node already have a corresponding LocalNode under
                // a different name or elsewhere in the filesystem?
                if (rit->second->localnode)
                {
                    if (rit->second->localnode->parent)
                    {
                        string curpath;

                        rit->second->localnode->getlocalpath(&curpath);
                        rit->second->localnode->treestate(TREESTATE_SYNCING);

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
                            success = false;
                        }
                    }
                }
                else
                {
                    // missing node is not associated with an existing LocalNode
                    if (rit->second->type == FILENODE)
                    {
                        // start fetching this node, unless fetch is already in progress
                        // FIXME: to cover renames that occur during the
                        // download, reconstruct localname in complete()
                        if (!rit->second->syncget)
                        {
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
                        // create local path, add to LocalNodes and recurse
                        if (fsaccess->mkdirlocal(localpath))
                        {
                            LocalNode* ll = l->sync->checkpath(l, localpath, &localname);

                            if (ll && ll != (LocalNode*)~0)
                            {
                                ll->setnode(rit->second);
                                ll->setnameparent(l, localpath);
                                ll->sync->statecacheadd(ll);

                                if (!syncdown(ll, localpath, rubbish) && success)
                                {
                                    success = false;
                                }
                            }
                        }
                        else if (success && fsaccess->transient_error)
                        {
                            success = false;
                        }
                    }
                }

                localpath->resize(t);
            }
        }
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
void MegaClient::syncup(LocalNode* l, dstime* nds)
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

                        reqtag = 0;

                        // report an "undecrypted child" event
                        reportevent("CU", report);

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

                        reqtag = 0;

                        // report a "no-name child" event
                        reportevent("CN");
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
            continue;
        }

        localname = *lit->first;
        fsaccess->local2name(&localname);
        if (!localname.size() || !ll->name.size())
        {
            if(!ll->reported)
            {
                ll->reported = true;

                char report[256];
                sprintf(report, "%d %d %d %d", (int)lit->first->size(), (int)localname.size(), (int)ll->name.size(), (int)ll->type);

                // report a "no-name localnode" event
                reqtag = 0;
                reportevent("LN", report);
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
                LOG_warn << "Type changed: " << localname;
                movetosyncdebris(rit->second, l->sync->inshare);
            }
            else
            {
                // file on both sides - do not overwrite if local version older or identical
                if (ll->type == FILENODE)
                {
                    // skip if this node is being fetched
                    if (rit->second->syncget)
                    {
                        continue;
                    }

                    // skip if remote file is newer
                    if (ll->mtime < rit->second->mtime)
                    {
                        continue;
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
                            ll->treestate(TREESTATE_SYNCED);
                            continue;
                        }
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
                    syncup(ll, nds);
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

            if (Waiter::ds < ll->nagleds)
            {
                if (ll->nagleds < *nds)
                {
                    *nds = ll->nagleds;
                }

                continue;
            }
            else
            {
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

                    delete fa;

                    ll->bumpnagleds();

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
                sprintf(report, "[%u %u %d] %d %d %d %" PRIi64, (int)nchildren.size(), (int)l->children.size(), !!l->node, ll->type, (int)ll->name.size(), (int)ll->mtime, ll->size);

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
                    sprintf(strchr(report, 0), " %d %d %d %" PRIi64 " ", ll->node->type, namelen, (int)ll->node->mtime, ll->node->size);
                    Base64::btoa((const byte *)&ll->node->nodehandle, MegaClient::NODEHANDLE, strchr(report, 0));

                }

                reqtag = 0;

                // report a "dupe" event
                reportevent("D", report);
            }
        }
        else
        {
            ll->created = true;

            // create remote folder or send file
            LOG_debug << "Adding local file to synccreate: " << ll->name;
            synccreate.push_back(ll);
            syncactivity = true;
        }

        if (ll->type == FOLDERNODE)
        {
            syncup(ll, nds);
        }
    }

    if (insync)
    {
        l->treestate(TREESTATE_SYNCED);
    }
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

            l->treestate(TREESTATE_PENDING);

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
                    // overwriting an existing remote node? send it to SyncDebris.
                    if (l->node)
                    {
                        movetosyncdebris(l->node, l->sync->inshare);
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

                reqs[r].add(new CommandPutNodes(this,
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
            reqtag = tn->tag;
            unlink(tn);
        }

        tn->tounlink_it = tounlink.end();
        tounlink.erase(tounlink.begin());
    }while (tounlink.size());
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

    // locate //bin/SyncDebris
    if ((n = childnodebyname(tn, SYNCDEBRISFOLDERNAME)))
    {
        tn = n;
        target = SYNCDEL_DEBRIS;

        // locate //bin/SyncDebris/yyyy-mm-dd
        if ((n = childnodebyname(tn, buf)))
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
                    reqtag = n->tag;
                    rename(n, tn, target, n->parent ? n->parent->nodehandle : UNDEF);
                    it++;
                }
                else
                {
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
        else if (n->syncdeleted == SYNCDEL_DEBRISDAY)
        {
            n->syncdeleted = SYNCDEL_NONE;
            n->todebris_it = todebris.end();
            todebris.erase(it++);
        }
        else
        {
            it++;
        }
    }

    if (target != SYNCDEL_DEBRISDAY && todebris.size() && !syncdebrisadding)
    {
        syncdebrisadding = true;

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

        reqs[r].add(new CommandPutNodes(this, tn->nodehandle, NULL, nn,
                        (target == SYNCDEL_DEBRIS) ? 1 : 2, 0,
                        PUTNODES_SYNCDEBRIS));
    }
}

// we cannot delete the Sync object directly, as it might have pending
// operations on it
void MegaClient::delsync(Sync* sync, bool deletecache)
{
    sync->changestate(SYNC_CANCELED);

    if(deletecache)
    {
        sync->statecachetable->remove();
    }

    syncactivity = true;
}

void MegaClient::putnodes_syncdebris_result(error e, NewNode* nn)
{
    delete[] nn;

    syncdebrisadding = false;
}
#endif

// inject file into transfer subsystem
// if file's fingerprint is not valid, it will be obtained from the local file
// (PUT) or the file's key (GET)
bool MegaClient::startxfer(direction_t d, File* f) {
    if (!f->transfer) {
        if (d == PUT) {
            if (!f->isvalid)    // (sync LocalNodes always have this set)
            {
                // missing FileFingerprint for local file - generate
                FileAccess* fa = fsaccess->newfileaccess();

                if (fa->fopen(&f->localname, d == PUT, d == GET)) {
                    f->genfingerprint(fa);
                }

                delete fa;
            }

            // if we are unable to obtain a valid file FileFingerprint, don't proceed
            if (!f->isvalid) {
                return false;
            }
        } else {
            if (!f->isvalid) {
                // no valid fingerprint: use filekey as its replacement
                memcpy(f->crc, f->filekey, sizeof f->crc);
            }
        }

        Transfer* t;
        transfer_map::iterator it = transfers[d].find(f);

        if (it != transfers[d].end()) {
            t = it->second;
        } else {
            t = new Transfer(this, d);
            *(FileFingerprint*) t = *(FileFingerprint*) f;
            t->size = f->size;
            t->tag = reqtag;
            t->transfers_it =
                    transfers[d].insert(
                            pair<FileFingerprint*, Transfer*>(
                                    (FileFingerprint*) t, t)).first;
            app->transfer_added(t);
        }

        f->file_it = t->files.insert(t->files.begin(), f);
        f->transfer = t;
    }

    return true;
}

// remove file from transfer subsystem
void MegaClient::stopxfer(File* f) {
    if (f->transfer) {
        Transfer *transfer = f->transfer;
        transfer->files.erase(f->file_it);
        f->transfer = NULL;
        f->terminated();

        // last file for this transfer removed? shut down transfer.
        if (!transfer->files.size()) {
            app->transfer_removed(transfer);
            delete transfer;
        }
    }
}

// pause/unpause transfers
void MegaClient::pausexfers(direction_t d, bool pause, bool hard) {
    xferpaused[d] = pause;

    if (!pause || hard) {
        WAIT_CLASS::bumpds();

        for (transferslot_list::iterator it = tslots.begin();
                it != tslots.end();) {
            if ((*it)->transfer->type == d) {
                if (pause) {
                    if (hard) {
                        (*it++)->disconnect();
                    }
                } else {
                    (*it)->lastdata = Waiter::ds;
                    (*it++)->doio(this);
                }
            } else {
                it++;
            }
        }
    }
}

Node* MegaClient::nodebyfingerprint(FileFingerprint* fingerprint) {
    fingerprint_set::iterator it;

    if ((it = fingerprints.find(fingerprint)) != fingerprints.end()) {
        return (Node*) *it;
    }

    return NULL;
}

// a chunk transfer request failed: record failed protocol & host
void MegaClient::setchunkfailed(string* url) {
    if (!chunkfailed && url->size() > 19) {
        chunkfailed = true;
        httpio->success = false;

        // record protocol and hostname
        if (badhosts.size()) {
            badhosts.append(",");
        }

        const char* ptr = url->c_str() + 4;

        if (*ptr == 's') {
            badhosts.append("S");
            ptr++;
        }

        badhosts.append(ptr + 6, 7);
    }
}

bool MegaClient::toggledebug() {
    SimpleLogger::setLogLevel(
            (SimpleLogger::logCurrentLevel >= logDebug) ?
                    logWarning : logDebug);
    return debugstate();
}

bool MegaClient::debugstate() {
    return SimpleLogger::logCurrentLevel >= logDebug;
}

void MegaClient::reportevent(const char* event, const char* details) {
    reqs[r].add(new CommandReportEvent(this, event, details));
}
} // namespace
