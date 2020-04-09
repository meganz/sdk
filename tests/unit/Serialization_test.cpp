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

#include <atomic>
#include <memory>
#include <numeric>
#include <thread>

#include <gtest/gtest.h>

#include <mega.h>
#include <megaapi.h>

#include "DefaultedFileSystemAccess.h"
#include "utils.h"

TEST(Serialization, JSON_storeobject)
{
    std::string in_str("Test");
    mega::JSON j;
    j.begin(in_str.data());
    j.storeobject(&in_str);
}

// Test 64-bit int serialization/unserialization
TEST(Serialization, Serialize64_serialize)
{
    uint64_t in = 0xDEADBEEF;
    uint64_t out;
    mega::byte buf[sizeof in];

    mega::Serialize64::serialize(buf, in);
    ASSERT_GT(mega::Serialize64::unserialize(buf, sizeof buf, &out), 0);
    ASSERT_EQ(in, out);
}

TEST(Serialization, CacheableReaderWriter)
{
    auto checksize = [](size_t& n, size_t added)
    {
        n += added;
        return n;
    };

    std::string writestring;
    mega::CacheableWriter w(writestring);

    mega::byte binary[] = { 1, 2, 3, 4, 5 };
    std::string cstr1("test1");
    std::string cstr2("test2diffdata");
    std::string stringtest("diffstringagaindefinitelybigger");
    int64_t i64 = 0x8765432112345678;
    uint32_t u32 = 0x87678765;
    mega::handle handle1 = 0x998;
    bool b = true;
    mega::byte by = 5;
    mega::chunkmac_map cm;
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
    ASSERT_EQ(writestring.size(), checksize(sizeadded, sizeof(mega::handle)));

    w.serializebool(b);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, sizeof(bool)));

    w.serializebyte(by);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 1));

    w.serializechunkmacs(cm);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 2 + 1 * (sizeof(m_off_t) + sizeof(mega::ChunkMAC))));

    w.serializeexpansionflags(1, 0, 1, 0, 0, 0, 1, 1);
    ASSERT_EQ(writestring.size(), checksize(sizeadded, 8));

    writestring += "abc";

    // now read the serialized data back
    std::string readstring = writestring;
    mega::CacheableReader r(readstring);

    mega::byte check_binary[5];
    std::string check_cstr1;
    std::string check_cstr2;
    std::string check_stringtest;
    int64_t check_i64;
    uint32_t check_u32;
    mega::handle check_handle1;
    bool check_b;
    mega::byte check_by;
    mega::chunkmac_map check_cm;

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

    mega::MediaProperties mp;
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
    std::string mps = mp.serialize();
    mega::MediaProperties mp2(mps);
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

TEST(Serialization, CacheableReader_32bit)
{
    // This is the result of serialization on 32bit Windows
    const std::array<unsigned char, 125> rawData = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x74, 0x65, 0x73, 0x74, 0x31,
        0x00, 0x0d, 0x00, 0x74, 0x65, 0x73, 0x74, 0x32, 0x64, 0x69, 0x66, 0x66,
        0x64, 0x61, 0x74, 0x61, 0x1f, 0x00, 0x64, 0x69, 0x66, 0x66, 0x73, 0x74,
        0x72, 0x69, 0x6e, 0x67, 0x61, 0x67, 0x61, 0x69, 0x6e, 0x64, 0x65, 0x66,
        0x69, 0x6e, 0x69, 0x74, 0x65, 0x6c, 0x79, 0x62, 0x69, 0x67, 0x67, 0x65,
        0x72, 0x78, 0x56, 0x34, 0x12, 0x21, 0x43, 0x65, 0x87,
        0x65, 0x87, 0x67, 0x87,
        0x98, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01,
        0x00, 0x09, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcd,
        0xcd, 0xcd,
        0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd,
        0xcd, 0x78, 0x03, 0x00, 0x00, 0x00,
        0xcd, 0xcd, 0xcd,
        0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x01, 0x01
    };
    std::string writestring(reinterpret_cast<const char*>(rawData.data()), rawData.size());

    writestring += "abc";

    // now read the serialized data back
    std::string readstring = writestring;
    mega::CacheableReader r(readstring);

    mega::byte binary[] = { 1, 2, 3, 4, 5 };
    std::string cstr1("test1");
    std::string cstr2("test2diffdata");
    std::string stringtest("diffstringagaindefinitelybigger");
    int64_t i64 = 0x8765432112345678;
    uint32_t u32 = 0x87678765;
    mega::handle handle1 = 0x998;
    bool b = true;
    mega::byte by = 5;
    mega::chunkmac_map cm;
    cm[777].offset = 888;

    mega::byte check_binary[5];
    std::string check_cstr1;
    std::string check_cstr2;
    std::string check_stringtest;
    int64_t check_i64;
    uint32_t check_u32;
    mega::handle check_handle1;
    bool check_b;
    mega::byte check_by;
    mega::chunkmac_map check_cm;

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
}

