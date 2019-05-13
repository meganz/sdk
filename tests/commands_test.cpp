/**
 * @file tests/commands_test.cpp
 * @brief Mega SDK unit tests for commands
 *
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

// Note: The tests in this module are meant to be pure unit tests: Fast tests without I/O.

#include <memory>

#include <gtest/gtest.h>

#include <mega/command.h>
#include <mega/json.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/types.h>

using namespace std;
using namespace mega;

namespace {

class MockApp_CommandGetRegisteredContacts : public MegaApp
{
public:
    using data_t = vector<tuple<string, string, string>>;

    int callCount = 0;
    ErrorCodes lastError = ErrorCodes::API_EINTERNAL;
    std::unique_ptr<data_t> registeredContacts;

    void getregisteredcontacts_result(const ErrorCodes e, data_t* const data) override
    {
        ++callCount;
        lastError = e;
        if (data)
        {
            registeredContacts = std::unique_ptr<data_t>{new data_t{*data}};
        }
        else
        {
            assert(e != ErrorCodes::API_OK);
        }
    }
};

} // anonymous

TEST(Commands, CommandGetRegisteredContacts_processResult_happyPath)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = R"([{"eud":"foo@mega.co.nz","id":"13","ud":"foo@mega.co.nz"},{"eud":"+64271234567","id":"42","ud":"+64 27 123 4567"}])";

    CommandGetRegisteredContacts::processResult(app, json);

    const vector<tuple<string, string, string>> expected{
        {"foo@mega.co.nz", "13", "foo@mega.co.nz"},
        {"+64271234567", "42", "+64 27 123 4567"},
    };

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_OK, app.lastError);
    ASSERT_NE(nullptr, app.registeredContacts);
    ASSERT_EQ(expected, *app.registeredContacts);
}

TEST(Commands, CommandGetRegisteredContacts_processResult_onlyOneContact)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = R"([{"eud":"foo@mega.co.nz","id":"13","ud":"foo@mega.co.nz"}])";

    CommandGetRegisteredContacts::processResult(app, json);

    const vector<tuple<string, string, string>> expected{
        {"foo@mega.co.nz", "13", "foo@mega.co.nz"},
    };

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_OK, app.lastError);
    ASSERT_NE(nullptr, app.registeredContacts);
    ASSERT_EQ(expected, *app.registeredContacts);
}

TEST(Commands, CommandGetRegisteredContacts_processResult_emptyResponse)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = "";

    CommandGetRegisteredContacts::processResult(app, json);

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_EINTERNAL, app.lastError);
    ASSERT_EQ(nullptr, app.registeredContacts);
}

TEST(Commands, CommandGetRegisteredContacts_processResult_jsonNotAnArray)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = R"({"eud":"foo@mega.co.nz","id":"13","ud":"foo@mega.co.nz"}])";

    CommandGetRegisteredContacts::processResult(app, json);

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_EINTERNAL, app.lastError);
    ASSERT_EQ(nullptr, app.registeredContacts);
}

TEST(Commands, CommandGetRegisteredContacts_processResult_errorCodeReceived)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = "-8";

    CommandGetRegisteredContacts::processResult(app, json);

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_EEXPIRED, app.lastError);
    ASSERT_EQ(nullptr, app.registeredContacts);
}

namespace {

class MockApp_CommandGetCountryCallingCodes : public MegaApp
{
public:
    using data_t = map<string, vector<string>>;

    int callCount = 0;
    ErrorCodes lastError = ErrorCodes::API_EINTERNAL;
    std::unique_ptr<data_t> countryCallingCodes;

    void getcountrycallingcodes_result(const ErrorCodes e, data_t* const data) override
    {
        ++callCount;
        lastError = e;
        if (data)
        {
            countryCallingCodes = std::unique_ptr<data_t>{new data_t{*data}};
        }
        else
        {
            assert(e != ErrorCodes::API_OK);
        }
    }
};

} // anonymous

TEST(Commands, CommandGetCountryCallingCodes_processResult_happyPath)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = R"([{"cc":"AD","l":[376]},{"cc":"AE","l":[971,13]},{"cc":"AF","l":[93,13,42]}])";

    CommandGetCountryCallingCodes::processResult(app, json);

    const map<string, vector<string>> expected{
        {"AD", {"376"}},
        {"AE", {"971", "13"}},
        {"AF", {"93", "13", "42"}},
    };

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_OK, app.lastError);
    ASSERT_NE(nullptr, app.countryCallingCodes);
    ASSERT_EQ(expected, *app.countryCallingCodes);
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_onlyOneCountry)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = R"([{"cc":"AD","l":[12,376]}])";

    CommandGetCountryCallingCodes::processResult(app, json);

    const map<string, vector<string>> expected{
        {"AD", {"12", "376"}},
    };

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_OK, app.lastError);
    ASSERT_NE(nullptr, app.countryCallingCodes);
    ASSERT_EQ(expected, *app.countryCallingCodes);
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_emptyResponse)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = "";

    CommandGetCountryCallingCodes::processResult(app, json);

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_EINTERNAL, app.lastError);
    ASSERT_EQ(nullptr, app.countryCallingCodes);
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_jsonNotAnArray)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = R"({"cc":"AD","l":[12,376]}])";

    CommandGetCountryCallingCodes::processResult(app, json);

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_EINTERNAL, app.lastError);
    ASSERT_EQ(nullptr, app.countryCallingCodes);
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_errorCodeReceived)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = "-8";

    CommandGetCountryCallingCodes::processResult(app, json);

    ASSERT_EQ(1, app.callCount);
    ASSERT_EQ(API_EEXPIRED, app.lastError);
    ASSERT_EQ(nullptr, app.countryCallingCodes);
}
