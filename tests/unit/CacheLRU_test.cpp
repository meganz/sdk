/**
 * @file CacheLRU_test.cpp
 * @brief Unitary test for cache LRU
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

#include <gtest/gtest.h>

#include <mega/megaclient.h>
#include <mega/megaapp.h>
#include <mega/user.h>
#include <mega/utils.h>

#include "utils.h"
#include "mega.h"


TEST(CacheLRU, checkNumNodes_higherLRUSize)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);


    for (uint32_t i = 0; i < LRUsize - 4; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &rootNode);
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), client->mNodeManager.getNumNodesAtCacheLRU());
    ASSERT_EQ(client->mNodeManager.getNodeCount(), client->mNodeManager.getNumberNodesInRam());

    for (uint32_t i = 0; i < 8; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &rootNode);
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    // 2 (rubbis + vault) -> root node is load at LRU when getParent is called
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 2);
    ASSERT_EQ(client->mNodeManager.getNodeCount(), index - 1);
}

TEST(CacheLRU, checkNumNodes_LRUSize)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    for (uint32_t i = 0; i < LRUsize - 4; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize);
    ASSERT_EQ(client->mNodeManager.getNodeCount(), LRUsize);

    for (uint32_t i = 0; i < 4; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    // 3 root nodes -> folder is at LRU cache, it accesed to set parent from new children
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 3);
    ASSERT_EQ(client->mNodeManager.getNodeCount(), index - 1);
}


TEST(CacheLRU, removeNode)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint64_t indexFromNodeAtLRU = index;
    uint32_t numNodes = 15;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        file.attrs.map = std::map<mega::nameid, std::string>{
                                                             {101, "foo"},
                                                             {102, "bar"},
                                                             };
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    // Root node + rubbish + vault
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(client->mNodeManager.getNodeCount(), numNodes + 4);

    // 3 root nodes -> folder is at LRU cache, it's accesed to set parent from new children
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 3);
    ASSERT_EQ(client->mNodeManager.getNodeCount(), index - 1);

    std::shared_ptr<mega::Node> nodeToRemove = client->mNodeManager.getNodeByHandle(mega::NodeHandle().set6byte(indexFromNodeAtLRU));
    nodeToRemove->changed.removed = true;
    client->mNodeManager.notifyNode(nodeToRemove);
    nodeToRemove.reset();
    client->mNodeManager.notifyPurge();
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 2);
    ASSERT_EQ(client->mNodeManager.getNumNodesAtCacheLRU(), LRUsize - 1);
    ASSERT_EQ(client->mNodeManager.getNodeCount(), index - 2);
}

TEST(CacheLRU, getNodebyFingerprint_RAM_NoLRU)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint32_t numNodes = 15;
    std::vector<std::string> fingerprints;
    std::shared_ptr<mega::Node> nodeRemovedFromLRU;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        // Modify fingerprint look nodes by fingerprint
        file.crc[0] = static_cast<int32_t>(index);
        file.crc[1] = static_cast<int32_t>(index);
        file.crc[2] = static_cast<int32_t>(index);
        file.crc[3] = static_cast<int32_t>(index);
        file.isvalid = true;
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"},};
        auxiliarNode.reset(&file);
        std::string fp;
        file.mega::FileFingerprint::serialize(&fp);
        fingerprints.push_back(fp);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());

        if (i == 1)
        {
            nodeRemovedFromLRU = auxiliarNode;
        }
    }

    // Root node + rubbish + vault + node with reference
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 4);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(client->mNodeManager.getNodeCount(), numNodes + 4);


    ASSERT_GT(fingerprints.size(), 1);

    // No found at LRU, fingerprint at DB
    const char* fingerpritnString = fingerprints.front().data();
    std::unique_ptr<mega::FileFingerprint> fp(mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size()));
    std::shared_ptr<mega::Node> node(client->mNodeManager.getNodeByFingerprint(*fp));
    ASSERT_NE(node.get(), nullptr);

    // No found at LRU, fingerprint at DB but node is in RAM
    fingerpritnString = fingerprints.at(1).data();
    fp = mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size());
    node = client->mNodeManager.getNodeByFingerprint(*fp);
    ASSERT_NE(node.get(), nullptr);
    ASSERT_EQ(node.get(), nodeRemovedFromLRU.get());

    //Found at LRU, fingerprint at mFingerPrints
    fingerpritnString = fingerprints.back().data();
    fp = mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size());
    node = client->mNodeManager.getNodeByFingerprint(*fp);
    ASSERT_NE(node.get(), nullptr);
}

TEST(CacheLRU, getNodeByFingerprint_NoRAM_NoLRU)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint32_t numNodes = 15;
    std::vector<std::string> fingerprints;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        // Modify fingerprint look nodes by fingerprint
        file.crc[0] = static_cast<int32_t>(index);
        file.crc[1] = static_cast<int32_t>(index);
        file.crc[2] = static_cast<int32_t>(index);
        file.crc[3] = static_cast<int32_t>(index);
        file.isvalid = true;
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"},};
        auxiliarNode.reset(&file);
        std::string fp;
        file.mega::FileFingerprint::serialize(&fp);
        fingerprints.push_back(fp);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    // Root node + rubbish + vault
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(client->mNodeManager.getNodeCount(), numNodes + 4);

    // 3 root nodes -> folder is at LRU cache, it's accesed to set parent from new children
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 3);
    ASSERT_EQ(client->mNodeManager.getNodeCount(), index - 1);

    ASSERT_GT(fingerprints.size(), 1);

    // No found at LRU, fingerprint at DB
    const char* fingerpritnString = fingerprints.front().data();
    std::unique_ptr<mega::FileFingerprint> fp(mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size()));
    mega::sharedNode_vector nodes(client->mNodeManager.getNodesByFingerprint(*fp));
    ASSERT_EQ(nodes.size(), 1);

    //Found at LRU, fingerprint at mFingerPrints
    fingerpritnString = fingerprints.back().data();
    fp = mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size());
    nodes = client->mNodeManager.getNodesByFingerprint(*fp);
    ASSERT_EQ(nodes.size(), 1);

}

TEST(CacheLRU, searchNode) // processUnserializedNodes
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint32_t numNodes = 15;
    std::vector<std::string> names;
    std::shared_ptr<mega::Node> nodeInRAM;
    std::string nameNodeInRam;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        std::string name = "name" + std::to_string(index);
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"},{110, name}};
        auxiliarNode.reset(&file);
        names.push_back(name);
        if (i == 1)
        {
            nodeInRAM = auxiliarNode;
            nameNodeInRam = name;
        }
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    // Root node + rubbish + vault + node in RAM
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 4);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(client->mNodeManager.getNodeCount(), numNodes + 4);
    ASSERT_EQ(client->mNodeManager.getNodeCount(), index - 1);

    ASSERT_GT(names.size(), 1);

    // No found at LRU
    mega::NodeSearchFilter searchFilter;
    searchFilter.byAncestors(std::vector<mega::handle>{rootNode.nodehandle, mega::UNDEF, mega::UNDEF});
    searchFilter.byName(names.front());
    mega::sharedNode_vector nodes(client->mNodeManager.searchNodes(searchFilter, 0 /*order None*/, mega::CancelToken(), mega::NodeSearchPage{0, 0}));
    ASSERT_EQ(nodes.size(), 1);

    // No found at LRU but in RAM
    searchFilter.byName(nameNodeInRam);
    nodes = client->mNodeManager.searchNodes(searchFilter, 0 /*order None*/, mega::CancelToken(), mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 1);

    //Found at LRU
    searchFilter.byName(names.back());
    nodes = client->mNodeManager.searchNodes(searchFilter, 0 /*order None*/, mega::CancelToken(), mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 1);
    ASSERT_EQ(nodes.size(), 1);
}


