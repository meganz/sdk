//
//  LoginViewController.h
//  Demo
//
//  Created by Javier Navarro on 15/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "LoginViewController.h"
#import "CloudDriveTableViewController.h"
#import "RegisterViewController.h"
#import "SVProgressHUD.h"

#define kUserAgent @"iOS Example/1.0"
#define kAppKey @"hNF3ELhK"
#define kSession @"kSession"
#define kEmail @"kEmail"

@interface LoginViewController ()

@property MegaSDK *megaSDK;

@property (weak, nonatomic) IBOutlet UITextField *inputEmail;
@property (weak, nonatomic) IBOutlet UITextField *inputPassword;

@end

@implementation LoginViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.megaSDK = [[MegaSDK alloc] initWithAppKey:kAppKey userAgent:kUserAgent basePath:nil];

    //----- LIST ALL FILES -----
    NSLog(@"LISTING ALL FILES FOUND");
    
    int Count;
    NSString *path;
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    path = [[paths objectAtIndex:0] stringByAppendingPathComponent:@"previews"];
    NSArray *directoryContent = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:path error:NULL];
    for (Count = 0; Count < (int)[directoryContent count]; Count++) {
        NSLog(@"File %d: %@", (Count + 1), [directoryContent objectAtIndex:Count]);
    }
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
}

- (IBAction)tapLogin:(id)sender {
    [self.megaSDK loginWithEmail:[self.inputEmail text] password:[self.inputPassword text] delegate:self];
}

- (IBAction)tapRegister:(id)sender {
    [self performSegueWithIdentifier:@"pushRegister" sender:self];
}

- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    if ([segue.identifier isEqualToString:@"showCloudDrive"]) {
        CloudDriveTableViewController *cloudDriveVC = segue.destinationViewController;
        cloudDriveVC.megaSDK = self.megaSDK;
        [cloudDriveVC setRoot:[self.megaSDK getRootNode]];
    }
    
    if ([segue.identifier isEqualToString:@"pushRegister"]) {
        RegisterViewController *registerViewController = segue.destinationViewController;
        registerViewController.megaSDK = self.megaSDK;
    }
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
            [self.megaSDK fetchNodesWithListener:self];
            break;
        }
            
        case MRequestTypeFastLogin: {
            [self performSegueWithIdentifier:@"showCloudDrive" sender:self];
            break;
        }
            
        case MRequestTypeFetchNodes: {
            NSLog(@"fetchnodes");
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
