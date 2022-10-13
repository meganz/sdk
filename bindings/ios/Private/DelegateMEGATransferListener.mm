/**
 * @file DelegateMEGATransferListener.mm
 * @brief Listener to reveice and send transfer events to the app
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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
#import "DelegateMEGATransferListener.h"
#import "MEGATransfer+init.h"
#import "MEGAError+init.h"
#import "MEGASdk+init.h"

using namespace mega;

DelegateMEGATransferListener::DelegateMEGATransferListener(MEGASdk *megaSDK, id<MEGATransferDelegate>listener, bool singleListener, ListenerQueueType queueType) {
    this->megaSDK = megaSDK;
    this->listener = listener;
    this->singleListener = singleListener;
    this->queueType = queueType;
}

id<MEGATransferDelegate>DelegateMEGATransferListener::getUserListener() {
    return listener;
}

void DelegateMEGATransferListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferStart:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGATransferDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onTransferStart:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGATransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferFinish:transfer:error:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGATransferDelegate> tempListener = this->listener;
        bool tempSingleListener = singleListener;
        dispatch(this->queueType, ^{
            [tempListener onTransferFinish:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            if (tempSingleListener) {
                [tempMegaSDK freeTransferListener:this];
            }
        });
    }
}

void DelegateMEGATransferListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferUpdate:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGATransferDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onTransferUpdate:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGATransferListener::onFolderTransferUpdate(MegaApi *api, mega::MegaTransfer *transfer, int stage, uint32_t foldercount, uint32_t createdfoldercount, uint32_t filecount, const char *currentFolder, const char *currentFileLeafname) {
    if (listener != nil && [listener respondsToSelector:@selector(onFolderTransferUpdate:transfer:stage:folderCount:createdFolderCount:fileCount:currentFolder:currentFileLeafName:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        NSString *currentFolderString = currentFolder ? [NSString stringWithUTF8String:currentFolder] : nil;
        NSString *currentFileLeafNameString = currentFileLeafname ? [NSString stringWithUTF8String:currentFileLeafname] : nil;
        id<MEGATransferDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onFolderTransferUpdate:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] stage:MEGATransferStage(stage) folderCount:foldercount createdFolderCount:createdfoldercount fileCount:filecount currentFolder:currentFolderString currentFileLeafName:currentFileLeafNameString];
        });
    }
}

void DelegateMEGATransferListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferTemporaryError:transfer:error:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGATransferDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onTransferTemporaryError:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}

bool DelegateMEGATransferListener::onTransferData(mega::MegaApi *api, mega::MegaTransfer *transfer, char *buffer, size_t size) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferData:transfer:buffer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        return [listener onTransferData:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] buffer:[[NSData alloc] initWithBytes:transfer->getLastBytes() length:(long)transfer->getDeltaSize()]];
    }
    return false;
}
