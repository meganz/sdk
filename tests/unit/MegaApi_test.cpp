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
#include <mega/mediafileattribute.h>
#include <mega/types.h>

#include <atomic>
#include <cstring>
#include <megaapi.h>
#include <megaapi_impl.h>
#include <memory>
#include <optional>
#include <thread>

using namespace std;
using namespace mega;

namespace {

unique_ptr<MegaStringList> createMegaStringList(const vector<const char*>& data)
{
    string_vector list;

    for (const auto& value : data)
    {
        list.emplace_back(value);
    }

    return unique_ptr<MegaStringList>(new MegaStringListPrivate(std::move(list)));
}

} // anonymous

TEST(MegaApi, MegaStringList_get_and_size_happyPath)
{
    const vector<const char*> data{
        "foo",
        "bar",
    };
    auto stringList = createMegaStringList(data);
    ASSERT_EQ(2, stringList->size());
    ASSERT_EQ(string{"foo"}, string{stringList->get(0)});
    ASSERT_EQ(string{"bar"}, string{stringList->get(1)});
    ASSERT_EQ(nullptr, stringList->get(2));
}

TEST(MegaApi, MegaStringList_get_and_size_emptyStringList)
{
    const vector<const char*> data{
    };
    auto stringList = createMegaStringList(data);
    ASSERT_EQ(0, stringList->size());
    ASSERT_EQ(nullptr, stringList->get(0));
}

TEST(MegaApi, MegaStringList_copy_happyPath)
{
    const vector<const char*> data{
        "foo",
        "bar",
    };
    auto stringList = createMegaStringList(data);
    auto copiedStringList = unique_ptr<MegaStringList>{stringList->copy()};
    ASSERT_EQ(2, copiedStringList->size());
    ASSERT_EQ(string{"foo"}, string{copiedStringList->get(0)});
    ASSERT_EQ(string{"bar"}, string{copiedStringList->get(1)});
    ASSERT_EQ(nullptr, copiedStringList->get(2));
}

TEST(MegaApi, MegaStringList_copy_emptyStringList)
{
    const vector<const char*> data{
    };
    auto stringList = createMegaStringList(data);
    auto copiedStringList = unique_ptr<MegaStringList>{stringList->copy()};
    ASSERT_EQ(0, copiedStringList->size());
    ASSERT_EQ(nullptr, copiedStringList->get(0));
}

TEST(MegaApi, MegaStringList_default_constructor)
{
    auto stringList = unique_ptr<MegaStringList>{new MegaStringListPrivate};
    ASSERT_EQ(0, stringList->size());
    ASSERT_EQ(nullptr, stringList->get(0));
}

TEST(MegaApi, MegaStringListMap_set_and_get_happyPath)
{
    auto stringListMap = unique_ptr<MegaStringListMap>{MegaStringListMap::createInstance()};
    auto stringList1 = createMegaStringList({"13", "42"}).release();
    auto stringList2 = createMegaStringList({"awesome", "sweet", "cool"}).release();
    stringListMap->set("foo", stringList1);
    stringListMap->set("bar", stringList2);
    ASSERT_EQ(2, stringListMap->size());
    ASSERT_EQ(*stringList1, *stringListMap->get("foo"));
    ASSERT_EQ(*stringList2, *stringListMap->get("bar"));
    ASSERT_EQ(nullptr, stringListMap->get("blah"));
    auto expectedKeys = createMegaStringList({"bar", "foo"});
    auto keys = std::unique_ptr<MegaStringList>{stringListMap->getKeys()};
    ASSERT_EQ(*expectedKeys, *keys);
}

TEST(MegaApi, MegaStringListMap_get_emptyStringListMap)
{
    auto stringListMap = unique_ptr<MegaStringListMap>{MegaStringListMap::createInstance()};
    ASSERT_EQ(0, stringListMap->size());
    ASSERT_EQ(nullptr, stringListMap->get("blah"));
    auto keys = std::unique_ptr<MegaStringList>{stringListMap->getKeys()};
    ASSERT_EQ(0, keys->size());
}

TEST(MegaApi, MegaStringListMap_copy_happyPath)
{
    auto stringListMap = unique_ptr<MegaStringListMap>{MegaStringListMap::createInstance()};
    auto stringList1 = createMegaStringList({"13", "42"}).release();
    auto stringList2 = createMegaStringList({"awesome", "sweet", "cool"}).release();
    stringListMap->set("foo", stringList1);
    stringListMap->set("bar", stringList2);
    auto copiedStringListMap = unique_ptr<MegaStringListMap>{stringListMap->copy()};
    ASSERT_EQ(2, copiedStringListMap->size());
    ASSERT_EQ(*stringList1, *copiedStringListMap->get("foo"));
    ASSERT_EQ(*stringList2, *copiedStringListMap->get("bar"));
    ASSERT_EQ(nullptr, copiedStringListMap->get("blah"));
    auto expectedKeys = createMegaStringList({"bar", "foo"});
    auto keys = std::unique_ptr<MegaStringList>{stringListMap->getKeys()};
    ASSERT_EQ(*expectedKeys, *keys);
}

