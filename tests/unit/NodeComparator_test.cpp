/**
 * @file tests/unit/NodeComparator_test.cpp
 * @brief Tests for MegaApiImpl's in-memory node comparators.
 *
 * (c) 2026- by Mega Limited, Auckland, New Zealand
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
 */

#include "utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <mega.h>
#include <megaapi_impl.h>
#include <memory>
#include <string>
#include <vector>

using namespace mega;

namespace
{

const nameid kNameId = AttrMap::string2nameid("n");

class NodeComparatorFixture: public ::testing::Test
{
protected:
    MegaApp mApp;
    std::shared_ptr<MegaClient> mClient;
    uint64_t mNextHandle = 1;

    void SetUp() override
    {
        // Transient client, no DbAccess - the comparators only read Node fields, and
        // getComparatorFunction ignores its MegaClient& argument entirely.
        mClient = mt::makeClient(mApp);
    }

    std::shared_ptr<Node> makeFile(const std::string& name, uint64_t mediaTsMs)
    {
        const NodeHandle h = NodeHandle().set6byte(mNextHandle++);
        auto node = mt::makeNode(*mClient, FILENODE, h);
        node->attrs.map[kNameId] = name;
        node->setMediaTs(mediaTsMs);
        return node;
    }

    std::shared_ptr<Node> makeFolder(const std::string& name)
    {
        const NodeHandle h = NodeHandle().set6byte(mNextHandle++);
        auto node = mt::makeNode(*mClient, FOLDERNODE, h);
        node->attrs.map[kNameId] = name;
        return node;
    }

    std::vector<std::string> sortedNames(sharedNode_vector v, int order)
    {
        MegaApiImpl::sortByComparatorFunction(v, order, *mClient);
        std::vector<std::string> names;
        for (const auto& n: v)
        {
            names.emplace_back(n->displayname());
        }
        return names;
    }
};

} // namespace

// Names run counter to mediats in every fixture below, so a comparator that fell back to
// name order - or dropped the mediats compare entirely - cannot pass by coincidence.

TEST_F(NodeComparatorFixture, MediaTsAsc_OrdersOldestFirst)
{
    const sharedNode_vector v{
        makeFile("a.jpg", 3000),
        makeFile("b.jpg", 1000),
        makeFile("c.jpg", 2000),
    };

    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_ASC),
              (std::vector<std::string>{"b.jpg", "c.jpg", "a.jpg"}));
}

TEST_F(NodeComparatorFixture, MediaTsDesc_OrdersNewestFirst)
{
    const sharedNode_vector v{
        makeFile("a.jpg", 3000),
        makeFile("b.jpg", 1000),
        makeFile("c.jpg", 2000),
    };

    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_DESC),
              (std::vector<std::string>{"a.jpg", "c.jpg", "b.jpg"}));
}

TEST_F(NodeComparatorFixture, MediaTsZero_SortsAsTheSmallestValue)
{
    // zzz.txt is a non-media file, so its mediats is the 0 sentinel. Its name sorts last,
    // so a name-order fallback would place it at the opposite end in both directions.
    const sharedNode_vector v{
        makeFile("aaa.jpg", 2000),
        makeFile("zzz.txt", 0),
    };

    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_ASC),
              (std::vector<std::string>{"zzz.txt", "aaa.jpg"}));
    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_DESC),
              (std::vector<std::string>{"aaa.jpg", "zzz.txt"}));
}

TEST_F(NodeComparatorFixture, FoldersPrecedeFiles_InBothDirections)
{
    // typeComparator runs before the timestamp compare and has no sense of direction, so
    // the folder leads both orders even though its mediats is 0 and its name sorts last.
    const sharedNode_vector v{
        makeFile("aaa.jpg", 5000),
        makeFolder("zzz_folder"),
    };

    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_ASC),
              (std::vector<std::string>{"zzz_folder", "aaa.jpg"}));
    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_DESC),
              (std::vector<std::string>{"zzz_folder", "aaa.jpg"}));
}

TEST_F(NodeComparatorFixture, EqualMediaTs_TieBreaksByNaturalName)
{
    // Pins the DESC tie-break to nodeNaturalComparatorDESC, which is what matches the SQL
    // clause's trailing "name COLLATE NATURALNOCASE DESC" for MEDIATS_DESC.
    const sharedNode_vector v{
        makeFile("b.jpg", 7000),
        makeFile("a.jpg", 7000),
    };

    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_ASC),
              (std::vector<std::string>{"a.jpg", "b.jpg"}));
    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_DESC),
              (std::vector<std::string>{"b.jpg", "a.jpg"}));
}

TEST_F(NodeComparatorFixture, ZeroAndOne_OrderAsPlainValues)
{
    // The 0 sentinel is just the smallest value, not a "missing timestamp" shunted into the
    // name-order fallback. Input order and name order both disagree with the expectation.
    const sharedNode_vector v{
        makeFile("aaa.jpg", 1),
        makeFile("zzz.jpg", 0),
    };

    EXPECT_EQ(sortedNames(v, MegaApi::ORDER_MEDIATS_ASC),
              (std::vector<std::string>{"zzz.jpg", "aaa.jpg"}));
}

// Fail-closed: a new ORDER_* value must have a comparator unless listed below with a reason.
// The switch is over `int` and has no `default:`, so the compiler cannot flag the omission —
// which is how the mediats values were missed.
TEST_F(NodeComparatorFixture, EveryOrderValueHasAComparatorUnlessExempt)
{
    // Handled by an explicit case that returns nullptr, for a stated reason.
    const std::array<int, 3> expectedNull{
        MegaApi::ORDER_NONE, // documented as "undefined order"
        MegaApi::ORDER_SHARE_CREATION_ASC, // shares are sorted by src/impl/share.cpp:129
        MegaApi::ORDER_SHARE_CREATION_DESC,
    };

    // Obsolete enumerators, commented out at megaapi.h:19470-19475. Not valid input:
    // getComparatorFunction reaches assert(false) on them, which aborts the whole binary
    // rather than failing one case, so they must be skipped and never called.
    const std::array<int, 6> obsolete{9, 10, 11, 12, 13, 14};

    for (int order = MegaApi::ORDER_NONE; order <= MegaApi::ORDER_MEDIATS_DESC; ++order)
    {
        if (std::find(obsolete.begin(), obsolete.end(), order) != obsolete.end())
        {
            continue;
        }

        const auto comparator = MegaApiImpl::getComparatorFunction(order, *mClient);

        if (std::find(expectedNull.begin(), expectedNull.end(), order) != expectedNull.end())
        {
            EXPECT_FALSE(comparator) << "order " << order << " unexpectedly gained a comparator";
        }
        else
        {
            EXPECT_TRUE(comparator)
                << "order " << order << " has no comparator - add one, or add it to "
                << "expectedNull with a reason";
        }
    }
}
