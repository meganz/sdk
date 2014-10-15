//
//  MListenerProtocol.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MTransfer.h"
#import "MRequest.h"
#import "MError.h"

@class MegaSDK;

@protocol MListenerDelegate <NSObject>

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)request;
- (void)onRequestFinish:(MegaSDK *)api request:(MRequest *)request error:(MError *)error;
- (void)onRequestUpdate:(MegaSDK *)api request:(MRequest *)request;
- (void)onRequestTemporaryError:(MegaSDK *)api request:(MRequest *)request error:(MError *)error;
- (void)onTransferStart:(MegaSDK *)api transfer:(MTransfer *)transfer;
- (void)onTransferFinish:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error;
- (void)onTransferUpdate:(MegaSDK *)api transfer:(MTransfer *)transfer;
- (void)onTransferTemporaryError:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error;
- (void)onUsersUpdate:(MegaSDK *)api;
- (void)onNodesUpdate:(MegaSDK *)api;
- (void)onReloadNeeded:(MegaSDK *)api;

@end
