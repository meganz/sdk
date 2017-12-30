/**
 * @file DelegateMEGARequestListener.mm
 * @brief Listener to reveice and send request events to the app
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
#import "DelegateMEGARequestListener.h"
#import "MEGARequest+init.h"
#import "MEGAError+init.h"
#import "MEGASdk+init.h"

using namespace mega;

DelegateMEGARequestListener::DelegateMEGARequestListener(MEGASdk *megaSDK, id<MEGARequestDelegate>listener, bool singleListener) : DelegateMEGABaseListener::DelegateMEGABaseListener(megaSDK, singleListener) {
    this->listener = listener;
}

id<MEGARequestDelegate>DelegateMEGARequestListener::getUserListener() {
    return listener;
}

void DelegateMEGARequestListener::onRequestStart(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestStart:request:)]) {
        MegaRequest *tempRequest = request->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onRequestStart:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGARequestListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestFinish:request:error:)]) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onRequestFinish:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            }
            if (this->singleListener) {
                [megaSDK freeRequestListener:this];
            }
        });
    }
}

void DelegateMEGARequestListener::onRequestUpdate(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestUpdate:request:)]) {
        MegaRequest *tempRequest = request->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onRequestUpdate:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGARequestListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestTemporaryError:request:error:)]) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onRequestTemporaryError:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            }
        });
    }
}
