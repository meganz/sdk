//
//  CloudDriveTableViewController.h
//  Demo
//
//  Created by Javier Navarro on 15/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MegaSDK.h"

@interface CloudDriveTableViewController : UITableViewController <MRequestDelegate, MTransferDelegate, UIActionSheetDelegate>

@property (nonatomic, strong) MNode *root;

@end
