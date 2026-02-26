/**
 * @file
 * @brief Tests for the utilities in mega/name_collision.hpp
 */

#include "mega/name_collision.h"
#include "mega/utils.h"

#include <gtest/gtest.h>

using namespace mega::ncoll;
using mega::fileExtensionDotPosition;

TEST(NameCollision, SplitBaseNameKindId)
{
    using namespace std::literals;
    EXPECT_EQ(getBaseNameKindId("test"), (std::tuple{"test"s, ENameType::baseNameOnly, 0u}));
    EXPECT_EQ(getBaseNameKindId("test (1)"), (std::tuple{"test"s, ENameType::withIdSpace, 1u}));
    EXPECT_EQ(getBaseNameKindId("test (0)"), (std::tuple{"test"s, ENameType::withIdSpace, 0u}));
    EXPECT_EQ(getBaseNameKindId("test(2)"), (std::tuple{"test"s, ENameType::withIdNoSpace, 2u}));
    EXPECT_EQ(getBaseNameKindId("test  (3)"), (std::tuple{"test "s, ENameType::withIdSpace, 3u}));
    EXPECT_EQ(getBaseNameKindId("t((3))"), (std::tuple{"t((3))"s, ENameType::baseNameOnly, 0u}));
}

TEST(NameCollision, GetDotPos)
{
    std::string name = "test.txt";
    size_t dotPos = fileExtensionDotPosition(name);
    ASSERT_EQ(dotPos, 4);
    ASSERT_EQ(name.substr(0, dotPos), "test");
    ASSERT_EQ(name.substr(dotPos), ".txt");

    name = "test .txt";
    dotPos = fileExtensionDotPosition(name);
    ASSERT_EQ(dotPos, 5);
    ASSERT_EQ(name.substr(0, dotPos), "test ");
    ASSERT_EQ(name.substr(dotPos), ".txt");

    name = "test.";
    dotPos = fileExtensionDotPosition(name);
    ASSERT_EQ(dotPos, 4);
    ASSERT_EQ(name.substr(0, dotPos), "test");
    ASSERT_EQ(name.substr(dotPos), ".");

    name = "test";
    dotPos = fileExtensionDotPosition(name);
    ASSERT_EQ(dotPos, 4);
    ASSERT_EQ(name.substr(0, dotPos), "test");
    ASSERT_EQ(name.substr(dotPos), "");
}

TEST(NameCollision, NextFreeIndexFromZero)
{
    NewFreeIndexProvider p;
    // Initially, all free
    ASSERT_TRUE(p.isFree(ENameType::baseNameOnly, 0));
    ASSERT_TRUE(p.isFree(ENameType::withIdSpace, 1));
    ASSERT_TRUE(p.isFree(ENameType::withIdNoSpace, 1));

    // Occupy the base name
    ASSERT_EQ(p.getNextFreeIndex(ENameType::baseNameOnly, 0), 0);
    ASSERT_FALSE(p.isFree(ENameType::baseNameOnly, 0));

    // Occupy sequentially with space
    ASSERT_EQ(p.getNextFreeIndex(ENameType::withIdSpace, 1), 1);
    ASSERT_EQ(p.getNextFreeIndex(ENameType::withIdSpace, 1), 2);
    ASSERT_EQ(p.getNextFreeIndex(ENameType::withIdSpace, 1), 3);

    // Occupy sequentially with no space
    ASSERT_EQ(p.getNextFreeIndex(ENameType::withIdNoSpace, 1), 1);
    ASSERT_EQ(p.getNextFreeIndex(ENameType::withIdNoSpace, 1), 2);
    ASSERT_EQ(p.getNextFreeIndex(ENameType::withIdNoSpace, 1), 3);
}

TEST(NameCollision, NextFreeIndexWithHoles)
{
    const auto validateKind = [](const ENameType& kind)
    {
        NewFreeIndexProvider p;
        // Fill leaving some empty ids
        p.addOccupiedIndex(kind, 2);
        p.addOccupiedIndex(kind, 3);
        p.addOccupiedIndex(kind, 5);
        p.addOccupiedIndex(kind, 7);

        // Check some occupied
        ASSERT_FALSE(p.isFree(kind, 2));
        ASSERT_FALSE(p.isFree(kind, 5));

        // The wholes are free
        ASSERT_TRUE(p.isFree(kind, 1));
        ASSERT_TRUE(p.isFree(kind, 4));
        ASSERT_TRUE(p.isFree(kind, 6));

        // Start getting from 1
        ASSERT_EQ(p.getNextFreeIndex(kind, 1), 1);
        ASSERT_EQ(p.getNextFreeIndex(kind, 1), 4);
        ASSERT_FALSE(p.isFree(kind, 4));
        // Get one form out range
        ASSERT_EQ(p.getNextFreeIndex(kind, 8), 8);
        // Continue getting from 1
        ASSERT_EQ(p.getNextFreeIndex(kind, 1), 6);
        ASSERT_EQ(p.getNextFreeIndex(kind, 1), 9);

        // We can also add the 0
        ASSERT_TRUE(p.isFree(kind, 0));
        p.addOccupiedIndex(kind, 0);
        ASSERT_FALSE(p.isFree(kind, 0));
    };
    ASSERT_NO_FATAL_FAILURE(validateKind(ENameType::withIdSpace));
    ASSERT_NO_FATAL_FAILURE(validateKind(ENameType::withIdNoSpace));
}

