#import <UIKit/UIKit.h>
#import <MWPhoto.h>
#import "MEGASdkManager.h"

@interface MEGAphoto : NSObject <MWPhoto, MEGARequestDelegate, MEGATransferDelegate>

@property (strong, nonatomic) NSString *caption;
@property (strong, nonatomic) NSString *imagePath;
@property (strong, nonatomic) MEGANode *node;

+ (MEGAphoto *)photoWithNode:(MEGANode *)node;

- (id)initWithNode:(MEGANode*)node;

@end
