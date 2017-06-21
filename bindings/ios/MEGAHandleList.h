#import <Foundation/Foundation.h>

@interface MEGAHandleList : NSObject

@property (readonly, nonatomic) NSUInteger size;

- (instancetype)clone;

- (void)addMegaHandle:(uint64_t)handle;
- (uint64_t)megaHandleAtIndex:(NSUInteger)index;

@end
