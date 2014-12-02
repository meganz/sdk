#import <Foundation/Foundation.h>
#import "MEGASdk.h"

@interface MEGASdkManager : NSObject

@property (nonatomic, strong) MEGASdk *megaSDK;

+ (void)setAppKey:(NSString *)appKey;
+ (void)setUserAgent:(NSString *)userAgent;
+ (MEGASdk *)sharedMEGASdk;

@end
