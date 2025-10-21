/**
 * @file CacheLRU_test.cpp
 * @brief Unitary test for NodeManger cache LRU
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

#include "mega.h"
#include "utils.h"

#include <gtest/gtest.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/user.h>
#include <mega/utils.h>

class CacheLRU: public testing::Test
{
protected:
    uint32_t mLruSize = 0;
    mega::MegaApp mApp;
    mega::NodeManager::MissingParentNodes mMissingParentNodes;
    uint64_t mIndex = 1;
    static constexpr uint64_t mNumRootNodes{3}; // Root node + rubbish + vault
    std::shared_ptr<mega::MegaClient> mClient;

    std::shared_ptr<mega::Node> addRootNodes()
    {
        auto rootNode = addNode(mega::nodetype_t::ROOTNODE, nullptr, false, true);
        addNode(mega::nodetype_t::VAULTNODE, nullptr, false, true);
        addNode(mega::nodetype_t::RUBBISHNODE, nullptr, false, true);
        return rootNode;
    }

public:
    std::shared_ptr<mega::Node> init(uint32_t lruSize)
    {
        mLruSize = lruSize;
        auto dbAccess = new mega::SqliteDbAccess(mega::LocalPath::fromAbsolutePath("."));
        mClient = mt::makeClient(mApp, dbAccess);
        mClient->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        mClient->opensctable();
        mClient->mNodeManager.setCacheLRUMaxSize(mLruSize);
        return addRootNodes();
    }

    ~CacheLRU()
    {
        mClient.reset();
    }

    uint64_t numNodesInRam() const
    {
        return mClient->mNodeManager.getNumberNodesInRam();
    }

    uint64_t numNodesInCacheLru() const
    {
        return mClient->mNodeManager.getNumNodesAtCacheLRU();
    }

    uint64_t numNodesTotal() const
    {
        return mClient->mNodeManager.getNodeCount();
    }

    void setLruMaxSize(uint32_t size)
    {
        mClient->mNodeManager.setCacheLRUMaxSize(size);
        mLruSize = size;
    }

    std::shared_ptr<mega::Node> addNode(mega::nodetype_t nodeType,
                                        const std::shared_ptr<mega::Node>& parent,
                                        bool notify,
                                        bool isFetching,
                                        std::function<void(mega::Node&)> nodeSetupCb = nullptr)
    {
        auto& nodeRef =
            mt::makeNode(*mClient, nodeType, mega::NodeHandle().set6byte(mIndex++), parent.get());
        std::shared_ptr<mega::Node> node(&nodeRef);
        if (nodeSetupCb)
        {
            nodeSetupCb(nodeRef);
        }

        node->serializefingerprint(&node->attrs.map['c']);
        node->setfingerprint();
        mClient->mNodeManager.addNode(node, notify, isFetching, mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(node.get());
        return node;
    }

    void checkNodesInCache(std::optional<uint64_t> expInRam,
                           std::optional<uint64_t> expTotal,
                           std::optional<uint64_t> expInLRU)
    {
        if (expInRam.has_value())
        {
            ASSERT_EQ(numNodesInRam(), expInRam) << "Unexpected num nodes in RAM";
        }
        if (expTotal.has_value())
        {
            ASSERT_EQ(numNodesTotal(), expTotal) << "Unexpected total num nodes";
        }
        if (expInLRU.has_value())
        {
            ASSERT_EQ(numNodesInCacheLru(), expInLRU) << "Unexpected num nodes in LRU cache";
        }
    }

    void addNodeInCache(std::vector<std::string>& fingerprints,
                        std::shared_ptr<mega::Node> folderNode,
                        const m_off_t size,
                        const int64_t mtime,
                        const int32_t crcPart,
                        const unsigned int numNodesWithSameData = 1,
                        const mega::handle owner = 1,
                        std::optional<mega::attr_map> attrs = std::nullopt)
    {
        for (unsigned int i = 0; i < numNodesWithSameData; ++i)
        {
            addNode(mega::nodetype_t::FILENODE,
                    folderNode,
                    true,
                    false,
                    [&fingerprints, attrs, owner, size, mtime, crcPart](mega::Node& file)
                    {
                        file.size = static_cast<m_off_t>(size);
                        file.owner = owner;
                        file.ctime = mtime;
                        file.mtime = mtime;
                        file.crc[0] = crcPart;
                        file.crc[1] = crcPart;
                        file.crc[2] = crcPart;
                        file.crc[3] = crcPart;
                        file.isvalid = true;
                        file.attrs.map = attrs.value_or(
                            []
                            {
                                mega::attr_map m;
                                m[101] = "foo";
                                m[102] = "bar";
                                return m;
                            }());

                        std::string fp;
                        file.mega::FileFingerprint::serialize(&fp);
                        fingerprints.push_back(fp);
                    });
        }
    }

    void expectedNodeCountByFp(std::vector<std::string>& fingerprints,
                               size_t expectedNodeCount,
                               const size_t fpIdx,
                               const bool excludeMtime = true)
    {
        ASSERT_LT(fpIdx, fingerprints.size()) << "expectedNodeCountByFp Invalid Fp Index";

        const char* fingerpritnString = fingerprints.at(fpIdx).data();
        std::unique_ptr<mega::FileFingerprint> fp(
            mega::FileFingerprint::unserialize(fingerpritnString,
                                               fingerpritnString + fingerprints.front().size()));
        ASSERT_TRUE(!!fp) << "expectedNodeCountByFp: fingerprint could not be unserialized";

        mega::sharedNode_vector nodes(
            mClient->mNodeManager.getNodesByFingerprint(*fp, excludeMtime));

        ASSERT_EQ(nodes.size(), expectedNodeCount)
            << "expectedNodeCountByFp: Unexpected node count";
    }
};

TEST_F(CacheLRU, checkNumNodes_higherLRUSize)
{
    auto rootNode = init(8);
    ASSERT_EQ(numNodesInRam(), 3);
    for (uint32_t i = 0; i < mLruSize - 4; i++)
    {
        addNode(mega::nodetype_t::FILENODE, rootNode, false, true);
    }

    ASSERT_EQ(numNodesInRam(), numNodesInCacheLru());
    ASSERT_EQ(numNodesTotal(), numNodesInRam());

    for (uint32_t i = 0; i < mLruSize; i++)
    {
        addNode(mega::nodetype_t::FILENODE, rootNode, true, false);
    }

    // 2 (rubbis + vault) -> root node is load at LRU when getParent is called
    ASSERT_EQ(numNodesInRam(), mLruSize + 2);
    ASSERT_EQ(numNodesTotal(), mIndex - 1);
}

TEST_F(CacheLRU, checkNumNodes_LRUSize)
{
    auto rootNode = init(8);
    ASSERT_EQ(numNodesInRam(), 3);
    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);

    for (uint32_t i = 0; i < mLruSize - 4; i++)
    {
        addNode(mega::nodetype_t::FILENODE, folder, true, false);
    }
    ASSERT_EQ(numNodesInRam(), mLruSize);
    ASSERT_EQ(numNodesTotal(), mLruSize);

    for (uint32_t i = 0; i < 4; i++)
    {
        addNode(mega::nodetype_t::FILENODE, folder, true, false);
    }

    // 3 root nodes -> folder is at LRU cache, it accesed to set parent from new children
    ASSERT_EQ(numNodesInRam(), mLruSize + 3);
    ASSERT_EQ(numNodesTotal(), mIndex - 1);
}

TEST_F(CacheLRU, removeNode)
{
    auto rootNode = init(8);
    ASSERT_EQ(numNodesInRam(), 3);
    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);
    uint64_t indexFromNodeAtLRU = mIndex;
    uint32_t numNodes = 15;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        addNode(
            mega::nodetype_t::FILENODE,
            folder,
            true,
            false,
            [this](mega::Node& file)
            {
                file.size = static_cast<m_off_t>(mIndex);
                file.owner = 88;
                file.ctime = 44;
                file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}};
            });
    }
    // Root node + rubbish + vault
    ASSERT_EQ(numNodesInRam(), mLruSize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(numNodesTotal(), numNodes + 4);
    // 3 root nodes -> folder is at LRU cache, it's accesed to set parent from new children
    ASSERT_EQ(numNodesInRam(), mLruSize + 3);
    ASSERT_EQ(numNodesTotal(), mIndex - 1);
    auto& nodeMgr = mClient->mNodeManager;
    auto nodeToRemove = nodeMgr.getNodeByHandle(mega::NodeHandle().set6byte(indexFromNodeAtLRU));
    nodeToRemove->changed.removed = true;
    nodeMgr.notifyNode(nodeToRemove);
    nodeToRemove.reset();
    nodeMgr.notifyPurge();
    ASSERT_EQ(numNodesInRam(), mLruSize + 2);
    ASSERT_EQ(numNodesInCacheLru(), mLruSize - 1);
    ASSERT_EQ(numNodesTotal(), mIndex - 2);
}

TEST_F(CacheLRU, getNodebyFingerprint_RAM_NoLRU)
{
    auto rootNode = init(8);
    ASSERT_EQ(numNodesInRam(), 3);
    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);
    uint32_t numNodes = 15;
    std::vector<std::string> fingerprints;
    std::shared_ptr<mega::Node> nodeRemovedFromLRU;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto fileNode = addNode(
            mega::nodetype_t::FILENODE,
            folder,
            true,
            false,
            [this, &fingerprints](mega::Node& file)
            {
                auto index = static_cast<int32_t>(mIndex);
                file.size = static_cast<m_off_t>(mIndex);
                file.owner = 88;
                file.ctime = 44;
                // Modify fingerprint look nodes by fingerprint
                file.crc[0] = index;
                file.crc[1] = index;
                file.crc[2] = index;
                file.crc[3] = index;
                file.isvalid = true;
                file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}};
                std::string fp;
                file.mega::FileFingerprint::serialize(&fp);
                fingerprints.push_back(fp);
            });
        if (i == 1)
        {
            nodeRemovedFromLRU = fileNode;
        }
    }
    folder.reset();
    // Root node + rubbish + vault + node with reference
    ASSERT_EQ(numNodesInRam(), mLruSize + 4);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(numNodesTotal(), numNodes + 4);

    ASSERT_GT(fingerprints.size(), 1);

    // No found at LRU, fingerprint at DB
    const char* fingerpritnString = fingerprints.front().data();
    std::unique_ptr<mega::FileFingerprint> fp(mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size()));
    std::shared_ptr<mega::Node> node(mClient->mNodeManager.getNodeByFingerprint(*fp));
    ASSERT_NE(node.get(), nullptr);

    // No found at LRU, fingerprint at DB but node is in RAM
    fingerpritnString = fingerprints.at(1).data();
    fp = mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size());
    node = mClient->mNodeManager.getNodeByFingerprint(*fp);
    ASSERT_NE(node.get(), nullptr);
    ASSERT_EQ(node.get(), nodeRemovedFromLRU.get());

    // Found at LRU, fingerprint at mFingerPrints
    fingerpritnString = fingerprints.back().data();
    fp = mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size());
    node = mClient->mNodeManager.getNodeByFingerprint(*fp);
    ASSERT_NE(node.get(), nullptr);
}

TEST_F(CacheLRU, getNodeByFingerprint_NoRAM_NoLRU)
{
    auto rootNode = init(8);
    ASSERT_EQ(mClient->mNodeManager.getNumberNodesInRam(), 3);

    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);
    uint32_t numNodes = 15;
    std::vector<std::string> fingerprints;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        addNode(
            mega::nodetype_t::FILENODE,
            folder,
            true,
            false,
            [this, &fingerprints](mega::Node& file)
            {
                auto index = static_cast<int32_t>(mIndex);
                file.size = static_cast<m_off_t>(index);
                file.owner = 88;
                file.ctime = 44;
                file.mtime = 44;
                // Modify fingerprint look nodes by fingerprint
                file.crc[0] = index;
                file.crc[1] = index;
                file.crc[2] = index;
                file.crc[3] = index;
                file.isvalid = true;
                file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}};
                std::string fp;
                file.mega::FileFingerprint::serialize(&fp);
                fingerprints.push_back(fp);
            });
    }
    folder.reset();
    // Root node + rubbish + vault
    ASSERT_EQ(numNodesInRam(), mLruSize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(numNodesTotal(), numNodes + 4);

    // 3 root nodes -> folder is at LRU cache, it's accesed to set parent from new children
    ASSERT_EQ(numNodesInRam(), mLruSize + 3);
    ASSERT_EQ(numNodesTotal(), mIndex - 1);

    ASSERT_GT(fingerprints.size(), 1);

    // No found at LRU, fingerprint at DB
    const char* fingerpritnString = fingerprints.front().data();
    std::unique_ptr<mega::FileFingerprint> fp(mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size()));
    mega::sharedNode_vector nodes(mClient->mNodeManager.getNodesByFingerprint(*fp));
    ASSERT_EQ(nodes.size(), 1);

    // Found at LRU, fingerprint at mFingerPrints
    fingerpritnString = fingerprints.back().data();
    fp = mega::FileFingerprint::unserialize(fingerpritnString, fingerpritnString + fingerprints.front().size());
    nodes = mClient->mNodeManager.getNodesByFingerprint(*fp);
    ASSERT_EQ(nodes.size(), 1);
}

TEST_F(CacheLRU, getNodesByFingerprintIgnoringMtime)
{
    constexpr uint32_t lruSize{8};
    auto rootNode = init(lruSize);
    ASSERT_EQ(mClient->mNodeManager.getNumberNodesInRam(), 3);
    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);
    std::vector<std::string> fingerprints;
    uint64_t expectedNumNodes{0};
    const std::vector<uint32_t> numNodes{5, 10, 15};

    expectedNumNodes = numNodes[0] * 3;
    addNodeInCache(fingerprints, folder, 10 /*size=*/, 20 /*mtime=*/, 30 /*crc*/, numNodes[0]);
    addNodeInCache(fingerprints, folder, 20 /*size=*/, 20 /*mtime=*/, 40 /*crc*/, numNodes[0]);
    addNodeInCache(fingerprints, folder, 30 /*size=*/, 40 /*mtime=*/, 50 /*crc*/, numNodes[0]);
    ASSERT_EQ(fingerprints.size(), expectedNumNodes);

    ASSERT_NO_FATAL_FAILURE(checkNodesInCache(mNumRootNodes + mLruSize,
                                              mNumRootNodes + expectedNumNodes + +1 /*testFolder*/,
                                              lruSize))
        << "TC1: Unexpected num nodes in cache";

    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 0 /*FpIndex*/, true /*excludeMtime*/))
        << "TC2: getNodesByFingerprint(ignoring mtime)";
    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 5 /*FpIndex*/, true /*excludeMtime*/))
        << "TC3: getNodesByFingerprint(ignoring mtime)";
    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 10 /*FpIndex*/, true /*excludeMtime*/))
        << "TC4: getNodesByFingerprint(ignoring mtime)";
    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 0 /*FpIndex*/, true /*excludeMtime*/))
        << "TC5: getNodesByFingerprint(ignoring mtime)";
    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 5 /*FpIndex*/, true /*excludeMtime*/))
        << "TC6: getNodesByFingerprint(ignoring mtime)";
    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 10 /*FpIndex*/, true /*excludeMtime*/))
        << "TC7: getNodesByFingerprint(ignoring mtime)";

    addNodeInCache(fingerprints, folder, 40, 50, 60, numNodes[1]);
    addNodeInCache(fingerprints, folder, 40, 80, 60, numNodes[2]);
    folder.reset();

    expectedNumNodes += numNodes[1] + numNodes[2];
    ASSERT_NO_FATAL_FAILURE(checkNodesInCache(mNumRootNodes + mLruSize,
                                              mNumRootNodes + expectedNumNodes + +1 /*testFolder*/,
                                              lruSize))
        << "TC8: Unexpected num nodes in cache";

    ASSERT_NO_FATAL_FAILURE(expectedNodeCountByFp(fingerprints,
                                                  numNodes[1] + numNodes[2],
                                                  15 /*FpIndex*/,
                                                  true /*excludeMtime*/))
        << "TC9: getNodesByFingerprint(ignoring mtime)";

    ASSERT_NO_FATAL_FAILURE(expectedNodeCountByFp(fingerprints,
                                                  numNodes[1] + numNodes[2],
                                                  15 /*FpIndex*/,
                                                  true /*excludeMtime*/))
        << "TC10: getNodesByFingerprint(ignoring mtime)";

    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[1], 15 /*FpIndex*/, false /*excludeMtime*/))
        << "TC11: getNodesByFingerprint(including mtime)";
}

