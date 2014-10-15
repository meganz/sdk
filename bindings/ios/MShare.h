//
//  MShare.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MShareType) {
    MShareTypeAccessUnkown = -1,
    MShareTypeAccessRead = 0,
    MShareTypeAccessReadWrite,
    MShareTypeAccessFull,
    MShareTypeAccessOwner
};

@interface MShare : NSObject

- (NSString *)getUser;
- (uint64_t)getNodeHandle;
- (NSInteger)getAccess;
- (NSDate *)getTimestamp;

@end