TEST(MegaApi, MegaStringListMap_copy_emptyStringListMap)
{
    auto stringListMap = unique_ptr<MegaStringListMap>{MegaStringListMap::createInstance()};
    auto copiedStringListMap = unique_ptr<MegaStringListMap>{stringListMap->copy()};
    ASSERT_EQ(0, copiedStringListMap->size());
    ASSERT_EQ(nullptr, copiedStringListMap->get("blah"));
    auto keys = std::unique_ptr<MegaStringList>{stringListMap->getKeys()};
    ASSERT_EQ(0, keys->size());
}

TEST(MegaApi, MegaStringTable_append_and_get_happyPath)
{
    auto stringListTable = unique_ptr<MegaStringTable>{MegaStringTable::createInstance()};
    auto stringList1 = createMegaStringList({"13", "42"}).release();
    auto stringList2 = createMegaStringList({"awesome", "sweet", "cool"}).release();
    stringListTable->append(stringList1);
    stringListTable->append(stringList2);
    ASSERT_EQ(2, stringListTable->size());
    ASSERT_EQ(*stringList1, *stringListTable->get(0));
    ASSERT_EQ(*stringList2, *stringListTable->get(1));
    ASSERT_EQ(nullptr, stringListTable->get(2));
}

TEST(MegaApi, MegaStringTable_get_emptyStringTable)
{
    auto stringListTable = unique_ptr<MegaStringTable>{MegaStringTable::createInstance()};
    ASSERT_EQ(0, stringListTable->size());
    ASSERT_EQ(nullptr, stringListTable->get(0));
}

TEST(MegaApi, MegaStringTable_copy_happyPath)
{
    auto stringListTable = unique_ptr<MegaStringTable>{MegaStringTable::createInstance()};
    auto stringList1 = createMegaStringList({"13", "42"}).release();
    auto stringList2 = createMegaStringList({"awesome", "sweet", "cool"}).release();
    stringListTable->append(stringList1);
    stringListTable->append(stringList2);
    auto copiedStringTable = unique_ptr<MegaStringTable>{stringListTable->copy()};
    ASSERT_EQ(2, copiedStringTable->size());
    ASSERT_EQ(*stringList1, *copiedStringTable->get(0));
    ASSERT_EQ(*stringList2, *copiedStringTable->get(1));
    ASSERT_EQ(nullptr, copiedStringTable->get(2));
}

TEST(MegaApi, MegaStringTable_copy_emptyStringTable)
{
    auto stringListTable = unique_ptr<MegaStringTable>{MegaStringTable::createInstance()};
    auto copiedStringTable = unique_ptr<MegaStringTable>{stringListTable->copy()};
    ASSERT_EQ(0, copiedStringTable->size());
    ASSERT_EQ(nullptr, copiedStringTable->get(0));
}

TEST(MegaApi, getMimeType)
{
    vector<thread> threads;
    atomic<int> successCount{0};

    // 100 threads was enough to reliably crash the old non-thread-safe version
    for (int i = 0; i < 100; ++i)
    {
        threads.emplace_back([&successCount]
        {
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType("nosuch")} == nullptr) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType(nullptr)} == nullptr) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType("3ds")}.get() == string("image/x-3ds")) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType(".3ds")}.get() == string("image/x-3ds")) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType("zip")}.get() == string("application/zip")) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType(".zip")}.get() == string("application/zip")) ++successCount;
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    ASSERT_EQ(600, successCount);
}

