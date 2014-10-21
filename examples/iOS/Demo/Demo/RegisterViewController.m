//
//  RegisterViewController.m
//  Demo
//
//  Created by Javier Navarro on 17/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "RegisterViewController.h"
#import "SVProgressHUD.h"

@interface RegisterViewController ()

@property (weak, nonatomic) IBOutlet UITextField *inputName;
@property (weak, nonatomic) IBOutlet UITextField *inputEmail;
@property (weak, nonatomic) IBOutlet UITextField *inputPassword;

@end

@implementation RegisterViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view.
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (IBAction)tapRegister:(id)sender {
    [self.megaSDK createAccountWithEmail:[self.inputEmail text] password:[self.inputPassword text] name:[self.inputName text] delegate:self];
}

#pragma mark - MRequestDelegate

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)request {
    [SVProgressHUD show];
}

- (void)onRequestFinish:(MegaSDK *)api request:(MRequest *)request error:(MError *)error {
    [SVProgressHUD dismiss];
    if ([error getErrorCode]) {
        UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Error"
                                                        message:@"Error create accout."
                                                       delegate:self
                                              cancelButtonTitle:@"OK"
                                              otherButtonTitles:nil];
        [alert show];
        return;
    }
    
    switch ([request getType]) {
        case MRequestTypeCreateAccount: {
            UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Account create"
                                                            message:@"Account create successfully."
                                                           delegate:self
                                                  cancelButtonTitle:@"OK"
                                                  otherButtonTitles:nil];
            [alert show];
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


@end
