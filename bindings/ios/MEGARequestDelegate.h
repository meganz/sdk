//
//  MEGARequestDelegate.h
//
//  Created by Javier Navarro on 09/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGARequest.h"
#import "MEGAError.h"

@class MEGASdk;

/**
 * @brief Protocol to receive information about requests
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
 * @brief This function is called when a request is about to start being processed
 *
 * The SDK retains the ownership of the request parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 */
- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when a request has finished
 *
 * There won't be more callbacks about this request.
 * The last parameter provides the result of the request. If the request finished without problems,
 * the error code will be MEGAErrorTypeApiOk
 *
 * The SDK retains the ownership of the request and error parameters.
 * Don't use them after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 * @param error Error information
 */
- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progres of a request
 *
 * Currently, this callback is only used for fetchNodes (MEGARequestTypeFetchNodes) requests
 *
 * The SDK retains the ownership of the request parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 * @see [MEGARequest totalBytes] [MEGARequest transferredBytes]
 */
- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when there is a temporary error processing a request
 *
 * The request continues after this callback, so expect more [MEGARequestListener onRequestTemporaryError] or
 * a [MEGARequestListener onRequestFinish] callback
 *
 * The SDK retains the ownership of the request and error parameters.
 * Don't use them after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 * @param error Error information
 */
- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

@end

