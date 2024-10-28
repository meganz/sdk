/**
 * (c) 2024 by Mega Limited, New Zealand
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

#include "mega/user.h"
#include "mega/user_attribute.h"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <tuple>

// attr2string
class attr2stringWithParam: public testing::TestWithParam<std::tuple<mega::attr_t, std::string>>
{};

TEST_P(attr2stringWithParam, attr2string)
{
    const auto& [at, name] = GetParam();
    ASSERT_EQ(mega::User::attr2string(at), name);
}

INSTANTIATE_TEST_SUITE_P(UserAttributeTests,
                         attr2stringWithParam,
                         testing::Values(std::make_tuple(mega::ATTR_AVATAR, "+a"),
                                         std::make_tuple(mega::ATTR_FIRSTNAME, "firstname"),
                                         std::make_tuple(mega::ATTR_AUTHRING, "*!authring"),
                                         std::make_tuple(mega::ATTR_ED25519_PUBK, "+puEd255")));

// attr2longname
class attr2longnameWithParam: public testing::TestWithParam<std::tuple<mega::attr_t, std::string>>
{};

TEST_P(attr2longnameWithParam, attr2longname)
{
    const auto& [at, longName] = GetParam();
    ASSERT_EQ(mega::User::attr2longname(at), longName);
}

INSTANTIATE_TEST_SUITE_P(UserAttributeTests,
                         attr2longnameWithParam,
                         testing::Values(std::make_tuple(mega::ATTR_AVATAR, "AVATAR"),
                                         std::make_tuple(mega::ATTR_FIRSTNAME, "FIRSTNAME"),
                                         std::make_tuple(mega::ATTR_AUTHRING, "AUTHRING"),
                                         std::make_tuple(mega::ATTR_ED25519_PUBK, "ED25519_PUBK")));

// string2attr
class string2attrWithParam: public testing::TestWithParam<std::tuple<std::string, mega::attr_t>>
{};

TEST_P(string2attrWithParam, string2attr)
{
    const auto& [name, at] = GetParam();
    ASSERT_EQ(mega::User::string2attr(name.c_str()), at);
}

INSTANTIATE_TEST_SUITE_P(UserAttributeTests,
                         string2attrWithParam,
                         testing::Values(std::make_tuple("+a", mega::ATTR_AVATAR),
                                         std::make_tuple("firstname", mega::ATTR_FIRSTNAME),
                                         std::make_tuple("*!authring", mega::ATTR_AUTHRING),
                                         std::make_tuple("+puEd255", mega::ATTR_ED25519_PUBK)));

// needversioning
class needversioningWithParam: public testing::TestWithParam<std::tuple<mega::attr_t, int>>
{};

TEST_P(needversioningWithParam, needversioning)
{
    const auto& [at, ver] = GetParam();
    ASSERT_EQ(mega::User::needversioning(at), ver);
}

INSTANTIATE_TEST_SUITE_P(UserAttributeTests,
                         needversioningWithParam,
                         testing::Values(std::make_tuple(mega::ATTR_AVATAR, 0),
                                         std::make_tuple(mega::ATTR_FIRSTNAME, 0),
                                         std::make_tuple(mega::ATTR_AUTHRING, 1),
                                         std::make_tuple(mega::ATTR_ED25519_PUBK, 1),
                                         std::make_tuple(mega::ATTR_UNKNOWN, -1)));

// scope
class scopeWithParam: public testing::TestWithParam<std::tuple<mega::attr_t, char>>
{};

TEST_P(scopeWithParam, scope)
{
    const auto& [at, scope] = GetParam();
    ASSERT_EQ(mega::User::scope(at), scope);
}

INSTANTIATE_TEST_SUITE_P(UserAttributeTests,
                         scopeWithParam,
                         testing::Values(std::make_tuple(mega::ATTR_AVATAR, '+'),
                                         std::make_tuple(mega::ATTR_FIRSTNAME, '#'),
                                         std::make_tuple(mega::ATTR_AUTHRING, '*'),
                                         std::make_tuple(mega::ATTR_ED25519_PUBK, '+')));

// isAuthring
class isAuthringWithParam: public testing::TestWithParam<std::tuple<mega::attr_t, bool>>
{};

TEST_P(isAuthringWithParam, isAuthring)
{
    const auto& [at, authring] = GetParam();
    if (authring)
        ASSERT_TRUE(mega::User::isAuthring(at));
    else
        ASSERT_FALSE(mega::User::isAuthring(at));
}

INSTANTIATE_TEST_SUITE_P(UserAttributeTests,
                         isAuthringWithParam,
                         testing::Values(std::make_tuple(mega::ATTR_AVATAR, false),
                                         std::make_tuple(mega::ATTR_FIRSTNAME, false),
                                         std::make_tuple(mega::ATTR_AUTHRING, true),
                                         std::make_tuple(mega::ATTR_ED25519_PUBK, false)));

// getMaxAttributeSize
class getMaxAttributeSizeWithParam: public testing::TestWithParam<std::tuple<mega::attr_t, size_t>>
{};

TEST_P(getMaxAttributeSizeWithParam, getMaxAttributeSize)
{
    const auto& [at, attributeMaxSize] = GetParam();
    ASSERT_EQ(mega::User::getMaxAttributeSize(at), attributeMaxSize);
}

static constexpr size_t MAX_USER_VAR_SIZE =
    16 * 1024 * 1024; // 16MB - User attributes whose second character is ! or ~ (per example
                      // *!dn, ^!keys", ...)
static constexpr size_t MAX_USER_ATTRIBUTE_SIZE = 64 * 1024; // 64kB  - Other user attributes

INSTANTIATE_TEST_SUITE_P(
    UserAttributeTests,
    getMaxAttributeSizeWithParam,
    testing::Values(std::make_tuple(mega::ATTR_AVATAR, MAX_USER_ATTRIBUTE_SIZE),
                    std::make_tuple(mega::ATTR_FIRSTNAME, MAX_USER_ATTRIBUTE_SIZE),
                    std::make_tuple(mega::ATTR_AUTHRING, MAX_USER_VAR_SIZE),
                    std::make_tuple(mega::ATTR_ED25519_PUBK, MAX_USER_ATTRIBUTE_SIZE)));

// interfaces
class InterfacesWithParam: public testing::TestWithParam<mega::attr_t>
{
protected:
    void validateValue(mega::attr_t at,
                       const mega::UserAttribute* attribute,
                       const std::optional<const std::string>& value)
    {
        if (!value)
        {
            ASSERT_TRUE(!attribute || attribute->isNotExisting() ||
                        (at == mega::ATTR_AVATAR && attribute->value().empty()));
        }
        else if (at == mega::ATTR_AVATAR)
        {
            ASSERT_TRUE(attribute->value().empty()) << "Found value " << attribute->value();
        }
        else
        {
            ASSERT_EQ(attribute->value(), *value);
        }
    }

    std::string mEmail{"foo@bar.com"};
    mega::User mUser{mEmail.c_str()};
    std::string mValue1{"Foo"};
    std::string mVersion1{"FHqlO7Gbl_w"};
};

TEST_P(InterfacesWithParam, SetValueAndVersion)
{
    auto unchanged = mUser.changed;
    mUser.setAttribute(GetParam(), mValue1, mVersion1);
    ASSERT_NE(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_THAT(attribute, testing::NotNull());
    ASSERT_TRUE(attribute->isValid());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, mValue1));
    ASSERT_EQ(attribute->version(), mVersion1);
}

TEST_P(InterfacesWithParam, SetValueSameVersion)
{
    mUser.setAttribute(GetParam(), mValue1, mVersion1);
    std::string value2{"Bar"};
    mUser.changed = {};
    auto unchanged = mUser.changed;
    ASSERT_FALSE(mUser.updateAttributeIfDifferentVersion(GetParam(), value2, mVersion1));
    ASSERT_EQ(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_THAT(attribute, testing::NotNull());
    ASSERT_TRUE(attribute->isValid());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, mValue1));
    ASSERT_EQ(attribute->version(), mVersion1);
}

TEST_P(InterfacesWithParam, SetValueDifferentVersion)
{
    mUser.setAttribute(GetParam(), mValue1, mVersion1);
    std::string value2{"Bar"};
    std::string version2{"FHqlO7Gbl_x"};
    mUser.changed = {};
    auto unchanged = mUser.changed;
    ASSERT_TRUE(mUser.updateAttributeIfDifferentVersion(GetParam(), value2, version2));
    ASSERT_NE(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_THAT(attribute, testing::NotNull());
    ASSERT_TRUE(attribute->isValid());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, value2));
    ASSERT_EQ(attribute->version(), version2);
}

TEST_P(InterfacesWithParam, SetValueEmptyVersion)
{
    auto unchanged = mUser.changed;
    mUser.setAttribute(GetParam(), mValue1, {});
    ASSERT_NE(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_THAT(attribute, testing::NotNull());
    ASSERT_TRUE(attribute->isValid());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, mValue1));
    ASSERT_TRUE(attribute->version().empty());
}

TEST_P(InterfacesWithParam, Invalidate)
{
    mUser.setAttribute(GetParam(), mValue1, mVersion1);
    mUser.changed = {};
    auto unchanged = mUser.changed;
    mUser.setAttributeExpired(GetParam());
    ASSERT_NE(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_THAT(attribute, testing::NotNull());
    ASSERT_FALSE(attribute->isValid());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, mValue1));
    ASSERT_EQ(attribute->version(), mVersion1);
}

TEST_P(InterfacesWithParam, RemoveValueUpdateVersion)
{
    mUser.setAttribute(GetParam(), mValue1, mVersion1);
    mUser.changed = {};
    auto unchanged = mUser.changed;
    std::string version2{"FHqlO7Gbl_x"};
    mUser.removeAttributeUpdateVersion(
        GetParam(),
        version2); // remove value, but keep updated version and attribute as invalid
    ASSERT_NE(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_THAT(attribute, testing::NotNull());
    ASSERT_FALSE(attribute->isValid());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, ""));
    ASSERT_EQ(attribute->version(), version2);
}

TEST_P(InterfacesWithParam, RemoveValueOwnUser)
{
    mUser.cacheNonExistingAttributes();
    mUser.setAttribute(GetParam(), mValue1, mVersion1);
    mUser.changed = {};
    auto unchanged = mUser.changed;
    mUser.removeAttribute(GetParam());
    ASSERT_NE(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, std::nullopt));
}

TEST_P(InterfacesWithParam, RemoveValueOtherUser)
{
    mUser.setAttribute(GetParam(), mValue1, mVersion1);
    mUser.changed = {};
    auto unchanged = mUser.changed;
    mUser.removeAttribute(GetParam());
    ASSERT_NE(memcmp(&mUser.changed, &unchanged, sizeof(mUser.changed)), 0);

    const mega::UserAttribute* attribute = mUser.getAttribute(GetParam());
    ASSERT_NO_FATAL_FAILURE(validateValue(GetParam(), attribute, std::nullopt));
}

INSTANTIATE_TEST_SUITE_P(UserAttributeTests,
                         InterfacesWithParam,
                         testing::Values(mega::ATTR_AVATAR,
                                         mega::ATTR_FIRSTNAME,
                                         mega::ATTR_AUTHRING,
                                         mega::ATTR_ED25519_PUBK));
