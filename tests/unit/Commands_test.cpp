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
    using DataType = vector<tuple<string, string, string>>;

    int mCallCount = 0;
    ErrorCodes mLastError = ErrorCodes::API_EINTERNAL;
    std::unique_ptr<DataType> mRegisteredContacts;

    void getregisteredcontacts_result(const ErrorCodes e, DataType* const data) override
    {
        ++mCallCount;
        mLastError = e;
        if (data)
        {
            mRegisteredContacts = std::unique_ptr<DataType>{new DataType{*data}};
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
    json.pos = R"({"eud":"foo@mega.co.nz","id":"13","ud":"foo@mega.co.nz"},{"eud":"+64271234567","id":"42","ud":"+64 27 123 4567"})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetRegisteredContacts::processResult(app, json);

    const vector<tuple<string, string, string>> expected{
        {"foo@mega.co.nz", "13", "foo@mega.co.nz"},
        {"+64271234567", "42", "+64 27 123 4567"},
    };

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_OK, app.mLastError);
    ASSERT_NE(nullptr, app.mRegisteredContacts);
    ASSERT_EQ(expected, *app.mRegisteredContacts);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetRegisteredContacts_processResult_onlyOneContact)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = R"({"eud":"foo@mega.co.nz","id":"13","ud":"foo@mega.co.nz"})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetRegisteredContacts::processResult(app, json);

    const vector<tuple<string, string, string>> expected{
        {"foo@mega.co.nz", "13", "foo@mega.co.nz"},
    };

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_OK, app.mLastError);
    ASSERT_NE(nullptr, app.mRegisteredContacts);
    ASSERT_EQ(expected, *app.mRegisteredContacts);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetRegisteredContacts_processResult_extraFieldShouldBeIgnored)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = R"({"eud":"foo@mega.co.nz","id":"13","ud":"foo@mega.co.nz","blah":"42"})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetRegisteredContacts::processResult(app, json);

    const vector<tuple<string, string, string>> expected{
        {"foo@mega.co.nz", "13", "foo@mega.co.nz"},
    };

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_OK, app.mLastError);
    ASSERT_NE(nullptr, app.mRegisteredContacts);
    ASSERT_EQ(expected, *app.mRegisteredContacts);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetRegisteredContacts_processResult_invalidResponse)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = R"({"eud":"foo@mega.co.nz","id":"13","blah":"foo@mega.co.nz"})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetRegisteredContacts::processResult(app, json);

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_EINTERNAL, app.mLastError);
    ASSERT_EQ(nullptr, app.mRegisteredContacts);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetRegisteredContacts_processResult_errorCodeReceived)
{
    MockApp_CommandGetRegisteredContacts app;

    JSON json;
    json.pos = "-8";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetRegisteredContacts::processResult(app, json);

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_EEXPIRED, app.mLastError);
    ASSERT_EQ(nullptr, app.mRegisteredContacts);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

namespace {

class MockApp_CommandGetCountryCallingCodes : public MegaApp
{
public:
    using DataType = map<string, vector<string>>;

    int mCallCount = 0;
    ErrorCodes mLastError = ErrorCodes::API_EINTERNAL;
    std::unique_ptr<DataType> mCountryCallingCodes;

    void getcountrycallingcodes_result(const ErrorCodes e, DataType* const data) override
    {
        ++mCallCount;
        mLastError = e;
        if (data)
        {
            mCountryCallingCodes = std::unique_ptr<DataType>{new DataType{*data}};
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
    json.pos = R"({"cc":"AD","l":[376]},{"cc":"AE","l":[971,13]},{"cc":"AF","l":[93,13,42]})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetCountryCallingCodes::processResult(app, json);

    const map<string, vector<string>> expected{
        {"AD", {"376"}},
        {"AE", {"971", "13"}},
        {"AF", {"93", "13", "42"}},
    };

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_OK, app.mLastError);
    ASSERT_NE(nullptr, app.mCountryCallingCodes);
    ASSERT_EQ(expected, *app.mCountryCallingCodes);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_onlyOneCountry)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = R"({"cc":"AD","l":[12,376]})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetCountryCallingCodes::processResult(app, json);

    const map<string, vector<string>> expected{
        {"AD", {"12", "376"}},
    };

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_OK, app.mLastError);
    ASSERT_NE(nullptr, app.mCountryCallingCodes);
    ASSERT_EQ(expected, *app.mCountryCallingCodes);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_extraFieldShouldBeIgnored)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = R"({"cc":"AD","l":[12,376],"blah":"42"})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetCountryCallingCodes::processResult(app, json);

    const map<string, vector<string>> expected{
        {"AD", {"12", "376"}},
    };

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_OK, app.mLastError);
    ASSERT_NE(nullptr, app.mCountryCallingCodes);
    ASSERT_EQ(expected, *app.mCountryCallingCodes);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_invalidResponse)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = R"({"cc":"AD","blah":[12,376]})";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetCountryCallingCodes::processResult(app, json);

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_EINTERNAL, app.mLastError);
    ASSERT_EQ(nullptr, app.mCountryCallingCodes);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}

TEST(Commands, CommandGetCountryCallingCodes_processResult_errorCodeReceived)
{
    MockApp_CommandGetCountryCallingCodes app;

    JSON json;
    json.pos = "-8";
    const auto jsonBegin = json.pos;
    const auto jsonLength = strlen(json.pos);

    CommandGetCountryCallingCodes::processResult(app, json);

    ASSERT_EQ(1, app.mCallCount);
    ASSERT_EQ(API_EEXPIRED, app.mLastError);
    ASSERT_EQ(nullptr, app.mCountryCallingCodes);
    ASSERT_EQ(jsonLength, static_cast<size_t>(std::distance(jsonBegin, json.pos))); // assert json has been parsed all the way
}
