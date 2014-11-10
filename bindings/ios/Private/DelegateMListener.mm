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

using namespace mega;

DelegateMListener::DelegateMListener(MegaSDK *megaSDK, id<MListenerDelegate>listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

id<MListenerDelegate>DelegateMListener::getUserListener() {
    return listener;
}

void DelegateMListener::onRequestStart(MegaApi *api, MegaRequest *request) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestStart:this->megaSDK request:[[MRequest alloc]initWithMegaRequest:request->copy() cMemoryOwn:YES]];
        });
    }
}

void DelegateMListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestFinish:this->megaSDK request:[[MRequest alloc]initWithMegaRequest:request->copy() cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e->copy() cMemoryOwn:YES]];
        });
    }
}

void DelegateMListener::onRequestUpdate(MegaApi *api, MegaRequest *request) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestUpdate:this->megaSDK request:[[MRequest alloc]initWithMegaRequest:request->copy() cMemoryOwn:YES]];
        });
    }
}

void DelegateMListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestTemporaryError:this->megaSDK request:[[MRequest alloc]initWithMegaRequest:request->copy() cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e->copy() cMemoryOwn:YES]];
        });
    }

}

void DelegateMListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferStart:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES]];
        });
    }
}

void DelegateMListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferFinish:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e->copy() cMemoryOwn:YES]];
        });
    }
}

void DelegateMListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferUpdate:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES]];
        });
    }
}

void DelegateMListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onTransferTemporaryError:this->megaSDK transfer:[[MTransfer alloc] initWithMegaTransfer:transfer->copy() cMemoryOwn:YES] error:[[MError alloc] initWithMegaError:e->copy() cMemoryOwn:YES]];
        });
    }
}

void DelegateMListener::onUsersUpdate(MegaApi *api) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onUsersUpdate:this->megaSDK];
        });
    }
}

void DelegateMListener::onNodesUpdate(MegaApi *api) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onNodesUpdate:this->megaSDK];
        });
    }
}

void DelegateMListener::onReloadNeeded(MegaApi *api) {
    if (listener != nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onReloadNeeded:this->megaSDK];
        });
    }
}
