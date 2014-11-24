//
//  MEGARequestDelegate.h
//
//  Created by Javier Navarro on 09/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGARequest.h"
#import "MEGAError.h"

@class MEGASdk;

@protocol MEGARequestDelegate <NSObject>

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request;
- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;
- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request;
- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

@end

