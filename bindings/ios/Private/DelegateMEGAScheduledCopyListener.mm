/**
 * @file DelegateMEGAScheduledCopyListener.mm
 * @brief Listener to reveice and send transfer events to the app
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
#import "DelegateMEGAScheduledCopyListener.h"
#import "MEGAScheduledCopy+init.h"
#import "MEGAError+init.h"
#import "MEGASdk+init.h"

using namespace mega;

DelegateMEGAScheduledCopyListener::DelegateMEGAScheduledCopyListener(MEGASdk *megaSDK, id<MEGAScheduledCopyDelegate>listener, ListenerQueueType queueType) {
    this->megaSDK = megaSDK;
    this->listener = listener;
    this->queueType = queueType;
}

id<MEGAScheduledCopyDelegate>DelegateMEGAScheduledCopyListener::getUserListener() {
    return listener;
}

void DelegateMEGAScheduledCopyListener::onBackupStateChanged(mega::MegaApi *api, mega::MegaScheduledCopy *backup) {
    if (listener != nil && [listener respondsToSelector:@selector(onBackupStateChanged:backup:request:)]) {
        MegaScheduledCopy *tempBackup = backup->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAScheduledCopyDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onBackupStateChanged:tempMegaSDK backup:[[MEGAScheduledCopy alloc] initWithMegaScheduledCopy:tempBackup cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAScheduledCopyListener::onBackupStart(mega::MegaApi *api, mega::MegaScheduledCopy *backup) {
    if (listener != nil && [listener respondsToSelector:@selector(onBackupStateChanged:backup:request:)]) {
        MegaScheduledCopy *tempBackup = backup->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAScheduledCopyDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onBackupStart:tempMegaSDK backup:[[MEGAScheduledCopy alloc] initWithMegaScheduledCopy:tempBackup cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAScheduledCopyListener::onBackupFinish(mega::MegaApi *api, mega::MegaScheduledCopy *backup, mega::MegaError *e) {
    MegaScheduledCopy *tempBackup = backup->copy();
    MegaError *tempError = e->copy();
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGAScheduledCopyDelegate> tempListener = this->listener;
    dispatch(this->queueType, ^{
        [tempListener onBackupFinish:tempMegaSDK backup:[[MEGAScheduledCopy alloc] initWithMegaScheduledCopy:tempBackup cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
    });
}

void DelegateMEGAScheduledCopyListener::onBackupUpdate(mega::MegaApi *api, mega::MegaScheduledCopy *backup) {
    MegaScheduledCopy *tempBackup = backup->copy();
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGAScheduledCopyDelegate> tempListener = this->listener;
    dispatch(this->queueType, ^{
        [tempListener onBackupUpdate:tempMegaSDK backup:[[MEGAScheduledCopy alloc] initWithMegaScheduledCopy:tempBackup cMemoryOwn:YES]];
    });
}

void DelegateMEGAScheduledCopyListener::onBackupTemporaryError(mega::MegaApi *api, mega::MegaScheduledCopy *backup, mega::MegaError *e) {
    MegaScheduledCopy *tempBackup = backup->copy();
    MegaError *tempError = e->copy();
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGAScheduledCopyDelegate> tempListener = this->listener;
    dispatch(this->queueType, ^{
        [tempListener onBackupTemporaryError:tempMegaSDK backup:[[MEGAScheduledCopy alloc] initWithMegaScheduledCopy:tempBackup cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
    });
}
