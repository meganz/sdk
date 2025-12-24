/**
 * @file MEGAUploadOptions.mm
 * @brief Optional parameters to customize an upload.
 *
 * (c) 2025 by Mega Limited, Auckland, New Zealand
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

#import "MEGAUploadOptions.h"
#import "MEGASdk.h"

@implementation MEGAUploadOptions

- (instancetype)init {
    return [self initWithFileName:nil
                            mtime:INVALID_CUSTOM_MOD_TIME
                          appData:nil
                 isSourceTemporary:NO
                       startFirst:NO
                     pitagTrigger:'.'];
}

- (instancetype)initWithFileName:(nullable NSString *)fileName {
    return [self initWithFileName:fileName
                            mtime:INVALID_CUSTOM_MOD_TIME
                          appData:nil
                 isSourceTemporary:NO
                       startFirst:NO
                     pitagTrigger:'.'];
}

- (instancetype)initWithFileName:(nullable NSString *)fileName mtime:(int64_t)mtime {
    return [self initWithFileName:fileName
                            mtime:mtime
                          appData:nil
                 isSourceTemporary:NO
                       startFirst:NO
                     pitagTrigger:'.'];
}

- (instancetype)initWithFileName:(nullable NSString *)fileName
                           mtime:(int64_t)mtime
                         appData:(nullable NSString *)appData {
    return [self initWithFileName:fileName
                            mtime:mtime
                          appData:appData
                 isSourceTemporary:NO
                       startFirst:NO
                     pitagTrigger:'.'];
}

- (instancetype)initWithFileName:(nullable NSString *)fileName
                           mtime:(int64_t)mtime
                         appData:(nullable NSString *)appData
                isSourceTemporary:(BOOL)isSourceTemporary
                      startFirst:(BOOL)startFirst
                    pitagTrigger:(char)pitagTrigger {
    self = [super init];
    if (self) {
        _fileName = [fileName copy];
        _mtime = mtime;
        _appData = [appData copy];
        _isSourceTemporary = isSourceTemporary;
        _startFirst = startFirst;
        _pitagTrigger = pitagTrigger;
    }
    return self;
}

@end
