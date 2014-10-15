//
//  MTransfer+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MTransfer.h"
#import "megaapi.h"

using namespace mega;

@interface MTransfer (init)

- (instancetype)initWithMegaTransfer:(MegaTransfer *)megaTransfer cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaTransfer *)getCPtr;

@end
