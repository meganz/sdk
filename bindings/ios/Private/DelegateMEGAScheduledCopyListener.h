/**
 * @file DelegateMEGAScheduledCopyListener.h
 * @brief Listener to reveice and send backup events to the app
 *
 * (c) 2023 by Mega Limited, Auckland, New Zealand
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
#import "MEGAScheduledCopyDelegate.h"
#import "megaapi.h"
#import "MEGASdk.h"
#import "ListenerDispatch.h"

class DelegateMEGAScheduledCopyListener : public mega::MegaScheduledCopyListener {

public:
    DelegateMEGAScheduledCopyListener(MEGASdk *megaSDK, id<MEGAScheduledCopyDelegate>listener, ListenerQueueType queueType);
    id<MEGAScheduledCopyDelegate>getUserListener();
    
    void onBackupStateChanged(mega::MegaApi *api, mega::MegaScheduledCopy *backup);
    void onBackupStart(mega::MegaApi *api, mega::MegaScheduledCopy *backup);
    void onBackupFinish(mega::MegaApi *api, mega::MegaScheduledCopy *backup, mega::MegaError *e);
    void onBackupUpdate(mega::MegaApi *api, mega::MegaScheduledCopy *backup);
    void onBackupTemporaryError(mega::MegaApi *api, mega::MegaScheduledCopy *backup, mega::MegaError *e);
    
private:
    MEGASdk *megaSDK;
    id<MEGAScheduledCopyDelegate>listener;
    ListenerQueueType queueType;
};

