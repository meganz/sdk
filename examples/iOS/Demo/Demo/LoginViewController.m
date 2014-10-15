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

#define kUserAgent @"iOS Example/1.0"
#define kAppKey @"hNF3ELhK"

@interface LoginViewController ()

@property MegaSDK *megaSDK;

@property (weak, nonatomic) IBOutlet UITextField *inputEmail;
@property (weak, nonatomic) IBOutlet UITextField *inputPassword;

@end

@implementation LoginViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.megaSDK = [[MegaSDK alloc] initWithAppKey:kAppKey userAgent:kUserAgent basePath:nil];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
}

- (IBAction)tapLogin:(id)sender {
    [self.megaSDK loginWithEmail:self.inputEmail.text password:self.inputPassword.text delegate:self];
}

- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    if ([segue.identifier isEqualToString:@"showCloudDrive"]) {
        CloudDriveTableViewController *cloudDriveTableViewController = segue.destinationViewController;
        cloudDriveTableViewController.megaSDK = self.megaSDK;
    }
}

#pragma mark - MRequestDelegate

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)transfer {
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
            [self performSegueWithIdentifier:@"showCloudDrive" sender:self];
            break;
        }
            
        default:
            break;
    }
}

- (void)onRequestUpdate:(MegaSDK *)api request:(MRequest *)request {
}

- (void)onRequestTemporaryError:(MegaSDK *)api request:(MRequest *)request error:(MError *)error {
}

#pragma mark - Dismiss keyboard

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    [self.view endEditing:YES];
}

@end
