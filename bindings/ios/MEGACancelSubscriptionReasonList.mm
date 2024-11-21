/**
 * @file MEGACancelSubscriptionReasonList.mm
 * @brief Represents a reason chosen from a multiple-choice by a user when canceling a subscription
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

#import "MEGACancelSubscriptionReasonList.h"
#import "MEGACancelSubscriptionReasonList+init.h"
#import "MEGACancelSubscriptionReason+init.h"
#import "megaapi.h"

using namespace mega;

@interface MEGACancelSubscriptionReasonList ()

@property MegaCancelSubscriptionReasonList *megaCancelSubscriptionReasonList;
@property BOOL cMemoryOwn;

@end

@implementation MEGACancelSubscriptionReasonList

- (instancetype)initWithMegaCancelSubscriptionReasonList:(MegaCancelSubscriptionReasonList *)megaCancelSubscriptionReasonList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaCancelSubscriptionReasonList = megaCancelSubscriptionReasonList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaCancelSubscriptionReasonList;
    }
}

+ (MEGACancelSubscriptionReasonList *)create {
    MegaCancelSubscriptionReasonList *list = MegaCancelSubscriptionReasonList::create();
    return [[MEGACancelSubscriptionReasonList alloc] initWithMegaCancelSubscriptionReasonList:list->copy() cMemoryOwn:YES];
}

- (MegaCancelSubscriptionReasonList *)getCPtr {
    return self.megaCancelSubscriptionReasonList;
}

- (NSInteger)size {
    return self.megaCancelSubscriptionReasonList ? self.megaCancelSubscriptionReasonList->size() : 0;
}

- (void)addReason:(MEGACancelSubscriptionReason *)reason {
    if (!self.megaCancelSubscriptionReasonList) return;
    self.megaCancelSubscriptionReasonList->add([reason getCPtr]);
}

- (nullable MEGACancelSubscriptionReason *)reasonAtIndex:(NSInteger)index {
    return self.megaCancelSubscriptionReasonList ? [[MEGACancelSubscriptionReason alloc] initWithMegaCancelSubscriptionReason:self.megaCancelSubscriptionReasonList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

@end
