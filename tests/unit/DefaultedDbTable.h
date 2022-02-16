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
        : DbTable(gen, false)
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
        //throw NotImplemented(__func__);
    }
    bool getChildren(mega::NodeHandle, std::map<mega::NodeHandle, mega::NodeSerialized>&) override
    {
        return false;
        //throw NotImplemented(__func__);
    }
    bool getChildrenHandles(mega::NodeHandle, std::set<mega::NodeHandle>&) override
    {
        return false;
        //throw NotImplemented(__func__);
    }
    bool getNodesByName(const std::string&, std::map<mega::NodeHandle, mega::NodeSerialized>&) override
    {
        return false;
        //throw NotImplemented(__func__);
    }
    bool getRecentNodes(unsigned maxcount, mega::m_time_t since, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
    {
        return false;
    }
    bool getFavouritesHandles(mega::NodeHandle, uint32_t, std::vector<mega::NodeHandle>&) override
    {
        return false;
    }
    m_off_t getNodeSize(mega::NodeHandle) override
    {
        return 0;
        //throw NotImplemented(__func__);
    }
    int getNumberOfChildren(mega::NodeHandle) override
    {
        return 0;
        //throw NotImplemented(__func__);
    }
    bool isNodesOnDemandDb() override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    mega::NodeHandle getFirstAncestor(mega::NodeHandle h) override
    {
        return h;
        //throw NotImplemented{__func__};
    }
    bool isNodeInDB(mega::NodeHandle) override
    {
        return false;
        //throw NotImplemented{__func__};
    }
    bool isAncestor(mega::NodeHandle, mega::NodeHandle) override
    {
        return false;
    }
    mega::nodetype_t getNodeType(mega::NodeHandle) override
    {
        return mega::TYPE_UNKNOWN;
    }
    uint64_t getNumberOfNodes() override
    {
        return false;
    }
    void cancelQuery() override
    {

    }
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
        //throw NotImplemented{__func__};
    }
    void remove() override
    {
        //throw NotImplemented{__func__};
    }
    bool inTransaction() const override
    {
        return false;
    }
    bool getFingerPrints(std::map<mega::FileFingerprint, std::map<mega::NodeHandle, mega::Node*>>& fingerprints) override
    {
        return false;
    }
};

} // mt