TEST_F(CacheLRU, getNodesByFingerprintIgnoringMtimeSmallLRUCache)
{
    constexpr uint32_t lruSize{2};
    auto rootNode = init(lruSize);
    uint64_t expectedNumNodes{0};
    ASSERT_EQ(mClient->mNodeManager.getNumberNodesInRam(), 3);
    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);
    std::vector<std::string> fingerprints;
    const std::vector<uint32_t> numNodes{2, 3};
    addNodeInCache(fingerprints, folder, 40, 50, 60, numNodes[0]);
    addNodeInCache(fingerprints, folder, 40, 80, 60, numNodes[1]);
    expectedNumNodes = numNodes[0] + numNodes[1];
    folder.reset();

    ASSERT_NO_FATAL_FAILURE(checkNodesInCache(mNumRootNodes + mLruSize,
                                              mNumRootNodes + expectedNumNodes + +1 /*testFolder*/,
                                              lruSize))
        << "TC1: Unexpected num nodes in cache";

    // Get nodes by Fingerprint (repeating use cases to force LRU cache to discard nodes)
    // Also test both cases including/ignoring mtime in fingerprint comparison
    ASSERT_NO_FATAL_FAILURE(expectedNodeCountByFp(fingerprints,
                                                  numNodes[0] + numNodes[1],
                                                  0 /*FpIndex*/,
                                                  true /*excludeMtime*/))
        << "TC2: getNodesByFingerprint(ignoring mtime)";

    ASSERT_NO_FATAL_FAILURE(expectedNodeCountByFp(fingerprints,
                                                  numNodes[0] + numNodes[1],
                                                  2 /*FpIndex*/,
                                                  true /*excludeMtime*/))
        << "TC3: getNodesByFingerprint(ignoring mtime)";

    ASSERT_NO_FATAL_FAILURE(expectedNodeCountByFp(fingerprints,
                                                  numNodes[0] + numNodes[1],
                                                  0 /*FpIndex*/,
                                                  true /*excludeMtime*/))
        << "TC4: getNodesByFingerprint(ignoring mtime)";

    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 0 /*FpIndex*/, false /*excludeMtime*/))
        << "TC5: getNodesByFingerprint(including mtime)";

    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[1], 2 /*FpIndex*/, false /*excludeMtime*/))
        << "TC6: getNodesByFingerprint(including mtime)";

    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[0], 0 /*FpIndex*/, false /*excludeMtime*/))
        << "TC7: getNodesByFingerprint(including mtime)";

    ASSERT_NO_FATAL_FAILURE(
        expectedNodeCountByFp(fingerprints, numNodes[1], 2 /*FpIndex*/, false /*excludeMtime*/))
        << "TC8: getNodesByFingerprint(including mtime)";
}

