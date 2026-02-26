/**
 * @file MEGALoggerDelegate.h
 * @brief Delegate to get SDK logs
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
#import "MEGALogLevel.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Protocol to receive information about SDK logs
 *
 * You can implement this class and pass an object of your subclass to [MEGASdk setLogObject]
 * to receive SDK logs. You will have to use also [MEGASdk setLogLevel] to select the level of
 * the logs that you want to receive.
 *
 */
@protocol MEGALoggerDelegate <NSObject>

@optional

/**
 * @brief This function will be called with all logs with level <= your selected
 * level of logging (by default it is MEGALogLevelInfo).
 *
 * @param time Readable string representing the current time.
 * @param logLevel Log level of this message.
 *
 * Valid values are:
 * - MEGALogLevelFatal = 0
 * - MEGALogLevelError = 1
 * - MEGALogLevelWarning = 2
 * - MEGALogLevelInfo = 3
 * - MEGALogLevelDebug = 4
 * - MEGALogLevelMax = 5
 *
 * @param source Location where this log was generated.
 *
 * For logs generated inside the SDK, this will contain the source file and the line of code.
 *
 * @param message Log message.
 *
 *
 */
- (void)logWithTime:(NSString*)time logLevel:(MEGALogLevel)logLevel source:(NSString *)source message:(NSString *)message
#ifdef ENABLE_LOG_PERFORMANCE
     directMessages:(NSArray <NSString *> *)directMessages numberMessages:(NSInteger)numberMessages
#endif
;

@end

NS_ASSUME_NONNULL_END
