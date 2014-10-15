//
//  MNodeList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MNode.h"

@interface MNodeList : NSObject

- (MNode *)getNodeAtPosition:(NSInteger)position;
- (NSNumber *)size;

@end
