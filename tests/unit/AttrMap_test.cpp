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

TEST(AttrMap, getjsonSkipsTheEmptyNameid)
{
    // string2nameid("") is 0, which is also EOO, so such an entry has no key that could be
    // written. It used to still emit the separator and the opening quote of a key that never
    // followed, which corrupted every attribute of the object, not only this one. 0 sorts first,
    // so this also pins that the attribute actually written first gets no leading comma
    AttrMap withEmptyNameid;
    withEmptyNameid.map[0] = "no name";
    withEmptyNameid.map[AttrMap::string2nameid("a")] = "kept";

    EXPECT_EQ(withEmptyNameid.getjson(), R"("a":"kept")");

    AttrMap parsed;
    parsed.fromjson(withEmptyNameid.getjson().c_str());
    EXPECT_EQ(parsed.map, toAttrMap({{"a", "kept"}}).map);

    AttrMap onlyEmptyNameid;
    onlyEmptyNameid.map[0] = "no name";
    EXPECT_EQ(onlyEmptyNameid.getjson(), "");
}

TEST(AttrMap, getjsonSkipsNamesThatCannotBeAJsonKey)
{
    // Such a name can no longer be stored through MegaApi, but it still reaches an AttrMap by
    // being parsed back from a node that an older or a different client wrote, and writing it out
    // again would corrupt every attribute of that node. string2nameid("_#\xc3\xa9") is a real
    // example: sign extension borrows from the '#' and turns it into a '"'
    const nameid signExtended = AttrMap::string2nameid("_#\xc3\xa9");
    ASSERT_EQ(AttrMap::nameid2string(signExtended), "_\"\xc2\xa9") << "test premise changed";

    AttrMap withBrokenNames;
    withBrokenNames.map[AttrMap::string2nameid("\"")] = "dropped, sorts first";
    withBrokenNames.map[AttrMap::string2nameid("a")] = "kept";
    withBrokenNames.map[AttrMap::string2nameid("n")] = "name.txt";
    withBrokenNames.map[signExtended] = "dropped, sorts last";

    // dropping the first and the last entry must leave no dangling separator either
    const std::string json = withBrokenNames.getjson();
    EXPECT_EQ(json, R"("a":"kept","n":"name.txt")");

    // and what was written has to parse back, which a raw broken key would not
    AttrMap parsed;
    parsed.fromjson(json.c_str());
    EXPECT_EQ(parsed.map, toAttrMap({{"a", "kept"}, {"n", "name.txt"}}).map);
}

TEST(AttrMap, getJsonObjectSkipsNamesThatCannotBeAJsonKey)
{
    AttrMap withBrokenNames;
    withBrokenNames.map[0] = "no name at all";
    withBrokenNames.map[AttrMap::string2nameid("\"")] = "dropped";
    withBrokenNames.map[AttrMap::string2nameid("a")] = "kept";

    const std::string json = withBrokenNames.getJsonObject();
    EXPECT_EQ(json, R"({"a":"kept"})");

    AttrMap parsed;
    parsed.fromjsonObject(json);
    EXPECT_EQ(parsed.map, toAttrMap({{"a", "kept"}}).map);
}

TEST(AttrMap, serializersWriteNothingWhenNoNameIsUsable)
{
    // Whatever gets dropped, what comes out has to stay well formed: no lone separator from
    // getjson(), and no bare "{}" from getJsonObject(), which returns nothing for an empty map too
    AttrMap onlyBrokenNames;
    onlyBrokenNames.map[0] = "no name at all";
    onlyBrokenNames.map[AttrMap::string2nameid("\"")] = "dropped";

    EXPECT_EQ(onlyBrokenNames.getjson(), "");
    EXPECT_EQ(onlyBrokenNames.getJsonObject(), "");

    const AttrMap empty;
    EXPECT_EQ(empty.getjson(), "");
    EXPECT_EQ(empty.getJsonObject(), "");
}

TEST(AttrMap, isValidJsonKeyAcceptsUsableNames)
{
    for (const std::string& name: {std::string("a"),
                                   std::string("_custom1"),
                                   std::string("12345678"),
                                   std::string("a b"),
                                   std::string("_~!@#$%")})
    {
        EXPECT_TRUE(AttrMap::isValidJsonKey(name)) << "rejected a usable name: " << name;
    }
}

TEST(AttrMap, isValidJsonKeyRejectsNamesThatWouldCorruptTheJson)
{
    // A key is written without escaping and read back without unescaping
    EXPECT_FALSE(AttrMap::isValidJsonKey(R"(a"b)"));
    EXPECT_FALSE(AttrMap::isValidJsonKey(R"(a\b)"));
    EXPECT_FALSE(AttrMap::isValidJsonKey("a\tb"));
    EXPECT_FALSE(AttrMap::isValidJsonKey(std::string("a\0b", 3)));

    // A nameid holds at most 8 bytes
    EXPECT_FALSE(AttrMap::isValidJsonKey(""));
    EXPECT_FALSE(AttrMap::isValidJsonKey("123456789"));

    // string2nameid() sign extends bytes above 0x7F, which borrows from the byte before. Here
    // that turns '#' into '"' and ']' into '\', so the key would hold the very characters that
    // the checks above reject, even though the name itself holds neither of them
    EXPECT_FALSE(AttrMap::isValidJsonKey("_#\xc3\xa9"));
    EXPECT_FALSE(AttrMap::isValidJsonKey("_]\xc3\xa9"));
    EXPECT_FALSE(AttrMap::isValidJsonKey("_\xc3\xa9"));
}

TEST(AttrMap, isValidJsonKeyRejectsEverythingAboveAscii)
{
    // A byte above 0x7F usually does not survive string2nameid(), but a name of exactly 8 bytes
    // is the exception: the borrow taken from its first byte is shifted out of the nameid, so
    // such a name does round trip unchanged. The key would still not be valid UTF-8, which a
    // strict JSON parser expects of a string, so the whole range is turned down explicitly
    EXPECT_FALSE(AttrMap::isValidJsonKey("\x98.nnl.@7"));
    EXPECT_FALSE(AttrMap::isValidJsonKey("\x84&@vzD>b"));

    // and the ordinary cases, where the round trip would have caught them anyway
    EXPECT_FALSE(AttrMap::isValidJsonKey("caf\xc3\xa9"));
    EXPECT_FALSE(AttrMap::isValidJsonKey("\xff"));
}

TEST(AttrMap, isValidJsonKeyRejectsDel)
{
    // DEL would not break the JSON, it needs no escaping there, but the public API promises
    // printable names and DEL is not printable
    EXPECT_FALSE(AttrMap::isValidJsonKey("_\x7f"));
    EXPECT_FALSE(AttrMap::isValidJsonKey("_a\x7f"
                                         "b"));
}

TEST(AttrMap, isValidJsonKeyDetectsKeysChangedBySignExtension)
{
    // The JSON key of an accepted name has to be the name itself. Parsing such a JSON back is
    // not attempted here, because a broken key desynchronizes JSON::getnameid() and trips its
    // assert, which is exactly what the check is there to prevent
    for (const std::string& name: {std::string("a"),
                                   std::string("_custom1"),
                                   std::string("_#\xc3\xa9"),
                                   std::string("_]\xc3\xa9"),
                                   std::string("_\xc3\xa9")})
    {
        AttrMap withOneAttribute;
        withOneAttribute.map[AttrMap::string2nameid(name)] = "value";

        const std::string expected = "\"" + name + "\":\"value\"";
        EXPECT_EQ(AttrMap::isValidJsonKey(name), withOneAttribute.getjson() == expected)
            << "disagreement for name: " << name;
    }
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
