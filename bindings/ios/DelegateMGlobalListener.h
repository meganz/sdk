//
//  DelegateMGlobalListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MegaSDK.h"
#import "MGlobalListenerDelegate.h"
#import "megaapi.h"

using namespace mega;

@interface DelegateMGlobalListener : NSObject

- (instancetype)initDelegateMGlobalListenerWithMegaSDK:(MegaSDK *)megaSDK delegate:(id<MGlobalListenerDelegate>)delegateObject;

- (void)onUsersUpdate:(MegaApi *)api;
- (void)onNodesUpdate:(MegaApi *)api;
- (void)onReloadNeeded:(MegaApi *)api;

@end