TEST_F(CacheLRU, searchNode) // processUnserializedNodes
{
    auto rootNode = init(8);
    ASSERT_EQ(mClient->mNodeManager.getNumberNodesInRam(), 3);

    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);
    uint32_t numNodes = 15;
    std::vector<std::string> names;
    std::shared_ptr<mega::Node> nodeInRAM;
    std::string nameNodeInRam;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        std::string name = "name" + std::to_string(mIndex);
        names.push_back(name);
        auto fileNode = addNode(
            mega::nodetype_t::FILENODE,
            folder,
            true,
            false,
            [this, &name](mega::Node& file)
            {
                file.size = static_cast<m_off_t>(mIndex);
                file.owner = 88;
                file.ctime = 44;
                file.attrs.map =
                    std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}, {110, name}};
            });
        if (i == 1)
        {
            nodeInRAM = fileNode;
            nameNodeInRam = name;
        }
    }

    // Root node + rubbish + vault + node in RAM
    ASSERT_EQ(numNodesInRam(), mLruSize + 4);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(numNodesTotal(), numNodes + 4);
    ASSERT_EQ(numNodesTotal(), mIndex - 1);

    ASSERT_GT(names.size(), 1);

    // No found at LRU
    mega::NodeSearchFilter searchFilter;
    searchFilter.byAncestors({rootNode->nodehandle, mega::UNDEF, mega::UNDEF});
    searchFilter.byName(names.front());
    auto& nodeMgr = mClient->mNodeManager;
    mega::sharedNode_vector nodes(nodeMgr.searchNodes(searchFilter,
                                                      0 /*order None*/,
                                                      mega::CancelToken(),
                                                      mega::NodeSearchPage{0, 0}));
    ASSERT_EQ(nodes.size(), 1);

    // No found at LRU but in RAM
    searchFilter.byName(nameNodeInRam);
    nodes = nodeMgr.searchNodes(searchFilter,
                                0 /*order None*/,
                                mega::CancelToken(),
                                mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 1);

    // Found at LRU
    searchFilter.byName(names.back());
    nodes = nodeMgr.searchNodes(searchFilter,
                                0 /*order None*/,
                                mega::CancelToken(),
                                mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 1);

    // Search a not out shared folder by out share
    searchFilter.byAncestors({mega::UNDEF, mega::UNDEF, mega::UNDEF});
    searchFilter.byName("");
    searchFilter.setIncludedShares(mega::OUT_SHARES);
    nodes = nodeMgr.searchNodes(searchFilter,
                                0 /*order None*/,
                                mega::CancelToken(),
                                mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 0);

    // Set the folder as public link
    folder->plink.reset(new mega::PublicLink{0x1, 0x1, 0x1, false});
    nodeMgr.saveNodeInDb(folder.get());
    // Search
    searchFilter.setIncludedShares(mega::LINK);
    nodes = nodeMgr.searchNodes(searchFilter,
                                0 /*order None*/,
                                mega::CancelToken(),
                                mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 16);

    // Set the folder as out shared as well
    mega::User user{"name@name.com"};
    folder->outshares.reset(new mega::share_map{});
    folder->outshares->emplace(0x1ull, std::make_unique<mega::Share>(&user, mega::FULL, 0x1));
    nodeMgr.saveNodeInDb(folder.get());
    // Search by public link
    searchFilter.setIncludedShares(mega::LINK);
    nodes = nodeMgr.searchNodes(searchFilter,
                                0 /*order None*/,
                                mega::CancelToken(),
                                mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 16);

    // Search out shares with name
    searchFilter.byName(names.back());
    searchFilter.setIncludedShares(mega::OUT_SHARES);
    nodes = nodeMgr.searchNodes(searchFilter,
                                0 /*order None*/,
                                mega::CancelToken(),
                                mega::NodeSearchPage{0, 0});
    ASSERT_EQ(nodes.size(), 1);
}

