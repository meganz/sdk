/**
 * @file MEGACurrency.mm
 * @brief Details about currencies
 *
 * (c) 2021- by Mega Limited, Auckland, New Zealand
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

#import "MEGACurrency.h"
#import "megaapi.h"

using namespace mega;

@interface MEGACurrency ()

@property MegaCurrency *currency;
@property BOOL cMemoryOwn;

@end

@implementation MEGACurrency

- (instancetype)initWithMegaCurrency:(MegaCurrency *)currency cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _currency = currency;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (MegaCurrency *)getCPtr {
    return self.currency;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _currency;
    }
}

- (NSString *)currencySymbol {
    if (self.currency) {
        const char *symbol = self.currency->getCurrencySymbol();
        if (symbol) {
            return [NSString stringWithUTF8String:symbol];
        }
    }
    return nil;
}

- (NSString *)currencyName {
    if (self.currency) {
        const char *name = self.currency->getCurrencyName();
        if (name) {
            return [NSString stringWithUTF8String:name];
        }
    }
    return nil;
}

- (NSString *)localCurrencySymbol {
    if (self.currency) {
        const char *localSymbol = self.currency->getLocalCurrencySymbol();
        if (localSymbol) {
            return [NSString stringWithUTF8String:localSymbol];
        }
    }
    return nil;
}

- (NSString *)localCurrencyName {
    if (self.currency) {
        const char *localName = self.currency->getLocalCurrencyName();
        if (localName) {
            return [NSString stringWithUTF8String:localName];
        }
    }
    return nil;
    
}

@end
