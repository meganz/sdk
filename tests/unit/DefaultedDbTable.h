/**
 * (c) 2020 by Mega Limited, Wellsford, New Zealand
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

#pragma once

#include <mega/db.h>

#include "NotImplemented.h"

namespace mt {

class DefaultedDbTable: public mega::DbTable, public mega::DBTableNodes
{
public:
    using mega::DbTable::DbTable;
    DefaultedDbTable(mega::PrnGen& gen)
        : DbTable(gen, false, nullptr)
    {
    }
    void rewind() override
    {
        //throw NotImplemented{__func__};
    }
    bool next(uint32_t*, std::string*) override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    bool get(uint32_t, std::string*) override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    bool getNode(mega::NodeHandle, mega::NodeSerialized&) override
    {
        return false;
        //throw NotImplemented{__func__};
    }

    bool getNodesByFingerprint(
        const std::string& /*fingerprint*/,
        std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
    {
        return false;
    }
    bool getNodeByFingerprint(const std::string&, mega::NodeSerialized&, mega::NodeHandle&) override
    {
        return false;
    }
    bool getNodesByOrigFingerprint(const std::string&, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
    {
        return false;
    }
    bool getRootNodes(std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
    {
        return false;
        //throw NotImplemented(__func__);
    }

    bool getNodesWithSharesOrLink(std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&, mega::ShareType_t) override
    {
        return false;
    }

    uint64_t getNumberOfChildren(mega::NodeHandle /*parentHandle*/) override
    {
        return 0;
    }
    bool getChildren(const mega::NodeSearchFilter&, int, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&, mega::CancelToken, const mega::NodeSearchPage&) override
    {
        return false;
        //throw NotImplemented(__func__);
    }
    bool searchNodes(const mega::NodeSearchFilter&, int, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&, mega::CancelToken, const mega::NodeSearchPage&) override
    {
        return false;
        //throw NotImplemented(__func__);
    }

    auto getNodeTagsBelow(mega::CancelToken, mega::NodeHandle, const std::string&)
        -> std::optional<std::set<std::string>> override
    {
        return std::nullopt;
    }

    bool getRecentNodes(const mega::NodeSearchPage&,
                        mega::m_time_t /*since*/,
                        std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
    {
        return false;
    }
    bool getFavouritesHandles(mega::NodeHandle, uint32_t, std::vector<mega::NodeHandle>&) override
    {
        return false;
    }

    bool childNodeByNameType(mega::NodeHandle,
                             const std::string& /*name*/,
                             mega::nodetype_t,
                             std::pair<mega::NodeHandle, mega::NodeSerialized>&) override
    {
        return false;
    }

    bool getNodeSizeTypeAndFlags(mega::NodeHandle,
                                 m_off_t& /*size*/,
                                 mega::nodetype_t&,
                                 uint64_t& /*oldFlags*/) override
    {
        return false;
    }
    bool isAncestor(mega::NodeHandle, mega::NodeHandle, mega::CancelToken) override
    {
        return false;
    }
    uint64_t getNumberOfNodes() override
    {
        return false;
    }

    uint64_t getNumberOfChildrenByType(mega::NodeHandle /*parentHandle*/, mega::nodetype_t) override
    {
      return 0;
    }

    void updateCounter(mega::NodeHandle, const std::string&) override {}

    void updateCounterAndFlags(mega::NodeHandle,
                               uint64_t /*flags*/,
                               const std::string& /*nodeCounterBlob*/) override
    {}

    void createIndexes(bool /*enableIndexesForSearching*/) override {}

    void dropSearchDBIndexes() override {}

    bool put(uint32_t, char*, unsigned) override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    bool put(mega::Node *) override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    bool del(uint32_t) override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    bool remove(mega::NodeHandle) override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    bool removeNodes() override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    void truncate() override
    {
        //throw NotImplemented{__func__};
    }
    void begin() override
    {
        //throw NotImplemented{__func__};
    }
    void commit() override
    {
        //throw NotImplemented{__func__};
    }
    void abort() override
    {
        // throw NotImplemented{__func__};
    }
    void remove() override
    {
        //throw NotImplemented{__func__};
    }
};

} // mt
