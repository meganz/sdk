#include "mega/megaclient.h"
#include "mega/name_collision.h"
#include "mega/pwm_file_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace mega::ncoll;
using namespace mega::pwm::import;
using namespace testing;
using namespace mega;

TEST(PWMImportValidator, NormalExecutionNoConflicts)
{
    NameCollisionSolver solver{};
    std::vector<PassEntryParseResult> entries{
        {{},                                                   "pas,test",           "passName",   "test.com", "uName",  "pass",  {}       },
        {{},                                                   "pas,foo",            "passName2",  "foo.com",  "uName2", "pass2", "Notes 1"},
        {PassEntryParseResult::ErrCode::INVALID_NUM_OF_COLUMN, "i,num,of",           {},           {},         {},       {},      {}       },
        {{},                                                   "noPassword,foo.com", "noPassword", "foo.com",  "name",   "",      "Notes 1"},
    };

    auto [bad, good] = MegaClient::validatePasswordEntries(std::move(entries), solver);

    // Check bad
    ASSERT_EQ(bad.size(), 2);
    ASSERT_THAT(bad, Contains(Pair("i,num,of", PasswordEntryError::PARSE_ERROR)));
    ASSERT_THAT(bad, Contains(Pair("noPassword,foo.com", PasswordEntryError::MISSING_PASSWORD)));

    // Check good
    ASSERT_EQ(good.size(), 2);
    // good 1
    ASSERT_NE(good["passName"], nullptr);
    ASSERT_EQ(good["passName"]->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_URL)],
              "test.com");
    ASSERT_EQ(good["passName"]->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_PWD)],
              "pass");
    ASSERT_EQ(good["passName"]->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_NOTES)],
              "");
    // good 2
    ASSERT_NE(good["passName2"], nullptr);
    ASSERT_EQ(good["passName2"]->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_URL)],
              "foo.com");
    ASSERT_EQ(good["passName2"]->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_NOTES)],
              "Notes 1");
}

TEST(PWMImportValidator, WithNameCollisions)
{
    NameCollisionSolver solver({"passName", "passName (1)", "passName (3)"});
    std::vector<PassEntryParseResult> entries{
        {{}, "pas,test", "passName",     "test.com", "uName",  "pass",  {}       },
        {{}, "pas,foo",  "passName2",    "foo.com",  "uName2", "pass2", "Notes 1"},
        {{}, "pas,foo",  "passName (2)", "foo.com",  "uName2", "pass2", "Notes 1"},
    };

    auto [bad, good] = MegaClient::validatePasswordEntries(std::move(entries), solver);

    ASSERT_TRUE(bad.empty());

    ASSERT_EQ(good.size(), 3);
    ASSERT_THAT(good, Contains(Key("passName (2)")));
    ASSERT_EQ(good["passName (2)"]->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_URL)],
              "test.com");

    ASSERT_THAT(good, Contains(Key("passName2")));

    ASSERT_THAT(good, Contains(Key("passName (4)")));
    ASSERT_EQ(good["passName (4)"]->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_URL)],
              "foo.com");
}
