//
//  MError+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MError.h"
#import "megaapi.h"

using namespace mega;

@interface MError (init)

- (instancetype)initWithMegaError:(MegaError *)megaError cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaError *)getCPtr;

@end
