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
#import "MEGANodeList+init.h"
#import "MEGAContactRequestList+init.h"
#import "MEGAEvent+init.h"

using namespace mega;

DelegateMEGAGlobalListener::DelegateMEGAGlobalListener(MEGASdk *megaSDK, id<MEGAGlobalDelegate>listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
    this->validListener = true;
}

id<MEGAGlobalDelegate> DelegateMEGAGlobalListener::getUserListener() {
    return listener;
}

void DelegateMEGAGlobalListener::setValidListener(bool validListener) {
    this->validListener = validListener;
}

void DelegateMEGAGlobalListener::onUsersUpdate(mega::MegaApi *api, mega::MegaUserList *userList) {
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

void DelegateMEGAGlobalListener::onNodesUpdate(mega::MegaApi *api, mega::MegaNodeList *nodeList) {
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

void DelegateMEGAGlobalListener::onAccountUpdate(mega::MegaApi *api) {
    if (listener !=nil && [listener respondsToSelector:@selector(onAccountUpdate:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onAccountUpdate:this->megaSDK];
            }
        });
    }
}

void DelegateMEGAGlobalListener::onContactRequestsUpdate(mega::MegaApi* api, mega::MegaContactRequestList* contactRequestList) {
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

void DelegateMEGAGlobalListener::onReloadNeeded(mega::MegaApi* api) {
    if (this->listener !=nil && [listener respondsToSelector:@selector(onReloadNeeded:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (this->validListener) {
                [this->listener onReloadNeeded:this->megaSDK];
            }
        });
    }
}

void DelegateMEGAGlobalListener::onEvent(mega::MegaApi *api, mega::MegaEvent *event) {
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