TEST_F(CacheLRU, getChildren)
{
    auto rootNode = init(8);
    auto& nodeMgr = mClient->mNodeManager;
    ASSERT_EQ(numNodesInRam(), 3);
    std::array<uint32_t, 3> numNodesForFolder = {2, 10, 10};
    std::array<std::shared_ptr<mega::Node>, 3> folders;
    std::shared_ptr<mega::Node> nodeInRAM;
    for (uint32_t f = 0; f < 3; f++)
    {
        auto& folder = folders[f] = addNode(mega::nodetype_t::FOLDERNODE,
                                            rootNode,
                                            false,
                                            true,
                                            [f](mega::Node& folderNode)
                                            {
                                                folderNode.attrs.map =
                                                    std::map<mega::nameid, std::string>{
                                                        {110, "Folder" + std::to_string(f + 1)}};
                                            });
        auto numNodes = numNodesForFolder[f];
        for (uint32_t i = 0; i < numNodes; i++)
        {
            auto fileNode =
                addNode(mega::nodetype_t::FILENODE,
                        folder,
                        true,
                        false,
                        [this](mega::Node& file)
                        {
                            file.size = static_cast<m_off_t>(mIndex);
                            file.owner = 88;
                            file.ctime = 44;
                            file.attrs.map =
                                std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}};
                        });
            if ((f == 2) && (i == numNodes / 2))
            {
                nodeInRAM = fileNode;
            }
        }
        mega::sharedNode_list children = nodeMgr.getChildren(folder.get()); // All children in RAM
        ASSERT_EQ(children.size(), numNodes);
    }
    auto children = nodeMgr.getChildren(folders[0].get()); // None node in RAM
    ASSERT_EQ(children.size(), numNodesForFolder[0]);
}

