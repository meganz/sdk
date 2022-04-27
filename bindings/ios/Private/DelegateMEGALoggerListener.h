/**
 * @file DelegateMEGALoggerListener.h
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
#import "megaapi.h"
#import "MEGASdk.h"

class DelegateMEGALoggerListener : public mega::MegaLogger {

public:
    DelegateMEGALoggerListener(id<MEGALoggerDelegate> listener);
    id<MEGALoggerDelegate>getUserListener();
    
    void log(const char *time, int logLevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
    , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
    );
    
private:
    MEGASdk *megaSDK;
    id<MEGALoggerDelegate> listener;
};
