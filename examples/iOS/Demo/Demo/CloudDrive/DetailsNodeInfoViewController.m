/**
 * @file DetailsNodeInfoViewController.m
 * @brief View controller that show details info about a node
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
#import "DetailsNodeInfoViewController.h"
#import "SVProgressHUD.h"
#import "Helper.h"

@interface DetailsNodeInfoViewController () {
    UIAlertView *renameAlertView;
    UIAlertView *removeAlertView;
}

@property (weak, nonatomic) IBOutlet UIImageView *thumbnailImageView;
@property (weak, nonatomic) IBOutlet UILabel *nameLabel;
@property (weak, nonatomic) IBOutlet UILabel *modificationTimeLabel;
@property (weak, nonatomic) IBOutlet UILabel *sizeLabel;
@property (weak, nonatomic) IBOutlet UIProgressView *downloadProgressView;
@property (weak, nonatomic) IBOutlet UILabel *saveLabel;
@property (weak, nonatomic) IBOutlet UIButton *downloadButton;

@end

@implementation DetailsNodeInfoViewController

- (void)viewDidLoad {
    [super viewDidLoad];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self reloadUI];
    [[MEGASdkManager sharedMEGASdk] addMEGADelegate:self];
    [[MEGASdkManager sharedMEGASdk] retryPendingConnections];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [[MEGASdkManager sharedMEGASdk] removeMEGADelegate:self];
}

- (void)reloadUI {
    if ([self.node type] == MEGANodeTypeFolder) {
        [self.downloadButton setHidden:YES];
    }
    
    NSString *thumbnailFilePath = [Helper pathForNode:self.node searchPath:NSCachesDirectory directory:@"thumbs"];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:thumbnailFilePath];
    
    if (!fileExists) {
        [self.thumbnailImageView setImage:[Helper imageForNode:self.node]];
    } else {
        [self.thumbnailImageView setImage:[UIImage imageWithContentsOfFile:thumbnailFilePath]];
    }
    
    self.nameLabel.text = [self.node name];
    
    struct tm *timeinfo;
    char buffer[80];
    time_t rawtime;
    if ([self.node isFile]) {
        rawtime = [[self.node modificationTime] timeIntervalSince1970];
    } else {
        rawtime = [[self.node creationTime] timeIntervalSince1970];
    }
    timeinfo = localtime(&rawtime);
    
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
    
    self.modificationTimeLabel.text = [NSString stringWithCString:buffer encoding:NSUTF8StringEncoding];
    
    if ([self.node isFile]) {
        self.sizeLabel.text = [NSByteCountFormatter stringFromByteCount:[[self.node size] longLongValue] countStyle:NSByteCountFormatterCountStyleMemory];
    } else {
        self.sizeLabel.text = [NSByteCountFormatter stringFromByteCount:[[[MEGASdkManager sharedMEGASdk] sizeForNode:self.node] longLongValue] countStyle:NSByteCountFormatterCountStyleMemory];
    }
    
    self.title = [self.node name];
    
    NSString *documentFilePath = [Helper pathForNode:self.node searchPath:NSDocumentDirectory];
    BOOL fileDocumentExists = [[NSFileManager defaultManager] fileExistsAtPath:documentFilePath];
    if (fileDocumentExists) {
        [self.downloadProgressView setHidden:YES];
        [self.saveLabel setHidden:NO];
        [self.downloadButton setImage:[UIImage imageNamed:@"savedFile"] forState:UIControlStateNormal];
        self.saveLabel.text = NSLocalizedString(@"savedForOffline", @"Saved for offline");
        
    }
}

- (IBAction)tapDownload:(UIButton *)sender {
    if ([self.node type] == MEGANodeTypeFile) {
        NSString *documentFilePath = [Helper pathForNode:self.node searchPath:NSDocumentDirectory];
        BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:documentFilePath];
        if (!fileExists) {
            [[MEGASdkManager sharedMEGASdk] startDownloadNode:self.node localPath:documentFilePath fileName:nil appData:nil startFirst:false cancelToken:MEGACancelToken.alloc.init collisionCheck:CollisionCheckFingerprint collisionResolution:CollisionResolutionNewWithN];
        }
    }
}

- (IBAction)tapGenerateLink:(UIButton *)sender {
    [[MEGASdkManager sharedMEGASdk] exportNode:self.node];
}

- (IBAction)tapRename:(UIButton *)sender {
    renameAlertView = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"renameNodeTitle", @"Rename") message:NSLocalizedString(@"renameNodeMessage", @"Enter the new name") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"renameNodeButton", @"Rename"), nil];
    [renameAlertView setAlertViewStyle:UIAlertViewStylePlainTextInput];
    [renameAlertView textFieldAtIndex:0].text = [[[self.node name] lastPathComponent] stringByDeletingPathExtension];
    removeAlertView.tag = 0;
    [renameAlertView show];
}

- (IBAction)tapDelete:(UIButton *)sender {
    removeAlertView = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"removeNodeTitle", @"Remove node") message:NSLocalizedString(@"removeNodeMessage", @"Are you sure?") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"ok", @"OK"), nil];
    [removeAlertView show];
    removeAlertView.tag = 1;
    [removeAlertView show];
}

#pragma mark - Alert delegate

- (void)alertView:(UIAlertView *)alertView didDismissWithButtonIndex:(NSInteger)buttonIndex {
    if (alertView.tag == 0){
        if (buttonIndex == 1) {
            if ([[[self.node name] pathExtension] isEqualToString:@""]) {
                [[MEGASdkManager sharedMEGASdk] renameNode:self.node newName:[alertView textFieldAtIndex:0].text];
            } else {
                NSString *newName = [[alertView textFieldAtIndex:0].text stringByAppendingFormat:@".%@", [[self.node name] pathExtension]];
                self.nameLabel.text = newName;
                self.title = newName;
                [[MEGASdkManager sharedMEGASdk] renameNode:self.node newName:newName];
            }
        }
    }
    if (alertView.tag == 1) {
        if (buttonIndex == 1) {
            [[MEGASdkManager sharedMEGASdk] removeNode:self.node];
            [self.navigationController popViewControllerAnimated:YES];
        }
    }
}

#pragma mark - MEGARequestDelegate

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request {
    switch ([request type]) {
        case MEGARequestTypeExport:
            [SVProgressHUD showWithStatus:NSLocalizedString(@"generateLink", @"Generate link...")];
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
            
        case MEGARequestTypeGetAttrFile: {
            if ([request nodeHandle] == [self.node handle]) {
                MEGANode *node = [[MEGASdkManager sharedMEGASdk] nodeForHandle:[request nodeHandle]];
                NSString *thumbnailFilePath = [Helper pathForNode:node searchPath:NSCachesDirectory directory:@"thumbs"];
                BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:thumbnailFilePath];
                if (fileExists) {
                    [self.thumbnailImageView setImage:[UIImage imageWithContentsOfFile:thumbnailFilePath]];
                }
            }
            
            break;
        }
        case MEGARequestTypeExport: {
            [SVProgressHUD dismiss];
            NSArray *itemsArray = [NSArray arrayWithObjects:[request link], nil];
            UIActivityViewController *activityVC = [[UIActivityViewController alloc] initWithActivityItems:itemsArray applicationActivities:nil];
            activityVC.excludedActivityTypes = @[UIActivityTypePrint, UIActivityTypeCopyToPasteboard, UIActivityTypeAssignToContact, UIActivityTypeSaveToCameraRoll];
            [self presentViewController:activityVC animated:YES completion:nil ];
            break;
        }
            
        default:
            break;
    }
}

- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request {
}

- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
}

#pragma mark - MEGAGlobalDelegate

- (void)onUsersUpdate:(MEGASdk *)api userList:(MEGAUserList *)userList{
}

- (void)onReloadNeeded:(MEGASdk *)api {
}

- (void)onNodesUpdate:(MEGASdk *)api nodeList:(MEGANodeList *)nodeList {
    self.node = [nodeList nodeAtIndex:0];
}

#pragma mark - MEGATransferDelegate

- (void)onTransferStart:(MEGASdk *)api transfer:(MEGATransfer *)transfer {
    [self.downloadProgressView setHidden:NO];
    [self.downloadProgressView setProgress:0];
}

- (void)onTransferUpdate:(MEGASdk *)api transfer:(MEGATransfer *)transfer {
    float progress = [[transfer transferredBytes] floatValue] / [[transfer totalBytes] floatValue];
    [self.downloadProgressView setProgress:progress];
}

- (void)onTransferFinish:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error {
    [self.downloadProgressView setHidden:YES];
    [self.downloadProgressView setProgress:1];
    [self.saveLabel setHidden:NO];
    [self.downloadButton setImage:[UIImage imageNamed:@"savedFile"] forState:UIControlStateNormal];
    self.saveLabel.text = NSLocalizedString(@"savedForOffline", @"Saved for offline");
}

-(void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error {
}

@end
