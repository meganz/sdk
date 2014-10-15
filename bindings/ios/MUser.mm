//
//  MUser.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MUser.h"
#import "megaapi.h"

using namespace mega;

@interface MUser ()

@property MegaUser *megaUser;
@property BOOL cMemoryOwn;

@end

@implementation MUser

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

- (MegaUser *)getCPtr {
    return self.megaUser;
}

- (NSString *)getEmail{
    if (!self.megaUser) return nil;
    
    return self.megaUser ? [[NSString alloc] initWithUTF8String:self.megaUser->getEmail()] : nil;
}

- (MUserVisibility)getVisibility {
    return (MUserVisibility) (self.megaUser ? self.megaUser->getVisibility() : ::mega::MegaUser::VISIBILITY_UNKNOWN);
}

- (NSDate *)getTimestamp {
    return self.megaUser ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaUser->getTimestamp()] : nil;
}

@end
