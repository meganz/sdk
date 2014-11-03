//
//  DelegateMTransferListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMTransferListener.h"
#import "MTransfer+init.h"
#import "MError+init.h"

using namespace mega;

DelegateMTransferListener::DelegateMTransferListener(MegaSDK *megaSDK, void *listener, bool singleListener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
    this->singleListener = singleListener;
}

void *DelegateMTransferListener::getUserListener() {
    return listener;
}

void DelegateMTransferListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        id<MTransferDelegate> delegate = (__bridge id<MTransferDelegate>)listener;
        [delegate onTransferStart:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES]];
    }
}

void DelegateMTransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        id<MTransferDelegate> delegate = (__bridge id<MTransferDelegate>)listener;
        [delegate onTransferFinish:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e->copy() cMemoryOwn:YES]];
        //TODO:free
        //        if (singleListener)
        //            megaSDK->freeRequestListener(this);
    }
}

void DelegateMTransferListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            id<MTransferDelegate> delegate = (__bridge  id<MTransferDelegate>)listener;
            [delegate onTransferUpdate:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMTransferListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            id<MTransferDelegate> delegate = (__bridge  id<MTransferDelegate>)listener;
            [delegate onTransferTemporaryError:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e cMemoryOwn:YES]];
        });
    }
}
