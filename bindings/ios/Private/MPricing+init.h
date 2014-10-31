//
//  MPricing+init.h
//  mega
//
//  Created by Javier Navarro on 31/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MPricing.h"
#import "megaapi.h"

using namespace mega;

@interface MPricing (init)

- (instancetype)initWithMegaPricing:(MegaPricing *)pricing cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaPricing *)getCPtr;

@end
