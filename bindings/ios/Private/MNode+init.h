//
//  MNode+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MNode.h"
#import "megaapi.h"

using namespace mega;

@interface MNode (init)

- (instancetype)initWithMegaNode:(MegaNode *)megaNode cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaNode *)getCPtr;

@end
