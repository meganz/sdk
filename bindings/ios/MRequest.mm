//
//  MRequest.m
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MRequest.h"
#import "MNode+init.h"
#import "MPricing+init.h"

using namespace mega;

@interface MRequest()

@property MegaRequest *megaRequest;
@property BOOL cMemoryOwn;

@end

@implementation MRequest

- (instancetype)initWithMegaRequest:(MegaRequest *)megaRequest cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaRequest = megaRequest;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn){
        delete _megaRequest;
    }
}

- (instancetype)clone {
    return  self.megaRequest ? [[MRequest alloc] initWithMegaRequest:self.megaRequest->copy() cMemoryOwn:YES] : nil;
}

- (MegaRequest *)getCPtr {
    return self.megaRequest;
}

- (MRequestType)getType {
    return (MRequestType) (self.megaRequest ? self.megaRequest->getType() : -1);
}

- (NSString *)getRequestString {
    if(!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getRequestString()] : nil;
}

- (uint64_t)getNodeHandle {
    return self.megaRequest ? self.megaRequest->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)getLink {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getLink() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getLink()] : nil;
}

- (uint64_t)getParentHandle {
    return self.megaRequest ? self.megaRequest->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)getSessionKey {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getSessionKey()] : nil;
}

- (NSString *)getName {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getName()] : nil;
}

- (NSString *)getEmail {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getEmail()] : nil;
}

- (NSString *)getPassword {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getPassword()] : nil;
}

- (NSString *)getNewPassword {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getNewPassword()] : nil;
}

- (NSString *)getPrivateKey {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getPrivateKey()] : nil;
}

- (MNodeAccessLevel)getAccess {
    return (MNodeAccessLevel) (self.megaRequest ? self.megaRequest->getAccess() : -1);
}

- (NSString *)getFile {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getFile() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getFile()] : nil;
    
}

- (MNode *)getPublicNode {
    return self.megaRequest && self.megaRequest->getPublicNode() ? [[MNode alloc] initWithMegaNode:self.megaRequest->getPublicNode()->copy() cMemoryOwn:YES] : nil;
}

- (NSInteger)getParamType {
    return  self.megaRequest ? self.megaRequest->getParamType() : 0;
}

- (BOOL)getFlag {
    return self.megaRequest ? self.megaRequest->getFlag() : NO;
}

- (NSNumber *)getTransferredBytes {
    return self.megaRequest ? [[NSNumber alloc] initWithLongLong:self.megaRequest->getTransferredBytes()] : nil;
}

- (NSNumber *)getTotalBytes {
    return self.megaRequest ? [[NSNumber alloc] initWithLongLong:self.megaRequest->getTotalBytes()] : nil;
}

- (MAccountDetails *)getMAccountDetails {
    return nil;
}

- (MPricing *)getPricing {
    return self.megaRequest ? [[MPricing alloc] initWithMegaPricing:self.megaRequest->getPricing() cMemoryOwn:YES] : nil;
}

@end
