/**
 * (c) 2021 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "megaapi_impl.h"
#include "mega/sync.h"

#ifdef ENABLE_SYNC

namespace SyncConflictTests {

using testing::Test;

class SyncConflictTest : public Test {
    public:
        SyncConflictTest(){}
        ~SyncConflictTest(){}
    private:

    protected:
};

/**
 * A Local change where Folder A is moved into folder B and
 * a Cloud change where Folder B is moved into folder A
 */
TEST(SyncConflictTest, LocalStallsWithChanges_Local_AB_Cloud_BA) { 
    mega::SyncStallInfo syncStallInfo;

    // Fresh from the oven. No stalls
    ASSERT_TRUE(syncStallInfo.empty());
    ASSERT_FALSE(syncStallInfo.hasImmediateStallReason());

    // Superposition!. Which universe should we choose from?
    const std::string theLocalPath  = "/here/there/be/Chicken/Egg";
    const std::string theRemotePath = "/here/there/be/Egg/Chicken";
    const auto localPath = mega::LocalPath::fromPlatformEncodedAbsolute(theLocalPath);

    syncStallInfo.waitingLocal(  
        localPath, 
        localPath,
        theRemotePath,
        mega::SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose
     );

    ASSERT_FALSE(syncStallInfo.empty()); // Houston! We have a conflict.
    ASSERT_TRUE(syncStallInfo.hasImmediateStallReason()); // User should choose

}


/**
 * A Cloud change where Folder B is moved into folder A and
 * a Local change where Folder A is moved into folder B
 */
TEST(SyncConflictTest, CloudStallsWithChanges_Cloud_AB_Local_BA) { 
    mega::SyncStallInfo syncStallInfo;
    ASSERT_TRUE(syncStallInfo.empty());

    // Fresh from the oven. No stalls
    ASSERT_TRUE(syncStallInfo.empty());
    ASSERT_FALSE(syncStallInfo.hasImmediateStallReason());

    // Superposition!. Which universe should we choose from?
    const std::string theLocalPath  = "/here/there/be/Chicken/Egg";
    const std::string theRemotePath = "/here/there/be/Egg/Chicken";
    const auto localPath = mega::LocalPath::fromPlatformEncodedAbsolute(theLocalPath);

    syncStallInfo.waitingCloud(
        theRemotePath,
        theRemotePath,
        localPath,
        mega::SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose
     );

    ASSERT_FALSE(syncStallInfo.empty());
    ASSERT_TRUE(syncStallInfo.hasImmediateStallReason()); // User should choose
}

TEST(SyncConflictTest, RetrieveSyncStallInfo_Private_Inteface) {
    mega::SyncStallInfo syncStallInfo;
    ASSERT_TRUE(syncStallInfo.empty());
    // Conflicting changes introduced here
}

TEST(SyncConflictTest, RetrieveSyncStallInfo_Public_Interface) {
    mega::SyncStallInfo syncStallInfo;
    ASSERT_TRUE(syncStallInfo.empty());
    // Conflicting changes introduced here
}

}

#endif // ENABLE_SYNC
