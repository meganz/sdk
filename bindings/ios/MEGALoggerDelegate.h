//
//  MEGALoggerDelegate.h
//  mega
//
//  Created by Javier Navarro on 20/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

@protocol MEGALoggerDelegate <NSObject>

@optional

/**
 * @brief This function will be called with all logs with level <= your selected
 * level of logging (by default it is MEGALogLevelInfo)
 *
 * @param time Readable string representing the current time.
 *
 * The SDK retains the ownership of this string, it won't be valid after this funtion returns.
 *
 * @param loglevel Log level of this message
 *
 * Valid values are:
 * - MEGALogLevelFatal = 0
 * - MEGALogLevelError = 1
 * - MEGALogLevelWarning = 2
 * - MEGALogLevelInfo = 3
 * - MEGALogLevelDebug = 4
 * - MEGALogLevelMax = 5
 *
 * @param source Location where this log was generated
 *
 * For logs generated inside the SDK, this will contain <source file>:<line of code>
 * The SDK retains the ownership of this string, it won't be valid after this funtion returns.
 *
 * @param message Log message
 *
 * The SDK retains the ownership of this string, it won't be valid after this funtion returns.
 *
 */
- (void)logWithTime:(NSString*)time logLevel:(NSInteger)logLevel source:(NSString *)source message:(NSString *)message;

@end
