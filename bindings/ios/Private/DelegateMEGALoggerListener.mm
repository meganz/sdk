#include "DelegateMEGALoggerListener.h"
#include <sstream>

using namespace mega;

DelegateMEGALogerListener::DelegateMEGALogerListener(id<MEGALoggerDelegate>listener) {
    this->listener = listener;
    MegaApi::setLoggerObject(this);
}

void DelegateMEGALogerListener::log(const char *time, int logLevel, const char *source, const char *message) {
    if (listener != nil && [listener respondsToSelector:@selector(logWithTime:logLevel:source:message:)]) {
        
        [listener logWithTime:(time ? [NSString stringWithUTF8String:time] : nil) logLevel:(NSInteger)logLevel source:(source ? [NSString stringWithUTF8String:source] : nil) message:(message ? [NSString stringWithUTF8String:message] : nil)];
    }
    else {
        std::ostringstream oss;
        oss << time;
        switch (logLevel)
        {
            case MEGALogLevelDebug:
                oss << " (debug): ";
                break;
            case MEGALogLevelError:
                oss << " (error): ";
                break;
            case MEGALogLevelFatal:
                oss << " (fatal): ";
                break;
            case MEGALogLevelInfo:
                oss << " (info):  ";
                break;
            case MEGALogLevelMax:
                oss << " (verb):  ";
                break;
            case MEGALogLevelWarning:
                oss << " (warn):  ";
                break;
        }
        
        oss << message;
        std::string filename = source;
        if (filename.size())
        {
            int index = filename.find_last_of('\\');
            if (index != std::string::npos && filename.size() > (index + 1))
            {
                filename = filename.substr(index + 1);
            }
            oss << " (" << filename << ")";
        }
        
        oss << std::endl;
        NSString *output = [NSString stringWithUTF8String:oss.str().c_str()];
        NSLog(@"%@", output);
    }
}