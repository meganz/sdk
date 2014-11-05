//
//  MError+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MError.h"
#import "megaapi.h"

@interface MError (init)

- (instancetype)initWithMegaError:(mega::MegaError *)megaError cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaError *)getCPtr;

@end
