//
//  MEGAShare+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAShare.h"
#import "megaapi.h"
@interface MEGAShare (init)

- (instancetype)initWithMegaShare:(mega::MegaShare *)megaShare cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaShare *) getCPtr;

@end
