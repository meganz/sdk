/**
 * @file MEGAPricing.mm
 * @brief Details about pricing plans
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
#import "MEGAPricing.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAPricing ()

@property MegaPricing *pricing;
@property BOOL cMemoryOwn;

@end

@implementation MEGAPricing

- (instancetype)initWithMegaPricing:(MegaPricing *)pricing cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _pricing = pricing;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (MegaPricing *)getCPtr {
    return self.pricing;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _pricing;
    }
}

- (NSInteger)products {
    return self.pricing ? self.pricing->getNumProducts() : 0;
}

- (uint64_t)handleAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getHandle((int)index) : INVALID_HANDLE;
}

- (MEGAAccountType)proLevelAtProductIndex:(NSInteger)index {
    return (MEGAAccountType) (self.pricing ? self.pricing->getProLevel((int)index) : 0);
}

- (NSInteger)storageGBAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getGBStorage((int)index) : 0;
}

- (NSInteger)transferGBAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getGBTransfer((int)index) : 0;
}

- (NSInteger)monthsAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMonths((int)index) : 0;
}

- (NSInteger)amountAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getAmount((int)index) : 0;
}

- (NSInteger)localPriceAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getLocalPrice((int)index) : 0;
}

- (NSString *)descriptionAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getDescription((int)index)] : nil;
}

- (NSString *)iOSIDAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getIosID((int)index)] : nil;
}

- (unsigned int)trialDurationInDaysAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getTrialDurationInDays((int)index) : 0;
}

- (BOOL)hasMobileOffersAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->hasMobileOffers((int)index) : NO;
}

- (NSString *)mobileOfferIdAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getMobileOfferId((int)index).c_str()] : nil;
}

- (BOOL)hasMobileOfferUatAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->hasMobileOfferUat((int)index) : NO;
}

- (NSString *)mobileOfferLabelAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getMobileOfferLabel((int)index).c_str()] : nil;
}

- (int)mobileOfferDiscountPercentageAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMobileOfferDiscountPercentage((int)index) : 0;
}

- (int64_t)mobileOfferExpiryTimestampAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMobileOfferExpiryTimestamp((int)index) : 0;
}

- (uint32_t)mobileOfferFlagsAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMobileOfferFlags((int)index) : 0;
}

- (int64_t)mobileOfferReshowIntervalAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMobileOfferReshowInterval((int)index) : 0;
}

- (uint64_t)mobileOfferCampaignIdAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMobileOfferCampaignId((int)index) : 0;
}

- (BOOL)hasMobileOfferIosAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->hasMobileOfferIos((int)index) : NO;
}

- (NSString *)mobileOfferIosOfferIdAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getMobileOfferIosOfferId((int)index).c_str()] : nil;
}

- (NSString *)mobileOfferIosKeyIdAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getMobileOfferIosKeyId((int)index).c_str()] : nil;
}

- (NSString *)mobileOfferIosNonceAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getMobileOfferIosNonce((int)index).c_str()] : nil;
}

- (int64_t)mobileOfferIosTimestampMsAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMobileOfferIosTimestampMs((int)index) : 0;
}

- (NSString *)mobileOfferIosSignatureAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getMobileOfferIosSignature((int)index).c_str()] : nil;
}

- (BOOL)hasMobileOfferAndroidAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->hasMobileOfferAndroid((int)index) : NO;
}

- (NSString *)mobileOfferAndroidOfferIdAtProductIndex:(NSInteger)index {
    return self.pricing ? [[NSString alloc] initWithUTF8String:self.pricing->getMobileOfferAndroidOfferId((int)index).c_str()] : nil;
}

@end
