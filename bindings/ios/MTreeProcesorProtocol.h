//
//  MTreeProcesorProtocol.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MNode.h"

@protocol MTreeProcesorProtocol <NSObject>

- (BOOL)proccessMNode:(MNode *)node;

@end
