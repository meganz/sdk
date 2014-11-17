//
//  MEGARequest.m
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGARequest.h"
#import "MEGANode+init.h"
#import "MEGAPricing+init.h"

using namespace mega;

@interface MEGARequest()

@property MegaRequest *megaRequest;
@property BOOL cMemoryOwn;

@end

@implementation MEGARequest

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
    return  self.megaRequest ? [[MEGARequest alloc] initWithMegaRequest:self.megaRequest->copy() cMemoryOwn:YES] : nil;
}

- (MegaRequest *)getCPtr {
    return self.megaRequest;
}

- (MEGARequestType)type {
    return (MEGARequestType) (self.megaRequest ? self.megaRequest->getType() : -1);
}

- (NSString *)requestString {
    if(!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getRequestString()] : nil;
}

- (uint64_t)nodeHandle {
    return self.megaRequest ? self.megaRequest->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)link {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getLink() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getLink()] : nil;
}

- (uint64_t)parentHandle {
    return self.megaRequest ? self.megaRequest->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)sessionKey {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getSessionKey()] : nil;
}

- (NSString *)name {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getName()] : nil;
}

- (NSString *)email {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getEmail()] : nil;
}

- (NSString *)password {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getPassword()] : nil;
}

- (NSString *)newPassword {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getNewPassword()] : nil;
}

- (NSString *)privateKey {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest ? [[NSString alloc] initWithUTF8String:self.megaRequest->getPrivateKey()] : nil;
}

- (MEGANodeAccessLevel)accessLevel {
    return (MEGANodeAccessLevel) (self.megaRequest ? self.megaRequest->getAccess() : -1);
}

- (NSString *)file {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getFile() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getFile()] : nil;
    
}

- (MEGANode *)publicNode {
    return self.megaRequest && self.megaRequest->getPublicNode() ? [[MEGANode alloc] initWithMegaNode:self.megaRequest->getPublicMegaNode() cMemoryOwn:YES] : nil;
}

- (NSInteger)paramType {
    return  self.megaRequest ? self.megaRequest->getParamType() : 0;
}

- (BOOL)flag {
    return self.megaRequest ? self.megaRequest->getFlag() : NO;
}

- (NSNumber *)transferredBytes {
    return self.megaRequest ? [[NSNumber alloc] initWithLongLong:self.megaRequest->getTransferredBytes()] : nil;
}

- (NSNumber *)totalBytes {
    return self.megaRequest ? [[NSNumber alloc] initWithLongLong:self.megaRequest->getTotalBytes()] : nil;
}

- (MEGAAcountDetails *)megaAcountDetails {
    return nil;
}

- (MEGAPricing *)pricing {
    return self.megaRequest ? [[MEGAPricing alloc] initWithMegaPricing:self.megaRequest->getPricing() cMemoryOwn:YES] : nil;
}

@end
