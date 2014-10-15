//
//  MRequestDelegate.h
//
//  Created by Javier Navarro on 09/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MRequest.h"
#import "MError.h"

@class MegaSDK;

@protocol MRequestDelegate <NSObject>

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)transfer;
- (void)onRequestFinish:(MegaSDK *)api request:(MRequest *)request error:(MError *)error;
- (void)onRequestUpdate:(MegaSDK *)api request:(MRequest *)request;
- (void)onRequestTemporaryError:(MegaSDK *)api request:(MRequest *)request error:(MError *)error;

@end