TEST(Serialization, CacheableReaderWriter_fsfp_t)
{
    std::string data;
    {
        mega::CacheableWriter writer{data};
        writer.serializefsfp(42);
    }
    mega::CacheableReader reader{data};
    fsfp_t fsfp;
    ASSERT_TRUE(reader.unserializefsfp(fsfp));
    ASSERT_EQ(1u, reader.fieldnum);
    ASSERT_EQ(reader.ptr, data.c_str() + data.size());
    ASSERT_EQ(42, fsfp);
}

namespace {

//struct MockFileSystemAccess : mt::DefaultedFileSystemAccess
//{
//    void local2path(std::string* local, std::string* path) const override
//    {
//        *path = *local;
//    }
//
//    void path2local(std::string* local, std::string* path) const override
//    {
//        *path = *local;
//    }
//
//    bool getsname(std::string*, std::string*) const override
//    {
//        return false;
//    }
//};

struct MockClient
{
    mega::MegaApp app;
    ::mega::FSACCESS_CLASS fs;
    std::shared_ptr<mega::MegaClient> cli = mt::makeClient(app, fs);
};

#ifdef ENABLE_SYNC
void checkDeserializedLocalNode(const mega::LocalNode& dl, const mega::LocalNode& ref)
{
    ASSERT_EQ(ref.type, dl.type);
    ASSERT_EQ(ref.size < 0 ? 0 : ref.size, dl.size);
    ASSERT_EQ(ref.parent_dbid, dl.parent_dbid);
    ASSERT_EQ(ref.fsid, dl.fsid);
    ASSERT_EQ(ref.localname, dl.localname);
    ASSERT_EQ(nullptr, dl.slocalname);
    ASSERT_EQ(ref.name, dl.name);
    ASSERT_EQ(ref.crc, dl.crc);
    ASSERT_EQ(ref.mtime, dl.mtime);
    ASSERT_EQ(true, dl.isvalid);
    ASSERT_EQ(nullptr, dl.parent);
    ASSERT_EQ(ref.sync, dl.sync);
    ASSERT_EQ(ref.mSyncable, dl.mSyncable);
    ASSERT_EQ(false, dl.created);
    ASSERT_EQ(false, dl.reported);
    ASSERT_EQ(true, dl.checked);
}
#endif

}

#ifdef ENABLE_SYNC
TEST(Serialization, LocalNode_shortData)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    std::string data = "I am too short";
    auto dl = std::unique_ptr<mega::LocalNode>{mega::LocalNode::unserialize(sync.get(), &data)};
    ASSERT_EQ(nullptr, dl);
}

TEST(Serialization, LocalNode_forFolder_withoutParent_withoutNode)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto& l = *sync->localroot;
    l.mSyncable = false;
    l.setfsid(10, client.cli->fsidnode);
    std::string data;
    ASSERT_TRUE(l.serialize(&data));
