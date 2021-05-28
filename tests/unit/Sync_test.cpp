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
#include <numeric>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mega/megaclient.h>
#include <mega/megaapp.h>
#include <mega/types.h>
#include <mega/heartbeats.h>
#include <mega/sync.h>
#include <mega/filesystem.h>

#include "constants.h"
#include "FsNode.h"
#include "DefaultedDbTable.h"
#include "DefaultedDirAccess.h"
#include "DefaultedFileAccess.h"
#include "DefaultedFileSystemAccess.h"
#include "utils.h"

#ifdef ENABLE_SYNC

namespace {

class MockApp : public mega::MegaApp
{
public:

    bool sync_syncable(mega::Sync*, const char*, mega::LocalPath& localpath) override
    {
        return mNotSyncablePaths.find(localpath) == mNotSyncablePaths.end();
    }

    bool sync_syncable(mega::Sync*, const char*, mega::LocalPath& localpath, mega::Node*) override
    {
        return mNotSyncablePaths.find(localpath) == mNotSyncablePaths.end();
    }

    void addNotSyncablePath(const mega::LocalPath& path)
    {
        mNotSyncablePaths.insert(path);
    }

private:
    std::set<mega::LocalPath> mNotSyncablePaths;
};

class MockFileAccess : public mt::DefaultedFileAccess
{
public:
    explicit MockFileAccess(std::map<mega::LocalPath, const mt::FsNode*>& fsNodes)
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

    bool fopen(mega::LocalPath& path, bool, bool, mega::DirAccess* iteratingDir, bool) override
    {
        mPath = path;
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
    mega::LocalPath mPath;
    bool mOpen = false;
    const mt::FsNode* mCurrentFsNode{};
    std::map<mega::LocalPath, const mt::FsNode*>& mFsNodes;
};

int MockFileAccess::sOpenFileCount{0};

class MockDirAccess : public mt::DefaultedDirAccess
{
public:
    explicit MockDirAccess(std::map<mega::LocalPath, const mt::FsNode*>& fsNodes)
    : mFsNodes{fsNodes}
    {}

    MEGA_DISABLE_COPY_MOVE(MockDirAccess)

    bool dopen(mega::LocalPath* path, mega::FileAccess* fa, bool) override
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

