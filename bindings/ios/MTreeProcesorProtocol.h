//
//  MTreeProcesorProtocol.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGANode.h"

@protocol MTreeProcesorProtocol <NSObject>

- (BOOL)proccessMEGANode:(MEGANode *)node;

@end
