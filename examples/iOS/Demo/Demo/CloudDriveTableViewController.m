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
#import "MegaSDKManager.h"

#define imagesSet   [[NSSet alloc] initWithObjects:@"gif", @"jpg", @"tif", @"jpeg", @"bmp", @"png",@"nef", nil]
#define isImage(n)  [imagesSet containsObject:n]

@interface CloudDriveTableViewController () {
    UIAlertView *folderAlert;
}

@property (nonatomic, strong) MNodeList *nodes;
@property (nonatomic, strong) NSMutableArray *photoPreviews;
@property (weak, nonatomic) IBOutlet UIBarButtonItem *logoutItem;
@property (strong, nonatomic) IBOutlet UIBarButtonItem *addItem;

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
    
    NSString *dateString = [NSDateFormatter localizedStringFromDate:[node getCreationTime]
                                                          dateStyle:NSDateFormatterShortStyle
                                                          timeStyle:NSDateFormatterNoStyle];
    
    cell.creationLabel.text = dateString;
    cell.nodeHandle = [node getHandle];
    
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
           
        case MNodeTypeFile: {
            NSString *extension = [@"." stringByAppendingString:[[node getName] pathExtension]];
            NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
            NSString *fileName = [[node getBase64Handle] stringByAppendingString:extension];
            NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:fileName];
            BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];

            if (!fileExists) {
                [[MegaSDKManager sharedMegaSDK] startDownloadWithNode:node localPath:destinationFilePath];
            }
        }
            
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
    [[MegaSDKManager sharedMegaSDK] logoutWithDelegate:(id<MRequestDelegate>)self];
}

- (IBAction)optionAdd:(id)sender {
    UIActionSheet *actionSheet = [[UIActionSheet alloc] initWithTitle:nil
                                                             delegate:self
                                                    cancelButtonTitle:@"Cancel"
                                               destructiveButtonTitle:nil
                                                    otherButtonTitles:@"Create folder", nil];
    [actionSheet showFromTabBar:self.tabBarController.tabBar];
}

#pragma mark - Action sheet delegate

- (void)actionSheet:(UIActionSheet *)actionSheet clickedButtonAtIndex:(NSInteger)buttonIndex {
    if (buttonIndex == 0) {
        folderAlert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"newFolderTitle", @"Create new folder") message:NSLocalizedString(@"newFolderName", @"Enter name for new folder") delegate:self cancelButtonTitle:NSLocalizedString(@"cancel", @"Cancel") otherButtonTitles:NSLocalizedString(@"createFolder", @"Create"), nil];
                              [folderAlert setAlertViewStyle:UIAlertViewStylePlainTextInput];
                              [folderAlert textFieldAtIndex:0].text = @"";
                              [folderAlert show];
        [folderAlert show];
    }
}

#pragma mark - Alert delegate

- (void)alertView:(UIAlertView *)alertView didDismissWithButtonIndex:(NSInteger)buttonIndex {
    if (buttonIndex == 1) {
        [[MegaSDKManager sharedMegaSDK] createFolderWithName:[[folderAlert textFieldAtIndex:0] text] parent:self.parentNode];
    }
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

#pragma mark - MRequestDelegate

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)request {
    switch ([request getType]) {
        case MRequestTypeLogout:
            [SVProgressHUD showWithStatus:@"Logout..."];
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
}

-(void)onTransferTemporaryError:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error {
}


@end
