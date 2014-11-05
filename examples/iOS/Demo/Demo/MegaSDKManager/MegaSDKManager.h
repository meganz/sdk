//
//  MegaSDKManager.h
//  Demo
//
//  Created by Javier Navarro on 05/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MegaSDK.h"

@interface MegaSDKManager : NSObject

@property (nonatomic, strong) MegaSDK *megaSDK;

+ (void)setAppKey:(NSString *)appKey;
+ (void)setUserAgent:(NSString *)userAgent;
+ (MegaSDK *)sharedMegaSDK;

@end