TEST(MegaApi, MegaApiImpl_calcRecommendedProLevel)
{
    MegaPricingPrivate pricing;
    std::function<void(int, int, int)> addTestProducts =
        [&](int proLevel, int gb, double pricecents)
    {
        pricing.addProduct({1000,
                            1000000,
                            static_cast<unsigned int>(proLevel),
                            gb,
                            gb == -1 ? -1 : gb * 10,
                            1,
                            pricecents,
                            10,
                            100,
                            0.0,
                            0.0,
                            0.0,
                            "monthly",
                            {},
                            "ios id",
                            "android id",
                            1,
                            std::make_unique<BusinessPlan>(),
                            0,
                            std::nullopt,
                            std::nullopt});
        pricing.addProduct({1000,
                            1000000,
                            static_cast<unsigned int>(proLevel),
                            gb,
                            gb == -1 ? -1 : gb * 10,
                            12,
                            pricecents * 12,
                            10,
                            100,
                            0.0,
                            0.0,
                            0.0,
                            "yearly",
                            {},
                            "ios id",
                            "android id",
                            1,
                            std::make_unique<BusinessPlan>(),
                            0,
                            std::nullopt,
                            std::nullopt});
    };
    addTestProducts(MegaAccountDetails::ACCOUNT_TYPE_LITE, 400, 499);
    addTestProducts(MegaAccountDetails::ACCOUNT_TYPE_PROI, 2048, 999);
    addTestProducts(MegaAccountDetails::ACCOUNT_TYPE_PROII, 8192, 1999);
    addTestProducts(MegaAccountDetails::ACCOUNT_TYPE_PROIII, 16384, 2999);
    addTestProducts(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, -1, 0);
    addTestProducts(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, -1, 0);
    pricing.addProduct({1000,
                        1000000,
                        MegaAccountDetails::ACCOUNT_TYPE_STARTER,
                        50,
                        50,
                        1,
                        1,
                        10,
                        100,
                        0.0,
                        0.0,
                        0.0,
                        "monthly",
                        {},
                        "ios id",
                        "android id",
                        1,
                        std::make_unique<BusinessPlan>(),
                        0,
                        std::nullopt,
                        std::nullopt}); // only monthly
    pricing.addProduct({1000,
                        1000000,
                        MegaAccountDetails::ACCOUNT_TYPE_BASIC,
                        100,
                        100,
                        1,
                        2,
                        10,
                        100,
                        0.0,
                        0.0,
                        0.0,
                        "monthly",
                        {},
                        "ios id",
                        "android id",
                        1,
                        std::make_unique<BusinessPlan>(),
                        0,
                        std::nullopt,
                        std::nullopt});
    pricing.addProduct({1000,
                        1000000,
                        MegaAccountDetails::ACCOUNT_TYPE_BASIC,
                        100,
                        100 * 12,
                        12,
                        2 * 12,
                        10,
                        100,
                        0.0,
                        0.0,
                        0.0,
                        "yearly",
                        {},
                        "ios id",
                        "android id",
                        1,
                        std::make_unique<BusinessPlan>(),
                        0,
                        std::nullopt,
                        std::nullopt});
    pricing.addProduct({1000,
                        1000000,
                        MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL,
                        200,
                        200,
                        1,
                        3,
                        10,
                        100,
                        0.0,
                        0.0,
                        0.0,
                        "monthly",
                        {},
                        "ios id",
                        "android id",
                        1,
                        std::make_unique<BusinessPlan>(),
                        0,
                        std::nullopt,
                        std::nullopt});
    Product testProduct = {
        1000,
        1000000,
        MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL,
        200,
        200 * 12,
        12,
        3 * 12,
        10,
        100,
        0.0,
        0.0,
        0.0,
        "yearly",
        {},
        "ios id",
        "android id",
        1,
        std::make_unique<BusinessPlan>(BusinessPlan{20, 40, 3, 50, 60, 70, 80, 90, 100, 15, 10}),
        0,
        std::nullopt,
        std::nullopt};
    pricing.addProduct(testProduct);
    const int testProductIndex = pricing.getNumProducts() - 1;

    ASSERT_EQ(pricing.getGBStorage(testProductIndex), testProduct.gbStorage);
    ASSERT_EQ(pricing.getAmount(testProductIndex), testProduct.amount);
    ASSERT_EQ(pricing.getAmountMonth(testProductIndex), testProduct.amountMonth);
    ASSERT_EQ(pricing.getAndroidID(testProductIndex), testProduct.androidid);
    ASSERT_EQ(pricing.getDescription(testProductIndex), testProduct.description);
    ASSERT_EQ(pricing.getGBPerStorage(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->gbPerStorage : 0);
    ASSERT_EQ(pricing.getGBPerTransfer(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->gbPerTransfer : 0);
    ASSERT_EQ(pricing.getGBStoragePerUser(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->gbStoragePerUser : 0);
    ASSERT_EQ(pricing.getGBTransferPerUser(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->gbTransferPerUser : 0);
    ASSERT_EQ(pricing.getHandle(testProductIndex), testProduct.productHandle);
    ASSERT_EQ(pricing.getIosID(testProductIndex), testProduct.iosid);
    ASSERT_EQ(pricing.getLocalPrice(testProductIndex), testProduct.localPrice);
    ASSERT_EQ(pricing.getMonths(testProductIndex), testProduct.months);
    ASSERT_EQ(pricing.getProLevel(testProductIndex), testProduct.proLevel);
    ASSERT_EQ(pricing.getTrialDurationInDays(testProductIndex), testProduct.trialDays);
    ASSERT_EQ(pricing.getPricePerStorage(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->pricePerStorage : 0);
    ASSERT_EQ(pricing.getPricePerTransfer(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->pricePerTransfer : 0);
    ASSERT_EQ(pricing.getPricePerUser(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->pricePerUser : 0);
    ASSERT_EQ(pricing.getLocalPricePerTransfer(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->localPricePerTransfer : 0);
    ASSERT_EQ(pricing.getLocalPricePerStorage(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->localPricePerStorage : 0);
    ASSERT_EQ(pricing.getLocalPricePerUser(testProductIndex),
              (testProduct.businessPlan) ? testProduct.businessPlan->localPricePerUser : 0);

    std::function<int(int, int)> test = [&](int level, int gb)
    {
        AccountDetails accDetails;
        AccountPlan accPlan;
        accPlan.level = level;
        accDetails.plans.push_back(std::move(accPlan));
        accDetails.storage_used = gb * (m_off_t)(1024 * 1024 * 1024);
        unique_ptr<MegaAccountDetails> details(MegaAccountDetailsPrivate::fromAccountDetails(&accDetails));
        return MegaApiImpl::calcRecommendedProLevel(pricing, *details.get());
    };

    int gb = 30;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_STARTER);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_BASIC);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_LITE);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);

    gb = 80;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_BASIC);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_BASIC);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_LITE);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);

    gb = 120;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_LITE);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);

    gb = 300;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_LITE);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_LITE);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_LITE);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_LITE);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);

    gb = 500;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);

    gb = 5000;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PROII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);

    gb = 10000;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PROIII);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);

    // too large - nothing found
    gb = 20000;
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_FREE, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_STARTER, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BASIC, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_LITE, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PROIII, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_BUSINESS, gb), MegaAccountDetails::ACCOUNT_TYPE_BUSINESS);
    ASSERT_EQ(test(MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI, gb), MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI);
}

