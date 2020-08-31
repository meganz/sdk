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
#include <cctype>
#include <algorithm>
#include <future>

#undef min // avoid issues with std::min and std::max
#undef max

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
string MegaClient::CHATSTATSURL = "https://stats.karere.mega.nz";

// maximum number of concurrent transfers (uploads + downloads)
const unsigned MegaClient::MAXTOTALTRANSFERS = 48;

// maximum number of concurrent transfers (uploads or downloads)
const unsigned MegaClient::MAXTRANSFERS = 32;

// maximum number of queued putfa before halting the upload queue
const int MegaClient::MAXQUEUEDFA = 30;

// maximum number of concurrent putfa
const int MegaClient::MAXPUTFA = 10;

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

// default number of seconds to wait after a bandwidth overquota
dstime MegaClient::USER_DATA_EXPIRATION_BACKOFF_SECS = 86400; // 1 day

// stats id
std::string MegaClient::statsid;

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

    sl = int(ptr - sk);

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
                        sendevent(99428,"Replacing share key", 0);
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
                                mNodeCounters[n->nodehandle] = n->subnodeCounts();
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
                if ((*it)->inshare && ((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN) && !checkaccess((*it)->localroot->node, FULL))
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

    size_t t = auth.size();
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

Node *MegaClient::getrootnode(Node *node)
{
    if (!node)
    {
        return NULL;
    }

    Node *n = node;
    while (n->parent)
    {
        n = n->parent;
    }
    return n;
}

bool MegaClient::isPrivateNode(handle h)
{
    Node *node = nodebyhandle(h);
    if (!node)
    {
        return false;
    }

    handle rootnode = getrootnode(node)->nodehandle;
    return (rootnode == rootnodes[0] || rootnode == rootnodes[1] || rootnode == rootnodes[2]);
}

bool MegaClient::isForeignNode(handle h)
{
    Node *node = nodebyhandle(h);
    if (!node)
    {
        return false;
    }

    handle rootnode = getrootnode(node)->nodehandle;
    return (rootnode != rootnodes[0] && rootnode != rootnodes[1] && rootnode != rootnodes[2]);
}

SCSN::SCSN()
{
    clear();
}

void SCSN::clear() 
{
    memset(scsn, 0, sizeof(scsn));
    stopsc = false;
}

// set server-client sequence number
bool SCSN::setScsn(JSON* j)
{
    handle t;

    if (j->storebinary((byte*)&t, sizeof t) != sizeof t)
    {
        return false;
    }

    setScsn(t);

    return true;
}

void SCSN::setScsn(handle h)
{
    Base64::btoa((byte*)&h, sizeof h, scsn);
}

void SCSN::stopScsn()
{
    memset(scsn, 0, sizeof(scsn));
    stopsc = true;
}

bool SCSN::ready() const
{
    return !stopsc && *scsn;
}

bool SCSN::stopped() const
{
    return stopsc;
}

const char* SCSN::text() const
{
    assert(ready());
    return scsn;
}

handle SCSN::getHandle() const
{
    assert(ready());
    handle t;
    Base64::atob(scsn, (byte*)&t, sizeof t);

    return t;
}

std::ostream& operator<<(std::ostream &os, const SCSN &scsn)
{
    os << scsn.text();
    return os;
}

SimpleLogger& operator<<(SimpleLogger &os, const SCSN &scsn)
{
    os << scsn.text();
    return os;
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
        fprintf(fp, "%8." PRIu32 "\t%s\n", it->first, it->second.c_str());
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

void MegaClient::confirmrecoverylink(const char *code, const char *email, const char *password, const byte *masterkeyptr, int accountversion)
{
    if (accountversion == 1)
    {
        byte pwkey[SymmCipher::KEYLENGTH];
        pw_key(password, pwkey);
        SymmCipher pwcipher(pwkey);

        string emailstr = email;
        uint64_t loginHash = stringhash64(&emailstr, &pwcipher);

        if (masterkeyptr)
        {
            // encrypt provided masterkey using the new password
            byte encryptedMasterKey[SymmCipher::KEYLENGTH];
            memcpy(encryptedMasterKey, masterkeyptr, sizeof encryptedMasterKey);
            pwcipher.ecb_encrypt(encryptedMasterKey);

            reqs.add(new CommandConfirmRecoveryLink(this, code, (byte*)&loginHash, sizeof(loginHash), NULL, encryptedMasterKey, NULL));
        }
        else
        {
            // create a new masterkey
            byte newmasterkey[SymmCipher::KEYLENGTH];
            rng.genblock(newmasterkey, sizeof newmasterkey);

            // generate a new session
            byte initialSession[2 * SymmCipher::KEYLENGTH];
            rng.genblock(initialSession, sizeof initialSession);
            key.setkey(newmasterkey);
            key.ecb_encrypt(initialSession, initialSession + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH);

            // and encrypt the master key to the new password
            pwcipher.ecb_encrypt(newmasterkey);

            reqs.add(new CommandConfirmRecoveryLink(this, code, (byte*)&loginHash, sizeof(loginHash), NULL, newmasterkey, initialSession));
        }
    }
    else
    {
        byte clientkey[SymmCipher::KEYLENGTH];
        rng.genblock(clientkey, sizeof(clientkey));

        string salt;
        HashSHA256 hasher;
        string buffer = "mega.nz";
        buffer.resize(200, 'P');
        buffer.append((char *)clientkey, sizeof(clientkey));
        hasher.add((const byte*)buffer.data(), unsigned(buffer.size()));
        hasher.get(&salt);

        byte derivedKey[2 * SymmCipher::KEYLENGTH];
        CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;
        pbkdf2.DeriveKey(derivedKey, sizeof(derivedKey), 0, (byte *)password, strlen(password),
                         (const byte *)salt.data(), salt.size(), 100000);

        string hashedauthkey;
        byte *authkey = derivedKey + SymmCipher::KEYLENGTH;
        hasher.add(authkey, SymmCipher::KEYLENGTH);
        hasher.get(&hashedauthkey);
        hashedauthkey.resize(SymmCipher::KEYLENGTH);

        SymmCipher cipher;
        cipher.setkey(derivedKey);

        if (masterkeyptr)
        {
            // encrypt provided masterkey using the new password
            byte encryptedMasterKey[SymmCipher::KEYLENGTH];
            memcpy(encryptedMasterKey, masterkeyptr, sizeof encryptedMasterKey);
            cipher.ecb_encrypt(encryptedMasterKey);
            reqs.add(new CommandConfirmRecoveryLink(this, code, (byte*)hashedauthkey.data(), SymmCipher::KEYLENGTH, clientkey, encryptedMasterKey, NULL));
        }
        else
        {
            // create a new masterkey
            byte newmasterkey[SymmCipher::KEYLENGTH];
            rng.genblock(newmasterkey, sizeof newmasterkey);

            // generate a new session
            byte initialSession[2 * SymmCipher::KEYLENGTH];
            rng.genblock(initialSession, sizeof initialSession);
            key.setkey(newmasterkey);
            key.ecb_encrypt(initialSession, initialSession + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH);

            // and encrypt the master key to the new password
            cipher.ecb_encrypt(newmasterkey);
            reqs.add(new CommandConfirmRecoveryLink(this, code, (byte*)hashedauthkey.data(), SymmCipher::KEYLENGTH, clientkey, newmasterkey, initialSession));
        }
    }
}

void MegaClient::getcancellink(const char *email, const char *pin)
{
    reqs.add(new CommandGetRecoveryLink(this, email, CANCEL_ACCOUNT, pin));
}

void MegaClient::confirmcancellink(const char *code)
{
    reqs.add(new CommandConfirmCancelLink(this, code));
}

void MegaClient::getemaillink(const char *email, const char *pin)
{
    reqs.add(new CommandGetEmailLink(this, email, 1, pin));
}

void MegaClient::confirmemaillink(const char *code, const char *email, const byte *pwkey)
{
    if (pwkey)
    {
        SymmCipher pwcipher(pwkey);
        string emailstr = email;
        uint64_t loginHash = stringhash64(&emailstr, &pwcipher);
        reqs.add(new CommandConfirmEmailLink(this, code, email, (const byte*)&loginHash, true));
    }
    else
    {
        reqs.add(new CommandConfirmEmailLink(this, code, email, NULL, true));
    }
}

void MegaClient::contactlinkcreate(bool renew)
{
    reqs.add(new CommandContactLinkCreate(this, renew));
}

void MegaClient::contactlinkquery(handle h)
{
    reqs.add(new CommandContactLinkQuery(this, h));
}

void MegaClient::contactlinkdelete(handle h)
{
    reqs.add(new CommandContactLinkDelete(this, h));
}

void MegaClient::multifactorauthsetup(const char *pin)
{
    reqs.add(new CommandMultiFactorAuthSetup(this, pin));
}

void MegaClient::multifactorauthcheck(const char *email)
{
    reqs.add(new CommandMultiFactorAuthCheck(this, email));
}

void MegaClient::multifactorauthdisable(const char *pin)
{
    reqs.add(new CommandMultiFactorAuthDisable(this, pin));
}

void MegaClient::fetchtimezone()
{
    string timeoffset;
    m_time_t rawtime = m_time(NULL);
    if (rawtime != -1)
    {
        struct tm lt, ut, it;
        memset(&lt, 0, sizeof(struct tm));
        memset(&ut, 0, sizeof(struct tm));
        memset(&it, 0, sizeof(struct tm));
        m_localtime(rawtime, &lt);
        m_gmtime(rawtime, &ut);
        if (memcmp(&ut, &it, sizeof(struct tm)) && memcmp(&lt, &it, sizeof(struct tm)))
        {
            m_time_t local_time = m_mktime(&lt);
            m_time_t utc_time = m_mktime(&ut);
            if (local_time != -1 && utc_time != -1)
            {
                double foffset = difftime(local_time, utc_time);
                int offset = int(fabs(foffset));
                if (offset <= 43200)
                {
                    ostringstream oss;
                    oss << ((foffset >= 0) ? "+" : "-");
                    oss << (offset / 3600) << ":";
                    int minutes = ((offset % 3600) / 60);
                    if (minutes < 10)
                    {
                        oss << "0";
                    }
                    oss << minutes;
                    timeoffset = oss.str();
                }
            }
        }
    }

    reqs.add(new CommandFetchTimeZone(this, "", timeoffset.c_str()));
}

void MegaClient::keepmealive(int type, bool enable)
{
    reqs.add(new CommandKeepMeAlive(this, type, enable));
}

void MegaClient::getpsa()
{
    reqs.add(new CommandGetPSA(this));
}

void MegaClient::acknowledgeuseralerts()
{
    useralerts.acknowledgeAll();
}

void MegaClient::activateoverquota(dstime timeleft, bool isPaywall)
{
    if (timeleft)
    {
        assert(!isPaywall);
        LOG_warn << "Bandwidth overquota for " << timeleft << " seconds";
        overquotauntil = Waiter::ds + timeleft;

        for (transfer_map::iterator it = transfers[GET].begin(); it != transfers[GET].end(); it++)
        {
            Transfer *t = it->second;
            t->bt.backoff(timeleft);
            if (t->slot && (t->state != TRANSFERSTATE_RETRYING
                            || !t->slot->retrying
                            || t->slot->retrybt.nextset() != overquotauntil))
            {
                t->state = TRANSFERSTATE_RETRYING;
                t->slot->retrybt.backoff(timeleft);
                t->slot->retrying = true;
                app->transfer_failed(t, API_EOVERQUOTA, timeleft);
                ++performanceStats.transferTempErrors;
            }
        }
    }
    else if (setstoragestatus(isPaywall ? STORAGE_PAYWALL : STORAGE_RED))
    {
        LOG_warn << "Storage overquota";
        int start = (isPaywall) ? GET : PUT;  // in Paywall state, none DLs/UPs can progress
        for (int d = start; d <= PUT; d += PUT - GET)
        {
            for (transfer_map::iterator it = transfers[d].begin(); it != transfers[d].end(); it++)
            {
                Transfer *t = it->second;
                t->bt.backoff(NEVER);
                if (t->slot)
                {
                    t->state = TRANSFERSTATE_RETRYING;
                    t->slot->retrybt.backoff(NEVER);
                    t->slot->retrying = true;
                    app->transfer_failed(t, isPaywall ? API_EPAYWALL : API_EOVERQUOTA, 0);
                    ++performanceStats.transferTempErrors;
                }
            }
        }
    }
    looprequested = true;
}

std::string MegaClient::getDeviceid() const
{
    if (MegaClient::statsid.empty())
    {
        fsaccess->statsid(&MegaClient::statsid);
    }

    return MegaClient::statsid;
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

// returns all the matching child nodes by UTF-8 name
vector<Node*> MegaClient::childnodesbyname(Node* p, const char* name, bool skipfolders)
{
    string nname = name;
    vector<Node*> found;

    if (!p || p->type == FILENODE)
    {
        return found;
    }

    fsaccess->normalize(&nname);

    for (node_list::iterator it = p->children.begin(); it != p->children.end(); it++)
    {
        if (nname == (*it)->displayname())
        {
            if ((*it)->type == FILENODE || !skipfolders)
            {
                found.push_back(*it);
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
    mAppliedKeyNodeCount = 0;
    faretrying = false;

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
    syncsup = true;
    syncdownrequired = false;
    syncuprequired = false;

    if (syncscanstate)
    {
        app->syncupdate_scanning(false);
        syncscanstate = false;
    }

    resetSyncConfigs();
#endif

    for (int i = sizeof rootnodes / sizeof *rootnodes; i--; )
    {
        rootnodes[i] = UNDEF;
    }

    pendingsc.reset();
    pendingscUserAlerts.reset();
    mBlocked = false;
    pendingcs_serverBusySent = false;

    btcs.reset();
    btsc.reset();
    btpfa.reset();
    btbadhost.reset();

    abortlockrequest();
    transferHttpCounter = 0;

    jsonsc.pos = NULL;
    insca = false;
    insca_notlast = false;
    scnotifyurl.clear();
    scsn.clear();

    notifyStorageChangeOnStateCurrent = false;
    mNotifiedSumSize = 0;
    mNodeCounters = NodeCounterMap();
    mOptimizePurgeNodes = false;
}

MegaClient::MegaClient(MegaApp* a, Waiter* w, HttpIO* h, FileSystemAccess* f, DbAccess* d, GfxProc* g, const char* k, const char* u, unsigned workerThreadCount)
    : useralerts(*this), btugexpiration(rng), btcs(rng), btbadhost(rng), btworkinglock(rng), btsc(rng), btpfa(rng)
#ifdef ENABLE_SYNC
    ,syncfslockretrybt(rng), syncdownbt(rng), syncnaglebt(rng), syncextrabt(rng), syncscanbt(rng)
#endif
    , mAsyncQueue(*w, workerThreadCount)
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
    scpaused = false;
    asyncfopens = 0;
    achievements_enabled = false;
    isNewSession = false;
    tsLogin = 0;
    versions_disabled = false;
    accountsince = 0;
    accountversion = 0;
    gmfa_enabled = false;
    gfxdisabled = false;
    ssrs_enabled = false;
    nsr_enabled = false;
    aplvp_enabled = false;
    mSmsVerificationState = SMS_STATE_UNKNOWN;
    loggingout = 0;
    loggedout = false;
    cachedug = false;
    minstreamingrate = -1;
    ephemeralSession = false;

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

    xferpaused[PUT] = false;
    xferpaused[GET] = false;
    putmbpscap = 0;
    mBizGracePeriodTs = 0;
    mBizExpirationTs = 0;
    mBizMode = BIZ_MODE_UNKNOWN;
    mBizStatus = BIZ_STATUS_UNKNOWN;

    overquotauntil = 0;
    ststatus = STORAGE_UNKNOWN;
    mOverquotaDeadlineTs = 0;
    looprequested = false;

    mFetchingAuthrings = false;
    fetchingkeys = false;
    signkey = NULL;
    chatkey = NULL;

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
        sessionid[i] = static_cast<char>('a' + rng.genuint32(26));
    }

    // initialize random API request sequence ID (server API is idempotent)
    for (i = sizeof reqid; i--; )
    {
        reqid[i] = static_cast<char>('a' + rng.genuint32(26));
    }

    nextuh = 0;  
    reqtag = 0;

    badhostcs = NULL;

    scsn.clear();
    cachedscsn = UNDEF;

    snprintf(appkey, sizeof appkey, "&ak=%s", k);

    // initialize useragent
    useragent = u;

    useragent.append(" (");
    fsaccess->osversion(&useragent, true);

    useragent.append(") MegaClient/" TOSTRING(MEGA_MAJOR_VERSION)
                     "." TOSTRING(MEGA_MINOR_VERSION)
                     "." TOSTRING(MEGA_MICRO_VERSION));
    useragent += sizeof(char*) == 8 ? "/64" : (sizeof(char*) == 4 ? "/32" : "");

    LOG_debug << "User-Agent: " << useragent;
    LOG_debug << "Cryptopp version: " << CRYPTOPP_VERSION;

    h->setuseragent(&useragent);
    h->setmaxdownloadspeed(0);
    h->setmaxuploadspeed(0);
}

MegaClient::~MegaClient()
{
    destructorRunning = true;
    locallogout(false);

    delete pendingcs;
    delete badhostcs;
    delete sctable;
    delete tctable;
    delete dbaccess;
}

#ifdef ENABLE_SYNC
void MegaClient::resetSyncConfigs()
{
    syncConfigs.reset();
    if (dbaccess && !uid.empty())
    {
        syncConfigs.reset(new SyncConfigBag{*dbaccess, *fsaccess, rng, uid});
    }
}
#endif

std::string MegaClient::getPublicLink(bool newLinkFormat, nodetype_t type, handle ph, const char *key)
{
    string strlink = "https://mega.nz/";
    string nodeType;
    if (newLinkFormat)
    {
        nodeType = (type == FOLDERNODE ?  "folder/" : "file/");
    }
    else
    {
        nodeType = (type == FOLDERNODE ? "#F!" : "#!");
    }

    strlink += nodeType;

    Base64Str<MegaClient::NODEHANDLE> base64ph(ph);
    strlink += base64ph;
    strlink += (newLinkFormat ? "#" : "");

    if (key)
    {
        strlink += (newLinkFormat ? "" : "!");
        strlink += key;
    }

    return strlink;
}

// nonblocking state machine executing all operations currently in progress
void MegaClient::exec()
{
    CodeCounter::ScopeTimer ccst(performanceStats.execFunction);

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
            sendevent(99427, "Timeout (server idle)", 0);

            disconnect();
        }
    }
    else if (pendingcs && EVER(pendingcs->lastdata) && !requestLock && !fetchingnodes
            &&  Waiter::ds >= pendingcs->lastdata + HttpIO::REQUESTTIMEOUT)
    {
        LOG_debug << clientname << "Request timeout. Triggering a lock request";
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

        if (cachedug && btugexpiration.armed())
        {
            LOG_debug << "Cached user data expired";
            getuserdata();
            fetchtimezone();
        }

        if (pendinghttp.size())
        {
            pendinghttp_map::iterator it = pendinghttp.begin();
            while (it != pendinghttp.end())
            {
                GenericHttpReq *req = it->second;
                switch (static_cast<reqstatus_t>(req->status))
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
                                     int(req->buf ? req->bufpos : req->in.size()));
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

                switch (static_cast<reqstatus_t>(fa->status))
                {
                    case REQ_SUCCESS:
                        if (fa->in.size() == sizeof(handle))
                        {
                            LOG_debug << "File attribute uploaded OK - " << fa->th;

                            // successfully wrote file attribute - store handle &
                            // remove from list
                            handle fah = MemAccess::get<handle>(fa->in.data());

                            if (fa->th == UNDEF)
                            {
                                // client app requested the upload without a node yet, and it will use the fa handle
                                app->putfa_result(fah, fa->type, API_OK);
                            }
                            else
                            {
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
                                    reqs.add(new CommandAttachFA(this, n->nodehandle, fa->type, fah, fa->tag));
                                }
                                else
                                {
                                    pendingfa[pair<handle, fatype>(fa->th, fa->type)] = pair<handle, int>(fah, fa->tag);
                                    LOG_debug << "Queueing pending file attribute. Total: " << pendingfa.size();
                                    checkfacompletion(fa->th);
                                }
                            }
                        }
                        else
                        {
                            LOG_warn << "Error attaching attribute";
                            Transfer *transfer = NULL;
                            handletransfer_map::iterator htit = faputcompletion.find(fa->th);
                            if (htit != faputcompletion.end())
                            {
                                // the failed attribute belongs to a pending upload
                                transfer = htit->second;
                            }
                            else
                            {
                                // check if the failed attribute belongs to an active upload
                                for (transfer_map::iterator it = transfers[PUT].begin(); it != transfers[PUT].end(); it++)
                                {
                                    if (it->second->uploadhandle == fa->th)
                                    {
                                        transfer = it->second;
                                        break;
                                    }
                                }
                            }

                            if (transfer)
                            {
                                // reduce the number of required attributes to let the upload continue
                                transfer->minfa--;
                                checkfacompletion(fa->th);
                                sendevent(99407,"Attribute attach failed during active upload", 0);
                            }
                            else
                            {
                                LOG_debug << "Transfer related to failed attribute not found: " << fa->th;
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
                switch (static_cast<reqstatus_t>(fc->req.status))
                {
                    case REQ_SUCCESS:
                        if (fc->req.contenttype.find("text/html") != string::npos
                            && !memcmp(fc->req.posturl.c_str(), "http:", 5))
                        {
                            LOG_warn << "Invalid Content-Type detected downloading file attr: " << fc->req.contenttype;
                            fc->urltime = 0;
                            usehttps = true;
                            app->notify_change_to_https();

                            sendevent(99436, "Automatic change to HTTPS", 0);
                        }
                        else
                        {
                            fc->parse(cit->first, true);
                        }

                        // notify app in case some attributes were not returned, then redispatch
                        fc->failed();
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
                            fc->parse(cit->first, false);
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

                            sendevent(99436, "Automatic change to HTTPS", 0);
                        }

                        fc->failed();
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
                        fc->dispatch();
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
                // handle retry reason for requests
                retryreason_t reason = RETRY_NONE;

                if (pendingcs->status == REQ_SUCCESS || pendingcs->status == REQ_FAILURE)
                {
                    performanceStats.csRequestWaitTime.stop();
                }

                switch (static_cast<reqstatus_t>(pendingcs->status))
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
                                    app->notify_retry(0, RETRY_NONE);
                                    csretrying = false;
                                }

                                // request succeeded, process result array
                                reqs.serverresponse(std::move(pendingcs->in), this);

                                WAIT_CLASS::bumpds();

                                delete pendingcs;
                                pendingcs = NULL;

                                notifypurge();
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

                                if (loggedout)
                                {
                                    locallogout(true);
                                    app->logout_result(API_OK);
                                }
                            }
                            else
                            {
                                // request failed
                                JSON json;
                                json.pos = pendingcs->in.c_str();
                                std::string requestError;
                                error e;
                                bool valid = json.storeobject(&requestError);
                                if (valid)
                                {
                                    if (strncmp(requestError.c_str(), "{\"err\":", 7) == 0)
                                    {
                                        e = (error)atoi(requestError.c_str() + 7);
                                    }
                                    else
                                    {
                                        e = (error)atoi(requestError.c_str());
                                    }
                                }
                                else
                                {
                                    e = API_EINTERNAL;
                                    requestError = std::to_string(e);
                                }

                                if (!e)
                                {
                                    e = API_EINTERNAL;
                                    requestError = std::to_string(e);
                                }

                                if (e == API_EBLOCKED && sid.size())
                                {
                                    block();
                                }

                                app->request_error(e);
                                delete pendingcs;
                                pendingcs = NULL;
                                csretrying = false;

                                reqs.servererror(requestError, this);
                                break;
                            }

                            btcs.reset();
                            break;
                        }
                        else
                        {
                            if (pendingcs->in == "-3")
                            {
                                reason = RETRY_API_LOCK;
                            }
                            else
                            {
                                reason = RETRY_RATE_LIMIT;
                            }
                            if (fetchingnodes)
                            {
                                fnstats.eAgainCount++;
                            }
                        }

                    // fall through
                    case REQ_FAILURE:
                        if (!reason && pendingcs->httpstatus != 200)
                        {
                            if (pendingcs->httpstatus == 500)
                            {
                                reason = RETRY_SERVERS_BUSY;
                            }
                            else if (pendingcs->httpstatus == 0)
                            {
                                reason = RETRY_CONNECTIVITY;
                            }
                            else
                            {
                                reason = RETRY_UNKNOWN;
                            }
                        }

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
                            sendevent(99453, "Invalid public key");
                            sslfakeissuer = pendingcs->sslfakeissuer;
                            app->request_error(API_ESSL);
                            sslfakeissuer.clear();

                            if (!retryessl)
                            {
                                delete pendingcs;
                                pendingcs = NULL;
                                csretrying = false;

                                reqs.servererror(std::to_string(API_ESSL), this);
                                break;
                            }
                        }

                        // failure, repeat with capped exponential backoff
                        app->request_response_progress(pendingcs->bufpos, -1);

                        delete pendingcs;
                        pendingcs = NULL;

                        btcs.backoff();
                        app->notify_retry(btcs.retryin(), reason);
                        csretrying = true;

                        reqs.requeuerequest();

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
                if (reqs.cmdspending())
                {
                    abortlockrequest();
                    pendingcs = new HttpReq();
                    pendingcs->protect = true;
                    pendingcs->logname = clientname + "cs ";
                    pendingcs_serverBusySent = false;

                    bool suppressSID = true;
                    reqs.serverrequest(pendingcs->out, suppressSID, pendingcs->includesFetchingNodes);

                    pendingcs->posturl = APIURL;

                    pendingcs->posturl.append("cs?id=");
                    pendingcs->posturl.append(reqid, sizeof reqid);
                    if (!suppressSID)
                    {
                        pendingcs->posturl.append(auth);
                    }
                    pendingcs->posturl.append(appkey);

                    string version = "v=2";
                    pendingcs->posturl.append("&" + version);
                    if (lang.size())
                    {
                        pendingcs->posturl.append("&");
                        pendingcs->posturl.append(lang);
                    }
                    pendingcs->type = REQ_JSON;

                    performanceStats.csRequestWaitTime.start();
                    pendingcs->post(this);
                    continue;
                }
                else
                {
                    btcs.reset();
                }
            }
            break;
        }

        // handle the request for the last 50 UserAlerts
        if (pendingscUserAlerts)
        {
            switch (static_cast<reqstatus_t>(pendingscUserAlerts->status))
            {
            case REQ_SUCCESS:
                if (*pendingscUserAlerts->in.c_str() == '{')
                {
                    JSON json;
                    json.begin(pendingscUserAlerts->in.c_str());
                    json.enterobject();
                    if (useralerts.procsc_useralert(json))
                    {
                        // NULL vector: "notify all elements"
                        app->useralerts_updated(NULL, int(useralerts.alerts.size()));
                    }
                    pendingscUserAlerts.reset();
                    break;
                }

                // fall through
            case REQ_FAILURE:
                if (pendingscUserAlerts->httpstatus == 200)
                {
                    error e = (error)atoi(pendingscUserAlerts->in.c_str());  
                    if (e == API_EAGAIN || e == API_ERATELIMIT)
                    {
                        btsc.backoff();
                        pendingscUserAlerts.reset();
                        LOG_warn << "Backing off before retrying useralerts request: " << btsc.retryin();
                        break;
                    }
                    LOG_err << "Unexpected sc response: " << pendingscUserAlerts->in;
                }
                LOG_err << "Useralerts request failed, continuing without them";
                if (useralerts.begincatchup)
                {
                    useralerts.begincatchup = false;
                    useralerts.catchupdone = true;
                }
                pendingscUserAlerts.reset();
                break;

            default:
                break;
            }
        }

        // handle API server-client requests
        if (!jsonsc.pos && !pendingscUserAlerts && pendingsc && !loggingout)
        {
            switch (static_cast<reqstatus_t>(pendingsc->status))
            {
            case REQ_SUCCESS:
                pendingscTimedOut = false;
                if (pendingsc->contentlength == 1
                        && pendingsc->in.size()
                        && pendingsc->in[0] == '0')
                {
                    LOG_debug << "SC keep-alive received";
                    pendingsc.reset();
                    btsc.reset();
                    break;
                }

                if (*pendingsc->in.c_str() == '{')
                {
                    insca = false;
                    insca_notlast = false;
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
                        scsn.stopScsn();
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
                    else if (e == API_EBLOCKED)
                    {
                        app->request_error(API_EBLOCKED);
                        block(true);
                    }
                    else
                    {
                        LOG_err << "Unexpected sc response: " << pendingsc->in;
                        scsn.stopScsn();
                    }
                }

                // fall through
            case REQ_FAILURE:
                pendingscTimedOut = false;
                if (pendingsc)
                {
                    if (!statecurrent && pendingsc->httpstatus != 200)
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

                    if (pendingsc->sslcheckfailed)
                    {
                        sendevent(99453, "Invalid public key");
                        sslfakeissuer = pendingsc->sslfakeissuer;
                        app->request_error(API_ESSL);
                        sslfakeissuer.clear();

                        if (!retryessl)
                        {
                            scsn.stopScsn();
                        }
                    }

                    pendingsc.reset();
                }

                if (scsn.stopped())
                {
                    btsc.backoff(NEVER);
                }
                else
                {
                    // failure, repeat with capped exponential backoff
                    btsc.backoff();
                }
                break;

            case REQ_INFLIGHT:
                if (!pendingscTimedOut && Waiter::ds >= (pendingsc->lastdata + HttpIO::SCREQUESTTIMEOUT))
                {
                    LOG_debug << "sc timeout expired";
                    // In almost all cases the server won't take more than SCREQUESTTIMEOUT seconds.  But if it does, break the cycle of endless requests for the same thing
                    pendingscTimedOut = true;
                    pendingsc.reset();
                    btsc.reset();
                }
                break;
            default:
                break;
            }
        }

#ifdef ENABLE_SYNC
        if (syncactivity)
        {
            syncops = true;
        }
        syncactivity = false;

        if (scsn.stopped() || mBlocked || scpaused || !statecurrent || !syncsup)
        {
            LOG_verbose << " Megaclient exec is pending resolutions."
                        << " scpaused=" << scpaused
                        << " stopsc=" << scsn.stopped()
                        << " mBlocked=" << mBlocked
                        << " jsonsc.pos=" << jsonsc.pos
                        << " syncsup=" << syncsup
                        << " statecurrent=" << statecurrent
                        << " syncadding=" << syncadding
                        << " syncactivity=" << syncactivity
                        << " syncdownrequired=" << syncdownrequired
                        << " syncdownretry=" << syncdownretry;
        }

        // do not process the SC result until all preconfigured syncs are up and running
        // except if SC packets are required to complete a fetchnodes
        if (!scpaused && jsonsc.pos && (syncsup || !statecurrent) && !syncdownrequired && !syncdownretry)
#else
        if (!scpaused && jsonsc.pos)
#endif
        {
            // FIXME: reload in case of bad JSON
            bool r = procsc();

            if (r)
            {
                // completed - initiate next SC request
                pendingsc.reset();
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

        if (!pendingsc && !pendingscUserAlerts && scsn.ready() && btsc.armed() && !mBlocked)
        {
            if (useralerts.begincatchup)
            {
                assert(!fetchingnodes);
                pendingscUserAlerts.reset(new HttpReq());
                pendingscUserAlerts->logname = clientname + "sc50 ";
                pendingscUserAlerts->protect = true;
                pendingscUserAlerts->posturl = APIURL;
                pendingscUserAlerts->posturl.append("sc");  // notifications/useralerts on sc rather than wsc, no timeout
                pendingscUserAlerts->posturl.append("?c=50");
                pendingscUserAlerts->posturl.append(auth);
                pendingscUserAlerts->type = REQ_JSON;
                pendingscUserAlerts->post(this);
            }
            else
            {
                pendingsc.reset(new HttpReq());
                pendingsc->logname = clientname + "sc ";
                if (scnotifyurl.size())
                {
                    pendingsc->posturl = scnotifyurl;
                }
                else
                {
                    pendingsc->posturl = APIURL;
                    pendingsc->posturl.append("wsc");
                }

                pendingsc->protect = true;
                pendingsc->posturl.append("?sn=");
                pendingsc->posturl.append(scsn.text());

                pendingsc->posturl.append(auth);

                pendingsc->type = REQ_JSON;
                pendingsc->post(this);
            }
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
            else if(badhostcs->status == REQ_FAILURE
                    || (badhostcs->status == REQ_INFLIGHT && Waiter::ds >= (badhostcs->lastdata + HttpIO::REQUESTTIMEOUT)))
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
                    if (!pendingcs_serverBusySent)
                    {
                        sendevent(99425, "Timeout (server busy)", 0);
                        pendingcs_serverBusySent = true;
                    }
                    pendingcs->lastdata = Waiter::ds;
                }
                else
                {
                    LOG_err << "Error in lock request: " << workinglockcs->in;
                    disconnecttimestamp = Waiter::ds + HttpIO::CONNECTTIMEOUT;
                }

                workinglockcs.reset();
                requestLock = false;
            }
            else if (workinglockcs->status == REQ_FAILURE
                     || (workinglockcs->status == REQ_INFLIGHT && Waiter::ds >= (workinglockcs->lastdata + HttpIO::REQUESTTIMEOUT)))
            {
                LOG_warn << "Failed lock request. Retrying...";
                btworkinglock.backoff();
                workinglockcs.reset();
            }
        }

        // fill transfer slots from the queue
        if (lastDispatchTransfersDs != Waiter::ds)
        {
            // don't run this too often or it may use a lot of cpu without starting new transfers, if the list is long
            lastDispatchTransfersDs = Waiter::ds;

            size_t lastCount = 0;
            size_t transferCount = transfers[GET].size() + transfers[PUT].size();
            do
            {
                lastCount = transferCount;
                
                // Check the list of transfers and start a few big files, and many small, up to configured limits.
                dispatchTransfers();
                
                // if we are cancelling a lot of transfers (eg. nodes to download were deleted), keep going. Avoid stalling when no transfers are active and all queued fail
                transferCount = transfers[GET].size() + transfers[PUT].size();
            } while (transferCount < lastCount);
        }

#ifndef EMSCRIPTEN
        assert(!asyncfopens);
#endif

        slotit = tslots.begin();


        if (!mBlocked) // handle active unpaused transfers
        {
            DBTableTransactionCommitter committer(tctable);

            while (slotit != tslots.end())
            {
                transferslot_list::iterator it = slotit;

                slotit++;

                if (!xferpaused[(*it)->transfer->type] && (!(*it)->retrying || (*it)->retrybt.armed()))
                {
                    (*it)->doio(this, committer);
                }
            }
        }
        else
        {
            LOG_debug << "skipping slots doio while blocked";
        }

#ifdef ENABLE_SYNC
        // verify filesystem fingerprints, disable deviating syncs
        // (this covers mountovers, some device removals and some failures)
        for (Sync* sync : syncs)
        {
            if (sync->fsfp)
            {
                fsfp_t current = sync->dirnotify->fsfingerprint();
                if (sync->fsfp != current)
                {
                    LOG_err << "Local fingerprint mismatch. Previous: " << sync->fsfp
                            << "  Current: " << current;
                    sync->errorcode = API_EFAILED;
                    sync->changestate(SYNC_FAILED);
                }
            }
        }

        if (!syncsup)
        {
            // set syncsup if there are no initializing syncs
            // this will allow incoming server-client commands to trigger the filesystem
            // actions that have occurred while the sync app was not running
            bool anyscanning = false;
            for (Sync* sync : syncs)
            {
                if (sync->state == SYNC_INITIALSCAN)
                {
                    anyscanning = true;
                }
            }

            if (!anyscanning)
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
                    for (Sync* sync : syncs)
                    {
                        prevpending = prevpending || sync->dirnotify->notifyq[q].size();
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
                for (Sync* sync : syncs)
                {
                    if (sync->isnetwork && (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN))
                    {
                        Notification notification;
                        while (sync->dirnotify->notifyq[DirNotify::EXTRA].popFront(notification))
                        {
                            dstime dsmin = Waiter::ds - Sync::EXTRA_SCANNING_DELAY_DS;
                            if (notification.timestamp <= dsmin)
                            {
                                LOG_debug << "Processing extra fs notification: " << notification.path.toPath(*fsaccess);
                                sync->dirnotify->notify(DirNotify::DIREVENTS, notification.localnode, std::move(notification.path));
                            }
                            else
                            {
                                sync->dirnotify->notifyq[DirNotify::EXTRA].unpopFront(notification);
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
                        for (auto it = syncs.begin(); it != syncs.end(); )
                        {
                            Sync* sync = *it++;

                            if (sync->state == SYNC_CANCELED || sync->state == SYNC_FAILED)
                            {
                                delete sync;  // removes itself from the client's list that we are iterating
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
                                            syncup(sync->localroot.get(), &nds);
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
                                    sync->deletemissing(sync->localroot.get());
                                }
                            }
                        }

                        if (syncadding)
                        {
                            break;
                        }
                    }
                }

                size_t totalpending = 0;
                size_t scanningpending = 0;
                for (int q = DirNotify::RETRY; q >= DirNotify::DIREVENTS; q--)
                {
                    for (Sync* sync : syncs)
                    {
                        sync->cachenodes();

                        totalpending += sync->dirnotify->notifyq[q].size();
                        Notification notification;
                        if (q == DirNotify::DIREVENTS)
                        {
                            scanningpending += sync->dirnotify->notifyq[q].size();
                        }
                        else if (!syncfslockretry && sync->dirnotify->notifyq[DirNotify::RETRY].peekFront(notification))
                        {
                            syncfslockretrybt.backoff(Sync::SCANNING_DELAY_DS);
                            blockedfile = notification.path;
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
                    for (Sync* sync : syncs)
                    {
                        // make sure that the remote synced folder still exists
                        if (!sync->localroot->node)
                        {
                            LOG_err << "The remote root node doesn't exist";
                            sync->errorcode = API_ENOENT;
                            sync->changestate(SYNC_FAILED);
                        }
                    }

                    // perform aggregate ops that require all scanqs to be fully processed
                    bool anyqueued = false;
                    for (Sync* sync : syncs)
                    {
                        if (sync->dirnotify->notifyq[DirNotify::DIREVENTS].size()
                          || sync->dirnotify->notifyq[DirNotify::RETRY].size())
                        {
                            if (!syncnagleretry && !syncfslockretry)
                            {
                                syncactivity = true;
                            }

                            anyqueued = true;
                        }
                    }

                    if (!anyqueued)
                    {
                        // execution of notified deletions - these are held in localsyncnotseen and
                        // kept pending until all creations (that might reference them for the purpose of
                        // copying) have completed and all notification queues have run empty (to ensure
                        // that moves are not executed as deletions+additions.
                        if (localsyncnotseen.size() && !synccreate.size())
                        {
                            // ... execute all pending deletions
                            LocalPath path;
                            auto fa = fsaccess->newfileaccess();
                            while (localsyncnotseen.size())
                            {
                                LocalNode* l = *localsyncnotseen.begin();
                                unlinkifexists(l, fa.get(), path);
                                delete l;
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
                            bool repeatsyncup = false;
                            bool syncupdone = false;
                            for (Sync* sync : syncs)
                            {
                                if ((sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
                                 && !syncadding && syncuprequired && !syncnagleretry)
                                {
                                    LOG_debug << "Running syncup on demand";
                                    repeatsyncup |= !syncup(sync->localroot.get(), &nds);
                                    syncupdone = true;
                                    sync->cachenodes();
                                }
                            }
                            syncuprequired = !syncupdone || repeatsyncup;

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
                            bool noneSkipped = true;
                            for (Sync* sync : syncs)
                            {
                                totalnodes += sync->localnodes[FILENODE] + sync->localnodes[FOLDERNODE];

                                if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
                                {
                                    if (sync->dirnotify->notifyq[DirNotify::DIREVENTS].size()
                                     || sync->dirnotify->notifyq[DirNotify::RETRY].size())
                                    {
                                        noneSkipped = false;
                                    }
                                    else
                                    {
                                        if (sync->fullscan)
                                        {
                                            // recursively delete all LocalNodes that were deleted (not moved or renamed!)
                                            sync->deletemissing(sync->localroot.get());
                                            sync->cachenodes();
                                        }

                                        // if the directory events notification subsystem is permanently unavailable or
                                        // has signaled a temporary error, initiate a full rescan
                                        if (sync->state == SYNC_ACTIVE)
                                        {
                                            sync->fullscan = false;

                                            string failedReason;
                                            auto failed = sync->dirnotify->getFailed(failedReason);

                                            if (syncscanbt.armed()
                                                    && (failed || fsaccess->notifyfailed
                                                        || sync->dirnotify->mErrorCount.load() || fsaccess->notifyerr))
                                            {
                                                LOG_warn << "Sync scan failed " << failed
                                                         << " " << fsaccess->notifyfailed
                                                         << " " << sync->dirnotify->mErrorCount.load()
                                                         << " " << fsaccess->notifyerr;
                                                if (failed)
                                                {
                                                    LOG_warn << "The cause was: " << failedReason;
                                                }
                                                scanfailed = true;

                                                sync->scan(&sync->localroot->localname, NULL);
                                                sync->dirnotify->mErrorCount = 0;
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
                                dstime backoff = 300 + totalnodes / 128;
                                syncscanbt.backoff(backoff);
                                syncscanfailed = true;
                                LOG_warn << "Next full scan in " << backoff << " ds";
                            }

                            // clear pending global notification error flag if all syncs were marked
                            // to be rescanned
                            if (fsaccess->notifyerr && noneSkipped)
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
                    for (Sync* sync : syncs)
                    {
                        // make sure that the remote synced folder still exists
                        if (!sync->localroot->node)
                        {
                            LOG_err << "The remote root node doesn't exist";
                            sync->errorcode = API_ENOENT;
                            sync->changestate(SYNC_FAILED);
                        }
                        else
                        {
                            LocalPath localpath = sync->localroot->localname;
                            if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
                            {
                                LOG_debug << "Running syncdown on demand";
                                if (!syncdown(sync->localroot.get(), localpath, true))
                                {
                                    // a local filesystem item was locked - schedule periodic retry
                                    // and force a full rescan afterwards as the local item may
                                    // be subject to changes that are notified with obsolete paths
                                    success = false;
                                    sync->dirnotify->mErrorCount = true;
                                }

                                sync->cachenodes();
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
            if (!auth.empty())
            {
                LOG_debug << "Sending lock request";
                workinglockcs.reset(new HttpReq());
                workinglockcs->logname = clientname + "accountBusyCheck ";
                workinglockcs->posturl = APIURL;
                workinglockcs->posturl.append("cs?");
                workinglockcs->posturl.append(auth);
                workinglockcs->posturl.append("&wlt=1");
                workinglockcs->type = REQ_JSON;
                workinglockcs->post(this);
            }
            else if (!EVER(disconnecttimestamp))
            {
                LOG_warn << "Possible server timeout, but we don't have auth yet, disconnect and retry";
                disconnecttimestamp = Waiter::ds + HttpIO::CONNECTTIMEOUT;
            }
        }


        for (vector<TimerWithBackoff *>::iterator it = bttimers.begin(); it != bttimers.end(); )
        {
            TimerWithBackoff *bttimer = *it;
            if (bttimer->armed())
            {
                restag = bttimer->tag;
                app->timer_result(API_OK);
                delete bttimer;
                it = bttimers.erase(it);
            }
            else
            {
                ++it;
            }
        }

        httpio->updatedownloadspeed();
        httpio->updateuploadspeed();
    } while (httpio->doio() || execdirectreads() || (!pendingcs && reqs.cmdspending() && btcs.armed()) || looprequested);


    NodeCounter storagesum;
    for (auto& nc : mNodeCounters)
    {
        if (nc.first == rootnodes[0] || nc.first == rootnodes[1] || nc.first == rootnodes[2])
        {
            storagesum += nc.second;
        }
    }
    if (mNotifiedSumSize != storagesum.storage)
    {
        mNotifiedSumSize = storagesum.storage;
        app->storagesum_changed(mNotifiedSumSize);
    }

#ifdef MEGA_MEASURE_CODE
    performanceStats.transfersActiveTime.start(!tslots.empty() && !performanceStats.transfersActiveTime.inprogress());
    performanceStats.transfersActiveTime.stop(tslots.empty() && performanceStats.transfersActiveTime.inprogress());

    static auto lasttime = Waiter::ds;
    if (Waiter::ds > lasttime + 1200)
    {
        lasttime = Waiter::ds;
        LOG_info << performanceStats.report(false, httpio, waiter, reqs);
    }
#endif
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
    CodeCounter::ScopeTimer ccst(performanceStats.prepareWait);

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
        transferSlotsBackoff.update(&nds, false);

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
        if (!pendingsc && !pendingscUserAlerts && scsn.ready() && !mBlocked)
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

        for (vector<TimerWithBackoff *>::iterator cit = bttimers.begin(); cit != bttimers.end(); cit++)
        {
            (*cit)->update(&nds);
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

        if (cachedug)
        {
            btugexpiration.update(&nds);
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


        if (badhostcs && EVER(badhostcs->lastdata)
                && badhostcs->status == REQ_INFLIGHT)
        {
            dstime timeout = badhostcs->lastdata + HttpIO::REQUESTTIMEOUT;
            if (timeout > Waiter::ds && timeout < nds)
            {
                nds = timeout;
            }
            else if (timeout <= Waiter::ds)
            {
                nds = 0;
            }
        }

        if (!pendingscTimedOut && !jsonsc.pos && pendingsc && pendingsc->status == REQ_INFLIGHT)
        {
            dstime timeout = pendingsc->lastdata + HttpIO::SCREQUESTTIMEOUT;
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

    // immediate action required?
    if (!nds)
    {
        ++performanceStats.prepwaitImmediate;
        return Waiter::NEEDEXEC;
    }

    // nds is either MAX_INT (== no pending events) or > Waiter::ds
    if (EVER(nds))
    {
        nds -= Waiter::ds;
    }

#ifdef MEGA_MEASURE_CODE
    bool reasonGiven = false;
    if (nds == 0)
    {
        ++performanceStats.prepwaitZero;
        reasonGiven = true;
    }
#endif

    waiter->init(nds);

    // set subsystem wakeup criteria (WinWaiter assumes httpio to be set first!)
    waiter->wakeupby(httpio, Waiter::NEEDEXEC);

#ifdef MEGA_MEASURE_CODE
    if (waiter->maxds == 0 && !reasonGiven)
    {
        ++performanceStats.prepwaitHttpio;
        reasonGiven = true;
    }
#endif

    waiter->wakeupby(fsaccess, Waiter::NEEDEXEC);

#ifdef MEGA_MEASURE_CODE
    if (waiter->maxds == 0 && !reasonGiven)
    {
        ++performanceStats.prepwaitFsaccess;
        reasonGiven = true;
    }
    if (!reasonGiven)
    {
        ++performanceStats.nonzeroWait;
    }
#endif

    return 0;
}

int MegaClient::dowait()
{
    CodeCounter::ScopeTimer ccst(performanceStats.doWait);

    return waiter->wait();
}

int MegaClient::checkevents()
{
    CodeCounter::ScopeTimer ccst(performanceStats.checkEvents);

    int r =  httpio->checkevents(waiter);
    r |= fsaccess->checkevents(waiter);
    if (gfx)
    {
        r |= gfx->checkevents(waiter);
    }
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
        if (ststatus != STORAGE_PAYWALL)    // in ODQ Paywall, ULs/DLs are not allowed
        {
            // in ODQ Red, only ULs are disallowed
            int end = (ststatus != STORAGE_RED) ? PUT : GET;
            for (int d = GET; d <= end; d += PUT - GET)
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

    if (!pendingsc && !pendingscUserAlerts && btsc.arm())
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

// activate enough queued transfers as necessary to keep the system busy - but not too busy
void MegaClient::dispatchTransfers()
{
    // do we have any transfer slots available?
    if (!slotavail())
    {
        LOG_verbose << "No slots available";
        return;
    }

    CodeCounter::ScopeTimer ccst(performanceStats.dispatchTransfers);

    struct counter 
    { 
        m_off_t remainingsum = 0; 
        unsigned total = 0; 
        unsigned added = 0;
        bool hasVeryBig = false;

        void addexisting(m_off_t size, m_off_t progressed)
        {
            remainingsum += size - progressed;
            total += 1;
            if (size > 100 * 1024 * 1024 && (size - progressed) > 5 * 1024 * 1024)
            {
                hasVeryBig = true;
            }
        }
        void addnew(m_off_t size)
        {
            addexisting(size, 0);
            added += 1;
        }
    };
    std::array<counter, 6> counters;

    // Determine average speed and total amount of data remaining for the given direction/size-category
    // We prepare data for put/get in index 0..1, and the put/get/big/small combinations in index 2..5
    for (TransferSlot* ts : tslots)
    {
        assert(ts->transfer->type == PUT || ts->transfer->type == GET);
        TransferCategory tc(ts->transfer);
        counters[tc.index()].addexisting(ts->transfer->size, ts->progressreported);
        counters[tc.directionIndex()].addexisting(ts->transfer->size,  ts->progressreported);
    }

    std::function<bool(Transfer*)> testAddTransferFunction = [&counters, this](Transfer* t) 
        { 
            TransferCategory tc(t);

            // hard limit on puts/gets
            if (counters[tc.directionIndex()].total >= MAXTRANSFERS)
            {
                return false;
            }

            // only request half the max at most, to get a quicker response from the API and get overlap with transfers going
            if (counters[tc.directionIndex()].added >= MAXTRANSFERS/2)
            {
                return false;
            }

            // If we have one very big file, that is enough to max out the bandwidth by itself; get that one done quickly (without preventing more small files).
            if (counters[tc.index()].hasVeryBig)
            {
                return false;
            }

            // queue up enough transfers that we can expect to keep busy for at least the next 30 seconds in this category
            m_off_t speed = (tc.direction == GET) ? httpio->downloadSpeed : httpio->uploadSpeed;
            m_off_t targetOutstanding = 30 * speed;
            targetOutstanding = std::max<m_off_t>(targetOutstanding, 2 * 1024 * 1024);
            targetOutstanding = std::min<m_off_t>(targetOutstanding, 100 * 1024 * 1024);

            if (counters[tc.index()].remainingsum >= targetOutstanding)
            {
                return false;
            }

            counters[tc.index()].addnew(t->size);
            counters[tc.directionIndex()].addnew(t->size);

            return true; 
        };

    std::array<vector<Transfer*>, 6> nextInCategory = transferlist.nexttransfers(testAddTransferFunction);

    // Iterate the 4 combinations in this order:
    static const TransferCategory categoryOrder[] = {
        TransferCategory(PUT, LARGEFILE),
        TransferCategory(GET, LARGEFILE),
        TransferCategory(PUT, SMALLFILE),
        TransferCategory(GET, SMALLFILE),
    };

    DBTableTransactionCommitter committer(tctable);

    for (auto category : categoryOrder)
    {
        for (Transfer *nexttransfer : nextInCategory[category.index()])
        {
            if (!slotavail())
            {
                return;
            }

            if (category.direction == PUT && queuedfa.size() > MAXQUEUEDFA)
            {
                // file attribute jam? halt uploads.
                LOG_warn << "Attribute queue full: " << queuedfa.size();
                break;
            }

            if (nexttransfer->localfilename.empty())
            {
                // this is a fresh transfer rather than the resumption of a partly
                // completed and deferred one
                if (nexttransfer->type == PUT)
                {
                    // generate fresh random encryption key/CTR IV for this file
                    byte keyctriv[SymmCipher::KEYLENGTH + sizeof(int64_t)];
                    rng.genblock(keyctriv, sizeof keyctriv);
                    memcpy(nexttransfer->transferkey.data(), keyctriv, SymmCipher::KEYLENGTH);
                    nexttransfer->ctriv = MemAccess::get<uint64_t>((const char*)keyctriv + SymmCipher::KEYLENGTH);
                }
                else
                {
                    // set up keys for the decryption of this file (k == NULL => private node)
                    const byte* k = NULL;
                    bool missingPrivateNode = false;

                    // locate suitable template file
                    for (file_list::iterator it = nexttransfer->files.begin(); it != nexttransfer->files.end(); it++)
                    {
                        if ((*it)->hprivate && !(*it)->hforeign)
                        {
                            // Make sure we have the size field
                            Node* n = nodebyhandle((*it)->h);
                            if (!n)
                            {
                                missingPrivateNode = true;
                            }
                            else if (n->type == FILENODE)
                            {
                                k = (const byte*)n->nodekey().data();
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
                            memcpy(nexttransfer->transferkey.data(), k, SymmCipher::KEYLENGTH);
                            SymmCipher::xorblock(k + SymmCipher::KEYLENGTH, nexttransfer->transferkey.data());
                            nexttransfer->ctriv = MemAccess::get<int64_t>((const char*)k + SymmCipher::KEYLENGTH);
                            nexttransfer->metamac = MemAccess::get<int64_t>((const char*)k + SymmCipher::KEYLENGTH + sizeof(int64_t));
                            break;
                        }
                    }

                    if (!k)
                    {
                        // there are no keys to decrypt this download - if it's because the node to download doesn't exist anymore, fail the transfer (otherwise wait for keys to become available)
                        if (missingPrivateNode)
                        {
                            nexttransfer->failed(API_EARGS, committer);
                        }
                        continue;
                    }
                }

                nexttransfer->localfilename.clear();

                // set file localnames (ultimate target) and one transfer-wide temp
                // localname
                for (file_list::iterator it = nexttransfer->files.begin();
                    nexttransfer->localfilename.empty() && it != nexttransfer->files.end(); it++)
                {
                    (*it)->prepare();
                }

                // app-side transfer preparations (populate localname, create thumbnail...)
                app->transfer_prepare(nexttransfer);
            }

            bool openok = false;
            bool openfinished = false;

            // verify that a local path was given and start/resume transfer
            if (!nexttransfer->localfilename.empty())
            {
                TransferSlot *ts = nullptr;

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
                        nexttransfer->asyncopencontext = (nexttransfer->type == PUT)
                            ? ts->fa->asyncfopen(nexttransfer->localfilename)
                            : ts->fa->asyncfopen(nexttransfer->localfilename, false, true, nexttransfer->size);
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
                    openok = (nexttransfer->type == PUT)
                        ? ts->fa->fopen(nexttransfer->localfilename)
                        : ts->fa->fopen(nexttransfer->localfilename, false, true);
                    openfinished = true;
                }

                if (openfinished && openok)
                {
                    handle h = UNDEF;
                    bool hprivate = true;
                    const char *privauth = NULL;
                    const char *pubauth = NULL;
                    const char *chatauth = NULL;

                    nexttransfer->pos = 0;
                    nexttransfer->progresscompleted = 0;

                    if (nexttransfer->type == GET || nexttransfer->tempurls.size())
                    {
                        m_off_t p = 0;

                        // resume at the end of the last contiguous completed block
                        nexttransfer->chunkmacs.calcprogress(nexttransfer->size, nexttransfer->pos, nexttransfer->progresscompleted, &p);

                        if (nexttransfer->progresscompleted > nexttransfer->size)
                        {
                            LOG_err << "Invalid transfer progress!";
                            nexttransfer->pos = nexttransfer->size;
                            nexttransfer->progresscompleted = nexttransfer->size;
                        }

                        ts->updatecontiguousprogress();
                        LOG_debug << "Resuming transfer at " << nexttransfer->pos
                            << " Completed: " << nexttransfer->progresscompleted
                            << " Contiguous: " << ts->progresscontiguous
                            << " Partial: " << p << " Size: " << nexttransfer->size
                            << " ultoken: " << (nexttransfer->ultoken != NULL);
                    }
                    else
                    {
                        nexttransfer->chunkmacs.clear();
                    }

                    ts->progressreported = nexttransfer->progresscompleted;

                    if (nexttransfer->type == PUT)
                    {
                        if (ts->fa->mtime != nexttransfer->mtime || ts->fa->size != nexttransfer->size)
                        {
                            LOG_warn << "Modification detected starting upload.   Size: " << nexttransfer->size << "  Mtime: " << nexttransfer->mtime
                                << "    FaSize: " << ts->fa->size << "  FaMtime: " << ts->fa->mtime;
                            nexttransfer->failed(API_EREAD, committer);
                            continue;
                        }

                        // create thumbnail/preview imagery, if applicable (FIXME: do not re-create upon restart)
                        if (!nexttransfer->localfilename.empty() && !nexttransfer->uploadhandle)
                        {
                            nexttransfer->uploadhandle = getuploadhandle();

                            if (!gfxdisabled && gfx && gfx->isgfx(nexttransfer->localfilename.editStringDirect()))
                            {
                                // we want all imagery to be safely tucked away before completing the upload, so we bump minfa
                                nexttransfer->minfa += gfx->gendimensionsputfa(ts->fa, nexttransfer->localfilename.editStringDirect(), nexttransfer->uploadhandle, nexttransfer->transfercipher(), -1, false);
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
                                chatauth = (*it)->chatauth;
                                break;
                            }
                            else
                            {
                                LOG_err << "Unexpected node ownership";
                            }
                        }
                    }

                    // dispatch request for temporary source/target URL
                    if (nexttransfer->tempurls.size())
                    {
                        ts->transferbuf.setIsRaid(nexttransfer, nexttransfer->tempurls, nexttransfer->pos, ts->maxRequestSize);
                        app->transfer_prepare(nexttransfer);
                    }
                    else
                    {
                        reqs.add((ts->pendingcmd = (nexttransfer->type == PUT)
                            ? (Command*)new CommandPutFile(this, ts, putmbpscap)
                            : (Command*)new CommandGetFile(this, ts, NULL, h, hprivate, privauth, pubauth, chatauth)));
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

                    performanceStats.transferStarts += 1;
                }
                else if (openfinished)
                {
                    string utf8path = nexttransfer->localfilename.toPath(*fsaccess);
                    if (nexttransfer->type == GET)
                    {
                        LOG_err << "Error dispatching transfer. Temporary file not writable: " << utf8path;
                        nexttransfer->failed(API_EWRITE, committer);
                    }
                    else if (!ts->fa->retry)
                    {
                        LOG_err << "Error dispatching transfer. Local file permanently unavailable: " << utf8path;
                        nexttransfer->failed(API_EREAD, committer);
                    }
                    else
                    {
                        LOG_warn << "Error dispatching transfer. Local file temporarily unavailable: " << utf8path;
                        nexttransfer->failed(API_EREAD, committer);
                    }
                }
            }
            else
            {
                LOG_err << "Error preparing transfer. No localfilename";
                nexttransfer->failed(API_EREAD, committer);
            }
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
        for (fa_map::iterator it = pendingfa.lower_bound(pair<handle, fatype>(th, fatype(0)));
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
    DBTableTransactionCommitter committer(tctable);
    for (auto transferPtr : transfers[d])
    {
        transferPtr.second->mOptimizedDelete = true;  // so it doesn't remove itself from this list while deleting
        app->transfer_removed(transferPtr.second);
        delete transferPtr.second;
    }
    transfers[d].clear();
    transferlist.transfers[GET].clear();
    transferlist.transfers[PUT].clear();
}

bool MegaClient::isFetchingNodesPendingCS()
{
    return pendingcs && pendingcs->includesFetchingNodes;
}

#ifdef ENABLE_SYNC
void MegaClient::resumeResumableSyncs()
{

    if (!syncConfigs || !allowAutoResumeSyncs)
    {
        return;
    }
    for (const auto& config : syncConfigs->all())
    {
        if (!config.isResumable())
        {
            continue;
        }
        const auto e = addsync(config, DEBRISFOLDER, nullptr);
        if (e == 0)
        {
            app->sync_auto_resumed(config.getLocalPath(), config.getRemoteNode(),
                                   static_cast<long long>(config.getLocalFingerprint()),
                                   config.getRegExps());
        }
    }
}
#endif
// determine next scheduled transfer retry
void MegaClient::nexttransferretry(direction_t d, dstime* dsmin)
{
    if (!xferpaused[d])   // avoid setting the timer's next=1 if it won't be processed
    {
        transferRetryBackoffs[d].update(dsmin, true);
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

    if (pendingscUserAlerts)
    {
        pendingscUserAlerts->disconnect();
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

// force retrieval of pending actionpackets immediately
// by closing pending sc, reset backoff and clear waitd URL
void MegaClient::catchup()
{
    if (pendingsc)
    {
        pendingsc->disconnect();

        pendingsc.reset();
    }
    btsc.reset();
    scnotifyurl.clear();
}

void MegaClient::abortlockrequest()
{
    workinglockcs.reset();
    btworkinglock.reset();
    requestLock = false;
    disconnecttimestamp = NEVER;
}

void MegaClient::logout()
{
    if (loggedin() != FULLACCOUNT)
    {
        locallogout(true);

        restag = reqtag;
        app->logout_result(API_OK);
        return;
    }

    loggingout++;
    reqs.add(new CommandLogout(this));
}

void MegaClient::locallogout(bool removecaches)
{
    mAsyncQueue.clearDiscardable();

    if (removecaches)
    {
        removeCaches();
    }

    delete sctable;
    sctable = NULL;
    pendingsccommit = false;

    me = UNDEF;
    uid.clear();
    unshareablekey.clear();
    publichandle = UNDEF;
    cachedscsn = UNDEF;
    achievements_enabled = false;
    isNewSession = false;
    tsLogin = 0;
    versions_disabled = false;
    accountsince = 0;
    gmfa_enabled = false;
    ssrs_enabled = false;
    nsr_enabled = false;
    aplvp_enabled = false;
    mNewLinkFormat = false;
    mSmsVerificationState = SMS_STATE_UNKNOWN;
    mSmsVerifiedPhone.clear();
    loggingout = 0;
    loggedout = false;
    cachedug = false;
    minstreamingrate = -1;
    ephemeralSession = false;
#ifdef USE_MEDIAINFO
    mediaFileInfo = MediaFileInfo();
#endif

    // remove any cached transfers older than two days that have not been resumed (updates transfer list)
    purgeOrphanTransfers();

    // delete all remaining transfers (optimized not to remove from transfer list one by one) 
    // transfer destructors update the transfer in the cache database
    freeq(GET);
    freeq(PUT);

    // close the transfer cache database.
    disconnect();
    closetc();

    freeq(GET);  // freeq after closetc due to optimizations
    freeq(PUT);

    purgenodesusersabortsc(false);

    reqs.clear();

    delete pendingcs;
    pendingcs = NULL;
    scsn.clear();
    mBlocked = false;

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

    for (vector<TimerWithBackoff *>::iterator it = bttimers.begin(); it != bttimers.end();  it++)
    {
        delete *it;
    }

    queuedfa.clear();
    activefa.clear();
    pendinghttp.clear();
    bttimers.clear();
    xferpaused[PUT] = false;
    xferpaused[GET] = false;
    putmbpscap = 0;
    fetchingnodes = false;
    fetchnodestag = 0;
    ststatus = STORAGE_UNKNOWN;
    overquotauntil = 0;
    mOverquotaDeadlineTs = 0;
    mOverquotaWarningTs.clear();
    mBizGracePeriodTs = 0;
    mBizExpirationTs = 0;
    mBizMode = BIZ_MODE_UNKNOWN;
    mBizStatus = BIZ_STATUS_UNKNOWN;
    mBizMasters.clear();
    mPublicLinks.clear();
    scpaused = false;

    for (fafc_map::iterator cit = fafcs.begin(); cit != fafcs.end(); cit++)
    {
        for (int i = 2; i--; )
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
    resetKeyring();

    key.setkey(SymmCipher::zeroiv);
    tckey.setkey(SymmCipher::zeroiv);
    asymkey.resetkey();
    mPrivKey.clear();
    pubk.resetkey();
    resetKeyring();
    memset((char*)auth.c_str(), 0, auth.size());
    auth.clear();
    sessionkey.clear();
    accountversion = 0;
    accountsalt.clear();
    sid.clear();
    k.clear();

    mAuthRings.clear();
    mAuthRingsTemp.clear();
    mFetchingAuthrings = false;

    init();

    if (dbaccess)
    {
        dbaccess->currentDbVersion = DbAccess::LEGACY_DB_VERSION;
    }

#ifdef ENABLE_SYNC
    syncadding = 0;
    totalLocalNodes = 0;
#endif

    fetchingkeys = false;
}

void MegaClient::removeCaches()
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
    if (syncConfigs)
    {
        syncConfigs->clear();
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
    GenericHttpReq *req = new GenericHttpReq(rng);
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = (usehttps ? string("https://") : string("http://")) + hostname;
    req->dns(this);
}

void MegaClient::gelbrequest(const char *service, int timeoutds, int retries)
{
    GenericHttpReq *req = new GenericHttpReq(rng);
    req->tag = reqtag;
    req->maxretries = retries;
    if (timeoutds > 0)
    {
        req->maxbt.backoff(timeoutds);
    }
    pendinghttp[reqtag] = req;
    req->posturl = GELBURL;
    req->posturl.append("?service=");
    req->posturl.append(service);
    req->protect = true;
    req->get(this);
}

void MegaClient::sendchatstats(const char *json, int port)
{
    GenericHttpReq *req = new GenericHttpReq(rng);
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = CHATSTATSURL;
    if (port > 0)
    {
        req->posturl.append(":");
        char stringPort[6];
        sprintf(stringPort, "%d", port);
        req->posturl.append(stringPort);
    }
    req->posturl.append("/stats");
    req->protect = true;
    req->out->assign(json);
    req->post(this);
}

void MegaClient::sendchatlogs(const char *json, const char *aid, int port)
{
    GenericHttpReq *req = new GenericHttpReq(rng);
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = CHATSTATSURL;
    if (port > 0)
    {
        req->posturl.append(":");
        char stringPort[6];
        sprintf(stringPort, "%d", port);
        req->posturl.append(stringPort);
    }
    req->posturl.append("/msglog?aid=");
    req->posturl.append(aid);
    req->posturl.append("&t=e");
    req->protect = true;
    req->out->assign(json);
    req->post(this);
}

void MegaClient::httprequest(const char *url, int method, bool binary, const char *json, int retries)
{
    GenericHttpReq *req = new GenericHttpReq(rng, binary);
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
    CodeCounter::ScopeTimer ccst(performanceStats.scProcessingTime);

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
                    jsonsc.storeobject(&scnotifyurl);
                    break;

                case MAKENAMEID2('i', 'r'):
                    // when spoonfeeding is in action, there may still be more actionpackets to be delivered.
                    insca_notlast = jsonsc.getint() == 1;
                    break;

                case MAKENAMEID2('s', 'n'):
                    // the sn element is guaranteed to be the last in sequence (except for notification requests (c=50))
                    scsn.setScsn(&jsonsc);
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
                    LOG_debug << "Processing of action packets finished.  More to follow: " << insca_notlast;
                    mergenewshares(1);
                    applykeys();

                    if (!statecurrent && !insca_notlast)   // with actionpacket spoonfeeding, just finishing a batch does not mean we are up to date yet - keep going while "ir":1
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
#ifdef ENABLE_SYNC
                            resumeResumableSyncs();
#endif

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

                        if (notifyStorageChangeOnStateCurrent)
                        {
                            app->notify_storage(STORAGE_CHANGE);
                            notifyStorageChangeOnStateCurrent = false;
                        }

                        if (tctable && cachedfiles.size())
                        {
                            DBTableTransactionCommitter committer(tctable);
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
                                if (!startxfer(type, file, committer))
                                {
                                    tctable->del(cachedfilesdbids.at(i));
                                    continue;
                                }
                            }
                            cachedfiles.clear();
                            cachedfilesdbids.clear();
                        }

                        WAIT_CLASS::bumpds();
                        fnstats.timeToTransfersResumed = Waiter::ds - fnstats.startTime;

                        string report;
                        fnstats.toJsonArray(&report);

                        sendevent(99426, report.c_str(), 0);    // Treeproc performance log

                        // NULL vector: "notify all elements"
                        app->nodes_updated(NULL, int(nodes.size()));
                        app->users_updated(NULL, int(users.size()));
                        app->pcrs_updated(NULL, int(pcrindex.size()));
#ifdef ENABLE_CHAT
                        app->chats_updated(NULL, int(chats.size()));
#endif
                        for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++)
                        {
                            memset(&(it->second->changed), 0, sizeof it->second->changed);
                        }

                        if (!loggedinfolderlink())
                        {
                            // historic user alerts are not supported for public folders
                            // now that we have loaded cached state, and caught up actionpackets since that state
                            // (or just fetched everything if there was no cache), our next sc request can be for useralerts
                            useralerts.begincatchup = true;
                        }
                    }

                    if (!insca_notlast)
                    {
                        app->catchup_result();
                    }
                    return true;

                case 'a':
                    if (jsonsc.enterarray())
                    {
                        LOG_debug << "Processing action packets";
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
#ifdef ENABLE_CHAT
                        bool readingPublicChat = false;
#endif
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
                                {
                                    useralerts.beginNotingSharedNodes();
                                    handle originatingUser = sc_newnodes();
                                    mergenewshares(1);
                                    useralerts.convertNotedSharedNodes(true, originatingUser);
                                }

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

                            case MAKENAMEID4('p', 's', 'e', 's'):
                                sc_paymentreminder();
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
                                sc_upc(true);
                                break;

                            case MAKENAMEID4('u', 'p', 'c', 'o'):
                                // outgoing pending contact request update (from them, accept/deny/ignore)
                                sc_upc(false);
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
                            case MAKENAMEID4('m', 'c', 'p', 'c'):      // fall-through
                            {
                                readingPublicChat = true;
                            }
                            case MAKENAMEID3('m', 'c', 'c'):
                                // chat creation / peer's invitation / peer's removal
                                sc_chatupdate(readingPublicChat);
                                break;

                            case MAKENAMEID5('m', 'c', 'f', 'p', 'c'):      // fall-through
                            case MAKENAMEID4('m', 'c', 'f', 'c'):
                                // chat flags update
                                sc_chatflags();
                                break;

                            case MAKENAMEID5('m', 'c', 'p', 'n', 'a'):      // fall-through
                            case MAKENAMEID4('m', 'c', 'n', 'a'):
                                // granted / revoked access to a node
                                sc_chatnode();
                                break;
#endif
                            case MAKENAMEID3('u', 'a', 'c'):
                                sc_uac();
                                break;

                            case MAKENAMEID2('l', 'a'):
                                // last acknowledged
                                sc_la();
                                break;

                            case MAKENAMEID2('u', 'b'):
                                // business account update
                                sc_ub();
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

// update the user's local state cache, on completion of the fetchnodes command
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
        handle tscsn = scsn.getHandle();
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
        LOG_debug << "Saving SCSN " << scsn.text() << " with " << nodes.size() << " nodes, " << users.size() << " users, " << pcrindex.size() << " pcrs and " << chats.size() << " chats to local cache (" << complete << ")";
#else

        LOG_debug << "Saving SCSN " << scsn.text() << " with " << nodes.size() << " nodes and " << users.size() << " users and " << pcrindex.size() << " pcrs to local cache (" << complete << ")";
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

        if (!scsn.ready())
        {
            LOG_err << "scsn not known, not updating database";
            return;
        }

        bool complete;

        // 1. update associated scsn
        handle tscsn = scsn.getHandle();
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
        LOG_debug << "Saving SCSN " << scsn.text() << " with " << nodenotify.size() << " modified nodes, " << usernotify.size() << " users, " << pcrnotify.size() << " pcrs and " << chatnotify.size() << " chats to local cache (" << complete << ")";
#else
        LOG_debug << "Saving SCSN " << scsn.text() << " with " << nodenotify.size() << " modified nodes, " << usernotify.size() << " users and " << pcrnotify.size() << " pcrs to local cache (" << complete << ")";
#endif
        finalizesc(complete);
    }
}

// commit or purge local state cache
void MegaClient::finalizesc(bool complete)
{
    if (complete)
    {
        cachedscsn = scsn.getHandle();
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
error MegaClient::getfa(handle h, string *fileattrstring, const string &nodekey, fatype t, int cancel)
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
            *fafcp = new FileAttributeFetchChannel(this);
        }

        if (!(*fafcp)->fafs[1].count(fah))
        {
            (*fafcp)->fahref = fah;

            // map returned handle to type/node upon retrieval response
            FileAttributeFetch** fafp = &(*fafcp)->fafs[0][fah];

            if (!*fafp)
            {
                *fafp = new FileAttributeFetch(h, nodekey, t, reqtag);
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

    for (fa_map::iterator it = pendingfa.lower_bound(pair<handle, fatype>(h, fatype(0)));
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
void MegaClient::putfa(handle th, fatype t, SymmCipher* key, std::unique_ptr<string> data, bool checkAccess)
{
    // CBC-encrypt attribute data (padded to next multiple of BLOCKSIZE)
    data->resize((data->size() + SymmCipher::BLOCKSIZE - 1) & -SymmCipher::BLOCKSIZE);
    key->cbc_encrypt((byte*)data->data(), data->size());

    queuedfa.push_back(new HttpReqCommandPutFA(this, th, t, std::move(data), checkAccess));
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
    return !mBlocked && tslots.size() < MAXTOTALTRANSFERS;
}

bool MegaClient::setstoragestatus(storagestatus_t status)
{
    // transition from paywall to red should not happen
    assert(status != STORAGE_RED || ststatus != STORAGE_PAYWALL);

    if (ststatus != status && (status != STORAGE_RED || ststatus != STORAGE_PAYWALL))
    {
        storagestatus_t pststatus = ststatus;
        ststatus = status;
        if (pststatus == STORAGE_PAYWALL)
        {
            mOverquotaDeadlineTs = 0;
            mOverquotaWarningTs.clear();
        }
        app->notify_storage(ststatus);
        if (pststatus == STORAGE_RED || pststatus == STORAGE_PAYWALL)
        {
            abortbackoff(true);
        }
        return true;
    }
    return false;
}

void MegaClient::getpubliclinkinfo(handle h)
{
    reqs.add(new CommandFolderLinkInfo(this, h));
}

error MegaClient::smsverificationsend(const string& phoneNumber, bool reVerifyingWhitelisted)
{
    if (!CommandSMSVerificationSend::isPhoneNumber(phoneNumber))
    {
        return API_EARGS;
    }

    reqs.add(new CommandSMSVerificationSend(this, phoneNumber, reVerifyingWhitelisted));
    if (reVerifyingWhitelisted)
    {
        reqs.add(new CommandGetUserData(this));
    }

    return API_OK;
}

error MegaClient::smsverificationcheck(const std::string &verificationCode)
{
    if (!CommandSMSVerificationCheck::isVerificationCode(verificationCode))
    {
        return API_EARGS;
    }

    reqs.add(new CommandSMSVerificationCheck(this, verificationCode));

    return API_OK;
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
                    bool notify = false;

                    if ((n = nodebyhandle(h)))
                    {
                        if (u && n->owner != u)
                        {
                            n->owner = u;
                            n->changed.owner = true;
                            notify = true;
                        }

                        if (a && ((n->attrstring && strcmp(n->attrstring->c_str(), a)) || !n->attrstring))
                        {
                            if (!n->attrstring)
                            {
                                n->attrstring.reset(new string);
                            }
                            Node::copystring(n->attrstring.get(), a);
                            n->changed.attrs = true;
                            notify = true;
                        }

                        if (ts != -1 && n->ctime != ts)
                        {
                            n->ctime = ts;
                            n->changed.ctime = true;
                            notify = true;
                        }

                        n->applykey();
                        n->setattr();

                        if (notify)
                        {
                            notifynode(n);
                        }
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
                    readnodes(j, 1, PUTNODES_APP, NULL, 0, false);
                    break;

                case MAKENAMEID2('f', '2'):
                    readnodes(j, 1, PUTNODES_APP, NULL, 0, false);
                    break;

                case 'u':
                    readusers(j, true);
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
handle MegaClient::sc_newnodes()
{
    handle originatingUser = UNDEF;
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 't':
                readtree(&jsonsc);
                break;

            case 'u':
                readusers(&jsonsc, true);
                break;

            case MAKENAMEID2('o', 'u'):
                originatingUser = jsonsc.gethandle(USERHANDLE);
                break;

            case EOO:
                return originatingUser;

            default:
                if (!jsonsc.storeobject())
                {
                    return originatingUser;
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
    handle ou = UNDEF;
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

            case MAKENAMEID2('o', 'u'):
                ou = jsonsc.gethandle(USERHANDLE);
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
                outbound = (oh == me);
                if (!ISUNDEF(oh) && !outbound && (uh != me))
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
                        if (!outbound && statecurrent)
                        {
                            User* u = finduser(oh);
                            // only new shares should be notified (skip permissions changes)
                            bool newShare = u && u->sharing.find(h) == u->sharing.end();
                            if (newShare)
                            {
                                useralerts.add(new UserAlert::NewShare(h, oh, u->email, ts, useralerts.nextId()));
                                useralerts.ignoreNextSharedNodesUnder(h);  // no need to alert on nodes already in the new share, which are delivered next
                            }
                        }

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
                        handle peer = outbound ? uh : oh;
                        if (peer != me && peer && !ISUNDEF(peer) && statecurrent && ou != me)
                        {
                            User* u = finduser(peer);
                            useralerts.add(new UserAlert::DeletedShare(peer, u ? u->email : "", oh, h, ts == 0 ? m_time() : ts, useralerts.nextId()));
                        }

                        // share revocation or share without key
                        newshares.push_back(new NewShare(h, outbound,
                                                         peer, r, 0, NULL, NULL, p, false, okremoved));
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
    int proNumber = 0;
    int itemclass = 0;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('i', 't'):
                itemclass = int(jsonsc.getint()); // itemclass. For now, it's always 0.
                break;

            case 'p':
                proNumber = int(jsonsc.getint()); //pro type
                break;

            case 'r':
                jsonsc.storeobject(&result);
                if (result == "s")
                {
                   success = true;
                }
                break;

            case EOO:
                if (itemclass == 0 && statecurrent)
                {
                    useralerts.add(new UserAlert::Payment(success, proNumber, m_time(), useralerts.nextId()));
                }
                return success;

            default:
                if (!jsonsc.storeobject())
                {
                    return false;
                }
        }
    }
}

void MegaClient::sc_paymentreminder()
{
    m_time_t expiryts = 0;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
        case MAKENAMEID2('t', 's'):
            expiryts = int(jsonsc.getint()); // timestamp
            break;

        case EOO:
            if (statecurrent)
            {
                useralerts.add(new UserAlert::PaymentReminder(expiryts ? expiryts : m_time(), useralerts.nextId()));
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

// user/contact updates come in the following format:
// u:[{c/m/ts}*] - Add/modify user/contact
void MegaClient::sc_contacts()
{
    handle ou = UNDEF;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'u':
                useralerts.startprovisional();
                readusers(&jsonsc, true);
                break;

            case MAKENAMEID2('o', 'u'):
                ou = jsonsc.gethandle(MegaClient::USERHANDLE);
                break;

            case EOO:
                useralerts.evalprovisional(ou);
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
                if (!ISUNDEF(h = jsonsc.gethandle()) && (n = nodebyhandle(h)) && n->sharekey)
                {
                    kshares.push_back(n);   // n->inshare is checked in cr_response
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
                else if (ualist.size() == uavlist.size())
                {
                    assert(ualist.size() && uavlist.size());

                    // invalidate only out-of-date attributes
                    for (itua = ualist.begin(), ituav = uavlist.begin();
                         itua != ualist.end();
                         itua++, ituav++)
                    {
                        attr_t type = User::string2attr(itua->c_str());
                        const string *cacheduav = u->getattrversion(type);
                        if (cacheduav)
                        {
                            if (*cacheduav != *ituav)
                            {
                                u->invalidateattr(type);
                                switch(type)
                                {
                                    case ATTR_KEYRING:
                                    {
                                        resetKeyring();
                                        break;
                                    }
                                    case ATTR_AUTHRING:     // fall-through
                                    case ATTR_AUTHCU255:    // fall-through
                                    case ATTR_AUTHRSA:
                                    {
                                        LOG_debug << User::attr2string(type) << " has changed externally. Fetching...";
                                        mAuthRings.erase(type);
                                        getua(u, type, 0);
                                        break;
                                    }
                                    default:
                                        break;
                                }
                            }
                            else
                            {
                                LOG_info << "User attribute already up to date";
                                return;
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

                        if (!fetchingnodes)
                        {
                            // silently fetch-upon-update these critical attributes
                            if (type == ATTR_DISABLE_VERSIONS || type == ATTR_PUSH_SETTINGS)
                            {
                                getua(u, type, 0);
                            }
                            else if (type == ATTR_STORAGE_STATE)
                            {
                                if (!statecurrent)
                                {
                                    notifyStorageChangeOnStateCurrent = true;
                                }
                                else
                                {
                                    LOG_debug << "Possible storage status change";
                                    app->notify_storage(STORAGE_CHANGE);
                                }
                            }
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
    m_off_t clv = 0;
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
            case MAKENAMEID3('c', 'l', 'v'):
                clv = jsonsc.getint();
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

                if (m && statecurrent)
                {
                    string email;
                    Node::copystring(&email, m);
                    useralerts.add(new UserAlert::IncomingPendingContact(dts, rts, p, email, ts, useralerts.nextId()));
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
                    pcr->autoaccepted = clv;
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
void MegaClient::sc_upc(bool incoming)
{
    // fields: p, uts, s, m
    m_time_t uts = 0;
    int s = 0;
    const char *m = NULL;
    handle p = UNDEF, ou = UNDEF;
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
                s = int(jsonsc.getint());
                break;
            case 'p':
                p = jsonsc.gethandle(MegaClient::PCRHANDLE);
                break;
            case MAKENAMEID2('o', 'u'):
                ou = jsonsc.gethandle(MegaClient::PCRHANDLE);
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

                if (statecurrent && ou != me && (incoming || s != 2))
                {
                    string email;
                    Node::copystring(&email, m);
                    using namespace UserAlert;
                    useralerts.add(incoming ? (Base*) new UpdatedPendingContactIncoming(s, p, email, uts, useralerts.nextId())
                                            : (Base*) new UpdatedPendingContactOutgoing(s, p, email, uts, useralerts.nextId()));
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
    bool reinstated = false;
    m_time_t ets = 0;
    m_time_t cts = 0;
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
        case MAKENAMEID4('d', 'o', 'w', 'n'):
            {
                int down = int(jsonsc.getint());
                takendown = (down == 1);
                reinstated = (down == 0);
            }
            break;
        case MAKENAMEID3('e', 't', 's'):
            ets = jsonsc.getint();
            break;
        case MAKENAMEID2('t', 's'):
            cts = jsonsc.getint();
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
            if (!deleted && !cts)
            {
                LOG_err << "creation timestamp element not provided";
                break;
            }

            n = nodebyhandle(h);
            if (n)
            {
                if ((takendown || reinstated) && !ISUNDEF(h) && statecurrent)
                {
                    useralerts.add(new UserAlert::Takedown(takendown, reinstated, n->type, h, m_time(), useralerts.nextId()));
                }

                if (deleted)        // deletion
                {
                    if (n->plink)
                    {
                        mPublicLinks.erase(n->nodehandle);
                        delete n->plink;
                        n->plink = NULL;
                    }
                }
                else
                {
                    n->setpubliclink(ph, cts, ets, takendown);
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
            status = int(jsonsc.getint());
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
void MegaClient::sc_chatupdate(bool readingPublicChat)
{
    // fields: id, u, cs, n, g, ou, ct, ts, m, ck
    handle chatid = UNDEF;
    userpriv_vector *userpriv = NULL;
    int shard = -1;
    userpriv_vector *upnotif = NULL;
    bool group = false;
    handle ou = UNDEF;
    string title;
    m_time_t ts = -1;
    bool publicchat = false;
    string unifiedkey;

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
                shard = int(jsonsc.getint());
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

            case 'm':
                assert(readingPublicChat);
                publicchat = jsonsc.getint();
                break;

            case MAKENAMEID2('c','k'):
                assert(readingPublicChat);
                jsonsc.storeobject(&unifiedkey);
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
                    bool mustHaveUK = false;
                    privilege_t oldPriv = PRIV_UNKNOWN;
                    if (chats.find(chatid) == chats.end())
                    {
                        chats[chatid] = new TextChat();
                        mustHaveUK = true;
                    }
                    else
                    {
                        oldPriv = chats[chatid]->priv;
                    }

                    TextChat *chat = chats[chatid];
                    chat->id = chatid;
                    chat->shard = shard;
                    chat->group = group;
                    chat->priv = PRIV_UNKNOWN;
                    chat->ou = ou;
                    chat->title = title;
                    // chat->flags = ?; --> flags are received in other AP: mcfc
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
                                mustHaveUK = (oldPriv <= PRIV_RM && upvit->second > PRIV_RM);
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
                                mustHaveUK = (oldPriv <= PRIV_RM && upvit->second > PRIV_RM);
                                chat->priv = upvit->second;
                                break;
                            }
                        }
                    }

                    if (chat->priv == PRIV_RM)
                    {
                        // clear the list of peers because API still includes peers in the
                        // actionpacket, but not in a fresh fetchnodes
                        delete userpriv;
                        userpriv = NULL;
                    }

                    delete chat->userpriv;  // discard any existing `userpriv`
                    chat->userpriv = userpriv;

                    if (readingPublicChat)
                    {
                        chat->setMode(publicchat);
                        if (!unifiedkey.empty())    // not all actionpackets include it
                        {
                            chat->unifiedKey = unifiedkey;
                        }
                        else if (mustHaveUK)
                        {
                            LOG_err << "Public chat without unified key detected";
                        }
                    }

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
                    textchat_map::iterator it = chats.find(chatid);
                    if (it == chats.end())
                    {
                        LOG_err << "Unknown chat for user/node access to attachment";
                        return;
                    }

                    TextChat *chat = it->second;
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

void MegaClient::sc_chatflags()
{
    bool done = false;
    handle chatid = UNDEF;
    byte flags = 0;
    while(!done)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('i','d'):
                chatid = jsonsc.gethandle(MegaClient::CHATHANDLE);
                break;

            case 'f':
                flags = byte(jsonsc.getint());
                break;

            case EOO:
            {
                done = true;
                textchat_map::iterator it = chats.find(chatid);
                if (it == chats.end())
                {
                    string chatidB64;
                    string tmp((const char*)&chatid, sizeof(chatid));
                    Base64::btoa(tmp, chatidB64);
                    LOG_err << "Received flags for unknown chatid: " << chatidB64.c_str();
                    break;
                }

                TextChat *chat = chats[chatid];
                chat->setFlags(flags);

                chat->setTag(0);    // external change
                notifychat(chat);
                break;
            }

            default:
                if (!jsonsc.storeobject())
                {
                    return;
                }
                break;
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

void MegaClient::sc_la()
{
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
        case EOO:
            useralerts.onAcknowledgeReceived();
            return;

        default:
            if (!jsonsc.storeobject())
            {
                LOG_warn << "Failed to parse `la` action packet";
                return;
            }
        }
    }
}

void MegaClient::sc_ub()
{
    BizStatus status = BIZ_STATUS_UNKNOWN;
    BizMode mode = BIZ_MODE_UNKNOWN;
    BizStatus prevBizStatus = mBizStatus;
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 's':
                status = BizStatus(jsonsc.getint());
                break;

            case 'm':
                mode = BizMode(jsonsc.getint());
                break;

            case EOO:
                if ((status < BIZ_STATUS_EXPIRED || status > BIZ_STATUS_GRACE_PERIOD))
                {
                    std::string err = "Missing or invalid status in `ub` action packet";
                    LOG_err << err;
                    sendevent(99449, err.c_str(), 0);
                    return;
                }
                if ( (mode != BIZ_MODE_MASTER && mode != BIZ_MODE_SUBUSER)
                     && (status != BIZ_STATUS_INACTIVE) )   // when inactive, `m` might be missing (unknown/undefined)
                {
                    LOG_err << "Unexpected mode for business account at `ub`. Mode: " << mode;
                    return;
                }

                mBizStatus = status;
                mBizMode = mode;

                if (mBizMode != BIZ_MODE_UNKNOWN)
                {
                    LOG_info << "Disable achievements for business account type";
                    achievements_enabled = false;
                }

                // FIXME: if API decides to include the expiration ts, remove the block below
                if (mBizStatus == BIZ_STATUS_ACTIVE)
                {
                    // If new status is active, reset timestamps of transitions
                    mBizGracePeriodTs = 0;
                    mBizExpirationTs = 0;
                }

                app->notify_business_status(mBizStatus);
                if (prevBizStatus == BIZ_STATUS_INACTIVE)
                {
                    app->account_updated();
                    getuserdata();  // update account flags
                }

                return;

            default:
                if (!jsonsc.storeobject())
                {
                    LOG_warn << "Failed to parse `ub` action packet";
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

    if (scsn.ready()) tscsn = scsn.getHandle();

    if (nodenotify.size() || usernotify.size() || pcrnotify.size()
#ifdef ENABLE_CHAT
            || chatnotify.size()
#endif
            || cachedscsn != tscsn)
    {

        if (scsn.ready())
        {
            // in case of CS operations inbetween login and fetchnodes, don't 
            // write these to the database yet, as we don't have the scsn
            updatesc();
        }

#ifdef ENABLE_SYNC
        // update LocalNode <-> Node associations
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            (*it)->cachenodes();
        }
#endif
    }

    if ((t = int(nodenotify.size())))
    {
#ifdef ENABLE_SYNC
        // check for deleted syncs
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            if (((*it)->state == SYNC_ACTIVE || (*it)->state == SYNC_INITIALSCAN)
             && (*it)->localroot->node->changed.removed)
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
                // make this just a warning to avoid auto test failure
                // this can happen if another client adds a folder in our share and the key for us is not available yet
                LOG_warn << "NO_KEY node: " << n->type << " " << n->size << " " << n->nodehandle << " " << n->nodekeyUnchecked().size();
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

    if ((t = int(pcrnotify.size())))
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
    if ((t = int(usernotify.size())))
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
                        sendevent(99435, "Orphan incoming share", 0);
                    }
                }
                u->sharing.clear();

                discarduser(u->userhandle, false);
            }
        }

        usernotify.clear();
    }

    if ((t = int(useralerts.useralertnotify.size())))
    {
        LOG_debug << "Notifying " << t << " user alerts";
        app->useralerts_updated(&useralerts.useralertnotify[0], t);

        for (i = 0; i < t; i++)
        {
            UserAlert::Base *ua = useralerts.useralertnotify[i];
            ua->tag = -1;
        }

        useralerts.useralertnotify.clear();
    }

#ifdef ENABLE_CHAT
    if ((t = int(chatnotify.size())))
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
    handle originatingUser = UNDEF;

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

            case MAKENAMEID2('o', 'u'):
                originatingUser = jsonsc.gethandle(USERHANDLE);
                break;

            case EOO:
                if (n)
                {
                    TreeProcDel td;
                    useralerts.beginNotingSharedNodes();

                    int creqtag = reqtag;
                    reqtag = 0;
                    proctree(n, &td);
                    reqtag = creqtag;
                    
                    useralerts.convertNotedSharedNodes(false, originatingUser);
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
        l = int(strlen(json));
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

void MegaClient::makeattr(SymmCipher* key, const std::unique_ptr<string>& attrstring, const char* json, int l) const
{
    makeattr(key, attrstring.get(), json, l);
}

// update node attributes
// (with speculative instant completion)
error MegaClient::setattr(Node* n, const char *prevattr)
{
    if (ststatus == STORAGE_PAYWALL)
    {
        return API_EPAYWALL;
    }

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

void MegaClient::putnodes_prepareOneFolder(NewNode* newnode, std::string foldername)
{
    string attrstring;
    byte buf[FOLDERNODEKEYLENGTH];

    // set up new node as folder node
    newnode->source = NEW_NODE;
    newnode->type = FOLDERNODE;
    newnode->nodehandle = 0;
    newnode->parenthandle = UNDEF;

    // generate fresh random key for this folder node
    rng.genblock(buf, FOLDERNODEKEYLENGTH);
    newnode->nodekey.assign((char*)buf, FOLDERNODEKEYLENGTH);
    tmpnodecipher.setkey(buf);

    // generate fresh attribute object with the folder name
    AttrMap attrs;

    fsaccess->normalize(&foldername);
    attrs.map['n'] = foldername;

    // JSON-encode object and encrypt attribute string
    attrs.getjson(&attrstring);
    newnode->attrstring.reset(new string);
    makeattr(&tmpnodecipher, newnode->attrstring, attrstring.c_str());
}

// send new nodes to API for processing
void MegaClient::putnodes(handle h, vector<NewNode>&& newnodes, const char *cauth)
{
    reqs.add(new CommandPutNodes(this, h, NULL, move(newnodes), reqtag, PUTNODES_APP, cauth));
}

// drop nodes into a user's inbox (must have RSA keypair)
void MegaClient::putnodes(const char* user, vector<NewNode>&& newnodes)
{
    User* u;

    restag = reqtag;

    if (!(u = finduser(user, 0)) && !user)
    {
        return app->putnodes_result(API_EARGS, USER_HANDLE, newnodes);
    }

    queuepubkeyreq(user, ::mega::make_unique<PubKeyActionPutNodes>(move(newnodes), reqtag));
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
// API_ECIRCULAR otherwise. Also returns API_EPAYWALL if in PAYWALL.
error MegaClient::checkmove(Node* fn, Node* tn)
{
    // precondition #0: not in paywall
    if (ststatus == STORAGE_PAYWALL)
    {
        return API_EPAYWALL;
    }

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

    // condition #4: source can't be a version
    if (fn->parent->type == FILENODE)
    {
        return API_EACCESS;
    }

    // condition #5: tn must not be below fn (would create circular linkage)
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

    // condition #6: fn and tn must be in the same tree (same ultimate parent
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
error MegaClient::rename(Node* n, Node* p, syncdel_t syncdel, handle prevparent, const char *newName)
{
    error e;

    if ((e = checkmove(n, p)))
    {
        return e;
    }

    Node *prevParent = NULL;
    if (!ISUNDEF(prevparent))
    {
        prevParent = nodebyhandle(prevparent);
    }
    else
    {
        prevParent = n->parent;
    }

    if (n->setparent(p))
    {
        bool updateNodeAttributes = false;
        if (prevParent)
        {
            Node *prevRoot = getrootnode(prevParent);
            Node *newRoot = getrootnode(p);
            handle rubbishHandle = rootnodes[RUBBISHNODE - ROOTNODE];
            nameid rrname = AttrMap::string2nameid("rr");

            if (prevRoot->nodehandle != rubbishHandle
                    && p->nodehandle == rubbishHandle)
            {
                // deleted node
                char base64Handle[12];
                Base64::btoa((byte*)&prevParent->nodehandle, MegaClient::NODEHANDLE, base64Handle);
                if (strcmp(base64Handle, n->attrs.map[rrname].c_str()))
                {
                    LOG_debug << "Adding rr attribute";
                    n->attrs.map[rrname] = base64Handle;
                    updateNodeAttributes = true;
                }
            }
            else if (prevRoot->nodehandle == rubbishHandle
                     && newRoot->nodehandle != rubbishHandle)
            {
                // undeleted node
                attr_map::iterator it = n->attrs.map.find(rrname);
                if (it != n->attrs.map.end())
                {
                    LOG_debug << "Removing rr attribute";
                    n->attrs.map.erase(it);
                    updateNodeAttributes = true;
                }
            }
        }

        if (newName)
        {
            string name(newName);
            fsaccess->normalize(&name);
            n->attrs.map['n'] = name;
            updateNodeAttributes = true;
        }

        n->changed.parent = true;
        n->tag = reqtag;
        notifynode(n);

        // rewrite keys of foreign nodes that are moved out of an outbound share
        rewriteforeignkeys(n);

        reqs.add(new CommandMoveNode(this, n, p, syncdel, prevparent));
        if (updateNodeAttributes)
        {
            setattr(n);
        }
    }

    return API_OK;
}

// delete node tree
error MegaClient::unlink(Node* n, bool keepversions, int tag, std::function<void(handle, error)> resultFunction)
{
    if (!n->inshare && !checkaccess(n, FULL))
    {
        return API_EACCESS;
    }

    if (mBizStatus > BIZ_STATUS_INACTIVE
            && mBizMode == BIZ_MODE_SUBUSER && n->inshare
            && mBizMasters.find(n->inshare->user->userhandle) != mBizMasters.end())
    {
        // business subusers cannot leave inshares from master biz users
        return API_EMASTERONLY;
    }

    if (ststatus == STORAGE_PAYWALL)
    {
        return API_EPAYWALL;
    }

    bool kv = (keepversions && n->type == FILENODE);
    reqs.add(new CommandDelNode(this, n->nodehandle, kv, tag, resultFunction));

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

// Converts a string in UTF8 to array of int32 in the same way than Webclient converts a string in UTF16 to array of 32-bit elements
// (returns NULL if the input is invalid UTF-8)
// unfortunately, discards bits 8-31 of multibyte characters for backwards compatibility
char* MegaClient::utf8_to_a32forjs(const char* str, int* len)
{
    if (!str)
    {
        return NULL;
    }

    int t = int(strlen(str));
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

    if (!(pw = utf8_to_a32forjs(utf8pw, &t)))
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

    tolower_string(*s);
    stringhash(s->c_str(), hash, c);

    return MemAccess::get<uint64_t>((const char*)hash);
}

// read and add/verify node array
int MegaClient::readnodes(JSON* j, int notify, putsource_t source, vector<NewNode>* nn, int tag, bool applykeys)
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
                    nni = int(j->getint());
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
                            sendevent(99437, "Node inconsistency", 0);
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
                    Node::copystring(n->attrstring.get(), a);
                    n->setkeyfromjson(k);
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
                    ts = m_time();
                }

                if (!(sts + 1))
                {
                    sts = ts;
                }

                n = new Node(this, &dp, h, ph, t, s, u, fas.c_str(), ts);
                n->changed.newnode = true;

                n->tag = tag;

                n->attrstring.reset(new string);
                Node::copystring(n->attrstring.get(), a);
                n->setkeyfromjson(k);

                if (!ISUNDEF(su))
                {
                    newshares.push_back(new NewShare(h, 0, su, rl, sts, sk ? buf : NULL));
                }

                if (u != me && !ISUNDEF(u) && !fetchingnodes)
                {
                    useralerts.noteSharedNode(u, t, ts, n);
                }

                if (nn && nni >= 0 && nni < int(nn->size()))
                {
                    auto& nn_nni = (*nn)[nni];
                    nn_nni.added = true;
                    nn_nni.mAddedHandle = h;

#ifdef ENABLE_SYNC
                    if (source == PUTNODES_SYNC)
                    {
                        if (nn_nni.localnode)
                        {
                            // overwrites/updates: associate LocalNode with newly created Node
                            nn_nni.localnode->setnode(n);
                            nn_nni.localnode->treestate(TREESTATE_SYNCED);

                            // updates cache with the new node associated
                            nn_nni.localnode->sync->statecacheadd(nn_nni.localnode);
                            nn_nni.localnode->newnode.reset(); // localnode ptr now null also
                        }
                    }
#endif

                    if (nn_nni.source == NEW_UPLOAD)
                    {
                        handle uh = nn_nni.uploadhandle;

                        // do we have pending file attributes for this upload? set them.
                        for (fa_map::iterator it = pendingfa.lower_bound(pair<handle, fatype>(uh, fatype(0)));
                             it != pendingfa.end() && it->first.first == uh; )
                        {
                            reqs.add(new CommandAttachFA(this, h, it->first.second, it->second.first, it->second.second));
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

            if (applykeys)
            {
                n->applykey();
            }
        }
    }

    // any child nodes that arrived before their parents?
    for (size_t i = dp.size(); i--; )
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
            const char *m = NULL;
            const char *msg = NULL;
            handle p = UNDEF;

            bool done = false;
            while (!done)
            {
                switch (j->getnameid()) {
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

error MegaClient::readmiscflags(JSON *json)
{
    while (1)
    {
        switch (json->getnameid())
        {
        // mcs:1 --> MegaChat enabled
        case MAKENAMEID3('a', 'c', 'h'):
            achievements_enabled = bool(json->getint());    //  Mega Achievements enabled
            break;
        case MAKENAMEID4('m', 'f', 'a', 'e'):   // multi-factor authentication enabled
            gmfa_enabled = bool(json->getint());
            break;
        case MAKENAMEID4('s', 's', 'r', 's'):   // server-side rubish-bin scheduler (only available when logged in)
            ssrs_enabled = bool(json->getint());
            break;
        case MAKENAMEID4('n', 's', 'r', 'e'):   // new secure registration enabled
            nsr_enabled = bool(json->getint());
            break;
        case MAKENAMEID5('a', 'p', 'l', 'v', 'p'):   // apple VOIP push enabled (only available when logged in)
            aplvp_enabled = bool(json->getint());
            break;
        case MAKENAMEID5('s', 'm', 's', 'v', 'e'):   // 2 = Opt-in and unblock SMS allowed 1 = Only unblock SMS allowed 0 = No SMS allowed
            mSmsVerificationState = static_cast<SmsVerificationState>(json->getint());
            break;
        case MAKENAMEID4('n', 'l', 'f', 'e'):   // new link format enabled
            mNewLinkFormat = static_cast<bool>(json->getint());
            break;
        case EOO:
            return API_OK;
        default:
            if (!json->storeobject())
            {
                return API_EINTERNAL;
            }
        }
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
            m_time_t cts = 0;
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
                    case MAKENAMEID2('t', 's'):
                        cts = j->getint();
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
                        if (!cts)
                        {
                            LOG_err << "creation timestamp element not provided";
                            break;
                        }

                        n = nodebyhandle(h);
                        if (n)
                        {
                            n->setpubliclink(ph, cts, ets, takendown);
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

void MegaClient::applykeys()
{
    CodeCounter::ScopeTimer ccst(performanceStats.applyKeys);

    int noKeyExpected = (rootnodes[0] != UNDEF) + (rootnodes[1] != UNDEF) + (rootnodes[2] != UNDEF);

    if (nodes.size() > size_t(mAppliedKeyNodeCount + noKeyExpected))
    {
        for (auto& it : nodes)
        {
            it.second->applykey();
        }
    }

    sendkeyrewrites();
}

void MegaClient::sendkeyrewrites()
{
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
}

// user/contact list
bool MegaClient::readusers(JSON* j, bool actionpackets)
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
        BizMode bizMode = BIZ_MODE_UNKNOWN;

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

                case 'm':   // email
                    m = j->getvalue();
                    break;

                case MAKENAMEID2('t', 's'):
                    ts = j->getint();
                    break;

                case 'b':
                {
                    if (j->enterobject())
                    {
                        nameid businessName;
                        while ((businessName = j->getnameid()) != EOO)
                        {
                            switch (businessName)
                            {
                                case 'm':
                                    bizMode = static_cast<BizMode>(j->getint());
                                    break;
                                default:
                                    if (!j->storeobject())
                                        return false;
                                    break;
                            }
                        }

                        j->leaveobject();
                    }

                    break;
                }

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
            if (actionpackets && v >= 0 && v <= 3 && statecurrent)
            {
                string email;
                Node::copystring(&email, m);
                useralerts.add(new UserAlert::ContactChange(v, uh, email, ts, useralerts.nextId()));
            }
            User* u = finduser(uh, 0);
            bool notify = !u;
            if (u || (u = finduser(uh, 1)))
            {
                const string oldEmail = u->email;
                mapuser(uh, m);

                u->mBizMode = bizMode;

                if (v != VISIBILITY_UNKNOWN)
                {
                    if (u->show != v || u->ctime != ts)
                    {
                        if (u->show == HIDDEN && v == VISIBLE)
                        {
                            u->invalidateattr(ATTR_FIRSTNAME);
                            u->invalidateattr(ATTR_LASTNAME);
                            if (oldEmail != u->email)
                            {
                                u->changed.email = true;
                            }
                        }
                        else if (u->show == VISIBILITY_UNKNOWN && v == VISIBLE
                                 && uh != me
                                 && !fetchingnodes)
                        {
                            // new user --> fetch keys
                            fetchContactKeys(u);
                        }

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

// Supported formats:
//   - file links:      #!<ph>[!<key>]
//                      <ph>[!<key>]
//                      /file/<ph>[<params>][#<key>]
//
//   - folder links:    #F!<ph>[!<key>]
//                      /folder/<ph>[<params>][#<key>]
error MegaClient::parsepubliclink(const char* link, handle& ph, byte* key, bool isFolderLink)
{
    bool isFolder;
    const char* ptr = nullptr;
    if ((ptr = strstr(link, "#F!")))
    {
        ptr += 3;
        isFolder = true;
    }
    else if ((ptr = strstr(link, "folder/")))
    {
        ptr += 7;
        isFolder = true;
    }
    else if ((ptr = strstr(link, "#!")))
    {
        ptr += 2;
        isFolder = false;
    }
    else if ((ptr = strstr(link, "file/")))
    {
        ptr += 5;
        isFolder = false;
    }
    else    // legacy file link format without '#'
    {
        ptr = link;
        isFolder = false;
    }

    if (isFolder != isFolderLink)
    {
        return API_EARGS;   // type of link mismatch
    }

    if (strlen(ptr) < 8)  // no public handle in the link
    {
        return API_EARGS;
    }

    ph = 0; //otherwise atob will give an unexpected result
    if (Base64::atob(ptr, (byte*)&ph, NODEHANDLE) == NODEHANDLE)
    {
        ptr += 8;

        // skip any tracking parameter introduced by third-party websites
        while(*ptr && *ptr != '!' && *ptr != '#')
        {
            ptr++;
        }

        if (!*ptr || ((*ptr == '#' || *ptr == '!') && *(ptr + 1) == '\0'))   // no key provided
        {
            return API_EINCOMPLETE;
        }

        if (*ptr == '!' || *ptr == '#')
        {
            const char *k = ptr + 1;    // skip '!' or '#' separator
            int keylen = isFolderLink ? FOLDERNODEKEYLENGTH : FILENODEKEYLENGTH;
            if (Base64::atob(k, key, keylen) == keylen)
            {
                return API_OK;
            }
        }
    }

    return API_EARGS;
}

error MegaClient::folderaccess(const char *folderlink)
{
    handle h = UNDEF;
    byte folderkey[FOLDERNODEKEYLENGTH];

    error e;
    if ((e = parsepubliclink(folderlink, h, folderkey, true)) == API_OK)
    {
        setrootnode(h);
        key.setkey(folderkey);
    }

    return e;
}

void MegaClient::prelogin(const char *email)
{
    reqs.add(new CommandPrelogin(this, email));
}

// create new session
void MegaClient::login(const char* email, const byte* pwkey, const char* pin)
{
    string lcemail(email);

    key.setkey((byte*)pwkey);

    uint64_t emailhash = stringhash64(&lcemail, &key);

    byte sek[SymmCipher::KEYLENGTH];
    rng.genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, email, (byte*)&emailhash, sizeof(emailhash), sek, 0, pin));
}

// create new session (v2)
void MegaClient::login2(const char *email, const char *password, string *salt, const char *pin)
{
    string bsalt;
    Base64::atob(*salt, bsalt);

    byte derivedKey[2 * SymmCipher::KEYLENGTH];
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;
    pbkdf2.DeriveKey(derivedKey, sizeof(derivedKey), 0, (byte *)password, strlen(password),
                     (const byte *)bsalt.data(), bsalt.size(), 100000);

    login2(email, derivedKey, pin);
}

void MegaClient::login2(const char *email, const byte *derivedKey, const char* pin)
{
    key.setkey((byte*)derivedKey);
    const byte *authKey = derivedKey + SymmCipher::KEYLENGTH;

    byte sek[SymmCipher::KEYLENGTH];
    rng.genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, email, authKey, SymmCipher::KEYLENGTH, sek, 0, pin));
}

void MegaClient::fastlogin(const char* email, const byte* pwkey, uint64_t emailhash)
{
    key.setkey((byte*)pwkey);

    byte sek[SymmCipher::KEYLENGTH];
    rng.genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, email, (byte*)&emailhash, sizeof(emailhash), sek));
}

void MegaClient::getuserdata()
{
    cachedug = false;
    reqs.add(new CommandGetUserData(this));
}

void MegaClient::getmiscflags()
{
    reqs.add(new CommandGetMiscFlags(this));
}

void MegaClient::getpubkey(const char *user)
{
    queuepubkeyreq(user, ::mega::make_unique<PubKeyActionNotifyApp>(reqtag));
}

// resume session - load state from local cache, if available
void MegaClient::login(const byte* session, int size)
{   
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
        rng.genblock(sek, sizeof sek);

        reqs.add(new CommandLogin(this, NULL, NULL, 0, sek, sessionversion));
        getuserdata();
        fetchtimezone();
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
        cipher.setkey((const byte *)sessionkey.data(), int(sessionkey.size()));
        cipher.ecb_encrypt(key.key, k);
        memcpy(session, k, sizeof k);
    }
    else
    {
        size = sid.size() + sizeof key.key;
        memcpy(session, key.key, sizeof key.key);
    }

    memcpy(session + sizeof key.key, sid.data(), sid.size());
    
    return int(size);
}

void MegaClient::resendverificationemail()
{
    reqs.add(new CommandResendVerificationEmail(this));
}

void MegaClient::resetSmsVerifiedPhoneNumber()
{
    reqs.add(new CommandResetSmsVerifiedPhoneNumber(this));
}

void MegaClient::copysession()
{
    reqs.add(new CommandCopySession(this));
}

string *MegaClient::sessiontransferdata(const char *url, string *session)
{
    if (!session && loggedin() != FULLACCOUNT)
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
        sids.resize(Base64::btoa((byte *)sid.data(), int(sid.size()), (char *)sids.data()));
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
    base64->resize(Base64::btoa((byte *)json.data(), int(json.size()), (char *)base64->data()));
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
        else if (loggedinfolderlink())
        {
            dbname.resize(NODEHANDLE * 4 / 3 + 3);
            dbname.resize(Base64::btoa((const byte*)&publichandle, NODEHANDLE, (char*)dbname.c_str()));
        }

        if (dbname.size())
        {
            sctable = dbaccess->open(rng, fsaccess, &dbname, false, false);
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
    tolower_string(nuid);

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
    tolower_string(nuid);

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

void MegaClient::discarduser(handle uh, bool discardnotified)
{
    User *u = finduser(uh);
    if (!u)
    {
        return;
    }

    while (u->pkrs.size())  // protect any pending pubKey request
    {
        auto& pka = u->pkrs.front();
        if(pka->cmd)
        {
            pka->cmd->invalidateUser();
        }
        pka->proc(this, u);
        u->pkrs.pop_front();
    }

    if (discardnotified)
    {
        discardnotifieduser(u);
    }

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
        auto& pka = u->pkrs.front();
        if(pka->cmd)
        {
            pka->cmd->invalidateUser();
        }
        pka->proc(this, u);
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
        assert(fetchingnodes);
        // while fetchingnodes, outgoing shares reference an "empty" PCR that is completed when `opc` is parsed
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
                    queuepubkeyreq(u, ::mega::make_unique<PubKeyActionSendShareKey>(sh));
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
void MegaClient::queuepubkeyreq(User* u, std::unique_ptr<PubKeyAction> pka)
{
    if (!u || u->pubk.isvalid())
    {
        restag = pka->tag;
        pka->proc(this, u);
    }
    else
    {
        u->pkrs.push_back(std::move(pka));

        if (!u->pubkrequested)
        {
            u->pkrs.back()->cmd = new CommandPubKeyRequest(this, u);
            reqs.add(u->pkrs.back()->cmd);
            u->pubkrequested = true;
        }
    }
}

void MegaClient::queuepubkeyreq(const char *uid, std::unique_ptr<PubKeyAction> pka)
{
    User *u = finduser(uid, 0);
    if (!u && uid)
    {
        if (strchr(uid, '@'))   // uid is an e-mail address
        {
            string nuid;
            Node::copystring(&nuid, uid);
            tolower_string(nuid);

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

    queuepubkeyreq(u, std::move(pka));
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
    size_t total = n->outshares ? n->outshares->size() : 0;
    total += n->pendingshares ? n->pendingshares->size() : 0;
    if (a == ACCESS_UNKNOWN && total == 1)
    {
        // rewrite keys of foreign nodes located in the outbound share that is getting canceled
        // FIXME: verify that it is really getting canceled to prevent benign premature rewrite
        rewriteforeignkeys(n);
    }

    queuepubkeyreq(user, ::mega::make_unique<PubKeyActionCreateShare>(n->nodehandle, a, reqtag, personal_representation));
}

// Add/delete/remind outgoing pending contact request
void MegaClient::setpcr(const char* temail, opcactions_t action, const char* msg, const char* oemail, handle contactLink)
{
    reqs.add(new CommandSetPendingContact(this, temail, action, msg, oemail, contactLink));
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
                                  handle lastPublicHandle, int phtype, int64_t ts)
{
    reqs.add(new CommandPurchaseAddItem(this, itemclass, item, price, currency, tax, country, lastPublicHandle, phtype, ts));
}

// obtain payment URL for given provider
void MegaClient::purchase_checkout(int gateway)
{
    reqs.add(new CommandPurchaseCheckout(this, gateway));
}

void MegaClient::submitpurchasereceipt(int type, const char *receipt, handle lph, int phtype, int64_t ts)
{
    reqs.add(new CommandSubmitPurchaseReceipt(this, type, receipt, lph, phtype, ts));
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

    string::iterator it = find_if(ccnumber.begin(), ccnumber.end(), char_is_not_digit);
    if (it != ccnumber.end())
    {
        return API_EARGS;
    }

    it = find_if(expm.begin(), expm.end(), char_is_not_digit);
    if (it != expm.end() || atol(expm.c_str()) > 12)
    {
        return API_EARGS;
    }

    it = find_if(expy.begin(), expy.end(), char_is_not_digit);
    if (it != expy.end() || atol(expy.c_str()) < 2015)
    {
        return API_EARGS;
    }

    it = find_if(cv2.begin(), cv2.end(), char_is_not_digit);
    if (it != cv2.end())
    {
        return API_EARGS;
    }


    //Luhn algorithm
    int odd = 1, sum = 0;
    for (size_t i = ccnumber.size(); i--; odd = !odd)
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
    PayCrypter payCrypter(rng);
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
    hash.add((byte *)hashstring, int(strlen(hashstring)));
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
    base64cc.resize(Base64::btoa((byte *)ccenc.data(), int(ccenc.size()), (char *)base64cc.data()));
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
 * @param at Attribute type.
 * @param av Attribute value.
 * @param avl Attribute value length.
 * @param ctag Tag to identify the request at intermediate layer

 */
void MegaClient::putua(attr_t at, const byte* av, unsigned avl, int ctag, handle lastPublicHandle, int phtype, int64_t ts)
{
    string data;

    if (!av)
    {
        if (at == ATTR_AVATAR)  // remove avatar
        {
            data = "none";
        }

        av = (const byte*) data.data();
        avl = unsigned(data.size());
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
        reqs.add(new CommandPutUA(this, at, av, avl, tag, lastPublicHandle, phtype, ts));
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
        attr_t type = it->first;

        if (User::needversioning(type) != 1)
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
 * @param at Attribute type.
 * @param ctag Tag to identify the request at intermediate layer
 */
void MegaClient::getua(User* u, const attr_t at, int ctag)
{
    if (at != ATTR_UNKNOWN)
    {
        // if we can solve those requests locally (cached values)...
        const string *cachedav = u->getattr(at);
        int tag = (ctag != -1) ? ctag : reqtag;

        if (!fetchingkeys && cachedav && u->isattrvalid(at))
        {
            if (User::scope(at) == '*') // private attribute, TLV encoding
            {
                TLVstore *tlv = TLVstore::containerToTLVrecords(cachedav, &key);
                restag = tag;
                app->getua_result(tlv, at);
                delete tlv;
                return;
            }
            else
            {
                restag = tag;
                app->getua_result((byte*) cachedav->data(), unsigned(cachedav->size()), at);
                return;
            }
        }
        else
        {
            reqs.add(new CommandGetUA(this, u->uid.c_str(), at, NULL, tag));
        }
    }
}

void MegaClient::getua(const char *email_handle, const attr_t at, const char *ph, int ctag)
{
    if (email_handle && at != ATTR_UNKNOWN)
    {
        reqs.add(new CommandGetUA(this, email_handle, at, ph,(ctag != -1) ? ctag : reqtag));
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

void MegaClient::senddevcommand(const char *command, const char *email, long long q, int bs, int us)
{
    reqs.add(new CommandSendDevCommand(this, command, email, q, bs, us));
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

            char* buf = new char[n->nodekey().size() * 4 / 3 + 4];
            Base64::btoa((byte *)n->nodekey().data(), int(n->nodekey().size()), buf);

            int changed = 0;
            changed |= (int)n->changed.removed;
            changed |= n->changed.attrs << 1;
            changed |= n->changed.owner << 2;
            changed |= n->changed.ctime << 3;
            changed |= n->changed.fileattrstring << 4;
            changed |= n->changed.inshare << 5;
            changed |= n->changed.outshares << 6;
            changed |= n->changed.pendingshares << 7;
            changed |= n->changed.parent << 8;
            changed |= n->changed.publiclink << 9;
            changed |= n->changed.newnode << 10;

            int attrlen = int(n->attrstring->size());
            string base64attrstring;
            base64attrstring.resize(attrlen * 4 / 3 + 4);
            base64attrstring.resize(Base64::btoa((byte *)n->attrstring->data(), int(n->attrstring->size()), (char *)base64attrstring.data()));

            char report[512];
            Base64::btoa((const byte *)&n->nodehandle, MegaClient::NODEHANDLE, report);
            sprintf(report + 8, " %d %" PRIu64 " %d %X %.200s %.200s", n->type, n->size, attrlen, changed, buf, base64attrstring.c_str());

            reportevent("NK", report, 0);
            sendevent(99400, report, 0);

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

                if (!n->changed.removed && (n->changed.newnode || n->changed.parent))
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

void MegaClient::transfercacheadd(Transfer *transfer, DBTableTransactionCommitter* committer)
{
    if (tctable && !transfer->skipserialization)
    {
        LOG_debug << "Caching transfer";
        tctable->checkCommitter(committer);
        tctable->put(MegaClient::CACHEDTRANSFER, transfer, &tckey);
    }
}

void MegaClient::transfercachedel(Transfer *transfer, DBTableTransactionCommitter* committer)
{
    if (tctable && transfer->dbid)
    {
        LOG_debug << "Removing cached transfer";
        tctable->checkCommitter(committer);
        tctable->del(transfer->dbid);
    }
}

void MegaClient::filecacheadd(File *file, DBTableTransactionCommitter& committer)
{
    if (tctable && !file->syncxfer)
    {
        LOG_debug << "Caching file";
        tctable->checkCommitter(&committer);
        tctable->put(MegaClient::CACHEDFILE, file, &tckey);
    }
}

void MegaClient::filecachedel(File *file, DBTableTransactionCommitter* committer)
{
    if (tctable && !file->syncxfer)
    {
        LOG_debug << "Removing cached file";
        tctable->checkCommitter(committer);
        tctable->del(file->dbid);
    }

    if (file->temporaryfile)
    {
        LOG_debug << "Removing temporary file";
        fsaccess->unlinklocal(file->localname);
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
                    size_t keysize = n->nodekey().size();
                    sn->sharekey->ecb_encrypt((byte*)n->nodekey().data(), keybuf, keysize);
                    reqs.add(new CommandSingleKeyCR(sh, nh, keybuf, keysize));
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
    if (j->enterobject())
    {
        bool done = false;
        while (!done)
        {
            bool readingPublicChats = false;
            switch(j->getnameid())
            {
                case MAKENAMEID2('p', 'c'):   // list of public and/or formerly public chatrooms
                {
                    readingPublicChats = true;
                }   // fall-through
                case 'c':   // list of chatrooms
                {
                    j->enterarray();

                    while(j->enterobject())   // while there are more chats to read...
                    {
                        handle chatid = UNDEF;
                        privilege_t priv = PRIV_UNKNOWN;
                        int shard = -1;
                        userpriv_vector *userpriv = NULL;
                        bool group = false;
                        string title;
                        string unifiedKey;
                        m_time_t ts = -1;
                        bool publicchat = false;

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
                                shard = int(j->getint());
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

                            case MAKENAMEID2('c', 'k'):  // store unified key for public chats
                                assert(readingPublicChats);
                                j->storeobject(&unifiedKey);
                                break;

                            case MAKENAMEID2('t', 's'):  // actual creation timestamp
                                ts = j->getint();
                                break;

                            case 'm':   // operation mode: 1 -> public chat; 0 -> private chat
                                assert(readingPublicChats);
                                publicchat = j->getint();
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

                                    if (readingPublicChats)
                                    {
                                        chat->publicchat = publicchat;  // true or false (formerly public, now private)
                                        chat->unifiedKey = unifiedKey;

                                        if (unifiedKey.empty())
                                        {
                                            LOG_err << "Received public (or formerly public) chat without unified key";
                                        }
                                    }

                                    // remove yourself from the list of users (only peers matter)
                                    if (userpriv)
                                    {
                                        if (chat->priv == PRIV_RM)
                                        {
                                            // clear the list of peers because API still includes peers in the
                                            // actionpacket, but not in a fresh fetchnodes
                                            delete userpriv;
                                            userpriv = NULL;
                                        }
                                        else
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

                    j->leavearray();
                    break;
                }

                case MAKENAMEID3('p', 'c', 'f'):    // list of flags for public and/or formerly public chatrooms
                {
                    readingPublicChats = true;
                }   // fall-through
                case MAKENAMEID2('c', 'f'):
                {
                    j->enterarray();

                    while(j->enterobject()) // while there are more chatid/flag tuples to read...
                    {
                        handle chatid = UNDEF;
                        byte flags = 0xFF;

                        bool readingFlags = true;
                        while (readingFlags)
                        {
                            switch (j->getnameid())
                            {

                            case MAKENAMEID2('i','d'):
                                chatid = j->gethandle(MegaClient::CHATHANDLE);
                                break;

                            case 'f':
                                flags = byte(j->getint());
                                break;

                            case EOO:
                                if (chatid != UNDEF && flags != 0xFF)
                                {
                                    textchat_map::iterator it = chats.find(chatid);
                                    if (it == chats.end())
                                    {
                                        string chatidB64;
                                        string tmp((const char*)&chatid, sizeof(chatid));
                                        Base64::btoa(tmp, chatidB64);
                                        LOG_err << "Received flags for unknown chatid: " << chatidB64.c_str();
                                    }
                                    else
                                    {
                                        it->second->setFlags(flags);
                                        assert(!readingPublicChats || !it->second->unifiedKey.empty());
                                    }
                                }
                                else
                                {
                                    LOG_err << "Failed to parse chat flags";
                                }
                                readingFlags = false;
                                break;

                            default:
                                if (!j->storeobject())
                                {
                                    LOG_err << "Failed to parse chat flags";
                                    readingFlags = false;
                                }
                                break;
                            }
                        }

                        j->leaveobject();
                    }

                    j->leavearray();
                    break;
                }

                case EOO:
                    done = true;
                    j->leaveobject();
                    break;

                default:
                    if (!j->storeobject())
                    {
                        return;
                    }
            }
        }
    }
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
                        textchat_map::iterator it = chats.find(chatid);
                        if (it == chats.end())
                        {
                            LOG_err << "Unknown chat for user/node access to attachment";
                        }
                        else
                        {
                            it->second->setNodeUserAccess(h, uh);
                        }
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
    for (unsigned i = unsigned(v->size()); i--; )
    {
        if ((*v)[i] == n)
        {
            return i;
        }
    }

    v->push_back(n);
    return unsigned(v->size() - 1);
}

// generate crypto key response
// if !selector, generate all shares*nodes tuples
void MegaClient::cr_response(node_vector* shares, node_vector* nodes, JSON* selector)
{
    node_vector rshares, rnodes;
    unsigned si, ni = unsigned(-1);
    Node* sn;
    Node* n;
    string crkeys;
    byte keybuf[FILENODEKEYLENGTH];
    char buf[128];
    int setkey = -1;

    // for security reasons, we only respond to key requests affecting our own
    // shares
    for (si = unsigned(shares->size()); si--; )
    {
        if ((*shares)[si] && ((*shares)[si]->inshare || !(*shares)[si]->sharekey))
        {
            // security feature: we only distribute node keys for our own outgoing shares.  
            LOG_warn << "Attempt to obtain node key for invalid/third-party share foiled";
            (*shares)[si] = NULL;
            sendevent(99445, "Inshare key request rejected", 0);
        }
    }

    if (!selector)
    {
        si = 0;
        ni = unsigned(-1);
        if (shares->empty() || nodes->empty())
        {
            return;
        }
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
            else
            {
                setkey = -1;
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
                    if (setkey == (int)n->nodekey().size())
                    {
                        sn->sharekey->ecb_decrypt(keybuf, n->nodekey().size());
                        n->setkey(keybuf);
                        setkey = -1;
                    }
                }
                else
                {
                    n->applykey();
                    int keysize = int(n->nodekey().size());
                    if (sn->sharekey && keysize == (n->type == FILENODE ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH))
                    {
                        unsigned nsi, nni;

                        nsi = addnode(&rshares, sn);
                        nni = addnode(&rnodes, n);

                        sprintf(buf, "\",%u,%u,\"", nsi, nni);

                        // generate & queue share nodekey
                        sn->sharekey->ecb_encrypt((byte*)n->nodekey().data(), keybuf, size_t(keysize));
                        Base64::btoa(keybuf, keysize, strchr(buf + 7, 0));
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
                                   bool purchases, bool sessions, int source)
{
    if (storage || transfer || pro)
    {
        reqs.add(new CommandGetUserQuota(this, ad, storage, transfer, pro, source));
    }

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
        if (ststatus == STORAGE_PAYWALL)
        {
            LOG_warn << "Rejecting public link request when ODQ paywall";
            return API_EPAYWALL;
        }
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
// formats supported: ...#!publichandle!key, publichandle!key or file/<ph>[<params>][#<key>]
void MegaClient::openfilelink(handle ph, const byte *key, int op)
{
    if (op)
    {
        reqs.add(new CommandGetPH(this, ph, key, op));
    }
    else
    {
        assert(key);
        reqs.add(new CommandGetFile(this, NULL, key, ph, false));
    }
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
    else // algorithm == 2 (fix legacy Webclient bug: swap data and key)
    {
        // verify HMAC with macKey(alg, f/F, ph, salt, encKey)
        HMACSHA256 hmacsha256(derivedKey + 32, 32);
        hmacsha256.add((byte *)linkBin.data(), unsigned(40 + encKeyLen));
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

        Base64Str<FILENODEKEYLENGTH> keyStr(key);
        decryptedLink->assign(MegaClient::getPublicLink(mNewLinkFormat, isFolder ? FOLDERNODE : FILENODE, ph, keyStr));
    }

    return API_OK;
}

error MegaClient::encryptlink(const char *link, const char *pwd, string *encryptedLink)
{
    if (!pwd || !link || !encryptedLink)
    {
        LOG_err << "Empty link or empty password to encrypt link";
        return API_EARGS;
    }

    bool isFolder = (strstr(link, "#F!") || strstr(link, "folder/"));
    handle ph;
    size_t linkKeySize = isFolder ? FOLDERNODEKEYLENGTH : FILENODEKEYLENGTH;
    std::unique_ptr<byte[]> linkKey(new byte[linkKeySize]);
    error e = parsepubliclink(link, ph, linkKey.get(), isFolder);
    if (e == API_OK)
    {
        // Derive MAC key with salt+pwd
        byte derivedKey[64];
        byte salt[32];
        rng.genblock(salt, 32);
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
        byte algorithm = 2;
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
            hmacsha256.add((byte *)payload.data(), unsigned(payload.size()));
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

    return e;
}

bool MegaClient::loggedinfolderlink()
{
    return !ISUNDEF(publichandle);
}

sessiontype_t MegaClient::loggedin()
{
    if (ISUNDEF(me))
    {
        return NOTLOGGEDIN;
    }

    if (ephemeralSession)
    {
        return EPHEMERALACCOUNT;
    }

    if (!asymkey.isvalid())
    {
        return CONFIRMEDACCOUNT;
    }

    return FULLACCOUNT;
}

void MegaClient::whyamiblocked()
{
    // make sure the smsve flag is up to date when we get the response
    getmiscflags();

    // queue the actual request
    reqs.add(new CommandWhyAmIblocked(this));
}

void MegaClient::block(bool fromServerClientResponse)
{
    LOG_verbose << "Blocking MegaClient, fromServerClientResponse: " << fromServerClientResponse;

    mBlocked = true;
}

void MegaClient::unblock()
{
    LOG_verbose << "Unblocking MegaClient";

    mBlocked = false;
}

error MegaClient::changepw(const char* password, const char *pin)
{
    User* u;

    if (!loggedin() || !(u = finduser(me)))
    {
        return API_EACCESS;
    }

    if (accountversion == 1)
    {
        error e;
        byte newpwkey[SymmCipher::KEYLENGTH];
        if ((e = pw_key(password, newpwkey)))
        {
            return e;
        }

        byte newkey[SymmCipher::KEYLENGTH];
        SymmCipher pwcipher;
        memcpy(newkey, key.key,  sizeof newkey);
        pwcipher.setkey(newpwkey);
        pwcipher.ecb_encrypt(newkey);

        string email = u->email;
        uint64_t stringhash = stringhash64(&email, &pwcipher);
        reqs.add(new CommandSetMasterKey(this, newkey, (const byte *)&stringhash, sizeof(stringhash), NULL, pin));
        return API_OK;
    }

    byte clientRandomValue[SymmCipher::KEYLENGTH];
    rng.genblock(clientRandomValue, sizeof(clientRandomValue));

    string salt;
    HashSHA256 hasher;
    string buffer = "mega.nz";
    buffer.resize(200, 'P');
    buffer.append((char *)clientRandomValue, sizeof(clientRandomValue));
    hasher.add((const byte*)buffer.data(), unsigned(buffer.size()));
    hasher.get(&salt);

    byte derivedKey[2 * SymmCipher::KEYLENGTH];
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;
    pbkdf2.DeriveKey(derivedKey, sizeof(derivedKey), 0, (byte *)password, strlen(password),
                     (const byte *)salt.data(), salt.size(), 100000);

    byte encmasterkey[SymmCipher::KEYLENGTH];
    SymmCipher cipher;
    cipher.setkey(derivedKey);
    cipher.ecb_encrypt(key.key, encmasterkey);

    string hashedauthkey;
    byte *authkey = derivedKey + SymmCipher::KEYLENGTH;
    hasher.add(authkey, SymmCipher::KEYLENGTH);
    hasher.get(&hashedauthkey);
    hashedauthkey.resize(SymmCipher::KEYLENGTH);

    // Pass the salt and apply to this->accountsalt if the command succeed to allow posterior checks of the password without getting it from the server
    reqs.add(new CommandSetMasterKey(this, encmasterkey, (byte*)hashedauthkey.data(), SymmCipher::KEYLENGTH, clientRandomValue, pin, &salt));
    return API_OK;
}

// create ephemeral session
void MegaClient::createephemeral()
{
    ephemeralSession = true;
    byte keybuf[SymmCipher::KEYLENGTH];
    byte pwbuf[SymmCipher::KEYLENGTH];
    byte sscbuf[2 * SymmCipher::KEYLENGTH];

    rng.genblock(keybuf, sizeof keybuf);
    rng.genblock(pwbuf, sizeof pwbuf);
    rng.genblock(sscbuf, sizeof sscbuf);

    key.setkey(keybuf);
    key.ecb_encrypt(sscbuf, sscbuf + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH);

    key.setkey(pwbuf);
    key.ecb_encrypt(keybuf);

    reqs.add(new CommandCreateEphemeralSession(this, keybuf, pwbuf, sscbuf));
}

void MegaClient::resumeephemeral(handle uh, const byte* pw, int ctag)
{
    ephemeralSession = true;
    reqs.add(new CommandResumeEphemeralSession(this, uh, pw, ctag ? ctag : reqtag));
}

void MegaClient::cancelsignup()
{
    reqs.add(new CommandCancelSignup(this));
}

void MegaClient::sendsignuplink(const char* email, const char* name, const byte* pwhash)
{
    SymmCipher pwcipher(pwhash);
    byte c[2 * SymmCipher::KEYLENGTH];

    memcpy(c, key.key, sizeof key.key);
    rng.genblock(c + SymmCipher::KEYLENGTH, SymmCipher::KEYLENGTH / 4);
    memset(c + SymmCipher::KEYLENGTH + SymmCipher::KEYLENGTH / 4, 0, SymmCipher::KEYLENGTH / 2);
    rng.genblock(c + 2 * SymmCipher::KEYLENGTH - SymmCipher::KEYLENGTH / 4, SymmCipher::KEYLENGTH / 4);

    pwcipher.ecb_encrypt(c, c, sizeof c);

    reqs.add(new CommandSendSignupLink(this, email, name, c));
}

string MegaClient::sendsignuplink2(const char *email, const char *password, const char* name)
{
    byte clientrandomvalue[SymmCipher::KEYLENGTH];
    rng.genblock(clientrandomvalue, sizeof(clientrandomvalue));

    string salt;
    HashSHA256 hasher;
    string buffer = "mega.nz";
    buffer.resize(200, 'P');
    buffer.append((char *)clientrandomvalue, sizeof(clientrandomvalue));
    hasher.add((const byte*)buffer.data(), unsigned(buffer.size()));
    hasher.get(&salt);

    byte derivedKey[2 * SymmCipher::KEYLENGTH];
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;
    pbkdf2.DeriveKey(derivedKey, sizeof(derivedKey), 0, (byte *)password, strlen(password),
                     (const byte *)salt.data(), salt.size(), 100000);

    byte encmasterkey[SymmCipher::KEYLENGTH];
    SymmCipher cipher;
    cipher.setkey(derivedKey);
    cipher.ecb_encrypt(key.key, encmasterkey);

    string hashedauthkey;
    byte *authkey = derivedKey + SymmCipher::KEYLENGTH;
    hasher.add(authkey, SymmCipher::KEYLENGTH);
    hasher.get(&hashedauthkey);
    hashedauthkey.resize(SymmCipher::KEYLENGTH);

    accountversion = 2;
    accountsalt = salt;
    reqs.add(new CommandSendSignupLink2(this, email, name, clientrandomvalue, encmasterkey, (byte*)hashedauthkey.data()));
    return string((const char*)derivedKey, 2 * SymmCipher::KEYLENGTH);
}

void MegaClient::resendsignuplink2(const char *email, const char *name)
{
    reqs.add(new CommandSendSignupLink2(this, email, name));
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

void MegaClient::confirmsignuplink2(const byte *code, unsigned len)
{
    reqs.add(new CommandConfirmSignupLink2(this, code, len));
}

// generate and configure encrypted private key, plaintext public key
void MegaClient::setkeypair()
{
    CryptoPP::Integer pubk[AsymmCipher::PUBKEY];

    string privks, pubks;

    asymkey.genkeypair(rng, asymkey.key, pubk, 2048);

    AsymmCipher::serializeintarray(pubk, AsymmCipher::PUBKEY, &pubks);
    AsymmCipher::serializeintarray(asymkey.key, AsymmCipher::PRIVKEY, &privks);

    // add random padding and ECB-encrypt with master key
    unsigned t = unsigned(privks.size());

    privks.resize((t + SymmCipher::BLOCKSIZE - 1) & - SymmCipher::BLOCKSIZE);
    rng.genblock((byte*)(privks.data() + t), privks.size() - t);

    key.ecb_encrypt((byte*)privks.data(), (byte*)privks.data(), privks.size());

    reqs.add(new CommandSetKeyPair(this,
                                      (const byte*)privks.data(),
                                      unsigned(privks.size()),
                                      (const byte*)pubks.data(),
                                      unsigned(pubks.size())));
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
                if ((pcr = PendingContactRequest::unserialize(&data)))
                {
                    mappcr(pcr->id, pcr);
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
    for (size_t i = dp.size(); i--; )
    {
        if ((n = nodebyhandle(dp[i]->parenthandle)))
        {
            dp[i]->setparent(n);
        }
    }

    mergenewshares(0);

    return true;
}

void MegaClient::purgeOrphanTransfers(bool remove)
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
        DBTableTransactionCommitter committer(tctable);
        while (cachedtransfers[d].size())
        {
            transfer_map::iterator it = cachedtransfers[d].begin();
            Transfer *transfer = it->second;
            if (remove || (purgeOrphanTransfers && (m_time() - transfer->lastaccesstime) >= 172500))
            {
                LOG_warn << "Purging orphan transfer";
                transfer->finished = true;
            }

            app->transfer_removed(transfer);
            delete transfer;
            cachedtransfers[d].erase(it);
        }
    }
}

void MegaClient::closetc(bool remove)
{
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
    else if (loggedinfolderlink())
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
        hash.add((const byte *)dbname.c_str(), unsigned(dbname.size() + 1));
        hash.get(&lok);
        tckey.setkey((const byte*)lok.data());
    }

    dbname.insert(0, "transfers_");

    tctable = dbaccess->open(rng, fsaccess, &dbname, true, true);
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
    if ((!sid.size() && !loggedinfolderlink()) || statecurrent)
    {
        DBTableTransactionCommitter committer(tctable);
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
            if (!startxfer(type, file, committer))
            {
                tctable->del(cachedfilesdbids.at(i));
                continue;
            }
        }
        cachedfiles.clear();
        cachedfilesdbids.clear();
    }
}

void MegaClient::disabletransferresumption(const char *loggedoutid)
{
    if (!dbaccess)
    {
        return;
    }
    purgeOrphanTransfers(true);
    closetc(true);

    string dbname;
    if (sid.size() >= SIDLEN)
    {
        dbname.resize((SIDLEN - sizeof key.key) * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)sid.data() + sizeof key.key, SIDLEN - sizeof key.key, (char*)dbname.c_str()));

    }
    else if (loggedinfolderlink())
    {
        dbname.resize(NODEHANDLE * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)&publichandle, NODEHANDLE, (char*)dbname.c_str()));
    }
    else
    {
        dbname = loggedoutid ? loggedoutid : "default";
    }
    dbname.insert(0, "transfers_");

    tctable = dbaccess->open(rng, fsaccess, &dbname, true, true);
    if (!tctable)
    {
        return;
    }

    purgeOrphanTransfers(true);
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
    else if (loggedinfolderlink())
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

        // allow sc requests to start 
        scsn.setScsn(cachedscsn);
        LOG_info << "Session loaded from local cache. SCSN: " << scsn.text();

#ifdef ENABLE_SYNC
        resumeResumableSyncs();
#endif
        app->fetchnodes_result(API_OK);

        loadAuthrings();

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
        pendingsc.reset();
        pendingscUserAlerts.reset();
        jsonsc.pos = NULL;
        scnotifyurl.clear();
        insca = false;
        insca_notlast = false;
        btsc.reset();

        // don't allow to start new sc requests yet
        scsn.clear();

#ifdef ENABLE_SYNC
        for (sync_list::iterator it = syncs.begin(); it != syncs.end(); it++)
        {
            (*it)->changestate(SYNC_CANCELED);
        }
#endif

        if (!loggedinfolderlink())
        {
            getuserdata();

            if (loggedin() == FULLACCOUNT)
            {
                fetchkeys();
                loadAuthrings();
            }

            fetchtimezone();
        }

        reqs.add(new CommandFetchNodes(this, nocache));
    }
}

void MegaClient::fetchkeys()
{
    fetchingkeys = true;

    resetKeyring();
    discarduser(me);
    User *u = finduser(me, 1);

    // RSA public key is retrieved by getuserdata

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
                    signkey = new EdDSA(rng, (unsigned char *) prEd255.data());
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

            sendevent(99417, "Ed25519 public key mismatch", 0);

            clearKeys();
            resetKeyring();
            return;
        }

        // Check Cu25519 public key against derive version
        if ((puCu255.size() != ECDH::PUBLIC_KEY_LENGTH) || memcmp(puCu255.data(), chatkey->pubKey, ECDH::PUBLIC_KEY_LENGTH))
        {
            LOG_warn << "Public key for Cu25519 mismatch.";

            sendevent(99412, "Cu25519 public key mismatch", 0);

            clearKeys();
            resetKeyring();
            return;
        }

        // Verify signatures for Cu25519
        if (!sigCu255.size() ||
                !EdDSA::verifyKey((unsigned char*) puCu255.data(),
                                    puCu255.size(),
                                    &sigCu255,
                                    (unsigned char*) puEd255.data()))
        {
            LOG_warn << "Signature of public key for Cu25519 not found or mismatch";

            sendevent(99413, "Signature of Cu25519 public key mismatch", 0);

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
            if (!pubkstr.size())
            {
                LOG_warn << "Error serializing RSA public key";
                sendevent(99421, "Error serializing RSA public key", 0);
            }
            if (!sigPubk.size())
            {
                LOG_warn << "Signature of public key for RSA not found";
                sendevent(99422, "Signature of public key for RSA not found", 0);
            }

            clearKeys();
            resetKeyring();
            return;
        }
        if (!EdDSA::verifyKey((unsigned char*) pubkstr.data(),
                                    pubkstr.size(),
                                    &sigPubk,
                                    (unsigned char*) puEd255.data()))
        {
            LOG_warn << "Verification of signature of public key for RSA failed";

            sendevent(99414, "Verification of signature of public key for RSA failed", 0);

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
            LOG_warn << "Public keys and/or signatures found without their respective private key.";

            sendevent(99415, "Incomplete keypair detected", 0);

            clearKeys();
            return;
        }
        else    // No keys were set --> generate keypairs and related attributes
        {
            // generate keypairs
            EdDSA *signkey = new EdDSA(rng);
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
            string *tlvContainer = tlvRecords.tlvRecordsToContainer(rng, &key);

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

        if (!chatkey)
        {
            sendevent(99416, "Incomplete keyring detected: private key for Cu25519 not found.", 0);
        }
        else // !signkey
        {
            sendevent(99423, "Incomplete keyring detected: private key for Ed25519 not found.", 0);
        }

        resetKeyring();
        clearKeys();
        return;
    }
}

void MegaClient::loadAuthrings()
{
    mFetchingAuthrings = true;

    std::set<attr_t> attrs { ATTR_AUTHRING, ATTR_AUTHCU255, ATTR_AUTHRSA };
    for (auto at : attrs)
    {
        User *ownUser = finduser(me);
        const string *av = ownUser->getattr(at);
        if (av)
        {
            if (ownUser->isattrvalid(at))
            {
                std::unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(av, &key));
                if (tlvRecords)
                {
                    mAuthRings.emplace(at, AuthRing(at, *tlvRecords));
                    LOG_info << "Authring succesfully loaded from cache: " << User::attr2string(at);
                }
                else
                {
                    LOG_err << "Failed to decrypt " << User::attr2string(at) << " from cached attribute";
                }

                continue;
            }
            else
            {
                LOG_warn << User::attr2string(at) << "  found in cache, but out of date. Fetching...";
            }
        }
        else
        {
            LOG_warn << User::attr2string(at) << " not found in cache. Fetching...";
        }

        getua(ownUser, at, 0);
    }

    // if all authrings were loaded from cache...
    if (mAuthRings.size() == attrs.size())
    {
        mFetchingAuthrings = false;
        fetchContactsKeys();
    }
}

void MegaClient::fetchContactsKeys()
{
    assert(mAuthRings.size() == 3);
    mAuthRingsTemp = mAuthRings;

    for (auto &it : users)
    {
        User *user = &it.second;
        if (user->userhandle != me)
        {
            fetchContactKeys(user);
        }
    }
}

void MegaClient::fetchContactKeys(User *user)
{
    getua(user, ATTR_ED25519_PUBK, 0);
    getua(user, ATTR_CU25519_PUBK, 0);

    int creqtag = reqtag;
    reqtag = 0;
    getpubkey(user->uid.c_str());
    reqtag = creqtag;
}

error MegaClient::trackKey(attr_t keyType, handle uh, const std::string &pubKey)
{
    User *user = finduser(uh);
    if (!user)
    {
        LOG_err << "Attempt to track a key for an unknown user " << Base64Str<MegaClient::USERHANDLE>(uh) << ": " << User::attr2string(keyType);
        assert(false);
        return API_EARGS;
    }
    const char *uid = user->uid.c_str();
    attr_t authringType = AuthRing::keyTypeToAuthringType(keyType);
    if (authringType == ATTR_UNKNOWN)
    {
        LOG_err << "Attempt to track an unknown type of key for user " << uid << ": " << User::attr2string(keyType);
        assert(false);
        return API_EARGS;
    }

    // If checking authrings for all contacts (new session), accumulate updates for all contacts first
    // in temporal authrings to put them all at once. Otherwise, update authring immediately
    AuthRing *authring = nullptr;
    unique_ptr<AuthRing> aux;
    auto it = mAuthRingsTemp.find(authringType);
    bool temporalAuthring = it != mAuthRingsTemp.end();
    if (temporalAuthring)
    {
        authring = &it->second;  // modify the temporal authring directly
    }
    else
    {
        it = mAuthRings.find(authringType);
        if (it == mAuthRings.end())
        {
            LOG_warn << "Failed to track public key in " << User::attr2string(authringType) << " for user " << uid << ": authring not available";
            assert(false);
            return API_ETEMPUNAVAIL;
        }
        aux = make_unique<AuthRing>(it->second);    // make a copy, once saved in API, it is updated
        authring = aux.get();
    }

    // compute key's fingerprint
    string keyFingerprint = AuthRing::fingerprint(pubKey);
    bool fingerprintMatch = false;

    // check if user's key is already being tracked in the authring
    bool keyTracked = authring->isTracked(uh);
    if (keyTracked)
    {
        fingerprintMatch = (keyFingerprint == authring->getFingerprint(uh));
        if (!fingerprintMatch)
        {
            if (!authring->isSignedKey())
            {
                LOG_err << "Failed to track public key in " << User::attr2string(authringType) << " for user " << uid << ": fingerprint mismatch";

                app->key_modified(uh, keyType);
                sendevent(99451, "Key modification detected");

                return API_EKEY;
            }
            //else --> verify signature, despite fingerprint does not match (it will be checked again later)
        }
        else
        {
            LOG_debug << "Authentication of public key in " << User::attr2string(authringType) << " for user " << uid << " was successful. Auth method: " << AuthRing::authMethodToStr(authring->getAuthMethod(uh));
        }
    }

    if (authring->isSignedKey())
    {
        if (authring->getAuthMethod(uh) != AUTH_METHOD_SIGNATURE || !fingerprintMatch)
        {
            // load public signing key and key signature
            getua(user, ATTR_ED25519_PUBK, 0);

            attr_t attrType = AuthRing::authringTypeToSignatureType(authringType);
            getua(user, attrType, 0); // in getua_result(), we check signature actually matches
        }
    }
    else if (!keyTracked)
    {
        LOG_debug << "Adding public key to " << User::attr2string(authringType) << " as seen for user " << uid;

        // tracking has changed --> persist authring
        authring->add(uh, keyFingerprint, AUTH_METHOD_SEEN);

        // if checking authrings for all contacts, accumulate updates for all contacts first
        bool finished = true;
        if (temporalAuthring)
        {
            for (auto &it : users)
            {
                User *user = &it.second;
                if (user->userhandle != me && !authring->isTracked(user->userhandle))
                {
                    // if only a current user is not tracked yet, update temporal authring
                    finished = false;
                    break;
                }
            }
        }
        if (finished)
        {
            std::unique_ptr<string> newAuthring(authring->serialize(rng, key));
            putua(authringType, reinterpret_cast<const byte *>(newAuthring->data()), static_cast<unsigned>(newAuthring->size()), 0);
            mAuthRingsTemp.erase(authringType); // if(temporalAuthring) --> do nothing
        }
    }

    return API_OK;
}

error MegaClient::trackSignature(attr_t signatureType, handle uh, const std::string &signature)
{
    User *user = finduser(uh);
    if (!user)
    {
        LOG_err << "Attempt to track a key for an unknown user " << Base64Str<MegaClient::USERHANDLE>(uh) << ": " << User::attr2string(signatureType);
        assert(false);
        return API_EARGS;
    }
    const char *uid = user->uid.c_str();
    attr_t authringType = AuthRing::signatureTypeToAuthringType(signatureType);
    if (authringType == ATTR_UNKNOWN)
    {
        LOG_err << "Attempt to track an unknown type of signature for user " << uid << ": " << User::attr2string(signatureType);
        assert(false);
        return API_EARGS;
    }

    // If checking authrings for all contacts (new session), accumulate updates for all contacts first
    // in temporal authrings to put them all at once. Otherwise, send the update immediately
    AuthRing *authring = nullptr;
    unique_ptr<AuthRing> aux;
    auto it = mAuthRingsTemp.find(authringType);
    bool temporalAuthring = it != mAuthRingsTemp.end();
    if (temporalAuthring)
    {
        authring = &it->second;  // modify the temporal authring directly
    }
    else
    {
        it = mAuthRings.find(authringType);
        if (it == mAuthRings.end())
        {
            LOG_warn << "Failed to track signature of public key in " << User::attr2string(authringType) << " for user " << uid << ": authring not available";
            assert(false);
            return API_ETEMPUNAVAIL;
        }
        aux = make_unique<AuthRing>(it->second);    // make a copy, once saved in API, it is updated
        authring = aux.get();
    }

    const string *pubKey;
    string pubKeyBuf;   // for RSA, need to serialize the key
    if (signatureType == ATTR_SIG_CU255_PUBK)
    {
        // retrieve public key whose signature wants to be verified, from cache
        if (!user || !user->isattrvalid(ATTR_CU25519_PUBK))
        {
            LOG_warn << "Failed to verify signature " << User::attr2string(signatureType) << " for user " << uid << ": CU25519 public key is not available";
            assert(false);
            return API_EINTERNAL;
        }
        pubKey = user->getattr(ATTR_CU25519_PUBK);
    }
    else if (signatureType == ATTR_SIG_RSA_PUBK)
    {
        if (!user->pubk.isvalid())
        {
            LOG_warn << "Failed to verify signature " << User::attr2string(signatureType) << " for user " << uid << ": RSA public key is not available";
            assert(false);
            return API_EINTERNAL;
        }
        user->pubk.serializekeyforjs(pubKeyBuf);
        pubKey = &pubKeyBuf;
    }
    else
    {
        LOG_err << "Attempt to track an unknown type of signature: " <<  User::attr2string(signatureType);
        assert(false);
        return API_EINTERNAL;
    }

    // retrieve signing key from cache
    if (!user->isattrvalid(ATTR_ED25519_PUBK))
    {
        LOG_warn << "Failed to verify signature " << User::attr2string(signatureType) << " for user " << uid << ": signing public key is not available";
        assert(false);
        return API_ETEMPUNAVAIL;
    }
    const string *signingPubKey = user->getattr(ATTR_ED25519_PUBK);

    // compute key's fingerprint
    string keyFingerprint = AuthRing::fingerprint(*pubKey);
    bool fingerprintMatch = false;
    bool keyTracked = authring->isTracked(uh);

    // check signature for the public key
    bool signatureVerified = EdDSA::verifyKey((unsigned char*) pubKey->data(), pubKey->size(), (string*)&signature, (unsigned char*) signingPubKey->data());
    if (signatureVerified)
    {
        LOG_debug << "Signature " << User::attr2string(signatureType) << " succesfully verified for user " << user->uid;

        // check if user's key is already being tracked in the authring
        if (keyTracked)
        {
            fingerprintMatch = (keyFingerprint == authring->getFingerprint(uh));
            if (!fingerprintMatch)
            {
                LOG_err << "Failed to track signature of public key in " << User::attr2string(authringType) << " for user " << uid << ": fingerprint mismatch";

                if (authring->isSignedKey()) // for unsigned keys, already notified in trackKey()
                {
                    app->key_modified(uh, signatureType == ATTR_SIG_CU255_PUBK ? ATTR_CU25519_PUBK : ATTR_UNKNOWN);
                    sendevent(99451, "Key modification detected");
                }

                return API_EKEY;
            }
            else
            {
                assert(authring->getAuthMethod(uh) != AUTH_METHOD_SIGNATURE);
                LOG_warn << "Updating authentication method for user " << uid << " to signature verified, currently authenticated as seen";

                authring->update(uh, AUTH_METHOD_SIGNATURE);
            }
        }
        else
        {
            LOG_debug << "Adding public key to " << User::attr2string(authringType) << " as signature verified for user " << uid;

            authring->add(uh, keyFingerprint, AUTH_METHOD_SIGNATURE);
        }

        // if checking authrings for all contacts, accumulate updates for all contacts first
        bool finished = true;
        if (temporalAuthring)
        {
            for (auto &it : users)
            {
                User *user = &it.second;
                if (user->userhandle != me && !authring->isTracked(user->userhandle))
                {
                    // if only a current user is not tracked yet, update temporal authring
                    finished = false;
                    break;
                }
            }
        }
        if (finished)
        {
            std::unique_ptr<string> newAuthring(authring->serialize(rng, key));
            putua(authringType, reinterpret_cast<const byte *>(newAuthring->data()), static_cast<unsigned>(newAuthring->size()), 0);
            mAuthRingsTemp.erase(authringType);
        }
    }
    else
    {
        LOG_err << "Failed to verify signature of public key in " << User::attr2string(authringType) << " for user " << uid << ": signature mismatch";

        app->key_modified(uh, signatureType);
        sendevent(99452, "Signature mismatch for public key");

        return API_EKEY;
    }

    return API_OK;
}

error MegaClient::verifyCredentials(handle uh)
{
    Base64Str<MegaClient::USERHANDLE> uid(uh);
    auto it = mAuthRings.find(ATTR_AUTHRING);
    if (it == mAuthRings.end())
    {
        LOG_warn << "Failed to track public key for user " << uid << ": authring not available";
        assert(false);
        return API_ETEMPUNAVAIL;
    }

    AuthRing authring = it->second; // copy, do not modify yet the cached authring
    AuthMethod authMethod = authring.getAuthMethod(uh);
    switch (authMethod)
    {
    case AUTH_METHOD_SEEN:
        LOG_debug << "Updating authentication method of Ed25519 public key for user " << uid << " from seen to signature verified";
        authring.update(uh, AUTH_METHOD_FINGERPRINT);
        break;

    case AUTH_METHOD_FINGERPRINT:
        LOG_err << "Failed to verify credentials for user " << uid << ": already verified";
        return API_EEXIST;

    case AUTH_METHOD_SIGNATURE:
        LOG_err << "Failed to verify credentials for user " << uid << ": invalid authentication method";
        return API_EINTERNAL;

    case AUTH_METHOD_UNKNOWN:
    {
        User *user = finduser(uh);
        const string *pubKey = user ? user->getattr(ATTR_ED25519_PUBK) : nullptr;
        if (pubKey)
        {
            string keyFingerprint = AuthRing::fingerprint(*pubKey);
            LOG_warn << "Adding authentication method of Ed25519 public key for user " << uid << ": key is not tracked yet";
            authring.add(uh, keyFingerprint, AUTH_METHOD_FINGERPRINT);
        }
        else
        {
            LOG_err << "Failed to verify credentials for user " << uid << ": key not tracked and not available";
            return API_ETEMPUNAVAIL;
        }
        break;
    }
    }

    std::unique_ptr<string> newAuthring(authring.serialize(rng, key));
    putua(ATTR_AUTHRING, reinterpret_cast<const byte *>(newAuthring->data()), static_cast<unsigned>(newAuthring->size()));

    return API_OK;
}

error MegaClient::resetCredentials(handle uh)
{
    Base64Str<MegaClient::USERHANDLE> uid(uh);
    if (mAuthRings.size() != 3)
    {
        LOG_warn << "Failed to reset credentials for user " << uid << ": authring/s not available";
        // TODO: after testing, if not hit, remove assertion below
        assert(false);
        return API_ETEMPUNAVAIL;
    }

    // store all required changes into user attributes
    userattr_map attrs;
    for (auto &it : mAuthRings)
    {
        AuthRing authring = it.second; // copy, do not update cached authring yet
        if (authring.remove(uh))
        {
            attrs[it.first] = *authring.serialize(rng, key);
        }
    }

    if (attrs.size())
    {
        LOG_debug << "Removing credentials for user " << uid << "...";
        putua(&attrs);
    }
    else
    {
        LOG_warn << "Failed to reset credentials for user " << uid << ": keys not tracked yet";
        return API_ENOENT;
    }

    return API_OK;
}

bool MegaClient::areCredentialsVerified(handle uh)
{
    AuthRingsMap::const_iterator it = mAuthRings.find(ATTR_AUTHRING);
    if (it != mAuthRings.end())
    {
        return it->second.areCredentialsVerified(uh);
    }
    return false;
}

void MegaClient::purgenodesusersabortsc(bool keepOwnUser)
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

    mOptimizePurgeNodes = true;
    mFingerprints.clear();
    mNodeCounters.clear();
    for (node_map::iterator it = nodes.begin(); it != nodes.end(); it++)
    {
        delete it->second;
    }
    nodes.clear();
    mOptimizePurgeNodes = false;

#ifdef ENABLE_SYNC
    todebris.clear();
    tounlink.clear();
    mFingerprints.clear();
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
    useralerts.clear();

#ifdef ENABLE_CHAT
    for (textchat_map::iterator it = chats.begin(); it != chats.end();)
    {
        delete it->second;
        chats.erase(it++);
    }
    chatnotify.clear();
#endif

    for (user_map::iterator it = users.begin(); it != users.end(); )
    {
        User *u = &(it->second);
        if ((!keepOwnUser || u->userhandle != me) || u->userhandle == UNDEF)
        {
            umindex.erase(u->email);
            uhindex.erase(u->userhandle);
            users.erase(it++);
        }
        else
        {
            // if there are changes to notify, restore the notification in the queue
            if (u->notified)
            {
                usernotify.push_back(u);
            }

            u->dbid = 0;
            it++;
        }
    }
    assert(users.size() <= 1 && uhindex.size() <= 1 && umindex.size() <= 1);

    for (handlepcr_map::iterator it = pcrindex.begin(); it != pcrindex.end(); it++)
    {
        delete it->second;
    }

    pcrindex.clear();

    scsn.clear();

    if (pendingsc)
    {
        app->request_response_progress(-1, -1);
        pendingsc->disconnect();
    }

    if (pendingscUserAlerts)
    {
        pendingscUserAlerts->disconnect();
    }

    init();
}

// request direct read by node pointer
void MegaClient::pread(Node* n, m_off_t count, m_off_t offset, void* appdata)
{
    queueread(n->nodehandle, true, n->nodecipher(), MemAccess::get<int64_t>((const char*)n->nodekey().data() + SymmCipher::KEYLENGTH), count, offset, appdata);
}

// request direct read by exported handle / key
void MegaClient::pread(handle ph, SymmCipher* key, int64_t ctriv, m_off_t count, m_off_t offset, void* appdata, bool isforeign, const char *privauth, const char *pubauth, const char *cauth)
{
    queueread(ph, isforeign, key, ctriv, count, offset, appdata, privauth, pubauth, cauth);
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

void MegaClient::queueread(handle h, bool p, SymmCipher* key, int64_t ctriv, m_off_t offset, m_off_t count, void* appdata, const char* privauth, const char *pubauth, const char *cauth)
{
    handledrn_map::iterator it;

    encodehandletype(&h, p);

    it = hdrns.find(h);

    if (it == hdrns.end())
    {
        // this handle is not being accessed yet: insert
        it = hdrns.insert(hdrns.end(), pair<handle, DirectReadNode*>(h, new DirectReadNode(this, h, p, key, ctriv, privauth, pubauth, cauth)));
        it->second->hdrn_it = it;
        it->second->enqueue(offset, count, reqtag, appdata);

        if (overquotauntil && overquotauntil > Waiter::ds)
        {
            dstime timeleft = dstime(overquotauntil - Waiter::ds);
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
            dstime timeleft = dstime(overquotauntil - Waiter::ds);
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
    CodeCounter::ScopeTimer ccst(performanceStats.execdirectreads);

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
        if (dsdrns.begin()->second->reads.size() && (dsdrns.begin()->second->tempurls.size() || dsdrns.begin()->second->pendingcmd))
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
            n = (*it)->localroot->node;

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
             && n == (*it)->localroot->node)
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

error MegaClient::addtimer(TimerWithBackoff *twb)
{
    bttimers.push_back(twb);
    return API_OK;
}

// check sync path, add sync if folder
// disallow nested syncs (there is only one LocalNode pointer per node)
// (FIXME: perform the same check for local paths!)
error MegaClient::addsync(SyncConfig syncConfig, const char* debris, string* localdebris, int tag, void *appData)
{
#ifdef ENABLE_SYNC
    Node* remotenode = nodebyhandle(syncConfig.getRemoteNode());
    if (!remotenode)
    {
        LOG_err << "Sync root does not exist in the cloud: "
                << syncConfig.getLocalPath()
                << ": "
                << LOG_NODEHANDLE(syncConfig.getRemoteNode());

        return API_ENOENT;
    }

    bool inshare = false;
    error e = isnodesyncable(remotenode, &inshare);
    if (e)
    {
        return e;
    }

    string localPath = syncConfig.getLocalPath();
    auto rootpath = LocalPath::fromPath(localPath, *fsaccess);
    rootpath.trimNonDriveTrailingSeparator(*fsaccess);
    
    bool isnetwork = false;
    if (!fsaccess->issyncsupported(rootpath, &isnetwork))
    {
        LOG_warn << "Unsupported filesystem";
        return API_EFAILED;
    }

    auto fa = fsaccess->newfileaccess();
    if (fa->fopen(rootpath, true, false, nullptr, true))
    {
        if (fa->type == FOLDERNODE)
        {
            LOG_debug << "Adding sync: " << syncConfig.getLocalPath() << " vs " << remotenode->displaypath();

            Sync* sync = new Sync(this, std::move(syncConfig), debris, localdebris, remotenode, inshare, tag, appData);
            sync->isnetwork = isnetwork;

            if (!sync->fsstableids)
            {
                if (sync->assignfsids())
                {
                    LOG_info << "Successfully assigned fs IDs for filesystem with unstable IDs";
                }
                else
                {
                    LOG_warn << "Failed to assign some fs IDs for filesystem with unstable IDs";
                }
            }

            if (sync->scan(&rootpath, fa.get()))
            {
                syncsup = false;
                e = API_OK;
                sync->initializing = false;
                LOG_debug << "Initial scan finished. New / modified files: " << sync->dirnotify->notifyq[DirNotify::DIREVENTS].size();
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
void MegaClient::stopxfers(LocalNode* l, DBTableTransactionCommitter& committer)
{
    if (l->type != FILENODE)
    {
        for (localnode_map::iterator it = l->children.begin(); it != l->children.end(); it++)
        {
            stopxfers(it->second, committer);
        }
    }
  
    stopxfer(l, &committer);
}

// add child to nchildren hash (deterministically prefer newer/larger versions
// of identical names to avoid flapping)
// apply standard unescaping, if necessary (use *strings as ephemeral storage
// space)
void MegaClient::addchild(remotenode_map* nchildren, string* name, Node* n, list<string>* strings, FileSystemType fsType) const
{
    Node** npp;

    if (name->find('%') + 1)
    {
        string tmplocalname;

        // perform one round of unescaping to ensure that the resulting local
        // filename matches
        fsaccess->path2local(name, &tmplocalname);
        fsaccess->local2name(&tmplocalname, fsType);

        strings->push_back(tmplocalname);
        name = &strings->back();
    }

    npp = &(*nchildren)[name];

    if (!*npp
     || n->mtime > (*npp)->mtime
     || (n->mtime == (*npp)->mtime && n->size > (*npp)->size)
     || (n->mtime == (*npp)->mtime && n->size == (*npp)->size && memcmp(n->crc.data(), (*npp)->crc.data(), sizeof n->crc) > 0))
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
bool MegaClient::syncdown(LocalNode* l, LocalPath& localpath, bool rubbish)
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
            ScopedLengthRestore restoreLen(localpath);
            localpath.appendWithSeparator(LocalPath::fromName(ait->second, *fsaccess, l->sync->mFilesystemType), true, fsaccess->localseparator);

            if (app->sync_syncable(l->sync, ait->second.c_str(), localpath, *it))
            {
                addchild(&nchildren, &ait->second, *it, &strings, l->sync->mFilesystemType);
            }
            else
            {
                LOG_debug << "Node excluded " << LOG_NODEHANDLE((*it)->nodehandle) << "  Name: " << (*it)->displayname();
            }
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

        ScopedLengthRestore restoreLen(localpath);
        localpath.appendWithSeparator(ll->localname, true, fsaccess->localseparator);

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

                if (*ll == *(FileFingerprint*)rit->second)
                {
                    // both files are identical
                    nchildren.erase(rit);
                }
                // file exists on both sides - do not overwrite if local version newer or same
                else if (ll->mtime > rit->second->mtime)
                {
                    // local version is newer
                    LOG_debug << "LocalNode is newer: " << ll->name << " LNmtime: " << ll->mtime << " Nmtime: " << rit->second->mtime;
                    nchildren.erase(rit);
                }
                else if (ll->mtime == rit->second->mtime
                         && (ll->size > rit->second->size
                             || (ll->size == rit->second->size && memcmp(ll->crc.data(), rit->second->crc.data(), sizeof ll->crc) > 0)))

                {
                    if (ll->size < rit->second->size)
                    {
                        LOG_warn << "Syncdown. Same mtime but lower size: " << ll->name
                                 << " mtime: " << ll->mtime << " LNsize: " << ll->size << " Nsize: " << rit->second->size
                                 << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);
                    }
                    else
                    {
                        LOG_warn << "Syncdown. Same mtime and size, but bigger CRC: " << ll->name
                                 << " mtime: " << ll->mtime << " size: " << ll->size << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);
                    }

                    nchildren.erase(rit);
                }
                else
                {
                    // means that the localnode is going to be overwritten
                    if (rit->second->localnode && rit->second->localnode->transfer)
                    {
                        LOG_debug << "Stopping an unneeded upload";
                        DBTableTransactionCommitter committer(tctable);
                        stopxfer(rit->second->localnode, &committer);  // TODO: can we have one transaction for recursing through syncdown() ?
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
                LocalPath tmplocalpath = ll->getLocalPath();

                auto fa = fsaccess->newfileaccess(false);
                if (fa->fopen(tmplocalpath, true, false))
                {
                    FileFingerprint fp;
                    fp.genfingerprint(fa.get());

                    if (!(fp == *(FileFingerprint*)ll))
                    {
                        ll->deleted = false;
                    }
                }
            }

            if (ll->deleted)
            {
                // attempt deletion and re-queue for retry in case of a transient failure
                ll->treestate(TREESTATE_SYNCING);

                if (l->sync->movetolocaldebris(localpath) || !fsaccess->transient_error)
                {
                    DBTableTransactionCommitter committer(tctable);
                    delete lit++->second;
                }
                else
                {
                    blockedfile = localpath;
                    LOG_warn << "Transient error deleting " << blockedfile.toPath(*fsaccess);
                    success = false;
                    lit++;
                }
            }
        }
        else
        {
            lit++;
        }
    }

    // create/move missing local folders / FolderNodes, initiate downloads of
    // missing local files
    for (rit = nchildren.begin(); rit != nchildren.end(); rit++)
    {

        localname = rit->second->attrs.map.find('n')->second;

        ScopedLengthRestore restoreLen(localpath);
        localpath.appendWithSeparator(LocalPath::fromName(localname, *fsaccess, l->sync->mFilesystemType), true, fsaccess->localseparator);

        LOG_debug << "Unsynced remote node in syncdown: " << localpath.toPath(*fsaccess) << " Nsize: " << rit->second->size
                  << " Nmtime: " << rit->second->mtime << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);

        // does this node already have a corresponding LocalNode under
        // a different name or elsewhere in the filesystem?
        if (rit->second->localnode && rit->second->localnode != (LocalNode*)~0)
        {
            LOG_debug << "has a previous localnode: " << rit->second->localnode->name;
            if (rit->second->localnode->parent)
            {
                LOG_debug << "with a previous parent: " << rit->second->localnode->parent->name;
                
                LocalPath curpath = rit->second->localnode->getLocalPath();
                rit->second->localnode->treestate(TREESTATE_SYNCING);

                LOG_debug << "Renaming/moving from the previous location to the new one";
                if (fsaccess->renamelocal(curpath, localpath))
                {
                    app->syncupdate_local_move(rit->second->localnode->sync,
                                               rit->second->localnode, localpath.toPath(*fsaccess).c_str());

                    // update LocalNode tree to reflect the move/rename
                    rit->second->localnode->setnameparent(l, &localpath, fsaccess->fsShortname(localpath));

                    rit->second->localnode->sync->statecacheadd(rit->second->localnode);

                    // update filenames so that PUT transfers can continue seamlessly
                    updateputs();
                    syncactivity = true;

                    rit->second->localnode->treestate(TREESTATE_SYNCED);
                }
                else if (success && fsaccess->transient_error)
                {
                    // schedule retry
                    blockedfile = curpath;
                    LOG_debug << "Transient error moving localnode " << blockedfile.toPath(*fsaccess);
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
                if (!rit->second->syncget)
                {
                    bool download = true;
                    auto f = fsaccess->newfileaccess(false);
                    if (rit->second->localnode != (LocalNode*)~0
                            && (f->fopen(localpath) || f->type == FOLDERNODE))
                    {
                        if (f->mIsSymLink && l->sync->movetolocaldebris(localpath))
                        {
                            LOG_debug << "Found a link in localpath " << localpath.toPath(*fsaccess);
                        }
                        else
                        {
                            LOG_debug << "Skipping download over an unscanned file/folder, or the file/folder is not to be synced (special attributes)";
                            download = false;
                        }
                    }
                    f.reset();
                    rit->second->localnode = NULL;

                    // start fetching this node, unless fetch is already in progress
                    // FIXME: to cover renames that occur during the
                    // download, reconstruct localname in complete()
                    if (download)
                    {
                        LOG_debug << "Start fetching file node";
                        app->syncupdate_get(l->sync, rit->second, localpath.toPath(*fsaccess).c_str());

                        rit->second->syncget = new SyncFileGet(l->sync, rit->second, localpath);
                        nextreqtag();
                        DBTableTransactionCommitter committer(tctable); // TODO: use one committer for all files in the loop, without calling syncdown() recursively
                        startxfer(GET, rit->second->syncget, committer);
                        syncactivity = true;
                    }
                }
            }
            else
            {
                LOG_debug << "Creating local folder";
                auto f = fsaccess->newfileaccess(false);
                if (f->fopen(localpath) || f->type == FOLDERNODE)
                {
                    LOG_debug << "Skipping folder creation over an unscanned file/folder, or the file/folder is not to be synced (special attributes)";
                }
                // create local path, add to LocalNodes and recurse
                else if (fsaccess->mkdirlocal(localpath))
                {
                    LocalNode* ll = l->sync->checkpath(l, &localpath, &localname, NULL, true, nullptr);

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
                    blockedfile = localpath;
                    LOG_debug << "Transient error creating folder " << blockedfile.toPath(*fsaccess);
                    success = false;
                }
                else if (!fsaccess->transient_error)
                {
                    LOG_debug << "Non transient error creating folder";
                }
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
bool MegaClient::syncup(LocalNode* l, dstime* nds, size_t& parentPending)
{
    bool insync = true;

    list<string> strings;
    remotenode_map nchildren;
    remotenode_map::iterator rit;

    // build array of sync-relevant (newest alias wins) remote children by name
    attr_map::iterator ait;

    // Number of nodes waiting for their parent to be created.
    size_t numPending = 0;

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
                        char* buf = new char[(*it)->nodekey().size() * 4 / 3 + 4];
                        Base64::btoa((byte *)(*it)->nodekey().data(), int((*it)->nodekey().size()), buf);

                        LOG_warn << "Sync: Undecryptable child node. " << buf;

                        l->reported = true;

                        char report[256];

                        Base64::btoa((const byte *)&(*it)->nodehandle, MegaClient::NODEHANDLE, report);
                        
                        sprintf(report + 8, " %d %.200s", (*it)->type, buf);

                        // report an "undecrypted child" event
                        reportevent("CU", report, 0);

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
                        reportevent("CN", NULL, 0);
                    }

                    continue;
                }

                addchild(&nchildren, &ait->second, *it, &strings, l->sync->mFilesystemType);
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

        // UTF-8 converted local name
        string localname = ll->localname.toName(*fsaccess, l->sync->mFilesystemType);
        if (!localname.size() || !ll->name.size())
        {
            if (!ll->reported)
            {
                ll->reported = true;

                char report[256];
                sprintf(report, "%d %d %d %d", (int)lit->first->editStringDirect()->size(), (int)localname.size(), (int)ll->name.size(), (int)ll->type);

                // report a "no-name localnode" event
                reportevent("LN", report, 0);
            }
            continue;
        }

        rit = nchildren.find(&localname);

        bool isSymLink = false;
#ifndef WIN32
        if (PosixFileAccess::mFoundASymlink)
        {
            unique_ptr<FileAccess> fa(fsaccess->newfileaccess(false));
            LocalPath localpath = ll->getLocalPath();

            fa->fopen(localpath);
            isSymLink = fa->mIsSymLink;
        }
#endif
        // do we have a corresponding remote child?
        if (rit != nchildren.end())
        {
            // corresponding remote node exists
            // local: folder, remote: file - overwrite
            // local: file, remote: folder - overwrite
            // local: folder, remote: folder - recurse
            // local: file, remote: file - overwrite if newer
            if (ll->type != rit->second->type || isSymLink)
            {
                insync = false;
                LOG_warn << "Type changed: " << localname << " LNtype: " << ll->type << " Ntype: " << rit->second->type << " isSymLink = " << isSymLink;
                movetosyncdebris(rit->second, l->sync->inshare);
            }
            else
            {
                // file on both sides - do not overwrite if local version older or identical
                if (ll->type == FILENODE)
                {
                    if (ll->node != rit->second)
                    {
                        ll->sync->statecacheadd(ll);
                    }
                    ll->setnode(rit->second);

                    // check if file is likely to be identical
                    if (*ll == *(FileFingerprint*)rit->second)
                    {
                        // files have the same size and the same mtime (or the
                        // same fingerprint, if available): no action needed
                        if (!ll->checked)
                        {
                            if (!gfxdisabled && gfx && gfx->isgfx(ll->localname.editStringDirect()))
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
                                        && !gfx->isvideo(ll->localname.editStringDirect()))
                                {
                                    char me64[12];
                                    Base64::btoa((const byte*)&me, MegaClient::USERHANDLE, me64);
                                    if (ll->node->attrs.map.find('f') == ll->node->attrs.map.end() || ll->node->attrs.map['f'] != me64)
                                    {
                                        LOG_debug << "Restoring missing attributes: " << ll->name;
                                        SymmCipher *symmcipher = ll->node->nodecipher();
                                        auto llpath = ll->getLocalPath();
                                        gfx->gendimensionsputfa(NULL, llpath.editStringDirect(), ll->node->nodehandle, symmcipher, missingattr);
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
                            DBTableTransactionCommitter committer(tctable);
                            stopxfer(ll, &committer);  // todo:  can we use just one commiter for all of the recursive syncup() calls?
                        }

                        ll->treestate(TREESTATE_SYNCED);
                        continue;
                    }

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

                        if (ll->size == rit->second->size && memcmp(ll->crc.data(), rit->second->crc.data(), sizeof ll->crc) < 0)
                        {
                            LOG_warn << "Syncup. Same mtime and size, but lower CRC: " << ll->name
                                     << " mtime: " << ll->mtime << " size: " << ll->size << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);

                            continue;
                        }
                    }

                    LOG_debug << "LocalNode change detected on syncupload: " << ll->name << " LNsize: " << ll->size << " LNmtime: " << ll->mtime
                              << " NSize: " << rit->second->size << " Nmtime: " << rit->second->mtime << " Nhandle: " << LOG_NODEHANDLE(rit->second->nodehandle);

#ifdef WIN32
                    if(ll->size == ll->node->size && !memcmp(ll->crc.data(), ll->node->crc.data(), sizeof(ll->crc)))
                    {
                        LOG_debug << "Modification time changed only";
                        auto f = fsaccess->newfileaccess();
                        auto lpath = ll->getLocalPath(true);
                        LocalPath stream = lpath;
                        stream.append(LocalPath::fromLocalname(string((const char *)(const wchar_t*)L":$CmdTcID:$DATA", 30)));
                        if (f->fopen(stream))
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
                                    if (value == 1 && fsaccess->setmtimelocal(lpath, ll->node->mtime))
                                    {
                                        LOG_warn << "Fixed modification time probably changed by COMODO";
                                        ll->mtime = ll->node->mtime;
                                        ll->treestate(TREESTATE_SYNCED);
                                        RegCloseKey(hKey);
                                        continue;
                                    }
                                }
                                RegCloseKey(hKey);
                            }
                        }

                        lpath.append(LocalPath::fromLocalname(string((const char *)(const wchar_t*)L":OECustomProperty", 34)));
                        if (f->fopen(lpath))
                        {
                            LOG_warn << "Windows Search detected";
                            continue;
                        }
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
                    if (!syncup(ll, nds, numPending))
                    {
                        parentPending += numPending;
                        return false;
                    }
                    continue;
                }
            }
        }

        if (isSymLink)
        {
            continue; //Do nothing for the moment
        }
        else if (ll->type == FILENODE)
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
                    m_time_t currentTime = m_time();
                    if (currentVersion->ctime > currentTime + 30)
                    {
                        // with more than 30 seconds of detected clock drift,
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
                            dstime backoffds = dstime((next - currentTime) * 10);
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

                LocalPath localpath = ll->getLocalPath();
                bool t;
                auto fa = fsaccess->newfileaccess(false);

                if (!(t = fa->fopen(localpath, true, false))
                 || fa->size != ll->size
                 || fa->mtime != ll->mtime)
                {
                    if (t)
                    {
                        ll->sync->localbytes -= ll->size;
                        ll->genfingerprint(fa.get());
                        ll->sync->localbytes += ll->size;

                        ll->sync->statecacheadd(ll);
                    }

                    ll->bumpnagleds();

                    LOG_debug << "Localnode not stable yet: " << ll->name << " " << t << " " << fa->size << " " << ll->size
                              << " " << fa->mtime << " " << ll->mtime << " " << ll->nagleds;

                    if (ll->nagleds < *nds)
                    {
                        *nds = ll->nagleds;
                    }

                    continue;
                }
                
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
                        namelen = int(ait->second.size());
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
                reportevent("D2", report, 0);
            }
            else
            {
                LOG_err << "LocalNode created and reported " << ll->name;
            }
        }
        else if (ll->parent->node)
        {
            ll->created = true;

            assert (!isSymLink);
            // create remote folder or send file
            LOG_debug << "Adding local file to synccreate: " << ll->name << " " << synccreate.size();
            synccreate.push_back(ll);
            syncactivity = true;

            if (synccreate.size() >= MAX_NEWNODES)
            {
                LOG_warn << "Stopping syncup due to MAX_NEWNODES";
                parentPending += numPending;
                return false;
            }
        }
        else
        {
            LOG_debug << "Skipping syncup of "
                      << ll->name
                      << " as its parent doesn't exist.";
            ++numPending;
        }

        if (ll->type == FOLDERNODE)
        {
            if (!syncup(ll, nds, numPending))
            {
                parentPending += numPending;
                return false;
            }
        }
    }

    if (insync && l->node && numPending == 0)
    {
        l->treestate(TREESTATE_SYNCED);
    }

    parentPending += numPending;

    return true;
}

bool MegaClient::syncup(LocalNode* l, dstime* nds)
{
    size_t numPending = 0;

    return syncup(l, nds, numPending) && numPending == 0;
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
        vector<NewNode> nn;
        nn.reserve(end - start);

        DBTableTransactionCommitter committer(tctable);
        for (i = start; i < end; i++)
        {
            n = NULL;
            l = synccreate[i];

            if (l->type == FILENODE && l->parent->node)
            {
                l->h = l->parent->node->nodehandle;
            }

            if (l->type == FOLDERNODE || (n = nodebyfingerprint(l)))
            {
                nn.resize(nn.size() + 1);
                auto nnp = &nn.back();

                // create remote folder or copy file if it already exists
                nnp->source = NEW_NODE;
                nnp->type = l->type;
                nnp->syncid = l->syncid;
                nnp->localnode.crossref(l, nnp);  // also sets l->newnode to nnp
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
                    nnp->nodekey = n->nodekey();
                    tattrs.map = n->attrs.map;

                    nameid rrname = AttrMap::string2nameid("rr");
                    attr_map::iterator it = tattrs.map.find(rrname);
                    if (it != tattrs.map.end())
                    {
                        LOG_debug << "Removing rr attribute";
                        tattrs.map.erase(it);
                    }

                    app->syncupdate_remote_copy(l->sync, l->name.c_str());
                }
                else
                {
                    // this is a folder - create, use fresh key & attributes
                    nnp->nodekey.resize(FOLDERNODEKEYLENGTH);
                    rng.genblock((byte*)nnp->nodekey.data(), FOLDERNODEKEYLENGTH);
                    tattrs.map.clear();
                }

                // set new name, encrypt and attach attributes
                tattrs.map['n'] = l->name;
                tattrs.getjson(&tattrstring);
                tkey.setkey((const byte*)nnp->nodekey.data(), nnp->type);
                nnp->attrstring.reset(new string);
                makeattr(&tkey, nnp->attrstring, tattrstring.c_str());

                l->treestate(TREESTATE_SYNCING);
            }
            else if (l->type == FILENODE)
            {
                l->treestate(TREESTATE_PENDING);

                // the overwrite will happen upon PUT completion
                string tmppath;

                nextreqtag();
                startxfer(PUT, l, committer);

                tmppath = l->getLocalPath(true).toPath(*fsaccess);
                app->syncupdate_put(l->sync, l, tmppath.c_str());
            }
        }

        if (!nn.empty())
        {
            // add nodes unless parent node has been deleted
            LocalNode *localNode = synccreate[start];
            if (localNode->parent->node)
            {
                syncadding++;

                // this assert fails for the case of two different files uploaded to the same path, and both putnodes occurring in the same exec()
                assert(localNode->type == FOLDERNODE
                       || localNode->h == localNode->parent->node->nodehandle); // if it's a file, it should match
                reqs.add(new CommandPutNodes(this,
                                                localNode->parent->node->nodehandle,
                                                NULL, move(nn),
                                                localNode->sync->tag,
                                                PUTNODES_SYNC));

                syncactivity = true;
            }
        }
    }

    synccreate.clear();
}

void MegaClient::putnodes_sync_result(error e, vector<NewNode>& nn)
{
    // check for file nodes that failed to copy and remove them from fingerprints
    // FIXME: retrigger sync decision upload them immediately
    auto nni = nn.size();
    while (nni--)
    {
        Node* n;
        if (nn[nni].type == FILENODE && !nn[nni].added)
        {
            if ((n = nodebyhandle(nn[nni].nodehandle)))
            {
                mFingerprints.remove(n);
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

void MegaClient::unlinkifexists(LocalNode *l, FileAccess *fa, LocalPath& reuseBuffer)
{
    // sdisable = true for this call.  In the case where we are doing a full scan due to fs notifications failing,
    // and a file was renamed but retains the same shortname, we would check the presence of the wrong file.
    // Also shortnames are slowly being deprecated by Microsoft, so using full names is now the normal case anyway.
    l->getlocalpath(reuseBuffer, true);
    if (fa->fopen(reuseBuffer) || fa->type == FOLDERNODE)
    {
        LOG_warn << "Deletion of existing file avoided";
        static bool reported99446 = false;
        if (!reported99446)
        {
            sendevent(99446, "Deletion of existing file avoided", 0);
            reported99446 = true;
        }

        // The local file or folder seems to be still there, but invisible
        // for the sync engine, so we just stop syncing it
        LocalTreeProcUnlinkNodes tpunlink;
        proclocaltree(l, &tpunlink);
    }
#ifdef _WIN32
    else if (fa->errorcode != ERROR_FILE_NOT_FOUND && fa->errorcode != ERROR_PATH_NOT_FOUND)
    {
        LOG_warn << "Unexpected error code for deleted file: " << fa->errorcode;
        static bool reported99447 = false;
        if (!reported99447)
        {
            ostringstream oss;
            oss << fa->errorcode;
            string message = oss.str();
            sendevent(99447, message.c_str(), 0);
            reported99447 = true;
        }
    }
#endif
}

void MegaClient::execsyncunlink()
{
    Node* n;
    Node* tn;

    // delete tounlink nodes
    do {
        n = tn = *tounlink.begin();

        while ((n = n->parent) && n->syncdeleted == SYNCDEL_NONE);

        if (!n)
        {
            unlink(tn, false, tn->tag);
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

    m_time_t ts;
    struct tm tms;
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

    ts = m_time();
    struct tm* ptm = m_localtime(ts, &tms);
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
        vector<NewNode> nnVec;
        SymmCipher tkey;
        string tattrstring;
        AttrMap tattrs;

        nnVec.resize((target == SYNCDEL_DEBRIS) ? 1 : 2);

        for (size_t i = nnVec.size(); i--; )
        {
            auto nn = &nnVec[i];

            nn->source = NEW_NODE;
            nn->type = FOLDERNODE;
            nn->nodehandle = i;
            nn->parenthandle = i ? 0 : UNDEF;

            nn->nodekey.resize(FOLDERNODEKEYLENGTH);
            rng.genblock((byte*)nn->nodekey.data(), FOLDERNODEKEYLENGTH);

            // set new name, encrypt and attach attributes
            tattrs.map['n'] = (i || target == SYNCDEL_DEBRIS) ? buf : SYNCDEBRISFOLDERNAME;
            tattrs.getjson(&tattrstring);
            tkey.setkey((const byte*)nn->nodekey.data(), FOLDERNODE);
            nn->attrstring.reset(new string);
            makeattr(&tkey, nn->attrstring, tattrstring.c_str());
        }

        reqs.add(new CommandPutNodes(this, tn->nodehandle, NULL, move(nnVec),
                                        -reqtag,
                                        PUTNODES_SYNCDEBRIS));
    }
}

// we cannot delete the Sync object directly, as it might have pending
// operations on it
void MegaClient::delsync(Sync* sync, bool deletecache)
{
    sync->changestate(SYNC_CANCELED);

    sync->setResumable(false);

    if (deletecache && sync->statecachetable)
    {
        sync->statecachetable->remove();
        delete sync->statecachetable;
        sync->statecachetable = NULL;
    }

    syncactivity = true;
}

void MegaClient::putnodes_syncdebris_result(error, vector<NewNode>& nn)
{
    syncdebrisadding = false;
}
#endif

// inject file into transfer subsystem
// if file's fingerprint is not valid, it will be obtained from the local file
// (PUT) or the file's key (GET)
bool MegaClient::startxfer(direction_t d, File* f, DBTableTransactionCommitter& committer, bool skipdupes, bool startfirst, bool donotpersist)
{
    if (!f->transfer)
    {
        if (d == PUT)
        {
            if (!f->isvalid)    // (sync LocalNodes always have this set)
            {
                // missing FileFingerprint for local file - generate
                auto fa = fsaccess->newfileaccess();

                if (fa->fopen(f->localname, d == PUT, d == GET))
                {
                    f->genfingerprint(fa.get());
                }
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
                memcpy(f->crc.data(), f->filekey, sizeof f->crc);
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
            if (!f->dbid && !donotpersist)
            {
                filecacheadd(f, committer);
            }
            app->file_added(f);

            if (startfirst)
            {
                transferlist.movetofirst(t, committer);
            }

            if (overquotauntil && overquotauntil > Waiter::ds && d != PUT)
            {
                dstime timeleft = dstime(overquotauntil - Waiter::ds);
                t->failed(API_EOVERQUOTA, committer, timeleft);
            }
            else if (d == PUT && ststatus == STORAGE_RED)
            {
                t->failed(API_EOVERQUOTA, committer);
            }
            else if (ststatus == STORAGE_PAYWALL)
            {
                t->failed(API_EPAYWALL, committer);
            }
        }
        else
        {
            it = cachedtransfers[d].find(f);
            if (it != cachedtransfers[d].end())
            {
                LOG_debug << "Resumable transfer detected";
                t = it->second;
                bool hadAnyData = t->pos > 0;
                if ((d == GET && !t->pos) || ((m_time() - t->lastaccesstime) >= 172500))
                {
                    LOG_warn << "Discarding temporary URL (" << t->pos << ", " << t->lastaccesstime << ")";
                    t->tempurls.clear();

                    if (d == PUT)
                    {
                        t->chunkmacs.clear();
                        t->progresscompleted = 0;
                        delete [] t->ultoken;
                        t->ultoken = NULL;
                        t->pos = 0;
                    }
                }

                auto fa = fsaccess->newfileaccess();
                if (!fa->fopen(t->localfilename))
                {
                    if (d == PUT)
                    {
                        LOG_warn << "Local file not found";
                        // the transfer will be retried to ensure that the file
                        // is not just just temporarily blocked
                    }
                    else
                    {
                        if (hadAnyData)
                        {
                            LOG_warn << "Temporary file not found";
                        }
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
                        if (f->genfingerprint(fa.get()))
                        {
                            LOG_warn << "The local file has been modified";
                            t->tempurls.clear();
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
                cachedtransfers[d].erase(it);
                LOG_debug << "Transfer resumed";
            }

            if (!t)
            {
                t = new Transfer(this, d);
                *(FileFingerprint*)t = *(FileFingerprint*)f;
            }

            t->skipserialization = donotpersist;

            t->lastaccesstime = m_time();
            t->tag = reqtag;
            f->tag = reqtag;
            t->transfers_it = transfers[d].insert(pair<FileFingerprint*, Transfer*>((FileFingerprint*)t, t)).first;

            f->file_it = t->files.insert(t->files.end(), f);
            f->transfer = t;
            if (!f->dbid && !donotpersist)
            {
                filecacheadd(f, committer);
            }

            transferlist.addtransfer(t, committer, startfirst);
            app->transfer_added(t);
            app->file_added(f);
            looprequested = true;

            if (overquotauntil && overquotauntil > Waiter::ds && d != PUT)
            {
                dstime timeleft = dstime(overquotauntil - Waiter::ds);
                t->failed(API_EOVERQUOTA, committer, timeleft);
            }
            else if (d == PUT && ststatus == STORAGE_RED)
            {
                t->failed(API_EOVERQUOTA, committer);
            }
            else if (ststatus == STORAGE_PAYWALL)
            {
                t->failed(API_EPAYWALL, committer);
            }
        }

        assert( (ISUNDEF(f->h) && f->targetuser.size() && (f->targetuser.size() == 11 || f->targetuser.find("@")!=string::npos) ) // <- uploading to inbox
                || (!ISUNDEF(f->h) && (nodebyhandle(f->h) || d == GET) )); // target handle for the upload should be known at this time (except for inbox uploads)
    }

    return true;
}

// remove file from transfer subsystem
void MegaClient::stopxfer(File* f, DBTableTransactionCommitter* committer)
{
    if (f->transfer)
    {
        LOG_debug << "Stopping transfer: " << f->name;

        Transfer *transfer = f->transfer;
        transfer->removeTransferFile(API_EINCOMPLETE, f, committer);

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
            if (transfer->type == PUT && !transfer->localfilename.empty())
            {
                LOG_debug << "Updating transfer path";
                transfer->files.front()->prepare();
            }
        }
    }
}

// pause/unpause transfers
void MegaClient::pausexfers(direction_t d, bool pause, bool hard, DBTableTransactionCommitter& committer)
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
                    (*it++)->doio(this, committer);
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
            connections[d] = (unsigned char)num;
            for (transferslot_list::iterator it = tslots.begin(); it != tslots.end(); )
            {
                TransferSlot *slot = *it++;
                if (slot->transfer->type == d)
                {
                    slot->transfer->state = TRANSFERSTATE_QUEUED;
                    if (slot->transfer->client->ststatus != STORAGE_RED || slot->transfer->type == GET)
                    {
                        slot->transfer->bt.arm();
                    }
                    delete slot;
                }
            }
        }
    }
}

Node* MegaClient::nodebyfingerprint(FileFingerprint* fingerprint)
{
    return mFingerprints.nodebyfingerprint(fingerprint);
}

#ifdef ENABLE_SYNC
Node* MegaClient::nodebyfingerprint(LocalNode* localNode)
{
    std::unique_ptr<const node_vector>
      remoteNodes(mFingerprints.nodesbyfingerprint(localNode));

    if (remoteNodes->empty())
        return nullptr;

    std::string localName = localNode->localname.toName(*fsaccess);

    
    // Only compare metamac if the node doesn't already exist.
    node_vector::const_iterator remoteNode =
      std::find_if(remoteNodes->begin(),
                   remoteNodes->end(),
                   [&](const Node *remoteNode) -> bool
                   {
                       return localName == remoteNode->displayname();
                   });

    if (remoteNode != remoteNodes->end())
        return *remoteNode;

    remoteNode = remoteNodes->begin();

    // Compare the local file's metamac against a random candidate.
    // 
    // If we're unable to generate the metamac, fail in such a way that
    // guarantees safe behavior.
    // 
    // That is, treat both nodes as distinct until we're absolutely certain
    // they are identical.
    auto ifAccess = fsaccess->newfileaccess();

    auto localPath = localNode->getLocalPath(true);

    if (!ifAccess->fopen(localPath, true, false))
        return nullptr;

    std::string remoteKey = (*remoteNode)->nodekey();
    const char *iva = &remoteKey[SymmCipher::KEYLENGTH];

    SymmCipher cipher;
    cipher.setkey((byte*)&remoteKey[0], (*remoteNode)->type);

    int64_t remoteIv = MemAccess::get<int64_t>(iva);
    int64_t remoteMac = MemAccess::get<int64_t>(iva + sizeof(int64_t));

    auto result = generateMetaMac(cipher, *ifAccess, remoteIv);
    if (!result.first || result.second != remoteMac)
        return nullptr;

    return *remoteNode;
}
#endif /* ENABLE_SYNC */

node_vector *MegaClient::nodesbyfingerprint(FileFingerprint* fingerprint)
{
    return mFingerprints.nodesbyfingerprint(fingerprint);
}

static bool nodes_ctime_less(const Node* a, const Node* b)
{
    // heaps return the largest element
    return a->ctime < b->ctime;
}

static bool nodes_ctime_greater(const Node* a, const Node* b)
{
    return a->ctime > b->ctime;
}

node_vector MegaClient::getRecentNodes(unsigned maxcount, m_time_t since, bool includerubbishbin)
{
    // 1. Get nodes added/modified not older than `since`
    node_vector v;
    v.reserve(nodes.size());
    for (node_map::iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
        if (i->second->type == FILENODE && i->second->ctime >= since &&  // recent files only 
            (!i->second->parent || i->second->parent->type != FILENODE)) // excluding versions
        {
            v.push_back(i->second);
        }
    }

    // heaps use a 'less' function, and pop_heap returns the largest item stored.
    std::make_heap(v.begin(), v.end(), nodes_ctime_less);

    // 2. Order them chronologically and restrict them to a maximum of `maxcount`
    node_vector v2;
    unsigned maxItems = std::min(maxcount, unsigned(v.size()));
    v2.reserve(maxItems);
    while (v2.size() < maxItems && !v.empty())
    {
        std::pop_heap(v.begin(), v.end(), nodes_ctime_less);
        Node* n = v.back();
        v.pop_back();
        if (includerubbishbin || n->firstancestor()->type != RUBBISHNODE)
        {
            v2.push_back(n);
        }
    }
    return v2;
}


namespace action_bucket_compare
{
    // these lists of file extensions (and the logic to use them) all come from the webclient - if updating here, please make sure the webclient is updated too, preferably webclient first.
    const static string webclient_is_image_def = ".jpg.jpeg.gif.bmp.png.";
    const static string webclient_is_image_raw = ".3fr.arw.cr2.crw.ciff.cs1.dcr.dng.erf.iiq.k25.kdc.mef.mos.mrw.nef.nrw.orf.pef.raf.raw.rw2.rwl.sr2.srf.srw.x3f.";
    const static string webclient_is_image_thumb = "psd.svg.tif.tiff.webp";  // leaving out .pdf
    const static string webclient_mime_photo_extensions = ".3ds.bmp.btif.cgm.cmx.djv.djvu.dwg.dxf.fbs.fh.fh4.fh5.fh7.fhc.fpx.fst.g3.gif.heic.heif.ico.ief.jpe.jpeg.jpg.ktx.mdi.mmr.npx.pbm.pct.pcx.pgm.pic.png.pnm.ppm.psd.ras.rgb.rlc.sgi.sid.svg.svgz.tga.tif.tiff.uvg.uvi.uvvg.uvvi.wbmp.wdp.webp.xbm.xif.xpm.xwd.";
    const static string webclient_mime_video_extensions = ".3g2.3gp.asf.asx.avi.dvb.f4v.fli.flv.fvt.h261.h263.h264.jpgm.jpgv.jpm.m1v.m2v.m4u.m4v.mj2.mjp2.mk3d.mks.mkv.mng.mov.movie.mp4.mp4v.mpe.mpeg.mpg.mpg4.mxu.ogv.pyv.qt.smv.uvh.uvm.uvp.uvs.uvu.uvv.uvvh.uvvm.uvvp.uvvs.uvvu.uvvv.viv.vob.webm.wm.wmv.wmx.wvx.";

    bool nodeIsVideo(const Node* n, char ext[12], const MegaClient& mc)
    {
        if (n->hasfileattribute(fa_media) && n->nodekey().size() == FILENODEKEYLENGTH)
        {
#ifdef USE_MEDIAINFO
            if (mc.mediaFileInfo.mediaCodecsReceived)
            {
                MediaProperties mp = MediaProperties::decodeMediaPropertiesAttributes(n->fileattrstring, (uint32_t*)(n->nodekey().data() + FILENODEKEYLENGTH / 2));
                unsigned videocodec = mp.videocodecid;
                if (!videocodec && mp.shortformat)
                {
                    auto& v = mc.mediaFileInfo.mediaCodecs.shortformats;
                    if (mp.shortformat < v.size())
                    {
                        videocodec = v[mp.shortformat].videocodecid;
                    }
                }
                // approximation: the webclient has a lot of logic to determine if a particular codec is playable in that browser.  We'll just base our decision on the presence of a video codec.
                if (!videocodec)
                {
                    return false; // otherwise double-check by extension
                }
            }
#endif  
        }
        return action_bucket_compare::webclient_mime_video_extensions.find(ext) != string::npos;
    }

    bool nodeIsPhoto(const Node* n, char ext[12])
    {
        // evaluate according to the webclient rules, so that we get exactly the same bucketing.
        return action_bucket_compare::webclient_is_image_def.find(ext) != string::npos ||
            action_bucket_compare::webclient_is_image_raw.find(ext) != string::npos ||
            (action_bucket_compare::webclient_mime_photo_extensions.find(ext) != string::npos && n->hasfileattribute(GfxProc::PREVIEW));
    }

    static bool compare(const Node* a, const Node* b, MegaClient* mc)
    {
        if (a->owner != b->owner) return a->owner > b->owner;
        if (a->parent != b->parent) return a->parent > b->parent;

        // added/updated - distinguish by versioning
        if (a->children.size() != b->children.size()) return a->children.size() > b->children.size();

        // media/nonmedia
        bool a_media = mc->nodeIsMedia(a, nullptr, nullptr);
        bool b_media = mc->nodeIsMedia(b, nullptr, nullptr);
        if (a_media != b_media) return a_media && !b_media;

        return false;
    }

    static bool comparetime(const recentaction& a, const recentaction& b)
    {
        return a.time > b.time;
    }

    bool getExtensionDotted(const Node* n, char ext[12], const MegaClient& mc)
    {
        auto localname = LocalPath::fromPath(n->displayname(), *mc.fsaccess);
        if (mc.fsaccess->getextension(localname, ext, 8))  // plenty of buffer space left to append a '.'
        {
            strcat(ext, ".");
            return true;
        }
        return false;
    }

}   // end namespace action_bucket_compare


bool MegaClient::nodeIsMedia(const Node* n, bool* isphoto, bool* isvideo) const
{
    char ext[12];
    if (n->type == FILENODE && action_bucket_compare::getExtensionDotted(n, ext, *this))
    {
        bool a = action_bucket_compare::nodeIsPhoto(n, ext);
        if (isphoto)
        {
            *isphoto = a;
        }
        if (a && !isvideo)
        {
            return true;
        }
        bool b = action_bucket_compare::nodeIsVideo(n, ext, *this);
        if (isvideo)
        {
            *isvideo = b;
        }
        return a || b;
    }
    return false;
}

recentactions_vector MegaClient::getRecentActions(unsigned maxcount, m_time_t since)
{
    recentactions_vector rav;
    node_vector v = getRecentNodes(maxcount, since, false);

    for (node_vector::iterator i = v.begin(); i != v.end(); )
    {
        // find the oldest node, maximum 6h
        node_vector::iterator bucketend = i + 1;
        while (bucketend != v.end() && (*bucketend)->ctime > (*i)->ctime - 6 * 3600)
        {
            ++bucketend;
        }

        // sort the defined bucket by owner, parent folder, added/updated and ismedia
        std::sort(i, bucketend, [this](const Node* n1, const Node* n2) { return action_bucket_compare::compare(n1, n2, this); });

        // split the 6h-bucket in different buckets according to their content
        for (node_vector::iterator j = i; j != bucketend; ++j)
        {
            if (i == j || action_bucket_compare::compare(*i, *j, this))
            {
                // add a new bucket
                recentaction ra;
                ra.time = (*j)->ctime;
                ra.user = (*j)->owner;
                ra.parent = (*j)->parent ? (*j)->parent->nodehandle : UNDEF;
                ra.updated = !(*j)->children.empty();   // children of files represent previous versions
                ra.media = nodeIsMedia(*j, nullptr, nullptr);
                rav.push_back(ra);
            }
            // add the node to the bucket
            rav.back().nodes.push_back(*j);
            i = j;
        }
        i = bucketend;
    }
    // sort nodes inside each bucket
    for (recentactions_vector::iterator i = rav.begin(); i != rav.end(); ++i)
    {
        // for the bucket vector, most recent (larger ctime) first
        std::sort(i->nodes.begin(), i->nodes.end(), nodes_ctime_greater);
        i->time = i->nodes.front()->ctime;
    }
    // sort buckets in the vector
    std::sort(rav.begin(), rav.end(), action_bucket_compare::comparetime);
    return rav;
}


void MegaClient::nodesbyoriginalfingerprint(const char* originalfingerprint, Node* parent, node_vector *nv)
{
    if (parent)
    {
        for (node_list::iterator i = parent->children.begin(); i != parent->children.end(); ++i)
        {
            if ((*i)->type == FILENODE)
            {
                attr_map::const_iterator a = (*i)->attrs.map.find(MAKENAMEID2('c', '0'));
                if (a != (*i)->attrs.map.end() && !a->second.compare(originalfingerprint))
                {
                    nv->push_back(*i);
                }
            }
            else
            {
                nodesbyoriginalfingerprint(originalfingerprint, *i, nv);
            }
        }
    }
    else
    {
        for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
        {
            if (i->second->type == FILENODE)
            {
                attr_map::const_iterator a = i->second->attrs.map.find(MAKENAMEID2('c', '0'));
                if (a != i->second->attrs.map.end() && !a->second.compare(originalfingerprint))
                {
                    nv->push_back(i->second);
                }
            }
        }
    }
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

void MegaClient::reportevent(const char* event, const char* details, int tag)
{
    int creqtag = reqtag;
    reqtag = tag;
    reportevent(event, details);
    reqtag = creqtag;
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
    Base64::btoa((byte *)useragent.data(), int(useragent.size()), (char *)base64userAgent.data());
    type.append(base64userAgent);

    reqs.add(new CommandUserFeedbackStore(this, type.c_str(), message, NULL));
}

void MegaClient::sendevent(int event, const char *desc)
{
    LOG_warn << clientname << "Event " << event << ": " << desc;
    reqs.add(new CommandSendEvent(this, event, desc));
}

void MegaClient::sendevent(int event, const char *message, int tag)
{
    int creqtag = reqtag;
    reqtag = tag;
    sendevent(event, message);
    reqtag = creqtag;
}

void MegaClient::supportticket(const char *message, int type)
{
    reqs.add(new CommandSupportTicket(this, message, type));
}

void MegaClient::cleanrubbishbin()
{
    reqs.add(new CommandCleanRubbishBin(this));
}

#ifdef ENABLE_CHAT
void MegaClient::createChat(bool group, bool publicchat, const userpriv_vector *userpriv, const string_map *userkeymap, const char *title)
{
    reqs.add(new CommandChatCreate(this, group, publicchat, userpriv, userkeymap, title));
}

void MegaClient::inviteToChat(handle chatid, handle uh, int priv, const char *unifiedkey, const char *title)
{
    reqs.add(new CommandChatInvite(this, chatid, uh, (privilege_t) priv, unifiedkey, title));
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

void MegaClient::archiveChat(handle chatid, bool archived)
{
    reqs.add(new CommandArchiveChat(this, chatid, archived));
}

void MegaClient::richlinkrequest(const char *url)
{
    reqs.add(new CommandRichLink(this, url));
}

void MegaClient::chatlink(handle chatid, bool del, bool createifmissing)
{
    reqs.add(new CommandChatLink(this, chatid, del, createifmissing));
}

void MegaClient::chatlinkurl(handle publichandle)
{
    reqs.add(new CommandChatLinkURL(this, publichandle));
}

void MegaClient::chatlinkclose(handle chatid, const char *title)
{
    reqs.add(new CommandChatLinkClose(this, chatid, title));
}

void MegaClient::chatlinkjoin(handle publichandle, const char *unifiedkey)
{
    reqs.add(new CommandChatLinkJoin(this, publichandle, unifiedkey));
}

void MegaClient::setchatretentiontime(handle chatid, int period)
{
    reqs.add(new CommandSetChatRetentionTime(this, chatid, period));
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

#ifdef MEGA_MEASURE_CODE
std::string MegaClient::PerformanceStats::report(bool reset, HttpIO* httpio, Waiter* waiter, const RequestDispatcher& reqs)
{
    std::ostringstream s;
    s << prepareWait.report(reset) << "\n"
        << doWait.report(reset) << "\n"
        << checkEvents.report(reset) << "\n"
        << execFunction.report(reset) << "\n"
        << transferslotDoio.report(reset) << "\n"
        << execdirectreads.report(reset) << "\n"
        << transferComplete.report(reset) << "\n"
        << dispatchTransfers.report(reset) << "\n"
        << applyKeys.report(reset) << "\n"
        << scProcessingTime.report(reset) << "\n"
        << csResponseProcessingTime.report(reset) << "\n"
        << " cs Request waiting time: " << csRequestWaitTime.report(reset) << "\n"
        << " cs requests sent/received: " << reqs.csRequestsSent << "/" << reqs.csRequestsCompleted << " batches: " << reqs.csBatchesSent << "/" << reqs.csBatchesReceived << "\n"
        << " transfers active time: " << transfersActiveTime.report(reset) << "\n"
        << " transfer starts/finishes: " << transferStarts << " " << transferFinishes << "\n"
        << " transfer temperror/fails: " << transferTempErrors << " " << transferFails << "\n"
        << " nowait reason: immedate: " << prepwaitImmediate << " zero: " << prepwaitZero << " httpio: " << prepwaitHttpio << " fsaccess: " << prepwaitFsaccess << " nonzero waits: " << nonzeroWait << "\n";
#ifdef USE_CURL
    if (auto curlhttpio = dynamic_cast<CurlHttpIO*>(httpio))
    {
        s << curlhttpio->countCurlHttpIOAddevents.report(reset) << "\n"
            << curlhttpio->countAddAresEventsCode.report(reset) << "\n"
            << curlhttpio->countAddCurlEventsCode.report(reset) << "\n"
            << curlhttpio->countProcessAresEventsCode.report(reset) << "\n"
            << curlhttpio->countProcessCurlEventsCode.report(reset) << "\n";
    }
#endif
#ifdef WIN32
    s << " waiter nonzero timeout: " << static_cast<WinWaiter*>(waiter)->performanceStats.waitTimedoutNonzero 
      << " zero timeout: " << static_cast<WinWaiter*>(waiter)->performanceStats.waitTimedoutZero
      << " io trigger: " << static_cast<WinWaiter*>(waiter)->performanceStats.waitIOCompleted 
      << " event trigger: "  << static_cast<WinWaiter*>(waiter)->performanceStats.waitSignalled << "\n";
#endif
    if (reset)
    {
        transferStarts = transferFinishes = transferTempErrors = transferFails = 0;
        prepwaitImmediate = prepwaitZero = prepwaitHttpio = prepwaitFsaccess = nonzeroWait = 0;
    }
    return s.str();
}
#endif

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