#ifndef WIN32    
    ASSERT_EQ(43u, data.size());
#endif
    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, l);
}

TEST(Serialization, LocalNode_forFile_withoutNode)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FILENODE, "sweet");
    l->mSyncable = false;
    l->size = 124;
    l->setfsid(10, client.cli->fsidnode);
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    l->mtime = 124124124;
    std::iota(l->crc.begin(), l->crc.end(), 1);
    std::string data;
    ASSERT_TRUE(l->serialize(&data));
#ifndef WIN32
    ASSERT_EQ(63u, data.size());
#endif
    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}

TEST(Serialization, LocalNode_forFile_withoutNode_withMaxMtime)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FILENODE, "sweet");
    l->size = 124;
    l->setfsid(10, client.cli->fsidnode);
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    l->mtime = std::numeric_limits<decltype(l->mtime)>::max();
    std::iota(l->crc.begin(), l->crc.end(), 1);
    std::string data;
    ASSERT_TRUE(l->serialize(&data));
#ifndef WIN32
    ASSERT_EQ(67u, data.size());
#endif
    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}

TEST(Serialization, LocalNode_forFolder_withoutParent)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto& l = *sync->localroot;
    l.setfsid(10, client.cli->fsidnode);
    std::string data;
    ASSERT_TRUE(l.serialize(&data));
#ifndef WIN32
    ASSERT_EQ(43u, data.size());
#endif
    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, l);
}

TEST(Serialization, LocalNode_forFolder)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FOLDERNODE, "sweet");
    l->mSyncable = false;
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    auto& n = mt::makeNode(*client.cli, mega::FOLDERNODE, 42);
    l->setfsid(10, client.cli->fsidnode);
    l->node = &n;
    std::string data;
    ASSERT_TRUE(l->serialize(&data));
#ifndef WIN32
    ASSERT_EQ(42u, data.size());
#endif
    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}

#ifndef WIN32
TEST(Serialization, LocalNode_forFolder_32bit)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FOLDERNODE, "sweet");
    l->mSyncable = false;
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    auto& n = mt::makeNode(*client.cli, mega::FOLDERNODE, 42);
    l->setfsid(10, client.cli->fsidnode);
    l->node = &n;

    // This is the result of serialization on 32bit Windows
    const std::array<unsigned char, 42> rawData = {
        0xff, 0xff, 0xff,
        0xff, 0xff, 0xff,
        0xff, 0xff, 0x0a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x05, 0x00, 0x73, 0x77, 0x65, 0x65, 0x74, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const std::string data(reinterpret_cast<const char*>(rawData.data()), rawData.size());

    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}

TEST(Serialization, LocalNode_forFolder_oldLocalNodeWithoutSyncable)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FOLDERNODE, "sweet");
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    auto& n = mt::makeNode(*client.cli, mega::FOLDERNODE, 42);
    l->setfsid(10, client.cli->fsidnode);
    l->node = &n;

    // This array represents an old LocalNode without extension bytes
    const std::array<unsigned char, 33> rawData = {
      0xff, 0xff, 0xff,
      0xff, 0xff, 0xff,
      0xff, 0xff, 0x0a, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x05, 0x00, 0x73, 0x77, 0x65, 0x65, 0x74
    };
    const std::string data(reinterpret_cast<const char*>(rawData.data()), rawData.size());

    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}

TEST(Serialization, LocalNode_forFile)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FILENODE, "sweet");
    l->mSyncable = false;
    auto& n = mt::makeNode(*client.cli, mega::FILENODE, 42);
    l->node = &n;
    l->size = 1;
    l->setfsid(10, client.cli->fsidnode);
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    l->mtime = 0;
    std::iota(l->crc.begin(), l->crc.end(), 1);
    std::string data;
    ASSERT_TRUE(l->serialize(&data));
    ASSERT_EQ(59u, data.size());
    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}

