//
//  MEGAUser.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAUser.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAUser ()

@property MegaUser *megaUser;
@property BOOL cMemoryOwn;

@end

@implementation MEGAUser

- (instancetype)initWithMegaUser:(MegaUser *)megaUser cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaUser = megaUser;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaUser;
    }
}

- (instancetype)clone {
    return self.megaUser ? [[MEGAUser alloc] initWithMegaUser:self.megaUser->copy() cMemoryOwn:YES] : nil;
}

- (MegaUser *)getCPtr {
    return self.megaUser;
}

- (NSString *)email{
    if (!self.megaUser) return nil;
    
    return self.megaUser ? [[NSString alloc] initWithUTF8String:self.megaUser->getEmail()] : nil;
}

- (MEGAUserVisibility)access {
    return (MEGAUserVisibility) (self.megaUser ? self.megaUser->getVisibility() : ::mega::MegaUser::VISIBILITY_UNKNOWN);
}

- (NSDate *)timestamp {
    return self.megaUser ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaUser->getTimestamp()] : nil;
}

@end
