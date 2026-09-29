/**
 * @file sc_streaming_parser.cpp
 * @brief Server-client packet streaming parser
 *
 * (c) 2026 by Mega Limited, Auckland, New Zealand
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

#include "mega/sc_streaming_parser.h"

#include "mega/megaclient.h"
#include "mega/tree_filters.h"

namespace mega
{

ScStreamingParser::ScStreamingParser(MegaClient& client, PrnGen& rng):
    mClient(client),
    mNeedToPurge(false),
    mBt(rng),
    mTreeFilters(client)
{
    clear();
}

void ScStreamingParser::init()
{
    // Start of parsing
    mFilters.emplace("",
                     [this](JSON*)
                     {
                         mOriginalAC = mClient.actionpacketsCurrent;
                         mClient.actionpacketsCurrent = false;
                         mClient.insca_notlast = false;

                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    // Parsing of chunk started
    mFilters.emplace("<",
                     [this](JSON*)
                     {
                         acquireLock();

                         // Start timer
                         mCcst = std::make_unique<CodeCounter::ScopeTimer>(
                             mClient.performanceStats.scProcessingTime);

                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    // Action packets (one by one)

    mFilters.emplace("{[a{\"a",
                     [this](JSON* json)
                     {
                         mClient.sc_updateStats();

                         mActionName = json->getnameid(json->pos + 1);

                         // A new share should be followed by a "t" packet
                         assert(mIsNewSharesMerged || mActionName == makeNameid("t"));

                         if (mActionName == makeNameid("t"))
                         {
                             mTreeFilters.start(mFiltersChain,
                                                [this]()
                                                {
                                                    checkActionPacket();
                                                });
                         }

                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    mFilters.emplace("{[a{\"i",
                     [this](JSON* json)
                     {
                         string jsonSid;
                         json->storeobject(&jsonSid);

                         mIsSelfOriginating =
                             jsonSid.size() == sizeof(mClient.sessionid) &&
                             !memcmp(jsonSid.data(), mClient.sessionid, sizeof(mClient.sessionid));

                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    mFilters.emplace("{[a{\"st",
                     [this](JSON* json)
                     {
                         json->storeobject(&mSeqTag);

                         if (!mClient.sc_checkSequenceTag(mSeqTag))
                         {
                             releaseLock();
                             return JSONSplitter::CallbackResult::PAUSED;
                         }

                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    mFilters.emplace("{[a{\"isn",
                     [this](JSON* json)
                     {
                         handle isn;
                         if (json->storebinary(reinterpret_cast<byte*>(&isn), sizeof(isn)) ==
                             sizeof(mInterimSn))
                         {
                             mInterimSn = isn;
                         }
                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    mFilters.emplace("{[a{",
                     [this](JSON* json)
                     {
                         if (mActionName == 0)
                         {
                             checkActionPacket();
                             mLastAPDeletedNode.reset();
                         }
                         else if (mTreeFilters.isStarted())
                         {
                             mTreeFilters.end(mFiltersChain);
                             mLastAPDeletedNode.reset();
                             mIsNewSharesMerged = true;
                         }
                         else
                         {
                             checkActionPacket();
                             mLastAPDeletedNode =
                                 mClient.sc_procActionPacketWithoutCommonTags(*json,
                                                                              mActionName,
                                                                              mIsSelfOriginating,
                                                                              &mIsNewSharesMerged);
                         }

                         if (mInterimSn != UNDEF && isnCanBeProcessed())
                         {
                             mClient.scsn.setScsn(mInterimSn);
                             mInterimSn = UNDEF;
                             mNeedToPurge = true;
                         }

                         clearActionPacketData();
                         return JSONSplitter::ResultFromBool(json->leaveobject());
                     });

    // End of action packets
    mFilters.emplace("{[a",
                     [this](JSON* json)
                     {
                         // No more Actions Packets. Force it to advance and process all
                         // the remaining command responses until a new "st" is found, if
                         // any. It will also process the latest command response
                         // associated (by the Sequence Tag) with the latest AP processed
                         // here.
                         mClient.sc_checkSequenceTag(string());

                         // This is intended to consume the '[' character if the array
                         // was empty and an empty array arrives here "[]".
                         // If the array was not empty, we receive here only the
                         // remaining character ']' and this call doesn't have any
                         // effect.
                         json->enterarray();

                         return JSONSplitter::ResultFromBool(json->leavearray());
                     });

    mFilters.emplace("{\"w",
                     [this](JSON* json)
                     {
                         return JSONSplitter::ResultFromBool(
                             json->storeobject(&mClient.scnotifyurl));
                     });

    mFilters.emplace("{\"ir",
                     [this](JSON* json)
                     {
                         // when spoonfeeding is in action, there may still be more
                         // actionpackets to be delivered.
                         mClient.insca_notlast = json->getint() == 1;
                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    mFilters.emplace("{\"sn",
                     [this](JSON* json)
                     {
                         mClient.sc_storeSn(*json);
                         mNeedToPurge = false;
                         mBt.backoff(MIN_COMMIT_INTERVAL);
                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    // Parsing of sc packet finished
    mFilters.emplace("{",
                     [&, this](JSON*)
                     {
                         mClient.sc_procEoo(mNodeTreeIsChanging, mOriginalAC);
                         releaseLock();
                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    // Parsing of chunk finished
    mFilters.emplace(">",
                     [this](JSON*)
                     {
                         mCcst.reset();

                         WAIT_CLASS::bumpds();

                         if (mNeedToPurge && mBt.armed())
                         {
                             mClient.sc_purgeAndCommit();
                             mNeedToPurge = false;
                             mBt.backoff(MIN_COMMIT_INTERVAL);
                         }

                         return JSONSplitter::CallbackResult::SUCCESS;
                     });

    mFiltersChain.emplace_back(&mFilters);
}

m_off_t ScStreamingParser::process(const char* data)
{
    return mJsonSplitter.processChunk(mFiltersChain, data);
}

bool ScStreamingParser::hasStarted()
{
    return !mJsonSplitter.isStarting();
}

bool ScStreamingParser::isFinished()
{
    return mJsonSplitter.hasFinished();
}

bool ScStreamingParser::isFailed()
{
    return mJsonSplitter.hasFailed();
}

bool ScStreamingParser::isPaused()
{
    return mJsonSplitter.hasPaused();
}

bool ScStreamingParser::isLastReceived()
{
    return mLast;
}

void ScStreamingParser::setLastReceived()
{
    mLast = true;
}

void ScStreamingParser::clear()
{
    if (mNeedToPurge)
    {
        mClient.sc_procTerm(isDuringMoveOperation());
        mClient.sc_purgeAndCommit();
    }

    mNeedToPurge = false;
    mBt.reset();

    if (mTreeFilters.isStarted())
    {
        mTreeFilters.clear(mFiltersChain);
    }

    mJsonSplitter.clear();
    mLast = false;
    mLastAPDeletedNode = nullptr;
    mIsNewSharesMerged = true;

    mInterimSn = UNDEF;

    clearActionPacketData();

    if (mCcst)
    {
        mCcst.reset();
    }
    releaseLock();
}

void ScStreamingParser::clearActionPacketData()
{
    mActionName = 0;
    mIsSelfOriginating = false;
    mSeqTag.clear();
}

void ScStreamingParser::acquireLock()
{
    if (!mNodeTreeIsChanging.owns_lock())
    {
        if (mNodeTreeIsChanging.mutex() == &mClient.nodeTreeMutex)
        {
            mNodeTreeIsChanging.lock();
        }
        else
        {
            mNodeTreeIsChanging = std::unique_lock<recursive_mutex>(mClient.nodeTreeMutex);
        }
    }
}

void ScStreamingParser::releaseLock()
{
    if (mNodeTreeIsChanging.owns_lock())
    {
        mNodeTreeIsChanging.unlock();
    }
}

void ScStreamingParser::checkActionPacket()
{
    // 'st' is not present
    if (mSeqTag.empty())
    {
        [[maybe_unused]] const bool ret =
            mClient.sc_checkActionPacketWithoutSt(mActionName, mLastAPDeletedNode.get());
        assert(ret);
    }
}

bool ScStreamingParser::isnCanBeProcessed()
{
    // In below combo operations, isn is received in the first packet, but is processed only after
    // the last packet is processed.
    //   - Move ("d" + "t" packets)
    //   - New shares ("s" + "t" packets)
    return !isDuringMoveOperation() && mIsNewSharesMerged;
}

bool ScStreamingParser::isDuringMoveOperation()
{
    return mLastAPDeletedNode != nullptr;
}
} // namespace
