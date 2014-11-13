//
//  MEGATransferList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGATransferList.h"
#import "megaapi.h"

@interface MEGATransferList (init)

- (instancetype)initWithTransferList:(mega::TransferList *)transferList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::TransferList *)getCPtr;

@end