TEST(MegaApi, MegaApiImpl_mobileOffer)
{
    std::string title{"Black-friday2025"};
    bool uat{true};
    std::string label{"Easter Sale"};
    int discountPercentage{30};
    Product testProduct = {
        1000,
        1000000,
        MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL,
        200,
        200 * 12,
        12,
        3 * 12,
        10,
        100,
        0.0,
        0.0,
        0.0,
        "yearly",
        {},
        "ios id",
        "android id",
        1,
        std::make_unique<BusinessPlan>(BusinessPlan{20, 40, 3, 50, 60, 70, 80, 90, 100, 15, 10}),
        0,
        MobileOffer{title, uat, label, discountPercentage, 0, 0, 0, 0, std::nullopt, std::nullopt},
        std::nullopt};
    MegaPricingPrivate pricing;
    pricing.addProduct(testProduct);
    int index{0};
    ASSERT_TRUE(pricing.hasMobileOffers(index));
    ASSERT_EQ(title, pricing.getMobileOfferId(index));
    ASSERT_EQ(uat, pricing.hasMobileOfferUat(index));
    ASSERT_EQ(label, pricing.getMobileOfferLabel(index));
    ASSERT_EQ(discountPercentage, pricing.getMobileOfferDiscountPercentage(index));
}

TEST(MegaApi, MegaApiImpl_mobileOfferIosAndAndroid)
{
    MobileOffer offer;
    offer.id = "mega-discount";
    offer.uat = false;
    offer.label = "MEGA Discount";
    offer.discountPercentage = 20;
    offer.expiryTimestamp = 1787464050;
    offer.flags = 3;
    offer.reshowInterval = 86400;
    offer.ios = MobileOfferIos{"mega.new.ios.pro1.oneYear.test.promo",
                               "KYV4FR348H",
                               "8c39d535-9237-4b96-888d-699296d5c877",
                               1783997257514,
                               "sig-abc+def/ghi=="};
    offer.android = MobileOfferAndroid{"mega-discount"};

    Product product;
    product.mobileOffer = offer;

    MegaPricingPrivate pricing;
    pricing.addProduct(product);
    const int index{0};

    ASSERT_TRUE(pricing.hasMobileOffers(index));
    EXPECT_EQ(1787464050, pricing.getMobileOfferExpiryTimestamp(index));
    EXPECT_EQ(3u, pricing.getMobileOfferFlags(index));
    EXPECT_EQ(86400, pricing.getMobileOfferReshowInterval(index));

    ASSERT_TRUE(pricing.hasMobileOfferIos(index));
    EXPECT_EQ("mega.new.ios.pro1.oneYear.test.promo", pricing.getMobileOfferIosOfferId(index));
    EXPECT_EQ("KYV4FR348H", pricing.getMobileOfferIosKeyId(index));
    EXPECT_EQ("8c39d535-9237-4b96-888d-699296d5c877", pricing.getMobileOfferIosNonce(index));
    EXPECT_EQ(1783997257514, pricing.getMobileOfferIosTimestampMs(index));
    EXPECT_EQ("sig-abc+def/ghi==", pricing.getMobileOfferIosSignature(index));

    ASSERT_TRUE(pricing.hasMobileOfferAndroid(index));
    EXPECT_EQ("mega-discount", pricing.getMobileOfferAndroidOfferId(index));
}

TEST(MegaApi, MegaApiImpl_mobileOfferWithoutPlatformSections)
{
    MobileOffer offer;
    offer.id = "backup-day";

    Product product;
    product.mobileOffer = offer;

    MegaPricingPrivate pricing;
    pricing.addProduct(product);
    const int index{0};

    ASSERT_TRUE(pricing.hasMobileOffers(index));
    EXPECT_FALSE(pricing.hasMobileOfferIos(index));
    EXPECT_FALSE(pricing.hasMobileOfferAndroid(index));
    EXPECT_TRUE(pricing.getMobileOfferIosOfferId(index).empty());
    EXPECT_EQ(0, pricing.getMobileOfferIosTimestampMs(index));
    EXPECT_TRUE(pricing.getMobileOfferAndroidOfferId(index).empty());
}

