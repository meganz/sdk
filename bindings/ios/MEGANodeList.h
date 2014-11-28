//
//  MEGANodeList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGANode.h"

@interface MEGANodeList : NSObject

@property (readonly) NSNumber *size;

- (MEGANode *)nodeAtPosition:(NSInteger)position;

@end
