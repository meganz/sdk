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
        mPath = *path;
        return sysopen();
    }

    bool sysstat(mega::m_time_t* curr_mtime, m_off_t* curr_size) override
    {
        *curr_mtime = mtime;
        *curr_size = size;
        return true;
    }

    bool sysopen(bool async = false) override
    {
        const auto fsNodePair = mFsNodes.find(mPath);
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

    bool sysread(mega::byte* buffer, unsigned size, m_off_t offset) override
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

    void sysclose() override
    {}

private:
    static int sOpenFileCount;
    std::string mPath;
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

    std::unique_ptr<mega::FileAccess> newfileaccess(bool) override
    {
        return std::unique_ptr<mega::FileAccess>{new MockFileAccess{mFsNodes}};
    }

    mega::DirAccess* newdiraccess() override
    {
        return new MockDirAccess{mFsNodes};
    }

    void local2path(std::string* local, std::string* path) const override
    {
        *path = *local;
    }

    size_t lastpartlocal(std::string* localname) const override
    {
        const char* ptr = localname->data();
        if ((ptr = strrchr(ptr, '/')))
        {
            return ptr - localname->data() + 1;
        }
        return 0;
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

namespace  {

void test_computeReversePathMatchScore(const std::string& sep)
{
    std::string acc;
    ASSERT_EQ(0, mega::computeReversePathMatchScore(acc, "", "", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(acc, "", sep + "a", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(acc, sep + "b", "", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(acc, "a", "b", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(acc, "cc", "cc", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(acc, sep, sep, sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(acc, sep + "b", sep + "a", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(acc, sep + "cc", sep + "cc", sep));
    ASSERT_EQ(0, mega::computeReversePathMatchScore(acc, sep + "b", sep + "b" + sep, sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(acc, sep + "a" + sep + "b", sep + "a" + sep + "b", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(acc, sep + "a" + sep + "c" + sep + "a" + sep + "b", sep + "a" + sep + "b", sep));
    ASSERT_EQ(3, mega::computeReversePathMatchScore(acc, sep + "aaa" + sep + "bbbb" + sep + "ccc", sep + "aaa" + sep + "bbb" + sep + "ccc", sep));
    ASSERT_EQ(2, mega::computeReversePathMatchScore(acc, "a" + sep + "b", "a" + sep + "b", sep));
    const std::string base = sep + "a" + sep + "b";
    const std::string reference = sep + "c12" + sep + "e34";
    ASSERT_EQ(6, mega::computeReversePathMatchScore(acc, base + reference, base + sep + "a65" + reference, sep));
    ASSERT_EQ(6, mega::computeReversePathMatchScore(acc, base + reference, base + sep + ".debris" + reference, sep));
    ASSERT_EQ(6, mega::computeReversePathMatchScore(acc, base + reference, base + sep + "ab" + reference, sep));
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
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0", d_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_1", d_1.getFingerprint());
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
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FOLDERNODE, "d_1_1", d_1_1.getFingerprint());

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, fx.mLocalNodes, mega::FILENODE, "f_1_1_0", f_1_1_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);
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
    constexpr std::size_t fileCount = 8;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*ld_1_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_2));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_1));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_1_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_1_1_0));
}

TEST(Sync, assignFilesystemIds_whenFilesystemFingerprintsMatchLocalNodes_oppositeDeclarationOrder)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0", d_0.getFingerprint());

    // Level 2

    // reverse order of declaration should still lead to same results (files vs localnodes)
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(d_0.getFsId(), ld_0->fsid);

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

TEST(Sync, assignFilesystemIds_whenNoLocalNodesMatchFilesystemFingerprints)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;

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

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

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
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0", d_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_1", d_1.getFingerprint());
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
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, fx.mLocalNodes, mega::FOLDERNODE, "d_1_1", d_1_1.getFingerprint());

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    f_1_1_0.assignContentFrom(f_1_0);
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, fx.mLocalNodes, mega::FILENODE, "f_1_1_0", f_1_1_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);
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
    constexpr std::size_t fileCount = 8;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
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
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    f_0.setFsId(mega::UNDEF);
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_FALSE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);

    // file node must have undef fs ID
    ASSERT_EQ(mega::UNDEF, lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 0;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_FALSE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_whenSomeFileCannotBeOpened)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    f_0.setOpenable(false);
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_FALSE(success);
}

