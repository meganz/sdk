/**
 * @file MEGAContactRequest.mm
 * @brief Represents a contact request with an user in MEGA
 *
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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

#import "MEGAContactRequest.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAContactRequest ()

@property MegaContactRequest *megaContactRequest;
@property BOOL cMemoryOwn;

@end

@implementation MEGAContactRequest

- (instancetype)initWithMegaContactRequest:(MegaContactRequest *)megaContactRequest cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaContactRequest = megaContactRequest;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (MegaContactRequest *)getCPtr {
    return self.megaContactRequest;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaContactRequest;
    }
}

- (uint64_t)handle {
    return self.megaContactRequest ? self.megaContactRequest->getHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)sourceEmail{
    if (!self.megaContactRequest) return nil;
    
    return self.megaContactRequest->getSourceEmail() ? [[NSString alloc] initWithUTF8String:self.megaContactRequest->getSourceEmail()] : nil;
}

- (NSString *)sourceMessage {
    if (!self.megaContactRequest) return nil;
    
    return self.megaContactRequest->getSourceMessage() ? [[NSString alloc] initWithUTF8String:self.megaContactRequest->getSourceMessage()] : nil;
}

- (NSString *)targetEmail {
    if (!self.megaContactRequest) return nil;
    
    return self.megaContactRequest->getTargetEmail() ? [[NSString alloc] initWithUTF8String:self.megaContactRequest->getTargetEmail()] : nil;
}

- (NSDate *)creationTime {
    return self.megaContactRequest ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaContactRequest->getCreationTime()] : nil;
}

- (NSDate *)modificationTime {
    return self.megaContactRequest ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaContactRequest->getModificationTime()] : nil;
}

- (MEGAContactRequestStatus)status {
    return self.megaContactRequest ? (MEGAContactRequestStatus)self.megaContactRequest->getStatus() : MEGAContactRequestStatusUnresolved;
}

- (BOOL)isOutgoing {
    return self.megaContactRequest ? self.megaContactRequest->isOutgoing() : NO;
}

@end
