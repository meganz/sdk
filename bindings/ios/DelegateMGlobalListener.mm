//
//  DelegateMGlobalListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMGlobalListener.h"

DelegateMGlobalListener::DelegateMGlobalListener(MegaSDK *megaSDK, void *listener) {
    this->megaSDK = megaSDK;
    this->listener = listener;
}

void *DelegateMGlobalListener::getUserListener() {
    return listener;
}

void DelegateMGlobalListener::onUsersUpdate(MegaApi *api) {
    if (listener !=nil) {
        id<MGlobalListenerDelegate> delegate = (__bridge id<MGlobalListenerDelegate>)listener;
        [delegate onUsersUpdate:this->megaSDK];
    }
}

void DelegateMGlobalListener::onNodesUpdate(MegaApi *api) {
    if (listener !=nil) {
        id<MGlobalListenerDelegate> delegate = (__bridge id<MGlobalListenerDelegate>)listener;
        [delegate onNodesUpdate:this->megaSDK];
    }
}

void DelegateMGlobalListener::onReloadNeeded(MegaApi* api) {
    if (listener !=nil) {
        id<MGlobalListenerDelegate> delegate = (__bridge id<MGlobalListenerDelegate>)listener;
        [delegate onReloadNeeded:this->megaSDK];
    }
}