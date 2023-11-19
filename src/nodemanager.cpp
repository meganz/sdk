/**
 * @file nodemanager.cpp
 * @brief Client access engine core logic
 *
 * (c) 2013-2023 by Mega Limited, Auckland, New Zealand
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

#include "mega/nodemanager.h"
#include "mega/megaclient.h"
#include "mega/base64.h"
#include "mega/megaapp.h"
#include "mega/share.h"


namespace mega {


NodeManager::NodeManager(MegaClient& client)
    : mClient(client)
{
}

void NodeManager::setTable(DBTableNodes *table)
{
    LockGuard g(mMutex);
    setTable_internal(table);
}

void NodeManager::setTable_internal(DBTableNodes *table)
{
    assert(mMutex.owns_lock());
    mTable = table;
}

void NodeManager::reset()
{
    LockGuard g(mMutex);
    reset_internal();
}

void NodeManager::reset_internal()
{
    assert(mMutex.owns_lock());
    setTable_internal(nullptr);
    cleanNodes_internal();
}

bool NodeManager::setrootnode(Node* node)
{
    LockGuard g(mMutex);
    return setrootnode_internal(node);
}

bool NodeManager::setrootnode_internal(Node* node)
{
    assert(mMutex.owns_lock());
    switch (node->type)
    {
        case ROOTNODE:
            rootnodes.files = node->nodeHandle();
            return true;

        case VAULTNODE:
            rootnodes.vault = node->nodeHandle();
            return true;

        case RUBBISHNODE:
            rootnodes.rubbish = node->nodeHandle();
            return true;

        default:
            assert(false);
            return false;
    }
}

void NodeManager::notifyNode(Node* n, node_vector* nodesToReport)
{
    LockGuard g(mMutex);
    notifyNode_internal(n, nodesToReport);
}

void NodeManager::notifyNode_internal(Node* n, node_vector* nodesToReport)
{
    assert(mMutex.owns_lock());
    n->applykey();

    if (!mClient.fetchingnodes)
    {
        if (n->changed.modifiedByThisClient && !n->changed.removed && n->attrstring)
        {
            // report a "NO_KEY" event

            char* buf = new char[n->nodekey().size() * 4 / 3 + 4];
            Base64::btoa((byte *)n->nodekey().data(), int(n->nodekey().size()), buf);

            int changed = 0;
            changed |= (int)n->changed.removed;
            changed |= n->changed.attrs << 1;
            changed |= n->changed.owner << 2;
            changed |= n->changed.ctime << 3;
            changed |= n->changed.fileattrstring << 4;
            changed |= n->changed.inshare << 5;
            changed |= n->changed.outshares << 6;
            changed |= n->changed.pendingshares << 7;
            changed |= n->changed.parent << 8;
            changed |= n->changed.publiclink << 9;
            changed |= n->changed.newnode << 10;
            changed |= n->changed.name << 11;
            changed |= n->changed.favourite << 12;
            changed |= n->changed.sensitive << 13;
            changed |= n->changed.pwdValue << 14;

            int attrlen = int(n->attrstring->size());
            string base64attrstring;
            base64attrstring.resize(attrlen * 4 / 3 + 4);
            base64attrstring.resize(Base64::btoa((byte *)n->attrstring->data(), int(n->attrstring->size()), (char *)base64attrstring.data()));

            char report[512];
            Base64::btoa((const byte *)&n->nodehandle, MegaClient::NODEHANDLE, report);
            snprintf(report + 8, sizeof(report)-8, " %d %" PRIu64 " %d %X %.200s %.200s", n->type, n->size, attrlen, changed, buf, base64attrstring.c_str());

            mClient.reportevent("NK", report, 0);
            mClient.sendevent(99400, report, 0);

            delete [] buf;
        }

#ifdef ENABLE_SYNC
        // is this a synced node that was moved to a non-synced location? queue for
        // deletion from LocalNodes.
        if (n->localnode && n->localnode->parent && n->parent && !n->parent->localnode)
        {
            if (n->changed.removed || n->changed.parent)
            {
                if (n->type == FOLDERNODE)
                {
                    LOG_debug << "Sync - remote folder deletion detected " << n->displayname();
                }
                else
                {
                    LOG_debug << "Sync - remote file deletion detected " << n->displayname() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
                }
            }

            n->localnode->deleted = true;
            n->localnode.reset();
        }
        else
        {
            // is this a synced node that is not a sync root, or a new node in a
            // synced folder?
            // FIXME: aggregate subtrees!
            if (n->localnode && n->localnode->parent)
            {
                n->localnode->deleted = n->changed.removed;
            }

            if (n->parent && n->parent->localnode && (!n->localnode || (n->localnode->parent != n->parent->localnode)))
            {
                if (n->localnode)
                {
                    n->localnode->deleted = n->changed.removed;
                }

                if (!n->changed.removed && (n->changed.newnode || n->changed.parent))
                {
                    if (!n->localnode)
                    {
                        if (n->type == FOLDERNODE)
                        {
                            LOG_debug << "Sync - remote folder addition detected " << n->displayname();
                        }
                        else
                        {
                            LOG_debug << "Sync - remote file addition detected " << n->displayname() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
                        }
                    }
                    else
                    {
                        Node* prevparent = n->localnode->parent ? n->localnode->parent->node.get() : nullptr;
                        LOG_debug << "Sync - remote move " << n->displayname() <<
                            " from " << (prevparent ? prevparent->displayname() : "?") <<
                            " to " << (n->parent ? n->parent->displayname() : "?");
                    }
                }
            }
            else if (!n->changed.removed && n->changed.name && n->localnode && n->localnode->name.compare(n->displayname()))
            {
                LOG_debug << "Sync - remote rename from " << n->localnode->name << " to " << n->displayname();
            }
        }
#endif
    }

    if (!n->notified)
    {
        n->notified = true;
        if (nodesToReport)
        {
            nodesToReport->push_back(n);
        }
        else
        {
            mNodeNotify.push_back(n);
        }
    }
}

bool NodeManager::addNode(Node *node, bool notify, bool isFetching, MissingParentNodes& missingParentNodes)
{
    LockGuard g(mMutex);
    return addNode_internal(node, notify, isFetching, missingParentNodes);
}

bool NodeManager::addNode_internal(Node *node, bool notify, bool isFetching, MissingParentNodes& missingParentNodes)
{
    assert(mMutex.owns_lock());
    // ownership of 'node' is taken by NodeManager::mNodes if node is kept in memory,
    // and by NodeManager::mNodeToWriteInDB if node is only written to DB. In the latter,
    // the 'node' is deleted upon saveNodeInDb()

    // 'isFetching' is true only when CommandFetchNodes is in flight and/or it has been received,
    // but it's been complemented with actionpackets. It's false when loaded from DB.

    // 'notify' is false when loading nodes from API or DB. True when node is received from
    // actionpackets and/or from response of CommandPutnodes

    bool rootNode = node->type == ROOTNODE || node->type == RUBBISHNODE || node->type == VAULTNODE;
    if (rootNode)
    {
        setrootnode_internal(node);
    }

    // getRootNodeFiles() is always set for folder links before adding any node (upon login)
    bool isFolderLink = rootnodes.files == node->nodeHandle();

    bool keepNodeInMemory = rootNode
            || isFolderLink
            || !isFetching
            || notify
            || node->parentHandle() == rootnodes.files; // first level of children for CloudDrive
    // Note: incoming shares are not kept in ram during fetchnodes from API. Instead, they are loaded
    // upon mergenewshares(), when fetchnodes is completed

    if (keepNodeInMemory)
    {
        saveNodeInRAM(node, rootNode || isFolderLink, missingParentNodes);   // takes ownership
    }
    else
    {
        // still keep it in memory temporary, until saveNodeInDb()
        assert(!mNodeToWriteInDb);
        mNodeToWriteInDb.reset(node);

        // when keepNodeInMemory is true, NodeManager::addChild is called by Node::setParent (from NodeManager::saveNodeInRAM)
        auto pair = mNodes.emplace(node->nodeHandle(), NodeManagerNode());
        // The NodeManagerNode could have been added by NodeManager::addChild() but, in that case, mNode would be invalid
        auto& nodePosition = pair.first;
        assert(!nodePosition->second.mNode);
        nodePosition->second.mAllChildrenHandleLoaded = true; // Receive a new node, children aren't received yet or they are stored in nodesWithMissingParents
        addChild_internal(node->parentHandle(), node->nodeHandle(), nullptr);
    }

    return true;
}

bool NodeManager::updateNode(Node *node)
{
    LockGuard g(mMutex);
    return updateNode_internal(node);
}

bool NodeManager::updateNode_internal(Node *node)
{
    assert(mMutex.owns_lock());

    if (!mTable)
    {
        assert(false);
        return false;
    }

    putNodeInDb(node);

    return true;
}

Node* NodeManager::getNodeByHandle(NodeHandle handle)
{
    LockGuard g(mMutex);
    return getNodeByHandle_internal(handle);
}

Node* NodeManager::getNodeByHandle_internal(NodeHandle handle)
{
    assert(mMutex.owns_lock());
    if (handle.isUndef()) return nullptr;

    if (mNodes.empty())
    {
        return nullptr;
    }

    Node* node = getNodeInRAM(handle);
    if (!node)
    {
        node = getNodeFromDataBase(handle);
    }

    return node;
}

node_list NodeManager::getChildren(const Node *parent, CancelToken cancelToken)
{
    LockGuard g(mMutex);
    return getChildren_internal(parent, cancelToken);
}

node_list NodeManager::getChildren_internal(const Node *parent, CancelToken cancelToken)
{
    assert(mMutex.owns_lock());

    node_list childrenList;
    if (!parent || !mTable || mNodes.empty())
    {
        return childrenList;
    }

    // if handles of all children are known, load missing child nodes one by one
    if (parent->mNodePosition->second.mAllChildrenHandleLoaded)
    {
        if (!parent->mNodePosition->second.mChildren)
        {
            return childrenList;
        }

        for (const auto &child : *parent->mNodePosition->second.mChildren)
        {
            if (cancelToken.isCancelled())
            {
                childrenList.clear();
                return childrenList;
            }

            if (child.second)
            {
                childrenList.push_back(child.second);
            }
            else
            {
                Node* n = getNodeFromDataBase(child.first);
                assert(n);
                if (n)
                {
                    childrenList.push_back(n);
                }
            }
        }
    }
    else // get all children from DB directly and load missing ones
    {
        if (parent->mNodePosition->second.mChildren)
        {
            for (const auto& child : *parent->mNodePosition->second.mChildren)
            {
                if (child.second)
                {
                    childrenList.push_back(child.second);
                }
            }
        }

        std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
        mTable->getChildren(parent->nodeHandle(), nodesFromTable, cancelToken);
        if (cancelToken.isCancelled())
        {
            childrenList.clear();
            return  childrenList;
        }

        if (!nodesFromTable.empty() && !parent->mNodePosition->second.mChildren)
        {
            parent->mNodePosition->second.mChildren = ::mega::make_unique<std::map<NodeHandle, Node*>>();
        }

        for (const auto& nodeSerializedIt : nodesFromTable)
        {
            if (cancelToken.isCancelled())
            {
                childrenList.clear();
                return  childrenList;
            }

            auto childIt = parent->mNodePosition->second.mChildren->find(nodeSerializedIt.first);
            if (childIt == parent->mNodePosition->second.mChildren->end() || !childIt->second) // handle or node not loaded
            {
                auto itNode = mNodes.find(nodeSerializedIt.first);
                if ( itNode == mNodes.end() || !itNode->second.mNode)    // not loaded
                {
                    Node* n = getNodeFromNodeSerialized(nodeSerializedIt.second);
                    if (!n)
                    {
                        childrenList.clear();
                        return childrenList;
                    }

                    childrenList.push_back(n);
                }
                else  // -> node loaded, but it isn't associated to the parent -> the node has been moved but DB isn't already updated
                {
                    assert(itNode->second.mNode->parentHandle() != parent->nodeHandle());
                }
            }
        }

        parent->mNodePosition->second.mAllChildrenHandleLoaded = true;
    }

    return childrenList;
}

node_vector NodeManager::getChildrenFromType(const NodeHandle& parent, nodetype_t type, CancelToken cancelToken)
{
    LockGuard g(mMutex);
    return getChildrenFromType_internal(parent, type, cancelToken);
}

node_vector NodeManager::getChildrenFromType_internal(const NodeHandle& parent, nodetype_t type, CancelToken cancelToken)
{
    assert(mMutex.owns_lock());

    if (!mTable || mNodes.empty())
    {
        return node_vector();
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    mTable->getChildrenFromType(parent, type, nodesFromTable, cancelToken);

    if (cancelToken.isCancelled())
    {
        return  node_vector();
    }

    return processUnserializedNodes(nodesFromTable, NodeHandle(), cancelToken);
}

node_vector NodeManager::getRecentNodes(unsigned maxcount, m_time_t since)
{
    LockGuard g(mMutex);
    return getRecentNodes_internal(maxcount, since);
}

node_vector NodeManager::getRecentNodes_internal(unsigned maxcount, m_time_t since)
{
    assert(mMutex.owns_lock());

    if (!mTable || mNodes.empty())
    {
        return node_vector();
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    mTable->getRecentNodes(maxcount, since, nodesFromTable);

    return processUnserializedNodes(nodesFromTable);
}

uint64_t NodeManager::getNodeCount()
{
    LockGuard g(mMutex);
    return getNodeCount_internal();
}

uint64_t NodeManager::getNodeCount_internal()
{
    assert(mMutex.owns_lock());

    if (mNodes.empty())
    {
        return 0;
    }

    uint64_t count = 0;
    node_vector roots = getRootNodesAndInshares();

    for (Node* node : roots)
    {
        NodeCounter nc = node->getCounter();
        count += nc.files + nc.folders + nc.versions;
    }

    // add roots to the count if logged into account (and fetchnodes is done <- roots are ready)
    if (!mClient.loggedIntoFolder() && roots.size())
    {
        // Root nodes aren't taken into consideration as part of node counters
        count += 3;
        assert(!rootnodes.files.isUndef() && !rootnodes.vault.isUndef() && !rootnodes.rubbish.isUndef());
    }

#ifndef NDEBUG
    if (mNodes.size())
    {
        uint64_t countDb = mTable ? mTable->getNumberOfNodes() : 0;
        if (!(mTable || count == countDb))
        {
            assert(!mTable || count == countDb);
        }
    }
#endif

    return count;
}

node_vector NodeManager::getChildren(const NodeSearchFilter& filter, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return getChildren_internal(filter, cancelFlag);
}

node_vector NodeManager::getChildren_internal(const NodeSearchFilter& filter, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    // validation
    if (filter.byLocationHandle() == UNDEF || !mTable || mNodes.empty())
    {
        assert(filter.byLocationHandle() != UNDEF && mTable && !mNodes.empty());
        return node_vector();
    }

    // small optimization to possibly skip the db look-up
    if (filter.bySensitivity())
    {
        Node* node = getNodeByHandle_internal(NodeHandle().set6byte(filter.byLocationHandle()));
        if (!node || node->isSensitiveInherited())
        {
            return node_vector();
        }
    }

    // db look-up
    vector<pair<NodeHandle, NodeSerialized>> nodesFromTable;
    if (!mTable->getChildren(filter, nodesFromTable, cancelFlag))
    {
        return node_vector();
    }

    node_vector nodes = processUnserializedChildren(nodesFromTable, filter, cancelFlag);

    return nodes;
}

node_vector NodeManager::searchNodes(const NodeSearchFilter& filter, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return searchNodes_internal(filter, cancelFlag);
}

node_vector NodeManager::searchNodes_internal(const NodeSearchFilter& filter, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    // validation
    if (!mTable || mNodes.empty())
    {
        assert(mTable && !mNodes.empty());
        return node_vector();
    }

    // small optimization to possibly skip the db look-up
    if (filter.bySensitivity() && filter.byLocationHandle() != UNDEF)
    {
        Node* node = getNodeByHandle_internal(NodeHandle().set6byte(filter.byLocationHandle()));
        if (!node || node->isSensitiveInherited())
        {
            return node_vector();
        }
    }

    // db look-up
    vector<pair<NodeHandle, NodeSerialized>> nodesFromTable;
    if (!mTable->searchNodes(filter, nodesFromTable, cancelFlag))
    {
        return node_vector();
    }

    node_vector nodes = processUnserializedNodes(nodesFromTable, filter, cancelFlag);

    return nodes;
}

/** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
node_vector NodeManager::search(NodeHandle ancestorHandle, const char* searchString, bool recursive, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return search_internal(ancestorHandle, searchString, recursive, requiredFlags, excludeFlags, excludeRecursiveFlags, cancelFlag);
}

/** @deprecated */
node_vector NodeManager::search_internal(NodeHandle ancestorHandle, const char* searchString, bool recursive, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    node_vector nodes;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nodes;
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    if (recursive)
    {
        mTable->searchForNodesByName(searchString, nodesFromTable, cancelFlag);
    }
    else
    {
        assert(!ancestorHandle.isUndef());
        mTable->searchForNodesByNameNoRecursive(searchString, nodesFromTable, ancestorHandle, cancelFlag);
    }

    nodes = processUnserializedNodes(nodesFromTable, ancestorHandle, cancelFlag);
    if (requiredFlags.any() || excludeFlags.any() || excludeRecursiveFlags.any())
    {
        node_vector isnodes;
        for (Node* node : nodes)
        {
            if (!node->areFlagsValid(requiredFlags, excludeFlags, excludeRecursiveFlags))
                continue;
            isnodes.push_back(node);
        }
        return isnodes;
    }

    return nodes;
}

/** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
node_vector NodeManager::getInSharesWithName(const char* searchString, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return getInSharesWithName_internal(searchString, cancelFlag);
}

/** @deprecated */
node_vector NodeManager::getInSharesWithName_internal(const char* searchString, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    node_vector nodes;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nodes;
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    mTable->searchInShareOrOutShareByName(searchString, nodesFromTable, ShareType_t::IN_SHARES, cancelFlag);
    nodes = processUnserializedNodes(nodesFromTable, NodeHandle(), cancelFlag);

    return nodes;
}

/** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
node_vector NodeManager::getOutSharesWithName(const char* searchString, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return getOutSharesWithName_internal(searchString, cancelFlag);
}

/** @deprecated */
node_vector NodeManager::getOutSharesWithName_internal(const char* searchString, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    node_vector nodes;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nodes;
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    mTable->searchInShareOrOutShareByName(searchString, nodesFromTable, ShareType_t::OUT_SHARES, cancelFlag);
    nodes = processUnserializedNodes(nodesFromTable, NodeHandle(), cancelFlag);

    return nodes;
}

/** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
node_vector NodeManager::getPublicLinksWithName(const char* searchString, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return getPublicLinksWithName_internal(searchString, cancelFlag);
}

/** @deprecated */
node_vector NodeManager::getPublicLinksWithName_internal(const char* searchString, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    node_vector nodes;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nodes;
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    mTable->searchInShareOrOutShareByName(searchString, nodesFromTable, ShareType_t::LINK, cancelFlag);
    nodes = processUnserializedNodes(nodesFromTable, NodeHandle(), cancelFlag);

    return nodes;
}

