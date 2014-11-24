#import <Foundation/Foundation.h>
#import "MEGANode.h"

@protocol MTreeProcesorProtocol <NSObject>

- (BOOL)proccessMEGANode:(MEGANode *)node;

@end
