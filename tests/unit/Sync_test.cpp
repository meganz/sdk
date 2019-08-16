/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#include <memory>

#include <gtest/gtest.h>

#include <mega/megaclient.h>
#include <mega/megaapp.h>
#include <mega/types.h>
#include <mega/sync.h>

#include "constants.h"
#include "FsNode.h"
#include "DefaultedDirAccess.h"
#include "DefaultedFileAccess.h"
#include "DefaultedFileSystemAccess.h"
#include "utils.h"

namespace {

class MockApp : public mega::MegaApp
{
public:

    bool sync_syncable(mega::Sync*, const char*, std::string* localpath) override
    {
        return mNotSyncablePaths.find(*localpath) == mNotSyncablePaths.end();
    }

    void addNotSyncablePath(std::string path)
    {
        mNotSyncablePaths.insert(std::move(path));
    }

private:
    std::set<std::string> mNotSyncablePaths;
};

class MockFileAccess : public mt::DefaultedFileAccess
{
public:
    explicit MockFileAccess(std::map<std::string, const mt::FsNode*>& fsNodes)
    : mFsNodes{fsNodes}
    {}

    ~MockFileAccess()
    {
        assert(sOpenFileCount <= 2); // Ensure there's not more than two files open at a time
        if (mOpen)
        {
            --sOpenFileCount;
        }
    }

    MEGA_DISABLE_COPY_MOVE(MockFileAccess)

    bool fopen(std::string* path, bool, bool) override
    {
        const auto fsNodePair = mFsNodes.find(*path);
        if (fsNodePair != mFsNodes.end())
        {
            mCurrentFsNode = fsNodePair->second;
            if (!mCurrentFsNode->getOpenable())
            {
                return false;
            }
            fsid = mCurrentFsNode->getFsId();
            fsidvalid = fsid != mega::UNDEF;
            size = mCurrentFsNode->getSize();
            mtime = mCurrentFsNode->getMTime();
            type = mCurrentFsNode->getType();
            mOpen = true;
            ++sOpenFileCount;
            return true;
        }
        else
        {
            return false;
        }
    }

    bool frawread(mega::byte* buffer, unsigned size, m_off_t offset) override
    {
        assert(mOpen);
        assert(mCurrentFsNode);
        if (!mCurrentFsNode->getReadable())
        {
            return false;
        }
        const auto& content = mCurrentFsNode->getContent();
        assert(static_cast<unsigned>(offset) + size <= content.size());
        std::copy(content.begin() + static_cast<unsigned>(offset), content.begin() + static_cast<unsigned>(offset) + size, buffer);
        return true;
    }

private:
    static int sOpenFileCount;
    bool mOpen = false;
    const mt::FsNode* mCurrentFsNode{};
    std::map<std::string, const mt::FsNode*>& mFsNodes;
};

int MockFileAccess::sOpenFileCount{0};

class MockDirAccess : public mt::DefaultedDirAccess
{
public:
    explicit MockDirAccess(std::map<std::string, const mt::FsNode*>& fsNodes)
    : mFsNodes{fsNodes}
    {}

    MEGA_DISABLE_COPY_MOVE(MockDirAccess)

    bool dopen(std::string* path, mega::FileAccess* fa, bool) override
    {
        assert(fa->type == mega::FOLDERNODE);
        const auto fsNodePair = mFsNodes.find(*path);
        if (fsNodePair != mFsNodes.end())
        {
            mCurrentFsNode = fsNodePair->second;
            return mCurrentFsNode->getOpenable();
        }
        else
        {
            return false;
        }
    }

    bool dnext(std::string* localpath, std::string* localname, bool = true, mega::nodetype_t* = NULL) override
    {
        assert(mCurrentFsNode);
        assert(mCurrentFsNode->getPath() == *localpath);
        const auto& children = mCurrentFsNode->getChildren();
        if (mCurrentChildIndex < children.size())
        {
            *localname = children[mCurrentChildIndex]->getName();
            ++mCurrentChildIndex;
            return true;
        }
        else
        {
            mCurrentChildIndex = 0;
            mCurrentFsNode = nullptr;
            return false;
        }
    }

private:
    const mt::FsNode* mCurrentFsNode{};
    std::size_t mCurrentChildIndex{};
    std::map<std::string, const mt::FsNode*>& mFsNodes;
};

class MockFileSystemAccess : public mt::DefaultedFileSystemAccess
{
public:
    explicit MockFileSystemAccess(std::map<std::string, const mt::FsNode*>& fsNodes)
    : mFsNodes{fsNodes}
    {}

