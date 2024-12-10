/**
 * @file MEGACancelSubscriptionReason.mm
 * @brief Represents a reason chosen by a user when canceling a subscription
 *
 * (c) 2024- by Mega Limited, Auckland, New Zealand
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

#import "MEGACancelSubscriptionReason.h"
#import "MEGACancelSubscriptionReason+init.h"
#import "megaapi.h"

using namespace mega;

@interface MEGACancelSubscriptionReason ()

@property MegaCancelSubscriptionReason *megaCancelSubscriptionReason;
@property BOOL cMemoryOwn;

@end

@implementation MEGACancelSubscriptionReason

- (instancetype)initWithMegaCancelSubscriptionReason:(MegaCancelSubscriptionReason *)megaCancelSubscriptionReason cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaCancelSubscriptionReason = megaCancelSubscriptionReason;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaCancelSubscriptionReason;
    }
}

- (MegaCancelSubscriptionReason *)getCPtr {
    return self.megaCancelSubscriptionReason;
}

+ (MEGACancelSubscriptionReason *)create:(NSString *)text position:(NSString *)position {
    MegaCancelSubscriptionReason *reason = MegaCancelSubscriptionReason::create(text.UTF8String, position.UTF8String);
    return [[MEGACancelSubscriptionReason alloc] initWithMegaCancelSubscriptionReason:reason->copy() cMemoryOwn:YES];
}

- (nullable NSString *)text {
    if (!self.megaCancelSubscriptionReason) return nil;
    
    return self.megaCancelSubscriptionReason->text() ? [[NSString alloc] initWithUTF8String:self.megaCancelSubscriptionReason->text()] : nil;
}

- (nullable NSString *)position {
    if (!self.megaCancelSubscriptionReason) return nil;
    
    return self.megaCancelSubscriptionReason->position() ? [[NSString alloc] initWithUTF8String:self.megaCancelSubscriptionReason->position()] : nil;
}

@end
