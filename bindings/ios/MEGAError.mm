/**
 * @file MEGAError.mm
 * @brief Error info
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
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

- (long long)value {
    return self.megaError ? self.megaError->getValue() : 0;
}

- (NSString *)nameWithErrorCode:(NSInteger)errorCode {
    return MegaError::getErrorString((int)errorCode) ? [[NSString alloc] initWithUTF8String:MegaError::getErrorString((int)errorCode)] : nil;
}

@end
