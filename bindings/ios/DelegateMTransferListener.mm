//
//  DelegateMTransferListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMTransferListener.h"
#import "MTransfer+init.h"
#import "MError+init.h"
#import "MegaSDK+init.h"

using namespace mega;

DelegateMTransferListener::DelegateMTransferListener(MegaSDK *megaSDK, id<MTransferDelegate>listener, bool singleListener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
    this->singleListener = singleListener;
}

id<MTransferDelegate>DelegateMTransferListener::getUserListener() {
    return listener;
}

void DelegateMTransferListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferStart:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMTransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferFinish:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            if (singleListener) {
                [megaSDK freeTransferListener:this];
            }
        });
    }
}

void DelegateMTransferListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferUpdate:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMTransferListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferTemporaryError:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}
