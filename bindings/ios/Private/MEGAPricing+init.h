#import "MEGAPricing.h"
#import "megaapi.h"

@interface MEGAPricing (init)

- (instancetype)initWithMegaPricing:(mega::MegaPricing *)pricing cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaPricing *)getCPtr;

@end