TEST_F(CacheLRU, getNodeByHandle)
{
    auto rootNode = init(8);
    ASSERT_EQ(numNodesInRam(), 3);

    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);

    uint32_t numNodes = 15;
    std::vector<mega::NodeHandle> handles;
    std::shared_ptr<mega::Node> nodeInRAM;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        auto fileNode = addNode(
            mega::nodetype_t::FILENODE,
            folder,
            true,
            false,
            [this, &handles](mega::Node& file)
            {
                file.size = static_cast<m_off_t>(mIndex);
                file.owner = 88;
                file.ctime = 44;
                std::string name = "name" + std::to_string(mIndex);
                file.attrs.map =
                    std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}, {110, name}};
                handles.push_back(file.nodeHandle());
            });
        if (i == numNodes / 2)
        {
            nodeInRAM = fileNode;
        }
    }
    ASSERT_GT(handles.size(), 1);
    mega::NodeHandle firstNodeHandle = handles.front();
    mega::NodeHandle lasttNodeHandle = handles.front();
    mega::NodeHandle nodeInRAMHandle = nodeInRAM->nodeHandle();

    auto& nodeMgr = mClient->mNodeManager;
    // No Node at RAM => no  at LRU

    // ASSERT_EQ(client->mNodeManager.getNodeInRAM(firstNodeHandle).get(), nullptr);
    std::shared_ptr<mega::Node> node = nodeMgr.getNodeByHandle(firstNodeHandle);

    // Node at RAM and LRU
    auto auxiliarNode = nodeMgr.getNodeByHandle(lasttNodeHandle);
    ASSERT_NE(auxiliarNode, nullptr);
    ASSERT_NE(auxiliarNode->mNodePosition->second.mLRUPosition, nodeMgr.invalidCacheLRUPos());
    node = nodeMgr.getNodeByHandle(lasttNodeHandle);
    ASSERT_EQ(auxiliarNode.get(), node.get());

    // Node at RAM, no at LRU
    // ASSERT_NE(client->mNodeManager.getNodeInRAM(nodeInRAMHandle).get(), nullptr);
    ASSERT_NE(nodeInRAM, nullptr);
    ASSERT_EQ(nodeInRAM->mNodePosition->second.mLRUPosition, nodeMgr.invalidCacheLRUPos());
    node = nodeMgr.getNodeByHandle(nodeInRAMHandle);
    ASSERT_EQ(nodeInRAM.get(), node.get());
}

