//
//  LoginViewController.h
//  Demo
//
//  Created by Javier Navarro on 15/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "LoginViewController.h"
#import "CloudDriveTableViewController.h"
#import "SVProgressHUD.h"

#define kSession @"kSession"
#define kEmail @"kEmail"

@interface LoginViewController ()

@property (weak, nonatomic) IBOutlet UITextField *inputEmail;
@property (weak, nonatomic) IBOutlet UITextField *inputPassword;

@end

@implementation LoginViewController

- (void)viewDidLoad {
    [super viewDidLoad];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
}

- (IBAction)tapLogin:(id)sender {
    [[MegaSDK sharedMegaSDK] loginWithEmail:[self.inputEmail text] password:[self.inputPassword text] delegate:self];
}

#pragma mark - MRequestDelegate

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)request {
    [SVProgressHUD show];
}

- (void)onRequestFinish:(MegaSDK *)api request:(MRequest *)request error:(MError *)error {
    if ([error getErrorCode]) {
        [SVProgressHUD dismiss];
        switch ([error getErrorCode]) {
            case MErrorTypeApiEArgs:{
                UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Error"
                                                                message:@"Email or password invalid."
                                                               delegate:self
                                                      cancelButtonTitle:@"OK"
                                                      otherButtonTitles:nil];
                [alert show];
                break;
            }
                
            case MErrorTypeApiENoent:{
                UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Error"
                                                                message:@"Email or password invalid."
                                                               delegate:self
                                                      cancelButtonTitle:@"OK"
                                                      otherButtonTitles:nil];
                [alert show];
                break;
            }
                
            default:
                break;
        }
        return;
    }
    
    switch ([request getType]) {
        case MRequestTypeLogin: {
            [[MegaSDK sharedMegaSDK] fetchNodesWithListener:self];
            break;
        }
            
        case MRequestTypeFetchNodes: {
            [SVProgressHUD dismiss];
            [self performSegueWithIdentifier:@"showCloudDrive" sender:self];
            break;
        }
            
        default:
            break;
    }
}

- (void)onRequestUpdate:(MegaSDK *)api request:(MRequest *)request {
    if ([request getType] == MRequestTypeFetchNodes){
        float progress = [[request getTransferredBytes] floatValue] / [[request getTotalBytes] floatValue];
        if (progress > 0 && progress <0.99) {
            [SVProgressHUD showProgress:progress status:@"Fetching nodes"];
        } else if (progress > 0.99 || progress < 0) {
            [SVProgressHUD showProgress:1 status:@"Preparing nodes"];
        }
    }
}

- (void)onRequestTemporaryError:(MegaSDK *)api request:(MRequest *)request error:(MError *)error {
}

#pragma mark - Dismiss keyboard

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    [self.view endEditing:YES];
}

@end
