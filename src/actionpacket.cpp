/**
 * @file actionpacket.cpp
 * @brief Implementation of various actionpacket
 *
 * (c) 2013-2025 by Mega Limited, Auckland, New Zealand
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
#include "mega/actionpacket.h"


namespace mega
{

ActionpacketNewNodes::ActionpacketNewNodes(MegaClient* client)
{
    LOG_debug << "Construct of ActionpacketNewNodes";
    assert(client);

    // Parsing of chunk started
    mFilters.emplace("<",
                    [this, client](JSON*)
                    {
                        if (!mFirstChunkProcessed)
                        {
                            mPreviousHandleForAlert = UNDEF;
                            mMissingParentNodes.clear();

                            assert(!mNodeTreeIsChanging.owns_lock());
                            mNodeTreeIsChanging =
                                std::unique_lock<recursive_mutex>(client->nodeTreeMutex);

                            mFirstChunkProcessed = true;
                        }
                        else
                        {
                            assert(!mNodeTreeIsChanging.owns_lock());
                            mNodeTreeIsChanging =
                                std::unique_lock<recursive_mutex>(client->nodeTreeMutex);
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
    auto f = mFilters.emplace(
        "{{t[f{",
        [this, client](JSON* json)
        {
            static int i = 0;
            LOG_debug << "1by1 reading node:" << i++;
            if (client->readnode(json,
                                 0,
                                 PUTNODES_APP,
                                 nullptr,
                                 false,
                                 true,
                                 mMissingParentNodes,
                                 mPreviousHandleForAlert,
                                 nullptr, // allParents disabled because Syncs::triggerSync
                                 // does nothing when MegaClient::fetchingnodes is true
                                 nullptr,
                                 nullptr) != 1)
            {
                return false;
            }
            return json->leaveobject();
        });

    // End of node array
    f = mFilters.emplace("{{t[f",
                         [this, client](JSON* json)
                         {
                             LOG_debug << "Array reading node" << json->pos;
                             client->mergenewshares(0);
                             client->mNodeManager.checkOrphanNodes(mMissingParentNodes);

                             mPreviousHandleForAlert = UNDEF;
                             mMissingParentNodes.clear();

                             json->enterarray();
                             return json->leavearray();
                         });
}

ActionpacketNewNodes::~ActionpacketNewNodes()
{
    LOG_debug << "Destruction of ActionpacketNewNodes";
    assert(!mNodeTreeIsChanging.owns_lock());
}

size_t ActionpacketNewNodes::processChunk(const char* chunk)
{
    size_t consumed = 0;

    consumed += mJsonSplitter.processChunk(&mFilters, chunk);
    if (mJsonSplitter.hasFailed())
    {
        // stop the processing
        assert(false);
        return 0;
    }

    if (mJsonSplitter.hasFinished())
    {
        consumed++;
    }

    return consumed;
}



}
