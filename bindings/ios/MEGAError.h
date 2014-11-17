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

/**
 * @brief Provides information about an error
 */
@interface MEGAError : NSObject

/**
 * @brief The error code associated with this MEGAError
 * @return Error code associated with this MEGAError
 */
@property (readonly, nonatomic) MEGAErrorType type;

/**
 * @brief Readable description of the error
 * @return Readable description of the error
 */
@property (readonly, nonatomic) NSString *name;

/**
 * @brief Creates a copy of this MEGAError object
 *
 * The resulting object is fully independent of the source MEGAError,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object
 *
 * @return Copy of the MEGAError object
 */
- (instancetype)clone;

/**
 * @brief Provides the error description associated with an error code
 *
 * This function returns a pointer to a statically allocated buffer.
 * You don't have to free the returned pointer
 *
 * @param errorCode Error code for which the description will be returned
 * @return Description associated with the error code
 */
- (NSString *)nameWithErrorCode:(NSInteger)errorCode;

@end
