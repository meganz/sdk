//
//  MEGASdkManager.h
//  Demo
//
//  Created by Javier Navarro on 05/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGASdk.h"

@interface MEGASdkManager : NSObject

@property (nonatomic, strong) MEGASdk *megaSDK;

+ (void)setAppKey:(NSString *)appKey;
+ (void)setUserAgent:(NSString *)userAgent;
+ (MEGASdk *)sharedMEGASdk;

@end
