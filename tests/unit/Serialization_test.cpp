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

#include "mega.h"
#include "gtest/gtest.h"

#include "megaapi.h"
#include <memory>
#include <thread>
#include <atomic>
using namespace std;

using namespace mega;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

TEST(Serialization, JSON_storeobject)
{
    std::string in_str("Test");
    JSON j;
    j.begin(in_str.data());
    j.storeobject(&in_str);
}

// Test 64-bit int serialization/unserialization
TEST(Serialization, Serialize64_serialize)
{
    uint64_t in = 0xDEADBEEF;
    uint64_t out;
    ::mega::byte buf[sizeof in];

    Serialize64::serialize(buf, in);
    ASSERT_GT(Serialize64::unserialize(buf, sizeof buf, &out), 0);
    ASSERT_EQ(in, out);
}

size_t checksize(size_t& n, size_t added)
{
    n += added;
    return n;
}

TEST(Serialization, CacheableReaderWriter)
{
    string writestring;
    CacheableWriter w(writestring);

    ::mega::byte binary[] = { 1, 2, 3, 4, 5 };
    string cstr1("test1");
    string cstr2("test2diffdata");
    string stringtest("diffstringagaindefinitelybigger");
    int64_t i64 = 0x8765432112345678;
    uint32_t u32 = 0x87678765;
    handle handle1 = 0x998;
    bool b = true;
    ::mega::byte by = 5;
    chunkmac_map cm;
    cm[777].offset = 888;

    size_t sizeadded = 0;

    w.serializebinary(binary, sizeof(binary));
    ASSERT_EQ(writestring.size(), checksize(sizeadded, sizeof(binary)));

    w.serializecstr(cstr1.c_str(), true);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 2 + cstr1.size() + 1));

    w.serializecstr(cstr2.c_str(), false);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 2 + cstr2.size()));

    w.serializestring(stringtest);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 2 + stringtest.size()));

    w.serializei64(i64);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 8));

    w.serializeu32(u32);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 4));

    w.serializehandle(handle1);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, sizeof(handle)));

    w.serializebool(b);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, sizeof(bool)));

    w.serializebyte(by);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 1));

    w.serializechunkmacs(cm);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 2 + 1 * (sizeof(m_off_t) + sizeof(ChunkMAC))));

    w.serializeexpansionflags(1, 0, 1, 0, 0, 0, 1, 1);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 8));

    writestring += "abc";

    // now read the serialized data back
    string readstring = writestring;
    CacheableReader r(readstring);

    ::mega::byte check_binary[5];
    string check_cstr1;
    string check_cstr2;
    string check_stringtest;
    int64_t check_i64;
    uint32_t check_u32;
    handle check_handle1;
    bool check_b;
    ::mega::byte check_by;
    chunkmac_map check_cm;

    ASSERT_TRUE(r.unserializebinary(check_binary, sizeof(check_binary)));
    ASSERT_EQ(0, memcmp(check_binary, binary, sizeof(binary)));

    ASSERT_TRUE(r.unserializecstr(check_cstr1, true));
    ASSERT_EQ(check_cstr1, cstr1);

    ASSERT_TRUE(r.unserializecstr(check_cstr2, false));
    ASSERT_EQ(check_cstr2, cstr2);

    ASSERT_TRUE(r.unserializestring(check_stringtest));
    ASSERT_EQ(check_stringtest, stringtest);

    ASSERT_TRUE(r.unserializei64(check_i64));
    ASSERT_EQ(check_i64, i64);

    ASSERT_TRUE(r.unserializeu32(check_u32));
    ASSERT_EQ(check_u32, u32);

    ASSERT_TRUE(r.unserializehandle(check_handle1));
    ASSERT_EQ(check_handle1, handle1);

    ASSERT_TRUE(r.unserializebool(check_b));
    ASSERT_EQ(check_b, b);

    ASSERT_TRUE(r.unserializebyte(check_by));
    ASSERT_EQ(check_by, by);

    ASSERT_TRUE(r.unserializechunkmacs(check_cm));
    ASSERT_EQ(check_cm[777].offset, cm[777].offset);

    unsigned char expansions[8];
    ASSERT_FALSE(r.unserializeexpansionflags(expansions, 7));
    ASSERT_TRUE(r.unserializeexpansionflags(expansions, 8));
    ASSERT_EQ(expansions[0], 1);
    ASSERT_EQ(expansions[1], 0);
    ASSERT_EQ(expansions[2], 1);
    ASSERT_EQ(expansions[3], 0);
    ASSERT_EQ(expansions[4], 0);
    ASSERT_EQ(expansions[5], 0);
    ASSERT_EQ(expansions[6], 1);
    ASSERT_EQ(expansions[7], 1);

    r.eraseused(readstring);
    ASSERT_EQ(readstring, "abc");

    MediaProperties mp;
    mp.shortformat = 1;
    mp.width = 2;
    mp.height = 3;
    mp.fps = 4;
    mp.playtime = 5;
    mp.containerid = 6;
    mp.videocodecid = 7;
    mp.audiocodecid = 8;
    mp.is_VFR = true;
    mp.no_audio = false;
    string mps = mp.serialize();
    MediaProperties mp2(mps);
    ASSERT_EQ(mps, mp2.serialize());
    ASSERT_EQ(mp2.shortformat, 1);
    ASSERT_EQ(mp2.width, 2u);
    ASSERT_EQ(mp2.height, 3u);
    ASSERT_EQ(mp2.fps, 4u);
    ASSERT_EQ(mp2.playtime, 5u);
    ASSERT_EQ(mp2.containerid, 6u);
    ASSERT_EQ(mp2.videocodecid, 7u);
    ASSERT_EQ(mp2.audiocodecid, 8u);
    ASSERT_EQ(mp2.is_VFR, true);
    ASSERT_EQ(mp2.no_audio, false);
}
