//
//  MEGAGlobalDelegate.H
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

/**
 * @brief Protocol to get information about global events
 *
 * You can implement this interface and start receiving events calling [MEGASdk addMEGAGlobalDelegate]
 *
 * MEGADelegate objects can also receive global events
 */
@protocol MEGAGlobalDelegate <NSObject>

@optional

/**
 * @brief This function is called when there are new or updated contacts in the account
 * @param api MEGASdk object connected to the account
 * @param users List that contains the new or updated contacts
 */
- (void)onUsersUpdate:(MEGASdk *)api userList:(MEGAUserList *)userList;

/**
 * @brief This function is called when there are new or updated nodes in the account
 * @param api MEGASdk object connected to the account
 * @param nodes List that contains the new or updated nodes
 */
- (void)onNodesUpdate:(MEGASdk *)api nodeList:(MEGANodeList *)nodeList;

/**
 * @brief This function is called when an inconsistency is detected in the local cache
 *
 * You should call [MEGASdk fetchNodes] when this callback is received
 *
 * @param api MEGASdk object connected to the account
 */
- (void)onReloadNeeded:(MEGASdk *)api;

@end
