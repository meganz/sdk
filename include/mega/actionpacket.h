/**
 * @file mega/actionpacket.h
 * @brief Request actionpacket component
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

#ifndef MEGA_ACTIONPACKET_H
#define MEGA_ACTIONPACKET_H 1

#include "account.h"
#include "http.h"
#include "json.h"
#include "network_connectivity_test_helpers.h"
#include "node.h"
#include "nodemanager.h"
#include "setandelement.h"
#include "textchat.h"
#include "types.h"

#include <memory>
#include <optional>
#include <variant>

namespace mega {

struct JSON;
struct MegaApp;

class MEGA_API Actionpacket
{
public:
    MegaClient* client; // non-owning
    // filters for JSON parsing in streaming
    std::map<std::string, std::function<bool(JSON *)>> mFilters;
    virtual size_t processChunk(const char* chunk) = 0;

    Actionpacket()
    {
        client = nullptr;
    };
    virtual ~Actionpacket() {};
    bool finishedChunk()
    {
        return mJsonSplitter.hasFinished();
    }
protected:
    JSONSplitter mJsonSplitter;
    handle mPreviousHandleForAlert = UNDEF;
    NodeManager::MissingParentNodes mMissingParentNodes;
    std::unique_lock<recursive_mutex> mNodeTreeIsChanging;
};


class MEGA_API ActionpacketNewNodes : public Actionpacket
{
public:
    ActionpacketNewNodes(MegaClient*);
    ~ActionpacketNewNodes() override;

    size_t processChunk(const char*);
protected:
    bool mFirstChunkProcessed = false;
};

}
#endif