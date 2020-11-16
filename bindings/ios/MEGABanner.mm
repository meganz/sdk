/**
 * @file MEGAUser.mm
 * @brief Represents an user in MEGA
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
#import "MEGABanner.h"
#import "megaapi.h"

using namespace mega;

@interface MEGABanner ()

@property MegaBanner *megaBanner;
@property BOOL cMemoryOwn;

@end

@implementation MEGABanner

- (instancetype)initWithMegaBanner:(MegaBanner *)megaBanner cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];

    if (self != nil) {
        _megaBanner = megaBanner;
        _cMemoryOwn = cMemoryOwn;
    }

    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaBanner;
    }
}

- (instancetype)clone {
    return self.megaBanner ? [[MEGABanner alloc] initWithMegaBanner:self.megaBanner->copy() cMemoryOwn:YES] : nil;
}

- (MegaBanner *)getCPtr {
    return self.megaBanner;
}

- (NSString *)title{
    if (!self.megaBanner) return nil;
    
    return self.megaBanner->getTitle() ? [[NSString alloc] initWithUTF8String:self.megaBanner->getTitle()] : nil;
}

- (NSString *)description {
    if (!self.megaBanner) return nil;
    
    return self.megaBanner->getDescription() ? [[NSString alloc] initWithUTF8String:self.megaBanner->getDescription()] : nil;
}

- (NSString *)imageFilename {
    if (!self.megaBanner) return nil;
    
    return self.megaBanner->getImage() ? [[NSString alloc] initWithUTF8String:self.megaBanner->getImage()] : nil;
}

- (NSString *)backgroundImageFilename {
    if (!self.megaBanner) return nil;
    
    return self.megaBanner->getBackgroundImage() ? [[NSString alloc] initWithUTF8String:self.megaBanner->getBackgroundImage()] : nil;
}


- (NSString *)imageLocationURLString {
    if (!self.megaBanner) return nil;
    
    return self.megaBanner->getImageLocation() ? [[NSString alloc] initWithUTF8String:self.megaBanner->getImageLocation()] : nil;
}

- (NSString *)URLString {
    if (!self.megaBanner) return nil;
    
    return self.megaBanner->getUrl() ? [[NSString alloc] initWithUTF8String:self.megaBanner->getUrl()] : nil;
}


@end
