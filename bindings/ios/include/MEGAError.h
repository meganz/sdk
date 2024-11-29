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

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Declaration of API error codes.
 */
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
    MEGAErrorTypeApiEgoingOverquota = -24,         // Not enough quota
    MEGAErrorTypeApiEMFARequired = -26,            // Multi-factor authentication required
    MEGAErrorTypeApiEMasterOnly = -27,             ///< Access denied for sub-users (only for business accounts)
    MEGAErrorTypeApiEBusinessPastDue = -28,        ///< Business account expired
    MEGAErrorTypeApiEPaywall = -29                 ///< Over Disk Quota Paywall
};


/**
 * @brief Api error code context.
 */
typedef NS_ENUM(NSInteger, MEGAErrorContext) {
    MEGAErrorContextDefault = 0,   ///< Default error code context
    MEGAErrorContextDownload = 1,  ///< Download transfer context.
    MEGAErrorContextImport = 2,    ///< Import context.
    MEGAErrorContextUpload = 3     ///< Upload transfer context.
};

/**
 * @brief User custom error details
 */
typedef NS_ENUM(NSInteger, MEGAUserErrorCode) {
    MEGAUserErrorCodeETDUnknown = -1,   ///< Unknown state
    MEGAUserErrorCodeCopyrightSuspension = 4,  /// Account suspended by copyright
    MEGAUserErrorCodeETDSuspension = 7  ///< Account suspend by an ETD/ToS 'severe'
};

/**
 * @brief Link custom error details
 */
typedef NS_ENUM(NSInteger, MEGALinkErrorCode) {
    MEGALinkErrorCodeUnknown = -1,      ///< Unknown state
    MEGALinkErrorCodeUndeleted = 0,     ///< Link is undeleted
    MEGALinkErrorCodeUndeletedDown = 1, ///< Link is deleted or down
    MEGALinkErrorCodeDownETD = 2        ///< Link is down due to an ETD specifically
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
 * @brief YES if error has extra info
 *
 * @note Can return YES for:
 *   - MEGARequestTypeFetchnodes with error MEGAErrorTypeApiENoent
 *   - MEGARequestTypeGetPublicNode with error MEGAErrorTypeApiETooMany
 *   - MEGARequestTypeImportLink with error MEGAErrorTypeApiETooMany
 *   - [MEGATransferDelegate onTransferFinish:transfer:error:] with error MEGAErrorTypeApiETooMany
 */
@property (readonly, nonatomic, getter=hasExtraInfo) BOOL extraInfo;

/**
 * @brief The user status
 *
 * This property contains valid value when extraInfo is YES
 * Possible values:
 *  MEGAUserErrorCodeETDSuspension
 *
 * Otherwise, it value is MEGAUserErrorCodeETDUnknown
 *
 */
@property (readonly, nonatomic) MEGAUserErrorCode userStatus;

/**
 * @brief The link status
 *
 * This property contains valid value when extraInfo is YES
 * Possible values:
 *  MEGALinkErrorCodeUndeleted
 *  MEGALinkErrorCodeUndeletedDown
 *  MEGALinkErrorCodeDownETD
 *
 * Otherwise, it value is MEGALinkErrorCodeUnknown
 *
 */
@property (readonly, nonatomic) MEGALinkErrorCode linkStatus;

/**
 * @brief Provides the error description associated with an error code.
 *
 * This function returns a pointer to a statically allocated buffer.
 * You don't have to free the returned pointer.
 *
 * @param errorCode Error code for which the description will be returned.
 * @return Description associated with the error code.
 */
- (nullable NSString *)nameWithErrorCode:(NSInteger)errorCode;

/**
 * @brief Provides the error description associated with an error code
 * given a certain context.
 *
 * @param errorCode Error code for which the description will be returned
 * @param context Context to provide a more accurate description (MEGAErrorContext)
 * @return Description associated with the error code
 */
+ (nullable NSString *)errorStringWithErrorCode:(NSInteger)errorCode context:(MEGAErrorContext)context;

NS_ASSUME_NONNULL_END

@end
