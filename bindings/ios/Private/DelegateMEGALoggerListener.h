//
//  DelegateMEGALoggerListener.h
//  mega
//
//  Created by Javier Navarro on 20/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//


#import "megaapi.h"
#import "MEGASdk.h"

class DelegateMEGALogerListener : public mega::MegaLogger {
    
public:
    DelegateMEGALogerListener(id<MEGALoggerDelegate> listener);
    void log(const char *time, int logLevel, const char *source, const char *message);
    
private:
    MEGASdk *megaSDK;
    id<MEGALoggerDelegate> listener;
};
