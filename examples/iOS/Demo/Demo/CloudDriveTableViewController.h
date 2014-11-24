#import <UIKit/UIKit.h>
#import "MEGASdkManager.h"

@interface CloudDriveTableViewController : UITableViewController <MEGADelegate, UIActionSheetDelegate, UIAlertViewDelegate, UINavigationControllerDelegate, UIImagePickerControllerDelegate>

@property (nonatomic, strong) MEGANode *parentNode;

@end
