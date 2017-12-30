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

DelegateMEGATransferListener::DelegateMEGATransferListener(MEGASdk *megaSDK, id<MEGATransferDelegate>listener, bool singleListener) : DelegateMEGABaseListener::DelegateMEGABaseListener(megaSDK, singleListener) {
    this->listener = listener;
}

id<MEGATransferDelegate>DelegateMEGATransferListener::getUserListener() {
    return listener;
}

void DelegateMEGATransferListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferStart:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onTransferStart:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGATransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferFinish:transfer:error:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onTransferFinish:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            }
            if (this->singleListener) {
                [this->megaSDK freeTransferListener:this];
            }
        });
    }
}

void DelegateMEGATransferListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferUpdate:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onTransferUpdate:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGATransferListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferTemporaryError:transfer:error:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onTransferTemporaryError:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            }
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
