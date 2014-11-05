//
//  MRequest+init.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MRequest.h"
#import "megaapi.h"

@interface MRequest (init)

- (instancetype)initWithMegaRequest:(mega::MegaRequest *)megaNode cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaRequest *)getCPtr;

@end
