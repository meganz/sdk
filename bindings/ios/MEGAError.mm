//
//  MEGAError.m
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAError.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAError()

@property MegaError *megaError;
@property BOOL cMemoryOwn;

@end

@implementation MEGAError

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
    return self.megaError ? [[MEGAError alloc] initWithMegaError:self.megaError->copy() cMemoryOwn:YES] : nil;
}

- (MegaError *)getCPtr {
    return self.megaError;
}

- (MEGAErrorType)type {
    return (MEGAErrorType) (self.megaError ? self.megaError->getErrorCode() : 0);
}

- (NSString *)name {
    return [[NSString alloc] initWithUTF8String:self.megaError->getErrorString()];
}

- (NSString *)nameWithErrorCode:(NSInteger)errorCode {
    return MegaError::getErrorString((int)errorCode) ? [[NSString alloc] initWithUTF8String:MegaError::getErrorString((int)errorCode)] : nil;
}

@end
