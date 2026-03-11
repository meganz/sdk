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

TEST(JSONNumericParsers, GetFloatParsesDecimalNumbers)
{
    mega::JSON integerJson("25");
    mega::JSON decimalJson("25.99");
    mega::JSON negativeJson("-12.5");
    mega::JSON leadingDotJson(".5");

    EXPECT_DOUBLE_EQ(integerJson.getfloat(), 25.0);
    EXPECT_DOUBLE_EQ(decimalJson.getfloat(), 25.99);
    EXPECT_DOUBLE_EQ(negativeJson.getfloat(), -12.5);
    EXPECT_DOUBLE_EQ(leadingDotJson.getfloat(), 0.5);
}

TEST(JSONNumericParsers, GetFloatParsesScientificNotation)
{
    mega::JSON negativeExpJson("-3.5e1");
    mega::JSON positiveExpJson("2E3");
    mega::JSON fractionalExpJson("4.2e-2");

    EXPECT_DOUBLE_EQ(negativeExpJson.getfloat(), -35.0);
    EXPECT_DOUBLE_EQ(positiveExpJson.getfloat(), 2000.0);
    EXPECT_DOUBLE_EQ(fractionalExpJson.getfloat(), 0.042);
}

TEST(JSONNumericParsers, GetFloatAdvancesAcrossSequentialTokens)
{
    mega::JSON json("25.99,-3.5e1,2E3");

    EXPECT_DOUBLE_EQ(json.getfloat(), 25.99);
    EXPECT_DOUBLE_EQ(json.getfloat(), -35.0);
    EXPECT_DOUBLE_EQ(json.getfloat(), 2000.0);
}

TEST(JSONNumericParsers, GetFloatParsesValuesAfterNameValueSeparator)
{
    mega::JSON json(":25.99,-4.2e-1");

    EXPECT_DOUBLE_EQ(json.getfloat(), 25.99);
    EXPECT_DOUBLE_EQ(json.getfloat(), -0.42);
}

TEST(JSONNumericParsers, GetFloatRejectsMalformedValues)
{
    mega::JSON invalidStart("abc");
    mega::JSON invalidSign("-");
    mega::JSON invalidExponent("1e");
    mega::JSON invalidExponentSign("1e-");

    EXPECT_DOUBLE_EQ(invalidStart.getfloat(), -1.0);
    EXPECT_DOUBLE_EQ(invalidSign.getfloat(), 0.0);
    EXPECT_DOUBLE_EQ(invalidExponent.getfloat(), 1.0);
    EXPECT_DOUBLE_EQ(invalidExponentSign.getfloat(), 1.0);
}
