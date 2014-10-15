//
//  MError.h
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, MErrorType) {
    MErrorTypeApiOk = 0,
    MErrorTypeApiEInternal = -1,                // internal error
    MErrorTypeApiEArgs = -2,                    // bad arguments
    MErrorTypeApiEAgain = -3,                   // request failed, retry with exponential backoff
    MErrorTypeApiERateLimit = -4,               // too many requests, slow down
    MErrorTypeApiEFailed = -5,                  // request failed permanently
    MErrorTypeApiETooMany = -6,                 // too many requests for this resource
    MErrorTypeApiERange = -7,                   // resource access out of rage
    MErrorTypeApiEExpired = -8,                 // resource expired
    MErrorTypeApiENoent = -9,                   // resource does not exist
    MErrorTypeApiECircular = -10,               // circular linkage
    MErrorTypeApiEAccess = -11,                 // access denied
    MErrorTypeApiEExist = -12,                  // resource already exists
    MErrorTypeApiEIncomplete = -13,             // request incomplete
    MErrorTypeApiEKey = -14,                    // cryptographic error
    MErrorTypeApiESid = -15,                    // bad session ID
    MErrorTypeApiEBlocked = -16,                // resource administratively blocked
    MErrorTypeApiEOverQuota = -17,              // quote exceeded
    MErrorTypeApiETempUnavail = -18,            // resource temporarily not available
    MErrorTypeApiETooManyConnections = -19,     // too many connections on this resource
    MErrorTypeApiEWrite = -20,                  // file could not be written to
    MErrorTypeApiERead = -21,                   // file could not be read from
    MErrorTypeApiEAppKey = -22                  // invalid or missing application key
};

@interface MError : NSObject

- (instancetype)clone;
- (MErrorType)getErrorCode;
- (NSString *)getErrorString;
- (NSString *)getErrorStringWithErrorCode: (NSInteger) errorCode;

@end
