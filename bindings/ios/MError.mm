//
//  MError.m
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MError.h"
#import "megaapi.h"

using namespace mega;

@interface MError()

@property MegaError *megaError;
@property BOOL cMemoryOwn;

@end

@implementation MError

- (instancetype)initWithMegaError:(MegaError *)megaError cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaError = megaError;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaError;
    }
}

- (instancetype)clone {
    return self.megaError ? [[MError alloc] initWithMegaError:self.megaError->copy() cMemoryOwn:YES] : nil;
}

- (MegaError *)getCPtr {
    return self.megaError;
}

- (MErrorType)getErrorCode {
    return (MErrorType) (self.megaError ? self.megaError->getErrorCode() : 0);
}

- (NSString *)getErrorString {
    return [[NSString alloc] initWithUTF8String:self.megaError->getErrorString()];
}

- (NSString *)getErrorStringWithErrorCode:(NSInteger)errorCode {
    return MegaError::getErrorString((int)errorCode) ? [[NSString alloc] initWithUTF8String:MegaError::getErrorString((int)errorCode)] : nil;
}

@end
