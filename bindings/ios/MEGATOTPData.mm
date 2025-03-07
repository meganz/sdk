/**
 * @file MEGATOTPData.mm
 * @brief Object Data for TOTP attributes
 *
 * (c) 2025 by Mega Limited, Auckland, New Zealand
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

#import "MEGATOTPData.h"
#import "MEGATOTPData+init.h"
#import "MEGATOTPDataValidation.h"
#import "MEGATOTPDataValidation+init.h"

@interface MEGATOTPData()

@property MegaTotpData *megaTOTPData;
@property BOOL cMemoryOwn;

@end

@implementation MEGATOTPData

- (instancetype)initWithSharedKey:(NSString *)sharedKey
                   expirationTime:(NSInteger)expirationTime
                    hashAlgorithm:(MEGATOTPHashAlgorithm)hashAlgorithm
                           digits:(NSInteger)digits {
    self = [super init];
    if (self) {
        _megaTOTPData = MegaTotpData::createInstance(sharedKey.UTF8String,
                                                     (int)expirationTime,
                                                     (int)hashAlgorithm,
                                                     (int)digits);
        _cMemoryOwn = YES;
    }
    return self;
}

- (instancetype)initWithMegaTotpData:(MegaTotpData *)megaTotpData cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self) {
        _megaTOTPData = megaTotpData;
        _cMemoryOwn = cMemoryOwn;
    }
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaTOTPData;
    }
}

- (MegaTotpData *)getCPtr {
    return self.megaTOTPData;
}

- (nullable NSString *)sharedKey {
    const char *sharedSecret = self.megaTOTPData ? self.megaTOTPData->sharedSecret() : nullptr;
    return sharedSecret ? [[NSString alloc] initWithUTF8String:sharedSecret] : nil;
}

- (NSInteger)expirationTime {
    return self.megaTOTPData ? self.megaTOTPData->expirationTime() : 0;
}

- (MEGATOTPHashAlgorithm)hashAlgorithm {
    if (self.megaTOTPData == nil) {
        return MEGATOTPHashUnknown;
    }
    return (MEGATOTPHashAlgorithm)self.megaTOTPData->hashAlgorithm();
}

- (NSInteger)digits {
    return self.megaTOTPData ? self.megaTOTPData->nDigits() : 0;
}

- (nullable MEGATOTPDataValidation *)validation {
    if (self.megaTOTPData == nil) { return nil; }
    
    MegaTotpDataValidation *totpValidation = self.megaTOTPData->getValidation();
    return [[MEGATOTPDataValidation alloc] initWithMegaTotpDataValidation:totpValidation cMemoryOwn:NO];
}

- (BOOL)markedToRemove {
    return self.megaTOTPData ? self.megaTOTPData->markedToRemove() : false;
}

+ (NSInteger)totpNoChangeValue {
    return -1;
}

@end
