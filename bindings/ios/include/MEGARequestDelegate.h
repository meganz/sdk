/**
 * @file MEGARequestDelegate.h
 * @brief Delegate to get request events
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
#import "MEGARequest.h"
#import "MEGAError.h"

NS_ASSUME_NONNULL_BEGIN

@class MEGASdk;

/**
 * @brief Protocol to receive information about requests.
 *
 * All requests allows to pass a pointer to an implementation of this protocol in the last parameter.
 * You can also get information about all requests using [MEGASdk addMEGARequestDelegate:]
 *
 * MEGADelegate objects can also receive information about requests
 *
 * This protocol uses MEGARequest objects to provide information of requests. Take into account that not all
 * fields of MEGARequest objects are valid for all requests. See the documentation about each request to know
 * which fields contain useful information for each one.
 *
 */
@protocol MEGARequestDelegate <NSObject>

@optional

/**
 * @brief This function is called when a request is about to start being processed.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 */
- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when a request has finished.
 *
 * There won't be more callbacks about this request.
 * The last parameter provides the result of the request. If the request finished without problems,
 * the error code will be MEGAErrorTypeApiOk.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 * @param error Error information.
 */
- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progres of a request.
 *
 * Currently, this callback is only used for fetchNodes (MEGARequestTypeFetchNodes) requests.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 * @see [MEGARequest totalBytes] [MEGARequest transferredBytes].
 */
- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when there is a temporary error processing a request.
 *
 * The request continues after this callback, so expect more 
 * [MEGARequestDelegate onRequestTemporaryError:request:error:] or
 * a [MEGARequestDelegate onRequestFinish:request:error:] callback.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 * @param error Error information.
 */
- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

@end

NS_ASSUME_NONNULL_END
