#import "MEGASdkManager.h"

@implementation MEGASdkManager

static NSString *_appKey = nil;
static NSString *_userAgent = nil;
static MEGASdk *_megaSDK = nil;

+ (void)setAppKey:(NSString *)appKey {
    _appKey = appKey;
}

+ (void)setUserAgent:(NSString *)userAgent {
    _userAgent = userAgent;
}

+ (MEGASdk *)sharedMEGASdk {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSAssert(_appKey != nil, @"setAppKey: should be called first");
        NSAssert(_userAgent != nil, @"setUserAgent: should be called first");
        NSString *basePath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
        _megaSDK = [[MEGASdk alloc] initWithAppKey:_appKey userAgent:_userAgent basePath:basePath];
    });
    return _megaSDK;
}

@end
