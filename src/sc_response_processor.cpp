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
        if (!sc_checkActionPacket(mlastAPDeletedNode.get()))
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
                        sc_updatenode();
                        break;

                    case makeNameid("t"):
                    {
                        bool isMoveOperation = false;
                        // node addition
                        {
                            if (!loggedIntoFolder())
                                useralerts.beginNotingSharedNodes();
                            handle originatingUser =
                                sc_newnodes(fetchingnodes ? nullptr : mlastAPDeletedNode.get(),
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
                        mlastAPDeletedNode = sc_deltree();
                        break;

                    case makeNameid("s"):
                    case makeNameid("s2"):
                        // share addition/update/revocation
                        if (sc_shares())
                        {
                            int creqtag = reqtag;
                            reqtag = 0;
                            mergenewshares(1);
                            reqtag = creqtag;
                        }
                        break;

                    case name_id::c:
                        // contact addition/update
                        sc_contacts();
                        break;

                    case makeNameid("fa"):
                        // file attribute update
                        sc_fileattr();
                        break;

                    case makeNameid("ua"):
                        // user attribute update
                        sc_userattr();
                        break;

                    case name_id::psts:
                    case name_id::psts_v2:
                    case makeNameid("ftr"):
                        if (sc_upgrade(name))
                        {
                            app->account_updated();
                            abortbackoff(true);
                        }
                        break;

                    case name_id::pses:
                        sc_paymentreminder();
                        break;

                    case name_id::ipc:
                        // incoming pending contact request (to us)
                        sc_ipc();
                        break;

                    case makeNameid("opc"):
                        // outgoing pending contact request (from us)
                        sc_opc();
                        break;

                    case name_id::upci:
                        // incoming pending contact request update (accept/deny/ignore)
                        sc_upc(true);
                        break;

                    case name_id::upco:
                        // outgoing pending contact request update (from them,
                        // accept/deny/ignore)
                        sc_upc(false);
                        break;

                    case makeNameid("ph"):
                        // public links handles
                        sc_ph();
                        break;

                    case makeNameid("se"):
                        // set email
                        sc_se();
                        break;
#ifdef ENABLE_CHAT
                    case makeNameid("mcpc"):
                    {
                        readingPublicChat = true;
                    } // fall-through
                    case makeNameid("mcc"):
                        // chat creation / peer's invitation / peer's removal
                        sc_chatupdate(readingPublicChat);
                        break;

                    case makeNameid("mcfpc"): // fall-through
                    case makeNameid("mcfc"):
                        // chat flags update
                        sc_chatflags();
                        break;

                    case makeNameid("mcpna"): // fall-through
                    case makeNameid("mcna"):
                        // granted / revoked access to a node
                        sc_chatnode();
                        break;

                    case name_id::mcsmp:
                        // scheduled meetings updates
                        sc_scheduledmeetings();
                        break;

                    case name_id::mcsmr:
                        // scheduled meetings removal
                        sc_delscheduledmeeting();
                        break;
#endif
                    case makeNameid("uac"):
                        sc_uac();
                        break;

                    case makeNameid("la"):
                        // last acknowledged
                        sc_la();
                        break;

                    case makeNameid("ub"):
                        // business account update
                        sc_ub();
                        break;

                    case makeNameid("sqac"):
                        // storage quota allowance changed
                        sc_sqac();
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
                        sc_uec();
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
                               actionpacketsCurrent = false;

                               assert(!mNodeTreeIsChanging.owns_lock());
                               mNodeTreeIsChanging =
                                   std::unique_lock<recursive_mutex>(nodeTreeMutex);

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
                           assert(mNodeTreeIsChanging.owns_lock());
                           mNodeTreeIsChanging.unlock();
                           return true;
                       });
    mSCFilters.emplace("{[a{",
                       [this](JSON* json)
                       {
                           // std::string tmp;
                           // json->storeobject(&tmp);
                           // LOG_debug << "Got action packet: " << tmp;
                           // return true;

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