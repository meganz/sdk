//
//  MEGARequest+init.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGARequest.h"
#import "megaapi.h"

@interface MEGARequest (init)

- (instancetype)initWithMegaRequest:(mega::MegaRequest *)megaNode cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaRequest *)getCPtr;

@end
