#import <UIKit/UIKit.h>
#import "MWPhotoBrowser.h"
#import "MEGASdkManager.h"

@interface CloudDriveTableViewController : UITableViewController <MWPhotoBrowserDelegate, MEGADelegate, UIActionSheetDelegate, UIAlertViewDelegate, UINavigationControllerDelegate, UIImagePickerControllerDelegate>

@property (nonatomic, strong) MEGANode *parentNode;

@end
