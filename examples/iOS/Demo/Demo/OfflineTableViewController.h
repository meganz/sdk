//
//  OfflineTableViewController.h
//  Demo
//
//  Created by Javier Navarro on 03/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MWPhotoBrowser.h"
#import "MEGASdkManager.h"

@interface OfflineTableViewController : UITableViewController <MWPhotoBrowserDelegate, MEGATransferDelegate>

@end