TEST(MegaApi, UseCurrentPathIfNoBasePathIsGiven)
{
    constexpr const char* appKey{nullptr};
    constexpr const char* basePath{nullptr};
    auto megaApi{MegaApi(appKey, basePath)};

    ASSERT_STREQ(std::filesystem::current_path().string().c_str(), megaApi.getBasePath());
}

// Locks the byTimestampAnchor setter contract via the getters (no DB/network):
// only the "unset" sentinel (0,0,*) / order==-1 resets to 0/0/-1; everything
// else (incl. degenerate range, unsupported order, out-of-range bounds) is
// stored as-is and validated later by listAllNodesByPage.
TEST(MegaApi, MegaListAllNodesFilter_byTimestampAnchorContract)
{
    auto make = []
    {
        return unique_ptr<MegaListAllNodesFilter>{MegaListAllNodesFilter::createInstance()};
    };
    auto expectAnchor = [](const MegaListAllNodesFilter& f, int64_t start, int64_t end, int order)
    {
        EXPECT_EQ(f.byTimestampAnchorStartDate(), start);
        EXPECT_EQ(f.byTimestampAnchorEndDate(), end);
        EXPECT_EQ(f.byTimestampAnchorOrder(), order);
    };

    // Valid anchor is stored verbatim.
    {
        auto f = make();
        f->byTimestampAnchor(100, 200, MegaApi::ORDER_MODIFICATION_DESC);
        expectAnchor(*f, 100, 200, MegaApi::ORDER_MODIFICATION_DESC);
    }

    // start == end == 0 is the "unset" sentinel → reset to 0/0/-1.
    {
        auto f = make();
        f->byTimestampAnchor(100, 200, MegaApi::ORDER_MODIFICATION_ASC);
        f->byTimestampAnchor(0, 0, MegaApi::ORDER_MODIFICATION_ASC);
        expectAnchor(*f, 0, 0, -1);
    }

    // sectionOrder == -1 is the "unset" sentinel → reset.
    {
        auto f = make();
        f->byTimestampAnchor(100, 200, MegaApi::ORDER_MODIFICATION_ASC);
        f->byTimestampAnchor(100, 200, -1);
        expectAnchor(*f, 0, 0, -1);
    }

    // Out-of-range bounds (e.g. negative) are STORED, not reset — rejected later
    // by listAllNodesByPage.
    {
        auto f = make();
        f->byTimestampAnchor(-5, 200, MegaApi::ORDER_MODIFICATION_ASC);
        expectAnchor(*f, -5, 200, MegaApi::ORDER_MODIFICATION_ASC);
    }

    // Degenerate range (start >= end) is STORED, not reset — rejected later
    // by listAllNodesByPage.
    {
        auto f = make();
        f->byTimestampAnchor(200, 200, MegaApi::ORDER_MODIFICATION_DESC);
        expectAnchor(*f, 200, 200, MegaApi::ORDER_MODIFICATION_DESC);
    }

    // Unsupported order with a valid range is STORED, not reset — rejected
    // later by listAllNodesByPage.
    {
        auto f = make();
        f->byTimestampAnchor(100, 200, MegaApi::ORDER_SIZE_ASC);
        expectAnchor(*f, 100, 200, MegaApi::ORDER_SIZE_ASC);
    }
}

TEST(MegaApi, MegaGroupNodesByDateFilter_ByUtcOffset_RoundTripsAndDefaultsToEmpty)
{
    std::unique_ptr<MegaGroupNodesByDateFilter> f{MegaGroupNodesByDateFilter::createInstance()};

    // Default: never null, empty string.
    ASSERT_NE(f->byUtcOffset(), nullptr);
    EXPECT_STREQ(f->byUtcOffset(), "");

    f->byUtcOffset("+09:00");
    EXPECT_STREQ(f->byUtcOffset(), "+09:00");

    // copy() must carry the UTC offset.
    std::unique_ptr<MegaGroupNodesByDateFilter> c{f->copy()};
    EXPECT_STREQ(c->byUtcOffset(), "+09:00");

    // Null resets to empty.
    f->byUtcOffset(nullptr);
    EXPECT_STREQ(f->byUtcOffset(), "");
}

TEST(MegaApi, ParseUtcOffsetSeconds_ValidUnsetAndBoundaries)
{
    using ::mega::parseUtcOffsetSeconds;

    // Unset / empty → UTC (0), NOT nullopt.
    EXPECT_EQ(parseUtcOffsetSeconds(nullptr), std::optional<int64_t>{0});
    EXPECT_EQ(parseUtcOffsetSeconds(""), std::optional<int64_t>{0});

    // Well-formed values.
    EXPECT_EQ(parseUtcOffsetSeconds("+00:00"), std::optional<int64_t>{0});
    EXPECT_EQ(parseUtcOffsetSeconds("+09:00"), std::optional<int64_t>{32400});
    EXPECT_EQ(parseUtcOffsetSeconds("-05:30"), std::optional<int64_t>{-19800});
    EXPECT_EQ(parseUtcOffsetSeconds("+05:45"), std::optional<int64_t>{20700}); // :45 zone
    EXPECT_EQ(parseUtcOffsetSeconds("-12:00"), std::optional<int64_t>{-43200});
    EXPECT_EQ(parseUtcOffsetSeconds("+14:00"), std::optional<int64_t>{50400});
}

