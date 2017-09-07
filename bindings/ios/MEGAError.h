/**
 * @file MEGAError.h
 * @brief Error info
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
    MEGAErrorTypeApiEAppKey = -22,                 // invalid or missing application key
    MEGAErrorTypeApiESSL = -23,                    // invalid SSL key
    MEGAErrorTypeApiEgoingOverquota = -24          // Not enough quota
};

/**
 * @brief Provides information about an error.
 */
@interface MEGAError : NSObject

/**
 * @brief The error code associated with this MEGAError.
 */
@property (readonly, nonatomic) MEGAErrorType type;

/**
 * @brief Readable description of the error.
 */
@property (readonly, nonatomic) NSString *name;

/**
 * @brief Value associated with the error
 *
 * Currently, this value is only useful when it is related to an MEGAErrorTypeApiEOverQuota
 * error related to a transfer. In that case, it's the number of seconds until
 * the more bandwidth will be available for the account.
 *
 * In any other case, this value will be 0
 */
@property (readonly, nonatomic) long long value;

/**
 * @brief Creates a copy of this MEGAError object.
 *
 * The resulting object is fully independent of the source MEGAError,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGAError object.
 */
- (instancetype)clone;

/**
 * @brief Provides the error description associated with an error code.
 *
 * This function returns a pointer to a statically allocated buffer.
 * You don't have to free the returned pointer.
 *
 * @param errorCode Error code for which the description will be returned.
 * @return Description associated with the error code.
 */
- (NSString *)nameWithErrorCode:(NSInteger)errorCode;

@end
