#import "SettingsViewController.h"
#import "SVProgressHUD.h"
#import "LoginViewController.h"
#import "Helper.h"
#import "SSKeychain.h"

@interface SettingsViewController ()

@property (weak, nonatomic) IBOutlet UILabel *emailLabel;
@property (weak, nonatomic) IBOutlet UIImageView *avatarImageView;

@end

@implementation SettingsViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.emailLabel.text = [[MEGASdkManager sharedMEGASdk] myEmail];
    [self setUserAvatar];
}

#pragma mark - Private Methods

- (void)setUserAvatar {
    
    MEGAUser *user = [[MEGASdkManager sharedMEGASdk] contactForEmail:self.emailLabel.text];
    NSString *avatarFilePath = [Helper pathForUser:user searchPath:NSCachesDirectory directory:@"thumbs"];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:avatarFilePath];
    
    if (!fileExists) {
        [[MEGASdkManager sharedMEGASdk] getAvatarUser:user destinationFilePath:avatarFilePath delegate:self];
    } else {
        [self.avatarImageView setImage:[UIImage imageNamed:avatarFilePath]];
        
        self.avatarImageView.layer.cornerRadius = self.avatarImageView.frame.size.width/2;
        self.avatarImageView.layer.masksToBounds = YES;
    }
}

- (IBAction)logout:(id)sender {
    [[MEGASdkManager sharedMEGASdk] logoutWithDelegate:self];
}

#pragma mark - MEGARequestDelegate

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request {
    switch ([request type]) {
        case MEGARequestTypeLogout:
            [SVProgressHUD showWithStatus:NSLocalizedString(@"logout", @"Logout...")];
            break;
            
        default:
            break;
    }
}

- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
    if ([error type]) {
        return;
    }
    
    switch ([request type]) {
            
        case MEGARequestTypeLogout: {
            [SSKeychain deletePasswordForService:@"MEGA" account:@"session"];
            NSFileManager *fm = [NSFileManager defaultManager];
            NSError *error = nil;
            
            NSString *cacheDirectory = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
            for (NSString *file in [fm contentsOfDirectoryAtPath:cacheDirectory error:&error]) {
                BOOL success = [fm removeItemAtPath:[NSString stringWithFormat:@"%@/%@", cacheDirectory, file] error:&error];
                if (!success || error) {
                    NSLog(@"remove file error %@", error);
                }
            }
            NSString *documentDirectory = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
            for (NSString *file in [fm contentsOfDirectoryAtPath:documentDirectory error:&error]) {
                BOOL success = [fm removeItemAtPath:[NSString stringWithFormat:@"%@/%@", documentDirectory, file] error:&error];
                if (!success || error) {
                    NSLog(@"remove file error %@", error);
                }
            }
            
            [SVProgressHUD dismiss];
            UIStoryboard* storyboard = [UIStoryboard storyboardWithName:@"Main" bundle:nil];
            LoginViewController *loginVC = [storyboard instantiateViewControllerWithIdentifier:@"LoginViewControllerID"];
            
            [self presentViewController:loginVC animated:YES completion:nil];
            break;
        }
            
        case MEGARequestTypeGetAttrUser: {
            [self setUserAvatar];
        }
            
        default:
            break;
    }
}

- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request {
}

- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
}

@end
