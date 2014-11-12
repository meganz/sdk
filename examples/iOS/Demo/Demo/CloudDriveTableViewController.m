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

@property (nonatomic, strong) MNodeList *nodes;
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
    [[MegaSDKManager sharedMegaSDK] addDelegate:self];
    [[MegaSDKManager sharedMegaSDK] retryPendingConnections];
    [self reloadUI];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [[MegaSDKManager sharedMegaSDK] removeDelegate:self];
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
    
    MNode *node = [self.nodes getNodeAtPosition:indexPath.row];
    
    NSString *extension = [@"." stringByAppendingString:[[node getName] pathExtension]];
    NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
    NSString *fileName = [[node getBase64Handle] stringByAppendingString:extension];
    NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:@"thumbs"];
    destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
    
    if (!fileExists && [node getType] == MNodeTypeFile && [node hasThumbnail]) {
        [[MegaSDKManager sharedMegaSDK] getThumbnailWithNode:node destinationFilePath:destinationFilePath];
    }
    
    cell.nameLabel.text = [node getName];
    if ([node getType] == MNodeTypeFolder) {
        [cell.thumbnailImageView setImage:[UIImage imageNamed:@"folder"]];
    } else if (!fileExists && isImage([node getName].lowercaseString.lastPathComponent.pathExtension)) {
        [cell.thumbnailImageView setImage:[UIImage imageNamed:@"image"]];
    } else {
        [cell.thumbnailImageView setImage:[UIImage imageWithContentsOfFile:destinationFilePath]];
    }
    
    if ([node getType] == MNodeTypeFile) {
        NSString *modificationTimeString = [NSDateFormatter localizedStringFromDate:[node getModificationTime]
                                                                          dateStyle:NSDateFormatterShortStyle
                                                                          timeStyle:NSDateFormatterNoStyle];
        
        cell.modificationLabel.text = modificationTimeString;
    } else {
        NSString *creationTimeString = [NSDateFormatter localizedStringFromDate:[node getCreationTime]
                                                                              dateStyle:NSDateFormatterShortStyle
                                                                              timeStyle:NSDateFormatterNoStyle];
        
        cell.modificationLabel.text = creationTimeString;
    }
    
    cell.nodeHandle = [node getHandle];
    
    cell.leftUtilityButtons = [self leftButtons];
    cell.delegate = self;
    
    return cell;
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    return YES;
}

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    MNode *node = [self.nodes getNodeAtPosition:indexPath.row];
    switch ([node getType]) {
        case MNodeTypeFolder: {
            CloudDriveTableViewController *cdvc = [self.storyboard instantiateViewControllerWithIdentifier:@"drive"];
            [cdvc setParentNode:node];
            [self.navigationController pushViewController:cdvc animated:YES];
            break;
        }
           
//        case MNodeTypeFile: {
//            NSString *extension = [@"." stringByAppendingString:[[node getName] pathExtension]];
//            NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
//            NSString *fileName = [[node getBase64Handle] stringByAppendingString:extension];
//            NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:fileName];
//            BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
//
//            if (!fileExists) {
//                [[MegaSDKManager sharedMegaSDK] startDownloadWithNode:node localPath:destinationFilePath];
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
        MNode *node = [self.nodes getNodeAtPosition:indexPath.row];
        [[MegaSDKManager sharedMegaSDK] removeNode:node];
    }
}

