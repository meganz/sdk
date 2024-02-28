/**
 * @file MEGANotificationList.h
 * @brief List of MEGANotification objects
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
#import "megaapi.h"

#import "MEGANotificationList.h"
#import "MEGANotification+init.h"

using namespace mega;

@interface MEGANotificationList ()

@property MegaNotificationList *megaNotificationList;
@property BOOL cMemoryOwn;

@end

@implementation MEGANotificationList

- (instancetype)initWithMegaNotificationList:(MegaNotificationList *)megaNotificationList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaNotificationList = megaNotificationList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaNotificationList;
    }
}

- (nullable MEGANotification *)notificationAtIndex:(NSInteger)index {
    return self.megaNotificationList ? [[MEGANotification alloc] initWithMegaNotification:self.megaNotificationList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSInteger)size {
    return self.megaNotificationList ? self.megaNotificationList->size() : 0;
}

@end
