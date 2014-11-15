//
//  DelegateMEGAGlobalListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMEGAGlobalListener.h"

DelegateMEGAGlobalListener::DelegateMEGAGlobalListener(MEGASdk *megaSDK, id<MEGAGlobalDelegate>listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

id<MEGAGlobalDelegate> DelegateMEGAGlobalListener::getUserListener() {
    return listener;
}

void DelegateMEGAGlobalListener::onUsersUpdate(mega::MegaApi *api) {
    if (listener !=nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onUsersUpdate:this->megaSDK];
        });
    }
}

void DelegateMEGAGlobalListener::onNodesUpdate(mega::MegaApi *api) {
    if (listener !=nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onNodesUpdate:this->megaSDK];
        });
    }
}

void DelegateMEGAGlobalListener::onReloadNeeded(mega::MegaApi* api) {
    if (listener !=nil) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [listener onReloadNeeded:this->megaSDK];
        });
    }
}