TEST(MegaApi, ParseUtcOffsetSeconds_RejectsMalformedAndOutOfRange)
{
    using ::mega::parseUtcOffsetSeconds;

    // Only the exact "±HH:MM" form is accepted; other valid ISO-8601 spellings
    // ("+0900", "Z", "+09") are rejected.
    for (const char* bad: {"+9",
                           "09:00",
                           "+9:00",
                           "+09:99",
                           "+0900",
                           "Z",
                           "+09:0",
                           "+09:000",
                           "++9:00",
                           "+aa:bb",
                           "+15:00",
                           "-13:00",
                           "+14:01"})
    {
        EXPECT_FALSE(parseUtcOffsetSeconds(bad).has_value()) << "should reject: " << bad;
    }
}

TEST(MegaApi, ListAllNodesFilterByFavouriteRoundTrips)
{
    std::unique_ptr<MegaListAllNodesFilter> f{MegaListAllNodesFilter::createInstance()};
    EXPECT_EQ(f->byFavourite(), MegaNodeScopeFilter::BOOL_FILTER_DISABLED);

    f->byFavourite(MegaNodeScopeFilter::BOOL_FILTER_ONLY_TRUE);
    EXPECT_EQ(f->byFavourite(), MegaNodeScopeFilter::BOOL_FILTER_ONLY_TRUE);

    f->byFavourite(MegaNodeScopeFilter::BOOL_FILTER_ONLY_FALSE);
    EXPECT_EQ(f->byFavourite(), MegaNodeScopeFilter::BOOL_FILTER_ONLY_FALSE);

    f->byFavourite(999); // invalid -> ignored
    EXPECT_EQ(f->byFavourite(), MegaNodeScopeFilter::BOOL_FILTER_ONLY_FALSE);

    std::unique_ptr<MegaListAllNodesFilter> c{f->copy()};
    EXPECT_EQ(c->byFavourite(), MegaNodeScopeFilter::BOOL_FILTER_ONLY_FALSE);
}

// Regression test for SDK-6398: getShortformat() must fold protocol value 0
// ("exotic format") to the API sentinel -1, just as the sibling getters fold
// their zero values.
TEST(MegaNodePrivate, getShortformat_exotic_returns_minus_one)
{
    // Build a known file key (second half of the node key).
    uint32_t fakey[4] = {0x12345678, 0x9ABCDEF0, 0x0FEDCBA9, 0x87654321};
    std::string nodekey(FILENODEKEYLENGTH, '\0');
    std::memcpy(nodekey.data() + FILENODEKEYLENGTH / 2, fakey, sizeof(fakey));

    // shortformat == 0 is the protocol "exotic" sentinel: the codec triple is
    // not in the shortformat table and fa_mediaext carries the real ids.
    MediaProperties mp;
    mp.shortformat = 0;
    const std::string fileattrstring =
        "0:" + MediaProperties::encodeMediaPropertiesAttributes(mp, fakey);

    MegaNodePrivate node("test.m4a",
                         MegaNode::TYPE_FILE,
                         1024,
                         0,
                         0,
                         42,
                         &nodekey,
                         &fileattrstring,
                         nullptr,
                         nullptr,
                         INVALID_HANDLE);

    // Protocol 0 must be folded to the API sentinel -1, not exposed as 0.
    EXPECT_EQ(node.getShortformat(), -1);
}

TEST(MegaNodePrivate, getShortformat_known_format_returns_index)
{
    uint32_t fakey[4] = {0x12345678, 0x9ABCDEF0, 0x0FEDCBA9, 0x87654321};
    std::string nodekey(FILENODEKEYLENGTH, '\0');
    std::memcpy(nodekey.data() + FILENODEKEYLENGTH / 2, fakey, sizeof(fakey));

    MediaProperties mp;
    mp.shortformat = 5; // a valid entry in the shortformat table
    const std::string fileattrstring =
        "0:" + MediaProperties::encodeMediaPropertiesAttributes(mp, fakey);

    MegaNodePrivate node("test.mp4",
                         MegaNode::TYPE_FILE,
                         1024,
                         0,
                         0,
                         42,
                         &nodekey,
                         &fileattrstring,
                         nullptr,
                         nullptr,
                         INVALID_HANDLE);

    EXPECT_EQ(node.getShortformat(), 5);
}