    mega::FileAccess* newfileaccess() override
    {
        return new MockFileAccess{mFsNodes};
    }

    mega::DirAccess* newdiraccess() override
    {
        return new MockDirAccess{mFsNodes};
    }

    void local2path(std::string* local, std::string* path) const override
    {
        *path = *local;
    }

private:
    std::map<std::string, const mt::FsNode*>& mFsNodes;
};

struct Fixture
{
    explicit Fixture(std::string localname)
    : mSync{mt::makeSync(std::move(localname), mLocalNodes)}
    {}

    MEGA_DISABLE_COPY_MOVE(Fixture)

    MockApp mApp;
    std::map<std::string, const mt::FsNode*> mFsNodes;
    mega::handlelocalnode_map mLocalNodes;
    MockFileSystemAccess mFsAccess{mFsNodes};
    std::unique_ptr<mega::Sync> mSync;

    bool iteratorsCorrect(mega::LocalNode& l) const
    {
        if (l.fsid_it == mLocalNodes.end())
        {
            return false;
        }
        auto localNodePair = mLocalNodes.find(l.fsid);
        if (l.fsid_it != localNodePair)
        {
            return false;
        }
        if (&l != localNodePair->second)
        {
            return false;
        }
        return true;
    }
};

}

TEST(Sync, isPathSyncable)
{
    ASSERT_TRUE(mega::isPathSyncable("dir/foo", "dir/foo" + mt::gLocalDebris, "/"));
    ASSERT_FALSE(mega::isPathSyncable("dir/foo" + mt::gLocalDebris, "dir/foo" + mt::gLocalDebris, "/"));
    ASSERT_TRUE(mega::isPathSyncable(mt::gLocalDebris + "bar", mt::gLocalDebris, "/"));
    ASSERT_FALSE(mega::isPathSyncable(mt::gLocalDebris + "/", mt::gLocalDebris, "/"));
}

TEST(Sync, invalidateFilesystemIds)
{
    Fixture fx{"d"};

    // Level 0
    mega::LocalNode& d = fx.mSync->localroot;

    // Level 1
    auto d_0 = mt::makeLocalNode(*fx.mSync, d, fx.mLocalNodes, mega::FOLDERNODE, "d_0");
    auto f_0 = mt::makeLocalNode(*fx.mSync, d, fx.mLocalNodes, mega::FILENODE, "f_0");

    size_t count = 0;
    mega::invalidateFilesystemIds(fx.mLocalNodes, d, count);

    ASSERT_EQ(3, count);
    ASSERT_TRUE(fx.mLocalNodes.empty());
    ASSERT_EQ(fx.mLocalNodes.end(), d.fsid_it);
    ASSERT_EQ(fx.mLocalNodes.end(), d_0->fsid_it);
    ASSERT_EQ(fx.mLocalNodes.end(), f_0->fsid_it);
    ASSERT_EQ(mega::UNDEF, d.fsid);
    ASSERT_EQ(mega::UNDEF, d_0->fsid);
    ASSERT_EQ(mega::UNDEF, f_0->fsid);
}