TEST(Sync, assignFilesystemIds_whenRootDirCannotBeOpened)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    d.setOpenable(false);
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_FALSE(success);
}

TEST(Sync, assignFilesystemIds_whenSubDirCannotBeOpened)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    d_0.setOpenable(false);
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0", d_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_FALSE(success);

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

TEST(Sync, assignFilesystemIds_forSingleFile)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 1;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_whenPathIsNotSyncableThroughApp)
{
    Fixture fx{"d"};
    fx.mApp.addNotSyncablePath("d/f_1");

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode f_1{&d, mega::FILENODE, "f_1"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    constexpr std::size_t fileCount = 1;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_whenDebrisIsPartOfFiles)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, mt::gLocalDebris};

    // Level 2
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 1;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_preferredPathMatchAssignsFinalFsId)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

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

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

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

TEST(Sync, assignFilesystemIds_whenFolderWasMoved_differentLeafName)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0_renamed{&d, mega::FOLDERNODE, "d_0_renamed"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0", d_0_renamed.getFingerprint());

    // Level 2
    mt::FsNode f_0_0{&d_0_renamed, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0_renamed, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(mega::UNDEF, ld_0->fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_0_0.getFsId(), lf_0_0->fsid);
    ASSERT_EQ(f_0_1.getFsId(), lf_0_1->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 2;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_FALSE(fx.iteratorsCorrect(*ld_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_0));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0_1));
}

TEST(Sync, assignFilesystemIds_whenFolderWasMoved_sameLeafName)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0_renamed{&d, mega::FOLDERNODE, "d_0_renamed"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0", d_0_renamed.getFingerprint());

    // Level 2
    mt::FsNode d_0{&d_0_renamed, mega::FOLDERNODE, "d_0"};

    // Level 3
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, fx.mLocalNodes, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(d_0.getFsId(), ld_0->fsid);

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

TEST(Sync, assignFilesystemIds_whenFileWasCopied)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};

    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    // Level 2
    mt::FsNode f_1{&d_0, mega::FILENODE, "f_0"}; // same name as `f_0`
    f_1.assignContentFrom(f_0); // file was copied maintaining mtime

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    // assert that directories have correct fs IDs
    ASSERT_EQ(mega::UNDEF, ld.fsid);

    // assert that all file `LocalNode`s have same fs IDs as the corresponding `FsNode`s
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 1;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_whenFileWasMoved_differentLeafName)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0_renamed"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(mega::UNDEF, lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 0;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_FALSE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_whenFileWasMoved_sameLeafName)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};

    // Level 2
    mt::FsNode f_0{&d_0, mega::FILENODE, "f_0"};

    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint()); // still at level 1

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(f_0.getFsId(), lf_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 1;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_TRUE(fx.iteratorsCorrect(*lf_0));
}

TEST(Sync, assignFilesystemIds_emptyFolderStaysUnassigned)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FOLDERNODE, "d_0");

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_TRUE(success);

    ASSERT_EQ(mega::UNDEF, ld.fsid);
    ASSERT_EQ(mega::UNDEF, ld_0->fsid);

    // assert that the local node map is correct
    constexpr std::size_t fileCount = 0;
    ASSERT_EQ(fileCount, fx.mLocalNodes.size());

    ASSERT_FALSE(fx.iteratorsCorrect(ld));
    ASSERT_FALSE(fx.iteratorsCorrect(*ld_0));
}

#ifdef NDEBUG
TEST(Sync, assignFilesystemIds_whenRootPathIsNotAFolder_hittingAssert)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FILENODE, "d"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_FALSE(success);
}
#endif

#ifdef NDEBUG
TEST(Sync, assignFilesystemIds_whenFileTypeIsUnexpected_hittingAssert)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::TYPE_UNKNOWN, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, fx.mLocalNodes, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, "d/" + mt::gLocalDebris, "/");

    ASSERT_FALSE(success);
}
#endif
