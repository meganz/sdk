//
//  DelegateMEGARequestListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMEGARequestListener.h"
#import "MEGARequest+init.h"
#import "MEGAError+init.h"
#import "MEGASdk+init.h"

using namespace mega;

DelegateMEGARequestListener::DelegateMEGARequestListener(MEGASdk *megaSDK, id<MEGARequestDelegate>listener, bool singleListener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
    this->singleListener = singleListener;
}

id<MEGARequestDelegate>DelegateMEGARequestListener::getUserListener() {
    return listener;
}

void DelegateMEGARequestListener::onRequestStart(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestStart:request:)]) {
        MegaRequest *tempRequest = request->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestStart:this->megaSDK request:[[MEGARequest alloc]initWithMegaRequest:tempRequest cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGARequestListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestFinish:request:error:)]) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestFinish:this->megaSDK request:[[MEGARequest alloc]initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            if (singleListener) {
                [megaSDK freeRequestListener:this];
            }
        });
    }
}

void DelegateMEGARequestListener::onRequestUpdate(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestUpdate:request:)]) {
        MegaRequest *tempRequest = request->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestUpdate:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGARequestListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestTemporaryError:request:error:)]) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onRequestTemporaryError:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}
