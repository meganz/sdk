//
//  MEGATransfer+init.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGATransfer.h"
#import "megaapi.h"

@interface MEGATransfer (init)

- (instancetype)initWithMegaTransfer:(mega::MegaTransfer *)megaTransfer cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaTransfer *)getCPtr;

@end
