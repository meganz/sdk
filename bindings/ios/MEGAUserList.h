//
//  MEGAUserList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGAUser.h"

@interface MEGAUserList : NSObject

@property (readonly) NSNumber *size;

- (MEGAUser *)userAtPosition:(NSInteger)position;

@end
