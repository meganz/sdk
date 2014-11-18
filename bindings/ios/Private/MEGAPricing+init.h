//
//  MEGAPricing+init.h
//  mega
//
//  Created by Javier Navarro on 31/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAPricing.h"
#import "megaapi.h"

@interface MEGAPricing (init)

- (instancetype)initWithMegaPricing:(mega::MegaPricing *)pricing cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaPricing *)getCPtr;

@end
