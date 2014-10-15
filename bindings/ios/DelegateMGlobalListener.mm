//
//  DelegateMGlobalListener.m
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DelegateMGlobalListener.h"

@interface DelegateMGlobalListener ()

@property MegaSDK *megaSDK;
@property (nonatomic, weak) id<MGlobalListenerDelegate>delegate;

@end

@implementation DelegateMGlobalListener
@synthesize delegate;

- (instancetype)initDelegateMGlobalListenerWithMegaSDK:(MegaSDK *)megaSDK delegate:(id<MGlobalListenerDelegate>)delegateObject {
    self = [super init];
    if (self) {
        _megaSDK = megaSDK;
        delegate = delegateObject;
    }
    
    return self;
}

- (void)onUsersUpdate:(MegaApi *)api {
    if (delegate != nil) {
        [delegate onUsersUpdate:self.megaSDK];
    }
}

- (void)onNodesUpdate:(MegaApi *)api {
    if (delegate != nil) {
        [delegate onNodesUpdate:self.megaSDK];
    }
}

- (void)onReloadNeeded:(MegaApi *)api {
    if (delegate != nil) {
        [delegate onReloadNeeded:self.megaSDK];
    }
}

@end
