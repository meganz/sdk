#include "mega.h"
#include "mega/logging.h"
#include "mega/megaclient.h"

namespace mega
{
bool MegaClient::checksca(JSON* json)
{ // borrow the action packect handle logic
    auto actionpacketStart = json->pos;
    if (json->enterobject())
    {
        // Check if it is ok to process the current action packet.
        if (!sc_checkActionPacket(json, mlastAPDeletedNode.get()))
        {
            // We can't continue actionpackets until we know the next mCurrentSeqtag to
            // match against, wait for the CS request to deliver it.
            assert(reqs.cmdsInflight());
            json->pos = actionpacketStart;
            return false;
        }
    }
    json->pos = actionpacketStart;
    return true;
}

bool MegaClient::procsca(JSON* json)
{ // borrow the action packect handle logic

    if (json->enterobject())
    {
        // the "a" attribute is guaranteed to be the first in the object
        if (json->getnameid() == makeNameid("a"))
        {
            if (!statecurrent)
            {
                fnstats.actionPackets++;
            }

            auto name = json->getnameidvalue();

            // only process server-client request if not marked as
            // self-originating ("i" marker element guaranteed to be following
            // "a" element if present)
            if (fetchingnodes || !Utils::startswith(json->pos, "\"i\":\"") ||
                memcmp(json->pos + 5, sessionid, sizeof sessionid) ||
                json->pos[5 + sizeof sessionid] != '"' || name == name_id::d ||
                name == 't') // we still set 'i' on move commands to produce backward
                             // compatible actionpackets, so don't skip those here
            {
#ifdef ENABLE_CHAT
                bool readingPublicChat = false;
#endif
                switch (name)
                {
                    case name_id::u:
                        // node update
                        sc_updatenode(json);
                        break;

                    case makeNameid("t"):
                    {
                        bool isMoveOperation = false;
                        // node addition
                        {
                            if (!loggedIntoFolder())
                                useralerts.beginNotingSharedNodes();
                            handle originatingUser =
                                sc_newnodes(json,
                                            fetchingnodes ? nullptr : mlastAPDeletedNode.get(),
                                            isMoveOperation);
                            mergenewshares(1);
                            if (!loggedIntoFolder())
                                useralerts.convertNotedSharedNodes(true, originatingUser);
                        }
                        mlastAPDeletedNode = nullptr;
                    }
                    break;

                    case name_id::d:
                        // node deletion
                        mlastAPDeletedNode = sc_deltree(json);
                        break;

                    case makeNameid("s"):
                    case makeNameid("s2"):
                        // share addition/update/revocation
                        if (sc_shares(json))
                        {
                            int creqtag = reqtag;
                            reqtag = 0;
                            mergenewshares(1);
                            reqtag = creqtag;
                        }
                        break;

                    case name_id::c:
                        // contact addition/update
                        sc_contacts(json);
                        break;

                    case makeNameid("fa"):
                        // file attribute update
                        sc_fileattr(json);
                        break;

                    case makeNameid("ua"):
                        // user attribute update
                        sc_userattr(json);
                        break;

                    case name_id::psts:
                    case name_id::psts_v2:
                    case makeNameid("ftr"):
                        if (sc_upgrade(json, name))
                        {
                            app->account_updated();
                            abortbackoff(true);
                        }
                        break;

                    case name_id::pses:
                        sc_paymentreminder(json);
                        break;

                    case name_id::ipc:
                        // incoming pending contact request (to us)
                        sc_ipc(json);
                        break;

                    case makeNameid("opc"):
                        // outgoing pending contact request (from us)
                        sc_opc(json);
                        break;

                    case name_id::upci:
                        // incoming pending contact request update (accept/deny/ignore)
                        sc_upc(json, true);
                        break;

                    case name_id::upco:
                        // outgoing pending contact request update (from them,
                        // accept/deny/ignore)
                        sc_upc(json, false);
                        break;

                    case makeNameid("ph"):
                        // public links handles
                        sc_ph(json);
                        break;

                    case makeNameid("se"):
                        // set email
                        sc_se(json);
                        break;
#ifdef ENABLE_CHAT
                    case makeNameid("mcpc"):
                    {
                        readingPublicChat = true;
                    } // fall-through
                    case makeNameid("mcc"):
                        // chat creation / peer's invitation / peer's removal
                        sc_chatupdate(json, readingPublicChat);
                        break;

                    case makeNameid("mcfpc"): // fall-through
                    case makeNameid("mcfc"):
                        // chat flags update
                        sc_chatflags(json);
                        break;

                    case makeNameid("mcpna"): // fall-through
                    case makeNameid("mcna"):
                        // granted / revoked access to a node
                        sc_chatnode(json);
                        break;

                    case name_id::mcsmp:
                        // scheduled meetings updates
                        sc_scheduledmeetings(json);
                        break;

                    case name_id::mcsmr:
                        // scheduled meetings removal
                        sc_delscheduledmeeting(json);
                        break;
#endif
                    case makeNameid("uac"):
                        sc_uac(json);
                        break;

                    case makeNameid("la"):
                        // last acknowledged
                        sc_la(json);
                        break;

                    case makeNameid("ub"):
                        // business account update
                        sc_ub(json);
                        break;

                    case makeNameid("sqac"):
                        // storage quota allowance changed
                        sc_sqac(json);
                        break;

                    case makeNameid("asp"):
                        // new/update of a Set
                        sc_asp();
                        break;

                    case makeNameid("ass"):
                        sc_ass();
                        break;

                    case makeNameid("asr"):
                        // removal of a Set
                        sc_asr();
                        break;

                    case makeNameid("aep"):
                        // new/update of a Set Element
                        sc_aep();
                        break;

                    case makeNameid("aer"):
                        // removal of a Set Element
                        sc_aer();
                        break;
                    case makeNameid("pk"):
                        // pending keys
                        sc_pk();
                        break;

                    case makeNameid("uec"):
                        // User Email Confirm (uec)
                        sc_uec(json);
                        break;

                    case makeNameid("cce"):
                        // credit card for this user is potentially expiring soon or new
                        // card is registered
                        sc_cce();
                        break;
                }
            }
            else
            {
                mlastAPDeletedNode = nullptr;
            }
        }

        json->leaveobject();
    }
    else
    {
        return false;
    }

    return true;
}

void MegaClient::clearSC()
{
    jsonsc.pos = NULL;
    mSCJsonSplitter.clear();
    mSCChunkedProgress = 0;
    processingSC = false;
    mFirstChunkProcessed = false;
}

void MegaClient::setupSCFilters()
{
    // Parsing of chunk started
    mSCFilters.emplace("<",
                       [this](JSON*)
                       {
                           if (!mFirstChunkProcessed)
                           {
                               statecurrent = false;

                               assert(!mNodeTreeIsChanging.owns_lock());
                               mNodeTreeIsChanging =
                                   std::unique_lock<recursive_mutex>(nodeTreeMutex);

                               originalAC = actionpacketsCurrent;
                               actionpacketsCurrent = false;

                               mlastAPDeletedNode.reset();

                               mFirstChunkProcessed = true;
                           }
                           else
                           {
                               assert(!mNodeTreeIsChanging.owns_lock());
                               mNodeTreeIsChanging =
                                   std::unique_lock<recursive_mutex>(nodeTreeMutex);
                           }

                           return true;
                       });
    // Parsing of chunk finished
    mSCFilters.emplace(">",
                       [this](JSON*)
                       {
                           // assert(mNodeTreeIsChanging.owns_lock());
                           if (mNodeTreeIsChanging.owns_lock())
                               mNodeTreeIsChanging.unlock();
                           return true;
                       });
    mSCFilters.emplace("{[a{",
                       [this](JSON* json)
                       {
                           if (!checksca(json))
                           {
                               return false;
                           }

                           return procsca(json);
                       });
    // End of node array
    mSCFilters.emplace("{[a",
                       [this](JSON* json)
                       {
                           sc_checkSequenceTag(string());
                           json->enterarray();
                           return json->leavearray();
                       });
    mSCFilters.emplace("{\"w",
                       [this](JSON* json)
                       {
                           return json->storeobject(&scnotifyurl);
                       });

    mSCFilters.emplace("{\"ir",
                       [this](JSON* json)
                       {
                           insca_notlast = json->getint() == 1;
                           return true;
                       });

    mSCFilters.emplace("{\"sn",
                       [this](JSON* json)
                       {
                           scsn.setScsn(json);
                           assert(!mCurrentSeqtagSeen);
                           notifypurge();
                           if (sctable)
                           {
                               if (!pendingcs && !csretrying && !reqs.readyToSend())
                               {
                                   LOG_debug << "DB transaction COMMIT (sessionid: "
                                             << string(sessionid, sizeof(sessionid)) << ")";
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

                           return true;
                       });

    mSCFilters.emplace(
        "{",
        [this](JSON*)
        {
            if (!useralerts.isDeletedSharedNodesStashEmpty())
            {
                useralerts.purgeNodeVersionsFromStash();
                useralerts.convertStashedDeletedSharedNodes();
            }

            LOG_debug << "Processing of action packets for " << string(sessionid, sizeof(sessionid))
                      << " finished.  More to follow: " << insca_notlast;
            mergenewshares(1);
            applykeys();
            mNewKeyRepository.clear();

            if (!statecurrent && !insca_notlast) // with actionpacket spoonfeeding, just
                                                 // finishing a batch does not mean we are
                                                 // up to date yet - keep going while "ir":1
            {
                if (fetchingnodes)
                {
                    notifypurge();
                    if (sctable)
                    {
                        LOG_debug << "DB transaction COMMIT (sessionid: "
                                  << string(sessionid, sizeof(sessionid)) << ")";
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

                    if (!mBlockedSet &&
                        mCachedStatus.lookup(CacheableStatus::STATUS_BLOCKED,
                                             0)) // block state not received in this execution, and
                                                 // cached says we were blocked last time
                    {
                        LOG_debug << "cached blocked states reports blocked, and no block state "
                                     "has been received before, issuing whyamiblocked";
                        whyamiblocked(); // lets query again, to trigger transition and
                                         // restoreSyncs
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
                        // now that we have fetched everything and caught up actionpackets
                        // since that state, our next sc request can be for useralerts
                        useralerts.begincatchup = true;
                    }
                }
                else
                {
                    WAIT_CLASS::bumpds();
                    fnstats.timeToCurrent = Waiter::ds - fnstats.startTime;
                }
                uint64_t numNodes = mNodeManager.getNodeCount();
                fnstats.nodesCurrent = static_cast<long long>(numNodes);

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
                // Don't start sync activity until `statecurrent` as it could take actions
                // based on old state The reworked sync code can figure out what to do once
                // fully up to date.
                mNodeTreeIsChanging.unlock();
                if (!syncsAlreadyLoadedOnStatecurrent)
                {
                    syncs.resumeSyncsOnStateCurrent();
                    syncsAlreadyLoadedOnStatecurrent = true;
                }
#endif
                if (tctable && cachedfiles.size())
                {
                    TransferDbCommitter committer(tctable);
                    for (unsigned int i = 0; i < cachedfiles.size(); i++)
                    {
                        direction_t type = NONE;
                        File* file =
                            app->file_resume(&cachedfiles.at(i), &type, cachedfilesdbids.at(i));
                        if (!file || (type != GET && type != PUT))
                        {
                            tctable->del(cachedfilesdbids.at(i));
                            continue;
                        }
                        if (!startxfer(type,
                                       file,
                                       committer,
                                       false,
                                       false,
                                       false,
                                       UseLocalVersioningFlag,
                                       nullptr,
                                       nextreqtag())) // TODO: should we have serialized
                                                      // these flags and restored them?
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

                sendevent(99426, report.c_str(), 0); // Treeproc performance log

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

                // if ^!keys doesn't exist yet -> migrate the private keys from legacy attrs
                // to ^!keys
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
                bool scTagNotCaughtUp =
                    !mScDbStateRecord.seqTag.empty() && !mLargestEverSeenScSeqTag.empty() &&
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
        });
}

m_off_t MegaClient::processSCChunk(const char* chunk)
{
    m_off_t consumed = 0;

    bool start = !jsonsc.pos;
    jsonsc.begin(chunk);

    if (start)
    {
        if (*chunk != '{')
        {
            clearSC();
            return 0;
        }

        assert(mSCJsonSplitter.isStarting());
    }

    consumed += mSCJsonSplitter.processChunk(&mSCFilters, jsonsc.pos);
    if (mSCJsonSplitter.hasFailed())
    {
        // stop the processing

        clearSC();
        return 0;
    }

    mSCChunkedProgress += static_cast<size_t>(consumed);
    jsonsc.begin(chunk + consumed);
    if (mSCJsonSplitter.hasFinished())
    {
        assert(!chunk[consumed]);

        clearSC();
    }

    return consumed;
}

size_t MegaClient::chunkedSCProgress()
{
    return static_cast<size_t>(mSCChunkedProgress);
}
}