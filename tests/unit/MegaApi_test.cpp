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

#include <atomic>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <mega/types.h>
#include <megaapi.h>
#include <megaapi_impl.h>

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
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType("323")}.get() == string("text/h323")) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType(".323")}.get() == string("text/h323")) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType("zip")}.get() == string("application/x-zip-compressed")) ++successCount;
            if (std::unique_ptr<char[]>{::mega::MegaApi::getMimeType(".zip")}.get() == string("application/x-zip-compressed")) ++successCount;
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    ASSERT_EQ(600, successCount);
}

TEST(MegaApi, getNoSyncStall)
{
    auto syncStallInfo = std::make_unique<SyncStallInfo>();

    MegaStringList sentinel;
    MegaStringList* megaStringListPtr = &sentinel;
    ASSERT_EQ(megaStringListPtr, &sentinel);

    size_t numConflicts = MegaApiImpl::getSyncStalls(std::move(syncStallInfo), &megaStringListPtr);
    ASSERT_EQ(numConflicts, 0u); // No conflict
    ASSERT_EQ(megaStringListPtr, nullptr); // List not allocated
}

TEST(MegaApi, getLocalSyncStall)
{
    auto syncStallInfo = std::make_unique<SyncStallInfo>();

    MegaStringList sentinel;
    MegaStringList* megaStringListPtr = &sentinel;
    ASSERT_EQ(megaStringListPtr, &sentinel);

    const std::string theLocalPath  = "/here/there/be/Chicken/Egg";
    const std::string theRemotePath = "/here/there/be/Egg/Chicken";
    const auto localPath = LocalPath::fromPlatformEncodedAbsolute(theLocalPath);

    syncStallInfo->waitingLocal(  
        localPath, 
        localPath,
        theRemotePath,
        SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose
    );

    size_t numConflicts = MegaApiImpl::getSyncStalls(std::move(syncStallInfo), &megaStringListPtr);

    ASSERT_EQ(numConflicts, 1u); // conflict
    ASSERT_NE(megaStringListPtr, nullptr); // List allocated
    ASSERT_NE(megaStringListPtr, &sentinel); // List is not sentinel

    delete megaStringListPtr; // Release owned list
}

TEST(MegaApi, MegaSyncStallList_constructor){
    SyncStallInfo syncStallInfo;

    const std::string theLocalPath  = "/here/there/be/Chicken/Egg";
    const std::string theRemotePath = "/here/there/be/Egg/Chicken";
    const auto localPath = LocalPath::fromPlatformEncodedAbsolute(theLocalPath);

    // Simulate a stall detected locally
    syncStallInfo.waitingLocal(
        localPath,
        localPath,
        theRemotePath,
        SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose
    );

    // Simulate a stall detected remotelly
    syncStallInfo.waitingCloud(
        theRemotePath,
        theRemotePath,
        localPath,
        SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose
    );

    MegaSyncStallListImpl syncStallList(syncStallInfo);
    ASSERT_EQ(syncStallList.size(), 2u);

    MegaSyncStall* localStallPtr = syncStallList.get(0);
    MegaSyncStall* cloudStallPtr = syncStallList.get(1);

    ASSERT_NE(localStallPtr, nullptr);
    ASSERT_NE(cloudStallPtr, nullptr);

    // Check The local stall object
    ASSERT_EQ(theLocalPath.compare( localStallPtr->indexPath()),0);
    ASSERT_EQ(theLocalPath.compare( localStallPtr->localPath()),0);
    ASSERT_EQ(theRemotePath.compare(localStallPtr->cloudPath()),0);
    ASSERT_FALSE(localStallPtr->isCloud());

    // Check The cloud stall object
    ASSERT_EQ(theRemotePath.compare(cloudStallPtr->indexPath()),0);
    ASSERT_EQ(theLocalPath.compare( cloudStallPtr->localPath()),0);
    ASSERT_EQ(theRemotePath.compare(cloudStallPtr->cloudPath()),0);
    ASSERT_TRUE(cloudStallPtr->isCloud());

    // Remove objects
    delete localStallPtr;
    delete cloudStallPtr;
}
