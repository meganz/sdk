/**
 * @file MEGATimeZoneDetails.mm
 * @brief Time zone details
 *
 * (c) 2018 - by Mega Limited, Auckland, New Zealand
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

#import "MEGATimeZoneDetails.h"
#import "megaapi.h"

using namespace mega;

@interface MEGATimeZoneDetails ()

@property MegaTimeZoneDetails *megaTimeZoneDetails;
@property BOOL cMemoryOwn;

@end

@implementation MEGATimeZoneDetails

- (NSString *)description {
    return [NSString stringWithFormat:@"<%@: Time zone: %@>", self.class, [self timeZoneAtIndex:self.defaultTimeZone]];
}

- (instancetype)initWithMegaTimeZoneDetails:(MegaTimeZoneDetails *)megaTimeZoneDetails cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaTimeZoneDetails = megaTimeZoneDetails;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaTimeZoneDetails;
    }
}

- (NSInteger)numTimeZones {
    return self.megaTimeZoneDetails ? self.megaTimeZoneDetails->getNumTimeZones() : 0;
}

- (NSInteger)defaultTimeZone {
    return self.megaTimeZoneDetails ? self.megaTimeZoneDetails->getDefault() : 0;
}

- (nullable NSString *)timeZoneAtIndex:(NSInteger)index {
    return self.megaTimeZoneDetails ? [[NSString alloc] initWithUTF8String:self.megaTimeZoneDetails->getTimeZone((int)index)] : nil;
}

- (NSInteger)timeOffsetAtIndex:(NSInteger)index {
    return self.megaTimeZoneDetails ? self.megaTimeZoneDetails->getTimeOffset((int)index) : 0;
}

@end
