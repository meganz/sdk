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

#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <vector>

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

// Values that exercise every branch of JSON::escape(): the characters it turns into a two
// character escape, the ones it turns into a \uXXXX escape, and the bytes it has to pass through
// untouched. The last ones are not a theoretical case, a node name taken from a filesystem that
// is not UTF-8 really does reach getjson() as raw bytes
std::vector<std::string> valuesWithSpecialCharacters()
{
    return {
        "plain",
        R"(a "quoted" value)",
        R"(back\slash)",
        "line\nbreak\ttab",
        "\b\f\r",
        "\x01\x1f", // control characters with no short escape, written as \u0001 and \u001f
        std::string("nul\0byte", 8),
        "del\x7f-here", // DEL is not escaped by JSON and has to survive verbatim
        "caf\xc3\xa9", // valid multi-byte UTF-8, passed through as is
        "\xff\xfe raw", // not valid UTF-8 at all, also passed through as is
        "",
    };
}

// Keeps a failure message readable, and complete, for the values above
std::string describeValue(const std::string& value)
{
    std::string out;
    for (const char c: value)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= ' ' && byte < 0x7F)
        {
            out += c;
            continue;
        }

        char buf[5];
        snprintf(buf, sizeof(buf), "\\x%02x", byte);
        out += buf;
    }
    return out;
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

TEST(AttrMap, getjson_fromjson_roundTripsSpecialCharacters)
{
    for (const std::string& value: valuesWithSpecialCharacters())
    {
        const auto original = toAttrMap({{"a", value}, {"b", "sibling"}});

        const std::string json = original.getjson();

        AttrMap parsed;
        parsed.fromjson(json.c_str());

        EXPECT_EQ(parsed.map, original.map)
            << "roundtrip failed for value: " << describeValue(value);
        // The sibling attribute must remain intact: an unescaped '"'/'\' in "a" would otherwise
        // desynchronize parsing of "b" too.
        EXPECT_EQ(parsed.getString("b"), std::make_optional(std::string("sibling")))
            << "sibling lost for value: " << describeValue(value);
    }
}

TEST(AttrMap, getJsonObject_fromjsonObject_roundTripsSpecialCharacters)
{
    for (const std::string& value: valuesWithSpecialCharacters())
    {
        const auto original = toAttrMap({{"a", value}, {"b", "sibling"}});

        const std::string json = original.getJsonObject();

        AttrMap parsed;
        parsed.fromjsonObject(json);

        EXPECT_EQ(parsed.map, original.map)
            << "roundtrip failed for value: " << describeValue(value);
        EXPECT_EQ(parsed.getString("b"), std::make_optional(std::string("sibling")))
            << "sibling lost for value: " << describeValue(value);
    }
}

TEST(AttrMap, getjsonEscapesValuesExactly)
{
    // Pins the bytes that go on the wire. A round trip on its own would still pass if the writing
    // and the reading side ever changed together, and these bytes are read by other clients
    EXPECT_EQ(toJson({{"a", "plain"}}), R"("a":"plain")");
    // This one expectation is spelled out with escapes instead of a raw string: MSVC's
    // preprocessor mis-lexes a raw string that holds a backslash immediately before a quote and
    // rejects the line with C2017
    EXPECT_EQ(toJson({{"a", "\""}}), "\"a\":\"\\\"\"");
    EXPECT_EQ(toJson({{"a", "\\"}}), R"("a":"\\")");
    EXPECT_EQ(toJson({{"a", "\b\f\n\r\t"}}), R"("a":"\b\f\n\r\t")");
    EXPECT_EQ(toJson({{"a", "\x01\x1f"}}), R"("a":"\u0001\u001f")");
    EXPECT_EQ(toJson({{"a", std::string("\0", 1)}}), R"("a":"\u0000")");

    // DEL needs no escaping in JSON, and everything above it is passed through byte for byte,
    // whether it is valid UTF-8 or not
    EXPECT_EQ(toJson({{"a", "\x7f"}}), "\"a\":\"\x7f\"");
    EXPECT_EQ(toJson({{"a", "caf\xc3\xa9"}}), "\"a\":\"caf\xc3\xa9\"");
    EXPECT_EQ(toJson({{"a", "\xff\xfe"}}), "\"a\":\"\xff\xfe\"");

    // and the separator between two attributes
    EXPECT_EQ(toJson({{"a", "1"}, {"b", "2"}}), R"("a":"1","b":"2")");
}

TEST(AttrMap, getJsonObjectEscapesValuesExactly)
{
    // escaped rather than raw for the same reason as in getjsonEscapesValuesExactly
    EXPECT_EQ(toJsonObject({{"a", "\""}, {"b", "2"}}), "{\"a\":\"\\\"\",\"b\":\"2\"}");
    EXPECT_EQ(toJsonObject({{"a", "\x01"}}), R"({"a":"\u0001"})");
    EXPECT_EQ(toJsonObject({{"a", "caf\xc3\xa9"}}), "{\"a\":\"caf\xc3\xa9\"}");
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
