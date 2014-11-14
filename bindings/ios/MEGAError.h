//
//  MEGAError.h
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, MEGAErrorType) {
    MEGAErrorTypeApiOk = 0,
    MEGAErrorTypeApiEInternal = -1,                // internal error
    MEGAErrorTypeApiEArgs = -2,                    // bad arguments
    MEGAErrorTypeApiEAgain = -3,                   // request failed, retry with exponential backoff
    MEGAErrorTypeApiERateLimit = -4,               // too many requests, slow down
    MEGAErrorTypeApiEFailed = -5,                  // request failed permanently
    MEGAErrorTypeApiETooMany = -6,                 // too many requests for this resource
    MEGAErrorTypeApiERange = -7,                   // resource access out of rage
    MEGAErrorTypeApiEExpired = -8,                 // resource expired
    MEGAErrorTypeApiENoent = -9,                   // resource does not exist
    MEGAErrorTypeApiECircular = -10,               // circular linkage
    MEGAErrorTypeApiEAccess = -11,                 // access denied
    MEGAErrorTypeApiEExist = -12,                  // resource already exists
    MEGAErrorTypeApiEIncomplete = -13,             // request incomplete
    MEGAErrorTypeApiEKey = -14,                    // cryptographic error
    MEGAErrorTypeApiESid = -15,                    // bad session ID
    MEGAErrorTypeApiEBlocked = -16,                // resource administratively blocked
    MEGAErrorTypeApiEOverQuota = -17,              // quote exceeded
    MEGAErrorTypeApiETempUnavail = -18,            // resource temporarily not available
    MEGAErrorTypeApiETooManyConnections = -19,     // too many connections on this resource
    MEGAErrorTypeApiEWrite = -20,                  // file could not be written to
    MEGAErrorTypeApiERead = -21,                   // file could not be read from
    MEGAErrorTypeApiEAppKey = -22                  // invalid or missing application key
};

@interface MEGAError : NSObject

@property (readonly) MEGAErrorType type;
@property (readonly) NSString *name;

- (instancetype)clone;
- (NSString *)nameWithErrorCode:(NSInteger)errorCode;

@end
