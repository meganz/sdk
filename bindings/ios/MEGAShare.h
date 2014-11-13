//
//  MEGAShare.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGAShareType) {
    MEGAShareTypeAccessUnkown = -1,
    MEGAShareTypeAccessRead = 0,
    MEGAShareTypeAccessReadWrite,
    MEGAShareTypeAccessFull,
    MEGAShareTypeAccessOwner
};

@interface MEGAShare : NSObject

- (NSString *)getUser;
- (uint64_t)getNodeHandle;
- (NSInteger)getAccess;
- (NSDate *)getTimestamp;

@end
