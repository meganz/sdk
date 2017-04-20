/**
 * @file tests/tests.cpp
 * @brief Mega SDK main test file
 *
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
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

#include "mega.h"
#include "gtest/gtest.h"

using namespace mega;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

TEST(JSON, storeobject)
{
    std::string in_str("Test");
    JSON j;
    j.begin(in_str.data());
    j.storeobject(&in_str);
}

// Test 64-bit int serialization/unserialization
TEST(Serialize64, serialize)
{
    uint64_t in = 0xDEADBEEF;
    uint64_t out;
    byte buf[sizeof in];

    Serialize64::serialize(buf, in);
    ASSERT_GT(Serialize64::unserialize(buf, sizeof buf, &out), 0);
    ASSERT_EQ(in, out);
}

int main (int argc, char *argv[])
{
    InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
