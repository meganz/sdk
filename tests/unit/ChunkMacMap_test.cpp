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

#include <gtest/gtest.h>

#include <mega/utils.h>

namespace mega {

bool operator==(const mega::ChunkMAC& lhs, const mega::ChunkMAC& rhs)
{
    return std::equal(lhs.mac, lhs.mac + mega::SymmCipher::BLOCKSIZE, rhs.mac)
            && lhs.offset == rhs.offset && lhs.finished == rhs.finished;
}

}

TEST(ChunkMacMap, serialize_unserialize)
{
    mega::ChunkMAC chunkMac1;
    std::fill(chunkMac1.mac, chunkMac1.mac + mega::SymmCipher::BLOCKSIZE, 'X');
    chunkMac1.offset = 13;
    chunkMac1.finished = true;

    mega::ChunkMAC chunkMac2;
    std::fill(chunkMac2.mac, chunkMac2.mac + mega::SymmCipher::BLOCKSIZE, 'Y');
    chunkMac2.offset = 14;
    chunkMac2.finished = false;

    mega::chunkmac_map map;
    map[1] = chunkMac1;
    map[2] = chunkMac2;

    std::string d;
    map.serialize(d);

    mega::chunkmac_map newMap;
    auto data = d.c_str();
    ASSERT_TRUE(newMap.unserialize(data, d.c_str() + d.size()));
    EXPECT_EQ(map, newMap);
}

TEST(ChunkMacMap, unserialize_32bit)
{
    mega::ChunkMAC chunkMac1;
    std::fill(chunkMac1.mac, chunkMac1.mac + mega::SymmCipher::BLOCKSIZE, 'X');
    chunkMac1.offset = 13;
    chunkMac1.finished = true;

    mega::ChunkMAC chunkMac2;
    std::fill(chunkMac2.mac, chunkMac2.mac + mega::SymmCipher::BLOCKSIZE, 'Y');
    chunkMac2.offset = 14;
    chunkMac2.finished = false;

    mega::chunkmac_map map;
    map[1] = chunkMac1;
    map[2] = chunkMac2;

    // This is the result of serialization on 32bit Windows
    const std::array<unsigned char, 66> rawData = {
        0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
        0x58, 0x58, 0x0d, 0x00, 0x00, 0x00, 0x01, 0xcc,
        0xcc, 0xcc, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59,
        0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x0e, 0x00,
        0x00, 0x00, 0x00, 0xcc, 0xcc,
        0xcc
    };
    const std::string d(reinterpret_cast<const char*>(rawData.data()), rawData.size());

    mega::chunkmac_map newMap;
    auto data = d.c_str();
    ASSERT_TRUE(newMap.unserialize(data, d.c_str() + d.size()));
    EXPECT_EQ(map, newMap);
}