- (IBAction)logout:(id)sender {
    [[MegaSDKManager sharedMegaSDK] logout];
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
    MNode *node = [self.nodes getNodeAtPosition:indexNodeSelected];
    switch (index) {
        case 0: {
            if ([node getType] == MNodeTypeFile) {
                NSString *extension = [@"." stringByAppendingString:[[node getName] pathExtension]];
                NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
                NSString *fileName = [[node getBase64Handle] stringByAppendingString:extension];
                NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:fileName];
                BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
                
                if (!fileExists) {
                    [[MegaSDKManager sharedMegaSDK] startDownloadWithNode:node localPath:destinationFilePath];
                }
            }
            break;
        }
        case 1: {
            [[MegaSDKManager sharedMegaSDK] exportNode:node];
            break;
        }
        case 2:
            renameAlert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"renameFileTitle", @"Rename file") message:NSLocalizedString(@"renameFileMessage", @"Enter new name for file") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"renameFile", @"Rename"), nil];
            [renameAlert setAlertViewStyle:UIAlertViewStylePlainTextInput];
            [renameAlert textFieldAtIndex:0].text = [[[node getName] lastPathComponent] stringByDeletingPathExtension];
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
            [[MegaSDKManager sharedMegaSDK] createFolderWithName:[[folderAlert textFieldAtIndex:0] text] parent:self.parentNode];
        }
    }
    
    // Rename file
    if (alertView.tag == 2) {
        if (buttonIndex == 1) {
            MNode *node = [self.nodes getNodeAtPosition:indexNodeSelected];
            if ([[[node getName] pathExtension] isEqualToString:@""]) {
                [[MegaSDKManager sharedMegaSDK] renameNode:node newName:[alertView textFieldAtIndex:0].text];
            } else {
                NSString *newName = [[alertView textFieldAtIndex:0].text stringByAppendingFormat:@".%@", [[node getName] pathExtension]];
                [[MegaSDKManager sharedMegaSDK] renameNode:node newName:newName];
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
        
        [[MegaSDKManager sharedMegaSDK] startUploadWithLocalPath:localFilePath parent:self.parentNode];
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
        self.nodes = [[MegaSDKManager sharedMegaSDK] getChildrenWithParent:[[MegaSDKManager sharedMegaSDK] getRootNode]];
    } else {
        [self.navigationItem setTitle:[self.parentNode getName]];
        self.nodes = [[MegaSDKManager sharedMegaSDK] getChildrenWithParent:self.parentNode];
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

#pragma mark - MRequestDelegate

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)request {
    switch ([request getType]) {
        case MRequestTypeLogout:
            [SVProgressHUD showWithStatus:@"Logout..."];
            break;
            
        case MRequestTypeExport:
            [SVProgressHUD showWithStatus:@"Generate link..."];
            break;
        
        default:
            break;
    }
}

- (void)onRequestFinish:(MegaSDK *)api request:(MRequest *)request error:(MError *)error {
    if ([error getErrorCode]) {
        return;
    }
    
    switch ([request getType]) {
        case MRequestTypeLogout: {
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
            
        case MRequestTypeGetAttrFile: {
            for (int i = 0; i < [[self.tableView visibleCells] count]; i++) {
                if ([request getNodeHandle] == [[[self.tableView visibleCells] objectAtIndex:i] nodeHandle]) {
                    NSIndexPath *indexPath = [self.tableView indexPathForCell:[[self.tableView visibleCells] objectAtIndex:i]];
                    [self.tableView beginUpdates];
                    [self.tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
                    [self.tableView endUpdates];
                }
            }
            break;
        }
        case MRequestTypeExport: {
            [SVProgressHUD dismiss];
            NSArray *itemsArray = [NSArray arrayWithObjects:[request getLink], nil];
            UIActivityViewController *activityVC = [[UIActivityViewController alloc] initWithActivityItems:itemsArray applicationActivities:nil];
            activityVC.excludedActivityTypes = @[UIActivityTypePrint, UIActivityTypeCopyToPasteboard, UIActivityTypeAssignToContact, UIActivityTypeSaveToCameraRoll];
            [self presentViewController:activityVC animated:YES completion:nil ];
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

#pragma mark - MGlobalListener

- (void)onUsersUpdate:(MegaSDK *)api {

}

- (void)onReloadNeeded:(MegaSDK *)api {

}

- (void)onNodesUpdate:(MegaSDK *)api {
    [self reloadUI];
}


#pragma mark - MTransferDelegate

- (void)onTransferStart:(MegaSDK *)api transfer:(MTransfer *)transfer {
}

- (void)onTransferUpdate:(MegaSDK *)api transfer:(MTransfer *)transfer {
}

- (void)onTransferFinish:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error {
    if ([transfer getType] == MTransferTypeUpload) {
        NSError *e = nil;
        NSString *localFilePath = [NSTemporaryDirectory() stringByAppendingPathComponent:[transfer getFileName]];
        BOOL success = [[NSFileManager defaultManager] removeItemAtPath:localFilePath error:&e];
        if (!success || e) {
            NSLog(@"remove file error %@", e);
        }
    }

}

-(void)onTransferTemporaryError:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error {
}


@end
