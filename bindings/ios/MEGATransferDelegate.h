//
//  MEGATransferDelegate.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGATransfer.h"
#import "MEGAError.h"

@class MEGASdk;

@protocol MEGATransferDelegate <NSObject>

- (void)onTransferStart:(MEGASdk *)api transfer:(MEGATransfer *)transfer;
- (void)onTransferFinish:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;
- (void)onTransferUpdate:(MEGASdk *)api transfer:(MEGATransfer *)transfer;
- (void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;

@end
