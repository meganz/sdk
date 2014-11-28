//
//  MEGAGlobalDelegate.H
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

@protocol MEGAGlobalDelegate <NSObject>

- (void)onUsersUpdate:(MEGASdk *)api;
- (void)onNodesUpdate:(MEGASdk *)api;
- (void)onReloadNeeded:(MEGASdk *)api;

@end
