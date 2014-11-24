//
//  DelegateMEGAListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMEGAListener.h"
#import "MEGATransfer+init.h"
#import "MEGAError+init.h"
#import "MEGARequest+init.h"

using namespace mega;

DelegateMEGAListener::DelegateMEGAListener(MEGASdk *megaSDK, id<MEGADelegate>listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

id<MEGADelegate>DelegateMEGAListener::getUserListener() {
    return listener;
}

void DelegateMEGAListener::onRequestStart(MegaApi *api, MegaRequest *request) {
    if (listener != nil) {
        MegaRequest *tempRequest = request->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestStart:this->megaSDK request:[[MEGARequest alloc]initWithMegaRequest:tempRequest cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestFinish:this->megaSDK request:[[MEGARequest alloc]initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onRequestUpdate(MegaApi *api, MegaRequest *request) {
    if (listener != nil) {
        MegaRequest *tempRequest = request->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestUpdate:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestTemporaryError:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }

}

void DelegateMEGAListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferStart:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferFinish:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferUpdate:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferTemporaryError:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onUsersUpdate(MegaApi *api) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onUsersUpdate:this->megaSDK];
        });
    }
}

void DelegateMEGAListener::onNodesUpdate(MegaApi *api) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onNodesUpdate:this->megaSDK];
        });
    }
}

void DelegateMEGAListener::onReloadNeeded(MegaApi *api) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onReloadNeeded:this->megaSDK];
        });
    }
}