TEST_F(CacheLRU, childNodeByNameType)
{
    auto rootNode = init(8);
    ASSERT_EQ(numNodesInRam(), 3);
    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);

    uint32_t numNodes = 15;
    std::vector<std::string> names;
    std::shared_ptr<mega::Node> nodeInRAM;
    std::string nameNodeInRam;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        std::string name = "name" + std::to_string(mIndex);
        names.push_back(name);
        auto fileNode = addNode(
            mega::nodetype_t::FILENODE,
            folder,
            true,
            false,
            [this, &name](mega::Node& file)
            {
                file.size = static_cast<m_off_t>(mIndex);
                file.owner = 88;
                file.ctime = 44;
                file.attrs.map =
                    std::map<mega::nameid, std::string>{{101, "foo"}, {102, "bar"}, {110, name}};
            });
        if (i == 1)
        {
            nodeInRAM = fileNode;
            nameNodeInRam = name;
        }
    }

    // Root node + rubbish + vault + node in RAM
    ASSERT_EQ(numNodesInRam(), mLruSize + 4);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(numNodesTotal(), numNodes + 4);

    ASSERT_GT(names.size(), 1);
    auto& nodeMgr = mClient->mNodeManager;
    // No found at LRU
    std::shared_ptr<mega::Node> node = nodeMgr.childNodeByNameType(folder.get(),
                                                                   names.front().c_str(),
                                                                   mega::nodetype_t::FILENODE);
    ASSERT_NE(node, nullptr);

    // No found at LRU but in RAM
    node = nodeMgr.childNodeByNameType(folder.get(),
                                       nameNodeInRam.c_str(),
                                       mega::nodetype_t::FILENODE);
    ASSERT_NE(node, nullptr);

    // Found at LRU
    node =
        nodeMgr.childNodeByNameType(folder.get(), names.back().c_str(), mega::nodetype_t::FILENODE);
    ASSERT_NE(node, nullptr);
}

TEST_F(CacheLRU, reduceCacheLRUSize)
{
    auto rootNode = init(20);
    ASSERT_EQ(numNodesInRam(), 3);
    auto folder = addNode(mega::nodetype_t::FOLDERNODE, rootNode, false, true);

    uint32_t numNodes = mLruSize;
    for (uint32_t i = 0; i < numNodes; i++)
    {
        addNode(mega::nodetype_t::FILENODE,
                folder,
                true,
                false,
                [this](mega::Node& file)
                {
                    file.size = static_cast<m_off_t>(mIndex);
                    file.owner = 88;
                    file.ctime = 44;
                    std::string name = "name" + std::to_string(mIndex);
                    file.attrs.map = std::map<mega::nameid, std::string>{{101, "foo"},
                                                                         {102, "bar"},
                                                                         {110, name}};
                });
    }
    // Root node + rubbish + vault
    ASSERT_EQ(numNodesInRam(), mLruSize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(numNodesTotal(), numNodes + 4);

    setLruMaxSize(8);

    // Root node + rubbish + vault
    ASSERT_EQ(numNodesInRam(), mLruSize + 3);
    // Root node + rubbish + vault + folder
    ASSERT_EQ(numNodesTotal(), numNodes + 4);
}
