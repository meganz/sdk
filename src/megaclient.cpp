/**
 * @file megaclient.cpp
 * @brief Client access engine core logic
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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
#include <ctime>
#include <algorithm>
#include <functional>
#include <future>
#include <iomanip>
#include <random>
#include <cryptopp/hkdf.h> // required for derive key of master key
#include "mega/heartbeats.h"
#include "mega/testhooks.h"

#include <mega/fuse/common/normalized_path.h>

#undef min // avoid issues with std::min and std::max
#undef max

namespace mega {

// FIXME: generate cr element for file imports
// FIXME: support invite links (including responding to sharekey requests)
// FIXME: instead of copying nodes, move if the source is in the rubbish to reduce node creation load on the servers
// FIXME: prevent synced folder from being moved into another synced folder


// default for disable public key pinning (for testing purposes) (determines if we check the public key from APIURL)
bool g_disablepkp_default = false;

// root URL for API access
// MegaClient statics must be const or we get threading problems.  And this one is edited so it can't be const.
// Instead, we require a mutex to be locked before editing/reading it.  MegaClient's HttpIO takes a copy on construction
std::mutex g_APIURL_default_mutex;
string g_APIURL_default = "https://g.api.mega.co.nz/";

// user handle for customer support user
const string MegaClient::SUPPORT_USER_HANDLE = "pGTOqu7_Fek";

// root URL for chat stats
// MegaClient statics must be const or we get threading problems
const string MegaClient::SFUSTATSURL = "https://stats.sfu.mega.co.nz";

// root URL for request status monitoring
// MegaClient statics must be const or we get threading problems
const string MegaClient::REQSTATURL = "https://reqstat.api.mega.co.nz";

// root URL for Website
// MegaClient statics must be const or we get threading problems
const string MegaClient::MEGAURL = "https://mega.nz";

// maximum number of concurrent transfers (uploads + downloads)
const unsigned MegaClient::MAXTOTALTRANSFERS = 48;

// maximum number of concurrent transfers (uploads or downloads)
const unsigned MegaClient::MAXTRANSFERS = 32;

// minimum maximum number of concurrent transfers for dynamic calculation
const unsigned MegaClient::MIN_MAXTRANSFERS = 12;

// maximum number of concurrent raided transfers for mobile
const unsigned MegaClient::MAX_RAIDTRANSFERS_FOR_MOBILE = std::max<unsigned>(MAXTRANSFERS - 10, MIN_MAXTRANSFERS);

// meaningful portion of the maximum transfer queue size to consider raid representation
// i.e., there must be at least this number of raid transfers to let us predict whether the next download transfer will be raided or non-raided
const unsigned MegaClient::MEANINGFUL_PORTION_OF_MAXTRANSFERS_QUEUE_FOR_RAID_PREDICTIVE_SYSTEM = std::max<unsigned>(MAXTRANSFERS / 6, 1);

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

// -- JourneyID constructor and methods --
MegaClient::JourneyID::JourneyID(unique_ptr<FileSystemAccess>& clientFsaccess, const LocalPath& rootPath) :
    mTrackValue(false),
    mClientFsaccess(clientFsaccess)
{
    if (!rootPath.empty())
    {
        LocalPath newCacheFilePath = rootPath;
        mCacheFilePath = newCacheFilePath;
        mCacheFilePath.appendWithSeparator(LocalPath::fromRelativePath("jid"), true);

        auto fileAccess = mClientFsaccess->newfileaccess(false);
        LOG_verbose << "[MegaClient::JourneyID] Cache file path set [mCacheFilePath = '" << mCacheFilePath.toPath(false) << "']";

        // Try to open the file
        if (fileAccess->fopen(mCacheFilePath, FSLogging::logOnError))
        {
            // The file already exists - load values from cache
            loadValuesFromCache();
        }
    }
    else
    {
        LOG_debug << "[MegaClient::JourneyID] No file path for cache. No cache will be used";
    }
};

// Declaration of constexpr
constexpr size_t MegaClient::JourneyID::HEX_STRING_SIZE;

// Set the JourneyID value or update tracking flag
bool MegaClient::JourneyID::setValue(const string& jidValue)
{
    bool updateJourneyID = false;
    bool updateTrackingFlag = false;
    if (!jidValue.empty())
    {
        if (jidValue.size() != HEX_STRING_SIZE)
        {
            LOG_err << "[MegaClient::JourneyID::setValue] Param jidValue has an invalid size (" << jidValue.size() << "), expected size: " << HEX_STRING_SIZE;
            assert(false && "Invalid size for new jidValue");
            return false;
        }
        if (mJidValue.empty())
        {
            assert(!mTrackValue && "There is no JourneyID value, but tracking flag is set!!!");
            LOG_debug << "[MegaClient::JourneyID::setValue] Set new JourneyID: '" << jidValue << "'";
            mJidValue = jidValue;
            updateJourneyID = true;
        }
        else if (mTrackValue)
        {
            LOG_verbose << "[MegaClient::JourneyID::setValue] Tracking flag is already set [mJidValue: " << mJidValue << ", mTrackValue = " << mTrackValue << "]";
            return false;
        }
        LOG_debug << "[MegaClient::JourneyID::setValue] Set tracking flag [mJidValue: " << mJidValue << "]";
        mTrackValue = true;
        updateTrackingFlag = true;
    }
    else
    {
        if (!mTrackValue)
        {
            LOG_verbose << "[MegaClient::JourneyID::setValue] Tracking flag is already false [mJidValue: " << mJidValue << ", mTrackValue = " << mTrackValue << "]";
            return false;
        }
        LOG_debug << "[MegaClient::JourneyID::setValue] Unset tracking flag";
        mTrackValue = false;
        updateTrackingFlag = true;
    }
    LOG_debug << "[MegaClient::JourneyID::setValue] Store updated values in cache file";
    storeValuesToCache(updateJourneyID, updateTrackingFlag);
    return true;
}

// Check if the journeyID must be tracked (used on API reqs)
bool MegaClient::JourneyID::isTrackingOn() const
{
    if (mTrackValue && mJidValue.empty())
    {
        LOG_err << "[MegaClient::JourneyID::isTrackingOn] TrackValue is ON without a valid jidValue (0)";
        assert(false && "TrackValue is ON without a valid jidValue");
    }
    return mTrackValue;
}

// Get the 16-char hex string value
string MegaClient::JourneyID::getValue() const
{
    return mJidValue;
}

bool MegaClient::JourneyID::loadValuesFromCache()
{
    if (mCacheFilePath.empty())
    {
        LOG_debug << "[MegaClient::JourneyID::loadValuesFromCache] Cache file path is empty. Cannot load values from the local cache";
        return false;
    }
    auto fileAccess = mClientFsaccess->newfileaccess(false);
    bool success = fileAccess->fopen(mCacheFilePath, true, false, FSLogging::logOnError);
    if (success)
    {
        string cachedJidValue, cachedTrackValue;
        success &= fileAccess->fread(&cachedJidValue, HEX_STRING_SIZE, 0, 0, FSLogging::logOnError);
        success &= fileAccess->fread(&cachedTrackValue, 1, 0, HEX_STRING_SIZE, FSLogging::logOnError);
        if (success)
        {
            if (cachedJidValue.size() != HEX_STRING_SIZE)
            {
                resetCacheAndValues();
                LOG_err << "[MegaClient::JourneyID::loadValuesFromCache] CachedJidValue size is not HEX_STRING_SIZE!!!! -> reset cache";
                assert(false && "CachedJidValue size is not HEX_STRING_SIZE!!!!");
                return false;
            }
            if (cachedTrackValue.size() != 1)
            {
                resetCacheAndValues();
                LOG_err << "[MegaClient::JourneyID::loadValuesFromCache] CachedTrackValue size is not 1!!!! -> reset cache";
                assert(false && "CachedJidValue size is not 1!!!!");
                return false;
            }
            if (cachedTrackValue != "1" && cachedTrackValue != "0")
            {
                resetCacheAndValues();
                LOG_err << "[MegaClient::JourneyID::loadValuesFromCache] CachedTrackValue is not 1 or 0!!!! -> reset cache";
                assert(false && "CachedTrackValue size is not 1 or 0!!!!");
                return false;
            }
            mJidValue = cachedJidValue;
            mTrackValue = (cachedTrackValue == "1") ? true : false;
        }
    }
    if (!success)
    {
        resetCacheAndValues();
        LOG_err << "[MegaClient::JourneyID::loadValuesFromCache] Unable to load values from the local cache";
        return false;
    }
    LOG_debug << "[MegaClient::JourneyID::loadValuesFromCache] Values loaded from the local cache";
    return true;
}

bool MegaClient::JourneyID::storeValuesToCache(bool storeJidValue, bool storeTrackValue) const
{
    if (mCacheFilePath.empty())
    {
        LOG_debug << "[MegaClient::JourneyID::storeValuesToCache] Cache file path is empty. Cannot store values to the local cache";
        return false;
    }
    if (mJidValue.empty())
    {
        LOG_warn << "[MegaClient::JourneyID::storeValuesToCache] Jid value is empty. It cannot be stored to the cache";
        assert(!storeTrackValue && "storeTrackValue is true with an empty mJidValue!!!");
        return false;
    }
    auto fileAccess = mClientFsaccess->newfileaccess(false);
    bool success = fileAccess->fopen(mCacheFilePath, false, true, FSLogging::logOnError);
    if (success)
    {
        if (storeJidValue)
        {
            success &= fileAccess->fwrite((const byte*)(getValue().c_str()), HEX_STRING_SIZE, 0);
        }
        if (storeTrackValue)
        {
            success &= fileAccess->fwrite((const byte*)(mTrackValue ? "1" : "0"), 1, HEX_STRING_SIZE);
        }
    }
    if (!success)
    {
        LOG_err << "[MegaClient::JourneyID::storeValuesToCache] Unable to store values in the local cache";
        return false;
    }
    LOG_err << "[MegaClient::JourneyID::storeValuesToCache] Values stored in the local cache";
    return true;
}

bool MegaClient::JourneyID::resetCacheAndValues()
{
    // Reset local values
    mJidValue = "";
    mTrackValue = false;

    // Remove local cache file
    if (mCacheFilePath.empty())
    {
        LOG_debug << "[MegaClient::JourneyID::resetCacheAndValues] Cache file path is empty. Cannot remove local cache file";
        return false;
    }
    if (!mClientFsaccess->unlinklocal(mCacheFilePath))
    {
        LOG_err << "[MegaClient::JourneyID::resetCacheAndValues] Unable to remove local cache file";
        return false;
    }
    return true;
}
// -- JourneyID methods end --

// Generate ViewID
string MegaClient::generateViewId(PrnGen& rng)
{
    uint64_t viewId;
    rng.genblock((byte*)&viewId, sizeof(viewId));

    // Incorporate current timestamp in ms into the generated value for uniqueness
    uint64_t tsInMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    viewId ^= tsInMs;

    return Utils::uint64ToHexString(viewId);
}

// decrypt key (symmetric or asymmetric)
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
        // RSA-encrypted key - decrypt
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
void MegaClient::mergenewshares(bool notify, bool skipWriteInDb)
{
    newshare_list::iterator it;

    for (it = newshares.begin(); it != newshares.end(); )
    {
        NewShare* s = *it;

        mergenewshare(s, notify, skipWriteInDb);

        delete s;
        newshares.erase(it++);
    }
}

void MegaClient::mergenewshare(NewShare *s, bool notify, bool skipWriteInDb)
{
    bool skreceived = false;
    std::shared_ptr<Node> n = nodebyhandle(s->h);
    if (!n)
    {
        return;
    }

    if (s->access != ACCESS_UNKNOWN && !s->have_key)
    {
        // Check if the key is already in the key manager
        std::string shareKey = mKeyManager.getShareKey(s->h);
        if (shareKey.size() == sizeof(s->key))
        {
            memcpy(s->key, shareKey.data(), sizeof(s->key));
            s->have_key = 1;
            s->outgoing = s->outgoing > 0 ? -1 : s->outgoing;   // always authenticated when loaded from KeyManager
        }
    }

    // n was no shared or it was shared but sharekey has changed
    if (s->have_key && (!n->sharekey || memcmp(s->key, n->sharekey->key, SymmCipher::KEYLENGTH)))
    {
        // setting an outbound sharekey requires node authentication
        // unless coming from a trusted source (the local cache)
        bool auth = true;

        if (s->outgoing > 0)
        {
            // Once secure=true, the "ha" for shares are ignored or set to 0, so
            // comparing against "ha" values is not needed.
            if (!checkaccess(n.get(), OWNERPRELOGIN))
            {
                LOG_warn << "Attempt to create dislocated outbound share foiled: " << toNodeHandle(s->h);
                auth = false;
            }
            else if (!mKeyManager.generation())
            {
                byte buf[SymmCipher::KEYLENGTH];

                handleauth(s->h, buf);

                if (memcmp(buf, s->auth, sizeof buf))
                {
                    LOG_warn << "Attempt to create forged outbound share foiled: " << toNodeHandle(s->h);
                    auth = false;
                }
            }
        }

        // if authentication token is received...
        // and the sharekey can decrypt encrypted node's attributes (if still encrypted)...
        if (auth && n->testShareKey(s->key))
        {
            std::string newShareKey((const char *)s->key, SymmCipher::KEYLENGTH);
            std::string secureShareKey = mKeyManager.getShareKey(n->nodehandle);

            // newShareKey can arrive here from ^!keys from KeyManager::loadShareKeys
            // or loaded from mKeyManager at the beginning of this function
            // If newShareKey != secureShareKey the key should be legacy
            bool legacyKey = newShareKey != secureShareKey;

            // we don't allow legacy keys to replace trusted keys
            if (legacyKey && mKeyManager.isShareKeyTrusted(n->nodehandle))
            {
                LOG_warn << "A legacy key for " << toNodeHandle(n->nodehandle) << " has not been"
                         << " allowed to replace a trusted share key";
            }
            else
            {
                // in all other cases, we can apply the key
                if (n->sharekey)
                {
                    if (!fetchingnodes)
                    {
                        sendevent(99428,"Replacing share key", 0);
                    }
                }
                n->sharekey.reset(new SymmCipher(s->key));
                skreceived = true;
            }
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
                    n->outshares->erase(shareit);
                    found = true;
                    if (notify)
                    {
                        n->changed.outshares = true;
                        mNodeManager.notifyNode(n);
                    }
                }

                if (n->outshares->empty())
                {
                    n->outshares.reset();
                }
            }
            if (n->pendingshares && !found && s->pending)
            {
                // delete the pending share
                share_map::iterator shareit = n->pendingshares->find(s->pending);
                if (shareit != n->pendingshares->end())
                {
                    n->pendingshares->erase(shareit);
                    found = true;
                    if (notify)
                    {
                        n->changed.pendingshares = true;
                        mNodeManager.notifyNode(n);
                    }
                }

                if (n->pendingshares->empty())
                {
                    n->pendingshares.reset();
                }
            }

            // Erase sharekey if no outgoing shares (incl pending) exist
            // Sharekey is kept in KeyManager
            if (s->remove_key && !n->outshares && !n->pendingshares)
            {
                rewriteforeignkeys(n);
                n->sharekey.reset();
            }
        }
        else
        {
            if (n->inshare)
            {
                n->inshare->user->sharing.erase(n->nodehandle);
                notifyuser(n->inshare->user);
                n->inshare.reset();
            }

            // incoming share deleted - remove tree
            if (!n->parent || n->parent->changed.removed)
            {
                TreeProcDel td;
                proctree(n, &td, true);
            }
            else if (notify)
            {
                n->changed.inshare = true;
                mNodeManager.notifyNode(n);
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
                if (checkaccess(n.get(), OWNERPRELOGIN))
                {
                    unique_ptr<Share>* sharep;
                    if (!ISUNDEF(s->pending))
                    {
                        // Pending share
                        if (!n->pendingshares)
                        {
                            n->pendingshares.reset(new share_map());
                        }

                        if (s->upgrade_pending_to_full)
                        {
                            share_map::iterator shareit = n->pendingshares->find(s->pending);
                            if (shareit != n->pendingshares->end())
                            {
                                // This is currently a pending share that needs to be upgraded to a full share
                                // erase from pending shares & delete the pending share list if needed
                                n->pendingshares->erase(shareit);
                                if (notify)
                                {
                                    n->changed.pendingshares = true;
                                    mNodeManager.notifyNode(n);
                                }
                            }

                            if (n->pendingshares->empty())
                            {
                                n->pendingshares.reset();
                            }

                            // clear this so we can fall through to below and have it re-create the share in
                            // the outshares list
                            s->pending = UNDEF;

                            // create the outshares list if needed
                            if (!n->outshares)
                            {
                                n->outshares.reset(new share_map());
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
                            n->outshares.reset(new share_map());
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
                        sharep->reset(new Share(ISUNDEF(s->peer) ? NULL : finduser(s->peer, 1), s->access, s->ts, findpcr(s->pending)));
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
                        mNodeManager.notifyNode(n);
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
                    if (!checkaccess(n.get(), OWNERPRELOGIN))
                    {
                        // modification of existing share or new share
                        if (n->inshare)
                        {
                            n->inshare->update(s->access, s->ts);
                        }
                        else
                        {
                            n->inshare.reset(new Share(finduser(s->peer, 1), s->access, s->ts, NULL));
                            n->inshare->user->sharing.insert(n->nodehandle);
                        }

                        if (notify)
                        {
                            n->changed.inshare = true;
                            mNodeManager.notifyNode(n);
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


#ifdef ENABLE_SYNC
        if (n->inshare && s->access != FULL)
        {
            // check if the low(ered) access level is affecting any syncs
            // a) have we just cut off full access to a subtree of a sync?
            auto activeConfigs = syncs.getConfigs(true);
            for (auto& sc : activeConfigs)
            {
                if (n->isbelow(sc.mRemoteNode))
                {
                    LOG_warn << "Existing inbound share sync or part thereof lost full access";
                    syncs.disableSyncByBackupId(sc.mBackupId, SHARE_NON_FULL_ACCESS, false, true, nullptr);   // passing true for SYNC_FAILED
                }
            }

            // b) have we just lost full access to the subtree a sync is in?
            std::shared_ptr<Node> root;
            for (auto& sc : activeConfigs)
            {
                if (n->isbelow(sc.mRemoteNode) &&
                    (nullptr != (root = nodeByHandle(sc.mRemoteNode))) &&
                    !checkaccess(root.get(), FULL))
                {
                    LOG_warn << "Existing inbound share sync lost full access";

                    syncs.disableSyncByBackupId(sc.mBackupId, SHARE_NON_FULL_ACCESS, false, true, nullptr);   // passing true for SYNC_FAILED
                }
            };
        }
#endif
    }

    if (!notify && !skipWriteInDb)
    {
        mNodeManager.updateNode(n.get());
    }
    //else -> It will be updated at notifypurge
}

sharedNode_vector MegaClient::getInShares()
{
    sharedNode_vector nodes;
    for (auto &it : users)
    {
        for (auto &share : it.second.sharing)
        {
            std::shared_ptr<Node> n = nodebyhandle(share);
            if (n && !n->parent)    // top-level inshare have parent==nullptr
            {
                nodes.push_back(n);
            }
        }
    }

    return nodes;
}

sharedNode_vector MegaClient::getVerifiedInShares()
{
    sharedNode_vector nodes;
    for (auto &it : users)
    {
        for (auto &share : it.second.sharing)
        {
            std::shared_ptr<Node> n = nodebyhandle(share);
            if (n && !n->parent && !mKeyManager.isUnverifiedInShare(n->nodehandle, it.second.userhandle))    // top-level inshare have parent==nullptr
            {
                nodes.push_back(n);
            }
        }
    }

    return nodes;
}

sharedNode_vector MegaClient::getUnverifiedInShares()
{
    sharedNode_vector nodes;
    for (auto &it : users)
    {
        for (auto &share : it.second.sharing)
        {
            std::shared_ptr<Node> n = nodebyhandle(share);
            if (n && !n->parent && mKeyManager.isUnverifiedInShare(n->nodehandle, it.second.userhandle))    // top-level inshare have parent==nullptr
            {
                nodes.push_back(n);
            }
        }
    }

    return nodes;
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

// -- MegaClient JourneyID methods --
string MegaClient::getJourneyId() const
{
    return mJourneyId.getValue();
}

bool MegaClient::trackJourneyId() const
{
    return !getJourneyId().empty() && mJourneyId.isTrackingOn();
}

// Load the JourneyID values from the local cache.
bool MegaClient::loadJourneyIdCacheValues()
{
    return mJourneyId.loadValuesFromCache();
}

bool MegaClient::setJourneyId(const string& jid)
{
    if (mJourneyId.setValue(jid))
    {
        LOG_debug << "[MegaClient::setJourneyID] Set journeyID from string = '" << jid << "') [tracking: " << mJourneyId.isTrackingOn() << "]";
        return true;
    }
    return false;
}
// -- MegaClient JourneyID methods end --

error MegaClient::setbackupfolder(const char* foldername, int tag, std::function<void(Error)> addua_completion)
{
    if (!foldername)
    {
        return API_EARGS;
    }

    User* u = ownuser();
    if (!u)
    {
        return API_EACCESS; // not logged in?
    }

    if (u->isattrvalid(ATTR_MY_BACKUPS_FOLDER))
    {
        // if the attribute was previously set, only allow setting it again if the folder node was missing
        // (should never happen, but it already did)
        const string* buf = u->getattr(ATTR_MY_BACKUPS_FOLDER);
        handle h = 0;
        memcpy(&h, buf->data(), NODEHANDLE);
        if (nodebyhandle(h))
        {
            // cannot set a new folder if it already exists
            return API_EEXIST;
        }
    }

    // 1. prepare the NewNode and create it via putnodes(), with flag `vw:1`
    vector<NewNode> newnodes(1);
    NewNode& newNode = newnodes.back();
    putnodes_prepareOneFolder(&newNode, foldername, true);

    // 2. upon completion of putnodes(), set the user's attribute `^!bak`
    auto addua = [addua_completion, this](const Error& e, targettype_t handletype, vector<NewNode>& nodes, bool /*targetOverride*/, int tag)
    {
        if (e != API_OK)
        {
            addua_completion(e);
            return;
        }

        assert(handletype == NODE_HANDLE &&
            nodes.size() == 1 &&
            nodes.back().mAddedHandle != UNDEF);

        putua(ATTR_MY_BACKUPS_FOLDER, (const byte*)&nodes.back().mAddedHandle, NODEHANDLE,
            -1, UNDEF, 0, 0, addua_completion);
    };

    putnodes(mNodeManager.getRootNodeVault(), NoVersioning, std::move(newnodes), nullptr, tag, true, addua);
    // Note: this request should not finish until the user's attribute is set successfully

    return API_OK;
}

void MegaClient::removeFromBC(handle bkpId, handle targetDest, std::function<void(const Error&)> finalCompletion)
{
    shared_ptr<handle> bkpRoot = std::make_shared<handle>(0);
    shared_ptr<bool> isBackup = std::make_shared<bool>(false);

    // step 4: move or delete backup contents
    auto moveOrDeleteBackup = [this, bkpId, bkpRoot, targetDest, isBackup, finalCompletion](NodeHandle, Error setAttrErr)
    {
        if (!*isBackup || setAttrErr != API_OK)
        {
            if (setAttrErr != API_OK)
            {
                LOG_err << "Remove backup/sync: failed to set 'sds' for " << toHandle(bkpId) << ": " << setAttrErr;
            }
            finalCompletion(setAttrErr);
            return;
        }

        NodeHandle bkpRootNH;
        bkpRootNH.set6byte(*bkpRoot);
        NodeHandle targetDestNH;
        targetDestNH.set6byte(targetDest ? targetDest : UNDEF); // allow 0 as well

        unlinkOrMoveBackupNodes(bkpRootNH, targetDestNH, finalCompletion);
    };

    // step 3: set sds attribute
    auto updateSds = [this, bkpId, bkpRoot, moveOrDeleteBackup, finalCompletion](const Error& bkpRmvErr) mutable
    {
        if (bkpRmvErr != API_OK)
        {
            LOG_err << "Remove backup/sync: failed to remove " << toHandle(bkpId);
            // Don't break execution here in case of error. Still try to set 'sds' node attribute.
        }

        std::shared_ptr<Node> bkpRootNode = nodebyhandle(*bkpRoot);
        if (!bkpRootNode)
        {
            LOG_err << "Remove backup/sync: root folder not found";
            finalCompletion(API_ENOENT);
            return;
        }

        vector<pair<handle, int>> sdsBkps = bkpRootNode->getSdsBackups();
        sdsBkps.emplace_back(std::make_pair(bkpId, CommandBackupPut::DELETED));
        const string& sdsValue = Node::toSdsString(sdsBkps);
        attr_map sdsAttrMap(Node::sdsId(), sdsValue);

        auto e = setattr(bkpRootNode, std::move(sdsAttrMap), moveOrDeleteBackup, true);
        if (e != API_OK)
        {
            LOG_err << "Remove backup/sync: failed to set the 'sds' node attribute";
            finalCompletion(e);
        }
    };

    // step 1: fetch Backup/sync data from Backup Centre
    getBackupInfo([this, bkpId, bkpRoot, updateSds, isBackup, finalCompletion](const Error& e, const vector<CommandBackupSyncFetch::Data>& data)
        {
            if (e != API_OK)
            {
                LOG_err << "Remove backup/sync: getBackupInfo failed with " << e;
                finalCompletion(e);
                return;
            }

            for (auto& d : data)
            {
                if (d.backupId == bkpId)
                {
                    *bkpRoot = d.rootNode;
                    *isBackup = d.backupType == BackupType::BACKUP_UPLOAD;

                    // step 2: remove backup/sync
                    reqs.add(new CommandBackupRemove(this, bkpId, updateSds));
                    return;
                }
            }

            LOG_err << "Remove backup/sync: " << toHandle(bkpId) << " not returned by 'sr' command";
            finalCompletion(API_ENOENT);
        });
}

void MegaClient::getBackupInfo(std::function<void(const Error&, const vector<CommandBackupSyncFetch::Data>&)> f)
{
    reqs.add(new CommandBackupSyncFetch(f));
}

void MegaClient::setFolderLinkAccountAuth(const char *auth)
{
    if (auth)
    {
        mFolderLink.mAccountAuth = auth;
    }
    else
    {
        mFolderLink.mAccountAuth.clear();
    }
}

handle MegaClient::getFolderLinkPublicHandle()
{
    return mFolderLink.mPublicHandle;
}

bool MegaClient::isValidEndCallReason(int reason)
{
   return reason == END_CALL_REASON_REJECTED || reason == END_CALL_REASON_BY_MODERATOR;
}

bool MegaClient::isValidFolderLink()
{
    if (!ISUNDEF(mFolderLink.mPublicHandle))
    {
        NodeHandle h = mNodeManager.getRootNodeFiles();   // is the actual rootnode handle received?
        if (!h.isUndef())
        {
            std::shared_ptr<Node> n = nodeByHandle(h);
            if (n && (n->attrs.map.find('n') != n->attrs.map.end()))    // is it decrypted? (valid key)
            {
                return true;
            }
        }
    }

    return false;
}

shared_ptr<Node> MegaClient::getrootnode(shared_ptr<Node> node)
{
    if (!node)
    {
        return NULL;
    }

    std::shared_ptr<Node> n = node;
    while (n->parent)
    {
        n = n->parent;
    }

    return n;
}

bool MegaClient::isPrivateNode(NodeHandle h)
{
    std::shared_ptr<Node> node = nodeByHandle(h);
    if (!node)
    {
        return false;
    }

    NodeHandle rootnode = getrootnode(node)->nodeHandle();
    return mNodeManager.isRootNode(rootnode);
}

bool MegaClient::isForeignNode(NodeHandle h)
{
    std::shared_ptr<Node> node = nodeByHandle(h);
    if (!node)
    {
        return false;
    }

    NodeHandle rootnode = getrootnode(node)->nodeHandle();
    return !mNodeManager.isRootNode(rootnode);
}

SCSN::SCSN()
{
    clear();
}

void SCSN::clear()
{
    memset(scsn, 0, sizeof(scsn));
    stopsc = false;
    LOG_debug << "scsn cleared";
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
    bool wasReady = ready();
    Base64::btoa((byte*)&h, sizeof h, scsn);
    if (ready() != wasReady)
    {
        LOG_debug << "scsn now ready: " << ready();
    }
}

void SCSN::stopScsn()
{
    memset(scsn, 0, sizeof(scsn));
    stopsc = true;
    LOG_debug << "scsn stopped";
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

        vector<byte> derivedKey = deriveKey(password, salt, 2 * SymmCipher::KEYLENGTH);

        string hashedauthkey;
        const byte *authkey = derivedKey.data() + SymmCipher::KEYLENGTH;
        hasher.add(authkey, SymmCipher::KEYLENGTH);
        hasher.get(&hashedauthkey);
        hashedauthkey.resize(SymmCipher::KEYLENGTH);

        SymmCipher cipher;
        cipher.setkey(derivedKey.data());

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

void MegaClient::getpsa(bool urlSupport)
{
    reqs.add(new CommandGetPSA(urlSupport, this));
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

        for (auto& it : multi_transfers[GET])
        {
            Transfer *t = it.second;
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
            for (auto& it : multi_transfers[d])
            {
                Transfer *t = it.second;
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
}

std::string MegaClient::getDeviceidHash()
{
    string deviceIdHash;

    if (MegaClient::statsid.empty())
    {
        fsaccess->statsid(&statsid);
    }

    string id = MegaClient::statsid;
    if (id.size())
    {
        string hash;
        HashSHA256 hasher;
        hasher.add((const byte*)id.data(), unsigned(id.size()));
        hasher.get(&hash);
        Base64::btoa(hash, deviceIdHash);
    }
    return deviceIdHash;
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

// Preserve previous version attrs that should be kept
void MegaClient::honorPreviousVersionAttrs(Node *previousNode, AttrMap &attrs)
{
    if (previousNode)
    {
        for (const string& attr : Node::attributesToCopyIntoPreviousVersions) {
            nameid id = AttrMap::string2nameid(attr.c_str());
            auto it = previousNode->attrs.map.find(id);
            if (it != previousNode->attrs.map.end())
            {
                attrs.map[id] = it->second;
            }
        }
    }
}

// returns a matching child node by UTF-8 name (does not resolve name clashes)
// folder nodes take precedence over file nodes
// To improve performance, if this method is called several times over same folder
// getChildren should be call before the first call to this method,
// watch NodeManager::childNodeByNameType
std::shared_ptr<Node> MegaClient::childnodebyname(const Node* p, const char* name, bool skipfolders)
{
    string nname = name;

    if (!p || p->type == FILENODE)
    {
        return nullptr;
    }

    LocalPath::utf8_normalize(&nname);

    std::shared_ptr<Node> node;

    if (!skipfolders)
    {
        node = mNodeManager.childNodeByNameType(p, nname, FOLDERNODE);
    }

    if (!node)
    {
        node = mNodeManager.childNodeByNameType(p, nname, FILENODE);
    }

    return node;
}

// returns a matching child node by UTF-8 name (does not resolve name clashes)
// folder nodes take precedence over file nodes
// To improve performance, if this method is called several times over same folder
// getChildren should be call before the first call to this method,
// watch NodeManager::childNodeByNameType
std::shared_ptr<Node> MegaClient::childnodebynametype(Node* p, const char* name, nodetype_t mustBeType)
{
    string nname = name;

    if (!p || p->type == FILENODE)
    {
        return nullptr;
    }

    LocalPath::utf8_normalize(&nname);

     return mNodeManager.childNodeByNameType(p, nname, mustBeType);
}

// returns a matching child node that has the given attribute with the given value
std::shared_ptr<Node> MegaClient::childnodebyattribute(Node* p, nameid attrId, const char* attrValue)
{
    if (!p || p->type == FILENODE)
    {
        return nullptr;
    }

    // Using a DB query to avoid loading all children of 'p' (instead of only the matching
    // child nodes) will require to have dedicated columns for each attribute ID.
    // On top of that, this method is used exclusively upon creation of a new backup,
    // which implies ENABLE_SYNC.
    // (syncing always have all sync tree nodes in memory, so the DB query won't be faster)
    sharedNode_list childrenNodeList = getChildren(p);
    for (auto& child : childrenNodeList)
    {
        // find the attribute
        const auto& attrMap = child->attrs.map;
        auto found = attrMap.find(attrId);

        if (found != attrMap.end() && found->second == attrValue)
        {
            return child;
        }
    }

    return nullptr;
}

// returns all the matching child nodes by UTF-8 name
sharedNode_vector MegaClient::childnodesbyname(Node* p, const char* name, bool skipfolders)
{
    string nname = name;
    sharedNode_vector found;

    if (!p || p->type == FILENODE)
    {
        return found;
    }

    LocalPath::utf8_normalize(&nname);

    // TODO: a DB query could return the matching child nodes directly, avoiding to load all
    // children. However, currently this method is used only for internal sync tests.
    // (syncing always have all nodes in memory, so the DB query won't be faster)
    sharedNode_list nodeList = getChildren(p);
    for (sharedNode_list::iterator it = nodeList.begin(); it != nodeList.end(); it++)
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
    actionpacketsCurrent = false;
    totalNodes.store(0);
    mAppliedKeyNodeCount = 0;
    faretrying = false;

#ifdef ENABLE_SYNC
    syncdebrisadding = false;
    syncdebrisminute = 0;
#endif

    mNodeManager.setRootNodeFiles(NodeHandle());
    mNodeManager.setRootNodeVault(NodeHandle());
    mNodeManager.setRootNodeRubbish(NodeHandle());

    pendingsc.reset();
    pendingscUserAlerts.reset();
    mBlocked = false;
    mBlockedSet = false;
    pendingcs_serverBusySent = false;

    btcs.reset();
    btsc.reset();
    btpfa.reset();
    btbadhost.reset();
    btreqstat.reset();

    abortlockrequest();
    transferHttpCounter = 0;
    nextDispatchTransfersDs = 0;

    jsonsc.pos = NULL;
    insca = false;
    insca_notlast = false;
    scnotifyurl.clear();
    mPendingCatchUps = 0;
    mReceivingCatchUp = false;
    scsn.clear();

    // initialize random client application instance ID (for detecting own
    // actions in server-client stream)
    resetId(sessionid, sizeof sessionid, rng);

    notifyStorageChangeOnStateCurrent = false;
    mNotifiedSumSize = 0;

    mCurrentSeqtag.clear();
    mPriorSeqTag.clear();
    mCurrentSeqtagSeen = false;
    mCurrentSeqtagCmdtag = 0;

    mScDbStateRecord = ScDbStateRecord();
    mLastReceivedScSeqTag.clear();

    // Reset last known capacity.
    mLastKnownCapacity = -1;
}

MegaClient::MegaClient(MegaApp* a, shared_ptr<Waiter> w, HttpIO* h, DbAccess* d, GfxProc* g, const char* k, const char* u, unsigned workerThreadCount, ClientType clientType)
   : mAsyncQueue(*w, workerThreadCount)
   , mCachedStatus(this)
   , useralerts(*this)
   , btugexpiration(rng)
   , btcs(rng)
   , btbadhost(rng)
   , btworkinglock(rng)
   , btreqstat(rng)
   , btsc(rng)
   , btpfa(rng)
   , fsaccess(new FSACCESS_CLASS())
   , dbaccess(d)
   , mNodeManager(*this)
#ifdef ENABLE_SYNC
    , syncs(*this)
#endif
   , reqs(rng)
   , mKeyManager(*this)
   , mClientType(clientType)
   , mJourneyId(fsaccess, dbaccess ? dbaccess->rootPath() : LocalPath())
   , mFuseClientAdapter(*this)
   , mFuseService(mFuseClientAdapter)
{
    mNodeManager.reset();
    sctable.reset();
    pendingsccommit = false;
    tctable = NULL;
    statusTable = nullptr;
    me = UNDEF;
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
    aplvp_enabled = false;
    mSmsVerificationState = SMS_STATE_UNKNOWN;
    loggingout = 0;
    cachedug = false;
    minstreamingrate = -1;
    ephemeralSession = false;
    ephemeralSessionPlusPlus = false;

    mAppliedKeyNodeCount = 0;

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

    signkey = NULL;
    chatkey = NULL;

    init();

    fsaccess->waiter = w.get();
    transferlist.client = this;

    if ((app = a))
    {
        a->client = this;
    }

    waiter = w;
    httpio = h;

    if ((gfx = g))
    {
        g->client = this;
    }

    slotit = tslots.end();

    userid = 0;

    connections[PUT] = 3;
    connections[GET] = 4;

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
    LOG_debug << clientname << "~MegaClient running";
    destructorRunning = true;
    locallogout(false, true);

    delete pendingcs;
    delete badhostcs;
    delete dbaccess;
    LOG_debug << clientname << "~MegaClient completing";
}

void resetId(char *id, size_t length, PrnGen& rng)
{
    for (size_t i = length; i--; )
    {
        id[i] = static_cast<char>('a' + rng.genuint32(26));
    }
}

TypeOfLink MegaClient::validTypeForPublicURL(nodetype_t type)
{
    bool error;
    TypeOfLink lType;
    std::tie(error, lType) = toTypeOfLink(type);
    if (error)
    {
        assert(false);
        LOG_err << "Attempting to get a public link for node type " << type
                << ". Only valid node types are folders (" << FOLDERNODE
                << ") and files (" << FILENODE << ")";
    }

    return lType;
}

std::string MegaClient::publicLinkURL(bool newLinkFormat, TypeOfLink type, handle ph, const char *key)
{
    string strlink = MegaClient::MEGAURL + "/";
    string nodeType;
    if (newLinkFormat)
    {
        static const map<TypeOfLink, string> typeSchema = {{TypeOfLink::FOLDER, "folder/"}
                                                          ,{TypeOfLink::FILE, "file/"}
                                                          ,{TypeOfLink::SET, "collection/"}
                                                          };
        nodeType = typeSchema.at(type);
    }
    else if (type == TypeOfLink::SET)
    {
        LOG_err << "Requesting old link format URL for Set type";
        return string();
    }
    else
    {
        nodeType = (type == TypeOfLink::FOLDER ? "#F!" : "#!");
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

std::string MegaClient::getWritableLinkAuthKey(handle nodeHandle)
{
    auto node = nodebyhandle(nodeHandle);
    if (node->plink)
    {
        return node->plink->mAuthKey;
    }
    return {};
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
            LOG_debug << "Timeout (server idle)";
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

        if (cachedug && btugexpiration.armed())
        {
            LOG_debug << "Cached user data expired";
            getuserdata(reqtag);
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
                    // fall through
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
                    // fall through
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
                    // fall through
                default:
                    it++;
                }
            }
        }

        // file attribute puts (handled sequentially as a FIFO)
        if (activefa.size())
        {
            TransferDbCommitter committer(tctable);
            auto curfa = activefa.begin();
            while (curfa != activefa.end())
            {
                shared_ptr<HttpReqFA> fa = *curfa;

                auto erasePos = curfa;
                ++curfa;

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

                            if (fa->th.isUndef())
                            {
                                // client app requested the upload without a node yet, and it will use the fa handle
                                restag = fa->tag;
                                app->putfa_result(fah, fa->type, API_OK);
                            }
                            else
                            {
                                // do we have a valid upload handle?
                                if (fa->th.isNodeHandle())
                                {
                                    if (std::shared_ptr<Node> n = nodeByHandle(fa->th.nodeHandle()))
                                    {
                                        LOG_debug << "Attaching file attribute to Node";
                                        reqs.add(new CommandAttachFA(this, n->nodehandle, fa->type, fah, fa->tag));
                                    }
                                    else
                                    {
                                        LOG_debug << "Node to attach file attribute to no longer exists";
                                    }
                                }
                                else
                                {
                                    if (auto uploadFAPtr = fileAttributesUploading.lookupExisting(fa->th.uploadHandle()))
                                    {
                                        if (auto waitingFAPtr = uploadFAPtr->pendingfa.lookupExisting(fa->type))
                                        {
                                            waitingFAPtr->fileAttributeHandle = fah;
                                            waitingFAPtr->valueIsSet = true;

                                            LOG_debug << "File attribute, type " << fa->type << " for upload handle " << fa->th << " received: " << toHandle(fah);
                                            checkfacompletion(fa->th.uploadHandle());
                                        }
                                        else
                                        {
                                            LOG_debug << "File attribute, type " << fa->type << " for upload handle " << fa->th << " received, but that type was no longer needed";
                                        }
                                    }
                                    else
                                    {
                                        LOG_debug << "File attribute, type " << fa->type << " for upload handle " << fa->th << " received, but the upload was previously resolved";
                                    }
                                }
                            }
                        }
                        else
                        {
                            if (fa->th.isNodeHandle())
                            {
                                // TODO: possibly another gap here where we were generating for an existing Node, code only dealt with the upload-in-progress case
                                LOG_warn << "Error returned from file attribute servers for existing Node case: " << fa->in;
                            }
                            else
                            {
                                if (auto uploadFAPtr = fileAttributesUploading.lookupExisting(fa->th.uploadHandle()))
                                {
                                    LOG_debug << "File attribute, type " << fa->type << " for upload handle " << fa->th << " failed. Discarding the need for it";
                                    uploadFAPtr->pendingfa.erase(fa->type);
                                }
                                else
                                {
                                    LOG_debug << "File attribute, type " << fa->type << " for upload handle " << fa->th << " failed, but the upload was previously resolved";
                                }

                                checkfacompletion(fa->th.uploadHandle());
                                sendevent(99407,"Attribute attach failed during active upload", 0);
                            }
                        }

                        activefa.erase(erasePos);
                        LOG_debug << "Remaining file attributes: " << activefa.size() << " active, " << queuedfa.size() << " queued";
                        btpfa.reset();
                        faretrying = false;
                        break;

                    case REQ_FAILURE:
                        // repeat request with exponential backoff
                        LOG_warn << "Error setting file attribute. Will retry after backoff";
                        activefa.erase(erasePos);
                        fa->status = REQ_READY;
                        queuedfa.push_back(fa);
                        btpfa.backoff();
                        faretrying = true;
                        break;

                    case REQ_INFLIGHT:
                        // check if the transfer/file was cancelled while we were waiting for fa response (only for file uploads, not Node updates or app-requested fa put)
                        if (!fa->th.isNodeHandle())
                        {
                            if (auto uploadFAPtr = fileAttributesUploading.lookupExisting(fa->th.uploadHandle()))
                            {
                                uploadFAPtr->transfer->removeCancelledTransferFiles(&committer);
                                if (uploadFAPtr->transfer->files.empty())
                                {
                                    // this also removes it from slots and fileAttributesUploading
                                    activefa.erase(erasePos);
                                    uploadFAPtr->transfer->removeAndDeleteSelf(TRANSFERSTATE_CANCELLED);
                                }
                            }
                            else
                            {
                                LOG_debug << "activefa for " << fa->th.uploadHandle() << " has been orphaned, discarding";
                                activefa.erase(erasePos);
                            }
                        }
                        break;

                    default:
                        // other cases are not relevant for this one
                        break;
                }
            }
        }

        if (btpfa.armed())
        {
            faretrying = false;
            activatefa();
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
                        // timeout!
                        // fall through
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
                                if (pendingcs->mChunked)
                                {
                                    size_t consumedBytes = reqs.serverChunk(pendingcs->data(), this);
                                    LOG_verbose << "Consumed a chunk of " << consumedBytes << " bytes. "
                                                << "Total: " << reqs.chunkedProgress() << " of "
                                                << pendingcs->contentlength;
                                    pendingcs->purge(consumedBytes);
                                }

                                abortlockrequest();
                                app->request_response_progress(pendingcs->bufpos, pendingcs->contentlength);
                                pendingcs->notifiedbufpos = pendingcs->bufpos;
                            }
                        }
                        break;

                    case REQ_SUCCESS:
                        abortlockrequest();
                        app->request_response_progress(pendingcs->bufpos, -1);

                        if ((!pendingcs->mChunked && pendingcs->in != "-3" && pendingcs->in != "-4")
                            || (pendingcs->mChunked && (reqs.chunkedProgress() || (pendingcs->in != "-3" && pendingcs->in != "-4"))))
                        {
                            if ((!pendingcs->mChunked && *pendingcs->in.c_str() == '[')
                                || (pendingcs->mChunked && (reqs.chunkedProgress() || *pendingcs->in.c_str() == '[' || pendingcs->in.empty())))
                            {
                                CodeCounter::ScopeTimer ccst(performanceStats.csSuccessProcessingTime);

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

                                if (!pendingcs->mChunked)
                                {
                                    // request succeeded, process result array
                                    reqs.serverresponse(std::move(pendingcs->in), this);
                                }
                                else
                                {
                                    size_t consumedBytes = reqs.serverChunk(pendingcs->data(), this);
                                    if (consumedBytes)
                                    {
                                        LOG_verbose << "Consumed the last chunk of " << consumedBytes << " bytes";
                                    }

                                    // The requests should be already terminated
                                    assert(!reqs.chunkedProgress());
                                }

                                WAIT_CLASS::bumpds();

                                delete pendingcs;
                                pendingcs = NULL;

                                notifypurge();
                                if (sctable && pendingsccommit && !reqs.readyToSend() && scsn.ready())
                                {
                                    LOG_debug << "Executing postponed DB commit 2 (sessionid: " << string(sessionid, sizeof(sessionid)) << ")";
                                    sctable->commit();
                                    assert(!sctable->inTransaction());
                                    sctable->begin();
                                    app->notify_dbcommit();
                                    pendingsccommit = false;
                                }

                                if (auto completion = std::move(mOnCSCompletion))
                                {
                                    LOG_debug << "calling mOnCSCompletion after request reply processing";  // track possible lack of logout callbacks
                                    assert(mOnCSCompletion == nullptr);
                                    completion(this);
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

                                // A failed request implies any retry in progress has ended.
                                if (csretrying)
                                    app->notify_retry(0, RETRY_NONE);

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

                        if (!reason) reason = RETRY_UNKNOWN;

                        btcs.backoff();
                        app->notify_retry(btcs.retryin(), reason);
                        csretrying = true;
                        LOG_warn << "Retrying cs request in " << btcs.retryin() << " ds";

                        // the in-progress request will be resent, unchanged (for idempotence), when we are ready again.
                        reqs.inflightFailure(reason);

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
                if (reqs.readyToSend())
                {
                    abortlockrequest();
                    pendingcs = new HttpReq();
                    pendingcs->protect = true;
                    pendingcs->logname = clientname + "cs ";
                    pendingcs_serverBusySent = false;

                    bool v3;
                    string idempotenceId;
                    *pendingcs->out = reqs.serverrequest(pendingcs->includesFetchingNodes, v3, this, idempotenceId);

                    pendingcs->posturl = httpio->APIURL;
                    pendingcs->posturl.append("cs?id=");
                    pendingcs->posturl.append(idempotenceId);
                    pendingcs->posturl.append(getAuthURI());
                    pendingcs->posturl.append(appkey);

                    pendingcs->posturl.append(v3 ? "&v=3" : "&v=2");

                    if (lang.size())
                    {
                        pendingcs->posturl.append("&");
                        pendingcs->posturl.append(lang);
                    }
                    if (trackJourneyId())
                    {
                        pendingcs->posturl.append("&j=");
                        pendingcs->posturl.append(mJourneyId.getValue());
                    }
                    pendingcs->type = REQ_JSON;

                    if (pendingcs->includesFetchingNodes && !mNodeManager.hasCacheLoaded())
                    {
                        // Currently only fetchnodes requests can take advantage of chunked processing
                        pendingcs->mChunked = true;
                    }

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
                        app->useralerts_updated(NULL, int(useralerts.alerts.size())); // there are no 'removed' alerts at this point
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
                LOG_warn << "Useralerts request failed, continuing without them";

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
        if (!jsonsc.pos && !pendingscUserAlerts && pendingsc)
        {
            #ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                if (globalMegaTestHooks.interceptSCRequest)
                {
                    globalMegaTestHooks.interceptSCRequest(pendingsc);
                }
            #endif

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
                    if ((e == API_ESID) || (e == API_ENOENT && loggedIntoFolder()))
                    {
                        app->request_error(e);
                        scsn.stopScsn();
                    }
                    else if (e == API_ETOOMANY)
                    {
                        LOG_warn << "Too many pending updates - reloading local state";

                        // Stop the sc channel to prevent the reception of multiple
                        // API_ETOOMANY errors causing multiple consecutive reloads
                        scsn.stopScsn();

                        app->reloading();
                        int creqtag = reqtag;
                        reqtag = fetchnodestag; // associate with ongoing request, if any
                        fetchingnodes = false;
                        fetchnodestag = 0;

                        // reloading mid-session so we definitely go to the servers
                        // the node tree will be replaced when the reply arrives
                        // actionpacketsCurrent will be reset at that time
                        // nocache = true so that we get to an equal or later SCSN
                        // right away.  The ir:1 mechanism is not reliable for this
                        fetchnodes(true, false, true);
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
                    
                    if (pendingsc->httpstatus == 500 && !scnotifyurl.empty())
                    {
                        sendevent(99482, "500 received on wsc url");
                        LOG_err << "500 error on wsc URL. Clearing it";
                        scnotifyurl.clear();
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
                    LOG_debug << clientname << "sc backing off with delay ds: " << btsc.retryin();
                }
                break;

            case REQ_INFLIGHT:
                if (!pendingscTimedOut && Waiter::ds >= (pendingsc->lastdata + HttpIO::SCREQUESTTIMEOUT))
                {
                    LOG_debug << clientname << "sc timeout expired at ds: " << Waiter::ds << " and lastdata ds: " << pendingsc->lastdata;
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

        if (!scpaused && jsonsc.pos)
        {
            // FIXME: reload in case of bad JSON
            if (procsc())
            {
                // completed - initiate next SC request
                jsonsc.pos = nullptr;
                pendingsc.reset();
                btsc.reset();
            }
        }

        if (!pendingsc && !pendingscUserAlerts && scsn.ready() && btsc.armed() && !mBlocked)
        {
            if (useralerts.begincatchup)
            {
                assert(!fetchingnodes);
                pendingscUserAlerts.reset(new HttpReq());
                pendingscUserAlerts->logname = clientname + "sc50 ";
                pendingscUserAlerts->protect = true;
                pendingscUserAlerts->posturl = httpio->APIURL;
                pendingscUserAlerts->posturl.append("sc");  // notifications/useralerts on sc rather than wsc, no timeout
                pendingscUserAlerts->posturl.append("?c=50");
                pendingscUserAlerts->posturl.append(getAuthURI());
                pendingscUserAlerts->type = REQ_JSON;
                pendingscUserAlerts->post(this);
            }
            else
            {
                pendingsc.reset(new HttpReq());
                pendingsc->logname = clientname + "sc ";
                if (mPendingCatchUps && !mReceivingCatchUp)
                {
                    scnotifyurl.clear();
                    pendingsc->posturl = httpio->APIURL;
                    pendingsc->posturl.append("sc/wsc");
                    mReceivingCatchUp = true;
                }
                else
                {
                    if (scnotifyurl.size())
                    {
                        pendingsc->posturl = scnotifyurl;
                    }
                    else
                    {
                        pendingsc->posturl = httpio->APIURL;
                        pendingsc->posturl.append("wsc");
                    }
                }

                pendingsc->protect = true;
                pendingsc->posturl.append("?sn=");
                pendingsc->posturl.append(scsn.text());
                // folder links should not send "sid" to avoid receiving packets unrelated to the folder link
                bool suppressSID = loggedIntoFolder() ? true : false;
                pendingsc->posturl.append(getAuthURI(suppressSID, true));

                if (isClientType(ClientType::PASSWORD_MANAGER))
                {
                    pendingsc->posturl.append(getPartialAPs());
                }

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

        if (mReqStatCS)
        {
            if (mReqStatCS->status == REQ_SUCCESS)
            {
                if (mReqStatCS->isRedirection() && mReqStatCS->mRedirectURL.size())
                {
                    std::string reqstaturl = mReqStatCS->mRedirectURL;
                    LOG_debug << "Accessing reqstat URL: " << reqstaturl;
                    mReqStatCS.reset(new HttpReq());
                    mReqStatCS->logname = clientname + "reqstat ";
                    mReqStatCS->posturl = reqstaturl;
                    mReqStatCS->type = REQ_BINARY;
                    mReqStatCS->binary = true;
                    mReqStatCS->protect = true;
                    mReqStatCS->get(this);
                }
                else
                {
                    LOG_debug << "Successful reqstat request";
                    btreqstat.reset();
                    mReqStatCS.reset();
                }
            }
            else if (mReqStatCS->status == REQ_FAILURE)
            {
                LOG_err << "Failed reqstat request. Retrying";
                btreqstat.backoff();
                mReqStatCS.reset();
            }
            else if (mReqStatCS->status == REQ_INFLIGHT)
            {
                if (mReqStatCS->in.size())
                {
                    size_t bytesConsumed = procreqstat();
                    if (bytesConsumed)
                    {
                        btreqstat.reset();
                        mReqStatCS->in.erase(0, bytesConsumed);
                    }
                }
            }
        }

        // fill transfer slots from the queue
        if (nextDispatchTransfersDs <= Waiter::ds)
        {
            size_t lastCount = 0;
            size_t transferCount = multi_transfers[GET].size() + multi_transfers[PUT].size();
            do
            {
                lastCount = transferCount;

                // Check the list of transfers and start a few big files, and many small, up to configured limits.
                dispatchTransfers();

                // if we are cancelling a lot of transfers (eg. nodes to download were deleted), keep going. Avoid stalling when no transfers are active and all queued fail
                transferCount = multi_transfers[GET].size() + multi_transfers[PUT].size();
            } while (transferCount < lastCount);

            // don't run this too often or it may use a lot of cpu without starting new transfers, if the list is long
            nextDispatchTransfersDs = transferCount ? Waiter::ds + 1 : 0;
        }

#ifndef EMSCRIPTEN
        assert(!asyncfopens);
#endif

        slotit = tslots.begin();


        if (!mBlocked) // handle active unpaused transfers
        {
            TransferDbCommitter committer(tctable);

            while (slotit != tslots.end())
            {
                transferslot_list::iterator it = slotit;

                slotit++;

                // remove transfer files whose MegaTransfer associated has been cancelled (via cancel token)
                (*it)->transfer->removeCancelledTransferFiles(&committer);
                if ((*it)->transfer->files.empty())
                {
                    // this also removes it from slots
                    (*it)->transfer->removeAndDeleteSelf(TRANSFERSTATE_CANCELLED);
                }
                else if ((!xferpaused[(*it)->transfer->type] || (*it)->transfer->isForSupport())
                        && (!(*it)->retrying || (*it)->retrybt.armed()))
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
        if (!pendingDebris.empty())
        {
            // in case we need to retry after a prior failure
            getOrCreateSyncdebrisFolder();
        }

        if (!syncs.clientThreadActions.empty())
        {
            CodeCounter::ScopeTimer ccst(performanceStats.clientThreadActions);

            dstime ctr_start = waiter->ds;
            size_t ctr_N = 0;
            TransferDbCommitter committer(tctable);
            std::function<void(MegaClient&, TransferDbCommitter&)> f;

            waiter->bumpds();
            while (ctr_start + 5 >= waiter->ds && syncs.clientThreadActions.popFront(f))
            {
                f(*this, committer);
                ++ctr_N;
                waiter->bumpds();
            }
            if (auto n = syncs.clientThreadActions.size())
            {
                LOG_debug << "Processed " << ctr_N << " sync requests in " << (waiter->ds - ctr_start) << "ms, " << n << " requests outstanding";
            }
        }

        auto noPutnodes = transferBackstop.getAbandoned();
        for (auto& np : noPutnodes)
        {
            restag = np->tag;
            app->file_removed(np.get(), API_EINCOMPLETE);
        }
        noPutnodes.clear();

#endif

        // Dispatch FUSE client-side requests.
        mFuseClientAdapter.dispatch();

        notifypurge();

        if (!badhostcs && badhosts.size() && btbadhost.armed())
        {
            // report hosts affected by failed requests
            LOG_debug << "Sending badhost report: " << badhosts;
            badhostcs = new HttpReq();
            badhostcs->posturl = httpio->APIURL;
            badhostcs->posturl.append("pf?h");
            badhostcs->outbuf = badhosts;
            badhostcs->type = REQ_JSON;
            badhostcs->post(this);
            badhosts.clear();
        }

        if (!workinglockcs && requestLock && btworkinglock.armed())
        {
            string auth = getAuthURI();
            if (auth.size())
            {
                LOG_debug << clientname << "Sending lock request";
                workinglockcs.reset(new HttpReq());
                workinglockcs->logname = clientname + "accountBusyCheck ";
                workinglockcs->posturl = httpio->APIURL;
                workinglockcs->posturl.append("cs?");
                workinglockcs->posturl.append(getAuthURI());
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

        if (!mReqStatCS && mReqStatEnabled && sid.size() && btreqstat.armed())
        {
            LOG_debug << clientname << "Sending reqstat request";
            mReqStatCS.reset(new HttpReq());
            mReqStatCS->logname = clientname + "reqstat ";
            mReqStatCS->mExpectRedirect = true;
            mReqStatCS->posturl = httpio->APIURL;
            mReqStatCS->posturl.append("cs/rs?sid=");
            mReqStatCS->posturl.append(Base64::btoa(sid));
            mReqStatCS->type = REQ_BINARY;
            mReqStatCS->protect = true;
            mReqStatCS->binary = true;
            mReqStatCS->post(this);
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
    } while (httpio->doio() || execdirectreads() || (!pendingcs && reqs.readyToSend() && btcs.armed()));


    if (!fetchingnodes)
    {
        NodeCounter nc = mNodeManager.getCounterOfRootNodes();
        m_off_t sum = nc.storage + nc.versionStorage;
        if (mNotifiedSumSize != sum)
        {
            mNotifiedSumSize = sum;
            app->storagesum_changed(mNotifiedSumSize);
        }
    }

#ifdef MEGA_MEASURE_CODE
    ccst.complete();
    performanceStats.transfersActiveTime.start(!tslots.empty() && !performanceStats.transfersActiveTime.inprogress());
    performanceStats.transfersActiveTime.stop(tslots.empty() && performanceStats.transfersActiveTime.inprogress());

    static auto lasttime = Waiter::ds;
    static unsigned reportFreqDs = 6000;
    if (Waiter::ds > lasttime + reportFreqDs)
    {
        lasttime = Waiter::ds;
        LOG_info << performanceStats.report(false, httpio, waiter.get(), reqs);

        debugLogHeapUsage();
    }
#endif

#ifdef USE_DRIVE_NOTIFICATIONS
    // check for Drive [dis]connects
    for (auto di = mDriveInfoCollector.get(); !di.first.empty(); di = mDriveInfoCollector.get())
    {
        app->drive_presence_changed(di.second, LocalPath::fromPlatformEncodedAbsolute(std::move(di.first)));
    }
#endif

    reportLoggedInChanges();
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
    if (!syncs.clientThreadActions.empty())
    {
        nds = Waiter::ds;
        return Waiter::NEEDEXEC;
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

        // newly queued transfers
        if (nextDispatchTransfersDs)
            nds = std::max(nextDispatchTransfersDs, Waiter::ds.load());

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

        if (!mReqStatCS && mReqStatEnabled && sid.size())
        {
            btreqstat.update(&nds);
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

    waiter->wakeupby(fsaccess.get(), Waiter::NEEDEXEC);

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

    int r =  httpio->checkevents(waiter.get());
    r |= fsaccess->checkevents(waiter.get());
    if (gfx)
    {
        r |= gfx->checkevents(waiter.get());
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
                for (auto& it : multi_transfers[d])
                {
                    if (it.second->bt.arm())
                    {
                        r = true;
                    }

                    if (it.second->slot && it.second->slot->retrying)
                    {
                        if (it.second->slot->retrybt.arm())
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
    if (CancelToken::haveAnyCancelsOccurredSince(lastKnownCancelCount))
    {
        // first deal with the possibility of cancelled transfers
        // only do this if any cancel tokens were activated
        // as walking the whole list on every exec() would be too expensive for large sets of transfers
        static direction_t putget[] = { PUT, GET };
        for (direction_t direction : putget)
        {
            TransferDbCommitter committer(tctable);
            auto& directionList = multi_transfers[direction];
            for (auto i = directionList.begin(); i != directionList.end(); )
            {
                auto it = i++;  // in case this entry is removed
                Transfer* transfer = it->second;

                transfer->removeCancelledTransferFiles(&committer);
                if (transfer->files.empty())
                {
                    // this also removes it from slots
                    transfer->removeAndDeleteSelf(TRANSFERSTATE_CANCELLED);
                }
            }
        }
    }

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
        double total = 0;
        double added = 0;
        bool hasVeryBig = false;

        void addexisting(m_off_t size, m_off_t progressed, double transferWeight)
        {
            remainingsum += size - progressed;
            total += transferWeight;
            if (size > 100 * 1024 * 1024 && (size - progressed) > 5 * 1024 * 1024)
            {
                hasVeryBig = true;
            }
        }
        void addnew(m_off_t size, double transferWeight)
        {
            addexisting(size, 0, transferWeight);
            added += transferWeight;
        }
    };
    std::array<counter, 6> counters;

    // Exponential function to calculate the maximum transfer queue size
    // This function uses a threshold (in KB/s) so the function has two different behaviors:
    // 1. Before the threshold, the function grows slowly from the minSize to the maxSize
    // 2. After the threshold, the function grows very quickly to the maxSize
    // This allows us to optimize the queue limit based on throughput.
    auto calcDynamicQueueLimit = [this]() -> unsigned
    {
        // Define the minimum and maximum scaling factors
        const int minScalingFactor = 2000; // KB/s
        const int maxScalingFactor = 20000; // KB/s

        // Define the minimum and maximum size limits
        const int minSize = MIN_MAXTRANSFERS; // Adjusted minimum size
        const int maxSize = MAXTRANSFERS;

        const int threshold = 16500; // Threshold (KB/S) to activate the additional term -> before this threshold, dynamic size grows slowly from minSize to maxSize. After the threshold, it will quickly grow to maxSize.
        if (threshold <= minScalingFactor)
        {
            LOG_err << "[calcDynamicQueueLimit] threshold (" << threshold << ") IS SMALLER OR EQUAL minScalingFactor(" << minScalingFactor << ") !!!!!!!!!! This must be fixed!!!!";
            assert(false && "[calcDynamicQueueLimit] thresold <= minScalingFactor!");
            return maxSize;
        }

        // Use an expontential function to obtain a very low growth rate before the threshold
        m_off_t throughputInKBPerSec = std::min<m_off_t>(httpio->downloadSpeed / 1024, minScalingFactor); // KB/s
        const double reductiveGrowthMultiplier = 0.12; // This allows us to keep the scaling factor within a very low growth rate
        double scaleFactor = (static_cast<double>(throughputInKBPerSec - minScalingFactor) / (threshold - minScalingFactor)) * reductiveGrowthMultiplier;
        double size = minSize + (maxSize - minSize) * (1 - exp(-scaleFactor));
        size = std::min(size, static_cast<double>(maxSize));

        if (throughputInKBPerSec >= threshold)
        {
            double minSizeAfterThreshold = size;
            scaleFactor = static_cast<double>(throughputInKBPerSec - threshold) / (maxScalingFactor - threshold);

            // Calculate size using exponential function with an additional term for a high growth rate after the threshold
            double additionalTerm = 20 * log(1 + static_cast<double>(throughputInKBPerSec - threshold) / threshold);
            double sizeAfterThreshold = minSizeAfterThreshold + (maxSize - minSizeAfterThreshold) * (1 - exp(-scaleFactor)) + additionalTerm;
            size = std::min(sizeAfterThreshold, static_cast<double>(maxSize));
        }

        //LOG_verbose << "[calcDynamicQueueSize] customLimit = " << size << " [throughput = " << (throughputInKBPerSec) << " KB/s]";
#if defined(__ANDROID__) || defined(USE_IOS)
        return std::min<unsigned>(static_cast<unsigned>(size), MAX_RAIDTRANSFERS_FOR_MOBILE);
#else
        return static_cast<unsigned>(size);
#endif
    };

    auto calcTransferWeight = [this, &calcDynamicQueueLimit](mega::direction_t transferDirection, bool forceDynamicLimit = false) -> double
    {
        if (transferDirection == GET &&
            ((raidTransfersCounter >= MEANINGFUL_PORTION_OF_MAXTRANSFERS_QUEUE_FOR_RAID_PREDICTIVE_SYSTEM) // 1/6 of the hard limit
            || forceDynamicLimit))
        {
            double averageFileSize = 0;
            for (TransferSlot* ts : tslots)
            {
                averageFileSize += (static_cast<double>(ts->transfer->size) / static_cast<double>(tslots.size())); // Take into account VERY LARGE FILE SIZES, that's why I divide for each iteration and not at the end
            }
            unsigned dynamicQueueLimit;
            const unsigned maxAverageFilesizeForFixedLimit = 2 * 1024 * 1024; // 2MB, it's better to used a fixed limit (and a larger queue max size) for transfer queues whose average filesize is smaller than this value
            if (static_cast<unsigned>(averageFileSize) <= maxAverageFilesizeForFixedLimit) // Truncate double averageFileSize (2,x MB ≡ 2MB)
            {
#if defined(__ANDROID__) || defined(USE_IOS)
                dynamicQueueLimit = MAX_RAIDTRANSFERS_FOR_MOBILE;
#else
                dynamicQueueLimit = MAXTRANSFERS + 10;
#endif
            }
            else
            {
                dynamicQueueLimit = calcDynamicQueueLimit();
            }
            double transferWeight = static_cast<double>(MAXTRANSFERS) / static_cast<double>(dynamicQueueLimit); 
            //LOG_verbose << "[calcTransferWeight] raidTransfersCounter = " << raidTransfersCounter << ", >= " << MEANINGFUL_PORTION_OF_MAXTRANSFERS_QUEUE_FOR_RAID_PREDICTIVE_SYSTEM << " -> transferWeight = " << transferWeight << " [tslots = " << tslots.size() << "] [dynamicQueueLimit = " << dynamicQueueLimit << "] [averageFileSize = " << (averageFileSize / 1024) << " KBs]";
            return transferWeight;
        }
        return 1;
    };

    // Determine average speed and total amount of data remaining for the given direction/size-category
    // We prepare data for put/get in index 0..1, and the put/get/big/small combinations in index 2..5
    for (TransferSlot* ts : tslots)
    {
        assert(ts->transfer->type == PUT || ts->transfer->type == GET);
        double transferWeightKnown = 0.0;
        TransferCategory tc(ts->transfer);
        if (ts->transfer->type == GET)
        {
            if (!ts->transfer->tempurls.empty())
            {
                if (ts->transferbuf.isNewRaid()) // Raid
                {
                    // Keep the counter within the limit to avoid overrepresentation: 1/6 of max transfers
                    // i.e., if the counter has already reached that value, we don't continue increasing it
                    if (raidTransfersCounter < MEANINGFUL_PORTION_OF_MAXTRANSFERS_QUEUE_FOR_RAID_PREDICTIVE_SYSTEM) raidTransfersCounter += 1;
                    transferWeightKnown = calcTransferWeight(tc.direction, true); // We already know this transfer is raided, force the dynamic calculation
                }
                else // Old raid or non-raid
                {
                    // As we kept the raid counter within a max limit (1/6), we obtain an accurate representation by decreasing the counter every time we find a non-raid transfer.
                    if (raidTransfersCounter > 1) raidTransfersCounter -= 1;
                    transferWeightKnown = 1.0; // We alredy know this transfer is non-raided, the weight should be 1.
                }
            }
        }
        auto transferWeight = transferWeightKnown != 0.0 ? transferWeightKnown : calcTransferWeight(tc.direction);
        counters[tc.index()].addexisting(ts->transfer->size, ts->progressreported, transferWeight);
        counters[tc.directionIndex()].addexisting(ts->transfer->size, ts->progressreported, transferWeight);
    }
    if (tslots.empty())
    {
        if (raidTransfersCounter != 0) { LOG_verbose << "[MegaClient::dispatchTransfers] reset raidTransfersCounter to 0!!! [raidTransfersCounter = " << raidTransfersCounter << "]"; }
        raidTransfersCounter = 0;
    }

    std::function<bool(direction_t)> continueDirection = [this, &counters](direction_t putget)
    {
        if (Waiter::ds % 50 == 0) // Avoid to log too frequently, do it every 5 secs
        {
            LOG_verbose << "[continueDirection] counters[putget].total = " << std::round(counters[putget].total) << ", counters[putget].added = " << std::round(counters[putget].added) << ", MAXTRANSFERS = " << MAXTRANSFERS << " [tslots = " << tslots.size() << "]";;
        }
            // hard limit on puts/gets
            if (static_cast<unsigned>(std::round(counters[putget].total)) >= MAXTRANSFERS)
            {
                return false;
            }

            // only request half the max at most, to get a quicker response from the API and get overlap with transfers going
            if (static_cast<unsigned>(std::round(counters[putget].added)) >= MAXTRANSFERS/2)
            {
                return false;
            }

            return true;
    };

    std::function<bool(Transfer*)> testAddTransferFunction = [&counters, this, &calcTransferWeight](Transfer* t)
        {
            TransferCategory tc(t);

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

            auto transferWeight = calcTransferWeight(tc.direction);
            counters[tc.index()].addnew(t->size, transferWeight);
            counters[tc.directionIndex()].addnew(t->size, transferWeight);

            return true;
        };

    TransferDbCommitter committer(tctable);

    std::array<vector<Transfer*>, 6> nextInCategory = transferlist.nexttransfers(testAddTransferFunction, continueDirection, committer);

    // Iterate the 4 combinations in this order:
    static const TransferCategory categoryOrder[] = {
        TransferCategory(PUT, LARGEFILE),
        TransferCategory(GET, LARGEFILE),
        TransferCategory(PUT, SMALLFILE),
        TransferCategory(GET, SMALLFILE),
    };

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
                        if ((*it)->hprivate && !(*it)->hforeign && !(*it)->undelete())
                        {
                            // Make sure we have the size field
                            std::shared_ptr<Node> n = nodeByHandle((*it)->h);
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
                    (*it)->prepare(*fsaccess);
                }
                assert(nexttransfer->localfilename.isAbsolute());

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
                        LOG_debug << "Starting async open: "
                                  << nexttransfer->localfilename;

                        // try to open file (PUT transfers: open in nonblocking mode)
                        nexttransfer->asyncopencontext.reset( (nexttransfer->type == PUT)
                            ? ts->fa->asyncfopen(nexttransfer->localfilename, FSLogging::logOnError)
                            : ts->fa->asyncfopen(nexttransfer->localfilename, false, true, nexttransfer->size));
                        asyncfopens++;
                    }

                    if (nexttransfer->asyncopencontext->finished)
                    {
                        LOG_debug << "Async open finished: "
                                  << nexttransfer->localfilename;

                        openok = !nexttransfer->asyncopencontext->failed;
                        openfinished = true;
                        nexttransfer->asyncopencontext.reset();
                        asyncfopens--;
                        ts->fa->fopenSucceeded = openok;
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
                    LOG_debug << "Sync open: "
                              << nexttransfer->localfilename;

                    openok = (nexttransfer->type == PUT)
                        ? ts->fa->fopen(nexttransfer->localfilename, FSLogging::logOnError)
                        : ts->fa->fopen(nexttransfer->localfilename, false, true, FSLogging::logOnError);
                    openfinished = true;
                }

                if (openfinished && openok)
                {
                    NodeHandle h;
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

                        m_off_t progresscontiguous = ts->updatecontiguousprogress();

                        LOG_debug << "Resuming transfer at " << nexttransfer->pos
                            << " Completed: " << nexttransfer->progresscompleted
                            << " Contiguous: " << progresscontiguous
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
                            LOG_warn << "Modification detected starting upload."
                                     << " Path: "
                                     << nexttransfer->localfilename
                                     << " Size: "
                                     << nexttransfer->size
                                     << " Mtime: "
                                     << nexttransfer->mtime
                                     << " FaSize: "
                                     << ts->fa->size
                                     << " FaMtime: "
                                     << ts->fa->mtime;

                            nexttransfer->failed(API_EREAD, committer);
                            continue;
                        }

                        // create thumbnail/preview imagery, if applicable (FIXME: do not re-create upon restart)
                        if (!nexttransfer->localfilename.empty() && nexttransfer->uploadhandle.isUndef())
                        {
                            nexttransfer->uploadhandle = mUploadHandle.next();

                            if (!gfxdisabled && gfx && gfx->isgfx(nexttransfer->localfilename))
                            {
                                // we want all imagery to be safely tucked away before completing the upload, so we bump minfa
                                int bitmask = gfx->gendimensionsputfa(nexttransfer->localfilename, NodeOrUploadHandle(nexttransfer->uploadhandle), nexttransfer->transfercipher(), -1);

                                if (bitmask & (1 << GfxProc::THUMBNAIL))
                                {
                                    fileAttributesUploading.setFileAttributePending(nexttransfer->uploadhandle, GfxProc::THUMBNAIL, nexttransfer);
                                }

                                if (bitmask & (1 << GfxProc::PREVIEW))
                                {
                                    fileAttributesUploading.setFileAttributePending(nexttransfer->uploadhandle, GfxProc::PREVIEW, nexttransfer);
                                }
                            }
                        }
                    }
                    else
                    {
                        for (file_list::iterator it = nexttransfer->files.begin();
                            it != nexttransfer->files.end(); it++)
                        {
                            if ((*it)->undelete() || !(*it)->hprivate || (*it)->hforeign || nodeByHandle((*it)->h))
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
                            : new CommandGetFile(this, ts->transfer->transferkey.data(), SymmCipher::KEYLENGTH,
                                                 (!ts->transfer->files.empty() && ts->transfer->files.front()->undelete()),
                                                 h.as8byte(), hprivate, privauth, pubauth, chatauth, false,
                            [this, ts, hprivate, h](const Error &e, m_off_t s, dstime tl /*timeleft*/,
                               std::string* filename, std::string* /*fingerprint*/, std::string* /*fileattrstring*/,
                               const std::vector<std::string> &tempurls, const std::vector<std::string> &/*ips*/)
                        {
                            auto tslot = ts;
                            auto priv = hprivate;

                            tslot->pendingcmd = nullptr;

                            if (!filename) //failed! (Notice: calls not coming from !callFailedCompletion) will allways have that != nullptr
                            {
                                assert(s == -1 && "failing a transfer too soon: coming from a successful mCompletion call");
                                tslot->transfer->failed(e, *mTctableRequestCommitter);
                                return true;
                            }

                            if (s >= 0 && s != tslot->transfer->size)
                            {
                                tslot->transfer->size = s;
                                for (file_list::iterator it = tslot->transfer->files.begin(); it != tslot->transfer->files.end(); it++)
                                {
                                    (*it)->size = s;
                                }

                                if (priv)
                                {
                                    std::shared_ptr<Node> n = mNodeManager.getNodeByHandle(h);
                                    if (n)
                                    {
                                        n->size = s;
                                        mNodeManager.notifyNode(n);
                                    }
                                }

                                sendevent(99411, "Node size mismatch", 0);
                            }

                            tslot->starttime = tslot->lastdata = waiter->ds;

                            if ((tempurls.size() == 1 || tempurls.size() == RAIDPARTS) && s >= 0)
                            {
                                tslot->transfer->tempurls = tempurls;
                                tslot->transfer->downloadFileHandle = h;
                                tslot->transferbuf.setIsRaid(tslot->transfer, tempurls, tslot->transfer->pos, tslot->maxRequestSize);
                                tslot->progress();
                                return true;
                            }

                            if (e == API_EOVERQUOTA && tl <= 0)
                            {
                                // default retry interval
                                tl = MegaClient::DEFAULT_BW_OVERQUOTA_BACKOFF_SECS;
                            }

                            tslot->transfer->failed(e, *mTctableRequestCommitter, e == API_EOVERQUOTA ? tl * 10 : 0);
                            return true;

                        })));
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
                    if (nexttransfer->type == GET)
                    {
                        LOG_err << "Error dispatching transfer. Temporary file not writable: " << nexttransfer->localfilename;
                        nexttransfer->failed(API_EWRITE, committer);
                    }
                    else if (!ts->fa->retry)
                    {
                        LOG_err << "Error dispatching transfer. Local file permanently unavailable: " << nexttransfer->localfilename;
                        nexttransfer->failed(API_EREAD, committer);
                    }
                    else
                    {
                        LOG_warn << "Error dispatching transfer. Local file temporarily unavailable: " << nexttransfer->localfilename;
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

// do we have an upload that is still waiting for file attributes before being completed?
void MegaClient::checkfacompletion(UploadHandle th, Transfer* t, bool uploadCompleted)
{
    // For any particular transfer, this is called first with t supplied, when the file's data upload completes.
    // Calls after that are with !t, to check if we have all the file attributes yet, so we can make the putnodes call
    assert(!th.isUndef());

    if (auto uploadFAPtr = fileAttributesUploading.lookupExisting(th))
    {
        assert(!t || uploadFAPtr->transfer == t);
        t = uploadFAPtr->transfer;

        if (uploadCompleted)
        {
            uploadFAPtr->uploadCompleted = true;

            multi_transfers[t->type].erase(t->transfers_it);
            t->transfers_it = multi_transfers[t->type].end();

            delete t->slot;
            t->slot = NULL;
        }

        // abort if upload still running
        if (!uploadFAPtr->uploadCompleted)
        {
            LOG_debug << "Upload still running checking a file attribute - " << th;
            return;
        }

        assert(uploadFAPtr->transfer && uploadFAPtr->transfer->type == PUT);

        // do we have all the required the file attributes available? complete upload.
        int numUnresolvedFA = 0;
        for (auto& i : uploadFAPtr->pendingfa)
        {
            if (!i.second.valueIsSet) ++numUnresolvedFA;  // todo: fa_media
        }

        if (numUnresolvedFA)
        {
            LOG_debug << "Pending file attributes for upload - " << th <<  " : " << numUnresolvedFA;
            return;
        }
    }

    if (!t) return;

    LOG_debug << "Transfer finished, sending callbacks - " << th;
    t->state = TRANSFERSTATE_COMPLETED;
    t->completefiles();

    app->transfer_complete(t);
    delete t;
}

// clear transfer queue
void MegaClient::freeq(direction_t d)
{
    TransferDbCommitter committer(tctable);
    for (auto transferPtr : multi_transfers[d])
    {
        transferPtr.second->mOptimizedDelete = true;  // so it doesn't remove itself from this list while deleting
        app->transfer_removed(transferPtr.second);
        delete transferPtr.second;
    }
    multi_transfers[d].clear();
    transferlist.transfers[GET].clear();
    transferlist.transfers[PUT].clear();
}

bool MegaClient::isFetchingNodesPendingCS()
{
    return pendingcs && pendingcs->includesFetchingNodes;
}

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

    for (auto it = activefa.begin(); it != activefa.end(); it++)
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

    if (mReqStatCS)
    {
        mReqStatCS->disconnect();
    }

    httpio->lastdata = NEVER;
    httpio->disconnect();

    app->notify_disconnect();
}

// force retrieval of pending actionpackets immediately
// by closing pending sc, reset backoff and clear waitd URL
void MegaClient::catchup()
{
    mPendingCatchUps++;
    if (pendingsc && !jsonsc.pos)
    {
        LOG_debug << "Terminating pendingsc connection for catchup.   Pending: " << mPendingCatchUps;
        pendingsc->disconnect();
        pendingsc.reset();
    }
    btsc.reset();
}

void MegaClient::abortlockrequest()
{
    workinglockcs.reset();
    btworkinglock.reset();
    requestLock = false;
    disconnecttimestamp = NEVER;
}

void MegaClient::logout(bool keepSyncConfigsFile, CommandLogout::Completion completion)
{
    // Avoids us having to check validity later.
    if (!completion)
    {
        completion = [](error) { };
    }

    if (loggedin() != FULLACCOUNT)
    {
        locallogout(true, keepSyncConfigsFile);

        restag = reqtag;
        completion(API_OK);
        reportLoggedInChanges();
        return;
    }

    loggingout++;

    auto sendFinalLogout = [keepSyncConfigsFile, completion, this](){
        reqs.add(new CommandLogout(this, std::move(completion), keepSyncConfigsFile));
    };

#ifdef ENABLE_SYNC
    syncs.prepareForLogout(keepSyncConfigsFile, [this, keepSyncConfigsFile, sendFinalLogout](){
        syncs.locallogout(true, keepSyncConfigsFile, false);
        sendFinalLogout();
    });
#else
    sendFinalLogout();
#endif
}

void MegaClient::locallogout(bool removecaches, bool keepSyncsConfigFile)
{
    LOG_debug << clientname << "executing locallogout processing";  // track possible lack of logout callbacks
    executingLocalLogout = true;

    mAsyncQueue.clearDiscardable();

    mV1PswdVault.reset();

#ifdef ENABLE_SYNC
    syncs.locallogout(removecaches, keepSyncsConfigFile, false);

    // Process any lingering client actions.
    if (!syncs.clientThreadActions.empty())
    {
        TransferDbCommitter committer(tctable);
        Syncs::QueuedClientFunc func;

        while (syncs.clientThreadActions.popFront(func))
        {
            func(*this, committer);
        }
    }
#endif

    if (removecaches)
    {
        removeCaches();
    }

    // Deinitialize the FUSE Client Adapter.
    //
    // Keep in mind that at this point, FUSE mounts may be active and one or
    // more threads may be executing in the client when this call is made.
    //
    // That is, some threads may be in the process of asking the client for
    // information about some node, trying to start an upload or even be
    // waiting on the completion of a download.
    //
    // What this call does, is execute any tasks queued for execution and
    // sets a marker so that in the future if a thread queues a task for
    // execution, that task completes immediately as if it were cancelled.
    //
    // The marker also ensures that threads no longer come into the client
    // proper when trying to learn something about a node. Instead, all
    // requests for information for some node fail as it the node didn't
    // exist.
    //
    // While many threads may be in the client when call begins, all such
    // threads will have escaped by the time the call completes.
    //
    // Note, however, that when this completes, some threads may still be
    // waiting for the completion of some task such as a transfer or a
    // client-server request such as a putnodes.
    //
    // Threads blocked on some transfer will be awoken later when the
    // client tears down the transfer queues.
    //
    // Threads blocked on some request like putnodes will be awoken below
    // when all pending requests are "abandoned."
    mFuseClientAdapter.deinitialize();

    sctable.reset();
    mNodeManager.setTable(nullptr);
    pendingsccommit = false;

    statusTable.reset();

    me = UNDEF;
    uid.clear();
    unshareablekey.clear();
    mFolderLink.mPublicHandle = UNDEF;
    mFolderLink.mWriteAuth.clear();
    cachedscsn = UNDEF;
    achievements_enabled = false;
    isNewSession = false;
    tsLogin = 0;
    versions_disabled = false;
    accountsince = 0;
    gmfa_enabled = false;
    ssrs_enabled = false;
    aplvp_enabled = false;
    mNewLinkFormat = false;
    mCookieBannerEnabled = false;
    mABTestFlags.clear();
    mFeatureFlags.clear();
    mProFlexi = false;
    mSmsVerificationState = SMS_STATE_UNKNOWN;
    mSmsVerifiedPhone.clear();
    loggingout = 0;
    mOnCSCompletion = nullptr;
    cachedug = false;
    minstreamingrate = -1;
    ephemeralSession = false;
    ephemeralSessionPlusPlus = false;
#ifdef USE_MEDIAINFO
    mediaFileInfo = MediaFileInfo();
#endif
    mSets.clear();
    mSetElements.clear();
    stopSetPreview();

#ifdef ENABLE_CHAT
    mSfuid = sfu_invalid_id;
#endif

    // remove any cached transfers older than two days that have not been resumed (updates transfer list)
    purgeOrphanTransfers();

    // delete all remaining transfers (optimized not to remove from transfer list one by one)
    // transfer destructors update the transfer in the cache database
    freeq(GET);
    freeq(PUT);

    disconnect();

    // commit and close the transfer cache database.
    if (tctable && tctable->getTransactionCommitter())
    {
        auto committer = dynamic_cast<TransferDbCommitter*>(tctable->getTransactionCommitter());
        if (committer)
        {
            // If we don't commit the last changes to the transfer database here, they would be reverted in closetc()
            // freeq() has its own committer, but it doesn't do anything because it's usually nested by the one
            // in the intermediate layer (MegaApiImpl::sendPendingTransfers).
            committer->commitNow();
        }
    }

    closetc();

    freeq(GET);  // freeq after closetc due to optimizations
    freeq(PUT);

    // Deinitialize the FUSE Service.
    mFuseService.deinitialize();

    purgenodesusersabortsc(false);
    userid = 0;
    mNodeManager.reset();

    reqs.clear();

    delete pendingcs;
    pendingcs = NULL;
    scsn.clear();
    mBlocked = false;
    mBlockedSet = false;

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
    mCachedStatus.clear();
    scpaused = false;
    mLargestEverSeenScSeqTag.clear();

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

    fileAttributesUploading.clear();

    // erase keys & session ID
    resetKeyring();

    key.setkey(SymmCipher::zeroiv);
    tckey.setkey(SymmCipher::zeroiv);
    asymkey.resetkey();
    mPrivKey.clear();
    pubk.resetkey();
    sessionkey.clear();
    accountversion = 0;
    accountsalt.clear();
    sid.clear();
    k.clear();

    mAuthRings.clear();
    mAuthRingsTemp.clear();
    mPendingContactKeys.clear();

    reportLoggedInChanges();
    mLastLoggedInReportedState = NOTLOGGEDIN;
    syncsAlreadyLoadedOnStatecurrent = false;
    fetchnodesAlreadyCompletedThisSession = false;

    init();

    if (dbaccess)
    {
        dbaccess->currentDbVersion = DbAccess::LEGACY_DB_VERSION;
    }

    executingLocalLogout = false;
    mMyAccount = MyAccountData{};
    mKeyManager.reset();

    mLastErrorDetected = REASON_ERROR_NO_ERROR;
}

void MegaClient::removeCaches()
{
    mJourneyId.resetCacheAndValues();

    if (sctable)
    {
        mNodeManager.setTable(nullptr);
        sctable->remove();
        sctable.reset();
        pendingsccommit = false;
    }

    if (statusTable)
    {
        statusTable->remove();
        statusTable.reset();
    }

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

void MegaClient::sendchatstats(const char *json, int port)
{
    GenericHttpReq *req = new GenericHttpReq(rng);
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = SFUSTATSURL;
    if (port > 0)
    {
        req->posturl.append(":");
        char stringPort[6];
        snprintf(stringPort, sizeof(stringPort), "%d", static_cast<uint16_t>(port));
        req->posturl.append(stringPort);
    }
    req->posturl.append("/stats");
    req->protect = true;
    req->out->assign(json);
    req->post(this);
}

void MegaClient::sendchatlogs(const char *json, handle userid, handle callid, int port)
{
    GenericHttpReq *req = new GenericHttpReq(rng);
    req->tag = reqtag;
    req->maxretries = 0;
    pendinghttp[reqtag] = req;
    req->posturl = SFUSTATSURL;
    if (port > 0)
    {
        req->posturl.append(":");
        char stringPort[6];
        snprintf(stringPort, sizeof(stringPort), "%d", static_cast<uint16_t>(port));
        req->posturl.append(stringPort);
    }

    Base64Str<MegaClient::USERHANDLE> uid(userid);
    req->posturl.append("/msglog?userid=");
    req->posturl.append(uid);
    req->posturl.append("&t=e");
    if (callid != UNDEF)
    {
    Base64Str<MegaClient::USERHANDLE> cid(callid);
        req->posturl.append("&callid=");
        req->posturl.append(cid);
    }

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
    // prevent the sync thread from looking things up while we change the tree
    std::unique_lock<mutex> nodeTreeIsChanging(nodeTreeMutex);

    bool originalAC = actionpacketsCurrent;
    actionpacketsCurrent = false;

    CodeCounter::ScopeTimer ccst(performanceStats.scProcessingTime);
    nameid name;

    std::shared_ptr<Node> lastAPDeletedNode;

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
                        if (!pendingcs && !csretrying && !reqs.readyToSend())
                        {
                            LOG_debug << "DB transaction COMMIT (sessionid: " << string(sessionid, sizeof(sessionid)) << ")";
                            sctable->commit();
                            assert(!sctable->inTransaction());
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
                    if (!useralerts.isDeletedSharedNodesStashEmpty())
                    {
			useralerts.purgeNodeVersionsFromStash();
                        useralerts.convertStashedDeletedSharedNodes();
                    }


                    LOG_debug << "Processing of action packets for " << string(sessionid, sizeof(sessionid)) << " finished.  More to follow: " << insca_notlast;
                    mergenewshares(1);
                    applykeys();
                    mNewKeyRepository.clear();

                    if (!statecurrent && !insca_notlast)   // with actionpacket spoonfeeding, just finishing a batch does not mean we are up to date yet - keep going while "ir":1
                    {
                        if (fetchingnodes)
                        {
                            notifypurge();
                            if (sctable)
                            {
                                LOG_debug << "DB transaction COMMIT (sessionid: " << string(sessionid, sizeof(sessionid)) << ")";
                                sctable->commit();
                                assert(!sctable->inTransaction());
                                sctable->begin();
                                pendingsccommit = false;
                            }

                            WAIT_CLASS::bumpds();
                            fnstats.timeToResult = Waiter::ds - fnstats.startTime;
                            fnstats.timeToCurrent = fnstats.timeToResult;

                            fetchingnodes = false;
                            restag = fetchnodestag;
                            fetchnodestag = 0;

                            if (!mBlockedSet && mCachedStatus.lookup(CacheableStatus::STATUS_BLOCKED, 0)) //block state not received in this execution, and cached says we were blocked last time
                            {
                                LOG_debug << "cached blocked states reports blocked, and no block state has been received before, issuing whyamiblocked";
                                whyamiblocked();// lets query again, to trigger transition and restoreSyncs
                            }

                            enabletransferresumption();
                            app->fetchnodes_result(API_OK);
                            app->notify_dbcommit();
                            fetchnodesAlreadyCompletedThisSession = true;

                            WAIT_CLASS::bumpds();
                            fnstats.timeToSyncsResumed = Waiter::ds - fnstats.startTime;

                            if (!loggedIntoFolder())
                            {
                                // historic user alerts are not supported for public folders
                                // now that we have fetched everything and caught up actionpackets since that state,
                                // our next sc request can be for useralerts
                                useralerts.begincatchup = true;
                            }
                        }
                        else
                        {
                            WAIT_CLASS::bumpds();
                            fnstats.timeToCurrent = Waiter::ds - fnstats.startTime;
                        }
                        uint64_t numNodes = mNodeManager.getNodeCount();
                        fnstats.nodesCurrent = numNodes;

                        if (mKeyManager.generation())
                        {
                            // Clear in-use bit if needed for the shared nodes in ^!keys.
                            mKeyManager.syncSharekeyInUseBit();
                        }

                        statecurrent = true;
                        app->nodes_current();
                        mFuseService.current();
                        LOG_debug << "Cloud node tree up to date";

#ifdef ENABLE_SYNC
                        // Don't start sync activity until `statecurrent` as it could take actions based on old state
                        // The reworked sync code can figure out what to do once fully up to date.
                        nodeTreeIsChanging.unlock();
                        if (!syncsAlreadyLoadedOnStatecurrent)
                        {
                            syncs.resumeSyncsOnStateCurrent();
                            syncsAlreadyLoadedOnStatecurrent = true;
                        }
#endif

                        if (notifyStorageChangeOnStateCurrent)
                        {
                            app->notify_storage(STORAGE_CHANGE);
                            notifyStorageChangeOnStateCurrent = false;
                        }

                        if (tctable && cachedfiles.size())
                        {
                            TransferDbCommitter committer(tctable);
                            for (unsigned int i = 0; i < cachedfiles.size(); i++)
                            {
                                direction_t type = NONE;
                                File *file = app->file_resume(&cachedfiles.at(i), &type);
                                if (!file || (type != GET && type != PUT))
                                {
                                    tctable->del(cachedfilesdbids.at(i));
                                    continue;
                                }
                                file->dbid = cachedfilesdbids.at(i);
                                if (!startxfer(type, file, committer, false, false, false, UseLocalVersioningFlag, nullptr, nextreqtag()))  // TODO: should we have serialized these flags and restored them?
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
                        app->nodes_updated(NULL, int(numNodes));
                        app->users_updated(NULL, int(users.size()));
                        app->pcrs_updated(NULL, int(pcrindex.size()));
                        app->sets_updated(nullptr, int(mSets.size()));
                        app->setelements_updated(nullptr, int(mSetElements.size()));
#ifdef ENABLE_CHAT
                        app->chats_updated(NULL, int(chats.size()));
#endif
                        app->useralerts_updated(nullptr, int(useralerts.alerts.size()));
                        mNodeManager.removeChanges();

                        // if ^!keys doesn't exist yet -> migrate the private keys from legacy attrs to ^!keys
                        if (loggedin() == FULLACCOUNT)
                        {
                            if (!mKeyManager.generation())
                            {
                                assert(!mKeyManager.getPostRegistration());
                                app->upgrading_security();
                            }
                            else
                            {
                                fetchContactsKeys();
                                sc_pk();
                            }
                        }
                    }

                    {
                        // In case a fetchnodes() occurs mid-session.  We should not allow
                        // the syncs to see the new tree unless we've caught up to at least
                        // the same scsn/seqTag as we were at before.  ir:1 is not always reliable
                        bool scTagNotCaughtUp =  !mScDbStateRecord.seqTag.empty() &&
                                                 !mLargestEverSeenScSeqTag.empty() &&
                                                 (mScDbStateRecord.seqTag.size() < mLargestEverSeenScSeqTag.size() ||
                                                  (mScDbStateRecord.seqTag.size() == mLargestEverSeenScSeqTag.size() &&
                                                  mScDbStateRecord.seqTag < mLargestEverSeenScSeqTag));

                        bool ac = statecurrent && !insca_notlast && !scTagNotCaughtUp;

                        if (!originalAC && ac)
                        {
                            LOG_debug << clientname << "actionpacketsCurrent is true again";

                        }
                        actionpacketsCurrent = ac;
                    }

                    if (!insca_notlast)
                    {
                        sc_checkSequenceTag(string());
                        if (mReceivingCatchUp)
                        {
                            mReceivingCatchUp = false;
                            mPendingCatchUps--;
                            LOG_debug << "catchup complete. Still pending: " << mPendingCatchUps;
                            app->catchup_result();
                        }
                    }

                    if (pendingsccommit && sctable && !reqs.cmdsInflight() && scsn.ready())
                    {
                        LOG_debug << "Executing postponed DB commit 1";
                        sctable->commit();
                        assert(!sctable->inTransaction());
                        sctable->begin();
                        app->notify_dbcommit();
                        pendingsccommit = false;
                    }

                    if (pendingsccommit)
                    {
                        LOG_debug << "Postponing DB commit until cs requests finish (spoonfeeding)";
                    }

#ifdef ENABLE_SYNC
                    syncs.waiter->notify();
#endif

                    return true;

                case 'a':
                    if (jsonsc.enterarray())
                    {
                        LOG_debug << "Processing action packets for " << string(sessionid, sizeof(sessionid));
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
            auto actionpacketStart = jsonsc.pos;
            if (jsonsc.enterobject())
            {
                if (!sc_checkActionPacket(lastAPDeletedNode.get()))
                {
                    // We can't continue actionpackets until we know the next mCurrentSeqtag to match against, wait for the CS request to deliver it.
                    assert(reqs.cmdsInflight());
                    jsonsc.pos = actionpacketStart;
                    return false;
                }
            }
            jsonsc.pos = actionpacketStart;

            if (jsonsc.enterobject())
            {
                // the "a" attribute is guaranteed to be the first in the object
                if (jsonsc.getnameid() == 'a')
                {
                    if (!statecurrent)
                    {
                        fnstats.actionPackets++;
                    }

                    name = jsonsc.getnameidvalue();

                    // only process server-client request if not marked as
                    // self-originating ("i" marker element guaranteed to be following
                    // "a" element if present)
                    if (fetchingnodes || memcmp(jsonsc.pos, "\"i\":\"", 5)
                     || memcmp(jsonsc.pos + 5, sessionid, sizeof sessionid)
                     || jsonsc.pos[5 + sizeof sessionid] != '"'
                     || name == 'd' || name == 't')  // we still set 'i' on move commands to produce backward compatible actionpackets, so don't skip those here
                    {
#ifdef ENABLE_CHAT
                        bool readingPublicChat = false;
#endif
                        switch (name)
                        {
                            case 'u':
                                // node update
                                sc_updatenode();
                                break;

                            case 't':
                            {
                                bool isMoveOperation = false;
                                // node addition
                                {
                                    useralerts.beginNotingSharedNodes();
                                    handle originatingUser = sc_newnodes(fetchingnodes ? nullptr : lastAPDeletedNode.get(), isMoveOperation);
                                    mergenewshares(1);
                                    useralerts.convertNotedSharedNodes(true, originatingUser);
                                }
                                lastAPDeletedNode = nullptr;
                            }
                            break;

                            case 'd':
                                // node deletion
                                lastAPDeletedNode = sc_deltree();
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
                            case MAKENAMEID4('m', 'c', 'p', 'c'):
                            {
                                readingPublicChat = true;
                            } // fall-through
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

                            case MAKENAMEID5('m', 'c', 's', 'm', 'p'):
                                // scheduled meetings updates
                                sc_scheduledmeetings();
                                break;

                            case MAKENAMEID5('m', 'c', 's', 'm', 'r'):
                                // scheduled meetings removal
                                sc_delscheduledmeeting();
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

                            case MAKENAMEID4('s', 'q', 'a', 'c'):
                                // storage quota allowance changed
                                sc_sqac();
                                break;

                            case MAKENAMEID3('a', 's', 'p'):
                                // new/update of a Set
                                sc_asp();
                                break;

                            case MAKENAMEID3('a', 's', 's'):
                                sc_ass();
                                break;

                            case MAKENAMEID3('a', 's', 'r'):
                                // removal of a Set
                                sc_asr();
                                break;

                            case MAKENAMEID3('a', 'e', 'p'):
                                // new/update of a Set Element
                                sc_aep();
                                break;

                            case MAKENAMEID3('a', 'e', 'r'):
                                // removal of a Set Element
                                sc_aer();
                                break;
                            case MAKENAMEID2('p', 'k'):
                                // pending keys
                                sc_pk();
                                break;

                            case MAKENAMEID3('u', 'e', 'c'):
                                // User Email Confirm (uec)
                                sc_uec();
                                break;

                            case MAKENAMEID3('c', 'c', 'e'):
                                // credit card for this user is potentially expiring soon or new card is registered
                                sc_cce();
                                break;
                        }
                    }
                    else
                    {
                        lastAPDeletedNode = nullptr;
                    }
                }

                jsonsc.leaveobject();
            }
            else
            {
                jsonsc.leavearray();
                insca = false;
            }
        }
    }
}

size_t MegaClient::procreqstat()
{
    // reqstat packet format:
    // <num_users.2>[<userhandle.8>]<num_ops.2>[<ops.1>]<start.4><current.4><end.4>

    // data input enough for reading 2 bytes (number of users)?
    if (!mReqStatCS || mReqStatCS->in.size() < sizeof(uint16_t))
    {
        return 0;
    }

    uint16_t numUsers = MemAccess::get<uint16_t>(mReqStatCS->in.data());
    if (!numUsers)
    {
        LOG_debug << "reqstat: No operation in progress";
        app->reqstat_progress(-1);

        // resetting cs backoff here, because the account should be unlocked
        // and there should be connectivity with MEGA servers
        btcs.arm();
        return 2;
    }
    size_t startPosUsers = sizeof(uint16_t);
    size_t pos = startPosUsers + USERHANDLE * numUsers; // will read them later

    // data input enough for reading users + 2 bytes (number of operations)?
    if (mReqStatCS->in.size() < pos + sizeof(uint16_t)) // is there data for users + numOps?
    {
        return 0;
    }

    uint16_t numOps = MemAccess::get<uint16_t>(mReqStatCS->in.data() + pos);
    pos += sizeof(uint16_t);   // will read them later

    // data input enough for reading number of operations + 12 bytes (start + current + end)?
    if (mReqStatCS->in.size() < pos + numOps + 3 * sizeof(uint32_t))
    {
        return 0;
    }

    // read users
    std::ostringstream oss;
    oss << "reqstat: User " << Base64::btoa(mReqStatCS->in.substr(startPosUsers, USERHANDLE));
    if (numUsers > 1)
    {
        oss << ", affecting ";
        for (unsigned i = 1; i < numUsers; ++i)
        {
            if (i > 1)
            {
                oss << ",";
            }
            oss << Base64::btoa(mReqStatCS->in.substr(startPosUsers + USERHANDLE * i, USERHANDLE));
        }
        oss << ",";
    }

    // read operations
    if (numOps > 0)
    {
        oss << " is executing a ";
        for (unsigned i = 0; i < numOps; ++i)
        {
            if (i)
            {
                oss << "/";
            }

            if (mReqStatCS->in[pos + i] == 'p')
            {
                oss << "file or folder creation";
            }
            else
            {
                oss << "UNKNOWN operation";
            }
        }
    }
    pos += numOps;

    uint32_t start = MemAccess::get<uint32_t>(mReqStatCS->in.data() + pos); pos += sizeof(uint32_t);
    uint32_t curr = MemAccess::get<uint32_t>(mReqStatCS->in.data() + pos);  pos += sizeof(uint32_t);
    uint32_t end = MemAccess::get<uint32_t>(mReqStatCS->in.data() + pos);   pos += sizeof(uint32_t);
    float progress = 100.0f * static_cast<float>(curr) / static_cast<float>(end);

    oss << " since " << start << ", " << progress << "%";
    oss << " [" << curr << "/" << end << "]";
    LOG_debug << oss.str();

    app->reqstat_progress(1000 * curr / end);

    return pos;
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

        assert(sctable->inTransaction());
        sctable->truncate();

        // 1. write current scsn
        handle tscsn = scsn.getHandle();
        complete = sctable->put(CACHEDSCSN, (char*)&tscsn, sizeof tscsn);

        if (complete && !mScDbStateRecord.seqTag.empty())
        {
            complete = sctable->put(CACHEDDBSTATE, &mScDbStateRecord, &key);
            LOG_debug << "saving seqtag in db: " << mScDbStateRecord.seqTag;
        }

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
            // 4. write new or modified pcrs, purge deleted pcrs
            for (handlepcr_map::iterator it = pcrindex.begin(); it != pcrindex.end(); it++)
            {
                if (!(complete = sctable->put(CACHEDPCR, it->second.get(), &key)))
                {
                    break;
                }
            }
        }

        if (complete)
        {
            // 5. write Sets
            complete = initscsets();
        }

        if (complete)
        {
            // 6. write SetElements
            complete = initscsetelements();
        }

        // Nothing to do for persisting Alerts. cmd("f") will not provide any data about Alerts

#ifdef ENABLE_CHAT
        if (complete)
        {
            // 7. write new or modified chats
            for (textchat_map::iterator it = chats.begin(); it != chats.end(); it++)
            {
                if (!(complete = sctable->put(CACHEDCHAT, it->second, &key)))
                {
                    break;
                }
            }
        }
        LOG_debug << "Saving SCSN " << scsn.text() << " (sessionid: " << string(sessionid, sizeof(sessionid)) << ") with "
            << mNodeManager.getNodeCount() << " nodes, " << users.size() << " users, " << pcrindex.size() << " pcrs, "
            << mSets.size() << " sets and " << mSetElements.size() << " elements and " << chats.size() << " chats to local cache (" << complete << ")";
#else

        LOG_debug << "Saving SCSN " << scsn.text() << " (sessionid: " << string(sessionid, sizeof(sessionid)) << ") with "
            << mNodeManager.getNodeCount() << " nodes, " << users.size() << " users, " << pcrindex.size() << " pcrs, "
            << mSets.size() << " sets and " << mSetElements.size() << " elements to local cache (" << complete << ")";
#endif
        finalizesc(complete);

        if (complete)
        {
            // We have the data, and we have the corresponding scsn, all from fetchnodes finishing just now.
            // Commit now, otherwise we'll have to do fetchnodes again (on restart) if no actionpackets arrive.
            LOG_debug << "DB transaction COMMIT (sessionid: " << string(sessionid, sizeof(sessionid)) << ")";
            sctable->commit();
            assert(!sctable->inTransaction());
            sctable->begin();
            pendingsccommit = false;
        }
    }
}

void MegaClient::initStatusTable()
{
    if (statusTable)
    {
        // statusTable is different from sctable in that we begin/commit with each change
        assert(!statusTable->inTransaction());
        DBTableTransactionCommitter committer(statusTable);
        statusTable->truncate();
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
        LOG_debug << "SCSN write at DB " << tscsn << " -  " << scsn.text();

        if (complete && !mScDbStateRecord.seqTag.empty())
        {
            complete = sctable->put(CACHEDDBSTATE, &mScDbStateRecord, &key);
            LOG_debug << "saving seqtag in db: " << mScDbStateRecord.seqTag;
        }

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
                        LOG_verbose << clientname << "Removing inactive user from database: " << (Base64::btoa((byte*)&((*it)->userhandle),MegaClient::USERHANDLE,base64) ? base64 : "");
                        if (!(complete = sctable->del((*it)->dbid)))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    LOG_verbose << clientname << "Adding/updating user to database: " << (Base64::btoa((byte*)&((*it)->userhandle),MegaClient::USERHANDLE,base64) ? base64 : "");
                    if (!(complete = sctable->put(CACHEDUSER, *it, &key)))
                    {
                        break;
                    }
                }
            }
        }

        // 3. write new or modified nodes, purge deleted pcrs
        // NoD -> nodes are written to DB immediately

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

        if (complete)
        {
            // 5. write new or modified Sets, purge deleted ones
            complete = updatescsets();
        }

        if (complete)
        {
            // 6. write new or modified SetElements, purge deleted ones
            complete = updatescsetelements();
        }

#ifdef ENABLE_CHAT
        if (complete)
        {
            // 6. write new or modified chats
            for (textchat_map::iterator it = chatnotify.begin(); it != chatnotify.end(); it++)
            {
                LOG_verbose << "Adding chat to database: " << Base64Str<sizeof(handle)>(it->second->getChatId());
                if (!(complete = sctable->put(CACHEDCHAT, it->second, &key)))
                {
                    break;
                }
            }
        }
        LOG_debug << "Saving SCSN " << scsn.text() << " (sessionid: " << string(sessionid, sizeof(sessionid)) << ") with "
            << mNodeManager.nodeNotifySize() << " modified nodes, " << usernotify.size() << " users, " << pcrnotify.size() << " pcrs, "
            << setnotify.size() << " sets, " << setelementnotify.size() << " elements and " << chatnotify.size() << " chats to local cache (" << complete << ")";
#else
        LOG_debug << "Saving SCSN " << scsn.text() << " (sessionid: " << string(sessionid, sizeof(sessionid)) << ") with "
            << mNodeManager.nodeNotifySize() << " modified nodes, " << usernotify.size() << " users, " << pcrnotify.size() << " pcrs, "
            << setnotify.size() << " sets, " << setelementnotify.size() << " elements to local cache (" << complete << ")";
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
        LOG_err << "Cache update DB write error";
        assert(false);
    }
}

// queue node file attribute for retrieval or cancel retrieval
error MegaClient::getfa(handle h, string *fileattrstring, const string &nodekey, fatype t, int cancel)
{
    assert((cancel && nodekey.empty()) ||
          (!cancel && !nodekey.empty()));

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
void MegaClient::pendingattrstring(UploadHandle h, string* fa)
{
    char buf[128];

    if (auto uploadFAPtr = fileAttributesUploading.lookupExisting(h))
    {
        for (auto& it : uploadFAPtr->pendingfa)
        {
            if (it.first != fa_media)
            {
                snprintf(buf, sizeof(buf), "/%u*", (unsigned)it.first);
                Base64::btoa((byte*)&it.second.fileAttributeHandle, sizeof(it.second.fileAttributeHandle), strchr(buf + 3, 0));
                fa->append(buf + !fa->size());
                LOG_debug << "Added file attribute " << it.first << " to putnodes";
            }
        }
    }
}

// Upload file attribute data to fa servers. node handle can be UNDEF if we are giving fa handle back to the app
// Used for attaching file attribute to a Node, or prepping for Node creation after upload, or getting fa handle for app.
// FIXME: to avoid unnecessary roundtrips to the attribute servers, also cache locally
bool MegaClient::putfa(NodeOrUploadHandle th, fatype t, SymmCipher* key, int tag, std::unique_ptr<string> data)
{
    // CBC-encrypt attribute data (padded to next multiple of BLOCKSIZE)
    data->resize((data->size() + SymmCipher::BLOCKSIZE - 1) & -SymmCipher::BLOCKSIZE);
    if (!key->cbc_encrypt((byte*)data->data(), data->size()))
    {
        LOG_err << "Failed to CBC encrypt Node attribute data.";
        return false;
    }

    queuedfa.emplace_back(new HttpReqFA(th, t, usehttps, tag, std::move(data), true, this));
    LOG_debug << "File attribute added to queue - " << th << " : " << queuedfa.size() << " queued, " << activefa.size() << " active";

    // no other file attribute storage request currently in progress? POST this one.
    activatefa();
    return true;
}

void MegaClient::activatefa()
{
    while (activefa.size() < MAXPUTFA && queuedfa.size())
    {
        auto curfa = queuedfa.begin();
        shared_ptr<HttpReqFA> fa = *curfa;
        queuedfa.erase(curfa);
        activefa.push_back(fa);

        LOG_debug << "Adding file attribute to the active queue";

        fa->status = REQ_GET_URL;  // will become REQ_INFLIGHT after we get the URL and start data upload.  Don't delete while the reqs subsystem would end up with a dangling pointer
        reqs.add(fa->getURLForFACmd());
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
        storagestatus_t previousStatus = ststatus;
        ststatus = status;

        mCachedStatus.addOrUpdate(CacheableStatus::STATUS_STORAGE, status);

        app->notify_storage(ststatus);

#ifdef ENABLE_SYNC
        if (previousStatus == STORAGE_PAYWALL)
        {
            mOverquotaDeadlineTs = 0;
            mOverquotaWarningTs.clear();
        }
        app->notify_storage(ststatus);
        if (status == STORAGE_RED || status == STORAGE_PAYWALL) //transitioning to OQ
        {
            syncs.disableSyncs(STORAGE_OVERQUOTA, false, true);
        }
#endif

        switch (previousStatus)
        {
        case STORAGE_UNKNOWN:
            if (!(status == STORAGE_GREEN || status == STORAGE_ORANGE))
            {
                break;
            }
            // fall-through
        case STORAGE_PAYWALL:
        case STORAGE_RED:
            // Transition from OQ.
            abortbackoff(true);
        default:
            break;
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
        reqs.add(new CommandGetUserData(this, reqtag, nullptr));
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

bool MegaClient::sc_checkSequenceTag(const string& tag)
{
    if (tag.empty())
    {
        if (!mCurrentSeqtag.empty() && mCurrentSeqtagSeen)
        {
            LOG_verbose << clientname << "st tag exhausted for " << mCurrentSeqtag;
            reqs.continueProcessing(this);
        }

        if (!mLastReceivedScSeqTag.empty())
        {
            // we've reached the end of a set of actionpackets, with a latest st to report
            app->sequencetag_update(mLastReceivedScSeqTag);
            LOG_debug << "updated seqtag: " << mLastReceivedScSeqTag;
            mLastReceivedScSeqTag.clear();
        }
        return true;
    }
    else
    {
        mLastReceivedScSeqTag = tag;
        if (mLastReceivedScSeqTag.size() > mLargestEverSeenScSeqTag.size() ||
            (mLastReceivedScSeqTag.size() == mLargestEverSeenScSeqTag.size() &&
             mLastReceivedScSeqTag > mLargestEverSeenScSeqTag))
        {
            mLargestEverSeenScSeqTag = mLastReceivedScSeqTag;
        }

        // Also prep this one to be committed to db (if we delay until time to notify the app, we're too late for the commit)
        mScDbStateRecord.seqTag = mLastReceivedScSeqTag;

        for (;;)
        {
            if (mCurrentSeqtag.empty())
            {
                if (reqs.cmdsInflight())
                {
                    LOG_verbose << clientname << "st tag " << tag << " wait for cs requests";
                    return false;  // we can't tell yet if a command will give us a tag to wait for
                }
                else
                {
                    if (statecurrent)
                    {
                        // too much logging during catch-up otherwise
                        LOG_verbose << clientname << "st tag " << tag << " no tag pending";
                    }
                    return true;  // nothing pending, process everything
                }
            }
            else
            {
                if (tag == mCurrentSeqtag)
                {
                    LOG_verbose << clientname << "st tag " << tag << " matched";
                    // there may be more than one, process until a different one arrives
                    mCurrentSeqtagSeen = true;
                    return true;
                }
                else if (mCurrentSeqtagSeen)
                {
                    LOG_verbose << clientname << "st tag " << tag << " processing for " << mCurrentSeqtag;
                    reqs.continueProcessing(this);
                    continue;   // we may have a new mCurrentSeqtag now
                }
                else
                {
                    // We know there is a mCurrentSeqtag that we must receive, but we have not encountered it yet.  Continue with actionpackets
                    LOG_verbose << clientname << "current st tag " << mCurrentSeqtag;
                    LOG_verbose << clientname << "st tag " << tag << " catching up";
                    assert(tag.size() < mCurrentSeqtag.size() || (tag.size() == mCurrentSeqtag.size() && tag < mCurrentSeqtag));
                    return true;
                }
            }
            break;
        }
    }
}

bool MegaClient::sc_checkActionPacket(Node* lastAPDeletedNode)
{
    string tag;
    nameid cmd = 0;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
        case 'a':   // action referred by the packet
            cmd = jsonsc.getnameid();
            break;

        case 'i':   // id of the client who made the action triggering this packet
            jsonsc.storeobject();
            break;

        case MAKENAMEID2('s', 't'): // sequence tag
            jsonsc.storeobject(&tag);
            return sc_checkSequenceTag(tag);

        default:
            // if we reach any other tag, then 'st' is not present.

            if (cmd == 't' && lastAPDeletedNode && dynamic_cast<CommandMoveNode*>(reqs.getCurrentCommand(mCurrentSeqtagSeen)))
            {
                // special case for actionpackets from the move command - the 'd'+'t' sequence has the tag on 'd' but not 't'.
                // However we must process the 't' as part of the move, and only call the command completion after.
                LOG_verbose << clientname << "st tag implicity not changing for moves";
                return true;
            }
            else
            {
                return sc_checkSequenceTag(tag);
            }
        }

    }
    return true;
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
                    std::shared_ptr<Node> n;
                    bool notify = false;

                    if ((n = mNodeManager.getNodeByHandle(NodeHandle().set6byte(h))))
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
                            JSON::copystring(n->attrstring.get(), a);
                            n->changed.attrs = true;
                            notify = true;

#ifdef ENABLE_SYNC
                            if (n->parent)
                            {
                                // possible node name change; sync parent
                                syncs.triggerSync(n->parent->nodeHandle());
                            }
#endif
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
                            if (!mCurrentSeqtag.empty() && mCurrentSeqtagSeen)
                            {
                                // if this change by this client resulted in NO_KEY, report it
                                n->changed.modifiedByThisClient = true;
                            }
                            mNodeManager.notifyNode(n);
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

void MegaClient::CacheableStatusMap::loadCachedStatus(CacheableStatus::Type type, int64_t value)
{
#ifndef NDEBUG
    auto it =
#endif
    insert(pair<int64_t, CacheableStatus>(type, CacheableStatus(type, value)));
    assert(it.second);

    LOG_verbose << "Loaded status from cache: " << CacheableStatus::typeToStr(type) << " = " << value;

    switch(type)
    {
        case CacheableStatus::Type::STATUS_STORAGE:
        {
            mClient->ststatus = static_cast<storagestatus_t>(value);
            break;
        }
        case CacheableStatus::Type::STATUS_BUSINESS:
        {
            mClient->mBizStatus = static_cast<BizStatus>(value);
            break;
        }
        default:
            break;
    }
}

bool MegaClient::CacheableStatusMap::addOrUpdate(CacheableStatus::Type type, int64_t value)
{
    bool changed = false;

    CacheableStatus status(type, value);
    auto it_bool = emplace(type, status);
    if (!it_bool.second)    // already exists
    {
        if (it_bool.first->second.value() != value)
        {
            it_bool.first->second.setValue(value); // don't replace it, or we lose the dbid
            changed = true;
        }
    }
    else // added
    {
        changed = true;
    }

    assert(mClient->statusTable && "Updating status without status table");

    if (changed && mClient->statusTable)
    {
        DBTableTransactionCommitter committer(mClient->statusTable);
        LOG_verbose << "Adding/updating status to database: " << status.typeToStr() << " = " << value;
        if (!mClient->statusTable->put(MegaClient::CACHEDSTATUS, &it_bool.first->second, &mClient->key))
        {
            LOG_err << "Failed to add/update status to db: " << status.typeToStr() << " = " << value;
        }
    }

    return changed;
}

int64_t MegaClient::CacheableStatusMap::lookup(CacheableStatus::Type type, int64_t defaultValue)
{
    auto it = find(type);
    return it == end() ? defaultValue : it->second.value();
}

CacheableStatus *MegaClient::CacheableStatusMap::getPtr(CacheableStatus::Type type)
{
    auto it = find(type);
    return it == end() ? nullptr : &it->second;
}

// read tree object (nodes and users)
void MegaClient::readtree(JSON* j, Node* priorActionpacketDeletedNode, bool& firstHandleMatchesDelete)
{
    if (j->enterobject())
    {
        for (;;)
        {
            switch (jsonsc.getnameid())
            {
                case 'f':
                    if (auto putnodesCmd = dynamic_cast<CommandPutNodes*>(reqs.getCurrentCommand(mCurrentSeqtagSeen)))
                    {
                        putnodesCmd->emptyResponse = !memcmp(j->pos, "[]", 2);
                        readnodes(j, 1, putnodesCmd->source, &putnodesCmd->nn, putnodesCmd->tag, true, priorActionpacketDeletedNode, &firstHandleMatchesDelete);
                    }
                    else
                    {
                        readnodes(j, 1, PUTNODES_APP, nullptr, false, false, priorActionpacketDeletedNode, &firstHandleMatchesDelete);
                    }
                    break;

                case MAKENAMEID2('f', '2'):
                    if (auto putnodesCmd = dynamic_cast<CommandPutNodes*>(reqs.getCurrentCommand(mCurrentSeqtagSeen)))
                    {
                        // new nodes can appear in copied version lists too
                        readnodes(j, 1, putnodesCmd->source, &putnodesCmd->nn, putnodesCmd->tag, true, nullptr, nullptr);
                    }
                    else
                    {
                        readnodes(j, 1, PUTNODES_APP, nullptr, false, false, nullptr, nullptr);
                    }
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
handle MegaClient::sc_newnodes(Node* priorActionpacketDeletedNode, bool& firstHandleMatchesDelete)
{
    handle originatingUser = UNDEF;
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 't':
                readtree(&jsonsc, priorActionpacketDeletedNode, firstHandleMatchesDelete);
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
                if (loggedin() != FULLACCOUNT)
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

                if (!k || mKeyManager.generation()) // Same logic as below but without using the key
                {
                    if (!(!ISUNDEF(oh) && (!ISUNDEF(uh) || !ISUNDEF(p))))
                    {
                        return false;
                    }

                    if(outbound && ou != me && r == ACCESS_UNKNOWN // Sharee abandoned the share.
                        && mKeyManager.generation() // ^!keys in use
                        && statecurrent)
                    {
                        // Clear the in-use bit for the share key in ^!keys if it was the last sharee
                        if (mKeyManager.isShareKeyInUse(h))
                        {
                            std::shared_ptr<Node> n = nodebyhandle(h);
                            assert(n); // Share removals are received before node deletion.
                            if (n)
                            {
                                size_t total = n->outshares ? n->outshares->size() : 0;
                                total += n->pendingshares ? n->pendingshares->size() : 0;
                                if (total == 1)
                                {
                                    // Commit to clear the bit.
                                    LOG_debug << "Last sharee has left the share. uh: " << toHandle(uh) << ". Disabling in-use flag for the sharekey in KeyManager. nh: " << toNodeHandle(h);
                                    mKeyManager.commit(
                                        [this, h]()
                                        {
                                            mKeyManager.setSharekeyInUse(h, false);
                                        });
                                }
                            }
                        }
                    }

                    if (r == ACCESS_UNKNOWN)
                    {
                        handle peer = outbound ? uh : oh;
                        if (peer != me && peer && !ISUNDEF(peer) && statecurrent && ou != me)
                        {
                            User* u = finduser(peer);
                            useralerts.add(new UserAlert::DeletedShare(peer, u ? u->email : "", oh, h, ts == 0 ? m_time() : ts, useralerts.nextId()));
                        }
                    }
                    else
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
                    }

                    newshares.push_back(new NewShare(h, outbound,
                                                     outbound ? uh : oh,
                                                     r, ts, NULL, NULL, p,
                                                     upgrade_pending_to_full,
                                                     okremoved));

                    return r == ACCESS_UNKNOWN;
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
                itemclass = int(jsonsc.getint()); // itemclass
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
                if ((itemclass == 0 || itemclass == 1) && statecurrent)
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
    std::shared_ptr<Node> n;
    sharedNode_vector kshares;
    sharedNode_vector knodes;

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
    std::shared_ptr<Node> n = NULL;
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
                    JSON::copystring(&n->fileattrstring, fa);
                    n->changed.fileattrstring = true;
                    mNodeManager.notifyNode(n);
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
                                // some attributes should be fetched upon invalidation
                                switch(type)
                                {
                                    case ATTR_KEYRING:
                                    {
                                        assert(false);
                                        resetKeyring();
                                        break;
                                    }
                                    case ATTR_MY_BACKUPS_FOLDER:
                                    // there should be no actionpackets for this attribute. It is
                                    // created and never updated afterwards
                                    LOG_err << "The node handle for My backups folder has changed";
                                    //fall-through

                                    case ATTR_KEYS:                  // fall-through
                                    case ATTR_AUTHRING:              // fall-through
                                    case ATTR_AUTHCU255:             // fall-through
                                    case ATTR_DEVICE_NAMES:          // fall-through
                                    case ATTR_JSON_SYNC_CONFIG_DATA: // fall-through
                                    {
                                        if ((type == ATTR_AUTHRING || type == ATTR_AUTHCU255) && mKeyManager.generation())
                                        {
                                            // legacy authrings not useful anymore
                                            LOG_warn << "Ignoring update of : " << User::attr2string(type);
                                            break;
                                        }

                                        LOG_debug << User::attr2string(type) << " has changed externally. Fetching...";
                                        if (type == ATTR_JSON_SYNC_CONFIG_DATA)
                                        {
                                            // this user's attribute should be set only once and never change
                                            // afterwards. If it has changed, it may indicate a race condition
                                            // setting the attribute from another client at the same time
                                            LOG_warn << "Sync config data has changed, when it should not";
                                            assert(false);
                                        }

                                        getua(u, type, 0);
                                        break;
                                    }
                                    default:
                                        LOG_debug << User::attr2string(type) << " has changed externally (skip fetching)";
                                        break;
                                }
                            }
                            else
                            {
                                LOG_info << "User attribute already up to date: " << User::attr2string(type);
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
                    JSON::copystring(&email, m);
                    useralerts.add(new UserAlert::IncomingPendingContact(dts, rts, p, email, ts, useralerts.nextId()));
                }

                pcr = pcrindex.count(p) ? pcrindex[p].get() : (PendingContactRequest *) NULL;

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
                    mappcr(p, unique_ptr<PendingContactRequest>(pcr));
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

                pcr = pcrindex.count(p) ? pcrindex[p].get() : (PendingContactRequest *) NULL;

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
                    mappcr(p, unique_ptr<PendingContactRequest>(pcr));
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

                pcr = pcrindex.count(p) ? pcrindex[p].get() : (PendingContactRequest *) NULL;

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
                    JSON::copystring(&email, m);
                    using namespace UserAlert;
                    useralerts.add(incoming ? (UserAlert::Base*) new UpdatedPendingContactIncoming(s, p, email, uts, useralerts.nextId())
                                            : (UserAlert::Base*) new UpdatedPendingContactOutgoing(s, p, email, uts, useralerts.nextId()));
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
    std::shared_ptr<Node> n;
    std::string authKey;

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
        case 'w':
            static_cast<void>(jsonsc.storeobject(&authKey));
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
                    n->plink.reset();
                }
                else
                {
                    n->setpubliclink(ph, cts, ets, takendown, authKey);
                }

                n->changed.publiclink = true;
                mNodeManager.notifyNode(n);
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

                setEmail(u, email);
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
    bool meeting = false;

    // chat options: [0 (remove) | 1 (add)], if chat option is not included on action packet, that option is disabled
    int waitingRoom = 0;
    int openInvite = 0;
    int speakRequest = 0;

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

            case MAKENAMEID2('m', 'r'):
                assert(readingPublicChat);
                meeting = jsonsc.getbool();
                break;

            case 'w': // waiting room
                waitingRoom = jsonsc.getbool();
                break;

            case MAKENAMEID2('s','r'): // speak request
                speakRequest = jsonsc.getbool();
                break;

            case MAKENAMEID2('o','i'): // open invite
                openInvite = jsonsc.getbool();
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
                    TextChat* chat = nullptr;
                    bool mustHaveUK = false;
                    privilege_t oldPriv = PRIV_UNKNOWN;
                    if (chats.find(chatid) == chats.end())
                    {
                        chat = new TextChat(readingPublicChat ? publicchat : false);
                        chats[chatid] = chat;
                        mustHaveUK = true;
                    }
                    else
                    {
                        chat = chats[chatid];
                        oldPriv = chat->getOwnPrivileges();
                        if (readingPublicChat) { setChatMode(chat, publicchat); }
                    }

                    chat->setChatId(chatid);
                    chat->setShard(shard);
                    chat->setGroup(group);
                    chat->setOwnPrivileges(PRIV_UNKNOWN);
                    chat->setOwnUser(ou);
                    chat->setTitle(title);

                    // chat->flags = ?; --> flags are received in other AP: mcfc
                    if (ts != -1)
                    {
                        chat->setTs(ts);  // only in APs related to chat creation or when you're added to
                    }
                    chat->setMeeting(meeting);

                    if (group)
                    {
                        chat->addOrUpdateChatOptions(speakRequest, waitingRoom, openInvite);
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
                                chat->setOwnPrivileges(upvit->second);
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
                                chat->setOwnPrivileges(upvit->second);
                                break;
                            }
                        }
                    }

                    if (chat->getOwnPrivileges() == PRIV_RM)
                    {
                        // clear the list of peers because API still includes peers in the
                        // actionpacket, but not in a fresh fetchnodes
                        delete userpriv;
                        userpriv = NULL;
                    }

                    chat->setUserPrivileges(userpriv);

                    if (readingPublicChat)
                    {
                        if (!unifiedkey.empty())    // not all actionpackets include it
                        {
                            chat->setUnifiedKey(unifiedkey);
                        }
                        else if (mustHaveUK)
                        {
                            LOG_err << "Public chat without unified key detected";
                        }
                    }

                    auto cmd = reqs.getCurrentCommand(mCurrentSeqtagSeen);
                    if (cmd)
                    {
                        chat->setTag(cmd->tag ? cmd->tag : -1);
                    }
                    else
                    {
                        chat->setTag(0);    // 0 for external change
                    }

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

                    auto cmd = reqs.getCurrentCommand(mCurrentSeqtagSeen);
                    if (cmd)
                    {
                        chat->setTag(cmd->tag ? cmd->tag : -1);
                    }
                    else
                    {
                        chat->setTag(0);    // 0 for external change
                    }

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

// process mcsmr action packet
void MegaClient::sc_delscheduledmeeting()
{
    bool done = false;
    handle schedId = UNDEF;
    handle ou = UNDEF;

    while(!done)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('i','d'):
                schedId = jsonsc.gethandle(MegaClient::CHATHANDLE);
                break;

            case MAKENAMEID2('o', 'u'):
                ou = jsonsc.gethandle(MegaClient::USERHANDLE);
                break;

            case EOO:
            {
                done = true;
                for (auto auxit = chats.begin(); auxit != chats.end(); auxit++)
                {
                    TextChat* chat = auxit->second;
                    if (chat->removeSchedMeeting(schedId))
                    {
                        // remove children scheduled meetings (API requirement)
                        handle_set deletedChildren = chat->removeChildSchedMeetings(schedId);
                        handle chatid = chat->getChatId();

                        if (statecurrent)
                        {
                            for_each(begin(deletedChildren), end(deletedChildren),
                                     [this, ou, chatid](handle sm) { createDeletedSMAlert(ou, chatid, sm); });

                            createDeletedSMAlert(ou, chatid, schedId);
                        }

                        clearSchedOccurrences(*chat);
                        chat->setTag(0);    // external change
                        notifychat(chat);
                        break;
                    }

                }
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

// process mcsmp action packet (parse just 1 scheduled meeting per AP)
void MegaClient::sc_scheduledmeetings()
{
    handle ou = UNDEF;
    std::vector<std::unique_ptr<ScheduledMeeting>> schedMeetings;
    UserAlert::UpdatedScheduledMeeting::Changeset cs;
    handle_set childMeetingsDeleted;

    error e = parseScheduledMeetings(schedMeetings, false, &jsonsc, true, &ou, &cs, &childMeetingsDeleted);
    if (e != API_OK)
    {
        LOG_err << "Failed to parse 'mcsmp' action packet. Error: " << e;
        return;
    }

    assert(schedMeetings.size() == 1);
    for (auto &sm: schedMeetings)
    {
        textchat_map::iterator it = chats.find(sm->chatid());
        if (it == chats.end())
        {
            LOG_err << "Unknown chatid [" <<  Base64Str<MegaClient::CHATHANDLE>(sm->chatid()) << "] received on mcsmp";
            continue;
        }
        TextChat* chat = it->second;
        handle schedId = sm->schedId();
        handle parentSchedId = sm->parentSchedId();
        m_time_t overrides = sm->overrides();
        bool isNewSchedMeeting = !chat->hasScheduledMeeting(schedId);

        // remove child scheduled meetings in cmd (child meetings deleted) array
        chat->removeSchedMeetingsList(childMeetingsDeleted);

        // update scheduled meeting with updated record received at mcsmp AP
        bool res = chat->addOrUpdateSchedMeeting(std::move(sm));

        if (res || !childMeetingsDeleted.empty())
        {
            if (!res)
            {
                LOG_debug << "Error adding or updating a scheduled meeting schedId [" <<  Base64Str<MegaClient::CHATHANDLE>(schedId) << "]";
            }

            // if we couldn't update scheduled meeting, but we have deleted it's children, we also need to notify apps
            handle chatid = chat->getChatId();
            if (statecurrent)
            {
                // generate deleted scheduled meetings user alerts for each member in cmd (child meetings deleted) array
                for_each(begin(childMeetingsDeleted), end(childMeetingsDeleted),
                         [this, ou, chatid](const handle& sm) { createDeletedSMAlert(ou, chatid, sm); });

                if (res)
                {
                    if (isNewSchedMeeting) createNewSMAlert(ou, chat->getChatId(), schedId, parentSchedId, overrides);
                    else createUpdatedSMAlert(ou, chat->getChatId(), schedId, parentSchedId, overrides, std::move(cs));
                }
            }
        }

        clearSchedOccurrences(*chat);
        chat->setTag(0);    // external change
        notifychat(chat);
    }
}

void MegaClient::createNewSMAlert(const handle& ou, handle chatid, handle sm, handle parentSchedId, m_time_t startDateTime)
{
    if (ou == me)
    {
        LOG_verbose << "ScheduledMeetings: Avoiding New SM alert generated by myself"
                    << " in a different session";
        return;
    }
    useralerts.add(new UserAlert::NewScheduledMeeting(ou, m_time(), useralerts.nextId(), chatid, sm, parentSchedId, startDateTime));
}

void MegaClient::createDeletedSMAlert(const handle& ou, handle chatid, handle sm)
{
    if (ou == me)
    {
        LOG_verbose << "ScheduledMeetings: Avoiding Deleted SM alert generated by myself"
                    << " in a different session";
        return;
    }
    useralerts.add(new UserAlert::DeletedScheduledMeeting(ou, m_time(), useralerts.nextId(), chatid, sm));
}

void MegaClient::createUpdatedSMAlert(const handle& ou, handle chatid, handle sm, handle parentSchedId,
                                      m_time_t startDateTime, UserAlert::UpdatedScheduledMeeting::Changeset&& cs)
{
    if (ou == me)
    {
        LOG_verbose << "ScheduledMeetings: Avoiding Updated SM alert generated by myself"
                    << " in a differet session";
        return;
    }
    useralerts.add(new UserAlert::UpdatedScheduledMeeting(ou, m_time(), useralerts.nextId(), chatid, sm, parentSchedId, startDateTime, std::move(cs)));
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
                ephemeralSession = false;
                ephemeralSessionPlusPlus = false;
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

void MegaClient::sc_uec()
{
    handle u = UNDEF;
    string email;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case 'm':
                jsonsc.storeobject(&email);
                break;

            case 'u':
                u = jsonsc.gethandle(USERHANDLE);
                break;

            case EOO:
                if (email.empty())
                {
                    LOG_warn << "Missing email address in `uec` action packet";
                }
                if (u == UNDEF)
                {
                    LOG_warn << "Missing user handle in `uec` action packet";
                }
                if (u == me && email.size()) setEmail(ownuser(), email);
                app->account_updated();
                app->notify_confirm_user_email(u, email.c_str());
                ephemeralSession = false;
                ephemeralSessionPlusPlus = false;
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    LOG_warn << "Failed to parse `uec` action packet";
                    return;
                }
        }
    }
}

void MegaClient::sc_sqac()
{
    m_off_t gb = -1;
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
            case MAKENAMEID2('g','b'):
                gb = jsonsc.getint(); // should there be a notification about this?
                break;

            case EOO:
                if (gb == -1)
                {
                    LOG_warn << "Missing GB allowance in `sqac` action packet";
                }

                // Invalidate cached storage info.
                mLastKnownCapacity = -1;

                getuserdata(0);
                return;

            default:
                if (!jsonsc.storeobject())
                {
                    LOG_warn << "Failed to parse `sqac` action packet";
                    return;
                }
        }
    }
}

void MegaClient::sc_pk()
{
    if (!mKeyManager.generation())
    {
        LOG_debug << "Account not upgraded yet";
        return;
    }

    if (!statecurrent)
    {
        LOG_debug << "Skip fetching pending keys triggered by action packet during new session";
        return;
    }

    reqs.add(new CommandPendingKeys(this,
    [this] (Error e, std::string lastcompleted, std::shared_ptr<std::map<handle, std::map<handle, std::string>>> keys)
    {
        if (e)
        {
            LOG_debug << "No share keys: " << e;

            if (mKeyManager.promotePendingShares())
            {
                LOG_warn << "Promoting pending shares without new keys (received before contact keys?)";
                mKeyManager.commit([this]()
                {
                    // Changes to apply in the commit
                    mKeyManager.promotePendingShares();
                }); // No completion callback in this case
            }
            return;
        }

        mKeyManager.commit(
        [this, keys]()
        {
            // Changes to apply in the commit
            LOG_debug << "Processing pending keys";
            for (const auto& kv : *keys.get())
            {
                for (const auto& kv2 : kv.second)
                {
                    handle userHandle = kv.first;
                    handle shareHandle = kv2.first;
                    std::string key = kv2.second;

                    mKeyManager.addPendingInShare(toNodeHandle(shareHandle), userHandle, key);
                }
            }

            mKeyManager.promotePendingShares();
        },
        [this, lastcompleted](error e)
        {
            if (e == API_OK)
            {
                LOG_debug << "All pending keys were processed";
                reqs.add(new CommandPendingKeys(this, lastcompleted, [] (Error e)
                {
                    if (e)
                    {
                        LOG_err << "Error deleting pending keys";
                        return;
                    }
                    LOG_debug << "Pending keys deleted";
                }));
            }
        });
    }));
}

void MegaClient::sc_cce()
{
    LOG_debug << "Processing Credit Card Expiry";
    app->notify_creditCardExpiry();
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

void MegaClient::setBusinessStatus(BizStatus newBizStatus)
{
    BizStatus prevBizStatus = mBizStatus;

    if (newBizStatus != mBizStatus) //has changed
    {
        mBizStatus = newBizStatus;
        mCachedStatus.addOrUpdate(CacheableStatus::STATUS_BUSINESS, newBizStatus);

#ifdef ENABLE_SYNC
        if (mBizStatus == BIZ_STATUS_EXPIRED) //transitioning to expired
        {
            syncs.disableSyncs(ACCOUNT_EXPIRED, false, true);
        }
#endif
    }

    if (prevBizStatus != BIZ_STATUS_UNKNOWN && prevBizStatus != mBizStatus) //has changed
    {
        app->notify_business_status(mBizStatus);
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

                mBizMode = mode;

                setBusinessStatus(status);
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

                if (prevBizStatus == BIZ_STATUS_INACTIVE)
                {
                    app->account_updated();
                    getuserdata(reqtag);  // update account flags
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
// - set export enable/disable
// purge removed nodes after notification
void MegaClient::notifypurge(void)
{
    if (!mNodeManager.ready())
    {
        // the filesystem is being initialized
        return;
    }

    int i, t;

    handle tscsn = cachedscsn;

    if (scsn.ready()) tscsn = scsn.getHandle();

    if (mNodeManager.nodeNotifySize() || usernotify.size() || pcrnotify.size()
            || setnotify.size() || setelementnotify.size()
            || !useralerts.useralertnotify.empty()
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
    }

    // purge of notifications related to nodes have been moved to NodeManager since NodesOnDemand
    mNodeManager.notifyPurge();

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
                    std::shared_ptr<Node> n = nodebyhandle(*it);
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

    useralerts.purgescalerts();

    if (!setelementnotify.empty())
    {
        notifypurgesetelements();
    }

    if (!setnotify.empty())
    {
        notifypurgesets();
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
            chat->changed = {};
            chat->clearSchedMeetingsChanged();
            chat->clearUpdatedSchedMeetingOccurrences();
        }

        chatnotify.clear();
    }
#endif

    totalNodes.store(mNodeManager.getNodeCount());
}

void MegaClient::persistAlert(UserAlert::Base* a)
{
    if (!sctable) return;

    // Alerts are not critical. There is no need to break execution if db ops failed for some (rare) reason
    if (a->removed())
    {
        if (a->dbid)
        {
            if (sctable->del(a->dbid))
            {
                LOG_verbose << "UserAlert of type " << a->type << " removed from db.";
            }
            else
            {
                LOG_err << "Failed to remove UserAlert of type " << a->type << " from db.";
            }
        }
    }
    else // insert or replace
    {
        if (sctable->put(CACHEDALERT, a, &key))
        {
            LOG_verbose << "UserAlert of type " << a->type << " inserted or replaced in db.";
        }
        else
        {
            LOG_err << "Failed to insert or update UserAlert of type " << a->type << " in db.";
        }
    }
}

// return node pointer derived from node handle
shared_ptr<Node> MegaClient::nodebyhandle(handle h)
{
    return nodeByHandle(NodeHandle().set6byte(h));
}

shared_ptr<Node> MegaClient::nodeByHandle(NodeHandle h)
{
    if (h.isUndef()) return nullptr;

    return mNodeManager.getNodeByHandle(h);
}

shared_ptr<Node> MegaClient::nodeByPath(const char* path, std::shared_ptr<Node> node, nodetype_t type)
{
    if (!path) return NULL;

    std::shared_ptr<Node> cwd = node;
    vector<string> c;
    string s;
    int l = 0;
    const char* bptr = path;
    int remote = 0;
    std::shared_ptr<Node> n;

    // split path by / or :
    do {
        if (!l)
        {
            if (*(const signed char*)path >= 0)
            {
                if (*path == '\\')
                {
                    if (path > bptr)
                    {
                        s.append(bptr, path - bptr);
                    }

                    bptr = ++path;

                    if (*bptr == 0)
                    {
                        c.push_back(s);
                        break;
                    }

                    path++;
                    continue;
                }

                if (*path == '/' || *path == ':' || !*path)
                {
                    if (*path == ':')
                    {
                        if (c.size())
                        {
                            return NULL;
                        }
                        remote = 1;
                    }

                    if (path > bptr)
                    {
                        s.append(bptr, path - bptr);
                    }

                    bptr = path + 1;

                    c.push_back(s);

                    s.erase();
                }
            }
            else if ((*path & 0xf0) == 0xe0)
            {
                l = 1;
            }
            else if ((*path & 0xf8) == 0xf0)
            {
                l = 2;
            }
            else if ((*path & 0xfc) == 0xf8)
            {
                l = 3;
            }
            else if ((*path & 0xfe) == 0xfc)
            {
                l = 4;
            }
        }
        else
        {
            l--;
        }
    } while (*path++);

    if (l)
    {
        return NULL;
    }

    if (remote)
    {
        // target: user inbox - it's not a node - return NULL
        if (c.size() == 2 && !c[1].size())
        {
            return NULL;
        }

        User* u;

        if ((u = finduser(c[0].c_str())))
        {
            // locate matching share from this user
            handle_set::iterator sit;
            string name;
            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
            {
                if ((n = nodebyhandle(*sit)))
                {
                    if(!name.size())
                    {
                        name =  c[1];
                        LocalPath::utf8_normalize(&name);
                    }

                    if (!strcmp(name.c_str(), n->displayname()))
                    {
                        l = 2;
                        break;
                    }
                }
            }
        }

        if (!l)
        {
            return NULL;
        }
    }
    else
    {
        // path starting with /
        if (c.size() > 1 && !c[0].size())
        {
            // path starting with //
            if (c.size() > 2 && !c[1].size())
            {
                if (c[2] == "in")
                {
                    n = nodeByHandle(mNodeManager.getRootNodeVault());
                    assert(!n || n->type == VAULTNODE);
                }
                else if (c[2] == "bin")
                {
                    n = nodeByHandle(mNodeManager.getRootNodeRubbish());
                    assert(!n || n->type == RUBBISHNODE);
                }
                else
                {
                    return NULL;
                }

                l = 3;
            }
            else
            {
                n = nodeByHandle(mNodeManager.getRootNodeFiles());
                assert(!n || loggedIntoFolder() || n->type == ROOTNODE); //folder links root node type is not ROOTNODE
                l = 1;
            }
        }
        else
        {
            n = cwd;
        }
    }

    // parse relative path
    while (n && l < (int)c.size())
    {
        if (c[l] != ".")
        {
            if (c[l] == "..")
            {
                if (n->parent)
                {
                    n = n->parent;
                }
            }
            else
            {
                // locate child node (explicit ambiguity resolution: not implemented)
                if (c[l].size())
                {
                    std::shared_ptr<Node> nn;

                    switch (type)
                    {
                    case FILENODE:
                    case FOLDERNODE:
                        nn = childnodebynametype(n.get(), c[l].c_str(),
                            l + 1 < int(c.size()) ? FOLDERNODE : type); // only the last leaf could be a file
                        break;
                    case TYPE_UNKNOWN:
                    default:
                        nn = childnodebyname(n.get(), c[l].c_str());
                        break;
                    }

                    if (!nn)
                    {
                        return NULL;
                    }

                    n = nn;
                }
            }
        }

        l++;
    }

    return (type == TYPE_UNKNOWN || (n && type == n->type)) ? n : nullptr;
}

// server-client deletion
std::shared_ptr<Node> MegaClient::sc_deltree()
{
    std::shared_ptr<Node> n;
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
                    td.setOriginatingUser(originatingUser);
                    proctree(n, &td);
                    reqtag = creqtag;

                    useralerts.stashDeletedNotedSharedNodes(originatingUser);
#ifdef ENABLE_SYNC
                    if (n->parent)
                    {
                        syncs.triggerSync(n->parent->nodeHandle());
                    }
#endif
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
void MegaClient::makeattr(SymmCipher* key, string* attrstring, const char* json, int l)
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

    if (!key->cbc_encrypt(buf, ll))
    {
        LOG_err << "Failed to CBC encrypt attribute";  // Is refactoring needed to add return value for current function?
        assert(false);
    }

    attrstring->assign((char*)buf, ll);

    delete[] buf;
}

void MegaClient::makeattr(SymmCipher* key, const std::unique_ptr<string>& attrstring, const char* json, int l)
{
    makeattr(key, attrstring.get(), json, l);
}

error MegaClient::addTagToNode(std::shared_ptr<Node> node,
                               const string& tag,
                               CommandSetAttr::Completion&& c)
{
    nameid tagNameid = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_TAGS);
    std::string tags = node->attrs.map[tagNameid];
    std::set<std::string> tokens = splitString(tags, TAG_DELIMITER);

    if (tokens.size() == MAX_NUMBER_TAGS)
    {
        return API_ETOOMANY;
    }

    if (getTagPosition(tokens, tag) != tokens.end())
    {
        return API_EEXIST;
    }

    if (tags.size()) // first tag, delimeter isnt' required
    {
        tags.push_back(TAG_DELIMITER);
    }

    tags.append(tag);

    if (tags.size() > MAX_TAGS_SIZE)
    {
        return API_EARGS;
    }

    AttrMap map;
    map.map[tagNameid] = std::move(tags);
    setattr(node, std::move(map.map), std::move(c), false);

    return API_OK;
}

error MegaClient::removeTagFromNode(std::shared_ptr<Node> node,
                                    const string& tag,
                                    CommandSetAttr::Completion&& c)
{
    nameid tagNameid = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_TAGS);
    std::string tags = node->attrs.map[tagNameid];
    std::set<std::string> tokens = splitString(tags, TAG_DELIMITER);

    auto tagPosition = getTagPosition(tokens, tag);
    if (tagPosition == tokens.end())
    {
        return API_ENOENT;
    }

    tokens.erase(tagPosition);
    std::string str = joinStrings(tokens.begin(), tokens.end(), std::string{TAG_DELIMITER});

    AttrMap map;
    map.map[tagNameid] = std::move(str);
    setattr(node, std::move(map.map), std::move(c), false);

    return API_OK;
}

error MegaClient::updateTagNode(std::shared_ptr<Node> node,
                                const string& newTag,
                                const string& oldTag,
                                CommandSetAttr::Completion&& c)
{
    nameid tagNameid = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_TAGS);
    std::string tags = node->attrs.map[tagNameid];
    std::set<std::string> tokens = splitString(tags, TAG_DELIMITER);

    auto tagPosition = getTagPosition(tokens, oldTag);
    if (tagPosition == tokens.end())
    {
        return API_ENOENT;
    }

    tokens.erase(tagPosition);

    tagPosition = getTagPosition(tokens, newTag);
    if (tagPosition != tokens.end())
    {
        return API_EEXIST;
    }

    tokens.insert(newTag);

    std::string str = joinStrings(tokens.begin(), tokens.end(), std::string{TAG_DELIMITER});

    if (str.size() > MAX_TAGS_SIZE)
    {
        return API_EARGS;
    }

    AttrMap map;
    map.map[tagNameid] = std::move(str);
    setattr(node, std::move(map.map), std::move(c), false);

    return API_OK;
}

// update node attributes
error MegaClient::setattr(std::shared_ptr<Node> n, attr_map&& updates, CommandSetAttr::Completion&& c, bool canChangeVault)
{
    if (ststatus == STORAGE_PAYWALL)
    {
        return API_EPAYWALL;
    }

    if (!checkaccess(n.get(), FULL))
    {
        return API_EACCESS;
    }

    if (SymmCipher* cipher = n->nodecipher())
    {
        std::string at;
        AttrMap attributeMap;
        attributeMap.map = updates;
        attributeMap.getjson(&at);
        makeattr(cipher, &at, at.c_str(), int(at.size()));
        if (at.size() > MAX_NODE_ATTRIBUTE_SIZE)
        {
            sendevent(99484, "Node attribute exceed maximun size");
            LOG_err << "Node attribute exceed maximun size";
            return API_EARGS;
        }
    }

    // Check and delete invalid fav attributes
    if (n->firstancestor()->getShareType() == ShareType_t::IN_SHARES) // Avoid an inshare to be tagged as favourite by the sharee
    {
        std::vector<nameid> nameIds = { AttrMap::string2nameid("fav"), AttrMap::string2nameid("lbl") };
        for (nameid& nameId : nameIds)
        {
            updates.erase(nameId);
        }
        if (updates.empty())
        {
            return API_EACCESS;
        }
    }

    // we only update the values stored in the node once the command completes successfully
    reqs.add(new CommandSetAttr(this, n, move(updates), move(c), canChangeVault));

    return API_OK;
}

error MegaClient::putnodes_prepareOneFile(NewNode* newnode, Node* parentNode, const char *utf8Name, const UploadToken& binaryUploadToken,
                                          const byte *theFileKey, const char *megafingerprint, const char *fingerprintOriginal,
                                          std::function<error(AttrMap&)> addNodeAttrsFunc, std::function<error(std::string *)> addFileAttrsFunc)
{
    error e = API_OK;

    // set up new node as file node
    newnode->source = NEW_UPLOAD;
    newnode->type = FILENODE;
    newnode->uploadtoken = binaryUploadToken;
    newnode->parenthandle = UNDEF;
    newnode->uploadhandle = mUploadHandle.next();
    newnode->attrstring.reset(new string);
    newnode->fileattributes.reset(new string);

    // add custom file attributes
    if (addFileAttrsFunc)
    {
        e = addFileAttrsFunc(newnode->fileattributes.get());
        if (e != API_OK)
        {
            return e;
        }
    }

    // fill node attributes (honoring those in previous version)
    AttrMap attrs;
    shared_ptr<Node> previousNode = childnodebyname(parentNode, utf8Name, true);
    honorPreviousVersionAttrs(previousNode.get(), attrs);
    attrs.map['n'] = utf8Name;
    attrs.map['c'] = megafingerprint;
    if (fingerprintOriginal)
    {
        attrs.map[MAKENAMEID2('c', '0')] = fingerprintOriginal;
    }

    // add custom node attributes
    if (addNodeAttrsFunc)
    {
        e = addNodeAttrsFunc(attrs);
        if (e != API_OK)
        {
            return e;
        }
    }

    // JSON-encode object and encrypt attribute string and node key itself too
    string tattrstring;
    attrs.getjson(&tattrstring);
    SymmCipher cipher;
    cipher.setkey(theFileKey);
    makeattr(&cipher, newnode->attrstring, tattrstring.c_str());
    newnode->nodekey.assign((char*)theFileKey, FILENODEKEYLENGTH);
    SymmCipher::xorblock((const byte*)newnode->nodekey.data() + SymmCipher::KEYLENGTH, (byte*)newnode->nodekey.data());

    // adjust previous version node
    string name(utf8Name);
    if (std::shared_ptr<Node> ovn = getovnode(parentNode, &name))
    {
        newnode->ovhandle = ovn->nodeHandle();
    }

    return e;
}

void MegaClient::putnodes_prepareOneFolder(NewNode* newnode, std::string foldername, bool canChangeVault, std::function<void(AttrMap&)> addAttrs)
{
    MegaClient::putnodes_prepareOneFolder(newnode, foldername, rng, tmpnodecipher, canChangeVault, addAttrs);
}

void MegaClient::putnodes_prepareOneFolder(NewNode* newnode, std::string foldername, PrnGen& rng, SymmCipher& tmpnodecipher, bool canChangeVault, std::function<void(AttrMap&)> addAttrs)
{
    string attrstring;
    byte buf[FOLDERNODEKEYLENGTH];

    // set up new node as folder node
    newnode->source = NEW_NODE;
    newnode->type = FOLDERNODE;
    newnode->nodehandle = 0;
    newnode->parenthandle = UNDEF;
    newnode->canChangeVault = canChangeVault;

    // generate fresh random key for this folder node
    rng.genblock(buf, FOLDERNODEKEYLENGTH);
    newnode->nodekey.assign((char*)buf, FOLDERNODEKEYLENGTH);
    tmpnodecipher.setkey(buf);

    // generate fresh attribute object with the folder name
    AttrMap attrs;

    LocalPath::utf8_normalize(&foldername);
    attrs.map['n'] = foldername;

    // add custom attributes
    if (addAttrs)  addAttrs(attrs);

    // JSON-encode object and encrypt attribute string
    attrs.getjson(&attrstring);
    newnode->attrstring.reset(new string);
    makeattr(&tmpnodecipher, newnode->attrstring, attrstring.c_str());
}

// send new nodes to API for processing
void MegaClient::putnodes(NodeHandle h, VersioningOption vo, vector<NewNode>&& newnodes, const char *cauth, int tag, bool canChangeVault, CommandPutNodes::Completion&& resultFunction)
{
    reqs.add(new CommandPutNodes(this, h, NULL, vo, std::move(newnodes), tag, PUTNODES_APP, cauth, std::move(resultFunction), canChangeVault));
}

// drop nodes into a user's inbox (must have RSA keypair) - obsolete feature, kept for sending logs to helpdesk
void MegaClient::putnodes(const char* user, vector<NewNode>&& newnodes, int tag, CommandPutNodes::Completion&& completion)
{
    if (!finduser(user, 0) && !user)
    {
        if (completion) completion(API_EARGS, USER_HANDLE, newnodes, false, tag);
        else app->putnodes_result(API_EARGS, USER_HANDLE, newnodes, false, tag);
        return;
    }

    queuepubkeyreq(user, std::make_unique<PubKeyActionPutNodes>(std::move(newnodes), tag, std::move(completion)));
}

void MegaClient::putFileAttributes(handle h, fatype t, const string& encryptedAttributes, int tag)
{
    std::shared_ptr<Node> node = mNodeManager.getNodeByHandle(NodeHandle().set6byte(h));
    if (node && node->fileattrstring.size() + encryptedAttributes.size() >= MAX_FILE_ATTRIBUTE_SIZE)
    {
        sendevent(99485, "Exceeded size for file attribute");
        LOG_err << "Exceede size for file attribute: " << t;
        restag = tag;
        app->putfa_result(h, t, API_EARGS);
        return;
    }

    reqs.add(new CommandAttachFA(this, h, t, encryptedAttributes, tag));
}

// returns 1 if node has accesslevel a or better, 0 otherwise
int MegaClient::checkaccess(Node* n, accesslevel_t a)
{
    // writable folder link access is supposed to be full
    if (loggedIntoWritableFolder())
    {
        return a <= FULL;
    }

    // folder link access is always read-only - ignore login status during
    // initial tree fetch
    if (a < OWNERPRELOGIN && !loggedin())
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

        n = n->parent.get();
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
    if (!fn->parent.get() || !checkaccess(fn->parent.get(), FULL))
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

        tn = tn->parent.get();
    }

    // condition #6: fn and tn must be in the same tree (same ultimate parent
    // node or shared by the same user)
    for (;;)
    {
        if (fn->inshare || !fn->parent)
        {
            break;
        }

        fn = fn->parent.get();
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
error MegaClient::rename(std::shared_ptr<Node> n, std::shared_ptr<Node> p, syncdel_t syncdel, NodeHandle prevparenthandle, const char *newName, bool canChangeVault, CommandMoveNode::Completion&& c)
{
    if (mBizStatus == BIZ_STATUS_EXPIRED)
    {
        return API_EBUSINESSPASTDUE;
    }

    error e;

    if ((e = checkmove(n.get(), p.get())))
    {
        return e;
    }

    if (p->firstancestor()->type == RUBBISHNODE)
    {
        // similar to the webclient, send `s2` along with `m` if the node is moving to the rubbish
        removeOutSharesFromSubtree(n, 0);
    }

    std::shared_ptr<Node> prevParent = NULL;
    if (!prevparenthandle.isUndef())
    {
        prevParent = nodeByHandle(prevparenthandle);
    }
    else
    {
        prevParent = n->parent;
    }

    attr_map attrUpdates;

    if (prevParent)
    {
        std::shared_ptr<Node> prevRoot = getrootnode(prevParent);
        std::shared_ptr<Node> newRoot = getrootnode(p);
        handle rubbishHandle = mNodeManager.getRootNodeRubbish().as8byte();
        nameid rrname = AttrMap::string2nameid("rr");

        if (prevRoot->nodehandle != rubbishHandle &&
            newRoot->nodehandle == rubbishHandle)
        {
            // deleted node
            char base64Handle[12];
            Base64::btoa((byte*)&prevParent->nodehandle, MegaClient::NODEHANDLE, base64Handle);
            if (strcmp(base64Handle, n->attrs.map[rrname].c_str()))
            {
                LOG_debug << "Adding rr attribute";
                attrUpdates[rrname] = base64Handle;
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
                attrUpdates[rrname] = "";
            }
        }
    }

    if (newName)
    {
        attrUpdates['n'] = newName;
    }

    if (!attrUpdates.empty())
    {
        // send attribute changes first so that any rename is already applied when the move node completes
        setattr(n, std::move(attrUpdates), nullptr, canChangeVault);  // no completion function, result is after the move completes
    }

    // rewrite keys of foreign nodes that are moved out of an outbound share
    rewriteforeignkeys(n);

    reqs.add(new CommandMoveNode(this, n, p, syncdel, prevparenthandle, move(c), canChangeVault));

    return API_OK;
}

error MegaClient::createFolder(std::shared_ptr<Node> parent, const char* name, int rTag)
{
    assert(parent);
    assert(name && *name);

    std::vector<NewNode> nn(1);
    bool canChangeVault = parent->isPasswordNodeFolder();
    NewNode& newPasswordNode = nn.front();
    putnodes_prepareOneFolder(&newPasswordNode, name, canChangeVault);
    const char* cauth = nullptr;

    // newNode.nodekey will be encrypted with user's MK in Command construction
    // using existing logic with default client->app->putnodes_result as callback for completion
    putnodes(parent->nodeHandle(), VersioningOption::NoVersioning, std::move(nn), cauth, rTag, canChangeVault);

    return API_OK;
}

std::pair<bool, error> MegaClient::checkRenameNodePrecons(std::shared_ptr<Node> n)
{
    if (ststatus == STORAGE_PAYWALL) return std::make_pair(false, API_EPAYWALL);

    if (!n) return std::make_pair(false, API_EARGS);

    if (!checkaccess(n.get(), FULL)) return std::make_pair(false, API_EACCESS);

    return std::make_pair(true, API_OK);
}

error MegaClient::renameNode(NodeHandle nh, const char* newName, CommandSetAttr::Completion&& cbRequest)
{
    auto node = nodeByHandle(nh);
    const auto aux = checkRenameNodePrecons(node);
    const bool& preconsOK = aux.first;
    const error& code = aux.second;
    if (!preconsOK) return code;

    if (!(newName && *newName)) return API_EARGS;

    const bool canChangeVault = node->isPasswordNodeFolder() || node->isPasswordNode();
    string sname = newName;
    LocalPath::utf8_normalize(&sname);
    return setattr(node, attr_map('n', sname), std::move(cbRequest), canChangeVault);
}

error MegaClient::removeNode(NodeHandle nh, bool keepVersions, int rTag)
{
    std::shared_ptr<Node> node = nodeByHandle(nh);
    if (!node) return API_ENOENT;

    if (keepVersions && node->type != FILENODE) return API_EARGS;

    bool canChangeVault = false;
    const bool isPNFolder = node->isPasswordNodeFolder();
    if (isPNFolder || node->isPasswordNode())
    {
        if (isPNFolder && node->nodeHandle() == getPasswordManagerBase())
        {
            LOG_err << "Password Manager: Password Manager Base cannot be deleted";
            return API_EARGS;
        }
        keepVersions = false;
        canChangeVault = true;
    }
    else if (node->type == ROOTNODE || node->type == VAULTNODE || node->type == RUBBISHNODE)  // rootnodes cannot be deleted
    {
        return API_EACCESS;
    }

    // use default callback function app->unlink_result
    return unlink(node.get(), keepVersions, rTag, canChangeVault);
}

void MegaClient::removeOutSharesFromSubtree(std::shared_ptr<Node> n, int tag)
{
    if (n->pendingshares)
    {
        for (auto& it : *n->pendingshares)
        {
            if (it.second->pcr)
            {
                setshare(n, it.second->pcr->targetemail.c_str(), ACCESS_UNKNOWN, false, nullptr, tag, [](Error, bool){});
            }
        }
    }

    if (n->outshares)
    {
        for (auto& it : *n->outshares)
        {
            if (it.second->user)
            {
                setshare(n, it.second->user->email.c_str(), ACCESS_UNKNOWN, false, nullptr, tag, [](Error, bool){});
            }
            else // folder links are a shared folder without user
            {
                setshare(n, nullptr, ACCESS_UNKNOWN, false, nullptr, tag, [](Error, bool) {});
            }
        }
    }

    for (auto& c : getChildren(n.get()))
    {
        removeOutSharesFromSubtree(c, tag);
    }
}

void MegaClient::unlinkOrMoveBackupNodes(NodeHandle backupRootNode, NodeHandle destination, std::function<void(Error)> completion)
{
    std::shared_ptr<Node> n = nodeByHandle(backupRootNode);
    if (!n)
    {
        if (destination.isUndef())
        {
            // we were going to delete these anyway, so no problem
            completion(API_OK);
        }
        else
        {
            // we can't move them if they don't exist
            completion(API_EARGS);
        }
        return;
    }

    if (n->firstancestor()->nodeHandle() != mNodeManager.getRootNodeVault())
    {
        // backup nodes are supposed to be in the vault - if not, something is wrong
        completion(API_EARGS);
        return;
    }

    if (destination.isUndef())
    {
        error e = unlink(n.get(), false, 0, true, [completion](NodeHandle, Error e){ completion(e); });
        if (e)
        {
            // error before we sent a request so call completion directly
            completion(e);
        }
    }
    else
    {
        // moving to target node

        std::shared_ptr<Node> p = nodeByHandle(destination);
        if (!p || p->firstancestor()->nodeHandle() != mNodeManager.getRootNodeFiles())
        {
            completion(API_EARGS);
            return;
        }

        error e = rename(n, p, SYNCDEL_NONE, NodeHandle(), nullptr, true, [completion](NodeHandle, Error e){ completion(e); });
        if (e)
        {
            // error before we sent a request so call completion directly
            completion(e);
        }
    }
}

// delete node tree
error MegaClient::unlink(Node* n, bool keepversions, int tag, bool canChangeVault, std::function<void(NodeHandle, Error)>&& resultFunction)
{
    if (mBizStatus == BIZ_STATUS_EXPIRED)
    {
        return API_EBUSINESSPASTDUE;
    }

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
    reqs.add(new CommandDelNode(this, n->nodeHandle(), kv, tag, move(resultFunction), canChangeVault));

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
        char c = static_cast<char>(str[i++] & 0xff);

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

SymmCipher *MegaClient::getRecycledTemporaryTransferCipher(const byte *key, int type)
{
    tmptransfercipher.setkey(key, type);
    return &tmptransfercipher;
}

SymmCipher *MegaClient::getRecycledTemporaryNodeCipher(const string *key)
{
    return tmpnodecipher.setkey(key) ? &tmpnodecipher : nullptr;
}

SymmCipher *MegaClient::getRecycledTemporaryNodeCipher(const byte *key)
{
    tmpnodecipher.setkey(key);
    return &tmpnodecipher;
}

// compute generic string hash
void MegaClient::stringhash(const char* s, byte* hash, SymmCipher* cipher)
{
    int t;

    t = static_cast<int>(strlen(s) & - SymmCipher::BLOCKSIZE);

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
    byte hash[SymmCipher::KEYLENGTH+1];

    tolower_string(*s);
    stringhash(s->c_str(), hash, c);

    return MemAccess::get<uint64_t>((const char*)hash);
}

// read and add/verify node array g
int MegaClient::readnodes(JSON* j, int notify, putsource_t source, vector<NewNode>* nn, bool modifiedByThisClient, bool applykeys, Node* priorActionpacketDeletedNode, bool* firstHandleMatchesDelete)
{
    if (!j->enterarray())
    {
        return 0;
    }

    handle previousHandleForAlert = UNDEF;

#ifdef ENABLE_SYNC
    set<NodeHandle> allParents;
#endif

    NodeManager::MissingParentNodes missingParentNodes;
    while (int e = readnode(j, notify, source, nn, modifiedByThisClient, applykeys, missingParentNodes, previousHandleForAlert,
#ifdef ENABLE_SYNC
                            &allParents,
#else
                            nullptr,
#endif
                            priorActionpacketDeletedNode, firstHandleMatchesDelete))
    {
        if (e != 1)
        {
            LOG_err << "Parsing error in readnodes: " << e;
            return 0;
        }
    }  

    mergenewshares(notify);
    mNodeManager.checkOrphanNodes(missingParentNodes);

#ifdef ENABLE_SYNC
    for (NodeHandle p : allParents)
    {
        syncs.triggerSync(p);
    }
#endif

    return j->leavearray();
}


int MegaClient::readnode(JSON* j, int notify, putsource_t source, vector<NewNode>* nn, bool modifiedByThisClient, bool applykeys,
                         mega::NodeManager::MissingParentNodes &missingParentNodes, handle &previousHandleForAlert, set<NodeHandle> *allParents,
                         Node* priorActionpacketDeletedNode, bool* firstHandleMatchesDelete)
{
    std::shared_ptr<Node> n;

    if (j->enterobject())
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
                    if (priorActionpacketDeletedNode && firstHandleMatchesDelete)
                    {
                        *firstHandleMatchesDelete = h == priorActionpacketDeletedNode->nodehandle;
                        priorActionpacketDeletedNode = nullptr;
                    }
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
                        return 2;
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

        if (ph != UNDEF && allParents)
        {
            allParents->insert(NodeHandle().set6byte(ph));
        }

        if (!warnlevel())
        {
            // 'notify' is false only while processing fetchnodes command
            // In that case, we can skip the lookup, since nodes are all new ones,
            // (they will not be found in DB)
            if (notify && (n = nodebyhandle(h)))
            {
                std::shared_ptr<Node> p;
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

                if (!ISUNDEF(ph))
                {
                    if (p)
                    {
                        if (n->setparent(p))
                        {
                            n->changed.parent = true;
                        }
                    }
                    else
                    {
                        n->setparent(NULL);
                        n->parenthandle = ph;
                        missingParentNodes[n->parentHandle()].insert(n);
                    }
                }

                if (a && k && n->attrstring)
                {
                    LOG_warn << "Updating the key of a NO_KEY node";
                    JSON::copystring(n->attrstring.get(), a);
                    n->setkeyfromjson(k);
                }
            }
            else
            {
                vector<byte> buf(SymmCipher::KEYLENGTH);

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

                    if (warnlevel())
                    {
                        su = UNDEF;
                    }
                    else
                    {
                        if (!mKeyManager.generation())
                        {
                            if (sk)
                            {
                                decryptkey(sk, buf.data(), static_cast<int>(buf.size()), &key, 1, h);
                            }
                        }
                        else
                        {
                            sk = nullptr;
                        }
                    }
                }

                string fas;

                JSON::copystring(&fas, fa);

                // fallback timestamps
                if (!(ts + 1))
                {
                    ts = m_time();
                }

                if (!(sts + 1))
                {
                    sts = ts;
                }

                n = std::make_shared<Node>(*this, NodeHandle().set6byte(h), NodeHandle().set6byte(ph), t, s, u, fas.c_str(), ts);
                n->changed.newnode = true;
                n->changed.modifiedByThisClient = modifiedByThisClient;

                n->attrstring.reset(new string);
                JSON::copystring(n->attrstring.get(), a);
                n->setkeyfromjson(k);

                if (loggedIntoFolder())
                {
                    // folder link access: first returned record defines root node and identity
                    // (this code used to be in Node::Node but is not suitable for session resume)
                    if (mNodeManager.getRootNodeFiles().isUndef())
                    {
                        mNodeManager.setRootNodeFiles(NodeHandle().set6byte(h));

                        if (loggedIntoWritableFolder())
                        {
                            // If logged into writable folder, we need the sharekey set in the root node
                            // so as to include it in subsequent put nodes
                            n->sharekey.reset(new SymmCipher(key)); //we use the "master key", in this case the secret share key
                        }
                    }
                }

                // NodeManager takes n ownership
                mNodeManager.addNode(n, notify,  fetchingnodes, missingParentNodes);

                if (!ISUNDEF(su))   // node represents an incoming share
                {
                    newshares.push_back(new NewShare(h, 0, su, rl, sts, sk ? buf.data() : NULL));
                    if (sk) // only if the key is valid, add it to the repository
                    {
                        mNewKeyRepository[NodeHandle().set6byte(h)] = std::move(buf);
                    }
                }

                if (u != me && !ISUNDEF(u) && !fetchingnodes)
                {
                    useralerts.noteSharedNode(u, t, ts, n.get(), UserAlert::type_put);
                }

                if (nn && nni >= 0 && nni < int(nn->size()))
                {
                    auto& nn_nni = (*nn)[nni];
                    nn_nni.added = true;
                    nn_nni.mAddedHandle = h;
                }
            }

            if (applykeys)
            {
                n->applykey();
            }

            if (notify)
            {
                // node is save in DB at notifypurge
                mNodeManager.notifyNode(n);
            }
            else // Only need to save in DB if node is not notified
            {
                mNodeManager.saveNodeInDb(n.get());
            }

            n = nullptr;    // ownership is taken by NodeManager upon addNode()

            // update-alerts for shared-nodes management
            if (!ISUNDEF(ph))
            {
                if (useralerts.isHandleInAlertsAsRemoved(h) && ISUNDEF(previousHandleForAlert))
                {
                    useralerts.setNewNodeAlertToUpdateNodeAlert(nodebyhandle(ph).get());
                    useralerts.removeNodeAlerts(nodebyhandle(h).get());
                    previousHandleForAlert = h;
                }
                else if ((t == FILENODE) || (t == FOLDERNODE))
                {
                    if (previousHandleForAlert == ph)
                    {
                        useralerts.removeNodeAlerts(nodebyhandle(h).get());
                        previousHandleForAlert = h;
                    }
                    // otherwise, the added TYPE_NEWSHAREDNODE is kept
                }
            }
        }

        return 1;
    }

    return 0;
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
    }
}

// - h/ha/k (outbound sharekeys, always symmetric)
void MegaClient::readokelement(JSON* j)
{
    handle h = UNDEF;
    byte ha[SymmCipher::BLOCKSIZE];
    byte auth[SymmCipher::BLOCKSIZE];
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

                if (!mKeyManager.generation())   // client not migrated yet
                {
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

                    vector<byte> buf(SymmCipher::BLOCKSIZE);
                    if (decryptkey(k, buf.data(), static_cast<int>(buf.size()), &key, 1, h))
                    {
                        newshares.push_back(new NewShare(h, 1, UNDEF, ACCESS_UNKNOWN, 0, buf.data(), ha));
                        if (mNewKeyRepository.find(NodeHandle().set6byte(h)) == mNewKeyRepository.end())
                        {
                            handleauth(h, auth);
                            if (!memcmp(auth, ha, buf.size()))
                            {
                                mNewKeyRepository[NodeHandle().set6byte(h)] = std::move(buf);
                            }
                        }
                    }
                }
                else
                {
                    LOG_debug << "Ignoring outgoing share keys from `ok0` (secured client with ^!keys already)";
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
                            pcrindex[p].reset(new PendingContactRequest(p, m, NULL, ts, uts, msg, false));
                        }

                        break;
                    default:
                       if (!j->storeobject())
                       {
                            j->leavearray();
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
                            pcrindex[p].reset(new PendingContactRequest(p, e, m, ts, uts, msg, true));
                        }

                        break;
                    default:
                       if (!j->storeobject())
                       {
                            j->leavearray();
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
    bool journeyIdFound = false;
    while (1)
    {
        string fieldName = json->getnameWithoutAdvance();
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
        case MAKENAMEID5('a', 'p', 'l', 'v', 'p'):   // apple VOIP push enabled (only available when logged in)
            aplvp_enabled = bool(json->getint());
            break;
        case MAKENAMEID5('s', 'm', 's', 'v', 'e'):   // 2 = Opt-in and unblock SMS allowed 1 = Only unblock SMS allowed 0 = No SMS allowed
            mSmsVerificationState = static_cast<SmsVerificationState>(json->getint());
            break;
        case MAKENAMEID4('n', 'l', 'f', 'e'):   // new link format enabled
            mNewLinkFormat = static_cast<bool>(json->getint());
            break;
        case MAKENAMEID4('c', 's', 'p', 'e'):   // cookie banner enabled
            mCookieBannerEnabled = bool(json->getint());
            break;
//        case MAKENAMEID2('p', 'f'): // is this account able to subscribe a pro flexi plan?
//            json->getint();
//            break;
        case MAKENAMEID3('j', 'i', 'd'):   // JourneyID value (16-char hex value)
            {
                string jid;
                if (!json->storeobject(&jid))
                {
                    LOG_err << "Invalid JourneyID (jid)";
                    assert(false);
                }
                if (jid.size() != JourneyID::HEX_STRING_SIZE)
                {
                    if (!jid.empty()) // If empty, it will be equivalent to no journeyId found (journeyIdFound = false)
                    {
                        LOG_err << "Invalid JourneyID size (" << jid.size()  << ") expected: " << JourneyID::HEX_STRING_SIZE;
                        jid.clear();
                        assert(false);
                    }
                }
                else
                {
                    journeyIdFound = true;
                    if (!trackJourneyId()) // If there is already a value and tracking flag is true, do nothing
                    {
                        LOG_verbose << "[MegaClient::readmiscflags] set jid: '" << jid << "'";
                        mJourneyId.setValue(jid);
                    }
                }
            }
            break;
        case EOO:
            if (!journeyIdFound && trackJourneyId()) // If there is no value or tracking flag is false, do nothing
            {
                LOG_verbose << "[MegaClient::readmiscflags] No JourneyId found -> set tracking to false";
                mJourneyId.setValue("");
            }
            return API_OK;
        default:
            if (fieldName.rfind("ab_", 0) == 0) // Starting with "ab_"
            {
                string tag = fieldName.substr(3); // The string after "ab_" prefix
                int64_t value = json->getint();
                if (value >= 0)
                {
                    mABTestFlags[tag] = static_cast<uint32_t>(value);
                }
                else
                {
                    LOG_err << "[MegaClient::readmiscflags] Invalid value for A/B Test flag";
                    assert(value >= 0 && "A/B test value must be greater or equal to 0");
                }
            }
            else if (fieldName.rfind("ff_", 0) == 0) // Starting with "ff_"
            {
                string tag = fieldName.substr(3); // The string after "ff_" prefix
                int64_t value = json->getint();
                if (value >= 0)
                {
                    mFeatureFlags[tag] = static_cast<uint32_t>(value);
                }
                else
                {
                    LOG_err << "[MegaClient::readmiscflags] Invalid value for Feature flag";
                    assert(value >= 0 && "Feature flag value must be greater or equal to 0");
                }
            }
            else if (!json->storeobject())
            {
                return API_EINTERNAL;
            }
        }
    }
}

bool MegaClient::procph(JSON *j)
{
    if (!j->enterarray())
    {
        return false;
    }

    while (int e = procphelement(j))
    {
        if (e != 1)
        {
            LOG_err << "Parsing error in procph: " << e;
            j->leavearray();
            return false;
        }
    }

    return j->leavearray();
}

int MegaClient::procphelement(JSON *j)
{
        // fields: h, ph, ets
        if (j->enterobject())
        {
            handle h = UNDEF;
            handle ph = UNDEF;
            m_time_t ets = 0;
            m_time_t cts = 0;
            std::shared_ptr<Node> n;
            bool takendown = false;
            std::string authKey;

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
                    case 'w':
                        j->storeobject(&authKey);
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
                            n->setpubliclink(ph, cts, ets, takendown, authKey);
                            mNodeManager.updateNode(n.get());
                        }
                        else
                        {
                            LOG_warn << "node for public link not found";
                        }

                        break;
                    default:
                       if (!j->storeobject())
                       {
                            return 2;
                       }
                }
            }
            return 1;
        }
        return 0;
}

void MegaClient::applykeys()
{
    CodeCounter::ScopeTimer ccst(performanceStats.applyKeys);

    int noKeyExpected = (mNodeManager.getRootNodeFiles().isUndef() ? 0 : 1)
                      + (mNodeManager.getRootNodeVault().isUndef() ? 0 : 1)
                      + (mNodeManager.getRootNodeRubbish().isUndef() ? 0 : 1);

    mNodeManager.applyKeys(uint32_t(mAppliedKeyNodeCount + noKeyExpected));

    if (!nodekeyrewrite.empty())
    {
        LOG_err << "Skipped to send key rewrites (secured client)";
        assert(nodekeyrewrite.empty());
        nodekeyrewrite.clear();
    }
}

// user/contact list
bool MegaClient::readusers(JSON* j, bool actionpackets)
{
    if (!j->enterarray())
    {
        return false;
    }

    while (int e = readuser(j, actionpackets))
    {
        if (e != 1)
        {
            LOG_err << "Parsing error in readusers: " << e;
            j->leavearray();
            return false;
        }
    }

    return j->leavearray();
}

int MegaClient::readuser(JSON* j, bool actionpackets)
{
    if (j->enterobject())
    {
        handle uh = 0;
        visibility_t v = VISIBILITY_UNKNOWN;    // new share objects do not override existing visibility
        m_time_t ts = 0;
        const char* m = NULL;
        nameid name;
        BizMode bizMode = BIZ_MODE_UNKNOWN;
        string pubk, puEd255, puCu255, sigPubk, sigCu255;

        bool exit = false;
        while (!exit)
        {
            string fieldName = j->getnameWithoutAdvance();
            name = j->getnameid();
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
                                        return 2;
                                    break;
                            }
                        }

                        j->leaveobject();
                    }

                    break;
                }

                case MAKENAMEID4('p', 'u', 'b', 'k'):
                    j->storebinary(&pubk);
                    break;

                case MAKENAMEID8('+', 'p', 'u', 'E', 'd', '2', '5', '5'):
                    j->storebinary(&puEd255);
                    break;

                case MAKENAMEID8('+', 'p', 'u', 'C', 'u', '2', '5', '5'):
                    j->storebinary(&puCu255);
                    break;

                case MAKENAMEID8('+', 's', 'i', 'g', 'P', 'u', 'b', 'k'):
                    j->storebinary(&sigPubk);
                    break;

                case EOO:
                    exit = true;
                    break;

                default:
                    switch (User::string2attr(fieldName.c_str()))
                    {
                        case ATTR_SIG_CU255_PUBK:
                            j->storebinary(&sigCu255);
                            break;

                        default:
                            if (!j->storeobject())
                            {
                                return 3;
                            }
                            break;
                    }
                    break;
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
                JSON::copystring(&email, m);
                useralerts.add(new UserAlert::ContactChange(v, uh, email, ts, useralerts.nextId()));
            }
            User* u = finduser(uh, 0);
            bool notify = !u;
            if (u || (u = finduser(uh, 1)))
            {
                const string oldEmail = u->email;
                mapuser(uh, m);

                u->mBizMode = bizMode;

                // The attributes received during the "ug" also include the version.
                // Keep them instead of the ones with no version from the fetch nodes.
                if (!(uh == me && fetchingnodes))
                {
                    if (pubk.size())
                    {
                        u->pubk.setkey(AsymmCipher::PUBKEY, (const byte*)pubk.data(), (int)pubk.size());
                    }

                    if (puEd255.size())
                    {
                        u->setattr(ATTR_ED25519_PUBK, &puEd255, nullptr);
                    }

                    if (puCu255.size())
                    {
                        u->setattr(ATTR_CU25519_PUBK, &puCu255, nullptr);
                    }

                    if (sigPubk.size())
                    {
                        u->setattr(ATTR_SIG_RSA_PUBK, &sigPubk, nullptr);
                    }

                    if (sigCu255.size())
                    {
                        u->setattr(ATTR_SIG_CU255_PUBK, &sigCu255, nullptr);
                    }
                }

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
                                 && statecurrent)  // otherwise, fetched when statecurrent is set
                        {
                            // new user --> fetch contact keys if they are not yet available.
                            // If keys are available for the user, fetchContactKeys will call trackKey directly.
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

        return 1;
    }

    return 0;
}

// Supported formats:
//   - file links:      #!<ph>[!<key>]
//                      <ph>[!<key>]
//                      /file/<ph>[<params>][#<key>]
//
//   - folder links:    #F!<ph>[!<key>]
//                      /folder/<ph>[<params>][#<key>]
//   - set links:       /collection/<ph>[<params>][#<key>]
error MegaClient::parsepubliclink(const char* link, handle& ph, byte* key, TypeOfLink type)
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
    else if ((ptr = strstr(link, "collection/")))
    {
        ptr += 11; // std::strlen("collection/");
        isFolder = false;
    }
    else    // legacy file link format without '#'
    {
        ptr = link;
        isFolder = false;
    }

    if (isFolder != (type == TypeOfLink::FOLDER))
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
            static const map<TypeOfLink, int> nodetypeKeylength = {{TypeOfLink::FOLDER, FOLDERNODEKEYLENGTH}
                                                                  ,{TypeOfLink::FILE, FILENODEKEYLENGTH}
                                                                  ,{TypeOfLink::SET, SETNODEKEYLENGTH}
                                                                  };
            int keylen = nodetypeKeylength.at(type);
            if (Base64::atob(k, key, keylen) == keylen)
            {
                return API_OK;
            }
        }
    }

    return API_EARGS;
}

void MegaClient::openStatusTable(bool loadFromCache)
{
    if (statusTable)
    {
        statusTable.reset();
        mCachedStatus.clear();
    }
    doOpenStatusTable();
    if (loadFromCache && statusTable)
    {
        fetchStatusTable(statusTable.get());
    }
}

void MegaClient::checkForResumeableSCDatabase()
{
    // see if we can resume from an already cached set of nodes for this folder
    opensctable();
    string t;
    if (sctable && sctable->get(CACHEDSCSN, &t) && t.size() == sizeof cachedscsn)
    {
        cachedscsn = MemAccess::get<handle>(t.data());
    }
}

error MegaClient::folderaccess(const char *folderlink, const char * authKey)
{
    handle h = UNDEF;
    byte folderkey[FOLDERNODEKEYLENGTH];

    error e;
    if ((e = parsepubliclink(folderlink, h, folderkey, TypeOfLink::FOLDER)) == API_OK)
    {
        if (authKey)
        {
            auto ptr = authKey;
            while (*ptr)
            {
                if (!URLCodec::issafe(*ptr))
                {
                    LOG_warn << "Authkey is not valid";
                    return API_EACCESS;
                }
                ptr++;
            }
            mFolderLink.mWriteAuth = authKey;
        }
        mFolderLink.mPublicHandle = h;
        // mFolderLink.mAccountAuth remain unchanged, since it can be reused for multiple links
        key.setkey(folderkey);

        // upon loginToFolder, don't load the existing (if any) cache, since it's generated by
        // a previous "session" and it could be outdated. Better to create a fresh one
        openStatusTable(false);
    }

    return e;
}

void MegaClient::prelogin(const char *email, CommandPrelogin::Completion completion)
{
    // Convenience.
    using std::bind;
    using namespace std::placeholders;

    // Invoke app callback if no completion has been specified.
    if (!completion)
        completion = bind(&MegaApp::prelogin_result, app, _1, _2, _3, _4);

    reqs.add(new CommandPrelogin(this, std::move(completion), email));
}

// create new session
void MegaClient::login(const char* email, const byte* pwkey, const char* pin, CommandLogin::Completion completion)
{
    if (!completion)
        completion = std::bind(&MegaApp::login_result, app, std::placeholders::_1);

    string lcemail(email);

    key.setkey((byte*)pwkey);

    uint64_t emailhash = stringhash64(&lcemail, &key);

    byte sek[SymmCipher::KEYLENGTH];
    rng.genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, std::move(completion), email, (byte*)&emailhash, sizeof(emailhash), sek, 0, pin));
}

// create new session (v2)
void MegaClient::login2(const char *email, const char *password, const string *salt, const char *pin, CommandLogin::Completion completion)
{
    if (!completion)
        completion = std::bind(&MegaApp::login_result, app, std::placeholders::_1);

    string bsalt;
    Base64::atob(*salt, bsalt);

    vector<byte> derivedKey = deriveKey(password, bsalt, 2 * SymmCipher::KEYLENGTH);

    login2(email, derivedKey.data(), pin, std::move(completion));
}

void MegaClient::login2(const char *email, const byte *derivedKey, const char* pin, CommandLogin::Completion completion)
{
    if (!completion)
        completion = std::bind(&MegaApp::login_result, app, std::placeholders::_1);

    key.setkey((byte*)derivedKey);
    const byte *authKey = derivedKey + SymmCipher::KEYLENGTH;

    byte sek[SymmCipher::KEYLENGTH];
    rng.genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, std::move(completion), email, authKey, SymmCipher::KEYLENGTH, sek, 0, pin));
}

void MegaClient::fastlogin(const char* email, const byte* pwkey, uint64_t emailhash, CommandLogin::Completion completion)
{
    if (!completion)
        completion = std::bind(&MegaApp::login_result, app, std::placeholders::_1);

    key.setkey((byte*)pwkey);

    byte sek[SymmCipher::KEYLENGTH];
    rng.genblock(sek, sizeof sek);

    reqs.add(new CommandLogin(this, std::move(completion), email, (byte*)&emailhash, sizeof(emailhash), sek));
}

void MegaClient::getuserdata(int tag, std::function<void(string*, string*, string*, error)> completion)
{
    cachedug = false;

    reqs.add(new CommandGetUserData(this, tag, std::move(completion)));
}

void MegaClient::getmiscflags()
{
    reqs.add(new CommandGetMiscFlags(this));
}

void MegaClient::getpubkey(const char *user)
{
    queuepubkeyreq(user, std::make_unique<PubKeyActionNotifyApp>(reqtag));
}

// resume session - load state from local cache, if available
void MegaClient::login(string session, CommandLogin::Completion completion)
{
    if (!completion)
        completion = std::bind(&MegaApp::login_result, app, std::placeholders::_1);

    int sessionversion = 0;
    if (session.size() == sizeof key.key + SIDLEN + 1)
    {
        sessionversion = session[0];

        if (sessionversion != 1)
        {
            restag = reqtag;
            completion(API_EARGS);
            return;
        }

        session.erase(0, 1);
    }

    if (session.size() == sizeof key.key + SIDLEN)
    {
        key.setkey((const byte*)session.data());
        sid.assign((const char*)session.data() + sizeof key.key, SIDLEN);

        checkForResumeableSCDatabase();

        byte sek[SymmCipher::KEYLENGTH];
        rng.genblock(sek, sizeof sek);

        reqs.add(new CommandLogin(this, std::move(completion), NULL, NULL, 0, sek, sessionversion));
        fetchtimezone();
    }
    else if (!session.empty() && session[0] == 2)
    {
        // folder link - read only or writable

        CacheableReader cr(session);

        byte sessionVersion;
        handle publicHandle, rootnode;
        byte k[FOLDERNODEKEYLENGTH];
        string writeAuth, accountAuth, padding;
        byte expansions[8];

        if (!cr.unserializebyte(sessionVersion) ||
            !cr.unserializenodehandle(publicHandle) ||
            !cr.unserializenodehandle(rootnode) ||
            !cr.unserializebinary(k, sizeof(k)) ||
            !cr.unserializeexpansionflags(expansions, 3) ||
            (expansions[0] && !cr.unserializestring(writeAuth)) ||
            (expansions[1] && !cr.unserializestring(accountAuth)) ||
            (expansions[2] && !cr.unserializestring(padding)) ||
            cr.hasdataleft())
        {
            restag = reqtag;
            completion(API_EARGS);
        }
        else
        {
            mNodeManager.setRootNodeFiles(NodeHandle().set6byte(rootnode));
            restag = reqtag;

            if (mNodeManager.getRootNodeFiles().isUndef())
            {
                completion(API_EARGS);
            }
            else
            {
                mFolderLink.mPublicHandle = publicHandle;
                mFolderLink.mWriteAuth = writeAuth;
                mFolderLink.mAccountAuth = accountAuth;

                key.setkey(k, FOLDERNODE);
                checkForResumeableSCDatabase();
                openStatusTable(true);
                completion(API_OK);
                reportLoggedInChanges();
            }
        }
    }
    else
    {
        restag = reqtag;
        completion(API_EARGS);
    }
}

// check password's integrity
error MegaClient::validatepwd(const char* pswd)
{
    User *u = finduser(me);
    if (!u)
    {
        return API_EACCESS;
    }

    if (accountversion == 1)
    {
        byte pwkey[SymmCipher::KEYLENGTH];
        pw_key(pswd, pwkey);

        SymmCipher pwcipher(pwkey);
        pwcipher.setkey((byte*)pwkey);

        string lcemail(u->email);
        uint64_t emailhash = stringhash64(&lcemail, &pwcipher);
        vector<byte> eh((byte*)&emailhash, (byte*)&emailhash + sizeof(emailhash) / sizeof(byte));

        reqs.add(new CommandValidatePassword(this, lcemail.c_str(), eh));

        return API_OK;

    }
    else if (accountversion == 2)
    {
        vector<byte> dk = deriveKey(pswd, accountsalt, 2 * SymmCipher::KEYLENGTH);
        dk = vector<byte>(dk.data() + SymmCipher::KEYLENGTH, dk.data() + 2 * SymmCipher::KEYLENGTH);
        reqs.add(new CommandValidatePassword(this, u->email.c_str(), dk));

        return API_OK;
    }
    else
    {
        return API_ENOENT;
    }
}

bool MegaClient::validatepwdlocally(const char* pswd)
{
    if (!pswd || !pswd[0] || k.size() != SymmCipher::KEYLENGTH)
    {
        return false;
    }

    string tmpk = k;
    if (accountversion == 1)
    {
        byte pwkey[SymmCipher::KEYLENGTH];
        if (pw_key(pswd, pwkey))
        {
            return false;
        }

        SymmCipher cipher(pwkey);
        cipher.ecb_decrypt((byte*)tmpk.data());
    }
    else if (accountversion == 2)
    {
        if (accountsalt.size() != 32) // SHA256
        {
            return false;
        }

        byte derivedKey[2 * SymmCipher::KEYLENGTH];
        CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;
        pbkdf2.DeriveKey(derivedKey, sizeof(derivedKey), 0, (byte*)pswd, strlen(pswd),
            (const byte*)accountsalt.data(), accountsalt.size(), 100000);

        SymmCipher cipher(derivedKey);
        cipher.ecb_decrypt((byte*)tmpk.data());
    }
    else
    {
        LOG_warn << "Version of account not supported";
        return false;
    }

    return !memcmp(tmpk.data(), key.key, SymmCipher::KEYLENGTH);
}

int MegaClient::dumpsession(string& session)
{
    session.clear();

    if (!loggedIntoFolder())
    {
        if (loggedin() == NOTLOGGEDIN)
        {
            return 0;
        }

        if (sessionkey.size())
        {
            session.resize(sizeof key.key + 1);

            session[0] = 1;

            byte k[SymmCipher::KEYLENGTH];
            SymmCipher cipher;
            cipher.setkey((const byte *)sessionkey.data(), int(sessionkey.size()));
            cipher.ecb_encrypt(key.key, k);
            memcpy(const_cast<char*>(session.data())+1, k, sizeof k);
        }
        else
        {
            session.resize(sizeof key.key);
            memcpy(const_cast<char*>(session.data()), key.key, sizeof key.key);
        }

        session.append(sid.data(), sid.size());
    }
    else
    {
        // Folder link sessions are identifed by type 2.
        // Read-only and writeable links are supported
        // As is the accountAuth if used

        CacheableWriter cw(session);

        cw.serializebyte(2);
        cw.serializenodehandle(mFolderLink.mPublicHandle);
        cw.serializenodehandle(mNodeManager.getRootNodeFiles().as8byte());
        cw.serializebinary(key.key, sizeof(key.key));
        cw.serializeexpansionflags(!mFolderLink.mWriteAuth.empty(), !mFolderLink.mAccountAuth.empty(), true);

        if (!mFolderLink.mWriteAuth.empty())
        {
            cw.serializestring(mFolderLink.mWriteAuth);
        }

        if (!mFolderLink.mAccountAuth.empty())
        {
            cw.serializestring(mFolderLink.mAccountAuth);
        }

        // make sure the final length is not equal to the old pre-versioned session length
        string padding(session.size() <= sizeof key.key + SIDLEN ? sizeof key.key + SIDLEN - session.size() + 3 : 1, 'P');
        cw.serializestring(padding);
    }
    return int(session.size());
}

void MegaClient::resendverificationemail()
{
    reqs.add(new CommandResendVerificationEmail(this));
}

void MegaClient::resetSmsVerifiedPhoneNumber()
{
    reqs.add(new CommandResetSmsVerifiedPhoneNumber(this));
}

error MegaClient::copysession()
{
    // only accounts fully confirmed are allowed to transfer a session,
    // since the transfer requires the RSA keypair to be available
    if (loggedin() != FULLACCOUNT)
    {
        return (loggedin() == NOTLOGGEDIN) ? API_ENOENT : API_EACCESS;
    }

    reqs.add(new CommandCopySession(this));
    return API_OK;
}

string MegaClient::sessiontransferdata(const char *url, string *session)
{
    std::stringstream ss;

    // open array
    ss << "[";

    // add AES key
    string aeskey;
    key.serializekeyforjs(&aeskey);
    ss << aeskey << ",\"";

    // add session ID
    ss << *session << "\",\"";

    // add URL
    if (url)
    {
        ss << url;
    }
    ss << "\",false]";

    // standard Base64 encoding
    string json = ss.str();
    string base64;
    base64.resize(json.size() * 4 / 3 + 4);
    base64.resize(Base64::btoa((byte *)json.data(), int(json.size()), (char *)base64.data()));
    std::replace(base64.begin(), base64.end(), '-', '+');
    std::replace(base64.begin(), base64.end(), '_', '/');
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
    // called from both login() and fetchnodes()
    if (dbaccess && !sctable)
    {
        string dbname;

        if (sid.size() >= SIDLEN)
        {
            dbname.resize((SIDLEN - sizeof key.key) * 4 / 3 + 3);
            dbname.resize(Base64::btoa((const byte*)sid.data() + sizeof key.key, SIDLEN - sizeof key.key, (char*)dbname.c_str()));
        }
        else if (loggedIntoFolder())
        {
            dbname.resize(NODEHANDLE * 4 / 3 + 3);
            dbname.resize(Base64::btoa((const byte*)&mFolderLink.mPublicHandle, NODEHANDLE, (char*)dbname.c_str()));
        }

        if (dbname.size())
        {
            // Historically, DB upgrades by asking the API for permission at login
            // If permission is granted, then the existing DB is discarding and a new
            // is created from the response of fetchnodes (from server)
            // NOD is a special case where existing DB can be upgraded by renaming the existing
            // file and migrating data to the new DB scheme. In consequence, we just want to
            // recycle it (hence the flag DB_OPEN_FLAG_RECYCLE)
            // Similarly, for SRW, we just need to rename the existing legacy DB, and only delete the DB if there is a downgrade (SRW to NO SRW),
            // hence why we need to increase the DB version, but without affecting the upgrade from NO SRW to SRW.
            int recycleDBVersion = (DbAccess::LEGACY_DB_VERSION == DbAccess::LAST_DB_VERSION_WITHOUT_NOD || DbAccess::LEGACY_DB_VERSION == DbAccess::LAST_DB_VERSION_WITHOUT_SRW) ?
                                            DB_OPEN_FLAG_RECYCLE :
                                            0;
            sctable.reset(dbaccess->openTableWithNodes(rng, *fsaccess, dbname, recycleDBVersion, [this](DBError error)
            {
                handleDbError(error);
            }));

            pendingsccommit = false;

            if (sctable)
            {
                DBTableNodes *nodeTable = dynamic_cast<DBTableNodes *>(sctable.get());
                assert(nodeTable);
                mNodeManager.setTable(nodeTable);

                // DB connection always has a transaction started (applies to both tables, statecache and nodes)
                // We only commit once we have an up to date SCSN and the table state matches it.
                sctable->begin();
                assert(sctable->inTransaction());
            }
            else
            {
                LOG_err << "Failed to open DB";
                MegaClient::fatalError(REASON_ERROR_DB_IO);
            }
        }
    }
}

void MegaClient::doOpenStatusTable()
{
    if (dbaccess && !statusTable)
    {
        string dbname;

        if (sid.size() >= SIDLEN)
        {
            dbname.resize((SIDLEN - sizeof key.key) * 4 / 3 + 3);
            dbname.resize(Base64::btoa((const byte*)sid.data() + sizeof key.key, SIDLEN - sizeof key.key, (char*)dbname.c_str()));
        }
        else if (loggedIntoFolder())
        {
            dbname.resize(NODEHANDLE * 4 / 3 + 3);
            dbname.resize(Base64::btoa((const byte*)&mFolderLink.mPublicHandle, NODEHANDLE, (char*)dbname.c_str()));
        }
        else
        {
            assert(false && "attempted to open status table without sid nor folderlink");
        }

        if (dbname.size())
        {
            dbname.insert(0, "status_");

            statusTable.reset(dbaccess->open(rng, *fsaccess, dbname, DB_OPEN_FLAG_RECYCLE, [this](DBError error)
            {
                handleDbError(error);
            }));
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
    JSON::copystring(&nuid, uid);
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
        JSON::copystring(&u->email, nuid.c_str());
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

    JSON::copystring(&nuid, email);
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
            umindex.erase(u->email);
            JSON::copystring(&u->email, nuid.c_str());
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

void MegaClient::dodiscarduser(User* u, bool discardnotified)
{
    if (!u)
    {
        return;
    }

    u->removepkrs(this);

    if (discardnotified)
    {
        discardnotifieduser(u);
    }

    int uidx = -1;

    if (!u->email.empty())
    {
        auto it = umindex.find(u->email);
        if (it != umindex.end())
        {
            uidx = it->second;
            umindex.erase(it);
        }
    }

    if (u->userhandle != UNDEF)
    {
        auto it = uhindex.find(u->userhandle);
        if (it != uhindex.end())
        {
            assert(uidx == -1 || uidx == it->second);
            uidx = it->second;
            uhindex.erase(it);
        }
    }

    assert(uidx != -1);
    users.erase(uidx);
}

void MegaClient::discarduser(handle uh, bool discardnotified)
{
    User *u = finduser(uh);
    dodiscarduser(u, discardnotified);
}

PendingContactRequest* MegaClient::findpcr(handle p)
{
    if (ISUNDEF(p))
    {
        return NULL;
    }

    auto& pcr = pcrindex[p];
    if (!pcr)
    {
        pcr.reset(new PendingContactRequest(p));
        assert(fetchingnodes);
        // while fetchingnodes, outgoing shares reference an "empty" PCR that is completed when `opc` is parsed
    }

    return pcr.get();
}

void MegaClient::mappcr(handle id, unique_ptr<PendingContactRequest>&& pcr)
{
    pcrindex[id] = std::move(pcr);
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
    // insecure functionality - disable
    j->storeobject();
}

void MegaClient::clearKeys()
{
    User *u = finduser(me);

    u->invalidateattr(ATTR_KEYRING);
    u->invalidateattr(ATTR_ED25519_PUBK);
    u->invalidateattr(ATTR_CU25519_PUBK);
    u->invalidateattr(ATTR_SIG_RSA_PUBK);
    u->invalidateattr(ATTR_SIG_CU255_PUBK);

}

void MegaClient::resetKeyring()
{
    delete signkey;
    signkey = NULL;

    delete chatkey;
    chatkey = NULL;
}

// process node tree (bottom up)
void MegaClient::proctree(std::shared_ptr<Node> n, TreeProc* tp, bool skipinshares, bool skipversions)
{
    if (!n) return;

    if (!skipversions || n->type != FILENODE)
    {
        sharedNode_list children = getChildren(n.get());
        for (sharedNode_list::iterator it = children.begin(); it != children.end(); )
        {
            auto& node = *it;
            it++;
            if (!(skipinshares && node->inshare))
            {
                proctree(node, tp, skipinshares);
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
        unique_ptr<User> cleanup(u && u->isTemporary ? u : nullptr);
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

User *MegaClient::getUserForSharing(const char *uid)
{
    User *u = finduser(uid, 0);
    if (!u && uid)
    {
        if (strchr(uid, '@'))   // uid is an e-mail address
        {
            string nuid;
            JSON::copystring(&nuid, uid);
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
    return u;
}

void MegaClient::queuepubkeyreq(const char *uid, std::unique_ptr<PubKeyAction> pka)
{
    User *u = getUserForSharing(uid);
    queuepubkeyreq(u, std::move(pka));
}

// rewrite keys of foreign nodes due to loss of underlying shareufskey
void MegaClient::rewriteforeignkeys(std::shared_ptr<Node> n)
{
    TreeProcForeignKeys rewrite;
    proctree(n, &rewrite);

    if (nodekeyrewrite.size())
    {
        reqs.add(new CommandNodeKeyUpdate(this, &nodekeyrewrite));
        nodekeyrewrite.clear();
    }
}

// Migrate the account to start using the new ^!keys attr.
void MegaClient::upgradeSecurity(std::function<void(Error)> completion)
{
    // Upgrade only fully logged in accounts.
    // All keys must be available before proceeding.
    if (loggedin() != FULLACCOUNT)
    {
        LOG_warn << "Not fully logged into an account to be upgraded.";
        completion(API_EARGS);
        return;
    }

    if (mKeyManager.generation())
    {
        LOG_warn << "Already upgraded";
        completion(API_OK);
        return;
    }

    LOG_debug << "Upgrading cryptographic subsystem.";

    string prEd255;
    string prCu255;
    User *u = finduser(me);
    const string *av = (u->isattrvalid(ATTR_KEYRING)) ? u->getattr(ATTR_KEYRING) : NULL;
    if (av)
    {
        unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(av, &key));
        if (tlvRecords)
        {
            tlvRecords->get(EdDSA::TLV_KEY, prEd255);
            tlvRecords->get(ECDH::TLV_KEY, prCu255);
        }
        else
        {
            LOG_warn << "Failed to decrypt keyring while initialization";
            completion(API_EKEY);
            return;
        }
    }
    else
    {
        LOG_warn << "Keys not available";
        completion(API_ETEMPUNAVAIL);
        return;
    }

    assert(prEd255.size() == EdDSA::SEED_KEY_LENGTH);
    assert(prCu255.size() == ECDH::PRIVATE_KEY_LENGTH);
    if ((prEd255.size() != EdDSA::SEED_KEY_LENGTH) || (prCu255.size() != ECDH::PRIVATE_KEY_LENGTH))
    {
        LOG_warn << "Invalid keys";
        completion(API_EKEY);
        return;
    }

    mKeyManager.setKey(key);
    mKeyManager.init(prEd255, prCu255, mPrivKey);

    int migratedInShares = 0;
    int totalInShares = 0;
    int migratedOutShares = 0;
    int totalOutShares = 0;

    sharedNode_vector shares = getInShares();
    for (auto &n : shares)
    {
        ++totalInShares;
        if (n->sharekey)
        {
            ++migratedInShares;
            mKeyManager.addShareKey(n->nodehandle, std::string((const char *)n->sharekey->key, SymmCipher::KEYLENGTH));
        }
    }

    shares = mNodeManager.getNodesWithOutShares();
    for (auto &n : shares)
    {
        ++totalOutShares;
        if (n->sharekey)
        {
            ++migratedOutShares;
            mKeyManager.addShareKey(n->nodehandle, std::string((const char *)n->sharekey->key, SymmCipher::KEYLENGTH));
        }
    }

    shares = mNodeManager.getNodesWithPendingOutShares();
    for (auto &n : shares)
    {
        ++totalOutShares;
        if (n->sharekey)
        {
            ++migratedOutShares;
            mKeyManager.addShareKey(n->nodehandle, std::string((const char *)n->sharekey->key, SymmCipher::KEYLENGTH));
        }
    }

    shares = mNodeManager.getNodesWithLinks();
    for (auto &n : shares)
    {
        ++totalOutShares;
        if (n->sharekey)
        {
            ++migratedOutShares;
            mKeyManager.addShareKey(n->nodehandle, std::string((const char *)n->sharekey->key, SymmCipher::KEYLENGTH));
        }
    }

    LOG_debug << "Migrated inshares: " << migratedInShares << " of " << totalInShares;
    LOG_debug << "Migrated outshares: " << migratedOutShares << " of " << totalOutShares;

    auto it = mAuthRings.find(ATTR_AUTHRING);
    assert(it != mAuthRings.end());
    if (it != mAuthRings.end())
    {
        mKeyManager.setAuthRing(it->second.serializeForJS());
    }

    it = mAuthRings.find(ATTR_AUTHCU255);
    assert(it != mAuthRings.end());
    if (it != mAuthRings.end())
    {
        mKeyManager.setAuthCU255(it->second.serializeForJS());
    }

    mKeyManager.commit(
    []()
    {
        // Nothing to include in this commit (apart from the changes already applied to mKeyManager).
        // putua shouldn't fail in this case. Otherwise, another client would have upgraded the account
        // at the same time and therefore we wouldn't have to apply the changes again.
    },
    [this, completion](error e)
    {
        completion(e);

        if (e) return;

        // Get pending keys for inshares
        fetchContactsKeys();
        sc_pk();
    });
}

void MegaClient::setContactVerificationWarning(bool enabled, std::function<void(Error)> completion)
{
    if (mKeyManager.getContactVerificationWarning() == enabled)
    {
        if (completion) completion(API_OK);
        return;
    }

    mKeyManager.commit(
        [this, enabled]()
        {
            mKeyManager.setContactVerificationWarning(enabled);
        },
        [completion](error e)
        {
            if (completion) completion(e);
        });
}

// Creates a new share key for the node if there is no share key already created.
void MegaClient::openShareDialog(Node* n, std::function<void(Error)> completion)
{
    if (!n)
    {
        completion(API_EARGS);
        return;
    }

    if (!mKeyManager.generation())
    {
        LOG_err << "Account not upgraded yet";
        completion(API_EINCOMPLETE);
        return;
    }

    bool updateKeys = false;
    if (!n->sharekey)
    {
        string previousKey = mKeyManager.getShareKey(n->nodehandle);
        if (!previousKey.size())
        {
            LOG_debug << "Creating new share key for " << toHandle(n->nodehandle);
            byte key[SymmCipher::KEYLENGTH];
            rng.genblock(key, sizeof key);
            n->sharekey.reset(new SymmCipher(key));
            updateKeys = true;
        }
        else
        {
            LOG_debug << "Setting node's sharekey from KeyManager (openShareDialog)";
            n->sharekey.reset(new SymmCipher((const byte*)previousKey.data()));
        }
    }
    else assert(mKeyManager.getShareKey(n->nodehandle).size());

    if (updateKeys)    // new share: add key to ^!keys
    {
        handle nodehandle = n->nodehandle;
        std::string shareKey((const char *)n->sharekey->key, SymmCipher::KEYLENGTH);

        LOG_debug << "Adding new share key to ^!keys for outshare " << toNodeHandle(nodehandle);
        mKeyManager.commit(
        [this, nodehandle, shareKey]()
        {
            // Changes to apply in the commit
            mKeyManager.addShareKey(nodehandle, shareKey, true);
        },
        [completion](error e)
        {
            completion(e);
        });
    }
    else
    {
        completion(API_OK);
    }
}

// if user has a known public key, complete instantly
// otherwise, queue and request public key if not already pending
// `user` is null for creating folder links
void MegaClient::setshare(std::shared_ptr<Node> n, const char* user, accesslevel_t a, bool writable, const char* personal_representation, int tag, std::function<void(Error, bool writable)> completion)
{
    assert(completion);

    if (!mKeyManager.generation())
    {
        LOG_err << "Account not upgraded yet";
        completion(API_EINCOMPLETE, writable);
        return;
    }

    size_t total = n->outshares ? n->outshares->size() : 0;
    total += n->pendingshares ? n->pendingshares->size() : 0;
    if (a == ACCESS_UNKNOWN && total == 1)
    {
        // rewrite keys of foreign nodes located in the outbound share that is getting canceled
        // FIXME: verify that it is really getting canceled to prevent benign premature rewrite
        rewriteforeignkeys(n);
    }

    if (a == ACCESS_UNKNOWN)
    {
        User *u = getUserForSharing(user);
        handle nodehandle = n->nodehandle;
        reqs.add(new CommandSetShare(this, n, u, a, 0, NULL, writable, personal_representation, tag,
        [this, u, total, nodehandle, completion](Error e, bool writable)
        {
            if (!e && total == 1)
            {
                if (mKeyManager.isShareKeyInUse(nodehandle))
                {
                    LOG_debug << "Last share: disabling in-use flag for the sharekey in KeyManager. nh: " << toNodeHandle(nodehandle);
                    mKeyManager.commit(
                    [this, nodehandle]()
                    {
                        mKeyManager.setSharekeyInUse(nodehandle, false);

                    },
                    [completion, e, writable](error commitError)
                    {
                        completion(commitError, writable);
                    });
                }
                else
                {
                    if (mKeyManager.isShareKeyTrusted(nodehandle))
                    {
                        LOG_warn << "in-use flag was already disabled for the sharekey in KeyManager when removing the last share. nh: " << toNodeHandle(nodehandle);
                    }
                    completion(e, writable);
                }
            }
            else
            {
                completion(e, writable);
            }

            if (u && u->isTemporary)
            {
                delete u;
            }
        }));
        return;
    }

    User *u = getUserForSharing(user);
    setShareCompletion(n.get(), u, a, writable, personal_representation, tag, std::move(completion)); // will release u, if temporary
}

void MegaClient::setShareCompletion(Node *n, User *user, accesslevel_t a, bool writable, const char* personal_representation, int tag, std::function<void(Error, bool writable)> completion)
{
    std::string msg;
    if (personal_representation)
    {
        msg = personal_representation;
    }

    std::string uid;
    if (user)
    {
        uid = (user->show == VISIBLE) ? user->uid : user->email;
    }

    bool newshare = !n->isShared();

    // if creating a folder link and there's no sharekey already
    bool newShareKey = false;
    if (!n->sharekey && uid.empty())
    {
        assert(newshare);

        string previousKey = mKeyManager.getShareKey(n->nodehandle);
        if (!previousKey.size())
        {
            LOG_debug << "Creating new share key for folder link on " << toHandle(n->nodehandle);
            byte key[SymmCipher::KEYLENGTH];
            rng.genblock(key, sizeof key);
            n->sharekey.reset(new SymmCipher(key));
            newShareKey = true;
        }
        else
        {
            LOG_debug << "Reusing node's sharekey from KeyManager for folder link on " << toHandle(n->nodehandle);
            n->sharekey.reset(new SymmCipher((const byte*)previousKey.data()));
        }
    }

    if (!n->sharekey)
    {
        LOG_err << "You should first create the key using MegaClient::openShareDialog (setshare)";
        completion(API_EKEY, writable);
        if (user && user->isTemporary) delete user;
        return;
    }

    handle nodehandle = n->nodehandle;
    std::string shareKey((const char *)n->sharekey->key, SymmCipher::KEYLENGTH);

    std::function<void()> completeShare =
    [this, user, nodehandle, a, newshare, msg, tag, writable, completion]()
    {
        std::shared_ptr<Node> n;
        // node vanished: bail
        if (!(n = nodebyhandle(nodehandle)))
        {
            completion(API_ENOENT, writable);
            if (user && user->isTemporary) delete user;
            return;
        }

        reqs.add(new CommandSetShare(this, n, user, a, newshare, NULL, writable, msg.c_str(), tag,
        [this, user, newshare, nodehandle, completion](Error e, bool writable)
        {
            if (!e)
            {
                if (mKeyManager.isShareKeyTrusted(nodehandle) && !mKeyManager.isShareKeyInUse(nodehandle))
                {
                    if (!newshare)
                    {
                        LOG_warn << "in-use flag for the sharekey in KeyManager is not set but the node was already shared. nh: " << toNodeHandle(nodehandle);
                    }

                    LOG_debug << "Enabling in-use flag for the sharekey in KeyManager. nh: " << toNodeHandle(nodehandle);
                    mKeyManager.commit(
                    [this, nodehandle]()
                    {
                        mKeyManager.setSharekeyInUse(nodehandle, true);
                    },
                    [completion, e, writable](error commitError)
                    {
                        completion(commitError, writable);
                    });
                }
                else
                {
                    if (!mKeyManager.isShareKeyTrusted(nodehandle)) // Legacy share
                    {
                        LOG_debug << "in-use flag for the sharekey in KeyManager not set. Share Key is not trusted. nh: " << toNodeHandle(nodehandle);
                    }
                    else if (newshare) // trusted, bit set but was not shared.
                    {
                        LOG_err << "in-use flag for the sharekey in KeyManager is already set but the node was not being shared before. nh: " << toNodeHandle(nodehandle);
                        string msg = "in-use flag already set for a node with no previous active share";
                        sendevent(99479, msg.c_str());
                        assert(!newshare && msg.c_str());
                    }
                    completion(e, writable);
                }
            }
            else
            {
                completion(e, writable);
            }

            if (user && user->isTemporary) delete user;
        }));
    };

    if (uid.size() || newShareKey) // share with a user or folder-link requiring new sharekey
    {
        LOG_debug << "Updating ^!keys before sharing " << toNodeHandle(nodehandle);
        mKeyManager.commit(
        [this, newShareKey, nodehandle, shareKey, uid]()
        {
            // Changes to apply in the commit
            if (newShareKey)
            {
                // Add outshare key into ^!keys
                mKeyManager.addShareKey(nodehandle, shareKey, true);
            }

            if (uid.size()) // not a folder link, but a share with a user
            {
                // Add pending outshare;
                mKeyManager.addPendingOutShare(nodehandle, uid);
            }
        },
        [completeShare, completion, user, writable](error e)
        {
            if (e == API_OK) completeShare();
            else
            {
                completion(e, writable);
                if (user && user->isTemporary) delete user;
            }
        });
        return;
    }
    else // folder link on an already shared folder or reusing existing sharekey -> no need to update ^!keys
    {
        completeShare();
    }
}

// Add/delete/remind outgoing pending contact request
void MegaClient::setpcr(const char* temail, opcactions_t action, const char* msg, const char* oemail, handle contactLink, CommandSetPendingContact::Completion completion)
{
    reqs.add(new CommandSetPendingContact(this, temail, action, msg, oemail, contactLink, std::move(completion)));
}

void MegaClient::updatepcr(handle p, ipcactions_t action, CommandUpdatePendingContact::Completion completion)
{
    reqs.add(new CommandUpdatePendingContact(this, p, action, std::move(completion)));
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
error MegaClient::removecontact(const char* email, visibility_t show, CommandRemoveContact::Completion completion)
{
    if (!strchr(email, '@') || (show != HIDDEN && show != BLOCKED))
    {
        return API_EARGS;
    }

    reqs.add(new CommandRemoveContact(this, email, show, std::move(completion)));

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
void MegaClient::putua(attr_t at, const byte* av, unsigned avl, int ctag, handle lastPublicHandle, int phtype, int64_t ts,
                       std::function<void(Error)> completion)
{
    string data;

    if (!completion)
    {
        completion = [this](Error e){
            app->putua_result(e);
        };
    }

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
        completion(API_EACCESS);
        return;
    }
    int needversion = u->needversioning(at);
    if (needversion == -1)
    {
        restag = tag;
        completion(API_EARGS);   // attribute not recognized
        return;
    }

    if (avl >= User::getMaxAttributeSize(at))
    {
        sendevent(99483, "User attribute exceeds maximum size");
        LOG_err << "User attribute exceeds maximum size: " << User::attr2string(at);
        restag = tag;
        return completion(API_EARGS);
    }

    if (!needversion)
    {
        reqs.add(new CommandPutUA(this, at, av, avl, tag, lastPublicHandle, phtype, ts, std::move(completion)));
    }
    else
    {
        // if the cached value is outdated, first need to fetch the latest version
        if (u->getattr(at) && !u->isattrvalid(at))
        {
            restag = tag;
            completion(API_EEXPIRED);
            return;
        }
        reqs.add(new CommandPutUAVer(this, at, av, avl, tag, std::move(completion)));
    }
}

void MegaClient::putua(userattr_map *attrs, int ctag, std::function<void (Error)> completion)
{
    int tag = (ctag != -1) ? ctag : reqtag;
    User *u = ownuser();

    if (!completion)
    {
        completion = [this](Error e){
            app->putua_result(e);
        };
    }

    if (!u || !attrs || !attrs->size())
    {
        restag = tag;
        return completion(API_EARGS);
    }

    for (userattr_map::iterator it = attrs->begin(); it != attrs->end(); it++)
    {
        attr_t type = it->first;

        if (User::needversioning(type) != 1)
        {
            restag = tag;
            return completion(API_EARGS);
        }

        // if the cached value is outdated, first need to fetch the latest version
        if (u->getattr(type) && !u->isattrvalid(type))
        {
            restag = tag;
            return completion(API_EEXPIRED);
        }

        if (it->second.size() >= User::getMaxAttributeSize(type))
        {
            sendevent(99483, "User attribute exceeds maximum size");
            LOG_err << "User attribute exceeds maximum size: " << User::attr2string(it->first);
            restag = tag;
            completion(API_EARGS);
            return;
        }
    }

    reqs.add(new CommandPutMultipleUAVer(this, attrs, tag, std::move(completion)));
}

/**
 * @brief Queue a user attribute retrieval.
 *
 * @param u User.
 * @param at Attribute type.
 * @param ctag Tag to identify the request at intermediate layer
 *
 * @return False when attribute requires a request to server. False otherwise (if cached, or unknown)
 */
bool MegaClient::getua(User* u, const attr_t at, int ctag, mega::CommandGetUA::CompletionErr completionErr, mega::CommandGetUA::CompletionBytes completionBytes, mega::CommandGetUA::CompletionTLV completionTLV)
{
    if (at != ATTR_UNKNOWN)
    {
        // if we can solve those requests locally (cached values)...
        const string *cachedav = u->getattr(at);
        int tag = (ctag != -1) ? ctag : reqtag;

        if (cachedav && u->isattrvalid(at))
        {
            if (User::scope(at) == '*') // private attribute, TLV encoding
            {
                TLVstore *tlv = TLVstore::containerToTLVrecords(cachedav, &key);
                restag = tag;
                completionTLV ? completionTLV(tlv, at) : app->getua_result(tlv, at);
                delete tlv;
                return true;
            }
            else
            {
                restag = tag;
                completionBytes ? completionBytes((byte*) cachedav->data(), unsigned(cachedav->size()), at)
                                : app->getua_result((byte*) cachedav->data(), unsigned(cachedav->size()), at);
                return true;
            }
        }
        else if (u->nonExistingAttribute(at))  // only own user attrs get marked as "no exits"
        {
            assert(u->userhandle == me);
            restag = tag;
            completionErr ? completionErr(API_ENOENT) : app->getua_result(API_ENOENT);
        }
        else
        {
            reqs.add(new CommandGetUA(this, u->uid.c_str(), at, NULL, tag, completionErr, completionBytes, completionTLV));
            return false;
        }
    }
    return true;
}

void MegaClient::getua(const char *email_handle, const attr_t at, const char *ph, int ctag, mega::CommandGetUA::CompletionErr ce, mega::CommandGetUA::CompletionBytes cb, mega::CommandGetUA::CompletionTLV ctlv)
{
    if (email_handle && at != ATTR_UNKNOWN)
    {
        reqs.add(new CommandGetUA(this, email_handle, at, ph, (ctag != -1) ? ctag : reqtag, ce, cb, ctlv));
    }
}

void MegaClient::getUserEmail(const char *uid)
{
    reqs.add(new CommandGetUserEmail(this, uid));
}

void MegaClient::loginResult(CommandLogin::Completion completion,
                             error e,
                             std::function<void()> onLoginOk)
{
    if (e != API_OK)
    {
        mV1PswdVault.reset(); // clear this before the app knows that login is done, might improve security
        completion(e);
        return;
    }

    // Capture the client's response tag as apps may require it.
    completion = [completion = std::move(completion), tag = restag, this](error result) {
        ScopedValue restorer(restag, tag);
        completion(result);
    }; // completion

    // Wrap the user's completion function so that we also initialize the sync engine.
    completion = [completion = std::move(completion), this](error result) {
        // Initialize the sync engine and call the user's completion function.
        injectSyncSensitiveData(std::move(completion), result);
    }; // completion

    assert(!mV1PswdVault || accountversion == 1);

    if (accountversion == 1 && mV1PswdVault)
    {
        auto v1PswdVault(std::move(mV1PswdVault));

        if (loggedin() == FULLACCOUNT)
        {
            // initiate automatic upgrade to V2
            unique_ptr<TLVstore> tlv(TLVstore::containerToTLVrecords(&v1PswdVault->first, &v1PswdVault->second));
            string pwd;
            if (tlv && tlv->get("p", pwd))
            {
                if (pwd.empty())
                {
                    char msg[] = "Account upgrade to v2 has failed (invalid content in vault)";
                    LOG_err << msg;
                    sendevent(99475, msg);

                    // report successful login, even if upgrade failed; user data was not affected, so apps can continue running
                    completion(API_OK);
                    if (onLoginOk)
                    {
                        onLoginOk();
                    }
                    return;
                }

                upgradeAccountToV2(pwd, restag, [this, completion, onLoginOk](error e)
                    {
                        // handle upgrade result
                        if (e == API_EEXIST)
                        {
                            LOG_debug << "Account upgrade to V2 failed with EEXIST. It must have been upgraded in the meantime. Fetching user data again.";

                            // upgrade done in the meantime by different client; get account details again
                            getuserdata(restag, [this, completion, onLoginOk](string*, string*, string*, error e)
                                {
                                    error loginErr = e == API_OK ? API_OK : API_EINTERNAL;
                                    completion(loginErr); // if error, report for login too because user data is inconsistent now

                                    if (e != API_OK)
                                    {
                                        LOG_err << "Failed to get user data after acccount upgrade to V2 ended with EEXIST, error: " << e;
                                    }
                                    else if (onLoginOk)
                                    {
                                        onLoginOk();
                                    }
                                }
                            );
                        }

                        else
                        {
                            if (e == API_OK)
                            {
                                LOG_info << "Account successfully upgraded to V2.";
                            }
                            else
                            {
                                LOG_warn << "Failed to upgrade account to V2, error: " << e;
                            }

                            // report successful login, even if upgrade failed; user data was not affected, so apps can continue running
                            completion(API_OK);
                            if (onLoginOk)
                            {
                                onLoginOk();
                            }
                        }
                    }
                );

                return; // stop here when account upgrade was initiated
            }
        }
    }

    // V2, or V1 without mandatory requirements for upgrade
    completion(API_OK);
    if (onLoginOk)
    {
        onLoginOk();
    }
}

//
// Account upgrade to V2
//
void MegaClient::saveV1Pwd(const char* pwd)
{
    assert(pwd);
    if (pwd && accountversion == 1)
    {
        vector<byte> pwkey(SymmCipher::KEYLENGTH);
        rng.genblock(pwkey.data(), pwkey.size());
        SymmCipher pwcipher(pwkey.data());

        TLVstore tlv;
        tlv.set("p", pwd);
        unique_ptr<string> tlvStr(tlv.tlvRecordsToContainer(rng, &pwcipher));

        if (tlvStr)
        {
            mV1PswdVault.reset(new pair<string, SymmCipher>(std::move(*tlvStr), std::move(pwcipher)));
        }
    }
}

void MegaClient::upgradeAccountToV2(const string& pwd, int ctag, std::function<void(error e)> completion)
{
    assert(loggedin() == FULLACCOUNT);
    assert(accountversion == 1);
    assert(!pwd.empty());

    vector<byte> clientRandomValue;
    vector<byte> encmasterkey;
    string hashedauthkey;
    string salt;

    fillCypheredAccountDataV2(pwd.c_str(), clientRandomValue, encmasterkey, hashedauthkey, salt);

    reqs.add(new CommandAccountVersionUpgrade(std::move(clientRandomValue), std::move(encmasterkey), std::move(hashedauthkey), std::move(salt), ctag, completion));
}
// -------- end of Account upgrade to V2

#ifdef DEBUG
void MegaClient::delua(const char *an)
{
    if (an)
    {
        reqs.add(new CommandDelUA(this, an));
    }
}

void MegaClient::senddevcommand(const char* command,
                                const char* email,
                                long long q,
                                int bs,
                                int us,
                                const char* abs_c)
{
    reqs.add(new CommandSendDevCommand(this, command, email, q, bs, us, abs_c));
}
#endif

void MegaClient::transfercacheadd(Transfer *transfer, TransferDbCommitter* committer)
{
    if (tctable && !transfer->skipserialization)
    {
        if (committer) committer->addTransferCount += 1;
        tctable->checkCommitter(committer);
        tctable->put(MegaClient::CACHEDTRANSFER, transfer, &tckey);
    }
}

void MegaClient::transfercachedel(Transfer *transfer, TransferDbCommitter* committer)
{
    if (tctable && transfer->dbid)
    {
        if (committer) committer->removeTransferCount += 1;
        tctable->checkCommitter(committer);
        tctable->del(transfer->dbid);
    }
}

void MegaClient::filecacheadd(File *file, TransferDbCommitter& committer)
{
    if (tctable && !file->syncxfer)
    {
        committer.addFileCount += 1;
        tctable->checkCommitter(&committer);
        tctable->put(MegaClient::CACHEDFILE, file, &tckey);
    }
}

void MegaClient::filecachedel(File *file, TransferDbCommitter* committer)
{
    if (tctable && !file->syncxfer)
    {
        if (committer) committer->removeFileCount += 1;
        tctable->checkCommitter(committer);
        tctable->del(file->dbid);
    }

    if (file->temporaryfile)
    {
        LOG_debug << "Removing temporary file";
        fsaccess->unlinklocal(file->getLocalname());
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
        chatnotify[chat->getChatId()] = chat;
    }
}
#endif

// process request for share node keys
// builds & emits k/cr command
// returns 1 in case of a valid response, 0 otherwise
void MegaClient::proccr(JSON* j)
{
    sharedNode_vector shares, nodes;
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
            j->leavearray();
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
            j->leavearray();
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
                j->leavearray();
                j->leavearray();
                return;
            }

            if (ISUNDEF((nh = j->gethandle())))
            {
                j->leavearray();
                j->leavearray();
                return;
            }

            std::shared_ptr<Node> sn = nodebyhandle(sh);

            if (sn && sn->sharekey && checkaccess(sn.get(), OWNER))
            {
                std::shared_ptr<Node> n = nodebyhandle(nh);

                if (n && n->isbelow(sn.get()))
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
                        bool meeting = false;

                        // chat options: [0 (remove) | 1 (add)], if chat option is not included, that option is disabled
                        int waitingRoom = 0;
                        int openInvite = 0;
                        int speakRequest = 0;

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

                           case MAKENAMEID2('m', 'r'):    // meeting room: 1; no meeting room: 0
                                meeting = j->getbool();
                                assert(readingPublicChats || !meeting); // public chats can be meetings or not. Private chats cannot be meetings
                                break;

                            case 'w':   // waiting room
                                waitingRoom = static_cast<int>(j->getint());
                                break;

                            case MAKENAMEID2('s','r'): // speak request
                                speakRequest = static_cast<int>(j->getint());
                                break;

                            case MAKENAMEID2('o','i'): // open invite
                                openInvite = static_cast<int>(j->getint());
                                break;

                            case EOO:
                                if (chatid != UNDEF && priv != PRIV_UNKNOWN && shard != -1)
                                {
                                    TextChat* chat = nullptr;
                                    if (chats.find(chatid) == chats.end())
                                    {
                                        chat = new TextChat(readingPublicChats && publicchat);
                                        chats[chatid] = chat;
                                    }
                                    else
                                    {
                                        chat = chats[chatid];
                                        if (readingPublicChats) { setChatMode(chat, publicchat); }
                                    }

                                    chat->setChatId(chatid);
                                    chat->setOwnPrivileges(priv);
                                    chat->setShard(shard);
                                    chat->setGroup(group);
                                    chat->setTitle(title);
                                    chat->setTs(ts != -1 ? ts : 0);
                                    chat->setMeeting(meeting);

                                    if (group)
                                    {
                                        chat->addOrUpdateChatOptions(speakRequest, waitingRoom, openInvite);
                                    }

                                    if (readingPublicChats)
                                    {
                                        chat->setUnifiedKey(unifiedKey);

                                        if (unifiedKey.empty())
                                        {
                                            LOG_err << "Received public (or formerly public) chat without unified key";
                                        }
                                    }

                                    // remove yourself from the list of users (only peers matter)
                                    if (userpriv)
                                    {
                                        if (chat->getOwnPrivileges() == PRIV_RM)
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
                                    chat->setUserPrivileges(userpriv);
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
                                        assert(!readingPublicChats || !it->second->getUnifiedKey().empty());
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

// process mcsm array at fetchnodes
void MegaClient::procmcsm(JSON *j)
{
    std::vector<std::unique_ptr<ScheduledMeeting>> schedMeetings;
    if (j && j->enterarray())
    {
        error e = parseScheduledMeetings(schedMeetings, false, j);
        if (e != API_OK)
        {
            LOG_err << "Failed to parse 'mcsm' array at fetchnodes. Error: " << e;
        }

        if (!j->leavearray())
        {
            LOG_err << "Failed to leave array at procmcsm. Error: " << API_EINTERNAL;
        }

        if (e != API_OK) { return; }
    }

    for (auto &sm: schedMeetings)
    {
        textchat_map::iterator it = chats.find(sm->chatid());
        if (it == chats.end())
        {
            assert(false);
            LOG_err << "Unknown chatid [" <<  Base64Str<MegaClient::CHATHANDLE>(sm->chatid()) << "] received on mcsm";
            continue;
        }

        // add scheduled meeting
        TextChat* chat = it->second;
        chat->addOrUpdateSchedMeeting(std::move(sm), false); // don't need to notify, as chats are also provided to karere
    }
}
#endif

// add node to vector, return position, deduplicate
unsigned MegaClient::addnode(sharedNode_vector* v, std::shared_ptr<Node> n) const
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
void MegaClient::cr_response(sharedNode_vector* shares, sharedNode_vector* nodes, JSON* selector)
{
    sharedNode_vector rshares, rnodes;
    unsigned si, ni = unsigned(-1);
    std::shared_ptr<Node> sn;
    std::shared_ptr<Node> n;
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
            LOG_warn << "Attempt to obtain node key for invalid/third-party share foiled: " << toNodeHandle((*shares)[si]->nodehandle);
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
            if (n->isbelow(sn.get()))
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

                        snprintf(buf, sizeof(buf), "\",%u,%u,\"", nsi, nni);

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

                mNodeManager.updateNode(n.get());
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

void MegaClient::getaccountdetails(std::shared_ptr<AccountDetails> ad, bool storage,
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

void MegaClient::getstorageinfo(std::function<void(const StorageInfo&, Error)> completion)
{
    assert(completion);

    // Haven't cached any info from the cloud.
    if (mLastKnownCapacity < 0)
    {
        auto wrapper = [this](std::function<void(const StorageInfo&, Error)>& completion,
                              const std::shared_ptr<AccountDetails> details,
                              Error result) {
            StorageInfo info;
            // Latch the account's capacity.
            if (result == API_OK)
            {
                assert(details);
                info.mCapacity = details->storage_max;
                info.mUsed = details->storage_used;
                if (info.mCapacity >= info.mUsed)
                {
                    info.mAvailable = info.mCapacity - info.mUsed;
                }
                mLastKnownCapacity = info.mCapacity;
            }

            // Forward result to user callback.
            completion(info, result);
        }; // wrapper

        std::function<void(std::shared_ptr<AccountDetails>, Error)> callback;
        callback = std::bind(std::move(wrapper),
                             std::move(completion),
                             std::placeholders::_1,
                             std::placeholders::_2);

        return reqs.add(new CommandGetUserQuota(this,
                                                std::make_shared<AccountDetails>(),
                                                true  /* storage */,
                                                false /* transfer */,
                                                false /* pro */,
                                                -1 /* source */,
                                                std::move(callback)));
    }

    // Ask the NM for our root node's storage info.
    auto counter = mNodeManager.getCounterOfRootNodes();

    StorageInfo info;

    // Populate info based on counter and cached state.
    info.mUsed = counter.storage + counter.versionStorage;
    info.mCapacity = mLastKnownCapacity;

    if (info.mCapacity >= info.mUsed)
        info.mAvailable = info.mCapacity - info.mUsed;

    // Forward info to caller.
    completion(info, API_OK);
}

void MegaClient::querytransferquota(m_off_t size)
{
    reqs.add(new CommandQueryTransferQuota(this, size));
}

// export node link
error MegaClient::exportnode(std::shared_ptr<Node> n, int del, m_time_t ets, bool writable, bool megaHosted,
    int tag, std::function<void(Error, handle, handle)> completion)
{
    if (n->plink && !del && !n->plink->takendown
            && (ets == n->plink->ets) && !n->plink->isExpired()
            && ( (writable && n->plink->mAuthKey.size()) || (!writable && !n->plink->mAuthKey.size()) )
                 )
    {
        if (ststatus == STORAGE_PAYWALL)
        {
            LOG_warn << "Rejecting public link request when ODQ paywall";
            return API_EPAYWALL;
        }
        restag = tag;
        completion(API_OK, n->nodehandle, n->plink->ph);
        return API_OK;
    }

    if (!checkaccess(n.get(), OWNER))
    {
        return API_EACCESS;
    }

    // export node
    switch (n->type)
    {
    case FILENODE:
        requestPublicLink(n.get(), del, ets, writable, false, tag, std::move(completion));
        break;

    case FOLDERNODE:
        if (del)
        {
            // deletion of outgoing share also deletes the link automatically
            // need to first remove the link and then the share
            NodeHandle h = n->nodeHandle();
            requestPublicLink(n.get(), del, ets, writable, false, tag, [this, completion, writable, tag, h](Error e, handle, handle){
                std::shared_ptr<Node> n = nodeByHandle(h);
                if (e || !n)
                {
                    completion(e, UNDEF, UNDEF);
                }
                else
                {
                    setshare(n, NULL, ACCESS_UNKNOWN, writable, nullptr, tag, [completion](Error e, bool) {
                        completion(e, UNDEF, UNDEF);
                        });
                }
            });
        }
        else
        {
            // Exporting folder - need to create share first
            // If share creation is successful, the share completion function calls requestPublicLink

            handle h = n->nodehandle;

            setshare(n, NULL, writable ? FULL : RDONLY, writable, nullptr, tag,
                     [this, megaHosted, h, ets, tag, writable, completion](Error e, bool)
            {
                if (e)
                {
                    completion(e, UNDEF, UNDEF);
                }
                else if (std::shared_ptr<Node> node = nodebyhandle(h))
                {
                    requestPublicLink(node.get(), false, ets, writable, megaHosted, tag, completion);
                }
                else
                {
                    completion(API_ENOENT, UNDEF, UNDEF);
                }
            });
        }
        break;

    default:
        return API_EACCESS;
    }

    return API_OK;
}

void MegaClient::requestPublicLink(Node* n, int del, m_time_t ets, bool writable, bool megaHosted, int tag, std::function<void(Error, handle, handle)> f)
{
    reqs.add(new CommandSetPH(this, n, del, ets, writable, megaHosted, tag, std::move(f)));
}

// open exported file link
// formats supported: ...#!publichandle!key, publichandle!key or file/<ph>[<params>][#<key>]
void MegaClient::openfilelink(handle ph, const byte *key)
{
    reqs.add(new CommandGetPH(this, ph, key, 1));   // check link
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

    string salt(ptr, 32);
    ptr += salt.size();

    string encKey;
    encKey.resize(encKeyLen);
    memcpy((byte *)encKey.data(), ptr, encKeyLen);
    ptr += encKeyLen;

    byte hmac[32];
    memcpy((char*)&hmac, ptr, 32);
    ptr += 32;

    // Derive MAC key with salt+pwd
    vector<byte> derivedKey = deriveKey(pwd, salt, 64);

    byte hmacComputed[32];
    if (algorithm == 1)
    {
        // verify HMAC with macKey(alg, f/F, ph, salt, encKey)
        HMACSHA256 hmacsha256((byte *)linkBin.data(), 40 + encKeyLen);
        hmacsha256.add(derivedKey.data() + 32, 32);
        hmacsha256.get(hmacComputed);
    }
    else // algorithm == 2 (fix legacy Webclient bug: swap data and key)
    {
        // verify HMAC with macKey(alg, f/F, ph, salt, encKey)
        HMACSHA256 hmacsha256(derivedKey.data() + 32, 32);
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
            key[i] = static_cast<byte>(encKey[i] ^ derivedKey[i]);
        }

        Base64Str<FILENODEKEYLENGTH> keyStr(key);
        decryptedLink->assign(publicLinkURL(mNewLinkFormat, isFolder ? TypeOfLink::FOLDER : TypeOfLink::FILE, ph, keyStr));
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

    if(strstr(link, "collection/"))
    {
        LOG_err << "Attempting to encrypt a non-folder, non-file link";
        assert(false);
        return API_EARGS;
    }

    bool isFolder = (strstr(link, "#F!") || strstr(link, "folder/"));
    handle ph;
    size_t linkKeySize = isFolder ? FOLDERNODEKEYLENGTH : FILENODEKEYLENGTH;
    std::unique_ptr<byte[]> linkKey(new byte[linkKeySize]);
    error e = parsepubliclink(link, ph, linkKey.get(), (isFolder ? TypeOfLink::FOLDER : TypeOfLink::FILE));
    if (e == API_OK)
    {
        // Derive MAC key with salt+pwd
        string salt(32u, '\0');
        rng.genblock((byte*)salt.data(), salt.size());
        vector<byte> derivedKey = deriveKey(pwd, salt, 64);

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
        payload.append(salt);
        payload.append(encKey);


        // Prepare HMAC
        byte hmac[32];
        if (algorithm == 1)
        {
            HMACSHA256 hmacsha256((byte *)payload.data(), payload.size());
            hmacsha256.add(derivedKey.data() + 32, 32);
            hmacsha256.get(hmac);
        }
        else if (algorithm == 2) // fix legacy Webclient bug: swap data and key
        {
            HMACSHA256 hmacsha256(derivedKey.data() + 32, 32);
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
        encLinkBytes.append(salt);
        encLinkBytes.append(encKey);
        encLinkBytes.append((char*) hmac, sizeof hmac);

        string encLink;
        Base64::btoa(encLinkBytes, encLink);

        encryptedLink->clear();
        encryptedLink->append(MegaClient::MEGAURL);
        encryptedLink->append("/#P!");
        encryptedLink->append(encLink);

        if (isFolder)
        {
            sendevent(99459, "Public folder link encrypted to a password");
        }
        else
        {
            sendevent(99460, "Public file link encrypted to a password");
        }
    }

    return e;
}

sessiontype_t MegaClient::loggedin()
{
    if (ISUNDEF(me))
    {
        return NOTLOGGEDIN;
    }

    if (ephemeralSessionPlusPlus)
    {
        return EPHEMERALACCOUNTPLUSPLUS;
    }

    if (ephemeralSession)
    {
        return EPHEMERALACCOUNT;
    }

    if (!asymkey.isvalid(AsymmCipher::PRIVKEY))
    {
        return CONFIRMEDACCOUNT;
    }

    return FULLACCOUNT;
}

void MegaClient::reportLoggedInChanges()
{
    auto currState = loggedin();
    string currentEmail = ownuser() ? ownuser()->email : "";
    if (mLastLoggedInReportedState != currState ||
            mLastLoggedInMeHandle != me ||
            (mLastLoggedInMyEmail != currentEmail))
    {
        mLastLoggedInReportedState = currState;
        mLastLoggedInMeHandle = me;
        mLastLoggedInMyEmail = currentEmail;
        app->loggedInStateChanged(currState, me, currentEmail);
    }
}

void MegaClient::whyamiblocked()
{
    // make sure the smsve flag is up to date when we get the response
    getmiscflags();

    // queue the actual request
    reqs.add(new CommandWhyAmIblocked(this));
}

void MegaClient::setBlocked(bool value)
{
    mBlocked = value;
    mBlockedSet = true;

    mCachedStatus.addOrUpdate(CacheableStatus::STATUS_BLOCKED, mBlocked);
}

void MegaClient::block(bool fromServerClientResponse)
{
    LOG_verbose << "Blocking MegaClient, fromServerClientResponse: " << fromServerClientResponse;
    setBlocked(true);
#ifdef ENABLE_SYNC
    syncs.disableSyncs(ACCOUNT_BLOCKED, false, true);
#endif
}

void MegaClient::unblock()
{
    LOG_verbose << "Unblocking MegaClient";
    setBlocked(false);
}

error MegaClient::changepw(const char* password, const char *pin)
{
    User* u;

    if (!loggedin() || !(u = finduser(me)))
    {
        return API_EACCESS;
    }

    // Confirm account version, not rely on cached values
    string spwd = password ? password : string();
    string spin = pin ? pin : string();
    getuserdata(reqtag,
        [this, u, spwd, spin](string* name, string* pubk, string* privk, error e)
        {
            if (e != API_OK)
            {
                app->changepw_result(e);
                return;
            }

            switch (accountversion)
            {
            case 1:
                e = changePasswordV1(u, spwd.c_str(), spin.c_str());
                break;

            default:
                LOG_warn << "Unexpected account version v" << accountversion << " processed as v2";
                // fallthrough

            case 2:
                e = changePasswordV2(spwd.c_str(), spin.c_str());
                break;
            }

            if (e != API_OK)
            {
                app->changepw_result(e);
            }
        }
    );

    return API_OK;
}

error MegaClient::changePasswordV1(User* u, const char* password, const char* pin)
{
    error e;
    byte newpwkey[SymmCipher::KEYLENGTH];
    if ((e = pw_key(password, newpwkey)))
    {
        return e;
    }

    byte newkey[SymmCipher::KEYLENGTH];
    SymmCipher pwcipher;
    memcpy(newkey, key.key, sizeof newkey);
    pwcipher.setkey(newpwkey);
    pwcipher.ecb_encrypt(newkey);

    string email = u->email;
    uint64_t stringhash = stringhash64(&email, &pwcipher);
    reqs.add(new CommandSetMasterKey(this, newkey, (const byte*)&stringhash, sizeof(stringhash), NULL, pin));
    return API_OK;
}

error MegaClient::changePasswordV2(const char* password, const char* pin)
{
    vector<byte> clientRandomValue;
    vector<byte> encmasterkey;
    string hashedauthkey;
    string salt;

    fillCypheredAccountDataV2(password, clientRandomValue, encmasterkey, hashedauthkey, salt);

    // Pass the salt and apply to this->accountsalt if the command succeed to allow posterior checks of the password without getting it from the server
    reqs.add(new CommandSetMasterKey(this, encmasterkey.data(), reinterpret_cast<const byte*>(hashedauthkey.data()), SymmCipher::KEYLENGTH,
                                     clientRandomValue.data(), pin, &salt));
    return API_OK;
}

void MegaClient::fillCypheredAccountDataV2(const char* password, vector<byte>& clientRandomValue, vector<byte>& encmasterkey,
                                           string& hashedauthkey, string& salt)
{
    clientRandomValue.resize(SymmCipher::KEYLENGTH, 0);
    rng.genblock(clientRandomValue.data(), clientRandomValue.size());

    string buffer = "mega.nz";
    buffer.resize(200, 'P');
    buffer.append(reinterpret_cast<const char*>(clientRandomValue.data()), clientRandomValue.size());
    HashSHA256 hasher;
    hasher.add(reinterpret_cast<const byte*>(buffer.data()), unsigned(buffer.size()));
    hasher.get(&salt);

    vector<byte> derivedKey = deriveKey(password, salt, 2 * SymmCipher::KEYLENGTH);

    SymmCipher cipher;
    cipher.setkey(derivedKey.data());
    encmasterkey.resize(SymmCipher::KEYLENGTH, 0);
    cipher.ecb_encrypt(key.key, encmasterkey.data());

    const byte *authkey = derivedKey.data() + SymmCipher::KEYLENGTH;
    hasher.add(authkey, SymmCipher::KEYLENGTH);
    hasher.get(&hashedauthkey);
    hashedauthkey.resize(SymmCipher::KEYLENGTH);
}

vector<byte> MegaClient::deriveKey(const char* password, const string& salt, size_t derivedKeySize)
{
    vector<byte> derivedKey(derivedKeySize);
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;
    pbkdf2.DeriveKey(derivedKey.data(), derivedKey.size(), 0, (const byte*)password, strlen(password),
        (const byte*)salt.data(), salt.size(), 100000);

    return derivedKey;
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

void MegaClient::resumeephemeralPlusPlus(const std::string& session)
{
    ephemeralSessionPlusPlus = true;
    // E++ cannot resume sessions as regular ephemeral accounts. The acccount's creation
    // does not require a password, so the session token would be undecryptable. That's
    // the reason to use a regular session ID and perform a regular login, instead of
    // calling here resumeephemeral() directly.
    login(session);
}

void MegaClient::cancelsignup()
{
    reqs.add(new CommandCancelSignup(this));
}

void MegaClient::createephemeralPlusPlus()
{
    ephemeralSessionPlusPlus = true;
    createephemeral();
}

string MegaClient::sendsignuplink2(const char *email, const char *password, const char* name, int ctag)
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

    vector<byte> derivedKey = deriveKey(password, salt, 2 * SymmCipher::KEYLENGTH);

    byte encmasterkey[SymmCipher::KEYLENGTH];
    SymmCipher cipher;
    cipher.setkey(derivedKey.data());
    cipher.ecb_encrypt(key.key, encmasterkey);

    string hashedauthkey;
    const byte *authkey = derivedKey.data() + SymmCipher::KEYLENGTH;
    hasher.add(authkey, SymmCipher::KEYLENGTH);
    hasher.get(&hashedauthkey);
    hashedauthkey.resize(SymmCipher::KEYLENGTH);

    accountversion = 2;
    accountsalt = salt;
    reqs.add(new CommandSendSignupLink2(this, email, name, clientrandomvalue, encmasterkey, (byte*)hashedauthkey.data(), ctag ? ctag : reqtag));
    return string((const char*)derivedKey.data(), derivedKey.size());
}

void MegaClient::resendsignuplink2(const char *email, const char *name)
{
    reqs.add(new CommandSendSignupLink2(this, email, name));
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

    asymkey.genkeypair(rng, pubk, 2048);

    AsymmCipher::serializeintarray(pubk, AsymmCipher::PUBKEY, &pubks);
    AsymmCipher::serializeintarray(asymkey.getKey(), AsymmCipher::PRIVKEY, &privks);

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

    mKeyManager.setPostRegistration(true);
}

bool MegaClient::fetchsc(DbTable* sctable)
{
    uint32_t id;
    string data;
    std::shared_ptr<Node> n;
    User* u;
    PendingContactRequest* pcr;

    LOG_info << "Loading session from local cache";

    sctable->rewind();

    bool hasNext = sctable->next(&id, &data, &key);
    WAIT_CLASS::bumpds();
    fnstats.timeToFirstByte = Waiter::ds - fnstats.startTime;

    bool isDbUpgraded = false;      // true when legacy DB is migrated to NOD's DB schema

    std::map<NodeHandle, std::vector<std::shared_ptr<Node> >> delayedParents;
    while (hasNext)
    {
        switch (id & (DbTable::IDSPACING - 1))
        {
            case CACHEDSCSN:
                if (data.size() != sizeof cachedscsn)
                {
                    return false;
                }
                break;

            case CACHEDNODE:
                if ((n = mNodeManager.getNodeFromBlob(&data)))
                {
                    // When all nodes are loaded we force a commit
                   isDbUpgraded = true;

                   bool rootNode = n->type == ROOTNODE || n->type == RUBBISHNODE || n->type == VAULTNODE;
                   if (rootNode)
                   {
                       mNodeManager.setrootnode(n);
                   }
                   else if (n->parent == nullptr)
                   {
                       // nodes in 'statecache' are not ordered by parent-child
                       // -> we might load nodes whose parents are not loaded yet
                       delayedParents[n->parentHandle()].push_back(n);
                   }

                   sctable->del(id);                // delete record from old DB table 'statecache'
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
                    mappcr(pcr->id, unique_ptr<PendingContactRequest>(pcr));
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

            case CACHEDALERT:
            {
                if (!useralerts.unserializeAlert(&data, id))
                {
                    LOG_err << "Failed - user notification read error";
                    // don't break execution, just ignore it
                }
                break;
            }

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

            case CACHEDDBSTATE:
                {
                    mScDbStateRecord = ScDbStateRecord::unserialize(data);
                    mScDbStateRecord.dbid = id;
                    LOG_debug << "reloaded seqtag from db: " << mScDbStateRecord.seqTag;
                }
                break;
            case CACHEDSET:
            {
                if (!fetchscset(&data, id))
                {
                    return false;
                }
                break;
            }

            case CACHEDSETELEMENT:
            {
                if (!fetchscsetelement(&data, id))
                {
                    return false;
                }
                break;
            }
        }
        hasNext = sctable->next(&id, &data, &key);
    }

    LOG_debug << "Max dbId after resume session: " << id;

    if (isDbUpgraded)   // nodes loaded during migration from `statecache` to `nodes` table and kept in RAM
    {
        LOG_info << "Upgrading cache to NOD";
        // call setparent() for the nodes whose parent was not available upon unserialization
        for (auto& it : delayedParents)
        {
            std::shared_ptr<Node> parent = mNodeManager.getNodeByHandle(it.first);
            for (auto& child : it.second)
            {
                // In DB migration we have to calculate counters as they aren't calculated previously
                child->setparent(parent, true);
            }
        }

        // now that Users and PCRs are loaded, need to mergenewshare()
        mergenewshares(0, true);

        // finally write nodes in DB
        mNodeManager.dumpNodes();

        // and force commit, since old DB has been upgraded to new schema for NOD
        LOG_debug << "DB transaction COMMIT (sessionid: " << string(sessionid, sizeof(sessionid)) << ")";
        sctable->commit();
        sctable->begin();
    }
    else
    {
        // nodes are not loaded, proceed to load them only after Users and PCRs are loaded,
        // since Node::unserialize() will call mergenewshare(), and the latter requires
        // Users and PCRs to be available
        if (!mNodeManager.loadNodes())
        {
            return false;
        }

    }

    WAIT_CLASS::bumpds();
    fnstats.timeToLastByte = Waiter::ds - fnstats.startTime;

    // user alerts are restored from DB upon session resumption. No need to send the sc50 to catchup, it will
    // generate new alerts from action packets as usual, once the session is up and running
    useralerts.catchupdone = true;


    if (!mScDbStateRecord.seqTag.empty())
    {
        app->sequencetag_update(mScDbStateRecord.seqTag);
    }

    return true;
}


bool MegaClient::fetchStatusTable(DbTable* table)
{
    uint32_t id;
    string data;

    LOG_info << "Loading session state from local cache";

    table->rewind();

    bool hasNext = table->next(&id, &data, &key);
    while (hasNext)
    {
        switch (id & (15))
        {
            case CACHEDSTATUS:
            {
                auto status = CacheableStatus::unserialize(this, data);
                if (status)
                {
                    status->dbid = id;
                }
                else
                {
                    LOG_err << "Failed - status record read error";
                    return false;
                }
                break;
            }
        }
        hasNext = table->next(&id, &data, &key);
    }

    return true;
}

void MegaClient::purgeOrphanTransfers(bool remove)
{
    bool purgeOrphanTransfers = statecurrent;

    unsigned purgeCount = 0;
    unsigned notPurged = 0;

    for (int d = GET; d == GET || d == PUT; d += PUT - GET)
    {
        TransferDbCommitter committer(tctable);
        while (multi_cachedtransfers[d].size())
        {
            transfer_multimap::iterator it = multi_cachedtransfers[d].begin();
            Transfer *transfer = it->second;
            if (remove || (purgeOrphanTransfers && (m_time() - transfer->lastaccesstime) >= 172500))
            {
                if (purgeCount == 0)
                {
                    LOG_warn << "Purging orphan transfers";
                }
                purgeCount ++;
                transfer->finished = true;
            }
            else
            {
                notPurged ++;
            }

            // if the transfer is still in cachedtransfers, then the app never knew about it
            // during this running instance, so no need to call the app back here.
            //app->transfer_removed(transfer);

            delete transfer;
            multi_cachedtransfers[d].erase(it);
        }
    }

    if (purgeCount > 0 || notPurged > 0)
    {
        LOG_warn << "Purged " << purgeCount << " orphan transfers, " << notPurged << " non-referenced cached transfers remain";
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
    tctable.reset();
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
    else if (loggedIntoFolder())
    {
        dbname.resize(NODEHANDLE * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)&mFolderLink.mPublicHandle, NODEHANDLE, (char*)dbname.c_str()));
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

    tctable.reset(dbaccess->open(rng, *fsaccess, dbname, DB_OPEN_FLAG_RECYCLE | DB_OPEN_FLAG_TRANSACTED, [this](DBError error)
    {
        handleDbError(error);
    }));

    if (!tctable)
    {
        return;
    }

    uint32_t id;
    string data;
    Transfer* t;
    size_t cachedTransfersLoaded = 0;
    size_t cachedFilesLoaded = 0;

    LOG_info << "Loading transfers from local cache";
    tctable->rewind();
    {
        TransferDbCommitter committer(tctable); // needed in case of tctable->del()
        while (tctable->next(&id, &data, &tckey))
        {
            switch (id & 15)
            {
                case CACHEDTRANSFER:
                    if ((t = Transfer::unserialize(this, &data, multi_cachedtransfers)))
                    {
                        t->dbid = id;
                        if (t->priority > transferlist.currentpriority)
                        {
                            transferlist.currentpriority = t->priority;
                        }
                        cachedTransfersLoaded += 1;
                    }
                    else
                    {
                        tctable->del(id);
                        LOG_err << "Failed - transfer record read error, or duplicate";
                    }
                    break;
                case CACHEDFILE:
                    cachedfiles.push_back(data);
                    cachedfilesdbids.push_back(id);
                    cachedFilesLoaded += 1;
                    break;
            }
        }
    }
    LOG_debug << "Cached transfers loaded: " << cachedTransfersLoaded;
    LOG_debug << "Cached files loaded: " << cachedFilesLoaded;

    LOG_debug << "Cached transfer PUT count: " << multi_cachedtransfers[PUT].size();
    LOG_debug << "Cached transfer GET count: " << multi_cachedtransfers[GET].size();

    // if we are logged in but the filesystem is not current yet
    // postpone the resumption until the filesystem is updated
    if ((!sid.size() && !loggedIntoFolder()) || statecurrent)
    {
        TransferDbCommitter committer(tctable);
        for (unsigned int i = 0; i < cachedfiles.size(); i++)
        {
            direction_t type = NONE;
            File *file = app->file_resume(&cachedfiles.at(i), &type);
            if (!file || (type != GET && type != PUT))
            {
                tctable->del(cachedfilesdbids.at(i));
                continue;
            }
            file->dbid = cachedfilesdbids.at(i);
            if (!startxfer(type, file, committer, false, false, false, UseLocalVersioningFlag, nullptr, nextreqtag()))  // TODO: should we have serialized these flags and reused them here?
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
    else if (loggedIntoFolder())
    {
        dbname.resize(NODEHANDLE * 4 / 3 + 3);
        dbname.resize(Base64::btoa((const byte*)&mFolderLink.mPublicHandle, NODEHANDLE, (char*)dbname.c_str()));
    }
    else
    {
        dbname = loggedoutid ? loggedoutid : "default";
    }
    dbname.insert(0, "transfers_");

    tctable.reset(dbaccess->open(rng, *fsaccess, dbname, DB_OPEN_FLAG_RECYCLE | DB_OPEN_FLAG_TRANSACTED, [this](DBError error)
    {
        handleDbError(error);
    }));

    if (!tctable)
    {
        return;
    }

    purgeOrphanTransfers(true);
    closetc(true);
}

void MegaClient::handleDbError(DBError error)
{
    switch (error)
    {
        case DBError::DB_ERROR_FULL:
            fatalError(ErrorReason::REASON_ERROR_DB_FULL);
            break;
        case DBError::DB_ERROR_IO:
            fatalError(ErrorReason::REASON_ERROR_DB_IO);
            break;
        case DBError::DB_ERROR_INDEX_OVERFLOW:
            fatalError(ErrorReason::REASON_ERROR_DB_INDEX_OVERFLOW);
            break;
        default:
            fatalError(ErrorReason::REASON_ERROR_UNKNOWN);
            break;
    }
}

void MegaClient::fatalError(ErrorReason errorReason)
{
    if (mLastErrorDetected != errorReason)
    {
#ifdef ENABLE_SYNC
        syncs.disableSyncs(FAILURE_ACCESSING_PERSISTENT_STORAGE, false, true);
#endif

        std::string reason;
        switch (errorReason)
        {
            case ErrorReason::REASON_ERROR_DB_IO:
                sendevent(99467, "Writing in DB error", 0);
                reason = "Failed to write to database";
                break;
            case ErrorReason::REASON_ERROR_UNSERIALIZE_NODE:
                reason = "Failed to unserialize a node";
                sendevent(99468, "Failed to unserialize node", 0);
                break;
            case ErrorReason::REASON_ERROR_DB_FULL:
                reason = "Data base is full";
                break;
            case ErrorReason::REASON_ERROR_DB_INDEX_OVERFLOW:
                reason = "DB index overflow";
                sendevent(99471, "DB index overflow", 0);
                break;
            default:
                reason = "Unknown reason";
                break;
        }

        mLastErrorDetected = errorReason;
        app->notifyError(reason.c_str(), errorReason);
        // TODO: maybe it's worth a locallogout
    }
}

bool MegaClient::accountShouldBeReloadedOrRestarted() const
{
    return mLastErrorDetected != REASON_ERROR_NO_ERROR;
}

void MegaClient::fetchnodes(bool nocache, bool loadSyncs, bool forceLoadFromServers)
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
    else if (loggedIntoFolder())
    {
        fnstats.type = FetchNodesStats::TYPE_FOLDER;
    }

    opensctable();

    if (sctable && cachedscsn == UNDEF)
    {
        LOG_debug << "Cachedscsn is UNDEF so we will not load the account database (and we are truncating it, for clean operation)";
        sctable->truncate();
    }

    std::unique_lock<mutex> nodeTreeIsChanging(nodeTreeMutex);

    // only initial load from local cache
    if (!forceLoadFromServers &&
        (loggedin() == FULLACCOUNT || loggedIntoFolder() || loggedin() == EPHEMERALACCOUNTPLUSPLUS) &&
            !mNodeManager.hasCacheLoaded() && !ISUNDEF(cachedscsn) &&
            sctable && fetchsc(sctable.get()))
    {
        nodeTreeIsChanging.unlock(); // nodes loaded from db
        debugLogHeapUsage();

        // Copy the current tag (the one from fetch nodes) so we can capture it in the lambda below.
        // ensuring no new request happens in between
        auto fetchnodesTag = reqtag;
        auto onuserdataCompletion = [this, fetchnodesTag, loadSyncs](string*, string*, string*, error e) {

            restag = fetchnodesTag;

            // upon ug completion
            if (e != API_OK)
            {
                LOG_err << "Session load failed: unable to get user data";
                app->fetchnodes_result(API_EINTERNAL);
                return; //from completion function
            }

            WAIT_CLASS::bumpds();
            fnstats.mode = FetchNodesStats::MODE_DB;
            fnstats.cache = FetchNodesStats::API_NO_CACHE;
            fnstats.nodesCached = mNodeManager.getNodeCount();
            fnstats.timeToCached = Waiter::ds - fnstats.startTime;
            fnstats.timeToResult = fnstats.timeToCached;

            statecurrent = false;

            assert(sctable->inTransaction());
            pendingsccommit = false;

            // allow sc requests to start
            scsn.setScsn(cachedscsn);
            LOG_info << "Session loaded from local cache. SCSN: " << scsn.text();

            assert(mNodeManager.getNodeCount() > 0);   // sometimes this is not true; if you see it, please investigate why (before we alter the db)
            assert(!mNodeManager.getRootNodeFiles().isUndef() ||  // we should know this by now - if
                   (isClientType(ClientType::PASSWORD_MANAGER) && // not, why not, please investigate
                    !mNodeManager.getRootNodeVault().isUndef())); // (before we alter the db)

            if (loggedIntoWritableFolder())
            {
                // If logged into writable folder, we need the sharekey set in the root node
                // so as to include it in subsequent put nodes
                if (std::shared_ptr<Node> n = nodeByHandle(mNodeManager.getRootNodeFiles()))
                {
                    n->sharekey.reset(new SymmCipher(key)); //we use the "master key", in this case the secret share key
                }
            }

            enabletransferresumption();

#ifdef ENABLE_SYNC
            if (loadSyncs)
                syncs.loadSyncConfigsOnFetchnodesComplete(true);
#endif
            app->fetchnodes_result(API_OK);
            fetchnodesAlreadyCompletedThisSession = true;
            loadAuthrings();

            WAIT_CLASS::bumpds();
            fnstats.timeToSyncsResumed = Waiter::ds - fnstats.startTime;
        };


        if (!loggedIntoFolder())
        {
            getuserdata(0, onuserdataCompletion);
        }
        else
        {
            onuserdataCompletion(nullptr, nullptr, nullptr, API_OK);
        }
    }
    else if (!fetchingnodes)
    {
        fnstats.mode = FetchNodesStats::MODE_API;
        fnstats.cache = nocache ? FetchNodesStats::API_NO_CACHE : FetchNodesStats::API_CACHE;
        fetchingnodes = true;

        // delay resetting the sc channel state.
        // we wait until `f` is sent, because when `f` is queued, there may be
        // other commands queued ahead of it, and those may need sc responses in order
        // to fully complete, and so we can't reset those members at this time.

        if (!loggedIntoFolder())
        {
            // Copy the current tag so we can capture it in the lambda below.
            const auto fetchtag = reqtag;

            // Sanity clean before getting the User Data
            resetKeyring();
            // Make sure that our own user is defined
            finduser(me, 1);

            // we need this one to ensure we have the sync config read/write key for example
            getuserdata(0, [this, fetchtag, loadSyncs, nocache](string*, string*, string*, error e)
            {
                if (e != API_OK)
                {
                    LOG_err << "Pre-failing fetching nodes: unable not get user data";
                    restag = fetchtag;
                    fetchingnodes = false;
                    app->fetchnodes_result(e);
                    return;
                }

                if (loggedin() == FULLACCOUNT
                        || loggedin() == EPHEMERALACCOUNTPLUSPLUS)
                {
                    initializekeys();
                    loadAuthrings();
                }

                // FetchNodes procresult() needs some data from `ug` (or it may try to make new Sync User Attributes for example)
                // So only submit the request after `ug` completes, otherwise everything is interleaved
                const auto partialFetchRoot = isClientType(ClientType::PASSWORD_MANAGER) ? getPasswordManagerBase() : NodeHandle{};
                reqs.add(new CommandFetchNodes(this, fetchtag, nocache, loadSyncs, partialFetchRoot));
            });

            fetchtimezone();
        }
        else
        {
            actionpacketsCurrent = false;
            reqs.add(new CommandFetchNodes(this, reqtag, nocache, loadSyncs));
        }
    }
}

void MegaClient::resetScForFetchnodes()
{
    // reset all the sc channel state, prevent sending sc requests while fetchnodes is sent
    // we wait until this moment, because when `f` is queued, there may be
    // other commands queued ahead of it, and those may need sc responses in order
    // to fully complete, and so we can't reset these members at that time.

    pendingsccommit = false;

    // prevent the processing of previous sc requests
    pendingsc.reset();
    pendingscUserAlerts.reset();
    jsonsc.pos = NULL;
    scnotifyurl.clear();
    mPendingCatchUps = 0;
    mReceivingCatchUp = false;
    insca = false;
    insca_notlast = false;
    btsc.reset();

    // don't allow to start new sc requests yet
    scsn.clear();
}


void MegaClient::initializekeys()
{
    string prEd255, puEd255;    // keypair for Ed25519  --> MegaClient::signkey
    string prCu255, puCu255;    // keypair for Cu25519  --> MegaClient::chatkey
    string sigCu255, sigPubk;   // signatures for Cu25519 and RSA

    User *u = finduser(me);
    assert(u && "Own user not available while initializing keys");

    if (mKeyManager.generation())   // account has ^!keys already available
    {
        prEd255 = mKeyManager.privEd25519();
        prCu255 = mKeyManager.privCu25519();
    }
    else
    {
        const string *av = (u->isattrvalid(ATTR_KEYRING)) ? u->getattr(ATTR_KEYRING) : NULL;
        if (av)
        {
            unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(av, &key));
            if (tlvRecords)
            {
                tlvRecords->get(EdDSA::TLV_KEY, prEd255);
                tlvRecords->get(ECDH::TLV_KEY, prCu255);
            }
            else
            {
                LOG_warn << "Failed to decrypt keyring while initialization";
            }
        }
    }

    // get public keys and signatures
    puEd255 = (u->isattrvalid(ATTR_ED25519_PUBK)) ? *u->getattr(ATTR_ED25519_PUBK) : "";
    puCu255 = (u->isattrvalid(ATTR_CU25519_PUBK)) ? *u->getattr(ATTR_CU25519_PUBK) : "";
    sigCu255 = (u->isattrvalid(ATTR_SIG_CU255_PUBK)) ? *u->getattr(ATTR_SIG_CU255_PUBK) : "";
    sigPubk = (u->isattrvalid(ATTR_SIG_RSA_PUBK)) ? *u->getattr(ATTR_SIG_RSA_PUBK) : "";

    // Initialize private keys
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

    if (prCu255.size() == ECDH::PRIVATE_KEY_LENGTH)
    {
        chatkey = new ECDH(prCu255);
        if (!chatkey->initializationOK)
        {
            delete chatkey;
            chatkey = NULL;
            clearKeys();
            return;
        }
    }

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
        if ((puCu255.size() != ECDH::PUBLIC_KEY_LENGTH) || memcmp(puCu255.data(), chatkey->getPubKey(), ECDH::PUBLIC_KEY_LENGTH))
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

        if (loggedin() != EPHEMERALACCOUNTPLUSPLUS)   // E++ accounts don't have RSA keys
        {
            // Verify signature for RSA public key
            if (pubk.isvalid() && sigPubk.empty())
            {
                string pubkStr;
                std::string buf;
                userattr_map attrs;
                pubk.serializekeyforjs(pubkStr);
                signkey->signKey((unsigned char*)pubkStr.data(), pubkStr.size(), &sigPubk);
                buf.assign(sigPubk.data(), sigPubk.size());
                attrs[ATTR_SIG_RSA_PUBK] = buf;
                putua(&attrs, 0);
            }

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
        }

        if (mKeyManager.generation() && asymkey.isvalid(AsymmCipher::PRIVKEY) && !mKeyManager.getPrivRSA().size())
        {
            // Ephemeral++ accounts create ^!keys before having RSA keys
            LOG_debug << "Attaching private RSA key into ^!keys";
            std::string privRSA;
            asymkey.serializekey(&privRSA, AsymmCipher::PRIVKEY_SHORT);
            mKeyManager.commit(
            [this, privRSA]()
            {
                mKeyManager.setPrivRSA(privRSA);
            });
        }

        // if we reached this point, everything is OK
        LOG_info << "Keypairs and signatures loaded successfully";
        return;
    }
    else if (!signkey && !chatkey)       // THERE ARE NO KEYS
    {
        // Check completeness of keypairs
        if (puEd255.size() || puCu255.size() || sigCu255.size() || sigPubk.size()
                || (!pubk.isvalid() && loggedin() != EPHEMERALACCOUNTPLUSPLUS))  // E++ accounts don't have RSA keys
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

            prEd255 = string((char *)signkey->keySeed, EdDSA::SEED_KEY_LENGTH);
            prCu255 = string((char *)chatkey->getPrivKey(), ECDH::PRIVATE_KEY_LENGTH);

            if (!chatkey->initializationOK || !signkey->initializationOK)
            {
                LOG_err << "Initialization of keys Cu25519 and/or Ed25519 failed";
                clearKeys();
                delete signkey;
                delete chatkey;
                return;
            }

            // store keys into user attributes (skipping the procresult() <-- reqtag=0)

            // prepare map of attributes to set: ^!keys|*!keyring + puEd255 + puCu255 + sigPubk + sigCu255
            userattr_map attrs;

            // private keys
            string buf;

            // save private keys into the ^!keys attribute
            assert(mKeyManager.generation() == 0);  // creating them, no init() done yet
            mKeyManager.setKey(key);
            mKeyManager.init(prEd255, prCu255, mPrivKey);

            // We are initializing the keys, so it's safe to assume that authrings are empty
            mAuthRings.emplace(ATTR_AUTHRING, AuthRing(ATTR_AUTHRING, TLVstore()));
            mAuthRings.emplace(ATTR_AUTHCU255, AuthRing(ATTR_AUTHCU255, TLVstore()));

            // Not using mKeyManager::commit() here to set ^!keys along with the other attributes.
            // Since the account is being initialized, putua should not fail.
            buf = mKeyManager.toKeysContainer();
            attrs[ATTR_KEYS] = buf;

            // save private keys into the *!keyring attribute (for backwards compatibility, so legacy
            // clients can retrieve chat and signing key for accounts created with ^!keys support)
            TLVstore tlvRecords;
            tlvRecords.set(EdDSA::TLV_KEY, string((const char*)signkey->keySeed, EdDSA::SEED_KEY_LENGTH));
            tlvRecords.set(ECDH::TLV_KEY, string((const char*)chatkey->getPrivKey(), ECDH::PRIVATE_KEY_LENGTH));
            unique_ptr<string> tlvContainer(tlvRecords.tlvRecordsToContainer(rng, &key));

            buf.assign(tlvContainer->data(), tlvContainer->size());
            attrs[ATTR_KEYRING] = buf;

            // create signatures of public RSA and Cu25519 keys
            if (loggedin() != EPHEMERALACCOUNTPLUSPLUS) // Ephemeral++ don't have RSA keys until confirmation, but need chat and signing key
            {
                // prepare signatures
                string pubkStr;
                pubk.serializekeyforjs(pubkStr);
                signkey->signKey((unsigned char*)pubkStr.data(), pubkStr.size(), &sigPubk);
            }
            signkey->signKey(chatkey->getPubKey(), ECDH::PUBLIC_KEY_LENGTH, &sigCu255);

            buf.assign((const char *) signkey->pubKey, EdDSA::PUBLIC_KEY_LENGTH);
            attrs[ATTR_ED25519_PUBK] = buf;

            buf.assign((const char *) chatkey->getPubKey(), ECDH::PUBLIC_KEY_LENGTH);
            attrs[ATTR_CU25519_PUBK] = buf;

            if (loggedin() != EPHEMERALACCOUNTPLUSPLUS) // Ephemeral++ don't have RSA keys until confirmation, but need chat and signing key
            {
                buf.assign(sigPubk.data(), sigPubk.size());
                attrs[ATTR_SIG_RSA_PUBK] = buf;
            }

            buf.assign(sigCu255.data(), sigCu255.size());
            attrs[ATTR_SIG_CU255_PUBK] = buf;

            putua(&attrs, 0, [this](Error e)
            {
                if (e != API_OK)
                {
                    LOG_err << "Error attaching keys: " << e;
                    sendevent(99419, "Error Attaching keys", 0);
                    clearKeys();
                    resetKeyring();
                }
            });

            delete chatkey;
            delete signkey; // MegaClient::signkey & chatkey are created on putua::procresult()

            LOG_info << "Creating new keypairs and signatures";
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
    if (User* ownUser = finduser(me))
    {
        // if KeyManager is ready, authrings are already retrieved by getuserdata (from ^!keys attribute)
        if (!mKeyManager.generation())
        {
            std::set<attr_t> attrs { ATTR_AUTHRING, ATTR_AUTHCU255 };
            for (auto at : attrs)
            {
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
                        LOG_err << User::attr2string(at) << " not available: found in cache, but out of date.";
                    }
                }
                else
                {
                    // It does not exist yet in the account. Will be created upon retrieval of contact keys.
                    LOG_warn << User::attr2string(at) << " not found in cache. Setting an empty one.";
                    mAuthRings.emplace(at, AuthRing(at, TLVstore()));
                }
            }

            fetchContactsKeys();

        }   // --> end if(!mKeyManager.generation())
    }
}

void MegaClient::fetchContactsKeys()
{
    assert(mAuthRings.size() == 2);

    // Populate mPendingContactKeys first, because if it's done just before fetchContactKeys,
    // it could finish synchronously and deactivate mAuthRingsTemp too early
    mPendingContactKeys.clear();
    auto &pendingEdKeys = mPendingContactKeys[ATTR_AUTHRING];
    auto &pendingCuKeys = mPendingContactKeys[ATTR_AUTHCU255];
    for (auto &it : users)
    {
        User *user = &it.second;
        if (user->userhandle != me)
        {
            pendingEdKeys.insert(user->userhandle);
            pendingCuKeys.insert(user->userhandle);
        }
    }

    if (pendingEdKeys.empty())
    {
        LOG_debug << "No need to fetch contact keys (no contacts)";
        mPendingContactKeys.clear();
        return;
    }

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
    // call trackKey() in case the key is in cache
    // otherwise, send getua() to server, CommandGetUA::procresult() will call trackKey()
    attr_t attrType = ATTR_ED25519_PUBK;
    if (!user->isattrvalid(attrType))
    {
        getua(user, attrType, 0);

        // if Ed25519 is not in cache, better to ensure that Ed25519 is tracked before Cu25519
        user->invalidateattr(ATTR_CU25519_PUBK);
    }
    else trackKey(attrType, user->userhandle, *user->getattr(attrType));

    attrType = ATTR_CU25519_PUBK;
    if (!user->isattrvalid(attrType)) getua(user, attrType, 0);
    else trackKey(attrType, user->userhandle, *user->getattr(attrType));

    // TODO: remove obsolete retrieval of public RSA keys and its signatures
    // (authrings for RSA are deprecated)
    if (!user->pubk.isvalid())
    {
        int creqtag = reqtag;
        reqtag = 0;
        getpubkey(user->uid.c_str());
        reqtag = creqtag;
    }
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
        aux = std::make_unique<AuthRing>(it->second);    // make a copy, once saved in API, it is updated
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

                // flush the temporal authring if needed
                if (temporalAuthring)
                {
                    updateAuthring(authring, authringType, temporalAuthring, uh);
                }
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
        assert(user->getattr(ATTR_ED25519_PUBK) != nullptr); // User's public Ed25519 should be in cache already

        attr_t attrType = AuthRing::authringTypeToSignatureType(authringType);
        const string* signature = user->getattr(attrType);
        if (signature)
        {
            trackSignature(attrType, uh, *signature);
        }
        else
        {
            getua(user, attrType, 0); // in CommandGetUA::procresult(), we check signature actually matches
        }
    }
    else // then it's the authring for public Ed25519 key
    {
        if (!keyTracked)
        {
            LOG_debug << "Adding public key to " << User::attr2string(authringType) << " as seen for user " << uid;

            // tracking has changed --> persist authring
            authring->add(uh, keyFingerprint, AUTH_METHOD_SEEN);
        }

        error e = updateAuthring(authring, authringType, temporalAuthring, uh);
        if (e)
        {
            return e;
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
        aux = std::make_unique<AuthRing>(it->second);    // make a copy, once saved in API, it is updated
        authring = aux.get();
    }

    const string *pubKey;
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

                app->key_modified(uh, signatureType == ATTR_SIG_CU255_PUBK ? ATTR_CU25519_PUBK : ATTR_UNKNOWN);
                sendevent(99451, "Key modification detected");

                return API_EKEY;
            }
            else if (authring->getAuthMethod(uh) != AUTH_METHOD_SIGNATURE)
            {
                LOG_debug << "Updating authentication method for user " << uid << " to signature verified";

                authring->update(uh, AUTH_METHOD_SIGNATURE);
            }
        }
        else
        {
            LOG_debug << "Adding public key to " << User::attr2string(authringType) << " as signature verified for user " << uid;

            authring->add(uh, keyFingerprint, AUTH_METHOD_SIGNATURE);
        }

        error e = updateAuthring(authring, authringType, temporalAuthring, uh);
        if (e)
        {
            return e;
        }
    }
    else
    {
        LOG_err << "Failed to verify signature of public key in " << User::attr2string(authringType) << " for user " << uid << ": signature mismatch";

        app->key_modified(uh, signatureType);
        sendevent(99452, "Signature mismatch for public key");

        // flush the temporal authring if needed
        if (temporalAuthring)
        {
            updateAuthring(authring, authringType, temporalAuthring, uh);
        }
        return API_EKEY;
    }

    return API_OK;
}

error MegaClient::updateAuthring(AuthRing *authring, attr_t authringType, bool temporalAuthring, handle updateduh)
{
    // if checking authrings for all contacts, accumulate updates for all contacts first
    bool finished = true;
    if (temporalAuthring)
    {
        auto it = mPendingContactKeys.find(authringType);
        assert(it != mPendingContactKeys.end());

        if (it != mPendingContactKeys.end())
        {
            it->second.erase(updateduh);
            if (it->second.size())
            {
                // initialization not finished yet
                finished = false;
            }
            else
            {
                mPendingContactKeys.erase(it);
                LOG_debug << "Authring " << User::attr2string(authringType) << " initialization finished";
            }
        }
    }

    if (finished)
    {
        if (authring->needsUpdate())
        {
            std::string serializedAuthring = authring->serializeForJS();
            if (mKeyManager.generation())
            {
                LOG_debug << "Updating " << User::attr2string(authringType) << " in ^!keys";
                mKeyManager.commit(
                [this, authringType, serializedAuthring]()
                {
                    // Changes to apply in the commit
                    if (authringType == ATTR_AUTHRING)
                    {
                        mKeyManager.setAuthRing(serializedAuthring);
                    }
                    else if (authringType == ATTR_AUTHCU255)
                    {
                        mKeyManager.setAuthCU255(serializedAuthring);
                    }
                });
            }
            else
            {
                // Account not migrated yet. Apply changes synchronously in the local authring to have it ready for the migration
                auto it = mAuthRings.find(authringType);
                if (it == mAuthRings.end())
                {
                    LOG_warn << "Failed to track signature of public key in " << User::attr2string(authringType) << " for user " << uid
                                << ": account not migrated and authring not available";
                    assert(false);
                    return API_ETEMPUNAVAIL;
                }
                it->second = *authring;
            }
        }

        mAuthRingsTemp.erase(authringType);
    }

    return API_OK;
}

error MegaClient::verifyCredentials(handle uh, std::function<void (Error)> completion)
{
    if (!mKeyManager.generation())
    {
        LOG_err << "Account not upgraded yet";
        return API_EINCOMPLETE;
    }

    Base64Str<MegaClient::USERHANDLE> uid(uh);
    auto itEd = mAuthRings.find(ATTR_AUTHRING);
    bool hasEdAuthring = itEd != mAuthRings.end();
    auto itCu = mAuthRings.find(ATTR_AUTHCU255);
    bool hasCuAuthring = itCu != mAuthRings.end();
    if (!hasEdAuthring || !hasCuAuthring)
    {
        LOG_warn << "Failed to verify public Ed25519 key for user " << uid << ": authring(s) not available";
        return API_ETEMPUNAVAIL;
    }

    if (itCu->second.getAuthMethod(uh) != AUTH_METHOD_SIGNATURE)
    {
        LOG_err << "Failed to verify credentials for user " << uid << ": signature of Cu25519 public key is not verified";
        assert(false);

        // Let's try to authenticate Cu25519 for this user
        // because its verification is required to promote pending sharesUser *user = finduser(uh);
        User *user = finduser(uh);
        if (user)
        {
            attr_t attrType = ATTR_CU25519_PUBK;
            if (!user->isattrvalid(attrType)) getua(user, attrType, 0);
            else trackKey(attrType, user->userhandle, *user->getattr(attrType));
        }
        return API_EINTERNAL;
    }

    AuthMethod authMethod = itEd->second.getAuthMethod(uh);
    switch (authMethod)
    {
    case AUTH_METHOD_SEEN:
        LOG_debug << "Updating authentication method of Ed25519 public key for user " << uid << " from seen to signature verified";
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
            LOG_warn << "Adding authentication method of Ed25519 public key for user " << uid << ": key is not tracked yet";
        }
        else
        {
            LOG_err << "Failed to verify credentials for user " << uid << ": key not tracked and not available";
            return API_ETEMPUNAVAIL;
        }
        break;
    }
    }

    mKeyManager.commit(
    [this, uh, uid]()
    {
        // Changes to apply in the commit
        auto itEd = mAuthRings.find(ATTR_AUTHRING);
        auto itCu = mAuthRings.find(ATTR_AUTHCU255);
        if (itEd == mAuthRings.end() || itCu == mAuthRings.end())
        {
            LOG_warn << "Failed to verify public Ed25519 key for user " << uid
                     << ": authring(s) not available during commit";
            return;
        }

        if (itCu->second.getAuthMethod(uh) != AUTH_METHOD_SIGNATURE)
        {
            LOG_err << "Failed to verify credentials for user " << uid
                    << ": signature of Cu25519 public key is not verified during commit";
            return;
        }

        AuthRing authring = itEd->second; // copy, do not modify yet the cached authring
        AuthMethod authMethod = authring.getAuthMethod(uh);
        switch (authMethod)
        {
        case AUTH_METHOD_SEEN:
            authring.update(uh, AUTH_METHOD_FINGERPRINT);
            break;
        case AUTH_METHOD_UNKNOWN:
        {
            User *user = finduser(uh);
            const string *pubKey = user ? user->getattr(ATTR_ED25519_PUBK) : nullptr;
            if (pubKey)
            {
                string keyFingerprint = AuthRing::fingerprint(*pubKey);
                LOG_warn << "Adding authentication method of Ed25519 public key for user " << uid
                         << ": key is not tracked yet during commit";
                authring.add(uh, keyFingerprint, AUTH_METHOD_FINGERPRINT);
                break;
            }
            else
            {
                LOG_err << "Failed to verify credentials for user " << uid
                        << ": key not tracked and not available during commit";
                return;
            }
            break;
        }
        default:
            LOG_err << "Failed to verify credentials for user " << uid
                    << " unexpected authMethod (" << authMethod << ") during commit";
            return;
        }

        std::string serializedAuthring = authring.serializeForJS();
        mKeyManager.setAuthRing(serializedAuthring);
    },
    [completion](error e)
    {
        completion(e);
    });

    return API_OK;
}

error MegaClient::resetCredentials(handle uh, std::function<void(Error)> completion)
{
    if (!mKeyManager.generation())
    {
        LOG_err << "Account not upgraded yet";
        return API_EINCOMPLETE;
    }

    Base64Str<MegaClient::USERHANDLE> uid(uh);
    auto it = mAuthRings.find(ATTR_AUTHRING);
    if (it == mAuthRings.end())
    {
        LOG_warn << "Failed to reset credentials for user " << uid << ": authring not available";
        return API_ETEMPUNAVAIL;
    }

    AuthMethod authMethod = it->second.getAuthMethod(uh);
    if (authMethod == AUTH_METHOD_SEEN)
    {
        LOG_warn << "Failed to reset credentials for user " << uid << ": Ed25519 key is not verified by fingerprint";
        return API_EARGS;
    }
    else if (authMethod == AUTH_METHOD_UNKNOWN)
    {
        LOG_warn << "Failed to reset credentials for user " << uid << ": Ed25519 key is not tracked yet";
        return API_ENOENT;
    }
    assert(authMethod == AUTH_METHOD_FINGERPRINT); // Ed25519 authring cannot be at AUTH_METHOD_SIGNATURE
    LOG_debug << "Reseting credentials for user " << uid << "...";

    mKeyManager.commit(
    [this, uh, uid]()
    {
        auto it = mAuthRings.find(ATTR_AUTHRING);
        if (it == mAuthRings.end())
        {
            LOG_warn << "Failed to reset credentials for user " << uid
                     << ": authring not available during commit";
            return;
        }

        AuthRing authring = it->second; // copy, do not update cached authring yet
        AuthMethod authMethod = authring.getAuthMethod(uh);
        if (authMethod != AUTH_METHOD_FINGERPRINT)
        {
            LOG_warn << "Failed to reset credentials for user " << uid
                     << " unexpected authMethod (" << authMethod << ") during commit";
            return;
        }

        authring.update(uh, AUTH_METHOD_SEEN);
        string serializedAuthring = authring.serializeForJS();

        // Changes to apply in the commit
        mKeyManager.setAuthRing(serializedAuthring);
    },
    [completion](error e)
    {
        if (completion)
        {
            completion(e);
        }
        return;
    });

    return API_OK;
}

bool MegaClient::areCredentialsVerified(handle uh)
{
    if (uh == me)
    {
        return false;
    }

    AuthRingsMap::const_iterator itCu = mAuthRings.find(ATTR_AUTHCU255);
    bool cuAuthringFound = itCu != mAuthRings.end();
    if (!cuAuthringFound || !itCu->second.areCredentialsVerified(uh))
    {
        LOG_err << "Cu25519 for " << toHandle(uh) << ": " << (!cuAuthringFound ? "authring missing" : "signature not verified");
        return false;
    }

    AuthRingsMap::const_iterator it = mAuthRings.find(ATTR_AUTHRING);
    bool edAuthringFound = it != mAuthRings.end();
    if (!edAuthringFound || !it->second.areCredentialsVerified(uh))
    {
        if (!edAuthringFound) LOG_err << "Ed25519 for " << toHandle(uh) << ": " << "authring missing";
        return false;
    }

    return true;
}

void MegaClient::purgenodesusersabortsc(bool keepOwnUser)
{
    // this function's purpose is to remove from RAM everything that we would populate in FetchNodes.

    app->clearing();

    while (!hdrns.empty())
    {
        delete hdrns.begin()->second;
    }

    mNodeManager.cleanNodes();

#ifdef ENABLE_SYNC
    pendingDebris.clear();
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
    mNewKeyRepository.clear();
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
            ++it;
            dodiscarduser(u, true);
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
    if (!keepOwnUser) // Force to remove all elements from user maps
    {
        users.clear();
        uhindex.clear();
        umindex.clear();
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
void MegaClient::pread(Node* n, m_off_t offset, m_off_t count, void* appdata)
{
    queueread(n->nodehandle, true, n->nodecipher(),
              MemAccess::get<int64_t>((const char*)n->nodekey().data() + SymmCipher::KEYLENGTH),
              offset, count, appdata);
}

// request direct read by exported handle / key
void MegaClient::pread(handle ph, SymmCipher* key, int64_t ctriv, m_off_t offset, m_off_t count, void* appdata, bool isforeign, const char *privauth, const char *pubauth, const char *cauth)
{
    queueread(ph, isforeign, key, ctriv, offset, count, appdata, privauth, pubauth, cauth);
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

void MegaClient::removeAppData(void* t)
{
    for (auto it = hdrns.begin(); it != hdrns.end(); ++it)
    {
        dr_list& dreads = it->second->reads;
        for(auto it2 = dreads.begin(); it2 != dreads.end(); ++it2)
        {
            DirectRead* dr = *it2;
            if (dr && dr->appdata == t)
            {
                dr->appdata = nullptr;
            }
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

error MegaClient::addtimer(TimerWithBackoff *twb)
{
    bttimers.push_back(twb);
    return API_OK;
}

#ifdef ENABLE_SYNC

error MegaClient::isnodesyncable(std::shared_ptr<Node> remotenode, bool *isinshare, SyncError *syncError)
{
    // cannot sync files, rubbish bins or vault
    if (remotenode->type != FOLDERNODE && remotenode->type != ROOTNODE)
    {
        if(syncError)
        {
            *syncError = INVALID_REMOTE_TYPE;
        }
        return API_EACCESS;
    }

    // any active syncs below?

    auto activeConfigs = syncs.getConfigs(true);
    for (auto& sc : activeConfigs)
    {
        if (std::shared_ptr<Node> syncRoot = nodeByHandle(sc.mRemoteNode))
        {
            // We cannot use this function re-test an existing sync
            // This is just for testing whether we can create a new one with `remotenode`
            bool above = remotenode->isbelow(syncRoot.get());
            bool below = syncRoot->isbelow(remotenode.get());
            if (above && below)
            {
                if (syncError) *syncError = ACTIVE_SYNC_SAME_PATH;
                return API_EEXIST;
            }
            else if (below)
            {
                if (syncError) *syncError = ACTIVE_SYNC_BELOW_PATH;
                return API_EEXIST;
            }
            else if (above)
            {
                if (syncError) *syncError = ACTIVE_SYNC_ABOVE_PATH;
                return API_EEXIST;
            }
        }
    }

    // any active syncs above? or node within //bin or inside non full access inshare
    std::shared_ptr<Node> n = remotenode;
    bool inshare = false;

    do {

        if (n->inshare && !inshare)
        {
            // we need FULL access to sync
            // FIXME: allow downsyncing from RDONLY and limited syncing to RDWR shares
            if (n->inshare->access != FULL)
            {
                if(syncError)
                {
                    *syncError = SHARE_NON_FULL_ACCESS;
                }
                return API_EACCESS;
            }

            inshare = true;
        }

        if (n->nodeHandle() == mNodeManager.getRootNodeRubbish())
        {
            if(syncError)
            {
                *syncError = REMOTE_NODE_INSIDE_RUBBISH;
            }
            return API_EACCESS;
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
                                if(syncError)
                                {
                                    *syncError = SHARE_NON_FULL_ACCESS;
                                }
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
}

error MegaClient::isLocalPathSyncable(const LocalPath& newPath, handle excludeBackupId, SyncError *syncError)
{
    if (newPath.empty())
    {
        if (syncError)
        {
            *syncError = LOCAL_PATH_UNAVAILABLE;
        }
        return API_EARGS;
    }

    LocalPath newLocallyEncodedPath = newPath;
    LocalPath newLocallyEncodedAbsolutePath;
    fsaccess->expanselocalpath(newLocallyEncodedPath, newLocallyEncodedAbsolutePath);

    error e = API_OK;
    for (auto& config : syncs.getConfigs(false))
    {
        // (when adding a new config, excludeBackupId=UNDEF, so it doesn't match any existing config)
        if (config.mBackupId != excludeBackupId)
        {
            LocalPath otherLocallyEncodedPath = config.getLocalPath();
            LocalPath otherLocallyEncodedAbsolutePath;
            fsaccess->expanselocalpath(otherLocallyEncodedPath, otherLocallyEncodedAbsolutePath);

            if (config.getEnabled() && !config.mError &&
                    ( newLocallyEncodedAbsolutePath.isContainingPathOf(otherLocallyEncodedAbsolutePath)
                      || otherLocallyEncodedAbsolutePath.isContainingPathOf(newLocallyEncodedAbsolutePath)
                    ) )
            {
                LOG_warn << "Path already associated with a sync: "
                         << newLocallyEncodedAbsolutePath
                         << " "
                         << toHandle(config.mBackupId)
                         << " "
                         << otherLocallyEncodedAbsolutePath;

                if (syncError)
                {
                    *syncError = LOCAL_PATH_SYNC_COLLISION;
                }
                e = API_EARGS;
            }
        }
    }

    return e;
}

// check sync path, add sync if folder
// disallow nested syncs (there is only one LocalNode pointer per node)
// (FIXME: perform the same check for local paths!)
error MegaClient::checkSyncConfig(SyncConfig& syncConfig, LocalPath& rootpath, std::unique_ptr<FileAccess>& openedLocalFolder, bool& inshare, bool& isnetwork)
{
    // Checking for conditions where we would not even add the sync config
    // Though, if the config is already present but now invalid for one of these reasons, we don't remove it

    syncConfig.mEnabled = true;
    syncConfig.mError = NO_SYNC_ERROR;
    syncConfig.mWarning = NO_SYNC_WARNING;

    // If failed to unserialize nodes from DB, syncs get disabled -> prevent re-enable them
    // until the account is reloaded (or the app restarts)
    if (accountShouldBeReloadedOrRestarted())
    {
        LOG_warn << "Cannot re-enable sync until account's reload (unserialize errors)";
        syncConfig.mError = FAILURE_ACCESSING_PERSISTENT_STORAGE;
        syncConfig.mEnabled = false;
        return API_EINTERNAL;
    }

    std::shared_ptr<Node> remotenode = nodeByHandle(syncConfig.mRemoteNode);
    inshare = false;
    if (!remotenode)
    {
        LOG_warn << "Sync root does not exist in the cloud: "
                 << syncConfig.getLocalPath()
                 << ": "
                 << LOG_NODEHANDLE(syncConfig.mRemoteNode);

        syncConfig.mError = REMOTE_NODE_NOT_FOUND;
        syncConfig.mEnabled = false;
        return API_ENOENT;
    }

    if (error e = isnodesyncable(remotenode, &inshare, &syncConfig.mError))
    {
        LOG_debug << "Node is not syncable for sync add";
        syncConfig.mEnabled = false;
        return e;
    }

    // Has the user requested the sync operate in periodic scanning mode?
    if (syncConfig.mChangeDetectionMethod == CDM_PERIODIC_SCANNING)
    {
        // Have they specfied a valid scan interval?
        if (!syncConfig.mScanIntervalSec)
        {
            LOG_debug << "No scan interval for periodic sync add";

            syncConfig.mEnabled = false;
            syncConfig.mError = INVALID_SCAN_INTERVAL;

            return API_EARGS;
        }
    }

    if (syncs.mBackupRestrictionsEnabled && syncConfig.isBackup() && remotenode->firstancestor()->nodeHandle() != mNodeManager.getRootNodeVault())
    {
        syncConfig.mError = INVALID_REMOTE_TYPE;
        syncConfig.mEnabled = false;
        return API_EARGS;
    }

    if (syncConfig.isExternal())
    {
        // Currently only possible for backup syncs.
        if (!syncConfig.isBackup())
        {
            LOG_warn << "Only Backups can be external";
            return API_EARGS;
        }

        const auto& drivePath = syncConfig.mExternalDrivePath;
        const auto& sourcePath = syncConfig.mLocalPath;

        // Source must be on the drive.
        if (!drivePath.isContainingPathOf(sourcePath))
        {
            LOG_debug << "Drive path inconsistent for sync add";
            syncConfig.mEnabled = false;
            syncConfig.mError = BACKUP_SOURCE_NOT_BELOW_DRIVE;

            return API_EARGS;
        }
    }

    rootpath = syncConfig.getLocalPath();
    rootpath.trimNonDriveTrailingSeparator();

    // Make sure the user isn't trying to sync a FUSE mount.
    if (!mFuseService.syncable(rootpath))
    {
        LOG_warn << "Trying to sync above (below) a FUSE mount: "
                 << rootpath;

        syncConfig.mError = LOCAL_PATH_MOUNTED;
        syncConfig.mEnabled = false;

        return API_EFAILED;
    }

    isnetwork = false;
    if (!fsaccess->issyncsupported(rootpath, isnetwork, syncConfig.mError, syncConfig.mWarning))
    {
        LOG_warn << "Unsupported filesystem";
        syncConfig.mError = UNSUPPORTED_FILE_SYSTEM;
        syncConfig.mEnabled = false;
        return API_EFAILED;
    }

    openedLocalFolder = fsaccess->newfileaccess();
    if (openedLocalFolder->fopen(rootpath, true, false, FSLogging::logOnError, nullptr, true))
    {
        if (openedLocalFolder->type == FOLDERNODE)
        {
            LOG_debug << "Adding sync: " << syncConfig.getLocalPath() << " vs " << remotenode->displaypath();

            // Note localpath is stored as utf8 in syncconfig as passed from the apps!
            // Note: we might want to have it expansed to store the full canonical path.
            // so that the app does not need to carry that burden.
            // Although it might not be required given the following test does expands the configured
            // paths to use canonical paths when checking for path collisions:
            error e = isLocalPathSyncable(syncConfig.getLocalPath(), syncConfig.mBackupId, &syncConfig.mError);
            if (e)
            {
                LOG_warn << "Local path not syncable: " << syncConfig.getLocalPath();

                if (syncConfig.mError == NO_SYNC_ERROR)
                {
                    syncConfig.mError = LOCAL_PATH_UNAVAILABLE;
                }
                syncConfig.mEnabled = false;
                return e;  // eg. API_EARGS
            }
        }
        else
        {
            LOG_warn << "Cannot sync non-folder";
            syncConfig.mError = INVALID_LOCAL_TYPE;
            syncConfig.mEnabled = false;
            return API_EACCESS;    // cannot sync individual files
        }
    }
    else
    {
        LOG_warn << "Cannot open rootpath for sync: " << rootpath;
        syncConfig.mError = openedLocalFolder->retry ? LOCAL_PATH_TEMPORARY_UNAVAILABLE : LOCAL_PATH_UNAVAILABLE;
        syncConfig.mEnabled = false;
        return openedLocalFolder->retry ? API_ETEMPUNAVAIL : API_ENOENT;
    }

    //check we are not in any blocking situation
    using CType = CacheableStatus::Type;
    bool overStorage = mCachedStatus.lookup(CType::STATUS_STORAGE, STORAGE_UNKNOWN) >= STORAGE_RED;
    bool businessExpired = mCachedStatus.lookup(CType::STATUS_BUSINESS, BIZ_STATUS_UNKNOWN) == BIZ_STATUS_EXPIRED;
    bool blocked = mCachedStatus.lookup(CType::STATUS_BLOCKED, 0) == 1;

    // the order is important here: a user needs to resolve blocked in order to resolve storage
    if (overStorage)
    {
        LOG_debug << "Overstorage for sync add";
        syncConfig.mError = STORAGE_OVERQUOTA;
        syncConfig.mEnabled = false;
        return API_EFAILED;
    }
    else if (businessExpired)
    {
        LOG_debug << "Account expired for sync add";
        syncConfig.mError = ACCOUNT_EXPIRED;
        syncConfig.mEnabled = false;
        return API_EFAILED;
    }
    else if (blocked)
    {
        LOG_debug << "Account blocked for sync add";
        syncConfig.mError = ACCOUNT_BLOCKED;
        syncConfig.mEnabled = false;
        return API_EFAILED;
    }

    return API_OK;
}

void MegaClient::copySyncConfig(const SyncConfig& config, std::function<void(handle, error)> completion)
{
    string deviceIdHash = getDeviceidHash();
    BackupInfoSync info(config, deviceIdHash, UNDEF, BackupInfoSync::getSyncState(config, xferpaused[GET], xferpaused[PUT]));

    reqs.add( new CommandBackupPut(this, info,
                                  [this, config, completion](Error e, handle backupId) {
        if (!e)
        {
            if (ISUNDEF(backupId))
            {
                e = API_EINTERNAL;
            }
            else
            {
                auto configWithId = config;
                configWithId.mBackupId = backupId;
                e = syncs.syncConfigStoreAdd(configWithId);
            }
        }

        completion(backupId, e);
    }));
}

void MegaClient::importSyncConfigs(const char* configs, std::function<void(error)> completion)
{
    // Kick off the import.
    syncs.importSyncConfigs(configs, std::move(completion));
}

void MegaClient::addsync(SyncConfig&& config, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath)
{
    assert(completion);
    assert(config.mExternalDrivePath.empty() || config.mExternalDrivePath.isAbsolute());
    assert(config.mLocalPath.isAbsolute());

    LocalPath rootpath;
    std::unique_ptr<FileAccess> openedLocalFolder;
    bool inshare, isnetwork;
    error e = checkSyncConfig(config, rootpath, openedLocalFolder, inshare, isnetwork);

    if (e)
    {
        // the cause is already logged in checkSyncConfig
        completion(e, config.mError, UNDEF);
        return;
    }

    string deviceIdHash = getDeviceidHash();
    if (deviceIdHash.empty())
    {
        completion(API_EARGS, UNABLE_TO_RETRIEVE_DEVICE_ID, UNDEF);
        return;
    }

    // Are we adding an external backup?
    handle driveId = UNDEF;
    if (config.isExternal())
    {
        const string& p = config.mExternalDrivePath.toPath(false);
        e = readDriveId(*fsaccess, p.c_str(), driveId);
        if (e != API_OK)
        {
            LOG_debug << "readDriveId failed for sync add";
            completion(e, config.mError, UNDEF);
            return;
        }
    }

    // Add the sync.
    BackupInfoSync info(config, deviceIdHash, driveId, BackupInfoSync::getSyncState(config, xferpaused[GET], xferpaused[PUT]));

    reqs.add(new CommandBackupPut(this, info,
        [this, config, completion, logname, excludedPath](Error e, handle backupId) mutable {
        if (ISUNDEF(backupId) && !e)
        {
            LOG_debug << "Request for backupId failed for sync add";
            e = API_EFAILED;
        }

        if (e)
        {
            LOG_warn << "Failed to register heartbeat record for new sync. Error: " << int(e);
            completion(e, config.mError, backupId);
        }
        else
        {
            // if we got this far, the syncConfig is kept (in db and in memory)
            config.mBackupId = backupId;

            syncs.appendNewSync(config, true, completion, true, logname, excludedPath);
        }
    }));
}


void MegaClient::preparebackup(SyncConfig sc, std::function<void(Error, SyncConfig, UndoFunction revertOnError)> completion)
{
    // get current user
    User* u = ownuser();

    // get handle of remote "My Backups" folder, from user attributes
    if (!u || !u->isattrvalid(ATTR_MY_BACKUPS_FOLDER))
    {
        LOG_err << "Add backup: \"My Backups\" folder was not set";
        return completion(API_EACCESS, sc, nullptr);
    }
    const string* handleContainerStr = u->getattr(ATTR_MY_BACKUPS_FOLDER);
    if (!handleContainerStr)
    {
        LOG_err << "Add backup: ATTR_MY_BACKUPS_FOLDER attribute had null value";
        return completion(API_EACCESS, sc, nullptr);
    }

    handle h = 0;
    memcpy(&h, handleContainerStr->data(), MegaClient::NODEHANDLE);
    if (!h || h == UNDEF)
    {
        LOG_err << "Add backup: ATTR_MY_BACKUPS_FOLDER attribute contained invalid handler value";
        return completion(API_ENOENT, sc, nullptr);
    }

    // get Node of remote "My Backups" folder
    std::shared_ptr<Node> myBackupsNode = nodebyhandle(h);
    if (!myBackupsNode)
    {
        LOG_err << "Add backup: \"My Backups\" folder could not be found using the stored handle";
        return completion(API_ENOENT, sc, nullptr);
    }

    // get 'device-id'
    string deviceId;
    bool isInternalDrive = sc.mExternalDrivePath.empty();
    if (isInternalDrive)
    {
        deviceId = getDeviceidHash();
    }
    else // external drive
    {
        // drive-id must have been already written to external drive, since a name was given to it
        handle driveId;
        error e = readDriveId(*fsaccess, sc.mExternalDrivePath.toPath(false).c_str(), driveId);
        if (e != API_OK)
        {
            LOG_err << "Add backup (external): failed to read drive id";
            return completion(e, sc, nullptr);
        }

        // create the device id from the drive id
        deviceId = Base64Str<MegaClient::DRIVEHANDLE>(driveId);
    }

    if (deviceId.empty())
    {
        LOG_err << "Add backup: invalid device id";
        return completion(API_EINCOMPLETE, sc, nullptr);
    }

    // prepare for new nodes
    vector<NewNode> newnodes;
    nameid attrId = isInternalDrive ?
        AttrMap::string2nameid("dev-id") : // "device-id" would be too long
        AttrMap::string2nameid("drv-id");
    std::function<void(AttrMap& attrs)> addAttrsFunc = [=](AttrMap& attrs)
    {
        attrs.map[attrId] = deviceId;
    };

    // search for remote folder "My Backups"/`DEVICE_NAME`/
    std::shared_ptr<Node> deviceNameNode = childnodebyattribute(myBackupsNode.get(), attrId, deviceId.c_str());
    if (deviceNameNode) // validate this node
    {
        if (deviceNameNode->type != FOLDERNODE)
        {
            LOG_err << "Add backup: device-name node did not have FOLDERNODE type";
            return completion(API_EACCESS, sc, nullptr);
        }

        // make sure there is no folder with the same name as the backup
        std::shared_ptr<Node> backupNameNode = childnodebyname(deviceNameNode.get(), sc.mName.c_str());
        if (backupNameNode)
        {
            LOG_err << "Add backup: a backup with the same name (" << sc.mName << ") already existed";
            return completion(API_EACCESS, sc, nullptr);
        }
    }
    else // create `DEVICE_NAME` remote dir
    {
        // get `DEVICE_NAME`, from user attributes
        attr_t attrType = ATTR_DEVICE_NAMES;
        if (!u->isattrvalid(attrType))
        {
            LOG_err << "Add backup: device/drive name not set";
            return completion(API_EINCOMPLETE, sc, nullptr);
        }
        const string* deviceNameContainerStr = u->getattr(attrType);
        if (!deviceNameContainerStr)
        {
            LOG_err << "Add backup: null attribute value for device/drive name";
            return completion(API_EINCOMPLETE, sc, nullptr);
        }

        string deviceName;
        std::unique_ptr<TLVstore> tlvRecords(TLVstore::containerToTLVrecords(deviceNameContainerStr, &key));
        const string& deviceNameKey = isInternalDrive ? deviceId : User::attributePrefixInTLV(ATTR_DEVICE_NAMES, true) + deviceId;
        if (!tlvRecords || !tlvRecords->get(deviceNameKey, deviceName) || deviceName.empty())
        {
            LOG_err << "Add backup: device/drive name not found";
            return completion(API_EINCOMPLETE, sc, nullptr);
        }

        // is there a folder with the same device-name already?
        deviceNameNode = childnodebyname(myBackupsNode.get(), deviceName.c_str());
        if (deviceNameNode)
        {
            LOG_err << "Add backup: new device, but a folder with the same device-name (" << deviceName << ") already existed";
            return completion(API_EEXIST, sc, nullptr);
        }

        // add a new node for it
        newnodes.emplace_back();
        NewNode& newNode = newnodes.back();

        putnodes_prepareOneFolder(&newNode, deviceName, true, addAttrsFunc);
        newNode.nodehandle = AttrMap::string2nameid("dummy"); // any value should do, let's make it somewhat "readable"
    }

    // create backupName remote dir
    newnodes.emplace_back();
    NewNode& backupNameNode = newnodes.back();

    putnodes_prepareOneFolder(&backupNameNode, sc.mName, true);    // backup node should not include dev-id/drv-id
    if (!deviceNameNode)
    {
        // Set parent handle if part of the new nodes array (it cannot be from an existing node)
        backupNameNode.parenthandle = newnodes[0].nodehandle;
    }

    // create the new node(s)
    putnodes(deviceNameNode ? deviceNameNode->nodeHandle() : myBackupsNode->nodeHandle(),
             NoVersioning, std::move(newnodes), nullptr, reqtag, true,
             [completion, sc, this](const Error& e, targettype_t, vector<NewNode>& nn, bool targetOverride, int tag){

                if (e)
                {
                    completion(e, sc, nullptr);
                }
                else
                {
                    handle newBackupNodeHandle = nn.back().mAddedHandle;

                    SyncConfig updatedConfig = sc;
                    updatedConfig.mRemoteNode.set6byte(newBackupNodeHandle);

                    if (std::shared_ptr<Node> backupRoot = nodeByHandle(updatedConfig.mRemoteNode))
                    {
                        updatedConfig.mOriginalPathOfRemoteRootNode = backupRoot->displaypath();
                    }
                    else
                    {
                        LOG_err << "Node created for backup is missing already";
                        completion(API_EEXIST, updatedConfig, nullptr);
                    }

                    // Offer the option to the caller, to remove the new Backup node if a later step fails
                    UndoFunction undoOnFail = [newBackupNodeHandle, this](std::function<void()> continuation){
                        if (std::shared_ptr<Node> n = nodebyhandle(newBackupNodeHandle))
                        {
                            unlink(n.get(), false, 0, true, [continuation](NodeHandle, Error){
                                if (continuation) continuation();
                            });
                        }
                        else
                        {
                            if (continuation) continuation();
                        }
                    };

                    completion(API_OK, updatedConfig, undoOnFail);
                }

             });
}

// move node to //bin, then on to the SyncDebris folder of the day (to prevent
// dupes)
void MegaClient::movetosyncdebris(Node* dn, bool inshare, std::function<void(NodeHandle, Error)>&& completion, bool canChangeVault)
{
    execmovetosyncdebris(dn, move(completion), canChangeVault, inshare);
}

void MegaClient::execmovetosyncdebris(Node* requestedNode, std::function<void(NodeHandle, Error)>&& completion, bool canChangeVault, bool isInshare)
{
    if (requestedNode)
    {
        pendingDebris.emplace_back(requestedNode->nodeHandle(), move(completion), isInshare, canChangeVault);
    }

    if (std::shared_ptr<Node> debrisTarget = getOrCreateSyncdebrisFolder())
    {
        for (auto& rec : pendingDebris)
        {
            if (std::shared_ptr<Node> n = nodeByHandle(rec.nodeHandle))
            {
                if (!rec.mIsInshare)
                {
                    LOG_debug << "Moving to cloud Syncdebris: " << n->displaypath() << " in " << debrisTarget->displaypath() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
                    rename(n, debrisTarget, SYNCDEL_DEBRISDAY, n->parent ? n->parent->nodeHandle() : NodeHandle(), nullptr, rec.mCanChangeVault, move(rec.completion));
                }
                else
                {
                    LOG_debug << "Copy and delete to cloud Syncdebris: " << n->displaypath() << " in " << debrisTarget->displaypath() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
                    TreeProcCopy tc;
                    proctree(n, &tc, false, false);
                    tc.allocnodes();
                    proctree(n, &tc, false, false);
                    tc.nn[0].parenthandle = UNDEF;
                    putnodes(debrisTarget->nodeHandle(), NoVersioning, std::move(tc.nn), nullptr, reqtag, rec.mCanChangeVault, [this, rec](const Error&e, targettype_t, vector<NewNode>&, bool, int)
                    {
                        if (e)
                        {
                            LOG_warn << "Error copying files at SyncDebris folder, continue process (remove them)";
                        }

                        if (std::shared_ptr<Node> n = nodeByHandle(rec.nodeHandle))
                        {
                            unlink(n.get(), false, 0, false, [rec](NodeHandle, Error e)
                            {
                                if (rec.completion) rec.completion(rec.nodeHandle, e);
                            });
                        }
                        else
                        {
                            if (rec.completion) rec.completion(rec.nodeHandle, API_EEXIST);
                        }
                    });
                }
            }
            else
            {
                if (rec.completion) rec.completion(rec.nodeHandle, API_EEXIST);
            }
        }
        pendingDebris.clear();
    }
}


std::shared_ptr<Node> MegaClient::getOrCreateSyncdebrisFolder()
{
    std::shared_ptr<Node> binNode = nodeByHandle(mNodeManager.getRootNodeRubbish());
    if (!binNode)
    {
        return nullptr;
    }
    assert(binNode->type == RUBBISHNODE);

    bool foundDebris = false;

    m_time_t ts = m_time();
    struct tm tms;
    char buf[32];
    struct tm* ptm = m_localtime(ts, &tms);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);

    // locate //bin/SyncDebris
    std::shared_ptr<Node> n;
    if ((n = childnodebynametype(binNode.get(), SYNCDEBRISFOLDERNAME, FOLDERNODE)))
    {
        binNode = n;
        foundDebris = true;

        // locate //bin/SyncDebris/yyyy-mm-dd
        if ((n = childnodebyname(binNode.get(), buf)) && n->type == FOLDERNODE)
        {
            binNode = n;
            return binNode; // all set to send node to this one
        }
    }

    // not found, so create whatever is missing (but don't attempt too frequently)
    m_time_t currentminute = ts / 60;
    if (syncdebrisadding || syncdebrisminute == currentminute)
    {
        return nullptr;
    }

    syncdebrisadding = true;
    syncdebrisminute = currentminute;
    LOG_debug << clientname << "Creating cloud daily SyncDebris and daily folder: " << buf;

    // create missing component(s) of the sync debris folder of the day
    vector<NewNode> nnVec;
    SymmCipher tkey;
    string tattrstring;
    AttrMap tattrs;

    nnVec.resize((foundDebris) ? 1 : 2);

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
        tattrs.map['n'] = (i || foundDebris) ? buf : SYNCDEBRISFOLDERNAME;
        tattrs.getjson(&tattrstring);
        tkey.setkey((const byte*)nn->nodekey.data(), FOLDERNODE);
        nn->attrstring.reset(new string);
        makeattr(&tkey, nn->attrstring, tattrstring.c_str());
    }

    reqs.add(new CommandPutNodes(
        this, binNode->nodeHandle(), NULL, NoVersioning, move(nnVec),
        0, PUTNODES_SYNCDEBRIS, nullptr,
        [this](const Error&, targettype_t, vector<NewNode>&, bool targetOverride, int tag){
            syncdebrisadding = false;
            // on completion, send the queued nodes
            LOG_debug << "Daily cloud SyncDebris folder created. Trigger remaining debris moves: " << pendingDebris.size();
            execmovetosyncdebris(nullptr, nullptr, false, false);
        }, false));
    return nullptr;
}

#endif

string MegaClient::cypherTLVTextWithMasterKey(const char* name, const string& text)
{
    TLVstore tlv;
    tlv.set(name, text);
    std::unique_ptr<string> tlvStr(tlv.tlvRecordsToContainer(rng, &key));

    return Base64::btoa(*tlvStr);
}

string MegaClient::decypherTLVTextWithMasterKey(const char* name, const string& encoded)
{
    string unencoded = Base64::atob(encoded);
    string value;

    unique_ptr<TLVstore> tlv(TLVstore::containerToTLVrecords(&unencoded, &key));
    if (tlv)
        tlv->get(name, value);

    return value;
}

// inject file into transfer subsystem
// if file's fingerprint is not valid, it will be obtained from the local file
// (PUT) or the file's key (GET)
bool MegaClient::startxfer(direction_t d, File* f, TransferDbCommitter& committer, bool skipdupes, bool startfirst, bool donotpersist, VersioningOption vo, error* cause, int tag, m_off_t availableDiskSpace)
{
    assert(f->getLocalname().isAbsolute());
    f->mVersioningOption = vo;

    // Dummy to avoid checking later.
    if (!cause)
    {
        // Initializer provided to silence warnings.
        static error dummy = API_OK;

        cause = &dummy;
    }

    // Is caller trying to start a download?
    if (d == GET)
    {
        auto targetPath = f->getLocalname().parentPath();

        assert(f->size >= 0);

        // Do we have enough space for the download?
        // fsaccess->availableDiskSpace is expensive over network driver. 
        // Pass in a positive value, check will use this value. A use case is downloading a folder
        auto available = availableDiskSpace > 0 ? availableDiskSpace : fsaccess->availableDiskSpace(targetPath);
        if (available <= f->size)
        {
            LOG_warn << "Insufficient space available for download: "
                     << f->getLocalname()
                     << ": available: "
                     << available
                     << ", required: "
                     << f->size;

            *cause = LOCAL_ENOSPC;

            return false;
        }
    }

    if (!f->transfer)
    {
        if (d == PUT)
        {
            if (!nodeByHandle(f->h) && (f->targetuser != SUPPORT_USER_HANDLE))
            {
                // the folder to upload is unknown - perhaps this is a resumed transfer
                // and the folder was deleted in the meantime
                *cause = API_EARGS;
                return false;
            }

            if (!f->isvalid)    // (sync LocalNodes always have this set)
            {
                // missing FileFingerprint for local file - generate
                auto fa = fsaccess->newfileaccess();

                if (fa->fopen(f->getLocalname(), d == PUT, d == GET, FSLogging::logOnError))
                {
                    f->genfingerprint(fa.get());
                }
            }

            // if we are unable to obtain a valid file FileFingerprint, don't proceed
            if (!f->isvalid)
            {
                LOG_err << "Unable to get a fingerprint " << f->name;

                *cause = API_EREAD;

                return false;
            }

#ifdef USE_MEDIAINFO
            mediaFileInfo.requestCodecMappingsOneTime(this, f->getLocalname());
#endif
        }
        else
        {
            if (!f->isvalid)
            {
                LOG_warn << "Downloading a file with invalid fingerprint, was: " << f->fingerprintDebugString() << " name: " << f->getLocalname();
                // no valid fingerprint: use filekey as its replacement
                memcpy(f->crc.data(), f->filekey, sizeof f->crc);
                LOG_warn << "Downloading a file with invalid fingerprint, adjusted to: " << f->fingerprintDebugString() << " name: " << f->getLocalname();
            }
        }

        Transfer* t = NULL;
        auto range = multi_transfers[d].equal_range(f);
        for (auto it = range.first; it != range.second; ++it)
        {
            if (it->second->files.empty()) continue;
            File* f2 = it->second->files.front();

            string ext1, ext2;
            if (fsaccess->getextension(f->getLocalname(), ext1) &&
                fsaccess->getextension(f2->getLocalname(), ext2))
            {
                if (!ext1.empty() && ext1[0] == '.') ext1.erase(0, 1);
                if (!ext2.empty() && ext2[0] == '.') ext2.erase(0, 1);

                if (treatAsIfFileDataEqual(*f, ext1,
                                           *f2, ext2))
                {
                    // Upload data for both these Files just once overall - two Files in one Transfer
                    t = it->second;
                    break;
                }
            }
        }

        if (t)
        {
            if (skipdupes)
            {
                for (file_list::iterator fi = t->files.begin(); fi != t->files.end(); fi++)
                {
                    if ((d == GET && f->getLocalname() == (*fi)->getLocalname())
                            || (d == PUT && f->h != UNDEF
                                && f->h == (*fi)->h
                                && !f->targetuser.size()
                                && !(*fi)->targetuser.size()
                                && f->name == (*fi)->name))
                    {
                        LOG_warn << "Skipping duplicated transfer";

                        *cause = API_EEXIST;

                        return false;
                    }
                }
            }
            f->file_it = t->files.insert(t->files.end(), f);
            f->transfer = t;
            f->tag = tag;
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
            // there is no existing transfer uploading this file (or any duplicate of it)
            // check if there used to be, and can we resume one.
            // Note that these multi_cachedtransfers always have an empty Files list, those are not attached when loading from db
            // Only the Transfer's own localpath field tells us the path it was uploading

            auto range = multi_cachedtransfers[d].equal_range(f);
            for (auto it = range.first; it != range.second; ++it)
            {
                assert(it->second->files.empty());
                if (it->second->localfilename.empty()) continue;

                if (d == PUT)
                {
                    // for uploads, check for the same source file
                    if (it->second->localfilename == f->getLocalname())
                    {
                        // the exact same file, so use this one (fingerprint is double checked below)
                        t = it->second;
                        multi_cachedtransfers[d].erase(it);
                        break;
                    }
                }
                else
                {
                    // for downloads, check for the same source node
                    if (it->second->downloadFileHandle == f->h &&
                       !it->second->downloadFileHandle.isUndef())
                    {
                        // the exact same cloud file, so use this one
                        t = it->second;
                        multi_cachedtransfers[d].erase(it);
                        break;
                    }
                }
            }

            if (!t && d == PUT)
            {
                // look to see if there a cached transfer that is similar enough
                // this case could occur if there were multiple Files before the transfer
                // got suspended (eg by app exit), and we are considering the File of one of the others
                // than the actual file path that was being uploaded.

                for (auto it = range.first; it != range.second; ++it)
                {
                    assert(it->second->files.empty());
                    if (it->second->localfilename.empty()) continue;

                    string ext1, ext2;
                    if (fsaccess->getextension(f->getLocalname(), ext1) &&
                        fsaccess->getextension(it->second->localfilename, ext2))
                    {
                        if (!ext1.empty() && ext1[0] == '.') ext1.erase(0, 1);
                        if (!ext2.empty() && ext2[0] == '.') ext2.erase(0, 1);

                        if (treatAsIfFileDataEqual(*f, ext1,
                                                   *it->first, ext2))
                        {
                            t = it->second;
                            multi_cachedtransfers[d].erase(it);
                            break;
                        }
                    }
                }
            }

            if (t)
            {
                bool hadAnyData = t->pos > 0;
                if ((d == GET && !t->pos) || ((m_time() - t->lastaccesstime) >= 172500))
                {
                    LOG_warn << "Discarding temporary URL (" << t->pos << ", " << t->lastaccesstime << ")";
                    t->tempurls.clear();

                    if (d == PUT)
                    {
                        t->chunkmacs.clear();
                        t->progresscompleted = 0;
                        t->ultoken.reset();
                        t->pos = 0;
                    }
                }

                auto fa = fsaccess->newfileaccess();

                if (t->localfilename.empty() || !fa->fopen(t->localfilename, FSLogging::logExceptFileNotFound))
                {
                    if (d == PUT)
                    {
                        if (!t->localfilename.empty())
                        {
                            // if empty, we had not started the upload. The real path is in the first File
                            LOG_warn << "Local file not found: " << t->localfilename;
                        }
                        // the transfer will be retried to ensure that the file
                        // is not just just temporarily blocked
                    }
                    else
                    {
                        if (hadAnyData)
                        {
                            LOG_warn << "Temporary file not found:" << t->localfilename;
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
                            LOG_warn << "The local file has been modified: " << t->localfilename;
                            t->tempurls.clear();
                            t->chunkmacs.clear();
                            t->progresscompleted = 0;
                            t->ultoken.reset();
                            t->pos = 0;
                        }
                    }
                    else
                    {
                        if (t->progresscompleted > fa->size)
                        {
                            LOG_warn << "Truncated temporary file: " << t->localfilename;
                            t->chunkmacs.clear();
                            t->progresscompleted = 0;
                            t->pos = 0;
                        }
                    }
                }
                LOG_debug << "Transfer resumed";
            }

            if (!t)
            {
                t = new Transfer(this, d);
                *(FileFingerprint*)t = *(FileFingerprint*)f;
            }

            t->skipserialization = donotpersist;

            t->lastaccesstime = m_time();
            t->tag = tag;
            f->tag = tag;
            t->transfers_it = multi_transfers[d].insert(pair<FileFingerprint*, Transfer*>((FileFingerprint*)t, t));

            f->file_it = t->files.insert(t->files.end(), f);
            f->transfer = t;
            if (!f->dbid && !donotpersist)
            {
                filecacheadd(f, committer);
            }

            transferlist.addtransfer(t, committer, startfirst);
            app->transfer_added(t);
            app->file_added(f);

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

        assert( (f->h.isUndef() && f->targetuser.size() && (f->targetuser.size() == 11 || f->targetuser.find("@")!=string::npos) ) // <- uploading to inbox
                || (!f->h.isUndef() && (nodeByHandle(f->h) || d == GET) )); // target handle for the upload should be known at this time (except for inbox uploads)
    }

    *cause = API_OK;

    return true;
}

// remove file from transfer subsystem
void MegaClient::stopxfer(File* f, TransferDbCommitter* committer)
{
    if (f->transfer)
    {
        LOG_debug << "Stopping transfer: " << f->name;

        Transfer *transfer = f->transfer;
        transfer->removeTransferFile(API_EINCOMPLETE, f, committer);

        // last file for this transfer removed? shut down transfer.
        if (!transfer->files.size())
        {
            transfer->removeAndDeleteSelf(TRANSFERSTATE_CANCELLED);
        }
        else
        {
            if (transfer->type == PUT && !transfer->localfilename.empty())
            {
                LOG_debug << "Updating transfer path";
                transfer->files.front()->prepare(*fsaccess);
            }
        }
    }
}

// pause/unpause transfers
void MegaClient::pausexfers(direction_t d, bool pause, bool hard, TransferDbCommitter& committer)
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

#ifdef ENABLE_SYNC
    syncs.transferPauseFlagsUpdated(xferpaused[GET], xferpaused[PUT]);
#endif
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

#if 0
std::shared_ptr<Node> MegaClient::nodebyfingerprint(LocalNode* localNode)
{
    sharedNode_vector remoteNodes = mNodeManager.getNodesByFingerprint(*localNode);

    if (remoteNodes.empty())
        return nullptr;

    // Only compare metamac if the node doesn't already exist.
    sharedNode_vector::const_iterator remoteNode =
      std::find_if(remoteNodes.begin(),
                   remoteNodes.end(),
                   [&](std::shared_ptr<Node> remoteNode) -> bool
                   {
                       return localNode->toName_of_localname == remoteNode->displayname();
                   });

    if (remoteNode != remoteNodes.end())
        return *remoteNode;

    remoteNode = remoteNodes.begin();

    // Compare the local file's metamac against a random candidate.
    //
    // If we're unable to generate the metamac, fail in such a way that
    // guarantees safe behavior.
    //
    // That is, treat both nodes as distinct until we're absolutely certain
    // they are identical.
    auto ifAccess = fsaccess->newfileaccess();

    auto localPath = localNode->getLocalPath();

    if (!ifAccess->fopen(localPath, true, false, FSLogging::logOnError))
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

static bool nodes_ctime_greater(const Node* a, const Node* b)
{
    return a->ctime > b->ctime;
}

static bool nodes_ctime_greater_shared_ptr(std::shared_ptr<Node> a, std::shared_ptr<Node> b)
{
    return nodes_ctime_greater(a.get(), b.get());
}

namespace action_bucket_compare
{
    static bool compare(const Node* a, const Node* b, MegaClient* mc)
    {
        if (a->owner != b->owner) return a->owner > b->owner;
        if (a->parent != b->parent) return a->parent > b->parent;

        // added/updated - distinguish by versioning
        size_t aChildrenCount = mc->getNumberOfChildren(a->nodeHandle());
        size_t bChildrenCount = mc->getNumberOfChildren(b->nodeHandle());
        if (aChildrenCount != bChildrenCount) return aChildrenCount > bChildrenCount;

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

}   // end namespace action_bucket_compare


bool MegaClient::nodeIsMedia(const Node *n, bool *isphoto, bool *isvideo) const
{
    if (n->type != FILENODE)
    {
        return false;
    }

    bool a = n->isIncludedForMimetype(MimeType_t::MIME_TYPE_PHOTO);
    if (isphoto)
    {
        *isphoto = a;
    }
    if (a && !isvideo)
    {
        return true;
    }
    bool b = n->isIncludedForMimetype(MimeType_t::MIME_TYPE_VIDEO);
    if (isvideo)
    {
        *isvideo = b;
    }

    return a || b;
}

bool MegaClient::nodeIsVideo(const Node *n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_VIDEO);
}

bool MegaClient::nodeIsPhoto(const Node *n, bool checkPreview) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_PHOTO, checkPreview);
}

bool MegaClient::nodeIsAudio(const Node *n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_AUDIO);
}

bool MegaClient::nodeIsDocument(const Node *n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_DOCUMENT);
}

bool MegaClient::nodeIsPdf(const Node *n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_PDF);
}

bool MegaClient::nodeIsPresentation(const Node *n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_PRESENTATION);
}

bool MegaClient::nodeIsArchive(const Node* n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_ARCHIVE);
}

bool MegaClient::nodeIsProgram(const Node* n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_PROGRAM);
}

bool MegaClient::nodeIsMiscellaneous(const Node* n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_MISC);
}

bool MegaClient::nodeIsSpreadsheet(const Node *n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_SPREADSHEET);
}

bool MegaClient::nodeIsOtherType(const Node* n) const
{
    return n->isIncludedForMimetype(MimeType_t::MIME_TYPE_OTHERS);
}

bool MegaClient::treatAsIfFileDataEqual(const FileFingerprint& node1, const LocalPath& file2, const string& filenameExtensionLowercaseNoDot)
{
    // if equal, upload or download could be skipped
    if (filenameExtensionLowercaseNoDot.empty()) return false;
    assert(filenameExtensionLowercaseNoDot[0] != '.');

    FileFingerprint fp;
    auto fa = fsaccess->newfileaccess();
    if (fa->fopen(file2, true, false, FSLogging::logOnError))
    {

        if (!fp.genfingerprint(fa.get())) return false;
        if (fp != node1) return false;
        if (!fp.isvalid || !node1.isvalid) return false;

        // In future (for non-media files) we might recalculate the MAC
        // of the on-disk file to see if it matches the Node's MAC.
        // That would be much, much more accurate

        return fp.size > 128 * 1024 &&
               isPhotoVideoAudioByName(filenameExtensionLowercaseNoDot);
    }
    return false;
}

bool MegaClient::treatAsIfFileDataEqual(const FileFingerprint& fp1, const string& filenameExtensionLowercaseNoDot1,
                                        const FileFingerprint& fp2, const string& filenameExtensionLowercaseNoDot2)
{
    // if equal, upload or download could be skipped or combined
    assert(filenameExtensionLowercaseNoDot1.empty() || filenameExtensionLowercaseNoDot1[0] != '.');
    assert(filenameExtensionLowercaseNoDot2.empty() || filenameExtensionLowercaseNoDot2[0] != '.');

    if (filenameExtensionLowercaseNoDot1.empty() || filenameExtensionLowercaseNoDot2.empty()) return false;
    if (filenameExtensionLowercaseNoDot1 != filenameExtensionLowercaseNoDot2) return false;
    if (!fp1.isvalid || !fp2.isvalid) return false;
    if (fp1 != fp2) return false;

    // In future (for non-media files) we might recalculate the MAC
    // of the on-disk file to see if it matches the Node's MAC.
    // That would be much, much more accurate.  (more parameters will be needed)

    return fp1.size > 128 * 1024 &&
            isPhotoVideoAudioByName(filenameExtensionLowercaseNoDot1);
}

recentactions_vector MegaClient::getRecentActions(unsigned maxcount, m_time_t since)
{
    recentactions_vector rav;
    sharedNode_vector v = mNodeManager.getRecentNodes(maxcount, since);

    for (auto i = v.begin(); i != v.end(); )
    {
        // find the oldest node, maximum 6h
        auto bucketend = i + 1;
        while (bucketend != v.end() && (*bucketend)->ctime > (*i)->ctime - 6 * 3600)
        {
            ++bucketend;
        }

        // sort the defined bucket by owner, parent folder, added/updated and ismedia
        std::sort(i, bucketend, [this](const std::shared_ptr<Node>& n1, const std::shared_ptr<Node>& n2) { return action_bucket_compare::compare(n1.get(), n2.get(), this); });

        // split the 6h-bucket in different buckets according to their content
        for (auto j = i; j != bucketend; ++j)
        {
            if (i == j || action_bucket_compare::compare(i->get(), j->get(), this))
            {
                // add a new bucket
                recentaction ra;
                ra.time = (*j)->ctime;
                ra.user = (*j)->owner;
                ra.parent = (*j)->parent ? (*j)->parent->nodehandle : UNDEF;
                ra.updated = getNumberOfChildren((*j)->nodeHandle());   // children of files represent previous versions
                ra.media = nodeIsMedia(j->get(), nullptr, nullptr);
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
        std::sort(i->nodes.begin(), i->nodes.end(), nodes_ctime_greater_shared_ptr);
        i->time = i->nodes.front()->ctime;
    }
    // sort buckets in the vector
    std::sort(rav.begin(), rav.end(), action_bucket_compare::comparetime);
    return rav;
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

void MegaClient::reportevent(const char* event, const char* details)
{
    LOG_err << "SERVER REPORT: " << event << " DETAILS: " << details;
    reqs.add(new CommandSendReport(this, event, details, Base64Str<MegaClient::USERHANDLE>(me)));
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

std::shared_ptr<Node> MegaClient::getovnode(Node *parent, string *name)
{
    if (parent && name)
    {
        return childnodebyname(parent, name->c_str(), true);
    }
    return nullptr;
}

sharedNode_list MegaClient::getChildren(const Node* parent, CancelToken cancelToken)
{
    return mNodeManager.getChildren(parent, cancelToken);
}

size_t MegaClient::getNumberOfChildren(NodeHandle parentHandle)
{
    return mNodeManager.getNumberOfChildrenFromNode(parentHandle);
}

bool MegaClient::loggedIntoFolder() const
{
    return !ISUNDEF(mFolderLink.mPublicHandle);
}

bool MegaClient::loggedIntoWritableFolder() const
{
    return loggedIntoFolder() && !mFolderLink.mWriteAuth.empty();
}

std::string MegaClient::getAuthURI(bool supressSID, bool supressAuthKey)
{
    string auth;

    if (loggedIntoFolder())
    {
        auth.append("&n=");
        auth.append(Base64Str<NODEHANDLE>(mFolderLink.mPublicHandle));
        if (!supressAuthKey)
        {
            auth.append(mFolderLink.mWriteAuth);
        }
        if (!supressSID && !mFolderLink.mAccountAuth.empty())
        {
            auth.append("&sid=");
            auth.append(mFolderLink.mAccountAuth);
        }
    }
    else
    {
        if (!supressSID && !sid.empty())
        {
            auth.append("&sid=");
            auth.append(Base64::btoa(sid));
        }

        if (inPublicSetPreview())
        {
            auth.append("&s=");
            auth.append(Base64Str<PUBLICSETHANDLE>(mPreviewSet->mPublicId));
        }
    }

    return auth;
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

    reqs.add(new CommandSendReport(this, type.c_str(), message, NULL));
}

void MegaClient::sendevent(int event, const char *desc, const char* viewId, bool addJourneyId)
{
    LOG_warn << clientname << "Event " << event << ": " << desc;
    reqs.add(new CommandSendEvent(this, event, desc, addJourneyId, viewId));
}

void MegaClient::sendevent(int event, const char *message, int tag, const char *viewId, bool addJourneyId)
{
    int creqtag = reqtag;
    reqtag = tag;
    sendevent(event, message, viewId, addJourneyId);
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
void MegaClient::createChat(bool group, bool publicchat, const userpriv_vector* userpriv, const string_map* userkeymap, const char* title, bool meetingRoom, int chatOptions, const ScheduledMeeting* schedMeeting)
{
    reqs.add(new CommandChatCreate(this, group, publicchat, userpriv, userkeymap, title, meetingRoom, chatOptions, schedMeeting));
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

void MegaClient::setChatMode(TextChat* chat, bool pubChat)
{
    if (!chat)
    {
        LOG_warn << "setChatMode: Invalid chat provided";
        return;
    }

    if (chat->setMode(pubChat) == API_EACCESS)
    {
        std::string msg = "setChatMode: trying to convert a chat from private into public. chatid: "
                          + std::string(Base64Str<MegaClient::CHATHANDLE>(chat->getChatId()));
        sendevent(99476, msg.c_str(), 0);
        LOG_warn << msg;
    }
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
                            j->leavearray();
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
                            j->leavearray();
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

void MegaClient::setchatretentiontime(handle chatid, unsigned period)
{
    reqs.add(new CommandSetChatRetentionTime(this, chatid, period));
}

error MegaClient::parseScheduledMeetings(std::vector<std::unique_ptr<ScheduledMeeting>>& schedMeetings,
                                         bool parsingOccurrences, JSON *j, bool parseOnce,
                                         handle* ou, UserAlert::UpdatedScheduledMeeting::Changeset* cs,
                                         handle_set* childMeetingsDeleted)
{
    /* - if any parsing error occurs: this method returns API_EINTERNAL, and schedMeetings vector will
     *   contain those valid scheduled meetings already parsed
     * - if no parsing error but any sched meeting is considered ill-formed by ScheduledMeeting::isValid():
     *   that sched meeting won't be added added to schedMeetings vector, and we'll continue processing JSON
     */
    JSON* auxJson = j;
    bool illFormedElems = false;
    bool parse = parseOnce
            ? true                      // parse a single object
            : auxJson->enterobject();   // parse an array of objects

    while (parse)
    {
        bool exit = false;
        handle chatid = UNDEF;
        handle organizerUserId = UNDEF;
        handle schedId = UNDEF;
        handle parentSchedId = UNDEF;
        handle originatingUser = UNDEF;
        std::string timezone;
        m_time_t startDateTime = mega_invalid_timestamp;
        m_time_t endDateTime = mega_invalid_timestamp;
        std::string title;
        std::string description;
        std::string attributes;
        m_time_t overrides = mega_invalid_timestamp;
        int cancelled = 0;
        std::unique_ptr<ScheduledFlags> flags;
        std::unique_ptr<ScheduledRules> rules;

        while (!exit)
        {
            switch (auxJson->getnameid())
            {
                case MAKENAMEID3('c', 'i', 'd'): // chatid
                {
                    chatid = auxJson->gethandle(MegaClient::CHATHANDLE);
                    break;
                }
                case MAKENAMEID2('i', 'd'):  // scheduled meeting id
                {
                    schedId = auxJson->gethandle(MegaClient::CHATHANDLE);
                    break;
                }
                case MAKENAMEID1('p'):  // parent callid
                {
                    parentSchedId = auxJson->gethandle(MegaClient::CHATHANDLE);
                    break;
                }
                case MAKENAMEID1('u'): // organizer user Handle
                {
                    organizerUserId = auxJson->gethandle(MegaClient::CHATHANDLE);
                    break;
                }
                case MAKENAMEID2('o', 'u'): // originating user
                {
                    assert(ou);
                    originatingUser = auxJson->gethandle(MegaClient::USERHANDLE);
                    break;
                }
                case MAKENAMEID2('t', 'z'): // timezone
                {
                    auxJson->storeobject(&timezone);
                    break;
                }
                case MAKENAMEID1('s'): // start date time
                {
                    startDateTime = auxJson->getint();
                    break;
                }
                case MAKENAMEID1('e'): // end date time
                {
                    endDateTime = auxJson->getint();
                    break;
                }
                case MAKENAMEID1('t'):  // title
                {
                    auxJson->storeobject(&title);
                    break;
                }
                case MAKENAMEID1('d'): // description
                {
                    auxJson->storeobject(&description);
                    break;
                }
                case MAKENAMEID2('a', 't'): // attributes
                {
                    auxJson->storeobject(&attributes);
                    break;
                }
                case MAKENAMEID1('o'): // override
                {
                    overrides = auxJson->getint();
                    break;
                }
                case MAKENAMEID1('c'): // cancelled
                {
                    cancelled = static_cast<int>(auxJson->getint());
                    break;
                }
                case MAKENAMEID1('f'): // flags
                {
                    flags.reset(new ScheduledFlags(static_cast<unsigned long>(auxJson->getint())));
                    break;
                }
                case MAKENAMEID1('r'): // scheduled meeting rules
                {
                    if (auxJson->enterobject())
                    {
                        string freq;
                        m_time_t until = mega_invalid_timestamp;
                        int interval = ScheduledRules::INTERVAL_INVALID;
                        ScheduledRules::rules_vector vWeek;
                        ScheduledRules::rules_vector vMonth;
                        ScheduledRules::rules_map mMonth;
                        bool exitRules = false;

                        while (!exitRules)
                        {
                            switch (auxJson->getnameid())
                            {
                                case MAKENAMEID1('f'):
                                {
                                    auxJson->storeobject(&freq);
                                    break;
                                }
                                case MAKENAMEID1('i'):
                                {
                                    interval = static_cast<int>(auxJson->getint());
                                    break;
                                }
                                case MAKENAMEID1('u'):
                                {
                                    until = auxJson->getint();
                                    break;
                                }
                                case MAKENAMEID2('w', 'd'):
                                {
                                    if (auxJson->enterarray())
                                    {
                                        while(auxJson->isnumeric())
                                        {
                                            vWeek.emplace_back(static_cast<int8_t>(auxJson->getint()));
                                        }
                                        auxJson->leavearray();
                                    }
                                    break;
                                }
                                case MAKENAMEID2('m', 'd'):
                                {
                                    if (auxJson->enterarray())
                                    {
                                        while(auxJson->isnumeric())
                                        {
                                            vMonth.emplace_back(static_cast<int8_t>(auxJson->getint()));
                                        }
                                        auxJson->leavearray();
                                    }
                                    break;
                                }
                                case MAKENAMEID3('m', 'w', 'd'):
                                {
                                    if (auxJson->enterarray())
                                    {
                                        while (auxJson->enterarray())
                                        {
                                            int8_t key = -1;
                                            int8_t value = -1;
                                            int i = 0;
                                            while (auxJson->isnumeric())
                                            {
                                                int8_t val = static_cast<int8_t>(auxJson->getint());
                                                if (i == 0) { key = val; }
                                                if (i == 1) { value = val; }
                                                i++;
                                            }

                                            if (i > 2 || key == -1 || value == -1) // ensure that each array just contains a pair of elemements
                                            {
                                                LOG_err << "scheduled meetings rules component mwd, is malformed";
                                            }
                                            else
                                            {
                                                mMonth.emplace(key, value);
                                            }
                                            auxJson->leavearray();
                                        }
                                        auxJson->leavearray();
                                    }
                                    break;
                                }
                                case EOO:
                                {
                                    exitRules = true;
                                    break;
                                }
                                default:
                                {
                                    if (!auxJson->storeobject())
                                    {
                                        return API_EINTERNAL;
                                    }
                                }
                            }
                        }
                        auxJson->leaveobject();
                        rules.reset(new ScheduledRules(ScheduledRules::stringToFreq(freq.c_str()), interval,
                                                       until, &vWeek, &vMonth, &mMonth));
                    }
                    break;
                }
                case MAKENAMEID2('c', 's'):
                {
                    assert(cs);
                    if (auxJson->enterobject())
                    {
                        if (parseScheduledMeetingChangeset(auxJson, cs) != API_OK)
                        {
                            LOG_err << "UpdatedScheduledMeeting user alert ctor: error parsing cs array";
                            assert(false);
                            return API_EINTERNAL;
                        }
                        auxJson->leaveobject();
                    }
                    break;
                }
                case MAKENAMEID3('c', 'm', 'd'):
                {
                    assert(childMeetingsDeleted);
                    if (auxJson->enterarray() && childMeetingsDeleted)
                    {
                        while(auxJson->ishandle(MegaClient::CHATHANDLE))
                        {
                            childMeetingsDeleted->insert(auxJson->gethandle());
                        }
                        auxJson->leavearray();
                    }
                    break;
                }
                case EOO:
                {
                    exit = true;
                    if (!parseOnce)
                    {
                        auxJson->leaveobject();
                    }

                    // note: we need to B64 decode the following params: timezone, title, description, attributes
                    std::unique_ptr<ScheduledMeeting> auxMeet(new ScheduledMeeting(chatid, Base64::atob(timezone), startDateTime, endDateTime,
                                         Base64::atob(title), Base64::atob(description), organizerUserId, schedId,
                                         parentSchedId, cancelled, Base64::atob(attributes),
                                         overrides, flags.get(), rules.get()));

                    if ((parsingOccurrences && rules)
                        || !auxMeet->isValid())
                    {
                        illFormedElems = true;
                    }
                    else
                    {
                        schedMeetings.emplace_back(std::move(auxMeet));
                        if (ou) *ou = originatingUser;
                    }
                    break;
                }
                default:
                {
                    if (!auxJson->storeobject())
                    {
                        return API_EINTERNAL;
                    }
                }
            }
        }

        parse = parseOnce
                    ? false
                    : auxJson->enterobject();
    }

    if (illFormedElems)
    {
        reportInvalidSchedMeeting();
    }
    return API_OK;
}

error MegaClient::parseScheduledMeetingChangeset(JSON* j, UserAlert::UpdatedScheduledMeeting::Changeset* cs)
{
    error e = API_OK;
    bool keepParsing = true;
    auto wasFieldUpdated = [&j]()
    {
        bool updated = false;
        if (j->enterarray())
        {
            j->storeobject();
            updated = j->storeobject(); // if it is an array, it didn't change unless there are 2 values
            j->leavearray();
        }
        else
        {
            int v = static_cast<int>(j->getint());
            if (v == 1)
            {
                //field has changed but don't receive old|new values due to size reasons
                updated = true;
            }
            else
            {
                LOG_err << "ScheduledMeetings: Expected a different flag to indicate updated "
                        << "field. Expected 1 received " << v;
                assert(false);
            }
        }

        return updated;
    };

    auto getOldNewStrValues = [&j](UserAlert::UpdatedScheduledMeeting::Changeset::StrChangeset& cs,
                const char *fieldMsg)
    {
        if (!j->enterarray())
        {
            LOG_err << "ScheduledMeetings: Received updated SM with updated " << fieldMsg
                    << ". Array could not be accessed, ill-formed Json";
            assert(false);
            return API_EINTERNAL;
        }

        if (!j->storeobject(&cs.oldValue)) { cs.oldValue.clear(); }
        if (!j->storeobject(&cs.newValue)) { cs.newValue.clear(); }
        j->leavearray();
        return API_OK;
    };

    auto getOldNewTsValues = [&j](UserAlert::UpdatedScheduledMeeting::Changeset::TsChangeset& cs,
                const char *fieldMsg)
    {
        if (!j->enterarray())
        {
            LOG_err << "ScheduledMeetings: Received updated SM with updated " << fieldMsg
                    << ". Array could not be accessed, ill-formed Json";
            assert(false);
            return API_EINTERNAL;
        }

        auto getTsVal = [&j](m_time_t& out)
        {
            out = mega_invalid_timestamp;
            if (j->isnumeric())
            {
                auto val = j->getint();
                if (val > -1)
                {
                    out = val;
                }
            }
        };

        getTsVal(cs.oldValue);
        getTsVal(cs.newValue);
        j->leavearray();
        return API_OK;
    };

    UserAlert::UpdatedScheduledMeeting::Changeset auxCS;
    using Changeset = UserAlert::UpdatedScheduledMeeting::Changeset;
    do
    {
        switch(j->getnameid())
        {
            case MAKENAMEID1('t'):
            {
                Changeset::StrChangeset tCs;
                auto err = getOldNewStrValues(tCs, "Title");
                if (err == API_OK)
                {
                    if (!tCs.oldValue.empty() && !tCs.newValue.empty())
                    {
                        auxCS.addChange(Changeset::CHANGE_TYPE_TITLE, &tCs);
                    }
                    // else => item unchanged, but old value provided for rendering purposes
                }
                else if (err == API_EINTERNAL && !j->storeobject())
                {
                    return API_EINTERNAL;
                }
            }
            break;

            case MAKENAMEID1('d'):
                if (wasFieldUpdated())
                {
                    auxCS.addChange(Changeset::CHANGE_TYPE_DESCRIPTION);
                }
                break;

            case MAKENAMEID1('c'):
                if (wasFieldUpdated())
                {
                    auxCS.addChange(Changeset::CHANGE_TYPE_CANCELLED);
                }
                break;

            case MAKENAMEID2('t', 'z'):
            {
                Changeset::StrChangeset tzCs;
                auto err = getOldNewStrValues(tzCs, "TimeZone");
                if (err == API_OK)
                {
                    if (!tzCs.oldValue.empty() && !tzCs.newValue.empty())
                    {
                        auxCS.addChange(Changeset::CHANGE_TYPE_TIMEZONE, &tzCs);
                    }
                    // else => item unchanged, but old value provided for rendering purposes
                }
                else if (err == API_EINTERNAL && !j->storeobject())
                {
                    return API_EINTERNAL;
                }
            }
            break;

            case MAKENAMEID1('s'):
            {
                Changeset::TsChangeset sdCs;
                auto err = getOldNewTsValues(sdCs, "StartDateTime");
                if (err == API_OK)
                {
                    if (sdCs.oldValue != mega_invalid_timestamp && sdCs.newValue != mega_invalid_timestamp)
                    {
                        auxCS.addChange(Changeset::CHANGE_TYPE_STARTDATE, nullptr, &sdCs);
                    }
                    // else => item unchanged, but old value provided for rendering purposes
                }
                else if (err == API_EINTERNAL && !j->storeobject())
                {
                    return API_EINTERNAL;
                }
            }
            break;

            case MAKENAMEID1('e'):
            {
                Changeset::TsChangeset edCs;
                auto err = getOldNewTsValues(edCs, "EndDateTime");
                if (err == API_OK)
                {
                    if (edCs.oldValue != mega_invalid_timestamp && edCs.newValue != mega_invalid_timestamp)
                    {
                        auxCS.addChange(Changeset::CHANGE_TYPE_ENDDATE, nullptr, &edCs);
                    }
                    // else => item unchanged, but old value provided for rendering purposes
                }
                else if (err == API_EINTERNAL && !j->storeobject())
                {
                    return API_EINTERNAL;
                }
            }
            break;

            case MAKENAMEID1('r'):
                /* - empty rules field           => scheduled meeting doesn't have rules
                 * - rules array with 1 element  => rules not modified
                 * - rules array with 2 elements => rules modified [old value, new value]
                 *   + note: if 2º value in array is empty, rules have been removed
                 */
                if (wasFieldUpdated())
                {
                    auxCS.addChange(Changeset::CHANGE_TYPE_RULES);
                }
                break;

            case EOO:
                keepParsing = false;
                *cs = std::move(auxCS);
            break;

            default:
                if (!j->storeobject())
                {
                    return API_EINTERNAL;
                }
            break;
        }
    } while (keepParsing);

    return e;
}

void MegaClient::clearSchedOccurrences(TextChat& chat)
{
    chat.clearUpdatedSchedMeetingOccurrences();
    chat.changed.schedOcurrReplace = true;
}

void MegaClient::reportInvalidSchedMeeting(const ScheduledMeeting* sched)
{
    std::string errMsg = "Ill-formed sched meeting(s)";

    sendevent(99481, errMsg.c_str());

    if (sched)
    {
        errMsg.append(" chatid:  ").append(toHandle(sched->chatid()))
            .append(" schedid: ").append(toHandle(sched->schedId()));
    }
    LOG_err << errMsg;
    assert(false);
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

bool MegaClient::startDriveMonitor()
{
#ifdef USE_DRIVE_NOTIFICATIONS
    auto notify = std::bind(&Waiter::notify, waiter);
    return mDriveInfoCollector.start(notify);
#else
    return false;
#endif
}

void MegaClient::stopDriveMonitor()
{
#ifdef USE_DRIVE_NOTIFICATIONS
    mDriveInfoCollector.stop();
#endif
}

bool MegaClient::driveMonitorEnabled()
{
#ifdef USE_DRIVE_NOTIFICATIONS
    return mDriveInfoCollector.enabled();
#else
    return false;
#endif
}

const char* MegaClient::newsignupLinkPrefix()
{
    static constexpr const char* prefix = "newsignup";
    return prefix;
}

const char* MegaClient::confirmLinkPrefix()
{
    static constexpr const char* prefix = "confirm";
    return prefix;
}

const char* MegaClient::verifyLinkPrefix()
{
    static constexpr const char* prefix = "verify";
    return prefix;
}

const char* MegaClient::recoverLinkPrefix()
{
    static constexpr const char* prefix = "recover";
    return prefix;
}

const char* MegaClient::cancelLinkPrefix()
{
    static constexpr const char* prefix = "cancel";
    return prefix;
}

#ifdef MEGA_MEASURE_CODE

extern CodeCounter::ScopeStats computeSyncSequencesStats;

std::string MegaClient::PerformanceStats::report(bool reset, HttpIO* httpio, Waiter* waiter, const RequestDispatcher& reqs)
{
    std::ostringstream s;
    s << prepareWait.report(reset) << "\n"
        << doWait.report(reset) << "\n"
        << checkEvents.report(reset) << "\n"
        << execFunction.report(reset) << "\n"
        << megaapiSendPendingTransfers.report(reset) << "\n"
        << transferslotDoio.report(reset) << "\n"
        << execdirectreads.report(reset) << "\n"
        << transferComplete.report(reset) << "\n"
        << dispatchTransfers.report(reset) << "\n"
        << applyKeys.report(reset) << "\n"
        << scProcessingTime.report(reset) << "\n"
        << csResponseProcessingTime.report(reset) << "\n"
        << csSuccessProcessingTime.report(reset) << "\n"
#ifdef ENABLE_SYNC
        << recursiveSyncTime.report(reset) << "\n"
        << computeSyncTripletsTime.report(reset) << "\n"
        << computeSyncSequencesStats.report(reset) << "\n"
        << ScanService::syncScanTime.report(reset) << "\n"
        << inferSyncTripletsTime.report(reset) << "\n"
        << g_compareUtfTimings.report(reset) << "\n"
        << syncItem.report(reset) << "\n"
        << syncItemCheckMove.report(reset) << "\n"
        << syncItemXXX.report(reset) << "\n"
        << syncItemXXF.report(reset) << "\n"
        << syncItemXSX.report(reset) << "\n"
        << syncItemXSF.report(reset) << "\n"
        << syncItemCXX.report(reset) << "\n"
        << syncItemCXF.report(reset) << "\n"
        << syncItemCSX.report(reset) << "\n"
        << syncItemCSF.report(reset) << "\n"
        << clientThreadActions.report(reset) << "\n"
#endif
        << " cs Request waiting time: " << csRequestWaitTime.report(reset) << "\n"
        << " cs requests sent/received: " << reqs.csRequestsSent << "/" << reqs.csRequestsCompleted << " batches: " << reqs.csBatchesSent << "/" << reqs.csBatchesReceived << "\n"
        << " transfers active time: " << transfersActiveTime.report(reset) << "\n"
        << " transfer starts/finishes: " << transferStarts << " " << transferFinishes << "\n"
        << " transfer temperror/fails: " << transferTempErrors << " " << transferFails << "\n"
        << " nowait reason: immedate: " << prepwaitImmediate << " zero: " << prepwaitZero << " httpio: " << prepwaitHttpio << " fsaccess: " << prepwaitFsaccess << " nonzero waits: " << nonzeroWait << "\n";
    if (auto curlhttpio = dynamic_cast<CurlHttpIO*>(httpio))
    {
        s << curlhttpio->countCurlHttpIOAddevents.report(reset) << "\n"
#ifdef MEGA_USE_C_ARES
            << curlhttpio->countAddAresEventsCode.report(reset) << "\n"
#endif
            << curlhttpio->countAddCurlEventsCode.report(reset) << "\n"
#ifdef MEGA_USE_C_ARES
            << curlhttpio->countProcessAresEventsCode.report(reset) << "\n"
#endif
            << curlhttpio->countProcessCurlEventsCode.report(reset) << "\n";
    }
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

m_time_t MegaClient::MyAccountData::getTimeLeft()
{
    auto timeleft = mProUntil - static_cast<m_time_t>(std::time(nullptr));
    auto isuserpro = mProLevel > AccountType::ACCOUNT_TYPE_FREE;

    return ( isuserpro ? timeleft : -1);
};

dstime MegaClient::overTransferQuotaBackoff(HttpReq* req)
{
    bool isuserpro = this->mMyAccount.getProLevel() > AccountType::ACCOUNT_TYPE_FREE;

    // if user is pro, subscription's remaining time is used
    // otherwise, use limit per IP coming from the header X-MEGA-Time-Left response header
    m_time_t timeleft = (isuserpro) ? this->mMyAccount.getTimeLeft() : req->timeleft;

    // send event only for negative timelefts received in the request header
    if (!isuserpro && (timeleft < 0))
    {
        sendevent(99408, "Overquota without timeleft", 0);
    }

    dstime backoff;
    if (timeleft > 0)
    {
        backoff = dstime(timeleft * 10);
    }
    else
    {
        // default retry interval
        backoff = MegaClient::DEFAULT_BW_OVERQUOTA_BACKOFF_SECS * 10;
    }

    return backoff;
}


//
// Sets and Elements
//

void MegaClient::putSet(Set&& s, std::function<void(Error, const Set*)> completion)
{
    string encrSetKey;
    std::unique_ptr<string> encrAttrs;

    // create Set
    if (s.id() == UNDEF)
    {
        if (s.type() >= Set::TYPE_SIZE)
        {
            LOG_err << "Sets: Invalid Set type " << static_cast<unsigned int>(s.type()) << ". Maximum valid type is "
                    << static_cast<unsigned int>(Set::TYPE_SIZE) - 1;
            if (completion) completion(API_EARGS, nullptr);
            return;
        }

        // generate AES-128 Set key
        encrSetKey = rng.genstring(SymmCipher::KEYLENGTH);
        s.setKey(encrSetKey);

        // encrypt Set key with master key
        if (!key.cbc_encrypt((byte*)&encrSetKey[0], encrSetKey.size())) // in c++17 and beyond it should use encrSetKey.data()
        {
            LOG_err << "Sets: Failed to encrypt Set key with master key.";
            if (completion)
            {
                completion(API_EKEY, nullptr);
            }
            return;
        }

        if (s.hasAttrs())
        {
            if (s.cover() != UNDEF)
            {
                LOG_err << "Sets: Cover cannot be set for a newly created Set.";
                if (completion)
                    completion(API_EARGS, nullptr);
                return;
            }
            string enc = s.encryptAttributes([this](const string_map& a, const string& k) { return encryptAttrs(a, k); });
            encrAttrs.reset(new string(std::move(enc)));
        }
    }
    // update Set
    else
    {
        if (!s.hasAttrs()) // should either remove all attrs or update [some of] them
        {
            LOG_err << "Sets: Nothing to update.";
            if (completion)
                completion(API_EARGS, nullptr);
            return;
        }

        auto it = mSets.find(s.id());
        if (it == mSets.end())
        {
            LOG_err << "Sets: Failed to update Set (not found).";
            if (completion)
                completion(API_ENOENT, nullptr);
            return;
        }

        if (s.cover() != UNDEF && !getSetElement(s.id(), s.cover()))
        {
            LOG_err << "Sets: Requested cover was not an Element of Set " << toHandle(s.id());
            if (completion)
                completion(API_EARGS, nullptr);
            return;
        }

        // copy the details that won't change
        const Set& setToBeUpdated = it->second;
        s.setKey(setToBeUpdated.key());
        s.setUser(setToBeUpdated.user());
        s.rebaseAttrsOn(setToBeUpdated);
        s.setPublicId(setToBeUpdated.publicId());
        s.setType(setToBeUpdated.type());

        string enc = s.encryptAttributes([this](const string_map& a, const string& k) { return encryptAttrs(a, k); });
        encrAttrs.reset(new string(std::move(enc)));
    }

    reqs.add(new CommandPutSet(this, std::move(s), std::move(encrAttrs), std::move(encrSetKey), completion));
}

void MegaClient::removeSet(handle sid, std::function<void(Error)> completion)
{
    if (getSet(sid))
    {
        reqs.add(new CommandRemoveSet(this, sid, completion));
    }
    else if (completion)
    {
        completion(API_ENOENT);
    }
}

void MegaClient::putSetElements(vector<SetElement>&& els, std::function<void(Error, const vector<const SetElement*>*, const vector<int64_t>*)> completion)
{
    // set-id is required
    assert(!els.empty() && els.front().set() != UNDEF);

    // make sure Set id is valid
    const Set* existingSet = (els.empty() || els.front().set() == UNDEF) ? nullptr : getSet(els.front().set());
    if (!existingSet)
    {
        LOG_err << "Sets: Set not found when adding bulk Elements";
        if (completion)
        {
            completion(API_ENOENT, nullptr, nullptr);
        }
        return;
    }

    // build encrypted details
    vector<StringPair> encrDetails(els.size()); // vector < {encrypted attrs, encrypted key} >
    for (size_t i = 0u; i < els.size(); ++i)
    {
        SetElement& el = els[i];

        std::shared_ptr<Node> n = nodebyhandle(el.node());
        if (!n || !n->keyApplied() || !n->nodecipher() || n->attrstring || n->type != FILENODE)
        {
            // if file node was invalid, reset it and let the API return error for it, to allow the other Elements to be created
            el.setNode(UNDEF);
        }
        else
        {
            el.setKey(n->nodekey());
            assert(el.key().size() == FILENODEKEYLENGTH);

            // encrypt element.key with set.key
            byte encryptBuffer[FILENODEKEYLENGTH];
            std::copy_n(el.key().begin(), sizeof(encryptBuffer), encryptBuffer);
            tmpnodecipher.setkey(&existingSet->key());
            if (!tmpnodecipher.cbc_encrypt(encryptBuffer, sizeof(encryptBuffer)))
            {
                LOG_err << "Sets: Failed to CBC encrypt an Element key with Set key";
                if (completion)
                {
                    completion(API_EKEY, nullptr, nullptr);
                }
                return;
            }

            auto& ed = encrDetails[i];
            ed.second.assign(reinterpret_cast<char*>(encryptBuffer), sizeof(encryptBuffer));

            if (el.hasAttrs())
            {
                ed.first = el.encryptAttributes([this](const string_map& a, const string& k) { return encryptAttrs(a, k); });
            }
        }
    }

    reqs.add(new CommandPutSetElements(this, std::move(els), std::move(encrDetails), completion));
}


void MegaClient::putSetElement(SetElement&& el, std::function<void(Error, const SetElement*)> completion)
{
    // setId is required
    assert(el.set() != UNDEF);

    // make sure Set id is valid
    const Set* existingSet = el.set() == UNDEF ? nullptr : getSet(el.set());
    if (!existingSet)
    {
        LOG_err << "Sets: Set not found when adding or updating Element";
        if (completion)
            completion(API_ENOENT, nullptr);
        return;
    }

    const SetElement* existingElement = nullptr;

    // copy element.key from nodekey (only for new Element)
    string encrKey;
    if (el.id() == UNDEF)
    {
        std::shared_ptr<Node> n = nodebyhandle(el.node());
        error e = !n ? API_ENOENT
                     : (!n->keyApplied() || !n->nodecipher() || n->attrstring ? API_EKEY
                        : (n->type != FILENODE ? API_EARGS : API_OK));
        if (e != API_OK)
        {
            LOG_err << "Sets: Invalid node for Element";
            if (completion)
                completion(e, nullptr);
            return;
        }

        el.setKey(n->nodekey());
        assert(el.key().size() == FILENODEKEYLENGTH);

        // encrypt element.key with set.key
        byte encryptBuffer[FILENODEKEYLENGTH];
        std::copy_n(el.key().begin(), sizeof(encryptBuffer), encryptBuffer);
        tmpnodecipher.setkey(&existingSet->key());
        if (!tmpnodecipher.cbc_encrypt(encryptBuffer, sizeof(encryptBuffer)))
        {
            LOG_err << "Sets: Failed to CBC encrypt Element key with Set key";
            if (completion)
            {
                completion(API_EKEY, nullptr);
            }
            return;
        }
        encrKey.assign((char*)encryptBuffer, sizeof(encryptBuffer));
    }
    // get element.key from existing element (only when updating attributes)
    else if (el.hasAttrs())
    {
        existingElement = getSetElement(el.set(), el.id());
        if (!existingElement)
        {
            LOG_err << "Sets: Element not found when updating Element: " << toHandle(el.id());
            if (completion)
                completion(API_ENOENT, nullptr);
            return;
        }

        el.setKey(existingElement->key());
    }

    // store element.attrs to TLV, and encrypt with element.key (copied from nodekey)
    std::unique_ptr<string> encrAttrs;
    if (el.hasAttrs())
    {
        if (existingElement && existingElement->hasAttrs())
        {
            el.rebaseAttrsOn(*existingElement);
            // the request to clear the last attribute of an existing Element must be remembered, so that
            // after a successful update, attrs of the existing Element will be cleared
            el.setAttrsClearedByLastUpdate(!el.hasAttrs());
        }

        string enc = el.encryptAttributes([this](const string_map& a, const string& k) { return encryptAttrs(a, k); });
        encrAttrs.reset(new string(std::move(enc)));
    }

    reqs.add(new CommandPutSetElement(this, std::move(el), std::move(encrAttrs), std::move(encrKey), completion));
}

void MegaClient::removeSetElements(handle sid, vector<handle>&& eids, std::function<void(Error, const vector<int64_t>*)> completion)
{
    // set-id is required
    assert(sid != UNDEF && !eids.empty());

    // make sure Set id is valid
    const Set* existingSet = (eids.empty() || sid == UNDEF) ? nullptr : getSet(sid);
    if (!existingSet)
    {
        LOG_err << "Sets: Invalid request data when removing bulk Elements";
        if (completion)
        {
            completion(API_ENOENT, nullptr);
        }
        return;
    }

    // Do not validate Element ids here. Let the API return error for invalid ones,
    // to allow valid ones to be removed.

    reqs.add(new CommandRemoveSetElements(this, sid, std::move(eids), completion));
}

void MegaClient::removeSetElement(handle sid, handle eid, std::function<void(Error)> completion)
{
    if (!getSetElement(sid, eid))
    {
        if (completion)
        {
            completion(API_ENOENT);
        }
        return;
    }

    reqs.add(new CommandRemoveSetElement(this, sid, eid, completion));
}

bool MegaClient::procaesp(JSON& j)
{
    bool ok = j.enterobject();
    if (ok)
    {
        map<handle, Set> newSets;
        map<handle, elementsmap_t> newElements;
        ok &= (readSetsAndElements(j, newSets, newElements) == API_OK);

        if (ok)
        {
            // save new data
            mSets.swap(newSets);
            mSetElements.swap(newElements);
        }

        ok &= j.leaveobject();
    }

    return ok;
}

error MegaClient::readSetsAndElements(JSON& j, map<handle, Set>& newSets, map<handle, elementsmap_t>& newElements)
{
    std::unique_ptr<std::map<handle, SetElement::NodeMetadata>> nodeData;

    for (bool loopAgain = true; loopAgain;)
    {
        switch (j.getnameid())
        {
        case MAKENAMEID1('s'):
        {
            // reuse this in "aft" (fetch-Set command) and "aesp" (in gettree/fetchnodes/"f" command):
            // "aft" will return a single Set for "s", while "aesp" will return an array of Sets
            bool enteredSetArray = j.enterarray();

            error e = readSets(j, newSets);
            if (e != API_OK) return e;

            if (enteredSetArray)
            {
                j.leavearray();
            }

            break;
        }

        case MAKENAMEID1('e'):
        {
            error e = readElements(j, newElements);
            if (e != API_OK) return e;
            break;
        }

        case MAKENAMEID1('n'):
        {
            nodeData.reset(new std::map<handle, SetElement::NodeMetadata>());
            error e = readAllNodeMetadata(j, *nodeData);
            if (e != API_OK) return e;
            break;
        }

        case MAKENAMEID1('p'):
        {
            // precondition: sets which ph is coming are already read and in memory
            if (!newSets.empty())
            {
                error e = readSetsPublicHandles(j, newSets);
                if (e != API_OK) return e;
            }
            break;
        }

        default: // skip unknown member
            if (!j.storeobject())
            {
                return API_EINTERNAL;
            }
            break;

        case EOO:
            loopAgain = false;
            break;
        }
    }

    // decrypt data, and confirm that all elements are valid
#ifndef NDEBUG
    size_t elCount =
#endif
    decryptAllSets(newSets, newElements, nodeData.get());

    // check for orphan Elements, it should not happen
    assert(elCount == [&newElements]() { size_t c = 0; for (const auto& els : newElements) c += els.second.size(); return c; } ());

    return API_OK;
}

size_t MegaClient::decryptAllSets(map<handle, Set>& newSets, map<handle, elementsmap_t>& newElements, map<handle, SetElement::NodeMetadata>* nodeData)
{
    size_t elCount = 0;

    for (auto itS = newSets.begin(); itS != newSets.end();)
    {
        error e = decryptSetData(itS->second);
        if (e != API_OK)
        {
            // skip this Set and its Elements
            // allow execution to continue, including the test for this scenario
            newElements.erase(itS->first);
            itS = newSets.erase(itS);
            continue;
        }

        auto itEls = newElements.find(itS->first);
        if (itEls != newElements.end())
        {
            for (auto itE = itEls->second.begin(); itE != itEls->second.end();)
            {
                // decrypt element key and attrs
                e = decryptElementData(itE->second, itS->second.key());
                if (e != API_OK)
                {
                    LOG_err << "Failed to decrypt element attributes. "
                            << "Element id = " << toHandle(itE->first) << ", Element key << " << Base64::btoa(itE->second.key())
                            << ", Set id = " << toHandle(itS->first) << ", Set key = " << Base64::btoa(itS->second.key()) << ", e = " << e;
                    assert(false && "failed to decrypt Element attributes"); // failed to decrypt Element attributes

                    itE = itEls->second.erase(itE);
                    continue;
                }

                // fill in node attributes in case of having foreign node
                if (nodeData)
                {
                    auto itNode = nodeData->find(itE->second.node());
                    if (itNode != nodeData->end())
                    {
                        SetElement::NodeMetadata& nodeMeta = itNode->second;

                        if (!nodeMeta.at.empty() && decryptNodeMetadata(nodeMeta, itE->second.key()))
                        {
                            itE->second.setNodeMetadata(std::move(nodeMeta));
                        }

                        nodeData->erase(itNode);
                    }

                    if (!itE->second.nodeMetadata())
                    {
                        LOG_err << "Invalid node for element. "
                                << "Element id = " << toHandle(itE->first) << ", Element key << " << Base64::btoa(itE->second.key())
                                << ", Set id = " << toHandle(itS->first) << ", Set key = " << Base64::btoa(itS->second.key());
                        itE = itEls->second.erase(itE);
                        continue;
                    }
                }

                ++elCount;
                ++itE;
            }
        }

        ++itS;
    }

    return elCount;
}

error MegaClient::decryptSetData(Set& s)
{
    if (!s.id() || s.id() == UNDEF)
    {
        LOG_err << "Sets: Missing mandatory Set data";
        return API_EINTERNAL;
    }

    if (inPublicSetPreview())
    {
        if ((mPreviewSet->mSet.id() == UNDEF)    // first time receiving Set data for preview Set
            || mPreviewSet->mSet.id() == s.id()) // followup receiving Set data for preview Set
        {
            s.setKey(mPreviewSet->mPublicKey); // already decrypted
            s.setPublicId(mPreviewSet->mPublicId);
        }
        else
        {
            LOG_err << "Sets: Data for Set |" << toHandle(s.id()) << "| fetched while public Set preview mode active for Set |"
                    << toHandle(mPreviewSet->mSet.id()) << "|\n";
            return API_EARGS;
        }
    }
    else
    {
        if (s.key().empty())
        {
            LOG_err << "Sets: Missing mandatory Set key";
            return API_EINTERNAL;
        }

        // decrypt Set key using the master key
        s.setKey(decryptKey(s.key(), key));
    }

    // decrypt attrs
    if (s.hasEncrAttrs())
    {
        auto decryptFunc = [this](const string& in, const string& k, string_map& out) { return decryptAttrs(in, k, out); };
        if (!s.decryptAttributes(decryptFunc))
        {
            LOG_err << "Sets: Unable to decrypt Set attrs " << toHandle(s.id());
            return API_EINTERNAL;
        }
    }

    return API_OK;
}

error MegaClient::decryptElementData(SetElement& el, const string& setKey)
{
    if (!el.id() || el.id() == UNDEF || !el.node() || el.node() == UNDEF || el.key().empty())
    {
        LOG_err << "Sets: Missing mandatory Element data [el.id = " << el.id() << ", el.node = " << el.node() << ", el.key = " << el.key() << "]";
        return API_EINTERNAL;
    }

    tmpnodecipher.setkey(&setKey);
    el.setKey(decryptKey(el.key(), tmpnodecipher));

    // decrypt attrs
    if (el.hasEncrAttrs())
    {
        auto decryptFunc = [this](const string& in, const string& k, string_map& out) { return decryptAttrs(in, k, out); };
        if (!el.decryptAttributes(decryptFunc))
        {
            LOG_err << "Sets: Unable to decrypt Element attrs " << toHandle(el.id());
            return API_EINTERNAL;
        }
    }

    return API_OK;
}

string MegaClient::decryptKey(const string& k, SymmCipher& cipher) const
{
    unique_ptr<byte[]> decrKey(new byte[k.size()]{ 0 });
    std::copy_n(k.begin(), k.size(), decrKey.get());
    if (!cipher.cbc_decrypt(decrKey.get(), k.size()))
    {
        LOG_err << "Failed to CBC decrypt key";
        return string();
    }
    return string((char*)decrKey.get(), k.size());
}

string MegaClient::encryptAttrs(const string_map& attrs, const string& encryptionKey)
{
    if (attrs.empty())
    {
        return string();
    }

    if (!tmpnodecipher.setkey(&encryptionKey))
    {
        LOG_err << "Sets: Failed to use cipher key when encrypting attrs";
        return string();
    }

    TLVstore tlvRecords;
    for (const auto& a : attrs)
    {
        tlvRecords.set(a.first, a.second);
    }

    unique_ptr<string> encrAttrs(tlvRecords.tlvRecordsToContainer(rng, &tmpnodecipher));

    if (!encrAttrs || encrAttrs->empty())
    {
        LOG_err << "Sets: Failed to write name to TLV container";
        return string();
    }

    return *encrAttrs;
}

bool MegaClient::decryptAttrs(const string& attrs, const string& decrKey, string_map& output)
{
    if (attrs.empty())
    {
        output.clear();
        return true;
    }

    assert(decrKey.size() == SymmCipher::KEYLENGTH || decrKey.size() == FILENODEKEYLENGTH);

    if (!tmpnodecipher.setkey(&decrKey))
    {
        LOG_err << "Sets: Failed to assign key to cipher when decrypting attrs";
        return false;
    }

    unique_ptr<TLVstore> sTlv(TLVstore::containerToTLVrecords(&attrs, &tmpnodecipher));
    if (!sTlv)
    {
        LOG_err << "Sets: Failed to build TLV container of attrs";
        return false;
    }

    output = *sTlv->getMap();

    return true;
}

error MegaClient::readSets(JSON& j, map<handle, Set>& sets)
{
    while (j.enterobject())
    {
        Set s;
        error e = readSet(j, s);
        if (e)
        {
            return e;
        }
        sets[s.id()] = std::move(s);

        j.leaveobject();
    }

    return API_OK;
}

error MegaClient::readSet(JSON& j, Set& s)
{
    for (;;)
    {
        switch (j.getnameid())
        {
        case MAKENAMEID2('i', 'd'):
            s.setId(j.gethandle(MegaClient::SETHANDLE));
            break;

        case MAKENAMEID2('p', 'h'):
        {
            s.setPublicId(j.gethandle(MegaClient::PUBLICSETHANDLE)); // overwrite if existed
            break;
        }

        case MAKENAMEID2('a', 't'):
        {
            string attrs;
            j.copystring(&attrs, j.getvalue()); // B64 encoded
            if (!attrs.empty())
            {
                attrs = Base64::atob(attrs);
            }
            s.setEncryptedAttrs(std::move(attrs)); // decrypt them after reading everything
            break;
        }

        case MAKENAMEID1('u'):
            s.setUser(j.gethandle(MegaClient::USERHANDLE));
            break;

        case MAKENAMEID1('k'): // used to encrypt attrs; encrypted itself with owner's key
        {
            string setKey;
            j.copystring(&setKey, j.getvalue()); // B64 encoded
            s.setKey(Base64::atob(setKey));
            break;
        }

        case MAKENAMEID2('t', 's'):
            s.setTs(j.getint());
            break;

        case MAKENAMEID3('c', 't', 's'):
            s.setCTs(j.getint());
            break;

        case MAKENAMEID1('t'):
            s.setType(static_cast<Set::SetType>(j.getint()));
            break;

        default: // skip unknown member
            if (!j.storeobject())
            {
                LOG_err << "Sets: Failed to parse Set";
                return API_EINTERNAL;
            }
            break;

        case EOO:
            return API_OK;
        }
    }
}

error MegaClient::readElements(JSON& j, map<handle, elementsmap_t>& elements)
{
    if (!j.enterarray())
    {
        return API_EINTERNAL;
    }

    while (j.enterobject())
    {
        SetElement el;
        error e = readElement(j, el);
        if (e)
        {
            j.leavearray();
            return e;
        }
        handle sid = el.set();
        handle eid = el.id();
        elements[sid].emplace(eid, std::move(el));

        j.leaveobject();
    }

    j.leavearray();
    return API_OK;
}

error MegaClient::readElement(JSON& j, SetElement& el)
{
    for (;;)
    {
        switch (j.getnameid())
        {
        case MAKENAMEID2('i', 'd'):
            el.setId(j.gethandle(MegaClient::SETELEMENTHANDLE));
            break;

        case MAKENAMEID1('s'):
            el.setSet(j.gethandle(MegaClient::SETHANDLE));
            break;

        case MAKENAMEID1('h'):
            el.setNode(j.gethandle(MegaClient::NODEHANDLE));
            break;

        case MAKENAMEID2('a', 't'):
        {
            string elementAttrs;
            j.copystring(&elementAttrs, j.getvalue());
            if (!elementAttrs.empty())
            {
                elementAttrs = Base64::atob(elementAttrs);
            }
            el.setEncryptedAttrs(std::move(elementAttrs)); // decrypt them after reading everything
            break;
        }

        case MAKENAMEID1('o'):
            el.setOrder(j.getint());
            break;

        case MAKENAMEID2('t', 's'):
            el.setTs(j.getint());
            break;

        case MAKENAMEID1('k'):
        {
            string elementKey;
            j.copystring(&elementKey, j.getvalue());
            if (!elementKey.empty())
            {
                elementKey = Base64::atob(elementKey);
            }
            el.setKey(std::move(elementKey));
            break;
        }

        default: // skip unknown member
            if (!j.storeobject())
            {
                LOG_err << "Sets: Failed to parse Element";
                return API_EINTERNAL;
            }
            break;

        case EOO:
            return API_OK;
        }
    }
}

error MegaClient::readAllNodeMetadata(JSON& j, map<handle, SetElement::NodeMetadata>& nodes)
{
    if (!j.enterarray())
    {
        return API_EINTERNAL;
    }

    while (j.enterobject())
    {
        SetElement::NodeMetadata eln;
        error e = readSingleNodeMetadata(j, eln);
        if (e)
        {
            j.leavearray();
            return e;
        }
        nodes.emplace(eln.h, std::move(eln));

        j.leaveobject();
    }

    j.leavearray();
    return API_OK;
}

error MegaClient::readSingleNodeMetadata(JSON& j, SetElement::NodeMetadata& eln)
{
    for (;;)
    {
        switch (j.getnameid())
        {
        case MAKENAMEID1('h'):
            eln.h = j.gethandle(MegaClient::NODEHANDLE);
            break;

        case MAKENAMEID1('u'):
            eln.u = j.gethandle(MegaClient::USERHANDLE);
            break;

        case MAKENAMEID1('s'):
            eln.s = j.getint();
            break;

        case MAKENAMEID2('a', 't'):
            if (!j.storeobject(&eln.at))
            {
                LOG_err << "Sets: Failed to read node attributes";
                assert(false);
                return API_EINTERNAL;
            }
            break;

        case MAKENAMEID2('f', 'a'):
            if (!j.storeobject(&eln.fa))
            {
                LOG_err << "Sets: Failed to read file attributes";
                assert(false);
                return API_EINTERNAL;
            }
            break;

        case MAKENAMEID2('t', 's'):
            eln.ts = j.getint();
            break;

        default: // skip unknown member
            if (!j.storeobject())
            {
                LOG_err << "Sets: Failed to parse node metadata";
                return API_EINTERNAL;
            }
            break;

        case EOO:
            return API_OK;
        }
    }
}

bool MegaClient::decryptNodeMetadata(SetElement::NodeMetadata& nodeMeta, const string& key)
{
    SymmCipher* cipher = getRecycledTemporaryNodeCipher(&key);
    std::unique_ptr<byte[]> buf;
    buf.reset(Node::decryptattr(cipher, nodeMeta.at.c_str(), nodeMeta.at.size()));
    if (!buf)
    {
        LOG_err << "Decrypting node attributes failed. Node Handle = " << toNodeHandle(nodeMeta.h);
        return false;
    }

    // all good, let's parse the attribute string
    JSON attrJson;
    attrJson.begin(reinterpret_cast<const char*>(buf.get()) + 5); // skip "MEGA{" prefix

    for (bool jsonHasData = true; jsonHasData;)
    {
        switch (attrJson.getnameid())
        {
        case 'c':
            if (!attrJson.storeobject(&nodeMeta.fingerprint))
            {
                LOG_err << "Reading node fingerprint failed. Node Handle = " << toNodeHandle(nodeMeta.h);
            }
            break;

        case 'n':
            if (!attrJson.storeobject(&nodeMeta.filename))
            {
                LOG_err << "Reading node filename failed. Node Handle = " << toNodeHandle(nodeMeta.h);
            }
            break;

        case EOO:
            jsonHasData = false;
            break;

        default:
            if (!attrJson.storeobject())
            {
                LOG_err << "Skipping unexpected node attribute failed. Node Handle = " << toNodeHandle(nodeMeta.h);
            }
        }
    }

    nodeMeta.at.clear();

    return true;
}

const Set* MegaClient::getSet(handle sid) const
{
    auto it = mSets.find(sid);
    return it == mSets.end() ? nullptr : &it->second;
}

const Set* MegaClient::addSet(Set&& a)
{
    handle sid = a.id();
    auto add = mSets.emplace(sid, std::move(a));
    assert(add.second);

    if (add.second) // newly inserted
    {
        Set& added = add.first->second;
        added.setChanged(Set::CH_NEW);
        notifyset(&added);
    }

    return &add.first->second;
}

void MegaClient::fixSetElementWithWrongKey(const Set& s)
{
    const auto els = getSetElements(s.id());
    if (!els) return;

    vector<SetElement> newEls;
    vector<handle> taintedEls;
    const auto hasWrongKey = [](const SetElement& el)
    {
        // Elements created by Webclient prior to a certain ts had invalid keys.
        // Some had keys of wrong size, others had keys of correct size but still invalid.
        // A criteria deemed good enough to spot the latter was to check for ts prior to a known value
        // and having no name - Webclient would create Elements without 'name' attribute being set.
        return el.key().size() != static_cast<size_t>(FILENODEKEYLENGTH) ||
               (el.ts() <= 1695340800 && el.name().empty());
    };
    for (auto& p : *els) // candidate to paral in >C++17 via algorithms
    {
        const SetElement& e = p.second;
        if (hasWrongKey(e))
        {
            LOG_warn << "Sets: SetElement " << toHandle(e.id()) << " from Set " << toHandle(s.id()) << " has invalid key";
            taintedEls.push_back(e.id());
            newEls.emplace_back(e);
        }
    }

    if (taintedEls.empty()) return;

    const auto logResult = [this](Error e, const vector<int64_t>* results, const std::string& msg)
    {
        if (e == API_OK && (!results ||
            std::all_of(begin(*results), end(*results), [](int64_t r) { return r == API_OK; })))
        {
            const std::string m = "Sets: SetElements with wrong key " + msg + " successfully";
            LOG_debug << m;
            sendevent(99477, m.c_str());
        }
        else
        {
            const std::string m = "Sets: Error: SetElements with wrong key failed to be " + msg;
            LOG_warn << m;
            sendevent(99478, m.c_str());
        }
    };
    // removal must take place before because there can't be 2 SetElements with the same node
    removeSetElements(s.id(), std::move(taintedEls),
                      [logResult](Error e, const vector<int64_t>* results) { logResult(e, results, "removed"); });

    putSetElements(std::move(newEls), [logResult](Error e, const vector<const SetElement*>*, const vector<int64_t>* results)
        { logResult(e, results, "created"); });
}

bool MegaClient::updateSet(Set&& s)
{
    auto it = mSets.find(s.id());
    if (it != mSets.end())
    {
        if (it->second.updateWith(std::move(s)))
        {
            notifyset(&it->second);
        }

        return true; // return true if found, even if nothing was updated
    }

    return false;
}

bool MegaClient::deleteSet(handle sid)
{
    auto it = mSets.find(sid);
    if (it != mSets.end())
    {
        it->second.setChanged(Set::CH_REMOVED);
        notifyset(&it->second);

        return true;
    }

    return false;
}

unsigned MegaClient::getSetElementCount(handle sid) const
{
    auto* elements = getSetElements(sid);
    return elements ? static_cast<unsigned>(elements->size()) : 0u;
}

const SetElement* MegaClient::getSetElement(handle sid, handle eid) const
{
    auto* elements = getSetElements(sid);
    if (elements)
    {
        auto ite = elements->find(eid);
        if (ite != elements->end())
        {
            return &(ite->second);
        }
    }

    return nullptr;
}

const elementsmap_t* MegaClient::getSetElements(handle sid) const
{
    auto itS = mSetElements.find(sid);
    return itS == mSetElements.end() ? nullptr : &itS->second;
}

bool MegaClient::deleteSetElement(handle sid, handle eid)
{
    auto its = mSetElements.find(sid);
    if (its != mSetElements.end())
    {
        auto ite = its->second.find(eid);
        if (ite != its->second.end())
        {
            ite->second.setChanged(SetElement::CH_EL_REMOVED);
            notifysetelement(&ite->second);
            return true;
        }
    }

    return false;
}

const SetElement* MegaClient::addOrUpdateSetElement(SetElement&& el)
{
    handle sid = el.set();
    assert(sid != UNDEF);
    handle eid = el.id();

    auto itS = mSetElements.find(sid);
    if (itS != mSetElements.end())
    {
        auto& elements = itS->second;
        auto ite = elements.find(eid);
        if (ite != elements.end())
        {
            if (ite->second.updateWith(std::move(el)))
            {
                notifysetelement(&ite->second);
            }
            return &ite->second;
        }
    }

    // not found, add it
    auto add = mSetElements[sid].emplace(eid, std::move(el));
    assert(add.second);

    SetElement& added = add.first->second;
    added.setChanged(SetElement::CH_EL_NEW);
    notifysetelement(&added);

    return &added;
}

void MegaClient::sc_asp()
{
    Set s;
    error e = readSet(jsonsc, s);
    if (e != API_OK)
    {
        LOG_err << "Sets: Failed to parse `asp` action packet";
        return;
    }

    // Set key is always received, let's use that
    if (decryptSetData(s) != API_OK)
    {
        LOG_err << "Sets: failed to decrypt attributes from `asp`. Skipping Set: " << toHandle(s.id());
        return;
    }

    auto it = mSets.find(s.id());
    if (it == mSets.end()) // add new
    {
        addSet(std::move(s));
    }
    else // update existing Set
    {
        Set& existing = it->second;
        if (s.key() != existing.key())
        {
            LOG_err << "Sets: key differed from existing one. Skipping Set:" << toHandle(s.id());
            sendevent(99458, "Set key has changed");
            assert(false);
            return;
        }

        // copy any existing data not received via AP
        s.setPublicId(existing.publicId());

        if (existing.updateWith(std::move(s)))
        {
            notifyset(&existing);
        }
    }
}

void MegaClient::sc_asr()
{
    handle setId = UNDEF;
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
        case MAKENAMEID2('i', 'd'):
            {
            setId = jsonsc.gethandle(MegaClient::SETHANDLE);
            break;
            }

        case EOO:
            if (ISUNDEF(setId) || !deleteSet(setId))
            {
                LOG_err << "Sets: Failed to remove Set in `asr` action packet for Set "
                        << toHandle(setId);
            }
            return;

        default:
            if (!jsonsc.storeobject())
            {
                LOG_warn << "Sets: Failed to parse `asr` action packet";
                return;
            }
        }
    }
}

void MegaClient::sc_aep()
{
    SetElement el;
    if (readElement(jsonsc, el) != API_OK)
    {
        LOG_err << "Sets: `aep` action packet: failed to parse data";
        return;
    }

    // find the Set for this Element
    Set* s = nullptr;
    auto it = mSets.find(el.set());
    if (it != mSets.end())
    {
        s = &it->second;
    }
    else
    {
        LOG_err << "Sets: `aep` action packet: failed to find Set for Element";
        return;
    }

    if (decryptElementData(el, s->key()) != API_OK)
    {
        LOG_err << "Sets: `aep` action packet: failed to decrypt Element data";
        return;
    }

    addOrUpdateSetElement(std::move(el));
}

void MegaClient::sc_aer()
{
    handle elemId = UNDEF;
    handle setId = UNDEF;

    for (;;)
    {
        switch (jsonsc.getnameid())
        {
        case MAKENAMEID2('i', 'd'):
            elemId = jsonsc.gethandle(MegaClient::SETELEMENTHANDLE);
            break;

        case MAKENAMEID1('s'):
            setId = jsonsc.gethandle(MegaClient::SETHANDLE);
            break;

          case EOO:
            if (ISUNDEF(setId) || ISUNDEF(elemId) || !deleteSetElement(setId, elemId))
            {
                LOG_err << "Sets: Failed to remove Element in `aer` action packet for Set "
                        << toHandle(setId) << " and Element " << toHandle(elemId);
            }
            return;

        default:
            if (!jsonsc.storeobject())
            {
                LOG_warn << "Sets: Failed to parse `aer` action packet";
                return;
            }
        }
    }
}

error MegaClient::readExportedSet(JSON& j, Set& s, pair<bool,m_off_t>& exportRemoved)
{
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
        case MAKENAMEID1('s'):
            s.setId(j.gethandle(MegaClient::SETHANDLE));
            break;

        case MAKENAMEID2('p', 'h'):
            s.setPublicId(j.gethandle(MegaClient::PUBLICSETHANDLE)); // overwrite if existed
            break;

        case MAKENAMEID2('t', 's'):
            s.setTs(j.getint());
            break;

        case MAKENAMEID1('r'):
            exportRemoved.first = j.getint() == 1;
            s.setPublicId(UNDEF);
            break;

        case MAKENAMEID1('c'):
            exportRemoved.second = j.getint();
            /* 0     => deleted by user
             * Other => ETD / ATD / dispute */
            break;

        default: // skip 'i' and any unknown/unexpected member
        {
            if (!j.storeobject())
            {
                LOG_err << "Sets: Failed to parse Set";
                return API_EINTERNAL;
            }

            LOG_debug << "Sets: Unknown member received in 'ass' action packet";
            break;
        }

        case EOO:
            return API_OK;

        }
    }
}

error MegaClient::readSetPublicHandle(JSON& j, map<handle, Set>& sets)
{
    handle item = UNDEF, itemPH = UNDEF;
    m_off_t ts = 0;
    for (;;)
    {
        switch (j.getnameid())
        {
        case MAKENAMEID1('s'):
            item = j.gethandle(MegaClient::SETHANDLE);
            break;

        case MAKENAMEID2('p', 'h'):
            itemPH = j.gethandle(MegaClient::PUBLICSETHANDLE);
            break;

        case MAKENAMEID2('t', 's'):
            ts = j.getint();
            break;

        default: // skip any unknown/unexpected member
        {
            if (!j.storeobject())
            {
                LOG_err << "Sets: Failed to parse public handles for Sets";
                return API_EINTERNAL;
            }

            LOG_debug << "Sets: Unknown member received in 'aesp' for an 'f' command";
            break;
        }

        case EOO:
            assert(item != UNDEF && itemPH != UNDEF);
            if (sets.find(item) != end(sets))
            {
                sets[item].setPublicId(itemPH);
                sets[item].setTs(ts);
            }
            else LOG_warn << "Sets: Set handle " << toHandle(item) << " not found in user's Sets";

            return API_OK;
        }
    }
}

error MegaClient::readSetsPublicHandles(JSON& j, map<handle, Set>& sets)
{
    if (!j.enterarray()) return API_EINTERNAL;

    error e = API_OK;
    while (j.enterobject())
    {
        e = readSetPublicHandle(j, sets);
        j.leaveobject();

        if (e != API_OK) break;
    }

    j.leavearray();
    return e;
}

void MegaClient::sc_ass()
{
    Set s;
    auto exportRemoved = std::make_pair(false, static_cast<m_off_t>(0));
    const error e = readExportedSet(jsonsc, s, exportRemoved);

    if (e != API_OK)
    {
        LOG_err << "Sets: Failed to parse `ass` action packet";
        return;
    }

    const auto existingSet = mSets.find(s.id());
    if (existingSet == mSets.end())
    {
        LOG_debug << "Sets: Received action packet for Set " << toHandle(s.id())
                  << " which is unrelated to current user";
    }
    else
    {
        Set updatedSet(existingSet->second);
        updatedSet.setPublicId(s.publicId());
        updatedSet.setTs(s.ts());
        updatedSet.setChanged(Set::CH_EXPORTED);
        updateSet(std::move(updatedSet));
    }
}

bool MegaClient::isExportedSet(handle sid) const
{
    auto s = getSet(sid);
    return s && s->isExported();
}

void MegaClient::exportSet(handle sid, bool makePublic, std::function<void(Error)> completion)
{
    const auto setToBeUpdated = getSet(sid);
    if (setToBeUpdated)
    {
        if (makePublic) // legacy bug: some Element's key were set incorrectly -> repair
        {
            fixSetElementWithWrongKey(*setToBeUpdated);
        }

        if (setToBeUpdated->isExported() == makePublic) completion(API_OK);
        else
        {
            Set s(*setToBeUpdated);
            reqs.add(new CommandExportSet(this, std::move(s), makePublic, completion));
        }
    }
    else
    {
        LOG_warn << "Sets: export requested for unknown Set " << toHandle(sid);
        if (completion) completion(API_ENOENT);
    }
}

pair<error,string> MegaClient::getPublicSetLink(handle sid) const
{
    const string paramErrMsg = "Sets: Incorrect parameters to create a public link for Set " + toHandle(sid);
    const auto& setIt = mSets.find(sid);
    if (setIt == end(mSets))
    {
        LOG_err << paramErrMsg << ". Provided Set id doesn't match any owned Set";
        return make_pair(API_ENOENT, string());
    }

    const Set& s = setIt->second;
    if (!s.isExported())
    {
        LOG_err << paramErrMsg << ". Provided Set is not exported";
        return make_pair(API_ENOENT, string());
    }

    error e = API_OK;
    string url = publicLinkURL(true /*newLinkFormat*/, TypeOfLink::SET, s.publicId(), Base64::btoa(s.key()).c_str());

    if (url.empty()) e = API_EARGS;

    return make_pair(e, url);
}

void MegaClient::fetchSetInPreviewMode(std::function<void(Error, Set*, elementsmap_t*)> completion)
{
    if (!inPublicSetPreview())
    {
        LOG_err << "Sets: Fetch set request with public Set preview mode disabled";
        completion(API_EACCESS, nullptr, nullptr);
        return;
    }

    auto clientUpdateOnCompletion = [completion, this](Error e, Set* s, elementsmap_t* els)
    {
        if ((e == API_OK) && s && els && inPublicSetPreview())
        {
            auto& previewSet = mPreviewSet->mSet;
            previewSet = *s;
            auto& previewMapElements = mPreviewSet->mElements;
            previewMapElements = *els;
        }
        else if (e != API_OK && inPublicSetPreview())
        {
            stopSetPreview();
        }
        completion(e, s, els);
    };
    reqs.add(new CommandFetchSet(this, clientUpdateOnCompletion));
}

error MegaClient::fetchPublicSet(const char* publicSetLink,
                                  std::function<void(Error, Set*, elementsmap_t*)> completion)
{
    handle publicSetId = UNDEF;
    std::array<byte, SETNODEKEYLENGTH> publicSetKey;
    error e = parsepubliclink(publicSetLink, publicSetId, publicSetKey.data(), TypeOfLink::SET);
    if (e == API_OK)
    {
        assert(publicSetId != UNDEF);

        if (inPublicSetPreview())
        {
            if (mPreviewSet->mPublicId == publicSetId)
            {
                completion(API_OK, new Set(mPreviewSet->mSet), new elementsmap_t(mPreviewSet->mElements));
                return e;
            }
            else stopSetPreview();
        }

        // 1. setup member mPreviewSet: publicId, key, publicSetLink
        mPreviewSet = std::make_unique<SetLink>();
        mPreviewSet->mPublicId = publicSetId;
        mPreviewSet->mPublicKey.assign(reinterpret_cast<char*>(publicSetKey.data()), publicSetKey.size());
        mPreviewSet->mPublicLink.assign(publicSetLink);

        // 2. send `aft` command and intercept to save at mPreviewSet: Set, SetElements map
        fetchSetInPreviewMode(completion);
    }

    return e;
}

bool MegaClient::initscsets()
{
    for (auto& i : mSets)
    {
        if (!sctable->put(CACHEDSET, &i.second, &key))
        {
            return false;
        }
    }

    return true;
}

bool MegaClient::initscsetelements()
{
    for (auto& s : mSetElements)
    {
        assert(mSets.find(s.first) != mSets.end());
        if (mSets.find(s.first) == mSets.end())
        {
            LOG_err << "Sets: elements for unknown set: " << toHandle(s.first);
            continue;
        }

        for (auto& e : s.second)
        {
            if (!sctable->put(CACHEDSETELEMENT, &e.second, &key))
            {
                return false;
            }
        }
    }

    return true;
}

bool MegaClient::fetchscset(string* data, uint32_t id)
{
    auto s = Set::unserialize(data);
    if (!s)
    {
        LOG_err << "Failed - Set record read error";
        return false;
    }

    handle sid = s->id();
    auto its = mSets.emplace(sid, std::move(*s));
    assert(its.second); // insertion must have occurred
    Set& addedSet = its.first->second;
    addedSet.resetChanges();
    addedSet.dbid = id;

    return true;
}

bool MegaClient::fetchscsetelement(string* data, uint32_t id)
{
    auto el = SetElement::unserialize(data);
    if (!el)
    {
        LOG_err << "Failed - SetElement record read error";
        return false;
    }

    handle sid = el->set();
    handle eid = el->id();
    auto ite = mSetElements[sid].emplace(eid, std::move(*el));
    assert(ite.second); // insertion must have occurred
    SetElement& addedEl = ite.first->second;
    addedEl.resetChanges();
    addedEl.dbid = id;

    return true;
}

bool MegaClient::updatescsets()
{
    for (Set* s : setnotify)
    {
        assert(s->changes());
        if (!s->changes())
        {
            LOG_err << "Sets: Notifying about unchanged Set: " << toHandle(s->id());
            continue;
        }

        char base64[12];
        if (!s->hasChanged(Set::CH_REMOVED)) // add / replace / exported / exported disabled
        {
            LOG_verbose << "Adding Set to database: " << (Base64::btoa((byte*)&(s->id()), MegaClient::SETHANDLE, base64) ? base64 : "");
            if (!sctable->put(CACHEDSET, s, &key))
            {
                return false;
            }
        }
        else if (s->dbid) // remove
        {
            LOG_verbose << "Removing Set from database: " << (Base64::btoa((byte*)&(s->id()), MegaClient::SETHANDLE, base64) ? base64 : "");

            // remove all elements of this set
            auto* elements = getSetElements(s->id());
            if (elements)
            {
                for (auto& e : *elements)
                {
                    if (!sctable->del(e.second.dbid))
                    {
                        return false;
                    }
                }
                clearsetelementnotify(s->id());
                mSetElements.erase(s->id());
            }

            if (!sctable->del(s->dbid))
            {
                return false;
            }
        }
    }

    return true;
}

bool MegaClient::updatescsetelements()
{
    for (SetElement* e : setelementnotify)
    {
        assert(e->changes());
        if (!e->changes())
        {
            LOG_err << "Sets: Notifying about unchanged SetElement: " << toHandle(e->id());
            continue;
        }

        char base64[12];
        if (!e->hasChanged(SetElement::CH_EL_REMOVED)) // add / replace
        {
            if (mSets.find(e->set()) == mSets.end())
            {
                continue;
            }

            LOG_verbose << (e->hasChanged(SetElement::CH_EL_NEW) ? "Adding" : "Updating") << " SetElement to database: " << (Base64::btoa((byte*)&(e->id()), MegaClient::SETELEMENTHANDLE, base64) ? base64 : "");
            if (!sctable->put(CACHEDSETELEMENT, e, &key))
            {
                return false;
            }
        }
        else if (e->dbid) // remove
        {
            LOG_verbose << "Removing SetElement from database: " << (Base64::btoa((byte*)&(e->id()), MegaClient::SETELEMENTHANDLE, base64) ? base64 : "");
            if (!sctable->del(e->dbid))
            {
                return false;
            }
        }
    }

    return true;
}

void MegaClient::notifyset(Set* s)
{
    if (!s->notified)
    {
        s->notified = true;
        setnotify.push_back(s);
    }
}

void MegaClient::notifysetelement(SetElement* e)
{
    if (!e->notified)
    {
        e->notified = true;
        setelementnotify.push_back(e);
    }
}

void MegaClient::notifypurgesets()
{
    if (!fetchingnodes)
    {
        app->sets_updated(setnotify.data(), (int)setnotify.size());
    }

    for (auto& s : setnotify)
    {
        if (s->hasChanged(Set::CH_REMOVED))
        {
            clearsetelementnotify(s->id());
            mSetElements.erase(s->id());
            mSets.erase(s->id());
        }
        else
        {
            s->notified = false;
            s->resetChanges();
        }
    }

    setnotify.clear();
}

void MegaClient::notifypurgesetelements()
{
    if (!fetchingnodes)
    {
        app->setelements_updated(setelementnotify.data(), (int)setelementnotify.size());
    }

    for (auto& e : setelementnotify)
    {
        if (e->hasChanged(SetElement::CH_EL_REMOVED))
        {
            mSetElements[e->set()].erase(e->id());
        }
        else
        {
            e->notified = false;
            e->resetChanges();
        }
    }

    setelementnotify.clear();
}

void MegaClient::clearsetelementnotify(handle sid)
{
    for (size_t i = setelementnotify.size(); i; --i)
    {
        if (setelementnotify[i - 1]->set() == sid)
        {
            setelementnotify.erase(setelementnotify.begin() + i - 1);
        }
    }
}

void MegaClient::setProFlexi(bool newProFlexi)
{
    mProFlexi = newProFlexi;
}

void MegaClient::setEmail(User* u, const string& email)
{
    assert(u);
    if (email == u->email)
    {
        return;
    }

    mapuser(u->userhandle, email.c_str()); // update email used as index for user's map
    u->changed.email = true;
    notifyuser(u);

    if (u->userhandle == me)
    {
        reportLoggedInChanges();  // produce a callback to update cached email in MegaApp
    }
}

Error MegaClient::sendABTestActive(const char* flag, CommandABTestActive::Completion completion)
{
    reqs.add(new CommandABTestActive(this, flag, std::move(completion)));
    return API_OK;
}

/* Mega VPN methods BEGIN */
StringKeyPair MegaClient::generateVpnKeyPair()
{
    auto vpnKey = std::make_unique<ECDH>();
    if (!vpnKey->initializationOK)
    {
        LOG_err << "Initialization of keys Cu25519 and/or Ed25519 failed";
        return StringKeyPair(std::string(), std::string());
    }
    string privateKey = std::string((const char *)vpnKey->getPrivKey(), ECDH::PRIVATE_KEY_LENGTH);
    string publicKey = std::string((const char *)vpnKey->getPubKey(), ECDH::PUBLIC_KEY_LENGTH);
    return StringKeyPair(std::move(privateKey), std::move(publicKey));
}

// Call "vpnr" command.
void MegaClient::getVpnRegions(CommandGetVpnRegions::Cb&& completion)
{
    reqs.add(new CommandGetVpnRegions(this, std::move(completion)));
}

// Call "vpng" command.
void MegaClient::getVpnCredentials(CommandGetVpnCredentials::Cb&& completion)
{
    reqs.add(new CommandGetVpnCredentials(this, std::move(completion)));
}

// Call "vpnp" command.
void MegaClient::putVpnCredential(std::string&& vpnRegion, CommandPutVpnCredential::Cb&& completion)
{
    auto vpnKeyPair = generateVpnKeyPair();
    reqs.add(new CommandPutVpnCredential(this, std::move(vpnRegion), std::move(vpnKeyPair), std::move(completion)));
}

// Call "vpnd" command.
void MegaClient::delVpnCredential(int slotID, CommandDelVpnCredential::Cb&& completion)
{
    reqs.add(new CommandDelVpnCredential(this, slotID, std::move(completion)));
}

// Call "vpnc" command.
void MegaClient::checkVpnCredential(std::string&& userPubKey, CommandDelVpnCredential::Cb&& completion)
{
    reqs.add(new CommandCheckVpnCredential(this, std::move(userPubKey), std::move(completion)));
}

// Generate the credential string.
string MegaClient::generateVpnCredentialString(int clusterID,
                                               std::string&& vpnRegion,
                                               std::string&& ipv4,
                                               std::string&& ipv6,
                                               StringKeyPair&& peerKeyPair)
{
    string peerPrivateKey = Base64::btoa(peerKeyPair.privKey);
    string peerPublicKey = std::move(peerKeyPair.pubKey);

    // Base64 standard format for Peer Key Pair
    Base64::toStandard(peerPrivateKey);
    Base64::toStandard(peerPublicKey);
    assert(peerPrivateKey.size() == 4 * ((ECDH::PUBLIC_KEY_LENGTH + 2) / 3)); // Check lengths as we have keys from different sources
    assert(peerPrivateKey.size() == peerPublicKey.size());

    // Now they peer keys are valid for WireGuard and can be added to the credentials.
    string credential;
    credential.reserve(300);
    credential.append("[Interface]\n")
              .append("PrivateKey = ").append(peerPrivateKey).append("\n")
              .append("Address = ").append(ipv4).append("/32").append(", ").append(ipv6).append("/128\n")
              .append("DNS = 8.8.8.8, 2001:4860:4860::8888\n\n")
              .append("[Peer]\n")
              .append("PublicKey = ").append(peerPublicKey).append("\n")
              .append("AllowedIPs = 0.0.0.0/0, ::/0\n")
              .append("Endpoint = ").append(vpnRegion).append(".vpn");
    if (clusterID > 1)
    {
        credential.append(std::to_string(clusterID));
    }
    credential.append(".mega.nz:51820");
    return credential;
}
/* Mega VPN methods END */

void MegaClient::fetchCreditCardInfo(CommandFetchCreditCardCompletion completion)
{
    reqs.add(new CommandFetchCreditCard(this, std::move(completion)));
}

const char* const MegaClient::NODE_ATTR_PASSWORD_MANAGER = "pwm";
const char* const MegaClient::PWM_ATTR_PASSWORD_NOTES = "n";
const char* const MegaClient::PWM_ATTR_PASSWORD_URL = "url";
const char* const MegaClient::PWM_ATTR_PASSWORD_USERNAME = "u";
const char* const MegaClient::PWM_ATTR_PASSWORD_PWD = "pwd";

NodeHandle MegaClient::getPasswordManagerBase()
{
    auto u = ownuser();
    return u ? toNodeHandle(u->getattr(ATTR_PWM_BASE)) : NodeHandle{};
}

void MegaClient::preparePasswordNodeData(attr_map& attrs, const AttrMap& data) const
{
    assert(!data.map.empty());

    std::string jsonData;
    data.getjson(&jsonData);
    attrs[AttrMap::string2nameid(NODE_ATTR_PASSWORD_MANAGER)] = std::move(jsonData);
}

std::string MegaClient::getPartialAPs()
{
    std::string ret;
    if (isClientType(ClientType::PASSWORD_MANAGER))
    {
        ret = "&e=" + toNodeHandle(getPasswordManagerBase()) + "&ir=1";
    }

    return ret;
}

void MegaClient::createPasswordManagerBase(int rTag, CommandCreatePasswordManagerBase::Completion cbRequest)
{
    LOG_info << "Password Manager: Requesting pwmh creation to server";

    auto newNode = std::make_unique<NewNode>();
    const bool canChangeVault = true;
    const std::string defaultBaseFolderName = "My Passwords";  // arbitrary default name, eventually updatable by client apps
    putnodes_prepareOneFolder(newNode.get(), defaultBaseFolderName, canChangeVault);

    // encrypt node password with user's master key to be sent to backend/API for storage
    std::array<byte, FILENODEKEYLENGTH> encryptedKey;
    this->key.ecb_encrypt(const_cast<byte*>(reinterpret_cast<const byte*>(newNode->nodekey.data())),
                          encryptedKey.data(), newNode->nodekey.size());
    newNode->nodekey.assign(reinterpret_cast<char*>(encryptedKey.data()), encryptedKey.size());

    reqs.add(new CommandCreatePasswordManagerBase(this, std::move(newNode), rTag, std::move(cbRequest)));
}

error MegaClient::createPasswordNode(const char* name, std::unique_ptr<AttrMap> data,
                                     std::shared_ptr<Node> nParent, int rTag)
{
    assert(nParent);
    assert(name && *name);

    const bool pwdPresent = data && (data->map.contains(AttrMap::string2nameid(PWM_ATTR_PASSWORD_PWD)));
    if (!(pwdPresent && nParent->isPasswordNodeFolder()))
    {
        LOG_err << "Password Manager: failed Password Node creation wrong paramenters "
                << (pwdPresent ? "" : "password ")
                << (nParent->isPasswordNodeFolder() ? "" : "Password Node Folder parent handle");
        return API_EARGS;
    }

    std::vector<NewNode> nn(1);
    NewNode& newPasswordNode = nn.front();
    const auto d = data.get();  // lambda capture initializers are C++14 so can't pass an std::unique_ptr
    const auto addAttrs = [this, d](AttrMap& attrs) { preparePasswordNodeData(attrs.map, *d); };
    const bool canChangeVault = true;
    putnodes_prepareOneFolder(&newPasswordNode, name, canChangeVault, addAttrs);
    // setting newPasswordNode.parenthandle will cause API_EARGS on request response
    const char* cauth = nullptr;

    putnodes(nParent->nodeHandle(), VersioningOption::NoVersioning, std::move(nn), cauth, rTag, canChangeVault);

    return API_OK;
}

error MegaClient::updatePasswordNode(NodeHandle nh, std::unique_ptr<AttrMap> newData,
                                     CommandSetAttr::Completion&& cb)
{
    auto pwdNode = nodeByHandle(nh);
    const auto aux = checkRenameNodePrecons(pwdNode);
    const bool& preconsOK = aux.first;
    const error& code = aux.second;
    if (!preconsOK) return code;

    if (!(newData && !newData->map.empty() && pwdNode->isPasswordNode()))
    {
        LOG_err << "Password Manager: failed Password Node update wrong parameters "
                << (pwdNode->isPasswordNode() ? "" : "Node provided is not Password Node ")
                << (newData && !newData->map.empty() ? "" : "nothing to update ");
        return API_EARGS;
    }

    AttrMap mergedData;
    mergedData.fromjson(pwdNode->attrs.map[AttrMap::string2nameid(NODE_ATTR_PASSWORD_MANAGER)].c_str());
    mergedData.applyUpdates(newData->map);
    attr_map updates;
    preparePasswordNodeData(updates, mergedData);

    const bool canChangeVault = true;
    return setattr(pwdNode, std::move(updates), std::move(cb), canChangeVault);
}

std::string MegaClient::generatePasswordChars(const bool useUpper,
                                              const bool useDigits,
                                              const bool useSymbols,
                                              const unsigned int length)
{
    if (length < 8 || length > 64)
    {
        LOG_err << "Characters-based password generation requested for invalid length. "
                << "Valid length range is [8, 64]";
        return std::string{};
    }

    static const std::string lowerCase = "abcdefghijklmnopqrstuvwxyz";
    static const std::string upperCase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const std::string digits = "0123456789";
    static const std::string symbols = "!@#$%^&*()";

    std::string pwd;
    pwd.reserve(length);

    std::random_device rd;
    std::mt19937 gen(rd()); // Mersenne Twister generator
    const auto appendOneRandom = [&pwd, &gen](const std::string& src)
    {
        std::uniform_int_distribution<> dis(0, static_cast<int>(src.size() - 1));
        pwd += src[static_cast<size_t>(dis(gen))];
    };

    std::string pool = lowerCase;
    appendOneRandom(lowerCase);
    if (useUpper)
    {
        appendOneRandom(upperCase); // Make sure there is at least 1
        pool += upperCase;
    }
    if (useDigits)
    {
        appendOneRandom(digits);
        pool += digits;
    }
    if (useSymbols)
    {
        appendOneRandom(symbols);
        pool += symbols;
    }

    std::uniform_int_distribution<> dis(0, static_cast<int>(pool.size() - 1));
    for (auto i = pwd.size(); i < length; ++i) pwd += pool[static_cast<size_t>(dis(gen))];

    // We shuffle to avoid the first mandatory types
    std::shuffle(std::begin(pwd), std::end(pwd), gen);

    return pwd;
}

void MegaClient::getNotifications(CommandGetNotifications::ResultFunc onResult)
{
    reqs.add(new CommandGetNotifications(this, onResult));
}

std::pair<uint32_t, uint32_t> MegaClient::getFlag(const char* flagName, bool commit)
{
    enum : uint32_t // 1:1 with enum values from public interface
    {
        FLAG_TYPE_INVALID = 0,
        FLAG_TYPE_AB_TEST = 1,
        FLAG_TYPE_FEATURE = 2,
    };

    if (!flagName)
    {
        return {FLAG_TYPE_INVALID, 0};
    }

    auto ab = mABTestFlags.find(flagName);
    if (ab != mABTestFlags.end())
    {
        return {FLAG_TYPE_AB_TEST, ab->second};
    }

    auto f = mFeatureFlags.find(flagName);
    if (f != mFeatureFlags.end())
    {
        return {FLAG_TYPE_FEATURE, f->second};
    }

    return {FLAG_TYPE_INVALID, 0};
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

const std::string KeyManager::SVCRYPTO_PAIRWISE_KEY = "strongvelope pairwise key\x01";

void KeyManager::init(const string& prEd25519, const string& prCu25519, const string& prRSA)
{
    if (mVersion != 0 || mGeneration != 0)
    {
        LOG_err << "Init invoked incorrectly";
        assert(false);
        return;
    }

    mVersion = 1;
    mCreationTime = static_cast<int32_t>(time(nullptr));
    mIdentity = mClient.me;
    mGeneration = 1;
    mPrivEd25519 = prEd25519;
    mPrivCu25519 = prCu25519;

    mPrivRSA.clear();
    if (prRSA.size())
    {
        string prRSABin = Base64::atob(prRSA);
        AsymmCipher ac;

        if (!ac.setkey(AsymmCipher::PRIVKEY, (const unsigned char*)prRSABin.data(), (int)prRSABin.size()))
        {
            LOG_err << "Priv RSA key problem during KeyManager initialization.";
            assert(false);
        }
        else
        {
            // Store it in the short format (3 Ints): pqd.
            ac.serializekey(&mPrivRSA, AsymmCipher::PRIVKEY_SHORT);
        }
    }
    else
    {
        assert(mClient.loggedin() == EPHEMERALACCOUNTPLUSPLUS);
    }

    if (!mPostRegistration)
    {
        // We request the upgrade after nodes_current to be able to migrate shares
        // mClient.app->upgrading_security();
    }
    else
    {
        mPostRegistration = false;
    }
}

void KeyManager::setKey(const mega::SymmCipher &masterKey)
{
    // Derive key from MK
    CryptoPP::HKDF<CryptoPP::SHA256> hkdf;
    byte derivedKey[SymmCipher::KEYLENGTH];
    byte info[1]; info[0] = 1;
    hkdf.DeriveKey(derivedKey, sizeof(derivedKey), masterKey.key, SymmCipher::KEYLENGTH, nullptr, 0, info, sizeof(info));
    mKey.setkey(derivedKey);

    if (mDebugContents)
    {
        LOG_verbose << "Derived key (B64): " << Base64::btoa(string((const char*)derivedKey, SymmCipher::KEYLENGTH));
    }
}

bool KeyManager::fromKeysContainer(const string &data)
{
    bool success = false;
    KeyManager km(mClient);  // keymanager to store values temporary

    if (data.size() > 2 && data[0] == 20)
    {
        // data[1] is reserved, always 0

        if (data.size() > 2 + IV_LEN)
        {
            const string keysCiphered((const char*)(data.data() + 2 + IV_LEN), (size_t)(data.size() - 2 - IV_LEN));
            const string iv((const char*)data.data() + 2, IV_LEN);

            // Decrypt ^!keys attribute
            string keysPlain;
            if (!mKey.gcm_decrypt(&keysCiphered, (byte*)data.data() + 2, IV_LEN, 16, &keysPlain))
            {
                LOG_err << "Failed to GCM decrypt ^!keys.";
                return false;
            }

            success = unserialize(km, keysPlain);
            if (!success)
            {
                LOG_err << "Failed to unserialize ^!keys. Ignoring received value";
                mClient.sendevent(99463, "KeyMgr / Failed to unserialize ^!keys");
            }
        }
        else LOG_err << "Failed to decode ^!keys. Unexpected size";
    }

    // validate received data and update local values
    if (success) success = isValidKeysContainer(km);
    if (success)
    {
        updateValues(km);
    }

    assert(success);
    return success;
}

bool KeyManager::isValidKeysContainer(const KeyManager& km)
{
    // downgrade attack detection
    if (km.mGeneration < mGeneration)
    {
        ostringstream msg;
        msg << "KeyMgr / Downgrade attack for ^!keys: " << km.mGeneration << " < " << mGeneration;
        LOG_err << msg.str();
        mClient.sendevent(99461, msg.str().c_str());

        // block updates of ^!keys attribute and notify the app, so it can
        // warn about the potential attack and block user's interface
        mDowngradeAttack = true;
        mClient.app->downgrade_attack();
        return false;
    }

    // validate private Ed25519 key
    if (mPrivEd25519.empty())
    {
        mPrivEd25519 = km.mPrivEd25519;
    }
    assert(mPrivEd25519 == km.mPrivEd25519);

    // validate private Cu25519 key
    if (mPrivCu25519.empty())
    {
        mPrivCu25519 = km.mPrivCu25519;
    }
    assert(mPrivCu25519 == km.mPrivCu25519);

    // validate private RSA key
    if (mPrivRSA.empty())
    {
        assert(km.mPrivRSA.empty() || km.mPrivRSA.size() >= 512);
        if (km.mPrivRSA.empty())
        {
            LOG_warn << "Empty RSA key";
        }
        else if (km.mPrivRSA.size() < 512)
        {
            LOG_err << "Invalid RSA key";
        }
        else
        {
            mPrivRSA = km.mPrivRSA;
            if (!decodeRSAKey())
            {
                LOG_warn << "Private key malformed while unserializing ^!keys.";
            }
            // Note: the copy of privRSA from ^!keys will be used exclusively for legacy RSA functionality (MEGAdrop, not supported by SDK)
        }
    }
    assert(mPrivRSA == km.mPrivRSA);

    return true;
}

void KeyManager::updateValues(KeyManager &km)
{
    mVersion            = km.mVersion;
    mCreationTime       = km.mCreationTime;
    mIdentity           = km.mIdentity;
    mGeneration         = km.mGeneration;
    mAttr               = std::move(km.mAttr);
    // private keys do not change -> no need to update
//        mPrivEd25519        = km.mPrivEd25519;
//        mPrivCu25519        = km.mPrivCu25519;
//        mPrivRSA            = km.mPrivRSA;
    updateAuthring(ATTR_AUTHRING, km.mAuthEd25519);
    updateAuthring(ATTR_AUTHCU255, km.mAuthCu25519);
    updateShareKeys(km.mShareKeys);
    mPendingOutShares   = std::move(km.mPendingOutShares);
    mPendingInShares    = std::move(km.mPendingInShares);
    mBackups            = std::move(km.mBackups);
    mWarnings           = std::move(km.mWarnings);
    mOther              = std::move(km.mOther);

    if (promotePendingShares())
    {
        LOG_debug << "Promoting pending shares after an update of ^!keys";
        commit([this]()
        {
            // Changes to apply in the commit
            promotePendingShares();
        }); // No completion callback in this case
    }
}

string KeyManager::toKeysContainer()
{
    if (mVersion == 0)
    {
        LOG_err << "Failed to prepare container from keys. Not initialized yet";
        assert(false);
        return string();
    }

    // Do not update mGeneration here, since it may lead to fake
    // detection of downgrade-attacks. Instead, use mGeneration+1
    // at the serialize(). The mGeneration will be updated later,
    // when the putua() from updateAttribute() success.
    //++mGeneration;

    const string iv = mClient.rng.genstring(IV_LEN);
    const string keysPlain = serialize();

    string keysCiphered;
    if (!mKey.gcm_encrypt(&keysPlain, (byte*)iv.data(), IV_LEN, 16, &keysCiphered))
    {
        LOG_err << "Failed to encrypt keys attribute.";
        assert(false);
        return string();
    }

#ifndef NDEBUG
    byte header[2] = {20, 0};
    assert(string({20, 0}) == string((const char*)header, sizeof(header)));
#endif

    return string({20, 0}) + iv + keysCiphered;
}

string KeyManager::tagHeader(const byte tag, size_t len) const
{
    vector<byte> res;

    res.push_back(tag);
    res.push_back(static_cast<byte>((len & 0xFF0000) >> 16));
    res.push_back((len & 0xFF00) >> 8);
    res.push_back(len & 0xFF);

    return string((const char*)res.data(), res.size());
}

string KeyManager::serialize() const
{
    string result;

    result.append(tagHeader(TAG_VERSION, sizeof(mVersion)));
    result.append((const char*)&mVersion, sizeof(mVersion));

    result.append(tagHeader(TAG_CREATION_TIME, sizeof(mCreationTime)));
    uint32_t creationTimeBE = htonl(mCreationTime); // Webclient sets this value as BigEndian
    result.append((const char*)&creationTimeBE, sizeof(creationTimeBE));

    result.append(tagHeader(TAG_IDENTITY, sizeof(mIdentity)));
    result.append((const char*)&mIdentity, sizeof(mIdentity));

    result.append(tagHeader(TAG_GENERATION, sizeof(mGeneration)));
    uint32_t generationBE = htonl(mGeneration+1); // Webclient sets this value as BigEndian
    result.append((const char*)&generationBE, sizeof(generationBE));

    result.append(tagHeader(TAG_ATTR, mAttr.size()));
    result.append(mAttr);

    assert(mPrivEd25519.size() == EdDSA::SEED_KEY_LENGTH);
    result.append(tagHeader(TAG_PRIV_ED25519, EdDSA::SEED_KEY_LENGTH));
    result.append(mPrivEd25519);

    assert(mPrivCu25519.size() == ECDH::PRIVATE_KEY_LENGTH);
    result.append(tagHeader(TAG_PRIV_CU25519, ECDH::PRIVATE_KEY_LENGTH));
    result.append(mPrivCu25519);

    assert(!mPrivRSA.size() || mPrivRSA.size() > 512);
    result.append(tagHeader(TAG_PRIV_RSA, mPrivRSA.size()));
    result.append(mPrivRSA);

    result.append(tagHeader(TAG_AUTHRING_ED25519, mAuthEd25519.size()));
    result.append(mAuthEd25519);

    result.append(tagHeader(TAG_AUTHRING_CU25519, mAuthCu25519.size()));
    result.append(mAuthCu25519);

    string shareKeys = serializeShareKeys();
    result.append(tagHeader(TAG_SHAREKEYS, shareKeys.size()));
    result.append(shareKeys);

    string pendingOutshares = serializePendingOutshares();
    result.append(tagHeader(TAG_PENDING_OUTSHARES, pendingOutshares.size()));
    result.append(pendingOutshares);

    string pendingInshares = serializePendingInshares();
    result.append(tagHeader(TAG_PENDING_INSHARES, pendingInshares.size()));
    result.append(pendingInshares);

    string backups = serializeBackups();
    result.append(tagHeader(TAG_BACKUPS, backups.size()));
    result.append(backups);

    string warnings = serializeWarnings();
    result.append(tagHeader(TAG_WARNINGS, warnings.size()));
    result.append(warnings);

    result.append(mOther);

    return result;
}

uint32_t KeyManager::generation() const
{
    return mGeneration;
}

string KeyManager::privEd25519() const
{
    return mPrivEd25519;
}

string KeyManager::privCu25519() const
{
    return mPrivCu25519;
}

void KeyManager::setPostRegistration(bool postRegistration)
{
    mPostRegistration = postRegistration;
}

bool KeyManager::addPendingOutShare(handle sharehandle, std::string uid)
{
    mPendingOutShares[sharehandle].insert(uid);
    return true;
}

bool KeyManager::addPendingInShare(std::string sharehandle, handle userHandle, std::string encrytedKey)
{
    mPendingInShares[sharehandle] = pair<handle, string>(userHandle, encrytedKey);
    return true;
}

bool KeyManager::removePendingOutShare(handle sharehandle, std::string uid)
{
    bool removed = false;
    User *user = mClient.finduser(uid.c_str(), 0);
    if (user)
    {
        removed = mPendingOutShares[sharehandle].erase(user->email);
        removed |= mPendingOutShares[sharehandle].erase(user->uid) > 0;
    }
    else
    {
        removed = mPendingOutShares[sharehandle].erase(uid);
    }
    return removed;
}

bool KeyManager::removePendingInShare(std::string shareHandle)
{
    return mPendingInShares.erase(shareHandle);
}

bool KeyManager::addShareKey(handle sharehandle, std::string shareKey, bool sharedSecurely)
{
    auto it = mShareKeys.find(sharehandle);
    if (it != mShareKeys.end() && it->second.second[ShareKeyFlagsId::TRUSTED] && it->second.first != shareKey)
    {
        LOG_warn << "Replacement of trusted sharekey for " << toNodeHandle(sharehandle);
        mClient.sendevent(99470, "KeyMgr / Replacing trusted sharekey");
        assert(false);
    }

    ShareKeyFlags flags;
    flags[ShareKeyFlagsId::TRUSTED] = sharedSecurely;

    mShareKeys[sharehandle] = pair<string, ShareKeyFlags>(shareKey, flags);
    return true;
}

string KeyManager::getShareKey(handle sharehandle) const
{
    auto it = mShareKeys.find(sharehandle);
    if (it != mShareKeys.end())
    {
        return it->second.first;
    }
    return std::string();
}

bool KeyManager::isShareKeyTrusted(handle sharehandle) const
{
    auto it = mShareKeys.find(sharehandle);
    return it != mShareKeys.end() && it->second.second[ShareKeyFlagsId::TRUSTED];
}

bool KeyManager::isShareKeyInUse(handle sharehandle) const
{
    auto it = mShareKeys.find(sharehandle);
    return it != mShareKeys.end() && it->second.second[ShareKeyFlagsId::INUSE];
}

void KeyManager::setSharekeyInUse(handle sharehandle, bool sent)
{
    auto it = mShareKeys.find(sharehandle);
    if (it != mShareKeys.end())
    {
        it->second.second[ShareKeyFlagsId::INUSE] = sent;
    }
    else
    {
        string msg = "Trying to set share key as in-use for non-existing share key";
        LOG_err << msg;
        assert(it != mShareKeys.end() && msg.c_str());
    }
}

void KeyManager::syncSharekeyInUseBit()
{
    vector<NodeHandle> sharesToClear;
    for(const auto &it : mShareKeys)
    {
        // Store the handle of the existing nodes which are no longer shared and with the in-use bit still set.
        if(it.second.second[ShareKeyFlagsId::INUSE])
        {
            NodeHandle nh = NodeHandle().set6byte(it.first);
            std::shared_ptr<Node> n = mClient.nodeByHandle(nh);
            if (n && !n->isShared())
            {
                sharesToClear.emplace_back(nh);
            }
        }
    }

    if(sharesToClear.size())
    {
        commit(
            [this, sharesToClear]()
            {
                string handles;
                for(const auto &nh : sharesToClear)
                {
                    setSharekeyInUse(nh.as8byte(), false);
                    handles += " " + toNodeHandle(nh);
                }
                LOG_debug << "Clearing in-use bit of the share-keys no longer in use. Applied to:" << handles;
            });
    }
}

string KeyManager::encryptShareKeyTo(handle userhandle, std::string shareKey)
{
    if (verificationRequired(userhandle))
    {
        return std::string();
    }

    std::string sharedKey = computeSymmetricKey(userhandle);
    if (!sharedKey.size())
    {
        return std::string();
    }

    std::string encryptedKey;
    encryptedKey.resize(CryptoPP::AES::BLOCKSIZE);

    CryptoPP::ECB_Mode<CryptoPP::AES>::Encryption aesencryption((byte *)sharedKey.data(), sharedKey.size());
    aesencryption.ProcessData((byte *)encryptedKey.data(), (byte *)shareKey.data(), shareKey.size());

    return encryptedKey;
}

string KeyManager::decryptShareKeyFrom(handle userhandle, std::string key)
{
    if (verificationRequired(userhandle))
    {
        return std::string();
    }

    std::string sharedKey = computeSymmetricKey(userhandle);
    if (!sharedKey.size())
    {
        return std::string();
    }

    std::string shareKey;
    shareKey.resize(CryptoPP::AES::BLOCKSIZE);

    CryptoPP::ECB_Mode<CryptoPP::AES>::Decryption aesencryption((byte *)sharedKey.data(), sharedKey.size());
    aesencryption.ProcessData((byte *)shareKey.data(), (byte *)key.data(), key.size());

    return shareKey;
}

void KeyManager::setAuthRing(std::string authring)
{
    mAuthEd25519 = authring;
}

void KeyManager::setAuthCU255(std::string authring)
{
    mAuthCu25519 = authring;
}

void KeyManager::setPrivRSA(std::string privRSA)
{
    mPrivRSA = privRSA;
}

string KeyManager::getPrivRSA()
{
    return mPrivRSA;
}

bool KeyManager::promotePendingShares()
{
    bool attributeUpdated = false;
    bool newshares = false;
    std::vector<std::string> keysToDelete;

    for (const auto& it : mPendingOutShares)
    {
        handle nodehandle = it.first;
        for (const auto& uid : it.second)
        {
            User *u = mClient.finduser(uid.c_str(), 0);
            if (u && !verificationRequired(u->userhandle))
            {
                LOG_debug << "Promoting pending outshare of node " << toNodeHandle(nodehandle) << " for " << uid;
                auto shareit = mShareKeys.find(nodehandle);
                if (shareit != mShareKeys.end())
                {
                    std::string encryptedKey = encryptShareKeyTo(u->userhandle, shareit->second.first);
                    if (encryptedKey.size())
                    {
                        mClient.reqs.add(new CommandPendingKeys(&mClient, u->userhandle, nodehandle, (byte *)encryptedKey.data(),
                        [uid](Error err)
                        {
                            if (err)
                            {
                                LOG_err << "Error sending share key: " << err;
                            }
                            else
                            {
                                LOG_debug << "Share key correctly sent";
                            }
                        }));

                        keysToDelete.push_back(uid);
                        attributeUpdated = true;
                    }
                    else
                    {
                        LOG_warn << "Unable to encrypt share key to promote pending outshare " << toNodeHandle(nodehandle) << " uh: " << toHandle(u->userhandle);
                    }
                }
            }
        }

        for (const auto& uid : keysToDelete)
        {
            removePendingOutShare(nodehandle, uid);
        }
        keysToDelete.clear();
    }

    for (const auto& it : mPendingInShares)
    {
        handle nodeHandle = 0;
        Base64::atob(it.first.c_str(), (byte*)&nodeHandle, MegaClient::NODEHANDLE);

        handle userHandle = it.second.first;
        std::string encryptedShareKey = it.second.second;

        if (!verificationRequired(userHandle))
        {
            if (encryptedShareKey.size() > 16)
            {
                // Legacy bug: SDK used to store share keys in B64. If size > 16
                // the share key should be converted first into binary data.
                string msg = "KeyMgr / Pending inshare key from string to binary";
                mClient.sendevent(99480, msg.c_str());
                encryptedShareKey = Base64::atob(it.second.second);
            }

            LOG_debug << "Promoting pending inshare of node " << toNodeHandle(nodeHandle) << " for " << toHandle(userHandle);
            std::string shareKey = decryptShareKeyFrom(userHandle, encryptedShareKey);
            if (shareKey.size())
            {
                auto skit = mShareKeys.find(nodeHandle);
                if (skit != mShareKeys.end() && skit->second.first != shareKey)
                {
                    LOG_warn << "Updating share key for inshare " << toNodeHandle(nodeHandle) << " uh: " << toHandle(userHandle);
                }

                addShareKey(nodeHandle, shareKey, true);
                mClient.newshares.push_back(new NewShare(nodeHandle, 0, UNDEF, ACCESS_UNKNOWN, 0, (byte *)shareKey.data()));
                keysToDelete.push_back(it.first);
                attributeUpdated = true;
                newshares = true;
            }
            else
            {
                LOG_warn << "Unable to decrypt share key to promote pending inshare " << toNodeHandle(nodeHandle) << " uh: " << toHandle(userHandle);
            }
        }
    }

    for (const auto& shareHandle : keysToDelete)
    {
        removePendingInShare(shareHandle);
    }
    keysToDelete.clear();

    if (newshares)
    {
        mClient.mergenewshares(true);
    }

    return attributeUpdated;
}

bool KeyManager::isUnverifiedOutShare(handle nodeHandle, const string& uid)
{
    auto it = mPendingOutShares.find(nodeHandle);
    if (it == mPendingOutShares.end())
    {
        return false;
    }

    for (const auto& uidIt : it->second)
    {
        if (uidIt == uid)
        {
            return true;
        }

        // if 'uid' is a userhandle, try to match by email
        // (in case of pending outshare that later upgrades to outshare by
        // sharee accepting the PCR, the 'uid' in 'keys.pendingoutshares' will
        // keep the email, but we already know the userHandle)
        if (uid.find("@") == uid.npos)
        {
            User* u = mClient.finduser(uid.c_str(), 0);
            if (u && uidIt == u->email)
            {
                return true;
            }
        }
    }

    return false;
}

bool KeyManager::isUnverifiedInShare(handle nodeHandle, handle userHandle)
{
    auto it = mPendingInShares.find(toNodeHandle(nodeHandle));
    if (it == mPendingInShares.end())
    {
        return false;
    }

    if (it->second.first == userHandle)
    {
        return true;
    }
    return false;
}

void KeyManager::loadShareKeys()
{
    for (const auto& it : mShareKeys)
    {
        handle sharehandle = it.first;
        std::string shareKey = it.second.first;

        std::shared_ptr<Node> n = mClient.nodebyhandle(sharehandle);
        if (n && !n->sharekey)
        {
            std::unique_ptr<NewShare> newShare(new NewShare(sharehandle, n->inshare ? 0 : -1,
                                                     UNDEF, ACCESS_UNKNOWN,
                                                     0, (byte *)shareKey.data()));

            mClient.mergenewshare(newShare.get(), true, false);
        }
    }
}

void KeyManager::commit(std::function<void ()> applyChanges, std::function<void (error e)> completion)
{
    LOG_debug << "[keymgr] New update requested";
    if (mVersion == 0)
    {
        LOG_err << "Not initialized yet. Cancelling the update.";
        assert(false);
        if (completion)
        {
            completion(API_EINTERNAL);
        }
        return;
    }

    nextQueue.push_back(std::pair<std::function<void()>, std::function<void(error e)>>(std::move(applyChanges), std::move(completion)));
    if (activeQueue.size())
    {
        LOG_debug << "[keymgr] Another commit is in progress. Queued updates: " << nextQueue.size();
        return;
    }

    nextCommit();
}

void KeyManager::reset()
{
    mVersion = 0;
    mCreationTime = 0;
    mIdentity = UNDEF;
    mGeneration = 0;
    mAttr.clear();
    mPrivEd25519.clear();
    mPrivCu25519.clear();
    mPrivRSA.clear();
    mAuthEd25519.clear();
    mAuthCu25519.clear();
    mBackups.clear();
    mWarnings.clear();
    mOther.clear();
    mPendingInShares.clear();
    mPendingOutShares.clear();
    mShareKeys.clear();
}

string KeyManager::toString() const
{
    ostringstream buf;

    buf << "Version: " << (int)mVersion << "\n";
    buf << "Creation time: " << mCreationTime<< "\n";
    buf << "Identity: " << toHandle(mIdentity)<< "\n";
    buf << "Generation: " << mGeneration<< "\n";
    buf << "Attr: " << Base64::btoa(mAttr)<< "\n";
    buf << "PrivEd25519: " << Base64::btoa(mPrivEd25519)<< "\n";
    buf << "PrivCu25519: " << Base64::btoa(mPrivCu25519)<< "\n";
    buf << "PrivRSA: " << Base64::btoa(mPrivRSA)<< "\n";
    buf << "Authring Ed25519:\n" << AuthRing::toString(mClient.mAuthRings.at(ATTR_AUTHRING))<< "\n";
    buf << "Authring Cu25519:\n" << AuthRing::toString(mClient.mAuthRings.at(ATTR_AUTHCU255))<< "\n";
    buf << shareKeysToString(*this);
    buf << pendingOutsharesToString(*this);
    buf << pendingInsharesToString(*this);
    buf << "Backups: " << Base64::btoa(mBackups) << "\n";
    buf << warningsToString(*this);

    return buf.str();
}

bool KeyManager::getContactVerificationWarning()
{
    auto it = mWarnings.find("cv");

    if (it != mWarnings.end() && mWarnings["cv"].size())
    {
        char* endp;
        long int res;
        errno = 0;
        res = strtol(mWarnings["cv"].c_str(), &endp, 10);
        if (*endp != '\0' || endp == mWarnings["cv"].c_str() || errno == ERANGE)
        {
            LOG_err << "cv field in warnings is malformed";
            return false;
        }
        return res;
    }
    return false;
}

void KeyManager::setContactVerificationWarning(bool enabled)
{
    mWarnings["cv"] = std::to_string(enabled);
}

string KeyManager::shareKeysToString(const KeyManager& km)
{
    ostringstream buf;
    buf << "Share Keys:\n";

    unsigned count = 0;
    for (const auto &it : km.mShareKeys)
    {
        ++count;
        handle h = it.first;
        const string& shareKeyStr = it.second.first;
        bool trust = it.second.second[ShareKeyFlagsId::TRUSTED];
        bool inUse = it.second.second[ShareKeyFlagsId::INUSE];
        buf << "\t#" << count << "\t h: " << toNodeHandle(h) <<
                       " sk: " << Base64::btoa(shareKeyStr) << " t: " << trust << " used: " << inUse << "\n";
    }

    return buf.str();
}

string KeyManager::pendingOutsharesToString(const KeyManager& km)
{
    ostringstream buf;
    buf << "Pending Outshares:\n";

    unsigned count = 0;
    for (const auto &it : km.mPendingOutShares)
    {
        ++count;
        handle h = it.first;
        for (const auto& uid : it.second)
        {
            buf << "\t#" << count << "\th: " << toNodeHandle(h) << " user: " << uid << "\n";
        }
    }

    return buf.str();
}

string KeyManager::pendingInsharesToString(const KeyManager& km)
{
    ostringstream buf;
    buf << "Pending Inshares:\n";

    unsigned count = 0;
    for (const auto &it : km.mPendingInShares)
    {
        ++count;
        const string& nh = it.first;
        const handle& uh = it.second.first;
        const string& shareKey = it.second.second;

        buf << "\t#" << count << "\tn: " << nh << " uh: " << toHandle(uh) << " sk: " << Base64::btoa(shareKey) << "\n";
    }

    return buf.str();
}

string KeyManager::warningsToString(const KeyManager& km)
{
    ostringstream buf;
    buf << "Warnings:\n";

    for (const auto &it : km.mWarnings)
    {
        buf << "\ttag: \"" << it.first << "\" \tval: \"" << it.second << "\"\n";
    }

    return buf.str();
}

void KeyManager::nextCommit()
{
    assert(activeQueue.empty());
    if (nextQueue.size())
    {
        LOG_debug << "[keymgr] Initializing a new commit"
                  << " with " << nextQueue.size() << " updates";
        activeQueue = std::move(nextQueue);
        nextQueue = {};
        tryCommit(API_EINCOMPLETE, [this]() { nextCommit(); });
    }
    else
    {
        LOG_debug << "[keymgr] No more updates in the queue.";
    }
}

void KeyManager::tryCommit(Error e, std::function<void ()> completion)
{
    if (!e || mDowngradeAttack || e == API_EARGS)
    {
        const std::string tmp(" with " + std::to_string(activeQueue.size()) + " updates");

        if (e == API_OK) LOG_debug << "[keymgr] Commit completed" << tmp;
        else if (e == API_EARGS) LOG_debug << "[keymgr] Commit aborted, keys too large?" << tmp;
        else LOG_debug << "[keymgr] Commit aborted (downgrade attack)" << tmp;

        for (auto &activeCommit : activeQueue)
        {
            if (activeCommit.second)
            {
                activeCommit.second(e); // Run update completion callback
            }
        }

        activeQueue = {};

        completion();   // -> nextCommit() or log "No more updates in the queue"
        return;
    }

    LOG_debug << "[keymgr] " << (e == API_EINCOMPLETE ? "Starting" : "Retrying")
              << " commit with " << activeQueue.size() << " updates";

    for (auto &activeCommit : activeQueue)
    {
        if (activeCommit.first)
        {
            activeCommit.first(); // Apply commit changes
        }
    }
    updateAttribute([this, completion](Error e)
    {
        tryCommit(e, completion);
    });
}

void KeyManager::updateAttribute(std::function<void (Error)> completion)
{
    string buf = toKeysContainer();
    mClient.putua(ATTR_KEYS, (byte*)buf.data(), (int)buf.size(), 0, UNDEF, 0, 0, [this, completion](Error e)
    {
        if (!e)
        {
            completion(API_OK);
            return;
        }

        User *ownUser = mClient.finduser(mClient.me);
        if (!ownUser)
        {
            LOG_err << "[keymgr] Not logged in during commit";
            completion(API_OK); // Returning API_OK to stop the loop
            return;
        }

        LOG_warn << "[keymgr] Error setting the value of ^!keys: (" << e << ")";
        if (e != API_EEXPIRED)
        {
            completion(e);
            return;
        }

        mClient.sendevent(99462, "KeyMgr / Versioning clash for ^!keys");

        mClient.reqs.add(new CommandGetUA(&mClient, ownUser->uid.c_str(), ATTR_KEYS, nullptr, 0,
        [completion](error err)
        {
            LOG_err << "[keymgr] Error getting the value of ^!keys (" << err << ")";
            completion(err);
        },
        [completion](byte*, unsigned, attr_t)
        {
            LOG_debug << "[keymgr] Success getting the value of ^!keys";
            completion(API_EEXPIRED);
        }, nullptr));
    });
}

bool KeyManager::getPostRegistration() const
{
    return mPostRegistration;
}

bool KeyManager::unserialize(KeyManager& km, const string &keysContainer)
{
    // Decode blob

    const char* blob = keysContainer.data();
    size_t blobLength = keysContainer.length();

    static const uint8_t headerSize = 4;  // 1 byte for Tag, 3 bytes for Length
    size_t offset = headerSize;
    while (offset <= blobLength)
    {
        byte tag = blob[offset - headerSize];
        size_t len = (static_cast<byte>(blob[offset - 3]) << 16) +
                     (static_cast<byte>(blob[offset - 2]) << 8) +
                      static_cast<byte>(blob[offset - 1]);

        if (offset + len > blobLength)
        {
            LOG_err << "Invalid record in ^!keys attributes: offset: " << offset << ", len: " << len << ", size: " << blobLength;
            return false;
        }

        if (mDebugContents)
        {
            LOG_verbose << "Tag: " << (int)tag << " Len: " << len;
        }

        switch (tag)
        {
        case TAG_VERSION:
            if (len != sizeof(km.mVersion)) return false;
            km.mVersion = MemAccess::get<uint8_t>(blob + offset);
            if (mDebugContents)
            {
                LOG_verbose << "Version: " << (int)km.mVersion;
            }
            break;

        case TAG_CREATION_TIME:
            if (len != sizeof(km.mCreationTime)) return false;
            km.mCreationTime = MemAccess::get<uint32_t>(blob + offset);
            km.mCreationTime = ntohl(km.mCreationTime); // Webclient sets this value as BigEndian
            if (mDebugContents)
            {
                LOG_verbose << "Creation time: " << km.mCreationTime;
            }
            break;

        case TAG_IDENTITY:
            if (len != sizeof(mIdentity)) return false;
            km.mIdentity = MemAccess::get<handle>(blob + offset);
            if (mDebugContents)
            {
                LOG_verbose << "Identity: " << toHandle(km.mIdentity);
            }
            break;

        case TAG_GENERATION:
        {
            if (len != sizeof(km.mGeneration)) return false;
            km.mGeneration = MemAccess::get<uint32_t>(blob + offset);
            km.mGeneration = ntohl(km.mGeneration); // Webclient sets this value as BigEndian
            LOG_verbose << "KeyManager generation: " << km.mGeneration;
            break;
        }
        case TAG_ATTR:
            km.mAttr.assign(blob + offset, len);
            if (mDebugContents)
            {
                LOG_verbose << "Attr: " << Base64::btoa(km.mAttr);
            }
            break;

        case TAG_PRIV_ED25519:
            if (len != EdDSA::SEED_KEY_LENGTH) return false;
            km.mPrivEd25519.assign(blob + offset, len);
            if (mDebugContents)
            {
                LOG_verbose << "PrivEd25519: " << Base64::btoa(km.mPrivEd25519);
            }
            break;

        case TAG_PRIV_CU25519:
            if (len != ECDH::PRIVATE_KEY_LENGTH) return false;
            km.mPrivCu25519.assign(blob + offset, len);
            if (mDebugContents)
            {
                LOG_verbose << "PrivCu25519: " << Base64::btoa(km.mPrivCu25519);
            }
            break;

        case TAG_PRIV_RSA:
        {
            km.mPrivRSA.assign(blob + offset, len);
            if (mDebugContents)
            {
                LOG_verbose << "PrivRSA: " << Base64::btoa(km.mPrivRSA);
            }
            break;
        }
        case TAG_AUTHRING_ED25519:
        {
            attr_t at = ATTR_AUTHRING;
            km.mAuthEd25519.assign(blob + offset, len);
            AuthRing tmp(at, km.mAuthEd25519);
            if (mDebugContents)
            {
                LOG_verbose << "Authring Ed25519:\n" << AuthRing::toString(tmp);
            }
            break;
        }
        case TAG_AUTHRING_CU25519:
        {
            attr_t at = ATTR_AUTHCU255;
            km.mAuthCu25519.assign(blob + offset, len);
            AuthRing tmp(at, km.mAuthCu25519);
            if (mDebugContents)
            {
                LOG_verbose << "Authring Cu25519:\n" << AuthRing::toString(tmp);
            }
            break;
        }
        case TAG_SHAREKEYS:
        {
            string buf(blob + offset, len);
            if (!deserializeShareKeys(km, buf)) return false;
            if (mDebugContents)
            {
                LOG_verbose << shareKeysToString(km);
            }
            break;
        }
        case TAG_PENDING_OUTSHARES:
        {
            string buf(blob + offset, len);
            if (!deserializePendingOutshares(km, buf)) return false;
            if (mDebugContents)
            {
                LOG_verbose << pendingOutsharesToString(km);
            }
            break;
        }
        case TAG_PENDING_INSHARES:
        {
            string buf(blob + offset, len);
            if (!deserializePendingInshares(km, buf)) return false;
            // Commented to trace possible issues with pending inshares.
            //if (mDebugContents)
            //{
                LOG_verbose << pendingInsharesToString(km);
            //}
            break;
        }
        case TAG_BACKUPS:
        {
            string buf(blob + offset, len);
            if (!deserializeBackups(km, buf)) return false;
            if (mDebugContents)
            {
                LOG_verbose << "Backups: " << Base64::btoa(km.mBackups);
            }
            break;
        }
        case TAG_WARNINGS:
        {
            string buf(blob + offset, len);
            if (!deserializeWarnings(km, buf)) return false;
            if (mDebugContents)
            {
                LOG_verbose << warningsToString(km);
            }
            break;
        }
        default:    // any other tag needs to be stored as well, and included in newer versions
            km.mOther.append(blob + offset - headerSize, headerSize + len);
            break;
        }

        offset += headerSize + len;
    }

    return true;
}

bool KeyManager::deserializeShareKeys(KeyManager& km, const string &blob)
{
    // clean old data, so we don't left outdated sharekeys in place
    km.mShareKeys.clear();

    // [nodeHandle.6 shareKey.16 flags.1]*
    CacheableReader r(blob);

    while(r.hasdataleft())
    {
        handle h = UNDEF;
        byte shareKey[SymmCipher::KEYLENGTH];
        byte flagsBuf = 0;

        if (!r.unserializenodehandle(h)
                || !r.unserializebinary(shareKey, sizeof(shareKey))
                || !r.unserializebyte(flagsBuf))
        {
            LOG_err << "Share keys is corrupt";
            return false;
        }

        string shareKeyStr((const char*)shareKey, sizeof(shareKey));
        ShareKeyFlags flags(flagsBuf);
        km.mShareKeys[h] = pair<string, ShareKeyFlags>(shareKeyStr, flags);
    }

    return true;
}

string KeyManager::serializeShareKeys() const
{
    string result;

    CacheableWriter w(result);

    for (const auto& it : mShareKeys)
    {
        handle h = it.first;
        w.serializenodehandle(h);

        size_t shareKeyLen = it.second.first.size();
        byte *shareKey = (byte*)it.second.first.data();
        w.serializebinary(shareKey, shareKeyLen);

        byte flagsBuf = static_cast<byte>(it.second.second.to_ulong());
        w.serializebyte(flagsBuf);
    }

    return result;
}

bool KeyManager::deserializePendingOutshares(KeyManager& km, const string &blob)
{
    // clean old data, so we don't left outdated pending outshares in place
    km.mPendingOutShares.clear();

    // [len.1 nodeHandle.6 uid]*
    // if len=0  -> uid is a user handle
    // if len!=0 -> uid is an email address
    CacheableReader r(blob);

    while(r.hasdataleft())
    {
        byte len = 0;
        handle h = UNDEF;
        string uid;

        if (!r.unserializebyte(len)
                || !r.unserializenodehandle(h))
        {
            LOG_err << "Pending outshare is corrupt: len or nodehandle";
            return false;
        }

        bool success;
        if (len == 0)   // user handle
        {
            handle uh = UNDEF;
            success = r.unserializehandle(uh);
            uid = toHandle(uh);
        }
        else
        {
            byte buf[256];
            success = r.unserializebinary(buf, len);
            uid.append((const char*)buf, len);
        }
        if (!success)
        {
            LOG_err << "Pending outshare is corrupt: uid";
            return false;
        }

        km.mPendingOutShares[h].emplace(uid);
    }

    return true;
}

string KeyManager::serializePendingOutshares() const
{
    // [len.1 nodeHandle.6 uid]
    // if uid is a user handle    --> len = 0
    // if uid is an email address --> len = emailAddress.size()
    string result;

    CacheableWriter w(result);

    for (const auto& itNodes : mPendingOutShares)
    {
        handle h = itNodes.first;   // handle of shared folder

        for (const std::string& uid : itNodes.second)
        {
            byte len = 0;
            if (uid.find('@') != string::npos)
            {
                if (uid.size() >= 256)
                {
                    LOG_err << "Incorrect email size in pending outshare: " << uid;
                    assert(!"Incorrect email size in pending outshare");
                    continue;
                }
                len = static_cast<byte>(uid.size());
            }
            else
            {
                if (uid.size() != 11)
                {
                    LOG_err << "Incorrect user handle in pending outshare: " << uid;
                    assert(!"Incorrect user handle in pending outshare");
                    continue;
                }
            }

            w.serializebyte(len);
            w.serializenodehandle(h);

            bool isEmail = len;
            if (isEmail) // uid is an email
            {
                w.serializebinary((byte*)uid.data(), uid.size());
            }
            else    // user's handle in binary format, 8 bytes
            {
                handle uh;
#ifndef NDEBUG
                    int uhsize =
#endif
                Base64::atob(uid.c_str(), (byte*)&uh, sizeof uh);
                assert(uhsize == MegaClient::USERHANDLE);
                w.serializehandle(uh);
            }
        }
    }

    return result;
}

bool KeyManager::deserializeFromLTLV(const string& blob, map<string, string>& data)
{
    // blob format as follows:
    // [len.1 tag.len lenValue.2|6 value.lenValue]*
    // if lenValue == 0xFFFF  -> length is indicated by next 4 extra bytes
    // if lenValue < 0xFFFF   -> actual length (no extra bytes present)

    CacheableReader r(blob);

    while(r.hasdataleft())
    {
        // length of the tag
        byte len = 0;
        if (!r.unserializebyte(len))
        {
            LOG_err << "Corrupt LTLV: len of tag";
            return false;
        }

        // read the tag
        string tag;
        tag.resize(len);
        if (!r.unserializebinary((byte*)tag.data(), tag.size()))
        {
            LOG_err << "Corrupt LTLV: tag";
            return false;
        }

        // len of the value
        uint32_t lenValue = 0;
        uint16_t lenValue16 = 0;
        bool success = r.unserializeu16(lenValue16);
        lenValue16 = ntohs(lenValue16); // Webclient sets length as BigEndian
        if (lenValue16 == 0xFFFF)
        {
            success = r.unserializeu32(lenValue);
            lenValue = ntohl(lenValue);
        }
        else
        {
            lenValue = lenValue16;
        }

        if (!success)
        {
            LOG_err << "Corrupt LTLV: value len";
            return false;
        }

        // read the value
        string value;
        value.resize(lenValue);
        if (!r.unserializebinary((byte*)value.data(), value.size()))
        {
            LOG_err << "Corrupt LTLV: value";
            return false;
        }

        data[tag] = value;
    }

    return true;
}

string KeyManager::serializeToLTLV(const map<string, string>& values)
{
    // Encoded format as follows:
    // [len.1 tag.len lenValue.2|6 value.lenValue]*
    // if length of value < 0xFFFF -> 2 bytes for lenValue
    // else -> 2 bytes set as 0xFFFF and 4 extra bytes for actual length

    string result;

    CacheableWriter w(result);

    for (const auto& it : values)
    {
        // write tag length
        w.serializebyte(static_cast<byte>(it.first.size()));
        // write tag
        w.serializebinary((byte*)it.first.data(), it.first.size());

        // write value length
        if (it.second.size() < 0xFFFF)
        {
            uint16_t lenValue16 = static_cast<uint16_t>(it.second.size());
            uint16_t lenValue16BE = htons(lenValue16); // Webclient sets length as BigEndian
            w.serializeu16(lenValue16BE);
        }
        else // excess, 4 extra bytes
        {
            w.serializeu16(0xFFFF);
            uint32_t lenValue32 = static_cast<uint32_t>(it.second.size());
            uint32_t lenValue32BE = htonl(lenValue32);
            w.serializeu32(lenValue32BE);
        }

        // write value
        w.serializebinary((byte*)it.second.data(), it.second.size());
    }

    return result;
}

bool KeyManager::deserializePendingInshares(KeyManager& km, const string &blob)
{
    // clean old data, so we don't left outdated pending inshares in place
    km.mPendingInShares.clear();

    // key is the node handle, value includes the user's handle (8 bytes) and the encrypted share key
    map<string, string> decodedBlob;
    if (!deserializeFromLTLV(blob, decodedBlob))
    {
        LOG_err << "Pending inshare is corrupt";
        return false;
    }

    for ( const auto& it : decodedBlob )
    {

        if (it.second.size() < sizeof(handle)) // it may have only the user handle (no share key yet)
        {
            LOG_err << "Pending inshare is corrupt: incorrect value size";
            return false;
        }

        // user handle (sharer) and share key
        CacheableReader r(it.second);

        handle uh = UNDEF;
        string shareKey;
        shareKey.resize(it.second.size() - sizeof(uh));
        if (!r.unserializehandle(uh)
                || !r.unserializebinary((byte*)shareKey.data(), shareKey.size()))
        {
            LOG_err << "Pending inshare is corrupt: incorrect sharer handle or sharekey";
            return false;
        }

        km.mPendingInShares[it.first] = pair<handle, string>(uh, shareKey);
    }

    return true;
}

string KeyManager::serializePendingInshares() const
{
    map<string, string> pendingInsharesToEncode;

    for (const auto& it : mPendingInShares)
    {
        string value;
        CacheableWriter w(value);

        w.serializehandle(it.second.first); // share's owner user handle
        w.serializebinary((byte*)it.second.second.data(), it.second.second.size());

        pendingInsharesToEncode[it.first] = value;
    }

    return serializeToLTLV(pendingInsharesToEncode);
}

bool KeyManager::deserializeBackups(KeyManager& km, const string &blob)
{
    // FIXME: add support to deserialize backups
    km.mBackups = blob;
    return true;
}

string KeyManager::serializeWarnings() const
{
    return serializeToLTLV(mWarnings);
}

bool KeyManager::deserializeWarnings(KeyManager& km, const string &blob)
{
    // clean old data, so we don't left outdated warnings
    km.mWarnings.clear();

    return deserializeFromLTLV(blob, km.mWarnings);
}


string KeyManager::computeSymmetricKey(handle user)
{
    User *u = mClient.finduser(user, 0);
    if (!u)
    {
        return std::string();
    }

    const string *cachedav = u->getattr(ATTR_CU25519_PUBK);
    if (!cachedav)
    {
        LOG_warn << "Unable to generate symmetric key. Public key not cached.";
        if (mClient.statecurrent && mClient.mAuthRingsTemp.find(ATTR_CU25519_PUBK) == mClient.mAuthRingsTemp.end())
        {
            // if statecurrent=true -> contact keys should have been fetched
            // if no temporal authring for Cu25519 -> contact keys should be in cache
            LOG_warn << "Public key not cached with the authring already updated.";
            assert(false);
            mClient.sendevent(99464, "KeyMgr / Ed/Cu retrieval failed");
        }
        return std::string();
    }

    std::string sharedSecret;
    ECDH ecdh(mClient.chatkey->getPrivKey(), *cachedav);
    if (!ecdh.computeSymmetricKey(sharedSecret))
    {
        return std::string();
    }

    std::string step1;
    step1.resize(32);
    CryptoPP::HMAC<CryptoPP::SHA256> hmac1(nullptr, 0);
    hmac1.CalculateDigest((byte *)step1.data(), (byte *)sharedSecret.data(), sharedSecret.size());

    std::string sharedKey;
    sharedKey.resize(32);
    CryptoPP::HMAC<CryptoPP::SHA256> hmac2((byte *)step1.data(), step1.size());
    hmac2.CalculateDigest((byte *)sharedKey.data(), (byte *)SVCRYPTO_PAIRWISE_KEY.data(), SVCRYPTO_PAIRWISE_KEY.size());

    sharedKey.resize(CryptoPP::AES::BLOCKSIZE);
    return sharedKey;
}

bool KeyManager::decodeRSAKey()
{
//    LOG_verbose << Base64::btoa(mPrivRSA) << "\n\n" << Utils::stringToHex(mPrivRSA);

    string currentPK;
    mClient.asymkey.serializekey(&currentPK, AsymmCipher::PRIVKEY_SHORT);

    // Compare serialized keys using find just in case pqdKey has extra bytes. It should be found at pos = 0.
    size_t pos = mPrivRSA.find(currentPK);
    bool keyOk = (pos == 0);

    // Keep client RSA key serialized as received by login/"ug"
    // It is used by intermediate layer and login/ug are 4 Ints long, not 3, which could cause problems when comparing them.
    // mClient.mPrivKey = Base64::btoa(mPrivRSA);

    // update asymcipher to use RSA from ^!keys
    if (keyOk && !mClient.asymkey.setkey(AsymmCipher::PRIVKEY_SHORT, (const unsigned char*)mPrivRSA.data(), (int)mPrivRSA.size()))
    {
        keyOk = false;
    }

    assert(keyOk);
    return keyOk;
}

void KeyManager::updateAuthring(attr_t at, string& value)
{
    string& authring = (at == ATTR_AUTHRING) ? mAuthEd25519 : mAuthCu25519;
    authring = std::move(value);
    mClient.mAuthRings.erase(at);
    if (authring.empty())
    {
        mClient.mAuthRings.emplace(at, AuthRing(at, TLVstore()));
    }
    else
    {
        mClient.mAuthRings.emplace(at, AuthRing(at, authring));
    }
}

void KeyManager::updateShareKeys(map<handle, pair<string, ShareKeyFlags>>& shareKeys)
{
    for (const auto& itNew : shareKeys)
    {
        handle h = itNew.first;

        const auto& itOld = mShareKeys.find(h);
        if (itOld != mShareKeys.end() && itNew.second != itOld->second)
        {
            if (itNew.second.first != itOld->second.first)
            {
                LOG_warn << "[keymgr] Sharekey for " << toNodeHandle(h) << " has changed. Updating...";
                assert(!itOld->second.second[ShareKeyFlagsId::TRUSTED]);
                mClient.sendevent(99469, "KeyMgr / Replacing sharekey");
            }
            else
            {
                if (itNew.second.second[ShareKeyFlagsId::TRUSTED] != itOld->second.second[ShareKeyFlagsId::TRUSTED])
                {
                    LOG_warn << "[keymgr] Trust for " << toNodeHandle(h) << " share key has changed ("
                             << static_cast<bool>(itOld->second.second[ShareKeyFlagsId::TRUSTED]) << " -> "
                             << static_cast<bool>(itNew.second.second[ShareKeyFlagsId::TRUSTED]) << "). Updating...";
                }
                if (itNew.second.second[ShareKeyFlagsId::INUSE] != itOld->second.second[ShareKeyFlagsId::INUSE])
                {
                    LOG_debug << "[keymgr] In-use flag for " << toNodeHandle(h) << " share key has changed ("
                             << static_cast<bool>(itOld->second.second[ShareKeyFlagsId::INUSE]) << " -> "
                             << static_cast<bool>(itNew.second.second[ShareKeyFlagsId::INUSE]) << "). Updating...";

                }
                // Compare the remaining flags
                ShareKeyFlags mask(0x03); // ShareKeyFlagsId::TRUSTED and ShareKeyFlagsId::INUSE
                mask.flip();
                if ((itNew.second.second & mask) != (itOld->second.second & mask))
                {
                    LOG_warn << "[keymgr] Flags for " << toNodeHandle(h) << " share key has changed ("
                             << itOld->second.second.to_ulong() << " -> " << itNew.second.second.to_ulong() << "). Updating...";
                }
            }
        }
    }

    mShareKeys = std::move(shareKeys);

    // Set the sharekey to the node, if missing (since it might not have been received along with
    // the share itself (ok / k is discontinued since ^!keys)
    loadShareKeys();
}

bool KeyManager::verificationRequired(handle userHandle)
{
    if (mManualVerification)
    {
        return !mClient.areCredentialsVerified(userHandle);
    }

    // if no manual verification required, still check Ed25519 public key is SEEN
    AuthRingsMap::const_iterator it = mClient.mAuthRings.find(ATTR_AUTHRING);
    bool edAuthringFound = it != mClient.mAuthRings.end();
    return !edAuthringFound || (it->second.getAuthMethod(userHandle) < AUTH_METHOD_SEEN);
}

string KeyManager::serializeBackups() const
{
    // FIXME: once we add support to deserialize, adjust it here too
    return mBackups;
}

bool ScDbStateRecord::serialize(string* s) const
{
    CacheableWriter w(*s);
    w.serializestring(seqTag);
    w.serializeexpansionflags();
    return s;
}

ScDbStateRecord ScDbStateRecord::unserialize(const std::string& data)
{
    unsigned char ef[8] = {0, };
    ScDbStateRecord result;
    CacheableReader reader(data);
    if (reader.unserializestring(result.seqTag) &&
        reader.unserializeexpansionflags(ef, 0))
    {
        // future fields processed here
    }

    return result;
}

void MegaClient::getJSCDUserAttributes(GetJSCDUserAttributesCallback callback)
{
    // Sanity.
    assert(callback);

    // Try and get our hands on the currently logged in user.
    auto* user = ownuser();

    // No user? No attributes.
    if (!user)
        return callback({}, API_EFAILED);

    // Called if we couldn't retrieve the attributes.
    auto failed = [callback, this](error result) mutable {
        JSCDUserAttributesRetrieved(std::move(callback), result, nullptr);
    }; // failed

    // Called if we could retrieve the attributes.
    auto retrieved = [callback = std::move(callback), this]
                     (TLVstore* store, attr_t) mutable {
        JSCDUserAttributesRetrieved(std::move(callback), API_OK, store);
    }; // retrieved

    // Try and retrieve the user's JSCD user attributes.
    getua(user,
          ATTR_JSON_SYNC_CONFIG_DATA,
          0,
          std::move(failed),
          nullptr,
          std::move(retrieved));
}

void MegaClient::createJSCDUserAttributes(GetJSCDUserAttributesCallback callback)
{
    // Sanity.
    assert(callback);

    // Generate the content for the user's JSCD user attributes.
    auto content = [this]() {
        // Convenience.
        constexpr auto Length = SymmCipher::KEYLENGTH;

        // Instantiate a TLV store to contain the JSCD attributes.
        TLVstore store;

        // Key used to authenticate the sync configuration database.
        store.set("ak", rng.genstring(Length));

        // Key used to encrypt the sync configuration database.
        store.set("ck", rng.genstring(Length));

        // The name of the sync configuration database.
        store.set("fn", rng.genstring(Length));

        // Translate the store into an encrypted binary blob.
        return std::unique_ptr<std::string>(store.tlvRecordsToContainer(rng, &key));
    }();

    // Called when the attribute has been created.
    auto created = [callback = std::move(callback), this](Error result) mutable {
        JSCDUserAttributesCreated(std::move(callback), result);
    }; // created

    // Try and create the JSCD user attribute.
    putua(ATTR_JSON_SYNC_CONFIG_DATA,
          reinterpret_cast<const byte*>(content->data()),
          static_cast<unsigned>(content->size()),
          0,
          UNDEF,
          0,
          0,
          std::move(created));
}

void MegaClient::injectSyncSensitiveData(CommandLogin::Completion callback,
                                         Error result)
{
    // Sanity.
    assert(callback);

    // Couldn't log the user in.
    if (result != API_OK)
        return callback(result);

    // Called when the JSCD user attributes have been retrieved.
    auto retrieved = [callback = std::move(callback), this]
                     (JSCDUserAttributes attributes, Error result) {
        // Couldn't retrieve the JSCD user attributes.
        if (result != API_OK)
        {
            LOG_warn << "Couldn't retrieve or create JSCD user attributes: "
                     << result;

            // This shouldn't prevent the user from logging on, however.
            return callback(API_OK);
        }

        SyncSensitiveData data;

        // Prepare sensitive data for injection.
        data.mJSCDUserAttributes = std::move(attributes);
        data.mStateCacheKey.assign(reinterpret_cast<const char*>(key.key),
                                   sizeof(key.key));

        // Inject sensitive data into the sync engine.
        syncs.injectSyncSensitiveData(std::move(data));

        // Let the user know that they're logged in.
        callback(API_OK);
    }; // retrieved

    // Try and retrieve the JSCD user attributes.
    getJSCDUserAttributes(std::move(retrieved));
}

void MegaClient::JSCDUserAttributesCreated(GetJSCDUserAttributesCallback callback,
                                           Error result)
{
    // Sanity.
    assert(callback);

    // Couldn't create attribute and it wasn't created by another client.
    if (result != API_OK && result != API_EEXPIRED)
        return callback({}, result);

    // Attribute was created by us or by another client so retrieve it.
    getJSCDUserAttributes(std::move(callback));
}

void MegaClient::JSCDUserAttributesRetrieved(GetJSCDUserAttributesCallback callback,
                                             Error result,
                                             TLVstore* store)
{
    // Sanity.
    assert(callback);

    // The user doesn't have any JSCD user attributes.
    if (result == API_ENOENT)
        return createJSCDUserAttributes(std::move(callback));

    // We weren't able to retrieve the user's JSCD user attributes.
    if (result != API_OK)
        return callback({}, result);

    JSCDUserAttributes attributes;

    // Extract JSCD user attributes.
    store->get("ak", attributes.mAuthenticationKey);
    store->get("ck", attributes.mCipherKey);
    store->get("fn", attributes.mFileName);

    // Saves a little typing.
    auto valid = [](const std::string& attribute) {
        return attribute.size() == SymmCipher::KEYLENGTH;
    }; // valid

    // Check validity of JSCD user attributes.
    if (!valid(attributes.mAuthenticationKey))
        return callback({}, API_EINTERNAL);

    if (!valid(attributes.mCipherKey))
        return callback({}, API_EINTERNAL);

    if (!valid(attributes.mFileName))
        return callback({}, API_EINTERNAL);

    // Translate filename to base64, as expected.
    attributes.mFileName = Base64::btoa(attributes.mFileName);

    // Pass attributes to callback.
    callback(std::move(attributes), API_OK);
}

} // namespace
