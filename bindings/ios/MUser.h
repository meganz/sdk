//
//  MUser.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MUserVisibility) {
    MUserVisibilityUnknown = -1,
    MUserVisibilityHidden = 0,
    MUserVisibilityVisible,
    MUserVisibilityMe
};

@interface MUser : NSObject

- (NSString *)getEmail;
- (MUserVisibility)getVisibility;
- (NSDate *)getTimestamp;

@end