// mediats is 0 for anything that is not a photo / video / audio file, so a
// mediats date-section grouping is only meaningful over a media category. This
// pins the partition the groupAllNodesByDate gate depends on.
TEST(MegaApi, IsMediaMimeType_PartitionsMediaFromNonMedia)
{
    using ::mega::isMediaMimeType;

    EXPECT_TRUE(isMediaMimeType(MIME_TYPE_PHOTO));
    EXPECT_TRUE(isMediaMimeType(MIME_TYPE_VIDEO));
    EXPECT_TRUE(isMediaMimeType(MIME_TYPE_AUDIO));
    EXPECT_TRUE(isMediaMimeType(MIME_TYPE_ALL_VISUAL_MEDIA));

    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_UNKNOWN));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_DOCUMENT));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_PDF));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_PRESENTATION));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_ARCHIVE));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_PROGRAM));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_MISC));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_SPREADSHEET));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_ALL_DOCS));
    EXPECT_FALSE(isMediaMimeType(MIME_TYPE_OTHERS));
}

TEST(MegaApi, MegaAccountDetails_getTransferQuotaState)
{
    const auto stateFor = [](const m_off_t transferMax, const m_off_t used)
    {
        AccountDetails accDetails;
        accDetails.transfer_max = transferMax;
        accDetails.transfer_own_used = used;
        unique_ptr<MegaAccountDetails> details(
            MegaAccountDetailsPrivate::fromAccountDetails(&accDetails));
        return details->getTransferQuotaState();
    };

    constexpr m_off_t allowance = 1000;

    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_GREEN, stateFor(allowance, 0));
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_GREEN, stateFor(allowance, 899)); // 89.9%
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_ORANGE, stateFor(allowance, 900)); // 90%
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_ORANGE, stateFor(allowance, 990)); // 99% exactly
    // Not floored, so any part of the last percent is already red
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_RED, stateFor(10 * allowance, 9901)); // 99.01%
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_RED, stateFor(allowance, 995)); // 99.5%
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_RED, stateFor(allowance, allowance));

    // getTransferUsed caps the total at the allowance, so going over it is still red
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_RED, stateFor(allowance, 10 * allowance));

    // free accounts have no allowance configured, so there is no ratio to report
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_UNKNOWN, stateFor(0, 10 * allowance));
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_UNKNOWN, stateFor(0, 0));

    // served and own usage both count towards the allowance
    AccountDetails accDetails;
    accDetails.transfer_max = allowance;
    accDetails.transfer_own_used = 500;
    accDetails.transfer_srv_used = 400;
    accDetails.transfer_hist = {50};
    unique_ptr<MegaAccountDetails> details(
        MegaAccountDetailsPrivate::fromAccountDetails(&accDetails));
    EXPECT_EQ(950, details->getTransferUsed());
    EXPECT_EQ(MegaApi::TRANSFER_QUOTA_STATE_ORANGE, details->getTransferQuotaState());
}

// A folder node with `childCount` file children, as a public folder link yields: the children are
// carried on the node itself, since there is no local tree to look them up in.
static std::unique_ptr<MegaNodePrivate> makeForeignFolder(const char* name, const int childCount)
{
    const std::string nodekey;
    const std::string fileattrstring;

    auto folder = std::make_unique<MegaNodePrivate>(name,
                                                    MegaNode::TYPE_FOLDER,
                                                    0,
                                                    0,
                                                    0,
                                                    1,
                                                    &nodekey,
                                                    &fileattrstring,
                                                    nullptr,
                                                    nullptr,
                                                    INVALID_HANDLE);

    auto children = std::make_unique<MegaNodeListPrivate>();
    for (int i = 0; i < childCount; ++i)
    {
        const std::string childName = "child" + std::to_string(i) + ".txt";
        MegaNodePrivate child(childName.c_str(),
                              MegaNode::TYPE_FILE,
                              1024,
                              0,
                              0,
                              static_cast<MegaHandle>(100 + i),
                              &nodekey,
                              &fileattrstring,
                              nullptr,
                              nullptr,
                              INVALID_HANDLE);
        children->addNode(&child);
    }

    folder->setChildren(children.release());

    // The children are only carried on a foreign node: anything else is looked up in the local tree
    folder->setForeign(true);
    return folder;
}

static std::unique_ptr<MegaNodePrivate> makeFile(const char* name, const MegaHandle handle)
{
    const std::string nodekey;
    const std::string fileattrstring;

    return std::make_unique<MegaNodePrivate>(name,
                                             MegaNode::TYPE_FILE,
                                             1024,
                                             0,
                                             0,
                                             handle,
                                             &nodekey,
                                             &fileattrstring,
                                             nullptr,
                                             nullptr,
                                             INVALID_HANDLE);
}

