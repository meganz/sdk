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
#import "MEGAUserAlertList+init.h"
#import "MEGAContactRequestList+init.h"
#import "MEGAEvent+init.h"
#import "MEGASet+init.h"
#import "MEGASetElement+init.h"

using namespace mega;

DelegateMEGAListener::DelegateMEGAListener(MEGASdk *megaSDK, id<MEGADelegate>listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

id<MEGADelegate>DelegateMEGAListener::getUserListener() {
    return listener;
}

void DelegateMEGAListener::onRequestStart(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestStart:request:)]) {
        MegaRequest *tempRequest = request->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onRequestStart:tempMegaSDK request:[[MEGARequest alloc]initWithMegaRequest:tempRequest cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestFinish:request:error:)]) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onRequestFinish:tempMegaSDK request:[[MEGARequest alloc]initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onRequestUpdate(MegaApi *api, MegaRequest *request) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestUpdate:request:)]) {
        MegaRequest *tempRequest = request->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onRequestUpdate:tempMegaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onRequestTemporaryError:request:error:)]) {
        MegaRequest *tempRequest = request->copy();
        MegaError *tempError = e->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onRequestTemporaryError:tempMegaSDK request:[[MEGARequest alloc] initWithMegaRequest:tempRequest cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }

}

void DelegateMEGAListener::onTransferStart(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferStart:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onTransferStart:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferFinish:transfer:error:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onTransferFinish:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferUpdate:transfer:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onTransferUpdate:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e) {
    if (listener != nil && [listener respondsToSelector:@selector(onTransferTemporaryError:transfer:error:)]) {
        MegaTransfer *tempTransfer = transfer->copy();
        MegaError *tempError = e->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onTransferTemporaryError:tempMegaSDK transfer:[[MEGATransfer alloc] initWithMegaTransfer:tempTransfer cMemoryOwn:YES] error:[[MEGAError alloc] initWithMegaError:tempError cMemoryOwn:YES]];
        });
    }
}

void DelegateMEGAListener::onUsersUpdate(mega::MegaApi *api, mega::MegaUserList *userList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onUsersUpdate:userList:)]) {
        MegaUserList *tempUserList = NULL;
        if (userList) {
            tempUserList = userList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onUsersUpdate:tempMegaSDK userList:(tempUserList ? [[MEGAUserList alloc] initWithUserList:tempUserList cMemoryOwn:YES] : nil)];
        });
        
    }
}

void DelegateMEGAListener::onUserAlertsUpdate(mega::MegaApi *api, mega::MegaUserAlertList *userAlertList) {
    if (listener && [listener respondsToSelector:@selector(onUserAlertsUpdate:userAlertList:)]) {
        MegaUserAlertList *tempUserAlertList = NULL;
        if (userAlertList) {
            tempUserAlertList = userAlertList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onUserAlertsUpdate:tempMegaSDK userAlertList:(tempUserAlertList ? [[MEGAUserAlertList alloc] initWithMegaUserAlertList:tempUserAlertList cMemoryOwn:YES] : nil)];
        });
    }
}

void DelegateMEGAListener::onNodesUpdate(mega::MegaApi *api, mega::MegaNodeList *nodeList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onNodesUpdate:nodeList:)]) {
        MegaNodeList *tempNodesList = NULL;
        if (nodeList) {
            tempNodesList = nodeList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onNodesUpdate:tempMegaSDK nodeList:(tempNodesList ? [[MEGANodeList alloc] initWithNodeList:tempNodesList cMemoryOwn:YES] : nil)];
        });
    }
}

void DelegateMEGAListener::onSetsUpdate(mega::MegaApi *api, mega::MegaSetList *setList) {
    if (listener == nil || ![listener respondsToSelector:@selector(onSetsUpdate:sets:)]) {
        return;
    }
    int size = 0;
    if (setList) {
        size = setList->size();
    }
    
    NSMutableArray<MEGASet *> *sets = [[NSMutableArray alloc] initWithCapacity:size];
    
    for (int i = 0; i < size; i++) {
        MEGASet *megaSet = [[MEGASet alloc] initWithMegaSet:setList->get(i)->copy() cMemoryOwn:YES];
        [sets addObject:megaSet];
    }
    
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGADelegate> tempListener = this->listener;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [tempListener onSetsUpdate:tempMegaSDK sets:[sets copy]];
    });
}

void DelegateMEGAListener::onSetElementsUpdate(mega::MegaApi* api, mega::MegaSetElementList* setElementList) {
    if (listener == nil || ![listener respondsToSelector:@selector(onSetElementsUpdate:setElements:)]) {
        return;
    }
    int size = 0;
    if (setElementList) {
        size = setElementList->size();
    }
    
    NSMutableArray<MEGASetElement *> *setsElements = [[NSMutableArray alloc] initWithCapacity:size];
    
    for (int i = 0; i < size; i++) {
        MEGASetElement *megaSetElement = [[MEGASetElement alloc] initWithMegaSetElement:setElementList->get(i)->copy() cMemoryOwn:YES];
        [setsElements addObject:megaSetElement];
    }
    
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGADelegate> tempListener = this->listener;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [tempListener onSetElementsUpdate:tempMegaSDK setElements:[setsElements copy]];
    });
}

void DelegateMEGAListener::onAccountUpdate(mega::MegaApi *api) {
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGADelegate> tempListener = this->listener;
    if (listener !=nil && [listener respondsToSelector:@selector(onAccountUpdate:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onAccountUpdate:tempMegaSDK];
        });
    }
}

void DelegateMEGAListener::onContactRequestsUpdate(mega::MegaApi* api, mega::MegaContactRequestList* contactRequestList) {
    if (listener != nil && [listener respondsToSelector:@selector(onContactRequestsUpdate:contactRequestList:)]) {
        MegaContactRequestList *tempContactRequestList = NULL;
        if(contactRequestList) {
            tempContactRequestList = contactRequestList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onContactRequestsUpdate:tempMegaSDK contactRequestList:(tempContactRequestList ? [[MEGAContactRequestList alloc] initWithMegaContactRequestList:tempContactRequestList cMemoryOwn:YES] : nil)];
        });
    }
    
}

void DelegateMEGAListener::onReloadNeeded(MegaApi *api) {
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGADelegate> tempListener = this->listener;
    if (listener != nil && [listener respondsToSelector:@selector(onReloadNeeded:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onReloadNeeded:tempMegaSDK];
        });
    }
}


void DelegateMEGAListener::onEvent(mega::MegaApi *api, mega::MegaEvent *event) {
    if (listener != nil && [listener respondsToSelector:@selector(onEvent:event:)]) {
        MegaEvent *tempEvent = event->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGADelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onEvent:tempMegaSDK event:(tempEvent ? [[MEGAEvent alloc] initWithMegaEvent:tempEvent cMemoryOwn:YES] : nil)];
        });
    }
}
