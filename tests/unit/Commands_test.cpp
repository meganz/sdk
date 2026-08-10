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

#include "utils.h"

#include <gtest/gtest.h>
#include <mega/account.h>
#include <mega/command.h>
#include <mega/json.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/types.h>

#include <memory>

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

namespace
{

TEST(Commands, CommandGetUserQuota_parsesPlanStartTime)
{
    // The uq `plans` array carries a per-plan start time in the `ts` field (plan
    // activation), alongside `expires`. Verify readPlans captures it into AccountPlan.
    MegaApp app;
    auto client = mt::makeClient(app);

    auto details = std::make_shared<AccountDetails>();

    // storage/transfer/pro all false: this skips the storage sanity checks and the
    // processPlans() account-status side effects, while readPlans still parses `plans`.
    CommandGetUserQuota command(client.get(), details, false, false, false, 0);
    command.client = client.get();

    // The framework enters the reply object before calling procresult(CmdObject).
    JSON json;
    json.pos = R"({"plans":[{"al":1,"ts":1783500245,"expires":1815036245,"type":2,)"
               R"("features":{"vpn":1,"pwm":1}}]})";
    ASSERT_TRUE(json.enterobject());

    command.procresult(Command::CmdObject, json);

    ASSERT_EQ(1u, details->plans.size());
    const AccountPlan& plan = details->plans[0];
    EXPECT_EQ(1783500245, plan.startTime);
    EXPECT_EQ(1815036245, plan.expiration);
    EXPECT_EQ(1, plan.level);
    EXPECT_EQ(2, plan.type);
}

// Captures the mobile offer parsed for each pricing item, to test utqa `mo` parsing.
class MobileOfferCapturingApp: public MegaApp
{
public:
    using MegaApp::enumeratequotaitems_result; // avoid hiding the other overloads

    std::optional<MobileOffer> capturedOffer;
    int errorCalls = 0;
    error lastError = API_OK;

    void enumeratequotaitems_result(const Product& product) override
    {
        if (product.mobileOffer)
        {
            capturedOffer = product.mobileOffer;
        }
    }

    void enumeratequotaitems_result(error e) override
    {
        // This overload also signals successful completion (API_OK); only real
        // errors are of interest here.
        if (e != API_OK)
        {
            ++errorCalls;
            lastError = e;
        }
    }
};

TEST(Commands, CommandEnumerateQuotaItems_parsesMobileOffer)
{
    MobileOfferCapturingApp app;
    auto client = mt::makeClient(app);

    // A utqa reply is a JSON array whose first element carries the shared
    // currency (`l`) data, followed by one object per plan. A Product is only
    // delivered once an item passes its sanity checks (currency set, non-zero
    // `tc` and `mbp`, storage/transfer, store ids, ...), so the plan item here
    // is fully formed; the mobile offer (`mo`) rides along on it.
    JSON json;
    json.pos = R"([{"l":{"c":"EUR","cs":"4oKs"}},)"
               R"({"it":0,"id":"ITKCjurRtbQ","al":1,"s":3072,"t":36864,"m":12,"p":9999,"mbp":999,)"
               R"("d":"MEGA Pro I","tc":1,"ios":"mega.new.ios.pro1.oneYear.test",)"
               R"("google":"mega.android.pro1.oneyear.test",)"
               R"("mo":{"id":"mega-discount","uat":0,"f":3,"l":"MEGA Discount","p":20,)"
               R"("e":1787464050,"r":86400,"c":90210,)"
               R"("ios":{"oid":"mega.new.ios.pro1.oneYear.test.promo","ki":"KYV4FR348H",)"
               R"("n":"8c39d535-9237-4b96-888d-699296d5c877","tsm":1783997257514,)"
               R"("s":"sig-abc+def/ghi=="},)"
               R"("and":{"oid":"mega-discount"}}}])";

    // The framework enters the reply array before calling procresult(CmdArray).
    ASSERT_TRUE(json.enterarray());

    CommandEnumerateQuotaItems command(std::nullopt, client.get());
    command.client = client.get();

    command.procresult(Command::CmdArray, json);

    ASSERT_EQ(0, app.errorCalls) << "parser reported an error, code=" << app.lastError;
    ASSERT_TRUE(app.capturedOffer.has_value());
    const MobileOffer& mo = *app.capturedOffer;
    EXPECT_EQ("mega-discount", mo.id);
    EXPECT_FALSE(mo.uat);
    EXPECT_EQ("MEGA Discount", mo.label);
    EXPECT_EQ(20, mo.discountPercentage);
    EXPECT_EQ(1787464050, mo.expiryTimestamp);
    EXPECT_EQ(3u, mo.flags);
    EXPECT_EQ(86400, mo.reshowInterval);
    EXPECT_EQ(90210u, mo.campaignId);

    ASSERT_TRUE(mo.ios.has_value());
    EXPECT_EQ("mega.new.ios.pro1.oneYear.test.promo", mo.ios->offerId);
    EXPECT_EQ("KYV4FR348H", mo.ios->keyId);
    EXPECT_EQ("8c39d535-9237-4b96-888d-699296d5c877", mo.ios->nonce);
    EXPECT_EQ(1783997257514, mo.ios->timestampMs);
    EXPECT_EQ("sig-abc+def/ghi==", mo.ios->signature);

    ASSERT_TRUE(mo.android.has_value());
    EXPECT_EQ("mega-discount", mo.android->offerId);
}

TEST(Commands, CommandEnumerateQuotaItems_mobileOfferWithoutCampaignId)
{
    // `mo.c` is only sent for offers that belong to a campaign group. When absent,
    // campaignId stays 0, which the API never uses as a real campaign id.
    MobileOfferCapturingApp app;
    auto client = mt::makeClient(app);

    JSON json;
    json.pos = R"([{"l":{"c":"EUR","cs":"4oKs"}},)"
               R"({"it":0,"id":"ITKCjurRtbQ","al":1,"s":3072,"t":36864,"m":12,"p":9999,"mbp":999,)"
               R"("d":"MEGA Pro I","tc":1,"ios":"mega.new.ios.pro1.oneYear.test",)"
               R"("google":"mega.android.pro1.oneyear.test",)"
               R"("mo":{"id":"mega-discount","uat":0,"f":3,"l":"MEGA Discount","p":20,)"
               R"("e":1787464050,"r":86400}}])";

    ASSERT_TRUE(json.enterarray());

    CommandEnumerateQuotaItems command(std::nullopt, client.get());
    command.client = client.get();

    command.procresult(Command::CmdArray, json);

    ASSERT_EQ(0, app.errorCalls) << "parser reported an error, code=" << app.lastError;
    ASSERT_TRUE(app.capturedOffer.has_value());
    EXPECT_EQ(0u, app.capturedOffer->campaignId);
}
} // anonymous
