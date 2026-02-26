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

/*TEST(Commands, CommandGetCountryCallingCodes_processResult_happyPath)
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
    ASSERT_EQ(ptrdiff_t(jsonLength), std::distance(jsonBegin, json.pos)); // assert json has been parsed all the way
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
    ASSERT_EQ(ptrdiff_t(jsonLength), std::distance(jsonBegin, json.pos)); // assert json has been parsed all the way
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
    ASSERT_EQ(ptrdiff_t(jsonLength), std::distance(jsonBegin, json.pos)); // assert json has been parsed all the way
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
    ASSERT_EQ(ptrdiff_t(jsonLength), std::distance(jsonBegin, json.pos)); // assert json has been parsed all the way
}

class FileSystemAccessMockup : public ::mega::FileSystemAccess
{
public:
    FileSystemAccessMockup()
    {}
    std::unique_ptr<FileAccess> newfileaccess(bool = true) override{ return std::unique_ptr<FileAccess>(); }
    DirAccess* newdiraccess() override {return nullptr;}
    bool getlocalfstype(const ::mega::LocalPath&, ::mega::FileSystemType&) const override { return false; }
    void path2local(const string*, string*) const override {}
    void local2path(const string*, string*) const override {}
    #if defined(_WIN32)
    void path2local(const string*, std::wstring*) const override {}
    void local2path(const std::wstring*, string*) const override {}
    #endif
    void tmpnamelocal(LocalPath&) const override {}
    bool getsname(const LocalPath& , LocalPath& ) const override { return false; }
    bool renamelocal(LocalPath&, LocalPath&, bool = true) override { return false; }
    bool copylocal(LocalPath&, LocalPath&, m_time_t) override { return false; }
    bool unlinklocal(LocalPath&) override { return false; }
    bool rmdirlocal(LocalPath&) override { return false; }
    bool mkdirlocal(LocalPath&, bool = false) override { return false; }
    bool setmtimelocal(LocalPath&, m_time_t) override { return false; }
    bool chdirlocal(LocalPath&) const override { return false; }
    bool getextension(const LocalPath&, string&) const override { return false; }
    bool expanselocalpath(LocalPath& , LocalPath& ) override { return false; }
    bool cwd(LocalPath&) const { return false; }

    void addevents(Waiter*, int) override {}

    virtual bool issyncsupported(const LocalPath&, bool& b, SyncError& se, SyncWarning& sw) { b = false; se = NO_SYNC_ERROR; sw = NO_SYNC_WARNING; return true;}
};

class HttpIOMockup : public ::mega::HttpIO
{
public:
    HttpIOMockup(){}
    void post(struct HttpReq*, const char* = NULL, unsigned = 0) override{};
    void cancel(HttpReq*) override{}
    m_off_t postpos(void*) override{ return 0; }
    bool doio(void)  override{ return false; }
    void setuseragent(string*) override{}

    void addevents(Waiter*, int) override {}
};

class MegaAppMockup : public ::mega::MegaApp
{
public:
    MegaAppMockup(){}
};

class ClientMockup : public ::mega::MegaClient
{
public:
    ClientMockup(MegaAppMockup& megaApp, HttpIOMockup& httpIO, FileSystemAccessMockup& fileSystem)
        : MegaClient(&megaApp, nullptr, &httpIO, &fileSystem, nullptr, nullptr, nullptr, "UserAgent", 1)
    {

    }
};


TEST(Commands, CommandFetchAds)
{
    FileSystemAccessMockup fileSystem;
    HttpIOMockup httpIO;
    MegaAppMockup megaApp;
    ClientMockup client(megaApp, httpIO, fileSystem);
    client.json.pos = R"({"id": "wphl","iu": "/22060108601/wph/wph_l"},{"id":"wphr","iu": "/22060108601/wph/wph_r"},{"id":"wpht","iu": "/22060108601/wph/wph_t"})";
    std::vector<std::string> v;
    handle h = UNDEF;
    int adFlags = 512;

    ::mega::CommandFetchAds command(&client, adFlags, v, h, [](::mega::Error e, ::mega::string_map value)
    {
        ASSERT_EQ(e, API_OK);
        ASSERT_EQ(value.size(), 3);
        ASSERT_NE(value.find("wphl"), value.end());
        ASSERT_EQ(value["wphl"], "/22060108601/wph/wph_l");
        ASSERT_NE(value.find("wphr"), value.end());
        ASSERT_EQ(value["wphr"], "/22060108601/wph/wph_r");
        ASSERT_NE(value.find("wpht"), value.end());
        ASSERT_EQ(value["wpht"], "/22060108601/wph/wph_t");
    });

    command.client = &client;

    ::mega::Command::Result r(::mega::Command::Outcome::CmdArray);
    command.procresult(r);
}

TEST(Commands, CommandQueryAds)
{
    FileSystemAccessMockup fileSystem;
    HttpIOMockup httpIO;
    MegaAppMockup megaApp;
    ClientMockup client(megaApp, httpIO, fileSystem);
    client.json.pos = R"(1)";
    handle h = UNDEF;
    int adFlags = 512;

    ::mega::CommandQueryAds command(&client, adFlags, h, [](::mega::Error e, int value)
    {
        ASSERT_EQ(e, API_OK);
        ASSERT_EQ(value, 1);
    });

    command.client = &client;

    ::mega::Command::Result r(::mega::Command::Outcome::CmdArray);
    command.procresult(r);
}
*/
