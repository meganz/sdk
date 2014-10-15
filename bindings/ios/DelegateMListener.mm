//
//  DelegateMListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMListener.h"
#import "MTransfer+init.h"
#import "MError+init.h"
#import "MRequest+init.h"

DelegateMListener::DelegateMListener(MegaSDK *megaSDK, void *listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

void *DelegateMListener::getUserListener() {
    return listener;
}

void DelegateMListener::onRequestStart(MegaApi *api, MegaRequest *request) {
    if (listener != nil) {
        id<MRequestDelegate> delegate = (__bridge id<MRequestDelegate>)listener;
        [delegate onRequestStart:this->megaSDK request:[[MRequest alloc]initWithMegaRequest:request->copy() cMemoryOwn:YES]];
    }
}

void DelegateMListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil) {
        id<MRequestDelegate> delegate = (__bridge id<MRequestDelegate>)listener;
        [delegate onRequestFinish:this->megaSDK request:[[MRequest alloc]initWithMegaRequest:request->copy() cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e->copy() cMemoryOwn:YES]];
        //TODO:free
        //        if (singleListener)
        //            megaSDK->freeRequestListener(this);
    }
}

void DelegateMListener::onRequestUpdate(MegaApi *api, MegaRequest *request) {
}

void DelegateMListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e) {
}

void DelegateMListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        id<MTransferDelegate> delegate = (__bridge id<MTransferDelegate>)listener;
        [delegate onTransferStart:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES]];
    }
}

void DelegateMListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        id<MTransferDelegate> delegate = (__bridge id<MTransferDelegate>)listener;
        [delegate onTransferFinish:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e->copy() cMemoryOwn:YES]];
        //TODO: free
        //        if (singleListener)
        //            megaSDK->freeRequestListener(this);
    }
}

void DelegateMListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
}

void DelegateMListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
}

void DelegateMListener::onUsersUpdate(MegaApi *api) {
    if (listener != nil) {
        id<MListenerDelegate> delegate = (__bridge id<MListenerDelegate>)listener;
        [delegate onUsersUpdate:this->megaSDK];
    }
}

void DelegateMListener::onNodesUpdate(MegaApi *api) {
    if (listener != nil) {
        id<MListenerDelegate> delegate = (__bridge id<MListenerDelegate>)listener;
        [delegate onNodesUpdate:this->megaSDK];
    }
}

void DelegateMListener::onReloadNeeded(MegaApi *api) {
    if (listener != nil) {
        id<MListenerDelegate> delegate = (__bridge id<MListenerDelegate>)listener;
        [delegate onReloadNeeded:this->megaSDK];
    }
}
