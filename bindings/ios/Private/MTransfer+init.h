//
//  MTransfer+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MTransfer.h"
#import "megaapi.h"

@interface MTransfer (init)

- (instancetype)initWithMegaTransfer:(mega::MegaTransfer *)megaTransfer cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaTransfer *)getCPtr;

@end