namespace  {

void test_computeReversePathMatchScore(const std::string& sep)
{
    ASSERT_EQ(0, mega::computeReversePathMatchScore("", "", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore("", sep + "a", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(sep + "b", "", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore("a", "b", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore("cc", "cc", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(sep, sep, sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(sep + "b", sep + "a", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(sep + "cc", sep + "cc", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(sep + "b", sep + "b" + sep, sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(sep + "a" + sep + "b", sep + "a" + sep + "b", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(sep + "a" + sep + "c" + sep + "a" + sep + "b", sep + "a" + sep + "b", sep));
    ASSERT_EQ(3, mega::computeReversePathMatchScore(sep + "aaa" + sep + "bbbb" + sep + "ccc", sep + "aaa" + sep + "bbb" + sep + "ccc", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore("a" + sep + "b", "a" + sep + "b", sep));
    const std::string base = sep + "a" + sep + "b";
    const std::string reference = sep + "c12" + sep + "e34";
    ASSERT_EQ(6, mega::computeReversePathMatchScore(base + reference, base + sep + "a65" + reference, sep));
    ASSERT_EQ(6, mega::computeReversePathMatchScore(base + reference, base + sep + ".debris" + reference, sep));
    ASSERT_EQ(6, mega::computeReversePathMatchScore(base + reference, base + sep + "ab" + reference, sep));
}

}

TEST(Sync, computeReverseMatchScore_oneByteSeparator)
{
    test_computeReversePathMatchScore("/");
}

TEST(Sync, computeReverseMatchScore_twoByteSeparator)
{
    test_computeReversePathMatchScore("//");
}

TEST(Sync, assignFilesystemIds_whenFilesystemFingerprintsMatchLocalNodes)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0");
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_1");
    mt::FsNode f_2{&d, mega::FILENODE, "f_2"};
    auto lf_2 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_2", f_2.getFingerprint());

    // Level 2
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    auto lf_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FILENODE, "f_1_0", f_1_0.getFingerprint());
    mt::FsNode d_1_1{&d_1, mega::FOLDERNODE, "d_1_1"};
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FOLDERNODE, "d_1_1");

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, fx.mLocalNodes, mega::FILENODE, "f_1_1_0", f_1_1_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(d.getFsId(), ld.fsid);
    ASSERT_EQ(d_0.getFsId(), ld_0->fsid);
    ASSERT_EQ(d_1.getFsId(), ld_1->fsid);
    ASSERT_EQ(d_1_1.getFsId(), ld_1_1->fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_2.getFsId(), lf_2->fsid);
    ASSERT_EQ(f_0_0.getFsId(), lf_0_0->fsid);
    ASSERT_EQ(f_0_1.getFsId(), lf_0_1->fsid);
    ASSERT_EQ(f_1_0.getFsId(), lf_1_0->fsid);
    ASSERT_EQ(f_1_1_0.getFsId(), lf_1_1_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 9;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_TRUE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_1_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_2));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_1_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_1_1_0));
}

TEST(Sync, assignFilesystemIds_whenNoLocalNodesMatchFilesystemFingerprints)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0");
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_1");
    mt::FsNode f_2{&d, mega::FILENODE, "f_2"};
    auto lf_2 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_2");

    // Level 2
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_0");
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_1");
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    auto lf_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FILENODE, "f_1_0");
    mt::FsNode d_1_1{&d_1, mega::FOLDERNODE, "d_1_1"};
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FOLDERNODE, "d_1_1");

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, fx.mLocalNodes, mega::FILENODE, "f_1_1_0");

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that files and directories have invalid fs IDs (no fingerprint matches)
    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(mega::UNDEF, ld_0->fsid);
    ASSERT_EQ(mega::UNDEF, ld_1->fsid);
    ASSERT_EQ(mega::UNDEF, ld_1_1->fsid);
    ASSERT_EQ(mega::UNDEF, lf_2->fsid);
    ASSERT_EQ(mega::UNDEF, lf_0_0->fsid);
    ASSERT_EQ(mega::UNDEF, lf_0_1->fsid);
    ASSERT_EQ(mega::UNDEF, lf_1_0->fsid);
    ASSERT_EQ(mega::UNDEF, lf_1_1_0->fsid);
}

TEST(Sync, assignFilesystemIds_whenTwoLocalNodesHaveSameFingerprint)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0");
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_1");
    mt::FsNode f_2{&d, mega::FILENODE, "f_2"};
    auto lf_2 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_2", f_2.getFingerprint());

