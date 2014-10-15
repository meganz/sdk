//
//  MUser+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MUser.h"
#import "megaapi.h"

using namespace mega;

@interface MUser (init)

- (instancetype)initWithMegaUser:(MegaUser *)megaUser cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaUser *)getCPtr;

@end
