/**
 * @file MEGACancelToken.m
 * @brief Cancel MEGASdk methods.
 *
 * (c) 2019- by Mega Limited, Auckland, New Zealand
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

#import "MEGACancelToken.h"
#import "megaapi.h"
#import "MEGACancelToken+init.h"

@interface MEGACancelToken ()

@property (nonatomic) mega::MegaCancelToken *megaCancelToken;

@end

@implementation MEGACancelToken

- (instancetype)init {
    self = [super init];
    
    if (self) {
        _megaCancelToken = mega::MegaCancelToken::createInstance();
    }
    
    return self;
}

- (void)dealloc {
    if (_megaCancelToken) {
        delete _megaCancelToken;
    }
}

- (mega::MegaCancelToken *)getCPtr {
    return self.megaCancelToken;
}

- (BOOL)isCancelled {
    return self.megaCancelToken->isCancelled();
}

- (void)cancel {
    if (_megaCancelToken) {
        self.megaCancelToken->cancel();
    }
}

@end