node_vector NodeManager::getNodesByFingerprint(FileFingerprint &fingerprint)
{
    LockGuard g(mMutex);
    return getNodesByFingerprint_internal(fingerprint);
}

node_vector NodeManager::getNodesByFingerprint_internal(FileFingerprint &fingerprint)
{
    assert(mMutex.owns_lock());

    node_vector nodes;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nodes;
    }

    // Take first nodes in RAM
    std::set<NodeHandle> fpLoaded;
    auto p = mFingerPrints.equal_range(&fingerprint);
    for (auto it = p.first; it != p.second; ++it)
    {
        Node* node = static_cast<Node*>(*it);
        fpLoaded.emplace(node->nodeHandle());
        nodes.push_back(node);
    }

    // If all fingerprints are loaded at DB, it isn't necessary search in DB
    if (mFingerPrints.allFingerprintsAreLoaded(&fingerprint))
    {
        return nodes;
    }

    // Look for nodes at DB
    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    std::string fingerprintString;
    fingerprint.FileFingerprint::serialize(&fingerprintString);
    mTable->getNodesByFingerprint(fingerprintString, nodesFromTable);
    if (nodesFromTable.size())
    {
        for (const auto& nodeIt : nodesFromTable)
        {
            // avoid to load already loaded nodes (found at mFingerPrints)
            if (fpLoaded.find(nodeIt.first) == fpLoaded.end())
            {
                Node* node = getNodeFromNodeSerialized(nodeIt.second);
                if (!node)
                {
                    nodes.clear();
                    return nodes;
                }

                nodes.push_back(node);
            }
        }
    }

    mFingerPrints.setAllFingerprintLoaded(&fingerprint);

    return nodes;
}

node_vector NodeManager::getNodesByOrigFingerprint(const std::string &fingerprint, Node *parent)
{
    LockGuard g(mMutex);
    return getNodesByOrigFingerprint_internal(fingerprint, parent);
}

node_vector NodeManager::getNodesByOrigFingerprint_internal(const std::string &fingerprint, Node *parent)
{
    assert(mMutex.owns_lock());

    node_vector nodes;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nodes;
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    mTable->getNodesByOrigFingerprint(fingerprint, nodesFromTable);

    nodes = processUnserializedNodes(nodesFromTable, parent ? parent->nodeHandle() : NodeHandle(), CancelToken());
    return nodes;
}

Node* NodeManager::getNodeByFingerprint(FileFingerprint &fingerprint)
{
    LockGuard g(mMutex);
    return getNodeByFingerprint_internal(fingerprint);
}

Node* NodeManager::getNodeByFingerprint_internal(FileFingerprint &fingerprint)
{
    assert(mMutex.owns_lock());

    Node* node = nullptr;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return node;
    }

    auto it = mFingerPrints.find(&fingerprint);
    if (it != mFingerPrints.end())
    {
        node = static_cast<Node*>(*it);
        assert(node);
        return node;
    }

    NodeSerialized nodeSerialized;
    std::string fingerprintString;
    fingerprint.FileFingerprint::serialize(&fingerprintString);
    mTable->getNodeByFingerprint(fingerprintString, nodeSerialized);
    if (nodeSerialized.mNode.size()) // nodes with that fingerprint found in DB
    {
        node = getNodeFromNodeSerialized(nodeSerialized);
    }

    return node;
}

Node* NodeManager::childNodeByNameType(const Node* parent, const std::string &name, nodetype_t nodeType)
{
    LockGuard g(mMutex);
    return childNodeByNameType_internal(parent, name, nodeType);
}

