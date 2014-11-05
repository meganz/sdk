//
//  MNode+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MNode.h"
#import "megaapi.h"

@interface MNode (init)

- (instancetype)initWithMegaNode:(mega::MegaNode *)megaNode cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaNode *)getCPtr;

@end
