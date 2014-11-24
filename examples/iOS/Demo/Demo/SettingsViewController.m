#import "SettingsViewController.h"
#import "SVProgressHUD.h"
#import "LoginViewController.h"

@interface SettingsViewController () {
    NSString * cacheDirectory;
}

@property (weak, nonatomic) IBOutlet UILabel *emailLabel;
@property (weak, nonatomic) IBOutlet UIImageView *avatarImageView;

@end

@implementation SettingsViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.emailLabel.text = [[MEGASdkManager sharedMEGASdk] myEmail];
    [self setUserAvatar];
    
    cacheDirectory = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

#pragma mark - Private Methods

- (void)setUserAvatar {
    
    MEGAUser *user = [[MEGASdkManager sharedMEGASdk] contactForEmail:self.emailLabel.text];
    NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
    NSString *fileName = [self.emailLabel.text stringByAppendingString:@".jpg"];
    NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:@"thumbs"];
    destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
    
    if (!fileExists) {
        [[MEGASdkManager sharedMEGASdk] getAvatarUser:user destinationFilePath:destinationFilePath delegate:self];
    } else {
        [self.avatarImageView setImage:[UIImage imageNamed:destinationFilePath]];
        
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
            [SVProgressHUD showWithStatus:@"Logout..."];
            break;
            
        default:
            break;
    }
}

- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
    if ([error type]) {
    }
    
    switch ([request type]) {
            
        case MEGARequestTypeLogout: {
            NSFileManager *fm = [NSFileManager defaultManager];
            NSError *error = nil;
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
