//
//  MTransferDelegate.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MTransfer.h"
#import "MError.h"

@class MegaSDK;

@protocol MTransferDelegate <NSObject>

- (void)onTransferStart:(MegaSDK *)api transfer:(MTransfer *)transfer;
- (void)onTransferFinish:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error;
- (void)onTransferUpdate:(MegaSDK *)api transfer:(MTransfer *)transfer;
- (void)onTransferTemporaryError:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error;

@end