TEST(CacheLRU, getChildren)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    std::shared_ptr<mega::Node> folder1(&mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode));
    folder1->attrs.map = std::map<mega::nameid, std::string>{{110, "Folder1"}};
    client->mNodeManager.addNode(folder1, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    std::shared_ptr<mega::Node> folder2(&mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode));
    folder2->attrs.map = std::map<mega::nameid, std::string>{{110, "Folder2"}};
    client->mNodeManager.addNode(folder2, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    std::shared_ptr<mega::Node> folder3(&mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode));
    folder3->attrs.map = std::map<mega::nameid, std::string>{{110, "Folder3"}};
    client->mNodeManager.addNode(folder3, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint32_t numNodesFolder1 = 2;
    for (uint32_t i = 0; i < numNodesFolder1; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), folder1.get());
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}};
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    mega::sharedNode_list children = client->mNodeManager.getChildren(folder1.get()); // All children in RAM
    ASSERT_EQ(children.size(), numNodesFolder1);

    uint32_t numNodesFolder2 = 10;
    for (uint32_t i = 0; i < numNodesFolder2; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), folder2.get());
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}};
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    children = client->mNodeManager.getChildren(folder2.get()); // Some children in LRU other in DB
    ASSERT_EQ(children.size(), numNodesFolder2);

    uint32_t numNodesFolder3 = 10;
    std::shared_ptr<mega::Node> nodeInRAM;
    for (uint32_t i = 0; i < numNodesFolder3; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), folder3.get());
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}};
        auxiliarNode.reset(&file);
        if (i == numNodesFolder3 / 2)
        {
            nodeInRAM = auxiliarNode;
        }

        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    children = client->mNodeManager.getChildren(folder3.get()); // Some children in LRU other in DB and one in RAM
    ASSERT_EQ(children.size(), numNodesFolder3);


    children = client->mNodeManager.getChildren(folder1.get()); // None node in RAM
    ASSERT_EQ(children.size(), numNodesFolder1);
}