TEST(NameCollision, SolverFromZero)
{
    NameCollisionSolver s;
    // Trivial case for test
    ASSERT_EQ(s("test"), "test");
    ASSERT_EQ(s("test"), "test (1)");
    ASSERT_EQ(s("test"), "test (2)");

    // Empty base name
    ASSERT_EQ(s(""), "");
    ASSERT_EQ(s(""), " (1)");
    ASSERT_EQ(s(""), " (2)");

    // Empty base name no space
    ASSERT_EQ(s("(2)"), "(2)");
    ASSERT_EQ(s("(2)"), "(3)");
    ASSERT_EQ(s("(0)"), "(0)");
    ASSERT_EQ(s("(0)"), "(1)");
    ASSERT_EQ(s("(0)"), "(4)");

    // Space at the end of the name
    ASSERT_EQ(s("test "), "test ");
    ASSERT_EQ(s("test "), "test  (1)");

    // Number stick to the name
    ASSERT_EQ(s("test(1)"), "test(1)");
    ASSERT_EQ(s("test(1)"), "test(2)");
    ASSERT_EQ(s("test(1)"), "test(3)");

    // We can add files with the 0
    ASSERT_EQ(s("test(0)"), "test(0)");
    ASSERT_EQ(s("test(0)"), "test(4)");
}

TEST(NameCollision, SolverFromExistingNoNumbers)
{
    NameCollisionSolver s({"test", "foo", "test "});

    ASSERT_EQ(s("test"), "test (1)");
    ASSERT_EQ(s("test"), "test (2)");

    ASSERT_EQ(s("foo"), "foo (1)");

    ASSERT_EQ(s("test "), "test  (1)");
}

TEST(NameCollision, SolverFromExistingWithNumbers)
{
    NameCollisionSolver s({"test", "test (1)", "test (3)", "foo"});

    // If it exists we get the next number available
    ASSERT_EQ(s("test (3)"), "test (4)");
    ASSERT_EQ(s("test (3)"), "test (5)");

    ASSERT_EQ(s("test"), "test (2)");
    ASSERT_EQ(s("test"), "test (6)");

    ASSERT_EQ(s("foo"), "foo (1)");
}

TEST(NameCollision, FileNameSolverFromZero)
{
    FileNameCollisionSolver s;
    // Trivial case for test and foo
    ASSERT_EQ(s("test.txt"), "test.txt");
    ASSERT_EQ(s("test.txt"), "test (1).txt");
    ASSERT_EQ(s("test.txt"), "test (2).txt");
    // Same name different extension
    ASSERT_EQ(s("test.md"), "test.md");
    ASSERT_EQ(s("test.md"), "test (1).md");
    ASSERT_EQ(s("test.md"), "test (2).md");

    ASSERT_EQ(s(".txt"), ".txt");
    ASSERT_EQ(s(".txt"), " (1).txt");
    ASSERT_EQ(s(".txt"), " (2).txt");

    // No extension (should be supported)
    ASSERT_EQ(s("foo"), "foo");
    ASSERT_EQ(s("foo"), "foo (1)");
    ASSERT_EQ(s("foo"), "foo (2)");

    // Space at the end of the base name
    ASSERT_EQ(s("test .txt"), "test .txt");
    ASSERT_EQ(s("test .txt"), "test  (1).txt");

    // Space at the end of the extension
    ASSERT_EQ(s("test.txt "), "test.txt ");
    ASSERT_EQ(s("test.txt "), "test (1).txt ");

    // Number stick to the name
    ASSERT_EQ(s("test(1).txt"), "test(1).txt");
    ASSERT_EQ(s("test(1).txt"), "test(2).txt");
    ASSERT_EQ(s("test(1).txt"), "test(3).txt");

    // Zero
    ASSERT_EQ(s("test(0).txt"), "test(0).txt");
    ASSERT_EQ(s("test(0).txt"), "test(4).txt");
}

TEST(NameCollision, FileNameSolverFromExistingNoNumbers)
{
    FileNameCollisionSolver s({"test.txt", "foo", "test.md"});

    ASSERT_EQ(s("test.txt"), "test (1).txt");
    ASSERT_EQ(s("test.txt"), "test (2).txt");

    ASSERT_EQ(s("foo"), "foo (1)");

    ASSERT_EQ(s("test.md"), "test (1).md");
}

TEST(NameCollision, FileNameSolverFromExistingWithNumbers)
{
    FileNameCollisionSolver s({"test.txt", "test (1).txt", "test (3).txt", "foo", "test (1).md"});

    // If it exists we get the next number available
    ASSERT_EQ(s("test (3).txt"), "test (4).txt");
    ASSERT_EQ(s("test (3).txt"), "test (5).txt");

    ASSERT_EQ(s("test.txt"), "test (2).txt");
    ASSERT_EQ(s("test.txt"), "test (6).txt");

    ASSERT_EQ(s("foo"), "foo (1)");

    ASSERT_EQ(s("test.md"), "test.md");
    ASSERT_EQ(s("test.md"), "test (2).md");
}
