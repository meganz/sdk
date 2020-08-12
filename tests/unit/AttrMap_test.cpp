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

#include <mega/attrmap.h>
#include <map>
#include <string>

TEST(AttrMap, serialize_unserialize)
{
    mega::AttrMap map;
    map.map = std::map<mega::nameid, std::string>{
        {13, "foo"},
        {42, "blah"},
    };

    std::string d;
    map.serialize(&d);

    mega::AttrMap newMap;
    newMap.unserialize(d.c_str(), d.c_str() + d.size());

    ASSERT_EQ(map.map, newMap.map);
}

#ifndef WIN32   // data was recorded with "mock" utf-8 not the actual utf-16
TEST(AttrMap, unserialize_32bit)
{
    // This is the result of serialization on 32bit Windows
    const std::array<char, 16> rawData = {
        0x01, 0x0d, 0x03, 0x00, 0x66, 0x6f, 0x6f, 0x01, 0x2a, 0x04, 0x00, 0x62,
        0x6c, 0x61, 0x68, 0x00
    };
    const std::string d(rawData.data(), rawData.size());

    mega::AttrMap expMap;
    expMap.map = std::map<mega::nameid, std::string>{
        {13, "foo"},
        {42, "blah"},
    };

    mega::AttrMap newMap;
    newMap.unserialize(d.c_str(), d.c_str() + d.size());

    ASSERT_EQ(expMap.map, newMap.map);
}
#endif