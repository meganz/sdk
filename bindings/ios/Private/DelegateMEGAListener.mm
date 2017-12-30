/**
 * @file DelegateMEGAListener.mm
 * @brief Listener to reveice and send events to the app
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
#import "DelegateMEGAListener.h"
#import "MEGATransfer+init.h"
#import "MEGAError+init.h"
#import "MEGARequest+init.h"
#import "MEGANodeList+init.h"
#import "MEGAUserList+init.h"
#import "MEGAContactRequestList+init.h"
#import "MEGAEvent+init.h"

using namespace mega;

DelegateMEGAListener::DelegateMEGAListener(MEGASdk *megaSDK, id<MEGADelegate>listener) : DelegateMEGABaseListener::DelegateMEGABaseListener(megaSDK) {
    this->listener = listener;
}

id<MEGADelegate>DelegateMEGAListener::getUserListener() {
    return listener;
}

void DelegateMEGAListener::onRequestStart(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestStart:request:)]) {
        MegaRequest *tempRequest = request->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onRequestStart:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGAListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestFinish:request:error:)]) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onRequestFinish:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGAListener::onRequestUpdate(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestUpdate:request:)]) {
        MegaRequest *tempRequest = request->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onRequestUpdate:this->megaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGAListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e) {
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

void DelegateMEGAListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferStart:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onTransferStart:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGAListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferFinish:transfer:error:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onTransferFinish:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGAListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferUpdate:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onTransferUpdate:this->megaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
            }
        });
    }
}

void DelegateMEGAListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
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

void DelegateMEGAListener::onUsersUpdate(mega::MegaApi *api, mega::MegaUserList *userList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onUsersUpdate:userList:)]) {
        MegaUserList *tempUserList = NULL;
        if (userList) {
            tempUserList = userList->copy();
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onUsersUpdate:this->megaSDK userList:(tempUserList ? [[MEGAUserList alloc] initWithUserList:tempUserList cMemoryOwn:YES] : nil)];
            }
        });
        
    }
}

void DelegateMEGAListener::onNodesUpdate(mega::MegaApi *api, mega::MegaNodeList *nodeList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onNodesUpdate:nodeList:)]) {
        MegaNodeList *tempNodesList = NULL;
        if (nodeList) {
            tempNodesList = nodeList->copy();
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onNodesUpdate:this->megaSDK nodeList:(tempNodesList ? [[MEGANodeList alloc] initWithNodeList:tempNodesList cMemoryOwn:YES] : nil)];
            }
        });
    }
}

void DelegateMEGAListener::onAccountUpdate(mega::MegaApi *api) {
    if (listener !=nil && [listener respondsToSelector:@selector(onAccountUpdate:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onAccountUpdate:this->megaSDK];
            }
        });
    }
}

void DelegateMEGAListener::onContactRequestsUpdate(mega::MegaApi* api, mega::MegaContactRequestList* contactRequestList) {
    if (listener != nil && [listener respondsToSelector:@selector(onContactRequestsUpdate:contactRequestList:)]) {
        MegaContactRequestList *tempContactRequestList = NULL;
        if(contactRequestList) {
            tempContactRequestList = contactRequestList->copy();
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onContactRequestsUpdate:this->megaSDK contactRequestList:(tempContactRequestList ? [[MEGAContactRequestList alloc] initWithMegaContactRequestList:tempContactRequestList cMemoryOwn:YES] : nil)];
            }
        });
    }
}

void DelegateMEGAListener::onReloadNeeded(MegaApi *api) {
    if (listener != nil && [listener respondsToSelector:@selector(onReloadNeeded:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onReloadNeeded:this->megaSDK];
            }
        });
    }
}

void DelegateMEGAListener::onEvent(mega::MegaApi *api, mega::MegaEvent *event) {
    switch (event->getType()) {
        case MegaEvent::EVENT_ACCOUNT_CONFIRMATION:            
            if (listener != nil && [listener respondsToSelector:@selector(onEvent:event:)]) {
                MegaEvent *tempEvent = NULL;
                if(event) {
                    tempEvent = event->copy();
                }
                
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (this->validListener) {
                        [this->listener onEvent:this->megaSDK event:(tempEvent ? [[MEGAEvent alloc] initWithMegaEvent:tempEvent cMemoryOwn:YES] : nil)];
                    }
                });
            }
            break;
            
        default:
            break;
    }
}
