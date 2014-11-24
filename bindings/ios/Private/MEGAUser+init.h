//
//  MEGAUser+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAUser.h"
#import "megaapi.h"

@interface MEGAUser (init)

- (instancetype)initWithMegaUser:(mega::MegaUser *)megaUser cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaUser *)getCPtr;

@end
