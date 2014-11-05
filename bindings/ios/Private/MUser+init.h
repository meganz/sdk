//
//  MUser+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MUser.h"
#import "megaapi.h"

@interface MUser (init)

- (instancetype)initWithMegaUser:(mega::MegaUser *)megaUser cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaUser *)getCPtr;

@end
