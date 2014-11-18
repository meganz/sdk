//
//  MEGADelegate.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGATransfer.h"
#import "MEGARequest.h"
#import "MEGAError.h"

@class MEGASdk;

@protocol MEGADelegate <NSObject>

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request;
- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;
- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request;
- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;
- (void)onTransferStart:(MEGASdk *)api transfer:(MEGATransfer *)transfer;
- (void)onTransferFinish:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;
- (void)onTransferUpdate:(MEGASdk *)api transfer:(MEGATransfer *)transfer;
- (void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;
- (void)onUsersUpdate:(MEGASdk *)api;
- (void)onNodesUpdate:(MEGASdk *)api;
- (void)onReloadNeeded:(MEGASdk *)api;

@end
