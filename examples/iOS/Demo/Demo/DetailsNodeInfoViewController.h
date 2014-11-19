//
//  NodeInfoDetailsViewController.h
//  Demo
//
//  Created by Javier Navarro on 18/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MEGASdkManager.h"

@interface DetailsNodeInfoViewController : UIViewController <MEGADelegate, UIAlertViewDelegate>

@property (nonatomic, strong) MEGANode *node;

@end
