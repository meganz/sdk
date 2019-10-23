/**
* @file MEGAProxy.mm
* @brief Contains the information related to a proxy server.
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

#import "MEGAProxy.h"

#import "MEGAProxy+init.h"

using namespace mega;

@interface MEGAProxy ()

@property MegaProxy *proxy;
@property BOOL cMemoryOwn;

@end

@implementation MEGAProxy

- (instancetype)init {
    self = [super init];
    if (self) {
        _proxy = new MegaProxy();
        _cMemoryOwn = YES;
    }
    
    return self;
}

- (instancetype)initWithMegaProxy:(mega::MegaProxy *)megaProxy cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self) {
        _proxy = megaProxy;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (MegaProxy *)getCPtr {
    return self.proxy;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _proxy;
    }
}

- (MEGAProxyType)type {
    return (MEGAProxyType) (self.proxy ? self.proxy->getProxyType() : 0);
}

- (void)setType:(MEGAProxyType)type {
    if (self.proxy) {
        self.proxy->setProxyType((int)type);
    }
}

- (NSURL * _Nullable)url {
    return self.proxy && self.proxy->getProxyURL() ? [[NSURL alloc] initWithString:[[NSString alloc] initWithUTF8String:self.proxy->getProxyURL()]] : nil;
}

- (void)setUrl:(NSURL * _Nullable)url {
    if (self.proxy) {
        self.proxy->setProxyURL(url.path.UTF8String);
    }
}

- (NSString * _Nullable)username {
    return self.proxy && self.proxy->getUsername() ? [[NSString alloc] initWithUTF8String:self.proxy->getUsername()] : nil;
}

- (NSString * _Nullable)password {
    return self.proxy && self.proxy->getPassword() ? [[NSString alloc] initWithUTF8String:self.proxy->getPassword()] : nil;
}

- (void)setCredentialsWithUsername:(NSString * _Nullable)username andPassword:(NSString * _Nullable)password {
    if (self.proxy) {
        self.proxy->setCredentials(username.UTF8String, password.UTF8String);
    }
}

@end
