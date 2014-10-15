//
//  MRequest+init.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MRequest.h"
#import "megaapi.h"

using namespace mega;

@interface MRequest (init)

- (instancetype)initWithMegaRequest:(MegaRequest *)megaNode cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaRequest *)getCPtr;

@end
