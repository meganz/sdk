//
//  MShare.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MShare.h"
#import "megaapi.h"

using namespace mega;

@interface MShare ()

@property MegaShare *megaShare;
@property BOOL cMemoryOwn;

@end

@implementation MShare

- (instancetype)initWithMegaShare:(MegaShare *)megaShare cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaShare = megaShare;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaShare;
    }
}

- (MegaShare *)getCPtr {
    return self.megaShare;
}

- (NSString *)getUser {
    if (!self.megaShare) return nil;
    
    return self.megaShare->getUser() ? [[NSString alloc] initWithUTF8String:self.megaShare->getUser()] : nil;
}

- (uint64_t)getNodeHandle {
    return self.megaShare ? self.megaShare->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (NSInteger)getAccess {
    return self.megaShare ? self.megaShare->getAccess() : MegaShare::ACCESS_UNKNOWN;
}

- (NSDate *)getTimestamp {
    return self.megaShare ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaShare->getTimestamp()] : nil;
}

@end
