//
//  DelegateMGlobalListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMGlobalListener.h"

DelegateMGlobalListener::DelegateMGlobalListener(MegaSDK *megaSDK, id<MGlobalListenerDelegate>listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

id<MGlobalListenerDelegate> DelegateMGlobalListener::getUserListener() {
    return listener;
}

void DelegateMGlobalListener::onUsersUpdate(mega::MegaApi *api) {
    if (listener !=nil) {
        [listener onUsersUpdate:this->megaSDK];
    }
}

void DelegateMGlobalListener::onNodesUpdate(mega::MegaApi *api) {
    if (listener !=nil) {
        [listener onNodesUpdate:this->megaSDK];
    }
}

void DelegateMGlobalListener::onReloadNeeded(mega::MegaApi* api) {
    if (listener !=nil) {
        [listener onReloadNeeded:this->megaSDK];
    }
}