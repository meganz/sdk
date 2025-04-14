/**
 * @file MEGATOTPDataValidation.mm
 * @brief Object Data for TOTP validation attributes
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

#import "MEGATOTPDataValidation.h"
#import "MEGATOTPDataValidation+init.h"

@interface MEGATOTPDataValidation()

@property MegaTotpDataValidation *megaTOTPDataValidation;
@property BOOL cMemoryOwn;

@end

@implementation MEGATOTPDataValidation

- (instancetype)initWithMegaTotpDataValidation:(MegaTotpDataValidation *)megaTotpDataValidation cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self != nil) {
        _megaTOTPDataValidation = megaTotpDataValidation;
        _cMemoryOwn = cMemoryOwn;
    }
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaTOTPDataValidation;
    }
}

- (MegaTotpDataValidation *)getCPtr {
    return self.megaTOTPDataValidation;
}

- (BOOL)sharedSecretExist {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->sharedSecretExist() : false;
}

- (BOOL)sharedSecretValid {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->sharedSecretValid() : false;
}

- (BOOL)algorithmExist {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->algorithmExist() : false;
}

- (BOOL)algorithmValid {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->algorithmValid() : false;
}

- (BOOL)expirationTimeExist {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->expirationTimeExist() : false;
}

- (BOOL)expirationTimeValid {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->expirationTimeValid() : false;
}

- (BOOL)digitsExist {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->nDigitsExist() : false;
}

- (BOOL)digitsValid {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->nDigitsValid() : false;
}

- (BOOL)isValidForCreate {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->isValidForCreate() : false;
}

- (BOOL)isValidForUpdate {
    return self.megaTOTPDataValidation ? self.megaTOTPDataValidation->isValidForUpdate() : false;
}

@end
