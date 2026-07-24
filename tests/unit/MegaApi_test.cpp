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
#include <mega/types.h>

#include <atomic>
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
        MobileOffer{title, uat, label, discountPercentage, 0, 0, 0, std::nullopt, std::nullopt},
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
