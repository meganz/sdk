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
