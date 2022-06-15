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
    bool getNodesByName(const std::string&, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
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
    bool getNodeByNameAtFirstLevel(mega::NodeHandle, const std::string& name, mega::nodetype_t, std::pair<mega::NodeHandle, mega::NodeSerialized>&) override
    {
        return false;
    }
    bool getNodeSizeAndType(mega::NodeHandle node, m_off_t& size, mega::nodetype_t& nodeType) override
    {
        return false;
    }
    bool isNodesOnDemandDb() override
    {
        return false;
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
    uint64_t getNumberOfNodes() override
    {
        return false;
    }
    void cancelQuery() override
    {

    }
    void updateCounter(mega::NodeHandle, const std::string&) override
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
    bool loadFingerprintsAndChildren(std::map<mega::FileFingerprint, std::map<mega::NodeHandle, mega::Node*>, mega::FileFingerprintCmp>& , std::map<mega::NodeHandle, std::set<mega::NodeHandle>>&) override
    {
        return false;
    }
};

} // mt
