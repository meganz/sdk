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
}

id<MEGALoggerDelegate>DelegateMEGALoggerListener::getUserListener() {
    return listener;
}

void DelegateMEGALoggerListener::log(const char *time, int logLevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
                                     , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
) {
    if (listener != nil && [listener respondsToSelector:@selector(logWithTime:logLevel:source:message:
#ifdef ENABLE_LOG_PERFORMANCE
                                                                  directMessages:numberMessages:
#endif
                                                                  )]) {
        
#ifdef ENABLE_LOG_PERFORMANCE
        NSMutableArray <NSString *> *messages = [NSMutableArray arrayWithCapacity:numberMessages];
        for (int i = 0; i < numberMessages; i++) {
            [messages addObject:[NSString stringWithUTF8String:directMessages[i]]];
        }
#endif
        [listener logWithTime:(time ? [NSString stringWithUTF8String:time] : @"")
                     logLevel:(MEGALogLevel)logLevel
                       source:(source ? [NSString stringWithUTF8String:source] : @"")
                      message:(message ? [NSString stringWithUTF8String:message] : @"")
#ifdef ENABLE_LOG_PERFORMANCE
               directMessages:messages.copy numberMessages:(NSInteger)numberMessages
#endif
        ];
    }
}
