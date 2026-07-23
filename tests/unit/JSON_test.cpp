/**
 * (c) 2026 by Mega Limited, New Zealand
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

#include <mega.h>
#include <string>

namespace
{

// Run JSON::unescape on a copy of the input and return the decoded result.
std::string unescaped(std::string s)
{
    mega::JSON::unescape(&s);
    return s;
}

} // namespace

//
// Acceptance tests: well-formed input must be decoded correctly.
//

TEST(JSONUnescape, EmptyStringStaysEmpty)
{
    EXPECT_EQ(unescaped(""), "");
}

TEST(JSONUnescape, StringWithoutEscapesIsUnchanged)
{
    EXPECT_EQ(unescaped("hello world"), "hello world");
    EXPECT_EQ(unescaped("no/escapes:here?!"), "no/escapes:here?!");
}

TEST(JSONUnescape, DecodesSimpleControlEscapes)
{
    EXPECT_EQ(unescaped("\\n"), "\n");
    EXPECT_EQ(unescaped("\\r"), "\r");
    EXPECT_EQ(unescaped("\\b"), "\b");
    EXPECT_EQ(unescaped("\\f"), "\f");
    EXPECT_EQ(unescaped("\\t"), "\t");
    EXPECT_EQ(unescaped("\\\\"), "\\");
}

TEST(JSONUnescape, DecodesControlEscapesEmbeddedInText)
{
    EXPECT_EQ(unescaped("line1\\nline2\\ttabbed"), "line1\nline2\ttabbed");
    EXPECT_EQ(unescaped("a\\\\b"), "a\\b");
}

TEST(JSONUnescape, UnknownEscapeKeepsEscapedCharacter)
{
    // No dedicated case: the backslash is dropped and the next char is kept.
    EXPECT_EQ(unescaped("\\\""), "\"");
    EXPECT_EQ(unescaped("\\/"), "/");
    EXPECT_EQ(unescaped("\\x"), "x");
    EXPECT_EQ(unescaped("a\\qb"), "aqb");
}

TEST(JSONUnescape, DecodesAsciiUnicodeEscape)
{
    EXPECT_EQ(unescaped("\\u0041"), "A");
    EXPECT_EQ(unescaped("\\u007e"), "~");
}

TEST(JSONUnescape, DecodesNulUnicodeEscape)
{
    EXPECT_EQ(unescaped("\\u0000"), std::string(1, '\0'));
}

TEST(JSONUnescape, DecodesTwoByteUtf8FromUnicodeEscape)
{
    // é
    EXPECT_EQ(unescaped("\\u00e9"), "\xC3\xA9");
    // First 2-byte UTF-8
    EXPECT_EQ(unescaped("\\u0080"), "\xC2\x80");
    // Last 2-byte UTF-8
    EXPECT_EQ(unescaped("\\u07ff"), "\xDF\xBF");
}

TEST(JSONUnescape, DecodesThreeByteUtf8FromUnicodeEscape)
{
    // First 3-byte UTF-8
    EXPECT_EQ(unescaped("\\u0800"), "\xE0\xA0\x80");
    // Euro sign
    EXPECT_EQ(unescaped("\\u20ac"), "\xE2\x82\xAC");
    // Last BMP
    EXPECT_EQ(unescaped("\\uffff"), "\xEF\xBF\xBF");
}

TEST(JSONUnescape, UnicodeEscapeHexIsCaseInsensitive)
{
    EXPECT_EQ(unescaped("\\u00E9"), "\xC3\xA9");
    EXPECT_EQ(unescaped("\\u00eF"), unescaped("\\u00ef"));
    EXPECT_EQ(unescaped("\\uABcd"), unescaped("\\uabCD"));
}

TEST(JSONUnescape, DecodesAllFourHexDigitsNotOnlyLowByte)
{
    // The full 16-bit value must be used: \u1234 must differ from \u0034,
    // which share the same low byte (0x34).
    EXPECT_EQ(unescaped("\\u0034"), "4");
    EXPECT_EQ(unescaped("\\u1234"), "\xE1\x88\xB4");
    EXPECT_NE(unescaped("\\u1234"), unescaped("\\u0034"));
}

TEST(JSONUnescape, DecodedBackslashIsNotReinterpreted)
{
    // \u005c decodes to a single backslash which must not start a new escape.
    EXPECT_EQ(unescaped("\\u005c"), "\\");
    // \u005c followed by 'n' must be backslash + 'n', not a newline.
    EXPECT_EQ(unescaped("\\u005cn"), "\\n");
}

TEST(JSONUnescape, DecodesEscapesAtStringBoundaries)
{
    EXPECT_EQ(unescaped("\\nstart"), "\nstart");
    EXPECT_EQ(unescaped("mid\\ndle"), "mid\ndle");
    EXPECT_EQ(unescaped("end\\t"), "end\t");
    EXPECT_EQ(unescaped("\\u0041bc"), "Abc");
    EXPECT_EQ(unescaped("ab\\u0041"), "abA");
}

TEST(JSONUnescape, DecodesConsecutiveAndMixedEscapes)
{
    EXPECT_EQ(unescaped("\\u00e9\\u00e9"), "\xC3\xA9\xC3\xA9");
    EXPECT_EQ(unescaped("a\\u0042c\\nd"), "aBc\nd");
    EXPECT_EQ(unescaped("\\t\\u20ac\\t"), "\t\xE2\x82\xAC\t");
}

//
// Negative tests: malformed / adversarial input must never read out of bounds
// and must degrade gracefully (the security fix for SDK-6267).
//

TEST(JSONUnescape, TrailingUnicodeEscapeDoesNotOverread)
{
    // Reproduces the reported PoC: a string whose last two bytes are backslash-u.
    // The '\u' handler used to read up to index size+3. The sequence must now
    // degrade to a literal 'u' with no out-of-bounds read.
    for (size_t n: {6u, 7u, 8u, 18u, 20u, 24u, 30u})
    {
        const std::string prefix(n - 2, 'A');
        EXPECT_EQ(unescaped(prefix + "\\u"), prefix + "u") << "size=" << n;
    }
}

TEST(JSONUnescape, TruncatedUnicodeEscapeDegradesToLiteral)
{
    // Fewer than four hex digits available after \u: drop the backslash, keep
    // 'u', and leave the remaining characters untouched.
    EXPECT_EQ(unescaped("\\u"), "u");
    EXPECT_EQ(unescaped("\\u1"), "u1");
    EXPECT_EQ(unescaped("\\u12"), "u12");
    EXPECT_EQ(unescaped("\\u123"), "u123");
}

TEST(JSONUnescape, UnicodeEscapeOffByOneBoundary)
{
    // Exactly at the boundary: "\u123" (5 chars) has no fourth hex digit and
    // must degrade, while "\u1234" (6 chars) is fully decoded.
    EXPECT_EQ(unescaped("\\u123"), "u123");
    EXPECT_EQ(unescaped("\\u1234"), "\xE1\x88\xB4");
}

TEST(JSONUnescape, NonHexUnicodeEscapeDegradesToLiteral)
{
    EXPECT_EQ(unescaped("\\uZZZZ"), "uZZZZ");
    EXPECT_EQ(unescaped("\\uG000"), "uG000");
    EXPECT_EQ(unescaped("\\u12XY"), "u12XY");
    // A non-hex digit in the last (boundary) position must also degrade.
    EXPECT_EQ(unescaped("\\u123g"), "u123g");
    // Space is not a hex digit.
    EXPECT_EQ(unescaped("\\u00 0"), "u00 0");
}

TEST(JSONUnescape, TrailingBackslashIsPreserved)
{
    // The loop guard (i + 1 < size) never processes a backslash in the final
    // position, so a lone trailing backslash is left as-is (no over-read).
    EXPECT_EQ(unescaped("\\"), "\\");
    EXPECT_EQ(unescaped("a\\"), "a\\");
    EXPECT_EQ(unescaped("abc\\"), "abc\\");
}

TEST(JSONUnescape, EscapedBackslashBeforeUIsNotDecodedAsUnicode)
{
    // "\\u" is an escaped backslash followed by a literal 'u', which must not be
    // treated as the start of a \u sequence.
    EXPECT_EQ(unescaped("\\\\u"), "\\u");
    EXPECT_EQ(unescaped("\\\\u0041"), "\\u0041");
}

TEST(JSONUnescape, RunsOfBackslashesCollapseInPairs)
{
    // Each "\\" collapses to a single backslash: 2m backslashes -> m, and an odd
    // run leaves the unpaired trailing backslash (guarded by i + 1 < size).
    EXPECT_EQ(unescaped(std::string(32, '\\')), std::string(16, '\\'));
    EXPECT_EQ(unescaped(std::string(31, '\\')), std::string(16, '\\'));
    // A long run followed by 'u' must not turn the paired-off tail into a \u
    // escape: pairs collapse, then the literal 'u' remains.
    EXPECT_EQ(unescaped(std::string(32, '\\') + "u"), std::string(16, '\\') + "u");
    // Odd run before 'u': the last backslash pairs with 'u' as a truncated \u,
    // which degrades to a literal 'u'.
    EXPECT_EQ(unescaped(std::string(33, '\\') + "u"), std::string(16, '\\') + "u");
}

TEST(JSONUnescape, LoneSurrogateIsEncodedAsThreeBytesNonStrict)
{
    // A lone UTF-16 surrogate is invalid in strict JSON. This non-strict decoder
    // encodes the raw code unit as a three-byte sequence (WTF-8) rather than
    // failing; this test locks that documented behavior.
    EXPECT_EQ(unescaped("\\ud83d"), "\xED\xA0\xBD");
}
