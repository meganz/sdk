/**
 * @file MEGAUserAlert.mm
 * @brief Represents a user alert in MEGA.
 *
 * (c) 2018-Present by Mega Limited, Auckland, New Zealand
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
#import "MEGANotification.h"
#import "megaapi.h"

using namespace mega;

@interface MEGANotification ()

@property MegaNotification *megaNotification;
@property BOOL cMemoryOwn;

@end

@implementation MEGANotification

- (instancetype)initWithMegaNotification:(MegaNotification *)megaNotification cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaNotification = megaNotification;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaNotification;
    }
}

- (MegaNotification *)getCPtr {
    return self.megaNotification;
}

- (NSUInteger)identifier {
    return self.megaNotification ? self.megaNotification->getID() : 0;
}

- (nullable NSString *)title {
    return self.megaNotification ? [[NSString alloc] initWithUTF8String:self.megaNotification->getTitle()] : nil;
}

- (nullable NSString *)description {
    return self.megaNotification ? [[NSString alloc] initWithUTF8String:self.megaNotification->getDescription()] : nil;
}

- (nullable NSString *)imageName {
    return self.megaNotification ? [[NSString alloc] initWithUTF8String:self.megaNotification->getImageName()] : nil;
}

- (nullable NSString *)imagePath {
    return self.megaNotification ? [[NSString alloc] initWithUTF8String:self.megaNotification->getImagePath()] : nil;
}

- (nullable NSString *)iconName {
    return self.megaNotification ? [[NSString alloc] initWithUTF8String:self.megaNotification->getIconName()] : nil;
}

- (nullable NSDate *)startDate {
    return self.megaNotification ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaNotification->getStart()] : nil;
}

- (nullable NSDate *)endDate {
    return self.megaNotification ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaNotification->getEnd()] : nil;
}

- (BOOL)shouldShowBanner {
    return self.megaNotification ? self.megaNotification->showBanner() : NO;
}

- (nullable NSDictionary<NSString *,NSString *> *)firstCallToAction {
    if (!self.megaNotification) { return nil; }
    
    const MegaStringMap *map = self.megaNotification->getCallToAction1();
    
    return [self callToAction:map];
}

- (nullable NSDictionary<NSString *,NSString *> *)secondCallToAction {
    if (!self.megaNotification) { return nil; }
    
    const MegaStringMap *map = self.megaNotification->getCallToAction2();
    
    return [self callToAction:map];
}

#pragma mark - Private

- (NSDictionary<NSString *,NSString *> *)callToAction:(const MegaStringMap *)map {
    NSMutableDictionary *dict = [NSMutableDictionary dictionaryWithCapacity:map->size()];
    MegaStringList *keyList = map->getKeys();
    
    for (int i = 0; i < keyList->size(); i++) {
        const char *key = keyList->get(i);
        dict[@(key)] = @(map->get(key));
    }
    
    delete keyList;
    return [dict copy];
}

@end

