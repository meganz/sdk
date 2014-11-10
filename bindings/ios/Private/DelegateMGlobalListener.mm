//
//  DelegateMGlobalListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMGlobalListener.h"

DelegateMGlobalListener::DelegateMGlobalListener(MegaSDK *megaSDK, id<MGlobalDelegate>listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

id<MGlobalDelegate> DelegateMGlobalListener::getUserListener() {
    return listener;
}

void DelegateMGlobalListener::onUsersUpdate(mega::MegaApi *api) {
    if (listener !=nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onUsersUpdate:this->megaSDK];
        });
    }
}

void DelegateMGlobalListener::onNodesUpdate(mega::MegaApi *api) {
    if (listener !=nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onNodesUpdate:this->megaSDK];
        });
    }
}

void DelegateMGlobalListener::onReloadNeeded(mega::MegaApi* api) {
    if (listener !=nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onReloadNeeded:this->megaSDK];
        });
    }
}