TEST(MegaApi, MegaDownloadItemList_keepsAFolderItemsChildren)
{
    // MegaNode::copy() nulls the child list, but a batch download of a public folder link has
    // nowhere else to read its children from: scanFolder walks getChildren() for a foreign node.
    // Losing them made the whole batch fail API_EINTERNAL.
    auto folder = makeForeignFolder("shared", 3);
    ASSERT_NE(nullptr, folder->getChildren());
    ASSERT_EQ(3, folder->getChildren()->size());

    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folder.get(), "/dest/", nullptr, nullptr, false, false);

    ASSERT_EQ(1, items->size());
    MegaNode* stored = items->getNode(0);
    ASSERT_NE(nullptr, stored);
    EXPECT_NE(folder.get(), stored) << "the list must hold its own copy";

    ASSERT_NE(nullptr, stored->getChildren()) << "the folder item lost its children";
    ASSERT_EQ(3, stored->getChildren()->size());
    EXPECT_STREQ("child0.txt", stored->getChildren()->get(0)->getName());
    EXPECT_STREQ("child2.txt", stored->getChildren()->get(2)->getName());

    // The copy is independent: the caller may free what it added straight after.
    folder.reset();
    EXPECT_STREQ("child1.txt", stored->getChildren()->get(1)->getName());
}

TEST(MegaApi, MegaDownloadItemList_copyIsDeepIncludingChildren)
{
    // setDownloadItems() copies the list onto the transfer, so a batch relies on copy() carrying
    // the children through a second time.
    auto folder = makeForeignFolder("shared", 2);

    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folder.get(), "/dest/", "renamed", "batch-7", true, true);

    std::unique_ptr<MegaDownloadItemList> copied{items->copy()};
    items.reset();
    folder.reset();

    ASSERT_EQ(1, copied->size());
    EXPECT_STREQ("/dest/", copied->getLocalPath(0));
    EXPECT_STREQ("renamed", copied->getCustomName(0));
    EXPECT_STREQ("batch-7", copied->getAppData(0));
    EXPECT_TRUE(copied->getStartFirst(0));
    EXPECT_TRUE(copied->getUndelete(0));

    MegaNode* stored = copied->getNode(0);
    ASSERT_NE(nullptr, stored);
    ASSERT_NE(nullptr, stored->getChildren()) << "copy() dropped the folder item's children";
    ASSERT_EQ(2, stored->getChildren()->size());
    EXPECT_STREQ("child0.txt", stored->getChildren()->get(0)->getName());
}

TEST(MegaApi, MegaDownloadItemList_emptyStringsMeanUnset)
{
    // "" is how a binding passes "no value"; it must read back as none, not as a zero-length
    // destination that would resolve to the filesystem root.
    auto folder = makeForeignFolder("shared", 1);

    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folder.get(), "", "", "", false, false);
    items->add(folder.get(), nullptr, nullptr, nullptr, false, false);

    ASSERT_EQ(2, items->size());
    for (int i = 0; i < 2; ++i)
    {
        EXPECT_EQ(nullptr, items->getLocalPath(i)) << "item " << i;
        EXPECT_EQ(nullptr, items->getCustomName(i)) << "item " << i;
        EXPECT_EQ(nullptr, items->getAppData(i)) << "item " << i;
    }
}

TEST(MegaApi, MegaDownloadItemList_perItemOptionsAreIndependent)
{
    // appData and startFirst are per item rather than per batch, so one selection may mix values:
    // every getter must answer for the index it was asked about.
    auto first = makeFile("first.txt", 101u);
    auto second = makeFile("second.txt", 102u);
    auto third = makeFile("third.txt", 103u);

    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(first.get(), "/a/", nullptr, "tag-a", true, false);
    items->add(second.get(), "/b/");
    items->add(third.get(), "/c/", nullptr, "tag-c", false, true);

    ASSERT_EQ(3, items->size());

    EXPECT_STREQ("tag-a", items->getAppData(0));
    EXPECT_TRUE(items->getStartFirst(0));
    EXPECT_FALSE(items->getUndelete(0));

    EXPECT_EQ(nullptr, items->getAppData(1));
    EXPECT_FALSE(items->getStartFirst(1));
    EXPECT_FALSE(items->getUndelete(1));

    EXPECT_STREQ("tag-c", items->getAppData(2));
    EXPECT_FALSE(items->getStartFirst(2));
    EXPECT_TRUE(items->getUndelete(2));
}

TEST(MegaApi, MegaDownloadItemList_gettersAreBoundsChecked)
{
    // A caller iterating past size() reads the neutral value rather than out of range.
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    EXPECT_EQ(0, items->size());

    for (const int i: {-1, 0, 7})
    {
        EXPECT_EQ(nullptr, items->getNode(i)) << "index " << i;
        EXPECT_EQ(nullptr, items->getLocalPath(i)) << "index " << i;
        EXPECT_EQ(nullptr, items->getCustomName(i)) << "index " << i;
        EXPECT_EQ(nullptr, items->getAppData(i)) << "index " << i;
        EXPECT_FALSE(items->getStartFirst(i)) << "index " << i;
        EXPECT_FALSE(items->getUndelete(i)) << "index " << i;
    }

    // A null node is kept as an item, so the indices the caller added still line up; scanItems
    // skips it.
    items->add(nullptr, "/dest/");
    EXPECT_EQ(1, items->size());
    EXPECT_EQ(nullptr, items->getNode(0));
    EXPECT_STREQ("/dest/", items->getLocalPath(0));
}
