//
//  MEGATransferList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGATransfer.h"

@interface MEGATransferList : NSObject

@property (readonly) NSNumber *size;

- (MEGATransfer *)getTransferAtPosition:(NSInteger)position;

@end
