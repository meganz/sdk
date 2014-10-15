//
//  MTransferList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MTransferList.h"
#import "megaapi.h"

using namespace mega;

@interface MTransferList (init)

- (instancetype)initWithTransferList:(TransferList *)transferList cMemoryOwn:(BOOL)cMemoryOwn;
- (TransferList *)getCPtr;

@end
