//
//  NodeInfoDetailsViewController.m
//  Demo
//
//  Created by Javier Navarro on 18/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "DetailsNodeInfoViewController.h"
#import "SVProgressHUD.h"

@interface DetailsNodeInfoViewController () {
    UIAlertView *renameAV;
}

@property (weak, nonatomic) IBOutlet UIImageView *thumbnailmageView;
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
    
    [self reloadUI];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
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
    
    NSString *cacheDirectory = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
    NSString *extension = [@"." stringByAppendingString:[[self.node name] pathExtension]];
    NSString *fileName = [[self.node base64Handle] stringByAppendingString:extension];
    NSString *destinationFilePath = [cacheDirectory stringByAppendingPathComponent:@"thumbs"];
    destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
    
    [self.thumbnailmageView setImage:[UIImage imageWithContentsOfFile:destinationFilePath]];
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
    
    self.sizeLabel.text = [NSByteCountFormatter stringFromByteCount:[[self.node size] longLongValue] countStyle:NSByteCountFormatterCountStyleMemory];
    
    self.title = [self.node name];
    
    
    NSString *documentsDirectory = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
    destinationFilePath = [documentsDirectory stringByAppendingPathComponent:fileName];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
    if (fileExists) {
        [self.downloadProgressView setHidden:YES];
        [self.saveLabel setHidden:NO];
        [self.downloadButton setImage:[UIImage imageNamed:@"savedFile"] forState:UIControlStateNormal];
        self.saveLabel.text = @"Saved for offline";
        
    }
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (IBAction)tapDownload:(UIButton *)sender {
    if ([self.node type] == MEGANodeTypeFile) {
        NSString *extension = [@"." stringByAppendingString:[[self.node name] pathExtension]];
        NSString *documentsDirectory = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
        NSString *fileName = [[self.node base64Handle] stringByAppendingString:extension];
        NSString *destinationFilePath = [documentsDirectory stringByAppendingPathComponent:fileName];
        BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
        
        if (!fileExists) {
            [[MEGASdkManager sharedMEGASdk] startDownloadWithNode:self.node localPath:destinationFilePath];
        }
    }
}

- (IBAction)tapGenerateLink:(UIButton *)sender {
    [[MEGASdkManager sharedMEGASdk] exportNode:self.node];
}

- (IBAction)tapRename:(UIButton *)sender {
    renameAV = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"renameFileTitle", @"Rename file") message:NSLocalizedString(@"renameFileMessage", @"Enter new name for file") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"renameFile", @"Rename"), nil];
    [renameAV setAlertViewStyle:UIAlertViewStylePlainTextInput];
    [renameAV textFieldAtIndex:0].text = [[[self.node name] lastPathComponent] stringByDeletingPathExtension];
    [renameAV show];
}

- (IBAction)tapDelete:(UIButton *)sender {
    [[MEGASdkManager sharedMEGASdk] removeNode:self.node];
    [self.navigationController popViewControllerAnimated:YES];
}

/*
#pragma mark - Navigation

// In a storyboard-based application, you will often want to do a little preparation before navigation
- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    // Get the new view controller using [segue destinationViewController].
    // Pass the selected object to the new view controller.
}
*/

#pragma mark - Alert delegate

- (void)alertView:(UIAlertView *)alertView didDismissWithButtonIndex:(NSInteger)buttonIndex {
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

#pragma mark - MEGARequestDelegate

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request {
    switch ([request type]) {
        case MEGARequestTypeExport:
            [SVProgressHUD showWithStatus:@"Generate link..."];
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

#pragma mark - MGlobalListener

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
    self.saveLabel.text = @"Saved for offline";
}

-(void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error {
}

@end
