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
    bool getNodesByFingerprint(const std::string& fingerprint, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
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

    /** @deprecated */
    bool getNodesWithSharesOrLink(std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&, mega::ShareType_t) override
    {
        return false;
        //throw NotImplemented(__func__);
    }

    /** @deprecated */
    bool getChildren(mega::NodeHandle parentHandle, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& children, mega::CancelToken cancelFlag) override
    {
        return false;
    }

    bool getChildrenFromType(mega::NodeHandle parentHandle, mega::nodetype_t nodeType, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& children, mega::CancelToken cancelFlag) override
    {
        return false;
    }
    uint64_t getNumberOfChildren(mega::NodeHandle parentHandle) override
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

    bool getAllNodeTags(const std::string&, std::set<std::string>&, mega::CancelToken) override
    {
        return false;
        // throw NotImplemented(__func__);
    }

    /** @deprecated */
    bool searchForNodesByName(const std::string&, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&, mega::CancelToken cancelFlag) override
    {
        return false;
        //throw NotImplemented(__func__);
    }

    /** @deprecated */
    bool searchForNodesByNameNoRecursive(const std::string& name, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& nodes, mega::NodeHandle parentHandle, mega::CancelToken cancelFlag)
    {
        return false;
    }

    /** @deprecated */
    bool searchInShareOrOutShareByName(const std::string& name, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& nodes, mega::ShareType_t shareType, mega::CancelToken cancelFlag) override
    {
        return false;
    }
    bool getRecentNodes(unsigned maxcount, mega::m_time_t since, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>&) override
    {
        return false;
    }
    bool getFavouritesHandles(mega::NodeHandle, uint32_t, std::vector<mega::NodeHandle>&) override
    {
        return false;
    }
    bool childNodeByNameType(mega::NodeHandle, const std::string& name, mega::nodetype_t, std::pair<mega::NodeHandle, mega::NodeSerialized>&) override
    {
        return false;
    }
    bool getNodeSizeTypeAndFlags(mega::NodeHandle node, m_off_t& size, mega::nodetype_t& nodeType, uint64_t& oldFlags) override
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
    uint64_t getNumberOfChildrenByType(mega::NodeHandle parentHandle, mega::nodetype_t nodeType) override
    {
      return 0;
    }

    /** @deprecated */
    bool getNodesByMimetype(mega::MimeType_t mimeType, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized> >& nodes, mega::Node::Flags requiredFlags, mega::Node::Flags excludeFlags, mega::CancelToken cancelFlag) override
    {
        return false;
    }

    /** @deprecated */
    bool getNodesByMimetypeExclusiveRecursive(mega::MimeType_t mimeType, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& nodes, mega::Node::Flags requiredFlags, mega::Node::Flags excludeFlags, mega::Node::Flags excludeRecursiveFlags, mega::NodeHandle anscestorHandle, mega::CancelToken cancelFlag) override
    {
        return false;
    }

    void updateCounter(mega::NodeHandle, const std::string&) override
    {

    }
    void updateCounterAndFlags(mega::NodeHandle nodeHandle, uint64_t flags, const std::string& nodeCounterBlob) override
    {

    }
    void createIndexes() override
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
};

} // mt
