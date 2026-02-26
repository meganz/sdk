/**
 * @file CloudDriveTableViewController.m
 * @brief Cloud drive table view controller of the app.
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
#import "CloudDriveTableViewController.h"
#import "NodeTableViewCell.h"
#import "SVProgressHUD.h"
#import "LoginViewController.h"
#import <AssetsLibrary/AssetsLibrary.h>
#import "DetailsNodeInfoViewController.h"
#import "Helper.h"

@interface CloudDriveTableViewController () {
    UIAlertView *folderAlertView;
    NSInteger indexNodeSelected;
}

@property (nonatomic, strong) NSMutableArray *cloudImages;
@property (nonatomic, strong) MEGANodeList *nodes;
@property (weak, nonatomic) IBOutlet UIBarButtonItem *addItem;
@property (weak, nonatomic) IBOutlet UIView *headerView;
@property (weak, nonatomic) IBOutlet UILabel *filesFolderLabel;

@property (nonatomic) UIImagePickerController *imagePickerController;

@end

@implementation CloudDriveTableViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    NSArray *buttonsItems = @[self.addItem];
    self.navigationItem.rightBarButtonItems = buttonsItems;
    
    NSString *thumbsDirectory = [[NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0] stringByAppendingPathComponent:@"thumbs"];
    NSError *error;
    if (![[NSFileManager defaultManager] fileExistsAtPath:thumbsDirectory]) {
        if (![[NSFileManager defaultManager] createDirectoryAtPath:thumbsDirectory withIntermediateDirectories:NO attributes:nil error:&error]) {
            NSLog(@"Create directory error: %@", error);
        }
    }
    
    NSString *previewsDirectory = [[NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0] stringByAppendingPathComponent:@"previews"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:previewsDirectory]) {
        if (![[NSFileManager defaultManager] createDirectoryAtPath:previewsDirectory withIntermediateDirectories:NO attributes:nil error:&error]) {
            NSLog(@"Create directory error: %@", error);
        }
    }
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    
    [[MEGASdkManager sharedMEGASdk] addMEGADelegate:self];
    [[MEGASdkManager sharedMEGASdk] retryPendingConnections];
    [self reloadUI];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    
    [[MEGASdkManager sharedMEGASdk] removeMEGADelegate:self];
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return [[self.nodes size] integerValue];
}


- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    NodeTableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"nodeCell" forIndexPath:indexPath];
    
    MEGANode *node = [self.nodes nodeAtIndex:indexPath.row];
    
    NSString *thumbnailFilePath = [Helper pathForNode:node searchPath:NSCachesDirectory directory:@"thumbs"];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:thumbnailFilePath];
    
    if (!fileExists && [node hasThumbnail]) {
        [[MEGASdkManager sharedMEGASdk] getThumbnailNode:node destinationFilePath:thumbnailFilePath];
    }
    
    if (!fileExists) {
        [cell.thumbnailImageView setImage:[Helper imageForNode:node]];
    } else {
        [cell.thumbnailImageView setImage:[UIImage imageWithContentsOfFile:thumbnailFilePath]];
    }
    
    cell.nameLabel.text = [node name];
    
    if ([node type] == MEGANodeTypeFile) {
        struct tm *timeinfo;
        char buffer[80];
        
        time_t rawtime = [[node modificationTime] timeIntervalSince1970];
        timeinfo = localtime(&rawtime);
        
        strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
        
        cell.modificationLabel.text = [NSString stringWithCString:buffer encoding:NSUTF8StringEncoding];
    } else {
        struct tm *timeinfo;
        char buffer[80];
        
        time_t rawtime = [[node creationTime] timeIntervalSince1970];
        timeinfo = localtime(&rawtime);
        
        strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
        
        cell.modificationLabel.text = [NSString stringWithCString:buffer encoding:NSUTF8StringEncoding];
    }
    
    cell.nodeHandle = [node handle];
    
    return cell;
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    return YES;
}

- (UIView *)tableView:(UITableView *)tableView viewForHeaderInSection:(NSInteger)section {
    return self.headerView;
}

- (CGFloat)tableView:(UITableView *)tableView heightForHeaderInSection:(NSInteger)section {
    return 20.0;
}

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    MEGANode *node = [self.nodes nodeAtIndex:indexPath.row];
    switch ([node type]) {
        case MEGANodeTypeFolder: {
            CloudDriveTableViewController *cdvc = [self.storyboard instantiateViewControllerWithIdentifier:@"CloudDriveID"];
            [cdvc setParentNode:node];
            [self.navigationController pushViewController:cdvc animated:YES];
            break;
        }
        case MEGANodeTypeFile: {
            break;
        }
        default:
            break;
    }
}

- (void)tableView:(UITableView *)tableView accessoryButtonTappedForRowWithIndexPath:(NSIndexPath *)indexPath {
    MEGANode *node = [self.nodes nodeAtIndex:indexPath.row];
    
    DetailsNodeInfoViewController *nodeInfoDetailsVC = [self.storyboard instantiateViewControllerWithIdentifier:@"nodeInfoDetails"];
    [nodeInfoDetailsVC setNode:node];
    [self.navigationController pushViewController:nodeInfoDetailsVC animated:YES];
}


- (UITableViewCellEditingStyle)tableView:(UITableView *)tableView editingStyleForRowAtIndexPath:(NSIndexPath *)indexPath {
    return (UITableViewCellEditingStyleDelete);
}

- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath {
    if (editingStyle==UITableViewCellEditingStyleDelete) {
        MEGANode *node = [self.nodes nodeAtIndex:indexPath.row];
        [[MEGASdkManager sharedMEGASdk] removeNode:node];
    }
}

- (void)tableView:(UITableView *)tableView didEndDisplayingCell:(UITableViewCell *)cell forRowAtIndexPath:(NSIndexPath *)indexPath {
    
}

- (IBAction)optionAdd:(id)sender {
    UIActionSheet *actionSheet = [[UIActionSheet alloc] initWithTitle:nil
                                                             delegate:self
                                                    cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel")
                                               destructiveButtonTitle:nil
                                                    otherButtonTitles:NSLocalizedString(@"createFolder", @"Create folder"), NSLocalizedString(@"uploadPhoto", @"Upload photo"), nil];
    [actionSheet showFromTabBar:self.tabBarController.tabBar];
}

#pragma mark - Action sheet delegate

- (void)actionSheet:(UIActionSheet *)actionSheet clickedButtonAtIndex:(NSInteger)buttonIndex {
    if (buttonIndex == 0) {
        folderAlertView = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"newFolderTitle", @"Create new folder") message:NSLocalizedString(@"newFolderMessage", @"Name for the new folder") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"createFolderButton", @"Create"), nil];
        [folderAlertView setAlertViewStyle:UIAlertViewStylePlainTextInput];
        [folderAlertView textFieldAtIndex:0].text = @"";
        folderAlertView.tag = 1;
        [folderAlertView show];
    } else if (buttonIndex == 1) {
        [self showImagePickerForSourceType:UIImagePickerControllerSourceTypePhotoLibrary];
    }
}

#pragma mark - Alert delegate

- (void)alertView:(UIAlertView *)alertView didDismissWithButtonIndex:(NSInteger)buttonIndex {
    if (alertView.tag == 1) {
        if (buttonIndex == 1) {
            [[MEGASdkManager sharedMEGASdk] createFolderWithName:[[folderAlertView textFieldAtIndex:0] text] parent:self.parentNode];
        }
    }
}

#pragma mark - UIImagePickerControllerDelegate

- (void)imagePickerController:(UIImagePickerController *)picker didFinishPickingMediaWithInfo:(NSDictionary *)info {
    NSURL *assetURL = [info objectForKey:UIImagePickerControllerReferenceURL];
    
    ALAssetsLibrary *library = [[ALAssetsLibrary alloc] init];
    [library assetForURL:assetURL resultBlock:^(ALAsset *asset)  {
        NSString *name = asset.defaultRepresentation.filename;
        NSDate *modificationTime = [asset valueForProperty:ALAssetPropertyDate];
        UIImageView *imageView = [[UIImageView alloc] init];
        imageView.image= [info objectForKey:@"UIImagePickerControllerOriginalImage"];
        NSData *webData = UIImageJPEGRepresentation(imageView.image, 0.9);
        
        
        NSString *localFilePath = [NSTemporaryDirectory() stringByAppendingPathComponent:name];
        [webData writeToFile:localFilePath atomically:YES];
        
        NSError *error = nil;
        NSDictionary *attributesDictionary = [NSDictionary dictionaryWithObject:modificationTime forKey:NSFileModificationDate];
        [[NSFileManager defaultManager] setAttributes:attributesDictionary ofItemAtPath:localFilePath error:&error];
        if (error) {
            NSLog(@"Error change modification date of file %@", error);
        }
        
        [[MEGASdkManager sharedMEGASdk] startUploadWithLocalPath:localFilePath parent:self.parentNode fileName:nil appData:nil isSourceTemporary:false startFirst:false cancelToken:MEGACancelToken.alloc.init];
    } failureBlock:nil];
    
    [self dismissViewControllerAnimated:YES completion:NULL];
    self.imagePickerController = nil;
}


- (void)imagePickerControllerDidCancel:(UIImagePickerController *)picker {
    [self dismissViewControllerAnimated:YES completion:NULL];
}

#pragma mark - Private methods

- (void)reloadUI {
    NSString *filesAndFolders;
    
    if (!self.parentNode) {
        [self.navigationItem setTitle:NSLocalizedString(@"cloudDrive", @"Cloud drive")];
        self.nodes = [[MEGASdkManager sharedMEGASdk] childrenForParent:[[MEGASdkManager sharedMEGASdk] rootNode]];
        
        NSInteger files = [[MEGASdkManager sharedMEGASdk] numberChildFilesForParent:[[MEGASdkManager sharedMEGASdk] rootNode]];
        NSInteger folders = [[MEGASdkManager sharedMEGASdk] numberChildFoldersForParent:[[MEGASdkManager sharedMEGASdk] rootNode]];
        
        if (files == 0 || files > 1) {
            if (folders == 0 || folders > 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"foldersFiles", @"Folders, files"), (int)folders, (int)files];
            } else if (folders == 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"folderFiles", @"Folder, files"), (int)folders, (int)files];
            }
        } else if (files == 1) {
            if (folders == 0 || folders > 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"foldersFile", @"Folders, file"), (int)folders, (int)files];
            } else if (folders == 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"folderFile", @"Folders, file"), (int)folders, (int)files];
            }
        }
        
    } else {
        [self.navigationItem setTitle:[self.parentNode name]];
        self.nodes = [[MEGASdkManager sharedMEGASdk] childrenForParent:self.parentNode];
        
        NSInteger files = [[MEGASdkManager sharedMEGASdk] numberChildFilesForParent:self.parentNode];
        NSInteger folders = [[MEGASdkManager sharedMEGASdk] numberChildFoldersForParent:self.parentNode];
        
        if (files == 0 || files > 1) {
            if (folders == 0 || folders > 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"foldersFiles", @"Folders, files"), (int)folders, (int)files];
            } else if (folders == 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"folderFiles", @"Folder, files"), (int)folders, (int)files];
            }
        } else if (files == 1) {
            if (folders == 0 || folders > 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"foldersFile", @"Folders, file"), (int)folders, (int)files];
            } else if (folders == 1) {
                filesAndFolders = [NSString stringWithFormat:NSLocalizedString(@"folderFile", @"Folders, file"), (int)folders, (int)files];
            }
        }
    }
    
    self.filesFolderLabel.text = filesAndFolders;
    
    [self.tableView reloadData];
}

- (void)showImagePickerForSourceType:(UIImagePickerControllerSourceType)sourceType {
    
    UIImagePickerController *imagePickerController = [[UIImagePickerController alloc] init];
    imagePickerController.modalPresentationStyle = UIModalPresentationCurrentContext;
    imagePickerController.sourceType = sourceType;
    imagePickerController.delegate = self;
    
    self.imagePickerController = imagePickerController;
    [self.tabBarController presentViewController:self.imagePickerController animated:YES completion:nil];
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
        case MEGARequestTypeFetchNodes:
            [SVProgressHUD dismiss];
            break;
            
        case MEGARequestTypeGetAttrFile: {
            for (NodeTableViewCell *ntvc in [self.tableView visibleCells]) {
                if ([request nodeHandle] == [ntvc nodeHandle]) {
                    MEGANode *node = [[MEGASdkManager sharedMEGASdk] nodeForHandle:[request nodeHandle]];
                    NSString *thumbnailFilePath = [Helper pathForNode:node searchPath:NSCachesDirectory directory:@"thumbs"];
                    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:thumbnailFilePath];
                    if (fileExists) {
                        [ntvc.thumbnailImageView setImage:[UIImage imageWithContentsOfFile:thumbnailFilePath]];
                    }
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

- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
}

#pragma mark - MEGAGlobalDelegate

- (void)onReloadNeeded:(MEGASdk *)api {
}

- (void)onNodesUpdate:(MEGASdk *)api nodeList:(MEGANodeList *)nodeList {
    [self reloadUI];
}


#pragma mark - MEGATransferDelegate

- (void)onTransferStart:(MEGASdk *)api transfer:(MEGATransfer *)transfer {
}

- (void)onTransferUpdate:(MEGASdk *)api transfer:(MEGATransfer *)transfer {
}

- (void)onTransferFinish:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error {
    if ([transfer type] == MEGATransferTypeUpload) {
        NSError *e = nil;
        NSString *localFilePath = [NSTemporaryDirectory() stringByAppendingPathComponent:[transfer fileName]];
        BOOL success = [[NSFileManager defaultManager] removeItemAtPath:localFilePath error:&e];
        if (!success || e) {
            NSLog(@"remove file error %@", e);
        }
    }
    
}

-(void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error {
}

@end
