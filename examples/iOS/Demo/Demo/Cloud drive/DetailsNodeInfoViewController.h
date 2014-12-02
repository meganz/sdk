#import <UIKit/UIKit.h>
#import "MEGASdkManager.h"

@interface DetailsNodeInfoViewController : UIViewController <MEGADelegate, UIAlertViewDelegate>

@property (nonatomic, strong) MEGANode *node;

@end