TEST(Serialization, LocalNode_forFiles_oldLocalNodeWithoutSyncable)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FILENODE, "sweet");
    auto& n = mt::makeNode(*client.cli, mega::FILENODE, 42);
    l->node = &n;
    l->size = 1;
    l->setfsid(10, client.cli->fsidnode);
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    l->mtime = 0;
    std::iota(l->crc.begin(), l->crc.end(), 1);

    // This array represents an old LocalNode without syncable flag
    const std::array<char, 50> rawData = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x05, 0x00, 0x73, 0x77, 0x65, 0x65, 0x74, 0x01, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
        0x00, 0x00
    };
    const std::string data(rawData.data(), rawData.size());

    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}

TEST(Serialization, LocalNode_forFile_32bit)
{
    MockClient client;
    auto sync = mt::makeSync(*client.cli, "wicked");
    auto l = mt::makeLocalNode(*sync, *sync->localroot, mega::FILENODE, "sweet");
    l->mSyncable = false;
    auto& n = mt::makeNode(*client.cli, mega::FILENODE, 42);
    l->node = &n;
    l->size = 1;
    l->setfsid(10, client.cli->fsidnode);
    l->parent->dbid = 13;
    l->parent_dbid = l->parent->dbid;
    l->mtime = 0;
    std::iota(l->crc.begin(), l->crc.end(), 1);

    // This is the result of serialization on 32bit Windows
    const std::array<char, 59> rawData = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x05, 0x00, 0x73, 0x77, 0x65, 0x65, 0x74, 0x01, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const std::string data(rawData.data(), rawData.size());

    std::unique_ptr<mega::LocalNode> dl{mega::LocalNode::unserialize(sync.get(), &data)};
    checkDeserializedLocalNode(*dl, *l);
}
#endif

#endif

namespace {

void checkDeserializedNode(const mega::Node& dl, const mega::Node& ref, bool ignore_fileattrstring = false)
{
    ASSERT_EQ(ref.type, dl.type);
    ASSERT_EQ(ref.size, dl.size);
    ASSERT_EQ(ref.nodehandle, dl.nodehandle);
    ASSERT_EQ(ref.parenthandle, dl.parenthandle);
    ASSERT_EQ(ref.owner, dl.owner);
    ASSERT_EQ(ref.ctime, dl.ctime);
    ASSERT_EQ(ref.nodekey(), dl.nodekey());
    ASSERT_EQ(ignore_fileattrstring ? "" : ref.fileattrstring, dl.fileattrstring);
    ASSERT_EQ(ref.attrs.map, dl.attrs.map);
    if (ref.plink)
    {
        ASSERT_NE(nullptr, dl.plink);
        ASSERT_EQ(ref.plink->ph, dl.plink->ph);
        ASSERT_EQ(ref.plink->cts, dl.plink->cts);
        ASSERT_EQ(ref.plink->ets, dl.plink->ets);
        ASSERT_EQ(ref.plink->takendown, dl.plink->takendown);
    }
    // TODO: deal with shares
}

}

TEST(Serialization, Node_whenNodeIsEncrypted)
{
    MockClient client;
    auto& n = mt::makeNode(*client.cli, mega::FILENODE, 42);
    n.attrstring.reset(new std::string);
    std::string data;
    ASSERT_FALSE(n.serialize(&data));
}

TEST(Serialization, Node_whenTypeIsUnsupported)
{
    MockClient client;
    auto& n = mt::makeNode(*client.cli, mega::TYPE_UNKNOWN, 42);
    std::string data;
    ASSERT_FALSE(n.serialize(&data));
}

