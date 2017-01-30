/**
 * @file DelegateMEGALoggerListener.mm
 * @brief Listener to reveice and send logs to the app
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
#include "DelegateMEGALoggerListener.h"
#include <sstream>

using namespace mega;

DelegateMEGALoggerListener::DelegateMEGALoggerListener(id<MEGALoggerDelegate>listener) {
    this->listener = listener;
    MegaApi::setLoggerObject(this);
}

void DelegateMEGALoggerListener::log(const char *time, int logLevel, const char *source, const char *message) {
    if (listener != nil && [listener respondsToSelector:@selector(logWithTime:logLevel:source:message:)]) {
        
        [listener logWithTime:(time ? [NSString stringWithUTF8String:time] : nil) logLevel:(NSInteger)logLevel source:(source ? [NSString stringWithUTF8String:source] : nil) message:(message ? [NSString stringWithUTF8String:message] : nil)];
    }
    else {
        NSString *output = [[NSString alloc] init];
        
        switch (logLevel) {
            case MEGALogLevelDebug:
                output = [output stringByAppendingString:@" (debug) "];
                break;
            case MEGALogLevelError:
                output = [output stringByAppendingString:@" (error) "];
                break;
            case MEGALogLevelFatal:
                output = [output stringByAppendingString:@" (fatal) "];
                break;
            case MEGALogLevelInfo:
                output = [output stringByAppendingString:@" (info) "];
                break;
            case MEGALogLevelMax:
                output = [output stringByAppendingString:@" (verb) "];
                break;
            case MEGALogLevelWarning:
                output = [output stringByAppendingString:@" (warn) "];
                break;
                
            default:
                break;
        }
        
        output = [output stringByAppendingString:[NSString stringWithUTF8String:message]];
        output = [output stringByAppendingString:@" ("];
        output = [output stringByAppendingString:[[NSString stringWithUTF8String:source] lastPathComponent]];
        output = [output stringByAppendingString:@")"];
        NSLog(@"%@", output);
    }
}
