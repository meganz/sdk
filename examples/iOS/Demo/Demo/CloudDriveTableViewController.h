//
//  CloudDriveTableViewController.h
//  Demo
//
//  Created by Javier Navarro on 15/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MEGASdkManager.h"
#import "SWTableViewCell.h"

@interface CloudDriveTableViewController : UITableViewController <MEGADelegate, UIActionSheetDelegate, UIAlertViewDelegate, UINavigationControllerDelegate, UIImagePickerControllerDelegate, SWTableViewCellDelegate>

@property (nonatomic, strong) MEGANode *parentNode;

@end
