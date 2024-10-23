/**
 * (c) 2021 by Mega Limited, Wellsford, New Zealand
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

#include <memory>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "megaapi_impl.h"
#include "mega/sync.h"

#ifdef ENABLE_SYNC

namespace SyncConflictTests {

using namespace mega;

TEST(SyncStallHashTest, MegaSyncStallPrivateGetHash)
{
    // Create some SyncStallEntry objects to initialize the MegaSyncStallPrivate after
    SyncStallEntry e1{
        SyncWaitReason::FileIssue,
        true,
        false,
        {NodeHandle{},                  "currentPath", PathProblem::DetectedSymlink},
        {NodeHandle{},                             "currentPath",                               PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::DetectedSymlink                              }
    };
    SyncStallEntry e1_same{
        SyncWaitReason::FileIssue,
        true,
        false,
        {NodeHandle{},                  "currentPath", PathProblem::DetectedSymlink},
        {NodeHandle{},                             "currentPath",                               PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::DetectedSymlink                              }
    };
    SyncStallEntry e2{
        SyncWaitReason::FileIssue,
        true,
        false,
        {NodeHandle{},                  "currentPath", PathProblem::DetectedSymlink},
        {NodeHandle{},                             "currentPath",                               PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::NoProblem                              }
    };

    std::hash<SyncStallEntry> hashEntryGetter;
    ASSERT_EQ(hashEntryGetter(e1), hashEntryGetter(e1));
    ASSERT_EQ(hashEntryGetter(e1), hashEntryGetter(e1_same));
    ASSERT_NE(hashEntryGetter(e1), hashEntryGetter(e2));

    MegaSyncStallPrivate s1(e1);
    MegaSyncStallPrivate s1_same(e1_same);
    MegaSyncStallPrivate s2(e2);

    ASSERT_EQ(hashEntryGetter(e1), s1.getHash());
    ASSERT_EQ(hashEntryGetter(e1_same), s1_same.getHash());
    ASSERT_EQ(hashEntryGetter(e2), s2.getHash());

    ASSERT_EQ(s1.getHash(), s1.getHash());
    ASSERT_EQ(s1.getHash(), s1_same.getHash());

    ASSERT_NE(s1.getHash(), s2.getHash());
}

TEST(SyncStallHashTest, MegaSyncNameConflictStallPrivateGetHash)
{
    // Create some NameConflict objects to initialize the MegaSyncNameConflictStallPrivate after
    std::vector<NameConflict::NameHandle> nhVec{};
    std::vector<LocalPath> clashingLNames{};
    NameConflict nc1{"cloudPath", nhVec, LocalPath(), clashingLNames};
    NameConflict nc1_same{"cloudPath", nhVec, LocalPath(), clashingLNames};

    nhVec.emplace_back("nameHandle", NodeHandle());
    clashingLNames.emplace_back(LocalPath::fromAbsolutePath("./test/local"));
    NameConflict nc2{"cloudPath", nhVec, LocalPath(), clashingLNames};

    std::hash<NameConflict> hashNCGetter;
    ASSERT_EQ(hashNCGetter(nc1), hashNCGetter(nc1));
    ASSERT_EQ(hashNCGetter(nc1), hashNCGetter(nc1_same));
    ASSERT_NE(hashNCGetter(nc1), hashNCGetter(nc2));

    MegaSyncNameConflictStallPrivate s1(nc1);
    MegaSyncNameConflictStallPrivate s1_same(nc1_same);
    MegaSyncNameConflictStallPrivate s2(nc2);

    ASSERT_EQ(hashNCGetter(nc1), s1.getHash());
    ASSERT_EQ(hashNCGetter(nc1_same), s1_same.getHash());
    ASSERT_EQ(hashNCGetter(nc2), s2.getHash());

    ASSERT_EQ(s1.getHash(), s1.getHash());
    ASSERT_EQ(s1.getHash(), s1_same.getHash());

    ASSERT_NE(s1.getHash(), s2.getHash());
}

/**
 * @class MegaSyncStallListTest
 * @brief Dummy implementation of the MegaSyncStallList for testing purpose
 *
 */