    bool dnext(mega::LocalPath& localpath, mega::LocalPath& localname, bool = true, mega::nodetype_t* = NULL) override
    {
        assert(mCurrentFsNode);
        assert(mCurrentFsNode->getPath() == localpath);
        const auto& children = mCurrentFsNode->getChildren();
        if (mCurrentChildIndex < children.size())
        {
            localname = children[mCurrentChildIndex]->getName();
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
    std::map<mega::LocalPath, const mt::FsNode*>& mFsNodes;
};

class MockFileSystemAccess : public mt::DefaultedFileSystemAccess
{
public:
    explicit MockFileSystemAccess(std::map<mega::LocalPath, const mt::FsNode*>& fsNodes)
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

    void local2path(const std::string* local, std::string* path) const override
    {
        *path = *local;
    }

    void path2local(const std::string* local, std::string* path) const override
    {
        *path = *local;
    }

    bool getsname(const mega::LocalPath&, mega::LocalPath&) const override
    {
        return false;
    }

private:
    std::map<mega::LocalPath, const mt::FsNode*>& mFsNodes;
};

struct Fixture
{
    explicit Fixture(std::string localname)
    : mUnifiedSync{mt::makeSync(*mClient, std::move(localname))}
    , mSync{mUnifiedSync->mSync}
    {}

    MEGA_DISABLE_COPY_MOVE(Fixture)

    MockApp mApp;
    std::map<mega::LocalPath, const mt::FsNode*> mFsNodes;
    MockFileSystemAccess mFsAccess{mFsNodes};
    std::shared_ptr<mega::MegaClient> mClient = mt::makeClient(mApp, mFsAccess);
    mega::handlelocalnode_map& mLocalNodes = mClient->fsidnode;
    std::unique_ptr<mega::UnifiedSync> mUnifiedSync;
    std::unique_ptr<mega::Sync>& mSync;

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

namespace {

using mega::LocalPath;
using std::string;

/*
 * Shim to make following test less painful.
 */
int computeReversePathMatchScore(const string& path1,
                                 const string& path2)
{
#if defined(_WIN32)
    mega::WinFileSystemAccess wfa;
    auto localpath1 = LocalPath::fromPath(path1, wfa);
    auto localpath2 = LocalPath::fromPath(path2, wfa);

    return mega::computeReversePathMatchScore(localpath1,
                                              localpath2,
                                              mt::DefaultedFileSystemAccess());
#else
    return mega::computeReversePathMatchScore(
        LocalPath::fromPlatformEncoded(path1),
        LocalPath::fromPlatformEncoded(path2),
        mt::DefaultedFileSystemAccess());

#endif
}

void test_computeReversePathMatchScore()
{
    string sepstr;
    sepstr.push_back(LocalPath::localPathSeparator);
    ASSERT_EQ(0, computeReversePathMatchScore("", ""));
    ASSERT_EQ(0, computeReversePathMatchScore("", sepstr + "a"));
    ASSERT_EQ(0, computeReversePathMatchScore(sepstr + "b", ""));
    ASSERT_EQ(0, computeReversePathMatchScore("a", "b"));
    ASSERT_EQ(2, computeReversePathMatchScore("cc", "cc"));
    ASSERT_EQ(0, computeReversePathMatchScore(sepstr, sepstr));
    ASSERT_EQ(0, computeReversePathMatchScore(sepstr + "b", sepstr + "a"));
    ASSERT_EQ(2, computeReversePathMatchScore(sepstr + "cc", sepstr + "cc"));
    ASSERT_EQ(0, computeReversePathMatchScore(sepstr + "b", sepstr + "b" + sepstr));
    ASSERT_EQ(2, computeReversePathMatchScore(sepstr + "a" + sepstr + "b", sepstr + "a" + sepstr + "b"));
    ASSERT_EQ(2, computeReversePathMatchScore(sepstr + "a" + sepstr + "c" + sepstr + "a" + sepstr + "b", sepstr + "a" + sepstr + "b"));
    ASSERT_EQ(3, computeReversePathMatchScore(sepstr + "aaa" + sepstr + "bbbb" + sepstr + "ccc", sepstr + "aaa" + sepstr + "bbb" + sepstr + "ccc"));
    ASSERT_EQ(2, computeReversePathMatchScore("a" + sepstr + "b", "a" + sepstr + "b"));

    const string base = sepstr + "a" + sepstr + "b";
    const string reference = sepstr + "c12" + sepstr + "e34";

    ASSERT_EQ(6, computeReversePathMatchScore(base + reference, base + sepstr + "a65" + reference));
    ASSERT_EQ(6, computeReversePathMatchScore(base + reference, base + sepstr + ".debris" + reference));
    ASSERT_EQ(6, computeReversePathMatchScore(base + reference, base + sepstr + "ab" + reference));
}

}

TEST(Sync, computeReverseMatchScore_oneByteSeparator)
{
    test_computeReversePathMatchScore();
}

/*TEST(Sync, assignFilesystemIds_whenFilesystemFingerprintsMatchLocalNodes)
{
    Fixture fx{"d"};

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0", d_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_1", d_1.getFingerprint());
    mt::FsNode f_2{&d, mega::FILENODE, "f_2"};
    auto lf_2 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_2", f_2.getFingerprint());

    // Level 2
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    auto lf_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1, mega::FILENODE, "f_1_0", f_1_0.getFingerprint());
    mt::FsNode d_1_1{&d_1, mega::FOLDERNODE, "d_1_1"};
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, mega::FOLDERNODE, "d_1_1", d_1_1.getFingerprint());

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, mega::FILENODE, "f_1_1_0", f_1_1_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0", d_0.getFingerprint());

    // Level 2

    // reverse order of declaration should still lead to same results (files vs localnodes)
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0");
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_1");
    mt::FsNode f_2{&d, mega::FILENODE, "f_2"};
    auto lf_2 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_2");

    // Level 2
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_0");
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_1");
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    auto lf_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1, mega::FILENODE, "f_1_0");
    mt::FsNode d_1_1{&d_1, mega::FOLDERNODE, "d_1_1"};
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, mega::FOLDERNODE, "d_1_1");

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, mega::FILENODE, "f_1_1_0");

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0", d_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};
    auto ld_1 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_1", d_1.getFingerprint());
    mt::FsNode f_2{&d, mega::FILENODE, "f_2"};
    auto lf_2 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_2", f_2.getFingerprint());

    // Level 2
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    auto lf_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1, mega::FILENODE, "f_1_0", f_1_0.getFingerprint());
    mt::FsNode d_1_1{&d_1, mega::FOLDERNODE, "d_1_1"};
    auto ld_1_1 = mt::makeLocalNode(*fx.mSync, *ld_1, mega::FOLDERNODE, "d_1_1", d_1_1.getFingerprint());

    // Level 3
    mt::FsNode f_1_1_0{&d_1_1, mega::FILENODE, "f_1_1_0"};
    f_1_1_0.assignContentFrom(f_1_0);
    auto lf_1_1_0 = mt::makeLocalNode(*fx.mSync, *ld_1_1, mega::FILENODE, "f_1_1_0", f_1_1_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_0{&d, mega::FOLDERNODE, "d_0"};
    d_0.setOpenable(false);
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0", d_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    fx.mApp.addNotSyncablePath(LocalPath::fromPath("d/f_1", fx.mFsAccess));

    // Level 0
    mt::FsNode d{nullptr, mega::FOLDERNODE, "d"};
    mega::LocalNode& ld = *fx.mSync->localroot;
    static_cast<mega::FileFingerprint&>(ld) = d.getFingerprint();

    // Level 1
    mt::FsNode f_0{&d, mega::FILENODE, "f_0"};
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode f_1{&d, mega::FILENODE, "f_1"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, mt::gLocalDebris};

    // Level 2
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());
    mt::FsNode d_1{&d, mega::FOLDERNODE, "d_1"};

    // the local node for f_1_0 is still at level 1 but the file moved to level 2 under a new folder (d_1)
    auto lf_1 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_1_0", f_0.getFingerprint());

    // Level 2
    mt::FsNode f_1_0{&d_1, mega::FILENODE, "f_1_0"};
    f_1_0.assignContentFrom(f_0);

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0", d_0_renamed.getFingerprint());

    // Level 2
    mt::FsNode f_0_0{&d_0_renamed, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0_renamed, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0", d_0_renamed.getFingerprint());

    // Level 2
    mt::FsNode d_0{&d_0_renamed, mega::FOLDERNODE, "d_0"};

    // Level 3
    mt::FsNode f_0_0{&d_0, mega::FILENODE, "f_0_0"};
    auto lf_0_0 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_0", f_0_0.getFingerprint());
    mt::FsNode f_0_1{&d_0, mega::FILENODE, "f_0_1"};
    auto lf_0_1 = mt::makeLocalNode(*fx.mSync, *ld_0, mega::FILENODE, "f_0_1", f_0_1.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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

    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());

    // Level 2
    mt::FsNode f_1{&d_0, mega::FILENODE, "f_0"}; // same name as `f_0`
    f_1.assignContentFrom(f_0); // file was copied maintaining mtime

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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

    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint()); // still at level 1

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto ld_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FOLDERNODE, "d_0");

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

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
    auto lf_0 = mt::makeLocalNode(*fx.mSync, ld, mega::FILENODE, "f_0", f_0.getFingerprint());

    mt::collectAllFsNodes(fx.mFsNodes, d);

    auto localdebris = LocalPath::fromPath("d/" + mt::gLocalDebris, fx.mFsAccess);
    const auto success = mega::assignFilesystemIds(*fx.mSync, fx.mApp, fx.mFsAccess, fx.mLocalNodes, localdebris);

    ASSERT_FALSE(success);
}
#endif
*/



namespace mega {
    enum { TYPE_TWOWAY = SyncConfig::TYPE_TWOWAY };
    enum { TYPE_UP = SyncConfig::TYPE_UP };
    enum { TYPE_DOWN = SyncConfig::TYPE_DOWN };
};

/*
TEST(Sync, SyncConfig_defaultOptions)
{
    const mega::SyncConfig config{"foo", "foo", 42, "remote",123};
    ASSERT_TRUE(config.getEnabled());
    ASSERT_EQ("foo", config.getLocalPath());
    ASSERT_EQ(42, config.getRemoteNode());
    ASSERT_EQ(123, config.getLocalFingerprint());
    ASSERT_TRUE(config.getRegExps().empty());
    ASSERT_EQ(mega::TYPE_TWOWAY, config.getType());
    ASSERT_TRUE(config.isUpSync());
    ASSERT_TRUE(config.isDownSync());
    ASSERT_TRUE(config.syncDeletions());
    ASSERT_FALSE(config.forceOverwrite());
    test_SyncConfig_serialization(config);
}

TEST(Sync, SyncConfig_defaultOptions_inactive)
{
    mega::SyncConfig config{"foo", "foo", 42, "remote",123};
    config.setEnabled(false);
    ASSERT_FALSE(config.getEnabled());
    ASSERT_EQ("foo", config.getLocalPath());
    ASSERT_EQ(42, config.getRemoteNode());
    ASSERT_EQ(123, config.getLocalFingerprint());
    ASSERT_TRUE(config.getRegExps().empty());
    ASSERT_EQ(mega::TYPE_TWOWAY, config.getType());
    ASSERT_TRUE(config.isUpSync());
    ASSERT_TRUE(config.isDownSync());
    ASSERT_TRUE(config.syncDeletions());
    ASSERT_FALSE(config.forceOverwrite());
    test_SyncConfig_serialization(config);
}

TEST(Sync, SyncConfig_defaultOptions_butWithRegExps)
{
    const std::vector<std::string> regExps{"aa", "bbb"};
    const mega::SyncConfig config{"foo", "foo", 42, "remote",123, regExps};
    ASSERT_TRUE(config.getEnabled());
    ASSERT_EQ("foo", config.getLocalPath());
    ASSERT_EQ(42, config.getRemoteNode());
    ASSERT_EQ(123, config.getLocalFingerprint());
    ASSERT_EQ(regExps, config.getRegExps());
    ASSERT_EQ(mega::TYPE_TWOWAY, config.getType());
    ASSERT_TRUE(config.isUpSync());
    ASSERT_TRUE(config.isDownSync());
    ASSERT_TRUE(config.syncDeletions());
    ASSERT_FALSE(config.forceOverwrite());
    test_SyncConfig_serialization(config);
}

TEST(Sync, SyncConfig_upSync_syncDelFalse_overwriteFalse)
{
    const std::vector<std::string> regExps{"aa", "bbb"};
    const mega::SyncConfig config{"foo", "foo", 42, "remote",123, regExps, true, mega::SyncConfig::TYPE_UP};
    ASSERT_TRUE(config.getEnabled());
    ASSERT_EQ("foo", config.getLocalPath());
    ASSERT_EQ(42, config.getRemoteNode());
    ASSERT_EQ(123, config.getLocalFingerprint());
    ASSERT_EQ(regExps, config.getRegExps());
    ASSERT_EQ(mega::TYPE_UP, config.getType());
    ASSERT_TRUE(config.isUpSync());
    ASSERT_FALSE(config.isDownSync());
    ASSERT_FALSE(config.syncDeletions());
    ASSERT_FALSE(config.forceOverwrite());
    test_SyncConfig_serialization(config);
}

TEST(Sync, SyncConfig_upSync_syncDelTrue_overwriteTrue)
{
    const std::vector<std::string> regExps{"aa", "bbb"};
    const mega::SyncConfig config{"foo", "foo", 42, "remote",123, regExps, true, mega::SyncConfig::TYPE_UP, true, true};
    ASSERT_TRUE(config.getEnabled());
    ASSERT_EQ("foo", config.getLocalPath());
    ASSERT_EQ(42, config.getRemoteNode());
    ASSERT_EQ(123, config.getLocalFingerprint());
    ASSERT_EQ(regExps, config.getRegExps());
    ASSERT_EQ(mega::TYPE_UP, config.getType());
    ASSERT_TRUE(config.isUpSync());
    ASSERT_FALSE(config.isDownSync());
    ASSERT_TRUE(config.syncDeletions());
    ASSERT_TRUE(config.forceOverwrite());
    test_SyncConfig_serialization(config);
}

TEST(Sync, SyncConfig_downSync_syncDelFalse_overwriteFalse)
{
    const std::vector<std::string> regExps{"aa", "bbb"};
    const mega::SyncConfig config{"foo", "foo", 42, "remote",123, regExps, true, mega::SyncConfig::TYPE_DOWN};
    ASSERT_TRUE(config.getEnabled());
    ASSERT_EQ("foo", config.getLocalPath());
    ASSERT_EQ(42, config.getRemoteNode());
    ASSERT_EQ(123, config.getLocalFingerprint());
    ASSERT_EQ(regExps, config.getRegExps());
    ASSERT_EQ(mega::TYPE_DOWN, config.getType());
    ASSERT_FALSE(config.isUpSync());
    ASSERT_TRUE(config.isDownSync());
    ASSERT_FALSE(config.syncDeletions());
    ASSERT_FALSE(config.forceOverwrite());
    test_SyncConfig_serialization(config);
}

TEST(Sync, SyncConfig_downSync_syncDelTrue_overwriteTrue)
{
    const std::vector<std::string> regExps{"aa", "bbb"};
    const mega::SyncConfig config{"foo", "foo", 42, "remote",123, regExps, true, mega::SyncConfig::TYPE_DOWN, true, true};
    ASSERT_TRUE(config.getEnabled());
    ASSERT_EQ("foo", config.getLocalPath());
    ASSERT_EQ(42, config.getRemoteNode());
    ASSERT_EQ(123, config.getLocalFingerprint());
    ASSERT_EQ(regExps, config.getRegExps());
    ASSERT_EQ(mega::SyncConfig::TYPE_DOWN, config.getType());
    ASSERT_FALSE(config.isUpSync());
    ASSERT_TRUE(config.isDownSync());
    ASSERT_TRUE(config.syncDeletions());
    ASSERT_TRUE(config.forceOverwrite());
    test_SyncConfig_serialization(config);
}
*/

#if 0
namespace
{

class MockDbTable : public mt::DefaultedDbTable
{
public:
    using mt::DefaultedDbTable::DefaultedDbTable;
    void rewind() override
    {
        mIndex = 0;
    }
    bool next(uint32_t* id, std::string* data) override
    {
        if (mIndex >= mData->size())
        {
            return false;
        }
        *id = (*mData)[mIndex].first;
        *data = (*mData)[mIndex].second;
        ++mIndex;
        return true;
    }
    bool put(uint32_t id, char* data, unsigned size) override
    {
        del(id);
        mData->emplace_back(id, std::string{data, size});
        return true;
    }
    bool del(uint32_t id) override
    {
        mData->erase(std::remove_if(mData->begin(), mData->end(),
                                   [id](const std::pair<uint32_t, std::string>& p)
                                   {
                                       return p.first == id;
                                   }),
                     mData->end());
        return true;
    }
    void truncate() override
    {
        mData->clear();
    }

    bool inTransaction() const override
    {
        return true;
    }

    std::vector<std::pair<uint32_t, std::string>>* mData = nullptr;

private:
    size_t mIndex = 0;
};

class MockDbAccess : public mega::DbAccess
{
public:
    MockDbAccess(std::vector<std::pair<uint32_t, std::string>>& data)
        : mData{data}
    {}
    mega::DbTable* open(mega::PrnGen &rng, mega::FileSystemAccess&, const std::string&, const int flags) override
    {
        auto table = new MockDbTable{rng, (flags & mega::DB_OPEN_FLAG_TRANSACTED) > 0};
        table->mData = &mData;
        return table;
    }

    bool probe(mega::FileSystemAccess&, const string&) const override
    {
        return true;
    }

    const mega::LocalPath &rootPath() const override
    {
        static mega::LocalPath const dummy;

        return dummy;
    }

private:
    std::vector<std::pair<uint32_t, std::string>>& mData;
};

}
#endif


namespace SyncConfigTests
{

using namespace mega;
using namespace testing;

class Directory
{
public:
    Directory(FSACCESS_CLASS& fsAccess, const LocalPath& path)
      : mFSAccess(fsAccess)
      , mPath(path)
    {
        mFSAccess.mkdirlocal(mPath, false);
    }

    ~Directory()
    {
        mFSAccess.emptydirlocal(mPath);
        mFSAccess.rmdirlocal(mPath);
    }

    MEGA_DISABLE_COPY_MOVE(Directory);

    operator const LocalPath&() const
    {
        return mPath;
    }

    const LocalPath& path() const
    {
        return mPath;
    }

private:
    FSACCESS_CLASS& mFSAccess;
    LocalPath mPath;
}; // Directory

// Temporary shims so that we can easily switch to using
// NiceMock / FakeStrictMock when GMock/GTest is upgraded on Jenkins.
#if 0

template<typename MockClass>
class FakeNiceMock
    : public MockClass
{
public:
        using MockClass::MockClass;
}; // FakeNiceMock<T>

template<typename MockClass>
class FakeStrictMock
    : public MockClass
{
public:
        using MockClass::MockClass;
}; // FakeStrictMock<T>

#else

template<typename T>
using FakeNiceMock = NiceMock<T>;

template<typename T>
using FakeStrictMock = StrictMock<T>;

#endif

class Utilities
{
public:
    static string randomBase64(const size_t n = 16)
    {
        return Base64::btoa(randomBytes(n));
    }

    static string randomBytes(const size_t n)
    {
        return mRNG.genstring(n);
    }

    static bool randomFile(LocalPath path, const size_t n = 64)
    {
        auto fileAccess = mFSAccess.newfileaccess(false);

        if (!fileAccess->fopen(path, false, true))
        {
            return false;
        }

        if (fileAccess->size > 0)
        {
            if (!fileAccess->ftruncate())
            {
                return false;
            }
        }

        const string data = randomBytes(n);
        const byte* bytes = reinterpret_cast<const byte*>(&data[0]);

        return fileAccess->fwrite(bytes, static_cast<unsigned>(n), 0x0);
    }

    static LocalPath randomPath(const LocalPath& prefix, const size_t n = 16)
    {
        LocalPath result = prefix;

        result.appendWithSeparator(randomPath(n), false);

        return result;
    }

    static LocalPath randomPath(const size_t n = 16)
    {
        return LocalPath::fromPath(randomBase64(n), mFSAccess);
    }

    static LocalPath separator()
    {
#ifdef _WIN32
        return LocalPath::fromPath("\\", mFSAccess);
#else // _WIN32
        return LocalPath::fromPath("/", mFSAccess);
#endif // ! _WIN32
    }

private:
    static FSACCESS_CLASS mFSAccess;
    static PrnGen mRNG;
}; // Utilities

FSACCESS_CLASS Utilities::mFSAccess;
PrnGen Utilities::mRNG;

class SyncConfigTest
  : public Test
{
public:
    class IOContext
      : public SyncConfigIOContext
    {
    public:
        IOContext(FileSystemAccess& fsAccess,
                  const string& authKey,
                  const string& cipherKey,
                  const string& name,
                  PrnGen& rng)
          : SyncConfigIOContext(fsAccess,
                                    authKey,
                                    cipherKey,
                                    name,
                                    rng)
        {
            // Perform real behavior by default.
            ON_CALL(*this, getSlotsInOrder(_, _))
              .WillByDefault(Invoke(this, &IOContext::getSlotsInOrderConcrete));

            ON_CALL(*this, read(_, _, _))
              .WillByDefault(Invoke(this, &IOContext::readConcrete));

            ON_CALL(*this, remove(_, _))
              .WillByDefault(Invoke(this, &IOContext::removeSlotConcrete));

            ON_CALL(*this, remove(_))
              .WillByDefault(Invoke(this, &IOContext::removeAllSlotsConcrete));

            ON_CALL(*this, write(_, _, _))
              .WillByDefault(Invoke(this, &IOContext::writeConcrete));
        }

        MOCK_METHOD2(getSlotsInOrder, error(const LocalPath&, vector<unsigned int>&));

        MOCK_METHOD3(read, error(const LocalPath&, string&, unsigned int));

        MOCK_METHOD2(remove, error(const LocalPath&, unsigned int));

        MOCK_METHOD1(remove, error(const LocalPath&));

        MOCK_METHOD3(write, error(const LocalPath&, const string&, unsigned int));

    private:
        // Delegate to real behavior.
        error getSlotsInOrderConcrete(const LocalPath& dbPath,
                                      vector<unsigned int>& slotsVec)
        {
            return SyncConfigIOContext::getSlotsInOrder(dbPath, slotsVec);
        }

        error readConcrete(const LocalPath& dbPath,
                           string& data,
                           unsigned int slot)
        {
            return SyncConfigIOContext::read(dbPath, data, slot);
        }

        error removeSlotConcrete(const LocalPath& dbPath,
                                 unsigned int slot)
        {
            return SyncConfigIOContext::remove(dbPath, slot);
        }

        error removeAllSlotsConcrete(const LocalPath& dbPath)
        {
            return SyncConfigIOContext::remove(dbPath);
        }

        error writeConcrete(const LocalPath& dbPath,
                            const string& data,
                            unsigned int slot)
        {
            return SyncConfigIOContext::write(dbPath, data, slot);
        }
    }; // IOContext

    SyncConfigTest()
      : Test()
      , mFSAccess()
      , mRNG()
      , mConfigAuthKey(Utilities::randomBytes(16))
      , mConfigCipherKey(Utilities::randomBytes(16))
      , mConfigName(Utilities::randomBase64(16))
      , mIOContext(mFSAccess,
                   mConfigAuthKey,
                   mConfigCipherKey,
                   mConfigName,
                   mRNG)
    {
    }

    string emptyDB() const
    {
        return "{\"sy\":[]}";
    }

    FSACCESS_CLASS& fsAccess()
    {
        return mFSAccess;
    }

    IOContext& ioContext()
    {
        return mIOContext;
    }

protected:
    FSACCESS_CLASS mFSAccess;
    PrnGen mRNG;
    const string mConfigAuthKey;
    const string mConfigCipherKey;
    const string mConfigName;
    FakeNiceMock<IOContext> mIOContext;
}; // SyncConfigTest

class SyncConfigIOContextTest
  : public SyncConfigTest
{
public:
    SyncConfigIOContextTest()
      : SyncConfigTest()
    {
    }

    string configName() const
    {
        return configPrefix() + mConfigName;
    }

    const string& configPrefix() const
    {
        return SyncConfigIOContext::NAME_PREFIX;
    }
}; // SyncConfigIOContextTest

TEST_F(SyncConfigIOContextTest, GetBadPath)
{
    vector<unsigned int> slotsVec;

    // Generate a bogus path.
    const auto drivePath = Utilities::randomPath();

    // Try to read slots from an invalid path.
    EXPECT_NE(ioContext().getSlotsInOrder(drivePath, slotsVec), API_OK);

    // Slots should be empty.
    EXPECT_TRUE(slotsVec.empty());
}

TEST_F(SyncConfigIOContextTest, GetNoSlots)
{
    // Make sure the drive path exists.
    Directory drive(fsAccess(), Utilities::randomPath());

    // Generate some malformed slots for this user.
    {
        LocalPath configPath = drive;

        // This file will be ignored as it has no slot suffix.
        configPath.appendWithSeparator(
          LocalPath::fromPath(configName(), fsAccess()), false);
        EXPECT_TRUE(Utilities::randomFile(configPath));

        // This file will be ignored as it has a malformed slot suffix.
        configPath.append(LocalPath::fromPath(".", fsAccess()));
        EXPECT_TRUE(Utilities::randomFile(configPath));

        // This file will be ignored as it has an invalid slot suffix.
        configPath.append(LocalPath::fromPath("Q", fsAccess()));
        EXPECT_TRUE(Utilities::randomFile(configPath));
    }

    // Generate a slot for a different user.
    {
        LocalPath configPath = drive;

        configPath.appendWithSeparator(
          LocalPath::fromPath(configPrefix(), fsAccess()), false);
        configPath.append(Utilities::randomPath());
        configPath.append(LocalPath::fromPath(".0", fsAccess()));

        EXPECT_TRUE(Utilities::randomFile(configPath));
    }

    vector<unsigned int> slotsVec;

    // Try and get a list of slots.
    EXPECT_EQ(ioContext().getSlotsInOrder(drive.path(), slotsVec), API_OK);

    // Slots should be empty.
    EXPECT_TRUE(slotsVec.empty());
}

TEST_F(SyncConfigIOContextTest, GetSlotsOrderedByModificationTime)
{
    const size_t NUM_SLOTS = 3;

    // Make sure drive path exists.
    Directory drive(fsAccess(), Utilities::randomPath());

    // Generate some slots for this user.
    {
        LocalPath configPath = drive;

        // Generate suitable config path prefix.
        configPath.appendWithSeparator(
          LocalPath::fromPath(configName(), fsAccess()), false);

        for (size_t i = 0; i < NUM_SLOTS; ++i)
        {
            using std::to_string;

            ScopedLengthRestore restorer(configPath);

            // Generate suffix.
            LocalPath suffixPath =
              LocalPath::fromPath("." + to_string(i), fsAccess());

            // Complete config path.
            configPath.append(suffixPath);

            // Populate the file.
            EXPECT_TRUE(Utilities::randomFile(configPath));

            // Set the modification time.
            EXPECT_TRUE(fsAccess().setmtimelocal(configPath, i * 1000));
        }
    }

    vector<unsigned int> slotsVec;

    // Get the slots.
    EXPECT_EQ(ioContext().getSlotsInOrder(drive.path(), slotsVec), API_OK);

    // Did we retrieve the correct number of slots?
    ASSERT_EQ(slotsVec.size(), NUM_SLOTS);

    // Are the slots ordered by descending modification time?
    {
        vector<unsigned int> expected(NUM_SLOTS, 0);

        iota(begin(expected), end(expected), 0);

        EXPECT_TRUE(equal(begin(expected), end(expected), slotsVec.rbegin()));
    }
}

TEST_F(SyncConfigIOContextTest, GetSlotsOrderedBySlotSuffix)
{
    const size_t NUM_SLOTS = 3;

    // Make sure drive path exists.
    Directory drive(fsAccess(), Utilities::randomPath());

    // Generate some slots for this user.
    {
        LocalPath configPath = drive;

        // Generate suitable config path prefix.
        configPath.appendWithSeparator(
          LocalPath::fromPath(configName(), fsAccess()), false);

        for (size_t i = 0; i < NUM_SLOTS; ++i)
        {
            using std::to_string;

            ScopedLengthRestore restorer(configPath);

            // Generate suffix.
            LocalPath suffixPath =
              LocalPath::fromPath("." + to_string(i), fsAccess());

            // Complete config path.
            configPath.append(suffixPath);

            // Populate the file.
            EXPECT_TRUE(Utilities::randomFile(configPath));

            // Set the modification time.
            EXPECT_TRUE(fsAccess().setmtimelocal(configPath, 0));
        }
    }

    vector<unsigned int> slotsVec;

    // Get the slots.
    EXPECT_EQ(ioContext().getSlotsInOrder(drive.path(), slotsVec), API_OK);

    // Did we retrieve the correct number of slots?
    EXPECT_EQ(slotsVec.size(), NUM_SLOTS);

    // Are the slots ordered by descending slot number when their
    // modification time is the same?
    {
        vector<unsigned int> expected(NUM_SLOTS, 0);

        iota(begin(expected), end(expected), 0);

        EXPECT_TRUE(equal(begin(expected), end(expected), slotsVec.rbegin()));
    }
}

TEST_F(SyncConfigIOContextTest, Read)
{
    // Make sure the drive path exists.
    Directory drive(fsAccess(), Utilities::randomPath());

    // Try writing some data out and reading it back again.
    {
        string read;
        string written = Utilities::randomBytes(64);

        EXPECT_EQ(ioContext().write(drive.path(), written, 0), API_OK);
        EXPECT_EQ(ioContext().read(drive.path(), read, 0), API_OK);
        EXPECT_EQ(read, written);
    }

    // Try a different slot to make sure it has an effect.
    {
        string read;
        string written = Utilities::randomBytes(64);

        EXPECT_EQ(ioContext().read(drive.path(), read, 1), API_EREAD);
        EXPECT_TRUE(read.empty());

        EXPECT_EQ(ioContext().write(drive.path(), written, 1), API_OK);
        EXPECT_EQ(ioContext().read(drive.path(), read, 1), API_OK);
        EXPECT_EQ(read, written);
    }
}

TEST_F(SyncConfigIOContextTest, ReadBadData)
{
    string data;

    // Make sure the drive path exists.
    Directory drive(fsAccess(), Utilities::randomPath());

    // Generate slot path.
    LocalPath slotPath = drive;

    slotPath.appendWithSeparator(
      LocalPath::fromPath(configName(), fsAccess()), false);

    slotPath.append(LocalPath::fromPath(".0", fsAccess()));

    // Try loading a file that's too short to be valid.
    EXPECT_TRUE(Utilities::randomFile(slotPath, 1));
    EXPECT_EQ(ioContext().read(drive.path(), data, 0), API_EREAD);
    EXPECT_TRUE(data.empty());

    // Try loading a file composed entirely of junk.
    EXPECT_TRUE(Utilities::randomFile(slotPath, 128));
    EXPECT_EQ(ioContext().read(drive.path(), data, 0), API_EREAD);
    EXPECT_TRUE(data.empty());
}

TEST_F(SyncConfigIOContextTest, ReadBadPath)
{
    const LocalPath drivePath = Utilities::randomPath();
    string data;

    // Try and read data from an insane path.
    EXPECT_EQ(ioContext().read(drivePath, data, 0), API_EREAD);
    EXPECT_TRUE(data.empty());
}

TEST_F(SyncConfigIOContextTest, RemoveSlot)
{
    // Make sure drive path exists.
    Directory drive(fsAccess(), Utilities::randomPath());

    // Generate a slot for this user.
    {
        LocalPath configPath = drive;

        // Generate path prefix.
        configPath.appendWithSeparator(
            LocalPath::fromPath(configName(), fsAccess()), false);

        // Generate suffix.
        configPath.append(LocalPath::fromPath(".0", fsAccess()));

        // Populate slot.
        EXPECT_TRUE(Utilities::randomFile(configPath));
    }

    // Remove the slot.
    EXPECT_EQ(ioContext().remove(drive.path(), 0), API_OK);

    // Remove should fail as the slot's already gone.
    EXPECT_EQ(ioContext().remove(drive.path(), 0), API_EWRITE);
}

TEST_F(SyncConfigIOContextTest, RemoveSlots)
{
    const auto drivePath = Utilities::randomPath();

    // No slots to remove.
    EXPECT_CALL(ioContext(),
                getSlotsInOrder(Eq(drivePath), _))
      .WillOnce(Return(API_ENOENT));

    EXPECT_EQ(ioContext().remove(drivePath), API_ENOENT);

    // Two slots to remove.
    static const vector<unsigned int> slotsVec = {0, 1};

    EXPECT_CALL(ioContext(),
                getSlotsInOrder(Eq(drivePath), _))
      .WillRepeatedly(DoAll(SetArgReferee<1>(slotsVec),
                            Return(API_OK)));
    
    // All slots should be removed successfully.
    EXPECT_CALL(ioContext(),
                remove(Eq(drivePath), _))
      .WillRepeatedly(Return(API_OK));

    EXPECT_EQ(ioContext().remove(drivePath), API_OK);

    // Should only succeed if all slots can be removed.
    EXPECT_CALL(ioContext(),
                remove(Eq(drivePath), Eq(0u)))
      .WillRepeatedly(Return(API_EWRITE));

    EXPECT_EQ(ioContext().remove(drivePath), API_EWRITE);
}

TEST_F(SyncConfigIOContextTest, Serialize)
{
    SyncConfigVector read;
    SyncConfigVector written;
    JSONWriter writer;

    // Populate the database with two configs.
    {
        SyncConfig config;

        config.mBackupId = 1;
        config.mEnabled = false;
        config.mError = NO_SYNC_ERROR;
        config.mLocalFingerprint = 1;
        config.mLocalPath = Utilities::randomPath();
        config.mName = Utilities::randomBase64();
        config.mOriginalPathOfRemoteRootNode = Utilities::randomBase64();
        config.mRemoteNode = NodeHandle();
        config.mWarning = NO_SYNC_WARNING;
        config.mSyncType = SyncConfig::TYPE_TWOWAY;
        config.mBackupState = SYNC_BACKUP_NONE;

        written.emplace_back(config);

        config.mBackupId = 2;
        config.mEnabled = true;
        config.mError = UNKNOWN_ERROR;
        config.mLocalFingerprint = 2;
        config.mLocalPath = Utilities::randomPath();
        config.mName = Utilities::randomBase64();
        config.mOriginalPathOfRemoteRootNode = Utilities::randomBase64();
        config.mRemoteNode.set6byte(3);
        config.mWarning = LOCAL_IS_FAT;
        config.mSyncType = SyncConfig::TYPE_BACKUP;
        config.mBackupId = SYNC_BACKUP_MIRROR;

        written.emplace_back(config);
    }

    // Serialize the database.
    {
        ioContext().serialize(written, writer);
        EXPECT_FALSE(writer.getstring().empty());
    }

    // Deserialize the database.
    {
        JSON reader(writer.getstring());
        EXPECT_TRUE(ioContext().deserialize(read, reader));
    }

    // Are the databases identical?
    EXPECT_EQ(read, written);
}

TEST_F(SyncConfigIOContextTest, SerializeEmpty)
{
    JSONWriter writer;

    // Serialize an empty database.
    {
        static const SyncConfigVector empty;

        // Does serializing an empty database yield an empty array?
        ioContext().serialize(empty, writer);
        EXPECT_EQ(writer.getstring(), emptyDB());
    }

    // Deserialize the empty database.
    {
        SyncConfigVector configs;
        JSON reader(writer.getstring());

        // Can we deserialize an empty database?
        EXPECT_TRUE(ioContext().deserialize(configs, reader));
        EXPECT_TRUE(configs.empty());
    }
}

TEST_F(SyncConfigIOContextTest, WriteBadPath)
{
    const LocalPath drivePath = Utilities::randomPath();
    const string data = Utilities::randomBytes(64);

    auto dbPath = drivePath;
    dbPath.appendWithSeparator(Utilities::randomPath(), false);

    // Try and write data to an insane path.
    EXPECT_NE(ioContext().write(dbPath, data, 0), API_OK);
}

class SyncConfigStoreTest
  : public SyncConfigTest
{
public:
    SyncConfigStoreTest()
      : SyncConfigTest()
    {
    }
}; // SyncConfigStoreTest

TEST_F(SyncConfigStoreTest, Read)
{
    Directory db(fsAccess(), Utilities::randomPath());

    SyncConfigVector written;

    // Create a database for later reading.
    {
        SyncConfigStore store(db, ioContext());

        // Read empty so that the drive is known.
        EXPECT_EQ(store.read(LocalPath(), written), API_ENOENT);

        // Drive should be known.
        ASSERT_TRUE(store.driveKnown(LocalPath()));

        // Create a config for writing.
        SyncConfig config;

        config.mBackupId = 1;
        config.mLocalPath = Utilities::randomPath();
        config.mRemoteNode.set6byte(2);

        written.emplace_back(config);

        // Write database to disk.
        EXPECT_EQ(store.write(LocalPath(), written), API_OK);
    }

    SyncConfigStore store(db, ioContext());

    SyncConfigVector read;

    // Read database back.
    EXPECT_EQ(store.read(LocalPath(), read), API_OK);

    // Drive should now be known.
    EXPECT_TRUE(store.driveKnown(LocalPath()));

    // Configs should be precisely what we wrote.
    EXPECT_EQ(read, written);
}

TEST_F(SyncConfigStoreTest, ReadEmpty)
{
    Directory db(fsAccess(), Utilities::randomPath());

    SyncConfigStore store(db, ioContext());

    // No slots available for reading.
    EXPECT_CALL(ioContext(),
                getSlotsInOrder(Eq(db.path()), _))
      .WillOnce(Return(API_ENOENT));

    // No attempts should be made to read from disk.
    EXPECT_CALL(ioContext(),
                read(Eq(db.path()), _, _))
      .Times(0);

    SyncConfigVector configs;

    // Read should inform the caller that no database is present.
    EXPECT_EQ(store.read(LocalPath(), configs), API_ENOENT);

    // Configs should remain empty.
    EXPECT_TRUE(configs.empty());

    // Drive should be known as read didn't signal a fatal error.
    EXPECT_TRUE(store.driveKnown(LocalPath()));

    // Store should remain clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, ReadFail)
{
    Directory db(fsAccess(), Utilities::randomPath());

    SyncConfigStore store(db, ioContext());

    // Return a single slot for reading.
    Expectation get =
      EXPECT_CALL(ioContext(),
                  getSlotsInOrder(Eq(db.path()), _))
        .WillOnce(DoAll(SetArgReferee<1>(vector<unsigned>{0}),
                        Return(API_OK)));

    // Attempts to read the slot should fail.
    Expectation read =
      EXPECT_CALL(ioContext(),
                  read(Eq(db.path()), _, Eq(0u)))
        .After(get)
        .WillOnce(Return(API_EREAD));

    SyncConfigVector configs;

    // Read should report a fatal read error.
    EXPECT_EQ(store.read(LocalPath(), configs), API_EREAD);

    // Configs should remain empty.
    EXPECT_TRUE(configs.empty());

    // Drive should remain unknown as read failed fatally.
    EXPECT_FALSE(store.driveKnown(LocalPath()));

    // Store should remain clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, ReadFailFallback)
{
    Directory db(fsAccess(), Utilities::randomPath());

    SyncConfigStore store(db, ioContext());

    // Return three slots for reading.
    Expectation get =
      EXPECT_CALL(ioContext(),
                  getSlotsInOrder(Eq(db.path()), _))
        .WillOnce(DoAll(SetArgReferee<1>(vector<unsigned>{2, 1, 0}),
                        Return(API_OK)));

    // Attempts to read slots 2 and 1 should fail.
    Expectation read21 =
      EXPECT_CALL(ioContext(),
                  read(Eq(db.path()), _, _))
        .Times(2)
        .After(get)
        .WillRepeatedly(Return(API_EREAD));

    // Attempts to read slot 0 should succeed.
    Expectation read0 =
      EXPECT_CALL(ioContext(),
                  read(Eq(db.path()), _, Eq(0u)))
        .After(read21)
        .WillOnce(DoAll(SetArgReferee<1>(emptyDB()),
                        Return(API_OK)));

    SyncConfigVector configs;

    // Read should succeed.
    EXPECT_EQ(store.read(LocalPath(), configs), API_OK);

    // Configs should remain empty.
    EXPECT_TRUE(configs.empty());

    // Drive should be known as the read eventually succeeded.
    EXPECT_TRUE(store.driveKnown(LocalPath()));

    // Should should remain clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, WriteDirty)
{
    Directory db(fsAccess(), Utilities::randomPath());

    SyncConfigStore store(db, ioContext());

    SyncConfigVector configs;

    // Perform a read such that the drive is known.
    EXPECT_EQ(store.read(LocalPath(), configs), API_ENOENT);

    SyncConfigVector internal;

    // Populate configs.
    {
        SyncConfig config;

        // External
        config.mBackupId = 1;
        config.mExternalDrivePath = Utilities::randomPath();
        config.mLocalPath = Utilities::randomPath(config.mExternalDrivePath);
        config.mRemoteNode.set6byte(2);

        configs.emplace_back(config);

        // Internal
        config.mBackupId = 2;
        config.mExternalDrivePath.clear();
        config.mLocalPath = Utilities::randomPath();
        config.mRemoteNode.set6byte(3);

        configs.emplace_back(config);
        internal.emplace_back(config);
    }

    // Mark internal database for writing.
    store.markDriveDirty(LocalPath());

    // Serialize configs for later comparison.
    JSONWriter writer;

    ioContext().serialize(internal, writer);

    // First attempt to write the database should fail.
    // Subsequent attempts should succeed.
    EXPECT_CALL(ioContext(),
                write(Eq(db.path()), Eq(writer.getstring()), Eq(0u)))
      .Times(2)
      .WillOnce(Return(API_EWRITE))
      .WillRepeatedly(Return(API_OK));

    // First attempt to write dirty databases should fail.
    store.writeDirtyDrives(configs);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Second attempt to write dirty databases should succeed.
    store.writeDirtyDrives(configs);

    // Store should now be clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, WriteEmpty)
{
    Directory db(fsAccess(), Utilities::randomPath());

    SyncConfigStore store(db, ioContext());

    SyncConfigVector configs;

    // Read empty so that the drive is known.
    EXPECT_EQ(store.read(LocalPath(), configs), API_ENOENT);

    // Drive should now be known.
    ASSERT_TRUE(store.driveKnown(LocalPath()));

    // Attempt to remove the database should fail.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path())))
      .WillOnce(Return(API_EWRITE));

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Write should fail.
    EXPECT_EQ(store.write(LocalPath(), configs), API_EWRITE);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Next attempt to remove should succeed.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path())))
      .WillOnce(Return(API_OK));

    // Write should succeed.
    EXPECT_EQ(store.write(LocalPath(), configs), API_OK);

    // Store should now be clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, Write)
{
    Directory db(fsAccess(), Utilities::randomPath());

    SyncConfigStore store(db, ioContext());

    SyncConfigVector configs;

    // Read empty so that the drive is known.
    EXPECT_EQ(store.read(LocalPath(), configs), API_ENOENT);

    // Drive should be known.
    ASSERT_TRUE(store.driveKnown(LocalPath()));

    // Populate config vector.
    {
        SyncConfig config;

        config.mBackupId = 2;
        config.mLocalPath = Utilities::randomPath();
        config.mRemoteNode.set6byte(3);

        configs.emplace_back(config);
    }

    // Serialize configs for later comparison.
    JSONWriter writer;

    ioContext().serialize(configs, writer);

    // Attempts to write the first slot should fail.
    EXPECT_CALL(ioContext(),
                write(Eq(db.path()), Eq(writer.getstring()), Eq(0u)))
      .WillOnce(Return(API_EWRITE));

    // No databases should be removed in anticipation as the write failed.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path()), Eq(1u)))
      .Times(0);

    // Mark drive as being dirty.
    store.markDriveDirty(LocalPath());

    // Write should fail.
    EXPECT_EQ(store.write(LocalPath(), configs), API_EWRITE);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Attempts to write the first slot should succeed.
    EXPECT_CALL(ioContext(),
                write(Eq(db.path()), Eq(writer.getstring()), Eq(0u)))
      .WillOnce(Return(API_OK));

    // Next slot should be removed in anticipation of future writes.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path()), Eq(1u)))
      .WillOnce(Return(API_OK));

    // Write should succeed.
    EXPECT_EQ(store.write(LocalPath(), configs), API_OK);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());
}

} // SyncConfigTests

#endif

