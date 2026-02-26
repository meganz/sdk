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
#include <string_view>

using namespace mega;

AttrMap toAttrMap(const std::map<std::string_view, std::string>& m)
{
    AttrMap result;
    for (const auto& [k, v]: m)
        result.map[AttrMap::string2nameid(k)] = v;
    return result;
}

std::string toJson(const std::map<std::string_view, std::string>& m)
{
    return toAttrMap(m).getjson();
}

std::string toJsonObject(const std::map<std::string_view, std::string>& m)
{
    return toAttrMap(m).getJsonObject();
}

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

TEST(AttrMap, applyUpdates)
{
    auto baseMap =
        toAttrMap({{"a", "hello"}, {"b", "world"}, {"n", toJson({{"a", "hi"}, {"b", "foo"}})}});
    const auto updateMap = toAttrMap(
        {{"a", ""}, {"b", "hello"}, {"c", "world"}, {"n", toJson({{"a", ""}, {"c", "hi"}})}});
    baseMap.applyUpdates(updateMap.map);
    const auto expected =
        toAttrMap({{"b", "hello"}, {"c", "world"}, {"n", toJson({{"a", ""}, {"c", "hi"}})}});
    EXPECT_EQ(baseMap.map, expected.map);
}

TEST(AttrMap, applyUpdatesWithNestedFields)
{
    using namespace std::literals;
    auto baseMap = toAttrMap(
        {{"a", "hello"}, {"b", "world"}, {"n", toJsonObject({{"a", "hi"}, {"b", "foo"}})}});
    auto updateMap = toAttrMap(
        {{"a", ""}, {"b", "hello"}, {"c", "world"}, {"n", toJsonObject({{"a", ""}, {"c", "hi"}})}});
    baseMap.applyUpdatesWithNestedFields(updateMap, std::array{"n"sv});
    auto expected = toAttrMap(
        {{"b", "hello"}, {"c", "world"}, {"n", toJsonObject({{"b", "foo"}, {"c", "hi"}})}});
    EXPECT_EQ(baseMap.map, expected.map);

    // Now remove the nested field
    updateMap = toAttrMap({{"n", ""}});
    baseMap.applyUpdatesWithNestedFields(updateMap, std::array{"n"sv});
    expected = toAttrMap({{"b", "hello"}, {"c", "world"}});
    EXPECT_EQ(baseMap.map, expected.map);
}
