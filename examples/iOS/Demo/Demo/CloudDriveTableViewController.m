//
//  CloudDriveTableViewController.m
//  Demo
//
//  Created by Javier Navarro on 15/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "CloudDriveTableViewController.h"
#import "NodeTableViewCell.h"
#import "SVProgressHUD.h"
#import "LoginViewController.h"
#import <AssetsLibrary/AssetsLibrary.h>

#define imagesSet   [[NSSet alloc] initWithObjects:@"gif", @"jpg", @"tif", @"jpeg", @"bmp", @"png",@"nef", nil]
#define isImage(n)  [imagesSet containsObject:n]

@interface CloudDriveTableViewController () {
    UIAlertView *folderAlert;
    UIAlertView *renameAlert;
    NSInteger indexNodeSelected;
}

@property (nonatomic, strong) MEGANodeList *nodes;
@property (weak, nonatomic) IBOutlet UIBarButtonItem *logoutItem;
@property (weak, nonatomic) IBOutlet UIBarButtonItem *addItem;

@property (nonatomic) UIImagePickerController *imagePickerController;

@end

@implementation CloudDriveTableViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    NSArray *buttonsItems = @[self.logoutItem, self.addItem];
    self.navigationItem.rightBarButtonItems = buttonsItems;
    
    NSString *path;
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    path = [[paths objectAtIndex:0] stringByAppendingPathComponent:@"thumbs"];
    NSError *error;
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        if (![[NSFileManager defaultManager] createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:&error]) {
            NSLog(@"Create directory error: %@", error);
        }
    }
    
    paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    path = [[paths objectAtIndex:0] stringByAppendingPathComponent:@"previews"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        if (![[NSFileManager defaultManager] createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:&error]) {
            NSLog(@"Create directory error: %@", error);
        }
    }
}

- (void)viewWillAppear:(BOOL)animated {
    [[MEGASdkManager sharedMEGASdk] addMEGADelegate:self];
    [[MEGASdkManager sharedMEGASdk] retryPendingConnections];
    [self reloadUI];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [[MEGASdkManager sharedMEGASdk] removeMEGADelegate:self];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
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
    
    NSString *extension = [@"." stringByAppendingString:[[node name] pathExtension]];
    NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
    NSString *fileName = [[node base64Handle] stringByAppendingString:extension];
    NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:@"thumbs"];
    destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
    
    if (!fileExists && [node type] == MEGANodeTypeFile && [node hasThumbnail]) {
        [[MEGASdkManager sharedMEGASdk] getThumbnailWithNode:node destinationFilePath:destinationFilePath];
    }
    
    cell.nameLabel.text = [node name];
    if ([node type] == MEGANodeTypeFolder) {
        [cell.thumbnailImageView setImage:[UIImage imageNamed:@"folder"]];
    } else if (!fileExists && isImage([node name].lowercaseString.lastPathComponent.pathExtension)) {
        [cell.thumbnailImageView setImage:[UIImage imageNamed:@"image"]];
    } else {
        [cell.thumbnailImageView setImage:[UIImage imageWithContentsOfFile:destinationFilePath]];
    }
    
    if ([node type] == MEGANodeTypeFile) {
        NSString *modificationTimeString = [NSDateFormatter localizedStringFromDate:[node modificationTime]
                                                                          dateStyle:NSDateFormatterShortStyle
                                                                          timeStyle:NSDateFormatterNoStyle];
        
        cell.modificationLabel.text = modificationTimeString;
    } else {
        NSString *creationTimeString = [NSDateFormatter localizedStringFromDate:[node creationTime]
                                                                              dateStyle:NSDateFormatterShortStyle
                                                                              timeStyle:NSDateFormatterNoStyle];
        
        cell.modificationLabel.text = creationTimeString;
    }
    
    cell.nodeHandle = [node handle];
    
    cell.leftUtilityButtons = [self leftButtons];
    cell.delegate = self;
    
    return cell;
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    return YES;
}

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    MEGANode *node = [self.nodes nodeAtIndex:indexPath.row];
    switch ([node type]) {
        case MEGANodeTypeFolder: {
            CloudDriveTableViewController *cdvc = [self.storyboard instantiateViewControllerWithIdentifier:@"drive"];
            [cdvc setParentNode:node];
            [self.navigationController pushViewController:cdvc animated:YES];
            break;
        }
           
//        case MEGANodeTypeFile: {
//            NSString *extension = [@"." stringByAppendingString:[[node name] pathExtension]];
//            NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
//            NSString *fileName = [[node base64Handle] stringByAppendingString:extension];
//            NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:fileName];
//            BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
//
//            if (!fileExists) {
//                [[MEGASdkManager sharedMEGASdk] startDownloadWithNode:node localPath:destinationFilePath];
//            }
//        }
            
        default:
            break;
    }
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

