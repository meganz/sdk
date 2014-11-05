//
//  MegaSDKManager.m
//  Demo
//
//  Created by Javier Navarro on 05/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MegaSDKManager.h"

@implementation MegaSDKManager

static NSString *_appKey = nil;
static NSString *_userAgent = nil;
static MegaSDK *_megaSDK = nil;

+ (void)setAppKey:(NSString *)appKey {
    _appKey = appKey;
}

+ (void)setUserAgent:(NSString *)userAgent {
    _userAgent = userAgent;
}

+ (MegaSDK *)sharedMegaSDK {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSAssert(_appKey != nil, @"setAppKey: should be called first");
        NSAssert(_userAgent != nil, @"setUserAgent: should be called first");
        _megaSDK = [[MegaSDK alloc] initWithAppKey:_appKey userAgent:_userAgent];
    });
    return _megaSDK;
}

@end