TEST(CacheLRU, getNodeByHandle)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint32_t numNodes = 15;
    std::vector<mega::NodeHandle> handles;
    std::shared_ptr<mega::Node> nodeInRAM;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        std::string name = "name" + std::to_string(index);
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"},{110, name}};
        auxiliarNode.reset(&file);
        handles.push_back(auxiliarNode->nodeHandle());
        if (i == numNodes / 2)
        {
            nodeInRAM = auxiliarNode;
        }
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    ASSERT_GT(handles.size(), 1);
    mega::NodeHandle firstNodeHandle = handles.front();
    mega::NodeHandle lasttNodeHandle = handles.front();
    mega::NodeHandle nodeInRAMHandle = nodeInRAM->nodeHandle();

    // No Node at RAM => no  at LRU

    //ASSERT_EQ(client->mNodeManager.getNodeInRAM(firstNodeHandle).get(), nullptr);
    std::shared_ptr<mega::Node> node = client->mNodeManager.getNodeByHandle(firstNodeHandle);

    // Node at RAM and LRU
    auxiliarNode = client->mNodeManager.getNodeByHandle(lasttNodeHandle);
    ASSERT_NE(auxiliarNode, nullptr);
    ASSERT_NE(auxiliarNode->mNodePosition->second.mLRUPosition, client->mNodeManager.invalidCacheLRUPos());
    node = client->mNodeManager.getNodeByHandle(lasttNodeHandle);
    ASSERT_EQ(auxiliarNode.get(), node.get());

    // Node at RAM, no at LRU
    //ASSERT_NE(client->mNodeManager.getNodeInRAM(nodeInRAMHandle).get(), nullptr);
    ASSERT_NE(nodeInRAM, nullptr);
    ASSERT_EQ(nodeInRAM->mNodePosition->second.mLRUPosition, client->mNodeManager.invalidCacheLRUPos());
    node = client->mNodeManager.getNodeByHandle(nodeInRAMHandle);
    ASSERT_EQ(nodeInRAM.get(), node.get());
}