- (IBAction)logout:(id)sender {
    [[MEGASdkManager sharedMEGASdk] logout];
}

- (IBAction)optionAdd:(id)sender {
    UIActionSheet *actionSheet = [[UIActionSheet alloc] initWithTitle:nil
                                                             delegate:self
                                                    cancelButtonTitle:@"Cancel"
                                               destructiveButtonTitle:nil
                                                    otherButtonTitles:@"Create folder", @"Upload photo", nil];
    [actionSheet showFromTabBar:self.tabBarController.tabBar];
}

#pragma mark - SWTableViewDelegate

- (void)swipeableTableViewCell:(SWTableViewCell *)cell didTriggerLeftUtilityButtonWithIndex:(NSInteger)index {
    NSIndexPath* pathOfTheCell = [self.tableView indexPathForCell:cell];
    indexNodeSelected = [pathOfTheCell row];
    MEGANode *node = [self.nodes nodeAtIndex:indexNodeSelected];
    switch (index) {
        case 0: {
            if ([node type] == MEGANodeTypeFile) {
                NSString *extension = [@"." stringByAppendingString:[[node name] pathExtension]];
                NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
                NSString *fileName = [[node base64Handle] stringByAppendingString:extension];
                NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:fileName];
                BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
                
                if (!fileExists) {
                    [[MEGASdkManager sharedMEGASdk] startDownloadWithNode:node localPath:destinationFilePath];
                }
            }
            break;
        }
        case 1: {
            [[MEGASdkManager sharedMEGASdk] exportNode:node];
            break;
        }
        case 2:
            renameAlert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"renameFileTitle", @"Rename file") message:NSLocalizedString(@"renameFileMessage", @"Enter new name for file") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"renameFile", @"Rename"), nil];
            [renameAlert setAlertViewStyle:UIAlertViewStylePlainTextInput];
            [renameAlert textFieldAtIndex:0].text = [[[node name] lastPathComponent] stringByDeletingPathExtension];
            renameAlert.tag = 2;
            [renameAlert show];

            NSLog(@"rename button was pressed");
            break;
        default:
            break;
    }
}

// utility button open/close event
- (void)swipeableTableViewCell:(SWTableViewCell *)cell scrollingToState:(SWCellState)state {

}

- (BOOL)swipeableTableViewCellShouldHideUtilityButtonsOnSwipe:(SWTableViewCell *)cell {
    return  YES;
}

- (BOOL)swipeableTableViewCell:(SWTableViewCell *)cell canSwipeToState:(SWCellState)state {
    return YES;
}

#pragma mark - Action sheet delegate

- (void)actionSheet:(UIActionSheet *)actionSheet clickedButtonAtIndex:(NSInteger)buttonIndex {
    if (buttonIndex == 0) {
        folderAlert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"newFolderTitle", @"Create new folder") message:NSLocalizedString(@"newFolderMessage", @"Enter name for new folder") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"createFolder", @"Create"), nil];
                              [folderAlert setAlertViewStyle:UIAlertViewStylePlainTextInput];
                              [folderAlert textFieldAtIndex:0].text = @"";
                              [folderAlert show];
        folderAlert.tag = 1;
        [folderAlert show];
    } else if (buttonIndex == 1) {
        [self showImagePickerForSourceType:UIImagePickerControllerSourceTypePhotoLibrary];
    }
}

#pragma mark - Alert delegate

- (void)alertView:(UIAlertView *)alertView didDismissWithButtonIndex:(NSInteger)buttonIndex {
    // Create folder
    if (alertView.tag == 1) {
        if (buttonIndex == 1) {
            [[MEGASdkManager sharedMEGASdk] createFolderWithName:[[folderAlert textFieldAtIndex:0] text] parent:self.parentNode];
        }
    }
    
    // Rename file
    if (alertView.tag == 2) {
        if (buttonIndex == 1) {
            MEGANode *node = [self.nodes nodeAtIndex:indexNodeSelected];
            if ([[[node name] pathExtension] isEqualToString:@""]) {
                [[MEGASdkManager sharedMEGASdk] renameNode:node newName:[alertView textFieldAtIndex:0].text];
            } else {
                NSString *newName = [[alertView textFieldAtIndex:0].text stringByAppendingFormat:@".%@", [[node name] pathExtension]];
                [[MEGASdkManager sharedMEGASdk] renameNode:node newName:newName];
            }
        }
    }
}

#pragma mark - UIImagePickerControllerDelegate