    // Level 2
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    auto lf_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FILENODE, "f_1_0", f_1_0.getFingerprint());
    mt::FsNode d_1_1{&d_1, mega::FOLDERNODE, "d_1_1"};
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FOLDERNODE, "d_1_1");

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    f_1_1_0.assignContentFrom(f_1_0);
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, fx.mLocalNodes, mega::FILENODE, "f_1_1_0", f_1_1_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(d.getFsId(), ld.fsid);
    ASSERT_EQ(d_0.getFsId(), ld_0->fsid);
    ASSERT_EQ(d_1.getFsId(), ld_1->fsid);
    ASSERT_EQ(d_1_1.getFsId(), ld_1_1->fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_2.getFsId(), lf_2->fsid);
    ASSERT_EQ(f_0_0.getFsId(), lf_0_0->fsid);
    ASSERT_EQ(f_0_1.getFsId(), lf_0_1->fsid);
    ASSERT_EQ(f_1_0.getFsId(), lf_1_0->fsid);
    ASSERT_EQ(f_1_1_0.getFsId(), lf_1_1_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 9;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_TRUE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_1_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_2));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_1_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_1_1_0));
}

TEST(Sync, assignFilesystemIds_whenSomeFsIdIsNotValid)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    f_0.setFsId(mega::UNDEF);
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(d.getFsId(), ld.fsid);

    // file node must have undef fs ID
    ASSERT_EQ(mega::UNDEF, lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 1;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_TRUE(fx.iteratorsCorrect(ld));
    ASSERT_FALSE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_whenSomeFileCannotBeOpened)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    f_0.setOpenable(false);
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_FALSE(success);
}

TEST(Sync, assignFilesystemIds_whenRootDirCannotBeOpened)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    d.setOpenable(false);
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_FALSE(success);
}

TEST(Sync, assignFilesystemIds_whenSubDirCannotBeOpened)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    d_0.setOpenable(false);
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0");

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that directories have invalid fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(mega::UNDEF, ld_0->fsid);

    // check file nodes
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 1;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
    ASSERT_FALSE(fx.iteratorsCorrect(*ld_0));
}

TEST(Sync, assignFilesystemIds_whenSomeFingerprintIsNotValid)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    f_0.setReadable(false);
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // all invalid
    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(mega::UNDEF, lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 0;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());
}

TEST(Sync, assignFilesystemIds_whenPathIsNotSyncableThroughApp)
{
    Fixture fx{"d"};
    fx.mApp.addNotSyncablePath("d/f_1");

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode f_1{&d, mega::FILENODE, "f_1"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    ASSERT_EQ(d.getFsId(), ld.fsid);
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    constexpr std::size_t fileCount = 2;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_TRUE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_whenDebrisIsPartOfFiles)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, mt::gLocalDebris};

    // Level 2
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(d.getFsId(), ld.fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 2;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_TRUE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_preferredPathMatchAssignsFinalFsId)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};

    // the local node for f_1_0 is still at level 1 but the file moved to level 2 under a new folder (d_1)
    auto lf_1 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_1_0", f_0.getFingerprint());

    // Level 2
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    f_1_0.assignContentFrom(f_0);

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);
    ASSERT_EQ(f_1_0.getFsId(), lf_1->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 2;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_1));
}

TEST(Sync, assignFilesystemIds_whenFolderWasMoved)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode d_0_renamed{&d, mega::FOLDERNODE, "d_0_renamed"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0");

    // Level 2
    mt::FsNode f_0_0{&d_0_renamed, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0_renamed, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(d_0_renamed.getFsId(), ld_0->fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_0_0.getFsId(), lf_0_0->fsid);
    ASSERT_EQ(f_0_1.getFsId(), lf_0_1->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 3;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_1));
}

#ifdef NDEBUG
TEST(Sync, assignFilesystemIds_whenRootPathIsNotAFolder_hittingAssert)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FILENODE, "d"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_FALSE(success);
}
#endif

#ifdef NDEBUG
TEST(Sync, assignFilesystemIds_whenFileTypeIsUnexpected_hittingAssert)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = fx.mSync->localroot;

    // Level 1
    mt::FsNode f_0{&d, mega::TYPE_UNKNOWN, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, mt::gLocalDebris, "/", true);

    ASSERT_FALSE(success);
}
#endif