class MegaSyncStallListTest: public MegaSyncStallList
{
public:
    std::vector<std::shared_ptr<MegaSyncStall>> mStalls;

    MegaSyncStallListTest(std::vector<std::shared_ptr<MegaSyncStall>>&& stalls):
        mStalls{stalls}
    {}

    size_t size() const override
    {
        return mStalls.size();
    }

    const MegaSyncStall* get(size_t i) const override
    {
        return mStalls[i].get();
    }
};

class MegaSyncStallMapTest: public MegaSyncStallMap
{
public:
    std::map<MegaHandle, MegaSyncStallListTest> mStalls;
    MegaSyncStallMapTest() = default;

    void add(const MegaHandle backupId, const MegaSyncStallListTest& testList)
    {
        mStalls.emplace(backupId, testList);
    }

    MegaHandleList* getKeys() const override
    {
        MegaHandleList* list = MegaHandleList::createInstance();
        for (const auto& stall: mStalls)
        {
            list->addMegaHandle(stall.first);
        }
        return list;
    }

    const MegaSyncStallListTest* get(const MegaHandle key) const override
    {
        if (const auto& it = mStalls.find(key); it != mStalls.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    size_t getHash() const override
    {
        uint64_t hash{};
        if (const MegaHandleList* keys = getKeys(); keys)
        {
            for (unsigned int i = 0; i < keys->size(); ++i)
            {
                hash = hashCombine(hash, get(keys->get(i))->getHash());
            }
        }
        return hash;
    }
};

TEST(SyncStallHashTest, MegaSyncStallIssuesGetHash)
{
    std::vector<NameConflict::NameHandle> nhVec{};
    std::vector<LocalPath> clashingLNames{};
    NameConflict nc1{"cloudPath", nhVec, LocalPath(), clashingLNames};
    std::shared_ptr<MegaSyncNameConflictStallPrivate> s1{new MegaSyncNameConflictStallPrivate(nc1)};

    SyncStallEntry e1{
        SyncWaitReason::FileIssue,
        true,
        false,
        {NodeHandle{},                  "currentPath", PathProblem::DetectedSymlink},
        {NodeHandle{},                             "currentPath",                               PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::DetectedSymlink},
        {LocalPath{}, PathProblem::DetectedSymlink                              }
    };
    std::shared_ptr<MegaSyncStallPrivate> s2{new MegaSyncStallPrivate(e1)};

    MegaSyncStallListTest testList1{
        std::vector<std::shared_ptr<MegaSyncStall>>{s1, s2}
    };
    MegaSyncStallListTest testList1_same{
        std::vector<std::shared_ptr<MegaSyncStall>>{s1, s2}
    };
    MegaSyncStallListTest testList2{
        std::vector<std::shared_ptr<MegaSyncStall>>{s2, s1}
    };
    MegaSyncStallListTest testList3{std::vector<std::shared_ptr<MegaSyncStall>>{s2}};

    ASSERT_EQ(testList1.getHash(), testList1.getHash());
    ASSERT_EQ(testList1.getHash(), testList1_same.getHash());

    ASSERT_NE(testList1.getHash(), testList2.getHash());
    ASSERT_NE(testList1.getHash(), testList3.getHash());
    ASSERT_NE(testList2.getHash(), testList3.getHash());

    MegaSyncStallMapTest map;
    map.add(111111111111111, testList1);
    map.add(222222222222222, testList2);
    map.add(333333333333333, testList3);

    uint64_t hash = {};
    hash = hashCombine(hash, testList1.getHash());
    hash = hashCombine(hash, testList2.getHash());
    hash = hashCombine(hash, testList3.getHash());

    ASSERT_EQ(hash, map.getHash());
}

} // namespace

#endif // ENABLE_SYNC
