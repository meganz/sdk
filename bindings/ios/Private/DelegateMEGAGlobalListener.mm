/**
 * @file DelegateMEGAGlobalListener.mm
 * @brief Listener to reveice and send global events to the app
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
#import "DelegateMEGAGlobalListener.h"
#import "MEGAUserList+init.h"
#import "MEGAUserAlertList+init.h"
#import "MEGANodeList+init.h"
#import "MEGAContactRequestList+init.h"
#import "MEGAEvent+init.h"
#import "MEGASet+init.h"
#import "MEGASetElement+init.h"

using namespace mega;

DelegateMEGAGlobalListener::DelegateMEGAGlobalListener(MEGASdk *megaSDK, id<MEGAGlobalDelegate>listener, ListenerQueueType queueType) {
    this->megaSDK = megaSDK;
    this->listener = listener;
    this->queueType = queueType;
}

id<MEGAGlobalDelegate> DelegateMEGAGlobalListener::getUserListener() {
    return listener;
}

void DelegateMEGAGlobalListener::onUsersUpdate(mega::MegaApi *api, mega::MegaUserList *userList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onUsersUpdate:userList:)]) {
        MegaUserList *tempUserList = NULL;
        if (userList) {
            tempUserList = userList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onUsersUpdate:tempMegaSDK userList:(tempUserList ? [[MEGAUserList alloc] initWithUserList:tempUserList cMemoryOwn:YES] : nil)];
        });
        
    }
}

void DelegateMEGAGlobalListener::onUserAlertsUpdate(mega::MegaApi *api, mega::MegaUserAlertList *userAlertList) {
    if (listener && [listener respondsToSelector:@selector(onUserAlertsUpdate:userAlertList:)]) {
        MegaUserAlertList *tempUserAlertList = NULL;
        if (userAlertList) {
            tempUserAlertList = userAlertList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onUserAlertsUpdate:tempMegaSDK userAlertList:(tempUserAlertList ? [[MEGAUserAlertList alloc] initWithMegaUserAlertList:tempUserAlertList cMemoryOwn:YES] : nil)];
        });
    }
}

void DelegateMEGAGlobalListener::onNodesUpdate(mega::MegaApi *api, mega::MegaNodeList *nodeList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onNodesUpdate:nodeList:)]) {
        MegaNodeList *tempNodesList = NULL;
        if (nodeList) {
            tempNodesList = nodeList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onNodesUpdate:tempMegaSDK nodeList:(tempNodesList ? [[MEGANodeList alloc] initWithNodeList:tempNodesList cMemoryOwn:YES] : nil)];
        });
    }
}

void DelegateMEGAGlobalListener::onSetsUpdate(mega::MegaApi *api, mega::MegaSetList *setList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onSetsUpdate:sets:)]) {
        int size = 0;
        if (setList) {
            size = setList->size();
        }
        NSMutableArray *sets = [[NSMutableArray alloc] initWithCapacity:size];
        
        for (int i = 0; i < size; i++) {
            MEGASet *megaSet = [[MEGASet alloc] initWithMegaSet:setList->get(i)->copy() cMemoryOwn:YES];
            [sets addObject:megaSet];
        }
        
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        
        dispatch(this->queueType, ^{
            [tempListener onSetsUpdate:tempMegaSDK sets:[sets copy]];
        });
    }
}

void DelegateMEGAGlobalListener::onSetElementsUpdate(mega::MegaApi* api, mega::MegaSetElementList* setElementList) {
    if (listener !=nil && [listener respondsToSelector:@selector(onSetElementsUpdate:setElements:)]) {
        int size = 0;
        if (setElementList) {
            size = setElementList->size();
        }
        NSMutableArray *setsElements = [[NSMutableArray alloc] initWithCapacity:size];
        
        for (int i = 0; i < size; i++) {
            MEGASetElement *megaSetElement = [[MEGASetElement alloc] initWithMegaSetElement:setElementList->get(i)->copy() cMemoryOwn:YES];
            [setsElements addObject:megaSetElement];
        }
        
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        
        dispatch(this->queueType, ^{
            [tempListener onSetElementsUpdate:tempMegaSDK setElements:[setsElements copy]];
        });
    }
}

void DelegateMEGAGlobalListener::onAccountUpdate(mega::MegaApi *api) {
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGAGlobalDelegate> tempListener = this->listener;
    if (listener !=nil && [listener respondsToSelector:@selector(onAccountUpdate:)]) {
        dispatch(this->queueType, ^{
            [tempListener onAccountUpdate:tempMegaSDK];
        });
    }
}

void DelegateMEGAGlobalListener::onContactRequestsUpdate(mega::MegaApi* api, mega::MegaContactRequestList* contactRequestList) {
    if (listener != nil && [listener respondsToSelector:@selector(onContactRequestsUpdate:contactRequestList:)]) {
        MegaContactRequestList *tempContactRequestList = NULL;
        if(contactRequestList) {
            tempContactRequestList = contactRequestList->copy();
        }
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onContactRequestsUpdate:tempMegaSDK contactRequestList:(tempContactRequestList ? [[MEGAContactRequestList alloc] initWithMegaContactRequestList:tempContactRequestList cMemoryOwn:YES] : nil)];
        });
    }
}

void DelegateMEGAGlobalListener::onReloadNeeded(mega::MegaApi* api) {
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGAGlobalDelegate> tempListener = this->listener;
    if (listener !=nil && [listener respondsToSelector:@selector(onReloadNeeded:)]) {
        dispatch(this->queueType, ^{
            [tempListener onReloadNeeded:tempMegaSDK];
        });
    }
}

void DelegateMEGAGlobalListener::onEvent(mega::MegaApi *api, mega::MegaEvent *event) {
    if (listener != nil && [listener respondsToSelector:@selector(onEvent:event:)]) {
        MegaEvent *tempEvent = event->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        dispatch(this->queueType, ^{
            [tempListener onEvent:tempMegaSDK event:(tempEvent ? [[MEGAEvent alloc] initWithMegaEvent:tempEvent cMemoryOwn:YES] : nil)];
        });
    }
}