TEST(Serialization, Node_forFile_withoutParent_withoutShares_withoutAttrs_withoutFileAttrString_withoutPlink)
{
    MockClient client;
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FILENODE, 42)};
    n->size = 12;
    n->owner = 43;
    n->ctime = 44;
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(90u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFolder_withoutParent_withoutShares_withoutAttrs_withoutFileAttrString_withoutPlink)
{
    MockClient client;
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FOLDERNODE, 42)};
    n->size = -1;
    n->owner = 43;
    n->ctime = 44;
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(71u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFile_withoutShares_withoutAttrs_withoutFileAttrString_withoutPlink)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FILENODE, 42, &parent)};
    n->size = 12;
    n->owner = 88;
    n->ctime = 44;
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(90u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFile_withoutShares_withoutFileAttrString_withoutPlink)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FILENODE, 42, &parent)};
    n->size = 12;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(104u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFile_withoutShares_withoutPlink)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FILENODE, 42, &parent)};
    n->size = 12;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    n->fileattrstring = "blah";
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(108u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFile_withoutShares)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FILENODE, 42, &parent)};
    n->size = 12;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    n->fileattrstring = "blah";
    n->plink = new mega::PublicLink{n->nodehandle, 1, 2, false};
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(131u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFile_withoutShares_32bit)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FILENODE, 42, &parent)};
    n->size = 12;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    n->fileattrstring = "blah";
    n->plink = new mega::PublicLink{n->nodehandle, 1, 2, false};

    // This is the result of serialization on 32bit Windows
    const std::array<char, 131> rawData = {
        0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x58, 0x58, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x05, 0x00, 0x62, 0x6c, 0x61, 0x68, 0x00, 0x01,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x65, 0x03,
        0x00, 0x66, 0x6f, 0x6f, 0x01, 0x66, 0x03, 0x00, 0x62, 0x61, 0x72, 0x00,
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const std::string data(rawData.data(), rawData.size());

    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFolder_withoutShares_withoutAttrs_withoutFileAttrString_withoutPlink)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FOLDERNODE, 42, &parent)};
    n->size = -1;
    n->owner = 88;
    n->ctime = 44;
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(71u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFolder_withoutShares_withoutFileAttrString_withoutPlink)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FOLDERNODE, 42, &parent)};
    n->size = -1;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(85u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n);
}

TEST(Serialization, Node_forFolder_withoutShares_withoutPlink)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FOLDERNODE, 42, &parent)};
    n->size = -1;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    n->fileattrstring = "blah";
    std::string data;
    ASSERT_TRUE(n->serialize(&data));
    ASSERT_EQ(85u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n, true);
}

TEST(Serialization, Node_forFolder_withoutShares)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FOLDERNODE, 42, &parent)};
    n->size = -1;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    n->fileattrstring = "blah";
    n->plink = new mega::PublicLink{n->nodehandle, 1, 2, false};
    std::string data;
    ASSERT_TRUE(n->serialize(&data));

    ASSERT_EQ(108u, data.size());
    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n, true);
}

TEST(Serialization, Node_forFolder_withoutShares_32bit)
{
    MockClient client;
    auto& parent = mt::makeNode(*client.cli, mega::FOLDERNODE, 43);
    std::unique_ptr<mega::Node> n{&mt::makeNode(*client.cli, mega::FOLDERNODE, 42, &parent)};
    n->size = -1;
    n->owner = 88;
    n->ctime = 44;
    n->attrs.map = {
        {101, "foo"},
        {102, "bar"},
    };
    n->fileattrstring = "blah";
    n->plink = new mega::PublicLink{n->nodehandle, 1, 2, false};

    // This is the result of serialization on 32bit Windows
    const std::array<unsigned char, 108> rawData = {
        0xff, 0xff, 0xff,
        0xff, 0xff, 0xff,
        0xff, 0xff, 0x2a, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x58, 0x58, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
        0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x65,
        0x03, 0x00, 0x66, 0x6f, 0x6f, 0x01, 0x66, 0x03, 0x00, 0x62, 0x61, 0x72,
        0x00, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const std::string data(reinterpret_cast<const char*>(rawData.data()), rawData.size());

    mega::node_vector dp;
    auto dn = mega::Node::unserialize(client.cli.get(), &data, &dp);
    checkDeserializedNode(*dn, *n, true);
}
