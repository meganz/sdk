//
//  RegisterViewController.h
//  Demo
//
//  Created by Javier Navarro on 17/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MegaSDK.h"

@interface RegisterViewController : UIViewController <MRequestDelegate>

@property MegaSDK *megaSDK;

@end
