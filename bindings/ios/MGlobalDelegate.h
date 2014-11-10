//
//  MGlobalListenerProtocol.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

@protocol MGlobalDelegate <NSObject>

- (void)onUsersUpdate:(MegaSDK *)api;
- (void)onNodesUpdate:(MegaSDK *)api;
- (void)onReloadNeeded:(MegaSDK *)api;

@end