TEST(CacheLRU, childNodeByNameType)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 8;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint32_t numNodes = 15;
    std::vector<std::string> names;
    std::shared_ptr<mega::Node> nodeInRAM;
    std::string nameNodeInRam;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        std::string name = "name" + std::to_string(index);
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"},{110, name}};
        auxiliarNode.reset(&file);
        names.push_back(name);
        if (i == 1)
        {
            nodeInRAM = auxiliarNode;
            nameNodeInRam = name;
        }
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    // Root node + rubbish + vault + node in RAM
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 4);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(client->mNodeManager.getNodeCount(), numNodes + 4);

    ASSERT_GT(names.size(), 1);

    // No found at LRU
    std::shared_ptr<mega::Node> node = client->mNodeManager.childNodeByNameType(&folder, names.front().c_str(), mega::nodetype_t::FILENODE);
    ASSERT_NE(node, nullptr);

    // No found at LRU but in RAM
    node = client->mNodeManager.childNodeByNameType(&folder, nameNodeInRam.c_str(), mega::nodetype_t::FILENODE);
    ASSERT_NE(node, nullptr);

    //Found at LRU
    node = client->mNodeManager.childNodeByNameType(&folder, names.back().c_str(), mega::nodetype_t::FILENODE);
    ASSERT_NE(node, nullptr);
}


TEST(CacheLRU, reduceCacheLRUSize)
{
    mega::MegaApp app;
    mega::SqliteDbAccess* dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));

    uint32_t LRUsize = 20;

    auto client = mt::makeClient(app, dbAccess);
    client->sid = "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";

    client->opensctable();
    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    uint64_t index = 1;

    mega::NodeManager::MissingParentNodes missingParentNodes;
    auto& rootNode = mt::makeNode(*client, mega::nodetype_t::ROOTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node> auxiliarRootNode(&rootNode);
    client->mNodeManager.addNode(auxiliarRootNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarRootNode.get());

    auto& vaultNode = mt::makeNode(*client, mega::nodetype_t::VAULTNODE, mega::NodeHandle().set6byte(index++), nullptr);
    std::shared_ptr<mega::Node>auxiliarNode(&vaultNode);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    auto& rubbishbin = mt::makeNode(*client, mega::nodetype_t::RUBBISHNODE, mega::NodeHandle().set6byte(index++), nullptr);
    auxiliarNode.reset(&rubbishbin);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), 3);

    auto& folder = mt::makeNode(*client, mega::nodetype_t::FOLDERNODE, mega::NodeHandle().set6byte(index++), &rootNode);
    auxiliarNode.reset(&folder);
    client->mNodeManager.addNode(auxiliarNode, false, true, missingParentNodes);
    client->mNodeManager.saveNodeInDb(auxiliarNode.get());

    uint32_t numNodes = LRUsize;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto& file = mt::makeNode(*client, mega::nodetype_t::FILENODE, mega::NodeHandle().set6byte(index++), &folder);
        file.size = index;
        file.owner = 88;
        file.ctime = 44;
        std::string name = "name" + std::to_string(index);
        file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"},{110, name}};
        auxiliarNode.reset(&file);
        client->mNodeManager.addNode(auxiliarNode, true, false, missingParentNodes);
        client->mNodeManager.saveNodeInDb(auxiliarNode.get());
    }

    // Root node + rubbish + vault
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(client->mNodeManager.getNodeCount(), numNodes + 4);

    LRUsize = 8;

    client->mNodeManager.setCacheLRUMaxSize(LRUsize);

    // Root node + rubbish + vault
    ASSERT_EQ(client->mNodeManager.getNumberNodesInRam(), LRUsize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(client->mNodeManager.getNodeCount(), numNodes + 4);

}
