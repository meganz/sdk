//
//  MEGAUser.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGAUserVisibility) {
    MEGAUserVisibilityUnknown = -1,
    MEGAUserVisibilityHidden = 0,
    MEGAUserVisibilityVisible,
    MEGAUserVisibilityMe
};

@interface MEGAUser : NSObject

@property (readonly) NSString *email;
@property (readonly) MEGAUserVisibility accessVisibility;
@property (readonly) NSDate *timestamp;

@end
