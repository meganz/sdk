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
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onUsersUpdate:tempMegaSDK userList:(tempUserList ? [[MEGAUserList alloc] initWithUserList:tempUserList cMemoryOwn:YES] : nil)];
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
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onNodesUpdate:tempMegaSDK nodeList:(tempNodesList ? [[MEGANodeList alloc] initWithNodeList:tempNodesList cMemoryOwn:YES] : nil)];
        });
    }
}

void DelegateMEGAGlobalListener::onAccountUpdate(mega::MegaApi *api) {
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGAGlobalDelegate> tempListener = this->listener;
    if (listener !=nil && [listener respondsToSelector:@selector(onAccountUpdate:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
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
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onContactRequestsUpdate:tempMegaSDK contactRequestList:(tempContactRequestList ? [[MEGAContactRequestList alloc] initWithMegaContactRequestList:tempContactRequestList cMemoryOwn:YES] : nil)];
        });
    }
}

void DelegateMEGAGlobalListener::onReloadNeeded(mega::MegaApi* api) {
    MEGASdk *tempMegaSDK = this->megaSDK;
    id<MEGAGlobalDelegate> tempListener = this->listener;
    if (listener !=nil && [listener respondsToSelector:@selector(onReloadNeeded:)]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onReloadNeeded:tempMegaSDK];
        });
    }
}

void DelegateMEGAGlobalListener::onEvent(mega::MegaApi *api, mega::MegaEvent *event) {
    if (listener != nil && [listener respondsToSelector:@selector(onEvent:event:)]) {
        MegaEvent *tempEvent = event->copy();
        MEGASdk *tempMegaSDK = this->megaSDK;
        id<MEGAGlobalDelegate> tempListener = this->listener;
        dispatch_async(dispatch_get_main_queue(), ^{
            [tempListener onEvent:tempMegaSDK event:(tempEvent ? [[MEGAEvent alloc] initWithMegaEvent:tempEvent cMemoryOwn:YES] : nil)];
        });
    }
}
