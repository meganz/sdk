//
//  MTransferList.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MTransferList.h"
#import "MTransfer+init.h"

@interface MTransferList ()

@property TransferList *transferList;
@property BOOL cMemoryOwn;

@end

@implementation MTransferList

- (instancetype)initWithTransferList:(TransferList *)transferList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _transferList = transferList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _transferList;
    }
}

- (TransferList *)getCPtr {
    return self.transferList;
}

- (MTransfer *)getTransferAtPosition:(NSInteger)position {
    return self.transferList ? [[MTransfer alloc] initWithMegaTransfer:self.transferList->get((int)position)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.transferList ? [[NSNumber alloc] initWithInt:self.transferList->size()] : nil;
}

@end