// This method is called when an image has been chosen from the library or taken from the camera.
- (void)imagePickerController:(UIImagePickerController *)picker didFinishPickingMediaWithInfo:(NSDictionary *)info {
    NSURL *assetURL = [info objectForKey:UIImagePickerControllerReferenceURL];
    
    ALAssetsLibrary *library = [[ALAssetsLibrary alloc] init];
    [library assetForURL:assetURL resultBlock:^(ALAsset *asset)  {
        NSString *name = asset.defaultRepresentation.filename;
        NSDate *creationTime = [asset valueForProperty:ALAssetPropertyDate];
        UIImageView *imageView = [[UIImageView alloc] init];
        imageView.image= [info objectForKey:@"UIImagePickerControllerOriginalImage"];
        NSData *webData = UIImagePNGRepresentation(imageView.image);
        
        
        NSString *localFilePath = [NSTemporaryDirectory() stringByAppendingPathComponent:name];
        [webData writeToFile:localFilePath atomically:YES];
        
        NSError *error = nil;
        NSDictionary *attributesDictionary = [NSDictionary dictionaryWithObject:creationTime forKey:NSFileModificationDate];
        [[NSFileManager defaultManager] setAttributes:attributesDictionary ofItemAtPath:localFilePath error:&error];
        if (error) {
            NSLog(@"Error change modification date of file %@", error);
        }
        
        [[MEGASdkManager sharedMEGASdk] startUploadWithLocalPath:localFilePath parent:self.parentNode];
    } failureBlock:nil];
    
    [self dismissViewControllerAnimated:YES completion:NULL];
    self.imagePickerController = nil;
}


- (void)imagePickerControllerDidCancel:(UIImagePickerController *)picker {
    [self dismissViewControllerAnimated:YES completion:NULL];
}

#pragma mark - Private methods

- (void)reloadUI {
    if (!self.parentNode) {
        [self.navigationItem setTitle:@"Cloud drive"];
        self.nodes = [[MEGASdkManager sharedMEGASdk] childrenWithParent:[[MEGASdkManager sharedMEGASdk] rootNode]];
    } else {
        [self.navigationItem setTitle:[self.parentNode name]];
        self.nodes = [[MEGASdkManager sharedMEGASdk] childrenWithParent:self.parentNode];
    }
    
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

- (NSArray *)leftButtons {
    NSMutableArray *leftUtilityButtons = [NSMutableArray new];
    
    [leftUtilityButtons sw_addUtilityButtonWithColor:
     [UIColor colorWithRed:236/255.0f green:240/255.0f blue:241/255.0f alpha:1.0f]
                                                icon:[UIImage imageNamed:@"saveFile"]];
    [leftUtilityButtons sw_addUtilityButtonWithColor:
     [UIColor colorWithRed:149/255.0f green:165/255.0f blue:166/255.0f alpha:1.0f]
                                                icon:[UIImage imageNamed:@"shareFile"]];
//    [leftUtilityButtons sw_addUtilityButtonWithColor:
//     [UIColor colorWithRed:189/255.0f green:195/255.0f blue:199/255.0f alpha:1.0f]
//                                                icon:[UIImage imageNamed:@"moveFile"]];
    [leftUtilityButtons sw_addUtilityButtonWithColor:
    [UIColor colorWithRed:127/255.0f green:140/255.0f blue:141/255.0f alpha:1.0f]
                                                icon:[UIImage imageNamed:@"renameFile"]];
    
    return leftUtilityButtons;
}

#pragma mark - MEGARequestDelegate

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request {
    switch ([request type]) {
        case MEGARequestTypeLogout:
            [SVProgressHUD showWithStatus:@"Logout..."];
            break;
            
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
        case MEGARequestTypeLogout: {
            NSFileManager *fm = [NSFileManager defaultManager];
            NSString *cacheDirectory = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
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
            LoginViewController *lvc = [storyboard instantiateViewControllerWithIdentifier:@"LoginViewControllerID"];
            
            [self presentViewController:lvc animated:YES completion:nil];
            break;
        }
            
        case MEGARequestTypeGetAttrFile: {
            for (int i = 0; i < [[self.tableView visibleCells] count]; i++) {
                if ([request nodeHandle] == [[[self.tableView visibleCells] objectAtIndex:i] nodeHandle]) {
                    NSIndexPath *indexPath = [self.tableView indexPathForCell:[[self.tableView visibleCells] objectAtIndex:i]];
                    [self.tableView beginUpdates];
                    [self.tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
                    [self.tableView endUpdates];
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

#pragma mark - MGlobalListener

- (void)onUsersUpdate:(MEGASdk *)api {

}

- (void)onReloadNeeded:(MEGASdk *)api {

}

- (void)onNodesUpdate:(MEGASdk *)api {
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
