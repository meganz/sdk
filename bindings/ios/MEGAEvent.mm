/**
 * @file MEGAEvent.mm
 * @brief Provides information about an event
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

#import "MEGAEvent.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAEvent ()

@property MegaEvent *megaEvent;
@property BOOL cMemoryOwn;

@end

@implementation MEGAEvent

- (instancetype)initWithMegaEvent:(MegaEvent *)megaEvent cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaEvent = megaEvent;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaEvent;
    }
}

- (instancetype)clone {
    return self.megaEvent ? [[MEGAEvent alloc] initWithMegaEvent:self.megaEvent->copy() cMemoryOwn:YES] : nil;
}

- (MegaEvent *)getCPtr {
    return self.megaEvent;
}

- (Event)type {
    return (Event) (self.megaEvent ? self.megaEvent->getType() : 0);
}

- (NSString *)text {
    return self.megaEvent ? [[NSString alloc] initWithUTF8String:self.megaEvent->getText()] : nil;
}


@end
