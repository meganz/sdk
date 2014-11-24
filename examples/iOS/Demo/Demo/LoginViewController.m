#import "LoginViewController.h"
#import "CloudDriveTableViewController.h"
#import "SVProgressHUD.h"

@interface LoginViewController ()

@property (weak, nonatomic) IBOutlet UITextField *emailTextField;
@property (weak, nonatomic) IBOutlet UITextField *passwordTextField;

@end

@implementation LoginViewController

- (void)viewDidLoad {
    [super viewDidLoad];
}

- (IBAction)tapLogin:(id)sender {
    [[MEGASdkManager sharedMEGASdk] loginWithEmail:[self.emailTextField text] password:[self.passwordTextField text] delegate:self];
}

#pragma mark - MEGARequestDelegate

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request {
    [SVProgressHUD show];
}

- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
    if ([error type]) {
        [SVProgressHUD dismiss];
        switch ([error type]) {
            case MEGAErrorTypeApiEArgs:{
                UIAlertView *alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"error", @"Error")
                                                                message:NSLocalizedString(@"invalidMailOrPassword", @"Email or password invalid.")
                                                               delegate:self
                                                      cancelButtonTitle:NSLocalizedString(@"ok", @"OK")
                                                      otherButtonTitles:nil];
                [alert show];
                break;
            }
                
            case MEGAErrorTypeApiENoent:{
                UIAlertView *alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"error", @"Error")
                                                                message:NSLocalizedString(@"invalidMailOrPassword", @"Email or password invalid.")
                                                               delegate:self
                                                      cancelButtonTitle:NSLocalizedString(@"ok", @"OK")
                                                      otherButtonTitles:nil];
                [alert show];
                break;
            }
                
            default:
                break;
        }
        return;
    }
    
    switch ([request type]) {
        case MEGARequestTypeLogin: {
            [api fetchNodesWithDelegate:self];
            break;
        }
            
        case MEGARequestTypeFetchNodes: {
            [SVProgressHUD dismiss];
            [self performSegueWithIdentifier:@"showCloudDrive" sender:self];
            break;
        }
            
        default:
            break;
    }
}

- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request {
    if ([request type] == MEGARequestTypeFetchNodes){
        float progress = [[request transferredBytes] floatValue] / [[request totalBytes] floatValue];
        if (progress > 0 && progress <0.99) {
            [SVProgressHUD showProgress:progress status:NSLocalizedString(@"fetchingNodes", @"Fetching nodes")];
        } else if (progress > 0.99 || progress < 0) {
            [SVProgressHUD showProgress:1 status:NSLocalizedString(@"preparingNodes", @"Preparing nodes")];
        }
    }
}

- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
}

#pragma mark - Dismiss keyboard

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    [self.view endEditing:YES];
}

@end
