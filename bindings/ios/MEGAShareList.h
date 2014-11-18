//
//  MEGAShareList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGAShare.h"

@interface MEGAShareList : NSObject

@property (readonly) NSNumber *size;

- (MEGAShare *)getShareAtPosition:(NSInteger)position;

@end