Node* NodeManager::childNodeByNameType_internal(const Node* parent, const std::string &name, nodetype_t nodeType)
{
    assert(mMutex.owns_lock());

    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nullptr;
    }

    // mAllChildrenHandleLoaded = false -> if not found, need check DB
    // mAllChildrenHandleLoaded = true  -> if all children have a pointer, no need to check DB
    bool allChildrenLoaded = parent->mNodePosition->second.mAllChildrenHandleLoaded;

    if (allChildrenLoaded && !parent->mNodePosition->second.mChildren)
    {
        return nullptr; // valid case
    }

    if (parent->mNodePosition->second.mChildren)
    {
        for (const auto& itNode : *parent->mNodePosition->second.mChildren)
        {
            Node* node = itNode.second;
            if (node && node->type == nodeType && name == node->displayname())
            {
                return node;
            }
            else if (!node)
            {
                // If not all child nodes have been loaded, check the DB
                allChildrenLoaded = false;
            }
        }
    }

    if (allChildrenLoaded)
    {
        return nullptr; // There is no match
    }

    std::pair<NodeHandle, NodeSerialized> nodeSerialized;
    if (!mTable->childNodeByNameType(parent->nodeHandle(), name, nodeType, nodeSerialized))
    {
        return nullptr;  // Not found at DB either
    }

    assert(!getNodeInRAM(nodeSerialized.first));  // not loaded yet
    return getNodeFromNodeSerialized(nodeSerialized.second);
}

node_vector NodeManager::getRootNodes()
{
    LockGuard g(mMutex);
    return getRootNodes_internal();
}

node_vector NodeManager::getRootNodes_internal()
{
    assert(mMutex.owns_lock());

    node_vector nodes;
    if (!mTable)
    {
        assert(false);
        return nodes;
    }

    if (mNodes.size()) // nodes already loaded from DB
    {
        Node* rootNode = getNodeByHandle_internal(rootnodes.files);
        assert(rootNode);
        nodes.push_back(rootNode);

        if (!mClient.loggedIntoFolder())
        {
            Node* inBox = getNodeByHandle_internal(rootnodes.vault);
            assert(inBox);
            nodes.push_back(inBox);

            Node* rubbish = getNodeByHandle_internal(rootnodes.rubbish);
            assert(rubbish);
            nodes.push_back(rubbish);
        }
    }
    else    // nodes not loaded yet
    {
        if (mClient.loggedIntoFolder())
        {
            NodeSerialized nodeSerialized;
            mTable->getNode(rootnodes.files, nodeSerialized);
            Node* n = getNodeFromNodeSerialized(nodeSerialized);
            if (!n)
            {
                return nodes;
            }

            nodes.push_back(n);
            //It isn't necessary call to setrootnode(n) because mClient.rootnodes.files is set correctly for folder link at login command
        }
        else
        {
            std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
            mTable->getRootNodes(nodesFromTable);

            for (const auto& nHandleSerialized : nodesFromTable)
            {
                assert(!getNodeInRAM(nHandleSerialized.first));
                Node* n = getNodeFromNodeSerialized(nHandleSerialized.second);
                if (!n)
                {
                    nodes.clear();
                    return nodes;
                }

                nodes.push_back(n);

                setrootnode_internal(n);
            }
        }
    }

    return nodes;
}

/** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
node_vector NodeManager::getNodesWithInShares()
{
    LockGuard g(mMutex);
    return getNodesWithInShares_internal();
}

node_vector NodeManager::getNodesWithInShares_internal()
{
    assert(mMutex.owns_lock());
    return getNodesWithSharesOrLink_internal(ShareType_t::IN_SHARES);
}

node_vector NodeManager::getNodesWithOutShares()
{
    LockGuard g(mMutex);
    return getNodesWithSharesOrLink_internal(ShareType_t::OUT_SHARES);
}

node_vector NodeManager::getNodesWithPendingOutShares()
{
    LockGuard g(mMutex);
    return getNodesWithSharesOrLink_internal(ShareType_t::PENDING_OUTSHARES);
}

node_vector NodeManager::getNodesWithLinks()
{
    LockGuard g(mMutex);
    return getNodesWithSharesOrLink_internal(ShareType_t::LINK);
}

node_vector NodeManager::getNodesByMimeType(MimeType_t mimeType, NodeHandle ancestorHandle, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return getNodesByMimeType_internal(mimeType, ancestorHandle, requiredFlags, excludeFlags, excludeRecursiveFlags, cancelFlag);
}

/** @deprecated */
node_vector NodeManager::getNodesByMimeType_internal(MimeType_t mimeType, NodeHandle ancestorHandle, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    if (!mTable || mNodes.empty())
    {
        assert(false);
        return node_vector();
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    if (excludeRecursiveFlags.none())
    {
        mTable->getNodesByMimetype(mimeType, nodesFromTable, requiredFlags, excludeFlags, cancelFlag);
    }
    else
    {
        mTable->getNodesByMimetypeExclusiveRecursive(mimeType, nodesFromTable, requiredFlags, excludeFlags, excludeRecursiveFlags, ancestorHandle, cancelFlag);
    }

    return processUnserializedNodes(nodesFromTable, ancestorHandle, cancelFlag);
}

/** @deprecated */
node_vector NodeManager::getNodesWithSharesOrLink_internal(ShareType_t shareType)
{
    assert(mMutex.owns_lock());

    if (!mTable || mNodes.empty())
    {
        //assert(false);
        return node_vector();
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodesFromTable;
    mTable->getNodesWithSharesOrLink(nodesFromTable, shareType);

    return processUnserializedNodes(nodesFromTable);
}

Node *NodeManager::getNodeFromNodeSerialized(const NodeSerialized &nodeSerialized)
{
    assert(mMutex.owns_lock());

    Node* node = unserializeNode(&nodeSerialized.mNode, false);
    if (!node)
    {
        assert(false);
        LOG_err << "Failed to unserialize node. Notifying the error to user";

        mClient.fatalError(ErrorReason::REASON_ERROR_UNSERIALIZE_NODE);

        return nullptr;
    }

    setNodeCounter(node, NodeCounter(nodeSerialized.mNodeCounter), false, nullptr);

    // do not automatically try to reload the account if we can't unserialize.
    // (1) we might go around in circles downloading the account over and over, DDOSing MEGA, because we get the same data back each time
    // (2) this function has no idea what is going on in the rest of the program.
    //     Reloading Nodes may be a terrible idea depending on what operations are in progress and calling this function.
    // (3) Reloading nodes will take a long time, and in the meantime we will be operating without this node anyway.  So, the damage is already done (eg, with syncs) and reloading is adding extra complications to diagnosis
    // (4) There should be an event issued here, so we can gather statistics on whether this happens or not, or how often
    // (5) Likely, reloading from here is completely untested.
    return node;
}

void NodeManager::setNodeCounter(Node* n, const NodeCounter &counter, bool notify, node_vector* nodesToReport)
{
    assert(mMutex.owns_lock());

    n->setCounter(counter);

    if (notify)
    {
        n->changed.counter = true;
        notifyNode_internal(n, nodesToReport);
    }
}

void NodeManager::updateTreeCounter(Node *origin, NodeCounter nc, OperationType operation, node_vector* nodesToReport)
{
    assert(mMutex.owns_lock());

    while (origin)
    {
        NodeCounter ancestorCounter = origin->getCounter();
        switch (operation)
        {
        case INCREASE:
            ancestorCounter += nc;
            break;

        case DECREASE:
            ancestorCounter -= nc;
            break;
        }

        setNodeCounter(origin, ancestorCounter, true, nodesToReport);
        origin = origin->parent;
    }
}

NodeCounter NodeManager::calculateNodeCounter(const NodeHandle& nodehandle, nodetype_t parentType, Node* node, bool isInRubbish)
{
    assert(mMutex.owns_lock());

    NodeCounter nc;
    if (!mTable)
    {
        assert(false);
        return nc;
    }

    m_off_t nodeSize = 0u;
    uint64_t flags = 0;
    nodetype_t nodeType = TYPE_UNKNOWN;
    if (node)
    {
        nodeType = node->type;
        nodeSize = node->size;
        flags = node->getDBFlags();
    }
    else
    {
        if (!mTable->getNodeSizeTypeAndFlags(nodehandle, nodeSize, nodeType, flags))
        {
            assert(false);
            return nc;
        }
        std::bitset<Node::FLAGS_SIZE> bitset(flags);
        flags = Node::getDBFlags(flags, isInRubbish, parentType == FILENODE, bitset.test(Node::FLAGS_IS_MARKED_SENSTIVE));
    }

    const nodePtr_map* children = nullptr;
    auto it = mNodes.find(nodehandle);
    if (it != mNodes.end())
    {
        children = it->second.mChildren.get();
    }

    if (children)
    {
        for (const auto& itNode : *children)
        {
            nc += calculateNodeCounter(itNode.first, nodeType, itNode.second, isInRubbish);
        }
    }

    if (nodeType == FILENODE)
    {
        bool isVersion = parentType == FILENODE;
        if (isVersion)
        {
            nc.versions++;
            nc.versionStorage += nodeSize;
        }
        else
        {
            nc.files++;
            nc.storage += nodeSize;
        }
    }
    else if (nodeType == FOLDERNODE)
    {
        nc.folders++;
    }

    if (node)
    {
        setNodeCounter(node, nc, false, nullptr);
    }

    mTable->updateCounterAndFlags(nodehandle, flags, nc.serialize());

    return nc;
}

std::vector<NodeHandle> NodeManager::getFavouritesNodeHandles(NodeHandle node, uint32_t count)
{
    LockGuard g(mMutex);
    return getFavouritesNodeHandles_internal(node, count);
}

std::vector<NodeHandle> NodeManager::getFavouritesNodeHandles_internal(NodeHandle node, uint32_t count)
{
    assert(mMutex.owns_lock());

    std::vector<NodeHandle> nodeHandles;
    if (!mTable || mNodes.empty())
    {
        assert(false);
        return nodeHandles;
    }

    mTable->getFavouritesHandles(node, count, nodeHandles);
    return nodeHandles;
}

size_t NodeManager::getNumberOfChildrenFromNode(NodeHandle parentHandle)
{
    LockGuard g(mMutex);
    return getNumberOfChildrenFromNode_internal(parentHandle);
}

size_t NodeManager::getNumberOfChildrenFromNode_internal(NodeHandle parentHandle)
{
    assert(mMutex.owns_lock());

    if (!mTable || mNodes.empty())
    {
        assert(false);
        return 0;
    }

    auto parentIt = mNodes.find(parentHandle);
    if (parentIt != mNodes.end() && parentIt->second.mAllChildrenHandleLoaded)
    {
        return parentIt->second.mChildren ? parentIt->second.mChildren->size() : 0;
    }

    return mTable->getNumberOfChildren(parentHandle);
}

size_t NodeManager::getNumberOfChildrenByType(NodeHandle parentHandle, nodetype_t nodeType)
{
    LockGuard g(mMutex);
    return getNumberOfChildrenByType_internal(parentHandle, nodeType);
}

size_t NodeManager::getNumberOfChildrenByType_internal(NodeHandle parentHandle, nodetype_t nodeType)
{
    assert(mMutex.owns_lock());

    if (!mTable || mNodes.empty())
    {
        assert(false);
        return 0;
    }

    assert(nodeType == FILENODE || nodeType == FOLDERNODE);

    return mTable->getNumberOfChildrenByType(parentHandle, nodeType);
}

bool NodeManager::isAncestor(NodeHandle nodehandle, NodeHandle ancestor, CancelToken cancelFlag)
{
    LockGuard g(mMutex);
    return isAncestor_internal(nodehandle, ancestor, cancelFlag);
}

bool NodeManager::isAncestor_internal(NodeHandle nodehandle, NodeHandle ancestor, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    if (!mTable)
    {
        assert(false);
        return false;
    }

    return mTable->isAncestor(nodehandle, ancestor, cancelFlag);
}

void NodeManager::removeChanges()
{
    LockGuard g(mMutex);
    removeChanges_internal();
}

void NodeManager::removeChanges_internal()
{
    assert(mMutex.owns_lock());

    for (auto& it : mNodes)
    {
        if (it.second.mNode)
        {
            memset(&(it.second.mNode->changed), 0, sizeof it.second.mNode->changed);
        }
    }
}

void NodeManager::cleanNodes()
{
    LockGuard g(mMutex);
    cleanNodes_internal();
}

void NodeManager::cleanNodes_internal()
{
    assert(mMutex.owns_lock());

    mFingerPrints.clear();
    mNodes.clear();
    mNodesInRam = 0;
    mNodeToWriteInDb.reset();
    mNodeNotify.clear();

    rootnodes.files.setUndef();
    rootnodes.rubbish.setUndef();
    rootnodes.vault.setUndef();

    if (mTable) mTable->removeNodes();

    mInitialized = false;
}

Node* NodeManager::getNodeFromBlob(const std::string* nodeSerialized)
{
    LockGuard g(mMutex);
    return getNodeFromBlob_internal(nodeSerialized);
}

Node* NodeManager::getNodeFromBlob_internal(const std::string* nodeSerialized)
{
    assert(mMutex.owns_lock());
    return unserializeNode(nodeSerialized, true);
}

// parse serialized node and return Node object - updates nodes hash and parent
// mismatch vector
Node *NodeManager::unserializeNode(const std::string *d, bool fromOldCache)
{
    assert(mMutex.owns_lock());

    std::list<std::unique_ptr<NewShare>> ownNewshares;

    if (Node* n = Node::unserialize(mClient, d, fromOldCache, ownNewshares))
    {

        auto pair = mNodes.emplace(n->nodeHandle(), NodeManagerNode());
        // The NodeManagerNode could have been added in the initial fetch nodes (without session)
        // Now, the node is loaded from DB, NodeManagerNode is updated with correct values
        mNodesInRam++;
        auto& nodePosition = pair.first;
        assert(!nodePosition->second.mNode);
        nodePosition->second.mNode.reset(n);
        n->mNodePosition = nodePosition;

        // setparent() skiping update of node counters, since they are already calculated in DB
        // In DB migration we have to calculate them as they aren't calculated previously
        n->setparent(getNodeByHandle_internal(n->parentHandle()), fromOldCache);

        // recreate node members related to shares (no need to write to DB,
        // since we just loaded the node from DB and has no changes)
        for (auto& share : ownNewshares)
        {
            mClient.mergenewshare(share.get(), false, true);
        }

        return n;
    }
    return nullptr;
}

void NodeManager::applyKeys(uint32_t appliedKeys)
{
    LockGuard g(mMutex);
    applyKeys_internal(appliedKeys);
}

void NodeManager::applyKeys_internal(uint32_t appliedKeys)
{
    assert(mMutex.owns_lock());

    if (mNodes.size() > appliedKeys)
    {
        for (auto& it : mNodes)
        {
            if (it.second.mNode)
            {
                it.second.mNode->applykey();
            }
        }
    }
}

void NodeManager::notifyPurge()
{
    // only lock to get the nodes to report
    node_vector nodesToReport;
    {
        LockGuard g(mMutex);
        nodesToReport.swap(mNodeNotify);
    }

    // we do our reporting outside the lock, as it involves callbacks to the client

    if (!nodesToReport.empty())
    {
        mClient.applykeys();

        if (!mClient.fetchingnodes)
        {
            assert(!mMutex.owns_lock());
            mClient.app->nodes_updated(&nodesToReport.data()[0], static_cast<int>(nodesToReport.size()));
        }

#ifdef ENABLE_SYNC

        // check for renamed/moved sync root folders
        mClient.syncs.forEachUnifiedSync([&](UnifiedSync& us){
            // Try and locate the sync's cloud root.
            Node* n = getNodeByHandle(us.mConfig.mRemoteNode);

            // Sync's root is no longer present in memory.
            if (!n)
                return;

            // Has this node received any commands from the backup center?
            auto commands = n->getSdsBackups();

            // Are any of the commands applicable to this sync?
            for (auto& command : commands)
            {
                // Command entry isn't applicable to us.
                if (command.first != us.mConfig.mBackupId)
                    continue;

                // Command entry isn't telling us to remove this sync.
                if (command.second != CommandBackupPut::DELETED)
                    continue;

                // For purposes of capture.
                auto id = us.mConfig.mBackupId;
                auto remoteNode = us.mConfig.mRemoteNode;

                auto completion = [id, remoteNode, this](Error result) {
                    // Had the sync already been removed?
                    if (result == API_ENOENT)
                    {
                        LOG_debug << "SDS: Sync "
                                  << toHandle(id)
                                  << " no longer present for the node "
                                  << remoteNode;
                        return;
                    }

                    // Was there any error removing the sync?
                    if (result != API_OK)
                    {
                        LOG_err << "SDS: Unable to remove sync "
                                << toHandle(id)
                                << " associated with the node "
                                << remoteNode
                                << " due to error "
                                << result;
                        return;
                    }

                    // Locate this sync's root node.
                    auto* node = getNodeByHandle(remoteNode);

                    // Is the node still present in memory?
                    if (!node)
                    {
                        LOG_warn << "SDS: Unable to update attribute as "
                                 << remoteNode
                                 << " is no longer present in memory.";
                        return;
                    }

                    // Is it worth updating the node's SDS attribute?
                    if (node->changed.removed)
                    {
                        LOG_debug << "SDS: Skipping attribute update as "
                                  << remoteNode
                                  << " has been removed.";
                        return;
                    }

                    auto commands = node->getSdsBackups();
                    auto updated = false;

                    // Update the attribute's value.
                    for (auto i = commands.size(); i--; )
                    {
                        auto& command = commands[i];

                        if (command.first != id
                            || command.second != CommandBackupPut::DELETED)
                            continue;

                        commands.erase(commands.begin() + i);
                        updated = true;
                    }

                    // Do we really need to update the attribute?
                    if (!updated)
                    {
                        LOG_warn << "SDS: Skipping no-op attribute update: "
                                 << remoteNode;
                        return;
                    }

                    auto completion = [](NodeHandle handle, Error result) {
                        // Were we unable to update the SDS attribute?
                        if (result != API_OK)
                        {
                            LOG_warn << "SDS: Unable to update attribute on "
                                     << handle
                                     << " due to error "
                                     << result;
                            return;
                        }

                        // Update was successful.
                        LOG_debug << "SDS: Attribute updated on "
                                  << handle;
                    };

                    // Update the attribute.
                    mClient.setattr(node,
                            attr_map(Node::sdsId(), Node::toSdsString(commands)),
                            std::move(completion),
                            true);
                };

                // Try and remove the sync.
                mClient.syncs.deregisterThenRemoveSync(us.mConfig.mBackupId,
                                                 std::move(completion), true);
            }

            //update sync root node location and trigger failing cases
            NodeHandle rubbishHandle = getRootNodeRubbish();

            // check if moved
            bool movedToRubbish = n->firstancestor()->nodehandle == rubbishHandle.as8byte();
            const string currentPath = n->displaypath(); // full remote path
            const string& originalPath = us.mConfig.mOriginalPathOfRemoteRootNode; // previous full remote path
            bool pathChanged = n->changed.parent || movedToRubbish ||
                               // the following were inspired by UnifiedSync::updateSyncRemoteLocation()
                               us.mConfig.mRemoteNode != n->nodehandle ||
                               originalPath != currentPath;

            if (n->changed.attrs || pathChanged || n->changed.removed)
            {
                bool removed = n->changed.removed;

                // update path in sync configuration
                us.updateSyncRemoteLocation(removed ? nullptr : n, false);

                auto &activeSync = us.mSync;
                if (!activeSync) // no active sync (already failed)
                {
                    return;
                }

                auto syncErr = NO_SYNC_ERROR;

                // fail sync if required
                if (movedToRubbish)
                {
                    syncErr = REMOTE_NODE_MOVED_TO_RUBBISH;
                }
                else if (removed)
                {
                    syncErr = REMOTE_NODE_NOT_FOUND;
                }
                else if (pathChanged) // moved
                {
                    syncErr = REMOTE_PATH_HAS_CHANGED;
                }

                if (syncErr != NO_SYNC_ERROR)
                {
                    mClient.syncs.disableSyncByBackupId(
                        activeSync->getConfig().mBackupId,
                        true, syncErr, false, nullptr);
                }
            }
        });
#endif
        LockGuard g(mMutex);
        TransferDbCommitter committer(mClient.tctable);

        unsigned removed = 0;
        unsigned added = 0;

        // check all notified nodes for removed status and purge
        for (size_t i = 0; i < nodesToReport.size(); i++)
        {
            Node* n = nodesToReport[i];

            if (n->attrstring)
            {
                // make this just a warning to avoid auto test failure
                // this can happen if another client adds a folder in our share and the key for us is not available yet
                LOG_warn << "NO_KEY node: " << n->type << " " << n->size << " " << toNodeHandle(n->nodehandle) << " " << n->nodekeyUnchecked().size();
#ifdef ENABLE_SYNC
                if (n->localnode)
                {
                    LOG_err << "LocalNode: " << n->localnode->name << " " << n->localnode->type << " " << n->localnode->size;
                }
#endif
            }

            if (n->changed.removed)
            {
                // remove inbound share
                if (n->inshare)
                {
                    n->inshare->user->sharing.erase(n->nodehandle);
                    mClient.notifyuser(n->inshare->user);
                }
            }
            else
            {
                n->notified = false;
                memset(&(n->changed), 0, sizeof(n->changed));
                n->changed.modifiedByThisClient = false;
            }

            if (!mTable)
            {
                assert(false);
                return;
            }

            if (n->changed.removed)
            {
                NodeHandle h = n->nodeHandle();

                // This will also require notifying/updating parents back to the root.  Report and
                // update them in this same operation, to ensure consistency in case of commit
                updateTreeCounter(n->parent, n->getCounter(), DECREASE, &nodesToReport);

                if (n->parent)
                {
                    // optimization: if the parent has already been deleted, the relationship
                    // of children with their parent has been removed by the parent already
                    // so we can avoid lookups for non existing parent handle.
                    removeChild(n->parent, h);
                }
                node_list children = getChildren(n);
                for (auto child : children)
                {
                    child->parent = nullptr;
                }

                removeFingerprint(n);

                // effectively delete node from RAM
                mNodesInRam--;
                mNodes.erase(n->mNodePosition);

                mTable->remove(h);

                removed += 1;
            }
            else
            {
                putNodeInDb(n);

                added += 1;
            }
        }

        if (removed)
        {
            LOG_verbose << mClient.clientname << "Removed " << removed << " nodes from database";
        }
        if (added)
        {
            LOG_verbose << mClient.clientname << "Added " << added << " nodes to database";
        }
    }
}

bool NodeManager::hasCacheLoaded()
{
    LockGuard g(mMutex);
    return mNodes.size();
}

bool NodeManager::loadNodes()
{
    LockGuard g(mMutex);
    return loadNodes_internal();
}

bool NodeManager::loadNodes_internal()
{
    assert(mMutex.owns_lock());

    if (!mTable)
    {
        assert(false);
        return false;
    }

    node_vector rootnodes = getRootNodes_internal();
    // We can't base in `user.sharing` because it's set yet. We have to get from DB
    node_vector inshares = getNodesWithInShares_internal();  // it includes nested inshares

    for (auto &node : rootnodes)
    {
        getChildren_internal(node);
    }

    mInitialized = true;
    return true;
}

Node* NodeManager::getNodeInRAM(NodeHandle handle)
{
    assert(mMutex.owns_lock());

    auto itNode = mNodes.find(handle);
    if (itNode != mNodes.end() && itNode->second.mNode)
    {
        return itNode->second.mNode.get();
    }

    return nullptr;
}

void NodeManager::saveNodeInRAM(Node *node, bool isRootnode, MissingParentNodes& missingParentNodes)
{
    assert(mMutex.owns_lock());

    auto pair = mNodes.emplace(node->nodeHandle(), NodeManagerNode());
    // The NodeManagerNode could have been added by NodeManager::addChild() but, in that case, mNode would be invalid
    mNodesInRam++;
    auto& nodePosition = pair.first;
    assert(!nodePosition->second.mNode);
    nodePosition->second.mNode.reset(node);
    nodePosition->second.mAllChildrenHandleLoaded = true; // Receive a new node, children aren't received yet or they are stored a missingParentNodes
    node->mNodePosition = nodePosition;

    // In case of rootnode, no need to add to missingParentNodes
    if (!isRootnode)
    {
        Node *parent = nullptr;
        if ((parent = getNodeByHandle_internal(node->parentHandle())))
        {
            node->setparent(parent);
        }
        else
        {
            missingParentNodes[node->parentHandle()].insert(node);
        }
    }

    auto it = missingParentNodes.find(node->nodeHandle());
    if (it != missingParentNodes.end())
    {
        for (Node* n : it->second)
        {
            n->setparent(node);
        }

        missingParentNodes.erase(it);
    }
}

bool NodeManager::isRootNode(NodeHandle h) const
{
    LockGuard g(mMutex);

    return rootnodes.isRootNode(h);
}

int NodeManager::getNumVersions(NodeHandle nodeHandle)
{
    LockGuard g(mMutex);

    Node *node = getNodeByHandle_internal(nodeHandle);
    if (!node || node->type != FILENODE)
    {
        return 0;
    }

    return static_cast<int>(node->getCounter().versions) + 1;
}

NodeHandle NodeManager::getRootNodeFiles() const
{
    LockGuard g(mMutex);
    return rootnodes.files;
}
NodeHandle NodeManager::getRootNodeVault() const
{
    LockGuard g(mMutex);
    return rootnodes.vault;
}
NodeHandle NodeManager::getRootNodeRubbish() const
{
    LockGuard g(mMutex);
    return rootnodes.rubbish;
}
void NodeManager::setRootNodeFiles(NodeHandle h)
{
    LockGuard g(mMutex);
    rootnodes.files = h;
}
void NodeManager::setRootNodeVault(NodeHandle h)
{
    LockGuard g(mMutex);
    rootnodes.vault = h;
}
void NodeManager::setRootNodeRubbish(NodeHandle h)
{
    LockGuard g(mMutex);
    rootnodes.rubbish = h;
}


void NodeManager::checkOrphanNodes(MissingParentNodes& nodesWithMissingParent)
{
    // we don't actually use any members here, so no need to lock.  (well, just mClient, not part of our data structure)
    assert(!mMutex.owns_lock());

    // detect if there's any orphan node and report to API
    for (const auto& it : nodesWithMissingParent)
    {
        for (const auto& orphan : it.second)
        {
            // For inshares, we get sent the inshare node including its parent handle
            // even though we will never actually get that parent node (unless the share is nested)
            // So, don't complain about those ones.  Just about really un-attached subtrees.
            if (!orphan->inshare)
            {
                // At this point, all nodes have been already parsed, so the parent should never arrive.
                // The orphan node won't be reachable anymore, and could have a whole tree inside.
                // This can happen if the local instance of the SDK deletes a folder, receives the response
                // from the server via the cs channel, and after that it receives action packets related to
                // things that happened inside the deleted folder.
                // This race condition should disappear when the local cache is exclusively driven via
                // action packets and Speculative Instant Completion (SIC) is gone.
                TreeProcDel td;
                mClient.proctree(orphan, &td);

                // TODO: Change this warning to an error when Speculative Instant Completion (SIC) is gone
               LOG_warn << "Detected orphan node: " << toNodeHandle(orphan->nodehandle)
                        << " Parent: " << toNodeHandle(orphan->parentHandle());

               mClient.sendevent(99455, "Orphan node(s) detected");

                // If we didn't get all the parents of all the (not inshare) nodes,
                // then the API is sending us inconsistent data,
                // or we have a bug processing it.  Please investigate
                assert(false);
            }
        }
    }
}

void NodeManager::initCompleted()
{
    LockGuard g(mMutex);
    initCompleted_internal();
}

void NodeManager::initCompleted_internal()
{
    assert(mMutex.owns_lock());

    if (!mTable)
    {
        assert(false);
        return;
    }

    node_vector rootNodes = getRootNodesAndInshares();
    for (Node* node : rootNodes)
    {
        calculateNodeCounter(node->nodeHandle(), TYPE_UNKNOWN, node, node->type == RUBBISHNODE);
    }

    mTable->createIndexes();
    mInitialized = true;
}

bool NodeManager::ready()
{
    return mInitialized;
}

NodeCounter NodeManager::getCounterOfRootNodes()
{
    LockGuard g(mMutex);
    return getCounterOfRootNodes_internal();
}

NodeCounter NodeManager::getCounterOfRootNodes_internal()
{
    assert(mMutex.owns_lock());

    NodeCounter c;

    // if not logged in yet, node counters are not available
    if (mNodes.empty())
    {
        assert((rootnodes.files.isUndef()
                && rootnodes.vault.isUndef()
                && rootnodes.rubbish.isUndef())
               || (mClient.loggedIntoFolder()));

        return c;
    }

    node_vector rootNodes = getRootNodes_internal();
    for (Node* node : rootNodes)
    {
        c += node->getCounter();
    }

    return c;
}

void NodeManager::updateCounter(Node& n, Node* oldParent)
{
    LockGuard g(mMutex);
    updateCounter_internal(n, oldParent);
}

void NodeManager::updateCounter_internal(Node& n, Node* oldParent)
{
    assert(mMutex.owns_lock());

    NodeCounter nc = n.getCounter();
    updateTreeCounter(oldParent, nc, DECREASE, nullptr);

    // if node is a new version
    if (n.parent && n.parent->type == FILENODE)
    {
        if (nc.files > 0)
        {
            assert(nc.files == 1);
            // discount the old version, previously counted as file
            nc.files--;
            nc.storage -= n.size;
            nc.versions++;
            nc.versionStorage += n.size;
            setNodeCounter(&n, nc, true, nullptr);
        }
    }
    // newest element at chain versions has been removed, the second one element is the newest now. Update node counter properly
    else if (oldParent && oldParent->type == FILENODE && n.parent && n.parent->type != FILENODE)
    {
        nc.files++;
        nc.storage += n.size;
        nc.versions--;
        nc.versionStorage -= n.size;
        setNodeCounter(&n, nc, true, nullptr);
    }

    updateTreeCounter(n.parent, nc, INCREASE, nullptr);
}

FingerprintPosition NodeManager::insertFingerprint(Node *node)
{
    LockGuard g(mMutex);
    return insertFingerprint_internal(node);
}

FingerprintPosition NodeManager::insertFingerprint_internal(Node *node)
{
    assert(mMutex.owns_lock());

    // if node is not to be kept in memory, don't save the pointer in the set
    // since it will be invalid once node is written to DB
    if (node->type == FILENODE && mNodeToWriteInDb.get() != node)
    {
        return mFingerPrints.insert(node);

    }

    return mFingerPrints.end();
}

void NodeManager::removeFingerprint(Node *node)
{
    LockGuard g(mMutex);
    removeFingerprint_internal(node);
}

void NodeManager::removeFingerprint_internal(Node *node)
{
    assert(mMutex.owns_lock());

    if (node->type == FILENODE && node->mFingerPrintPosition != mFingerPrints.end())  // remove from mFingerPrints
    {

        mFingerPrints.erase(node->mFingerPrintPosition);
        node->mFingerPrintPosition = mFingerPrints.end();
    }
}

FingerprintPosition NodeManager::invalidFingerprintPos()
{
    // no locking for this one, it returns a constant
    return mFingerPrints.end();
}

void NodeManager::dumpNodes()
{
    LockGuard g(mMutex);
    dumpNodes_internal();
}

void NodeManager::dumpNodes_internal()
{
    assert(mMutex.owns_lock());

    if (!mTable)
    {
        assert(false);
        return;
    }

    for (auto &it : mNodes)
    {
        if (it.second.mNode)
        {
            putNodeInDb(it.second.mNode.get());
        }
    }

    mTable->createIndexes();
    mInitialized = true;
}

void NodeManager::saveNodeInDb(Node *node)
{
    LockGuard g(mMutex);
    saveNodeInDb_internal(node);
}

void NodeManager::saveNodeInDb_internal(Node *node)
{
    assert(mMutex.owns_lock());

    if (!mTable)
    {
        assert(false);
        return;
    }

    putNodeInDb(node);

    if (mNodeToWriteInDb)   // not to be kept in memory
    {
        assert(mNodeToWriteInDb.get() == node);
        mNodeToWriteInDb.reset();
    }
}

uint64_t NodeManager::getNumberNodesInRam() const
{
    LockGuard g(mMutex);
    return mNodesInRam;
}

void NodeManager::addChild(NodeHandle parent, NodeHandle child, Node* node)
{
    LockGuard g(mMutex);
    addChild_internal(parent, child, node);
}

void NodeManager::addChild_internal(NodeHandle parent, NodeHandle child, Node* node)
{
    assert(mMutex.owns_lock());

    auto pair = mNodes.emplace(parent, NodeManagerNode());
    // The NodeManagerNode could have been added in add node, only update the child
    assert(!pair.first->second.mChildren || !(*pair.first->second.mChildren)[child]);
    if (!pair.first->second.mChildren)
    {
        pair.first->second.mChildren = ::mega::make_unique<std::map<NodeHandle, Node*>>();
    }
    (*pair.first->second.mChildren)[child] = node;
}

void NodeManager::removeChild(Node* parent, NodeHandle child)
{
    LockGuard g(mMutex);
    removeChild_internal(parent, child);
}

void NodeManager::removeChild_internal(Node* parent, NodeHandle child)
{
    assert(mMutex.owns_lock());

    assert(parent->mNodePosition->second.mChildren);
    if (parent->mNodePosition->second.mChildren)
    {
        parent->mNodePosition->second.mChildren->erase(child);
    }
}

Node* NodeManager::getNodeFromDataBase(NodeHandle handle)
{
    assert(mMutex.owns_lock());

    if (!mTable)
    {
        assert(!mClient.loggedin());
        return nullptr;
    }

    Node* node = nullptr;
    NodeSerialized nodeSerialized;
    if (mTable->getNode(handle, nodeSerialized))
    {
        node = getNodeFromNodeSerialized(nodeSerialized);
    }

    return node;
}

node_vector NodeManager::getRootNodesAndInshares()
{
    assert(mMutex.owns_lock());
    node_vector rootnodes;

    rootnodes = getRootNodes_internal();
    if (!mClient.loggedIntoFolder()) // logged into user's account: incoming shared folders
    {
        node_vector inshares = mClient.getInShares();
        rootnodes.insert(rootnodes.end(), inshares.begin(), inshares.end());
    }

    return rootnodes;
}

node_vector NodeManager::processUnserializedNodes(const vector<pair<NodeHandle, NodeSerialized>>& nodesFromTable, const NodeSearchFilter& filter, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    node_vector nodes;

    for (const auto& nodeIt : nodesFromTable)
    {
        // Check pointer and value
        if (cancelFlag.isCancelled()) break;

        Node* n = getNodeInRAM(nodeIt.first);
        NodeHandle ancestorHandle = NodeHandle().set6byte(filter.byLocationHandle());

        if (!ancestorHandle.isUndef())  // filter results by subtree (nodeHandle)
        {
            bool skip = n ? !n->isAncestor(ancestorHandle)
                          : !isAncestor(nodeIt.first, ancestorHandle, cancelFlag);

            if (skip) continue;
        }

        if (!n)
        {
            n = getNodeFromNodeSerialized(nodeIt.second);
            if (!n)
            {
                nodes.clear();
                return nodes;
            }
        }

        // filter by sensitivity when it was inherited  --  should probably
        // be [part of] a function passed to the sql query
        if (filter.bySensitivity() && n->isSensitiveInherited()) continue;

        nodes.push_back(n);
    }

    return nodes;
}

node_vector NodeManager::processUnserializedChildren(const vector<pair<NodeHandle, NodeSerialized>>& childrenFromTable, const NodeSearchFilter& filter, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    node_vector nodes;

    for (const auto& child : childrenFromTable)
    {
        // Check pointer and value
        if (cancelFlag.isCancelled()) break;

        Node* n = getNodeInRAM(child.first);
        if (!n)
        {
            n = getNodeFromNodeSerialized(child.second);
            if (!n)
            {
                nodes.clear();
                return nodes;
            }
        }

        // filter by sensitivity when it was inherited  --  should probably
        // be [part of] a function passed to the sql query
        if (filter.bySensitivity() && n->isSensitiveInherited()) continue;

        nodes.push_back(n);
    }

    return nodes;
}

node_vector NodeManager::processUnserializedNodes(const std::vector<std::pair<NodeHandle, NodeSerialized> >& nodesFromTable, NodeHandle ancestorHandle, CancelToken cancelFlag)
{
    assert(mMutex.owns_lock());

    node_vector nodes;

    for (const auto& nodeIt : nodesFromTable)
    {
        // Check pointer and value
        if (cancelFlag.isCancelled()) break;

        Node* n = getNodeInRAM(nodeIt.first);

        if (!ancestorHandle.isUndef())  // filter results by subtree (nodeHandle)
        {
            bool skip = n ? !n->isAncestor(ancestorHandle)
                          : !isAncestor(nodeIt.first, ancestorHandle, cancelFlag);

            if (skip) continue;
        }

        if (!n)
        {
            n = getNodeFromNodeSerialized(nodeIt.second);
            if (!n)
            {
                nodes.clear();
                return nodes;
            }
        }

        nodes.push_back(n);
    }

    return nodes;
}

void NodeManager::putNodeInDb(Node* node) const
{
    if (!node)
    {
        return;
    }

    if (node->attrstring)
    {
        // Last attempt to decrypt the node before storing it.
        LOG_debug << "Trying to store an encrypted node";
        node->applykey();
        node->setattr();

        if (node->attrstring)
        {
            LOG_debug << "Storing an encrypted node.";
        }
    }

    mTable->put(node);
}

size_t NodeManager::nodeNotifySize() const
{
    LockGuard g(mMutex);
    return mNodeNotify.size();
}

bool NodeManager::FingerprintContainer::allFingerprintsAreLoaded(const FileFingerprint *fingerprint) const
{
    return mAllFingerprintsLoaded.find(*fingerprint) != mAllFingerprintsLoaded.end();
}

void NodeManager::FingerprintContainer::setAllFingerprintLoaded(const mega::FileFingerprint *fingerprint)
{
    mAllFingerprintsLoaded.insert(*fingerprint);
}

void NodeManager::FingerprintContainer::clear()
{
    fingerprint_set::clear();
    mAllFingerprintsLoaded.clear();
}

} // namespace
