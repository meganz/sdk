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

#define imagesSet   [[NSSet alloc] initWithObjects:@"gif", @"jpg", @"tif", @"jpeg", @"bmp", @"png",@"nef", nil]
#define isImage(n)  [imagesSet containsObject:n]

@interface CloudDriveTableViewController ()

@property (nonatomic, strong) MNodeList *nodes;
@property (nonatomic, strong) NSMutableArray *photoPreviews;

@end

@implementation CloudDriveTableViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    if ([self.root getType] == MNodeTypeRoot) {
        [self.navigationItem setTitle:@"Cloud drive"];
        self.nodes = [[MegaSDK sharedMegaSDK] getChildrenWithParent:[[MegaSDK sharedMegaSDK] getRootNode]];
    } else {
        [self.navigationItem setTitle:[self.root getName]];
        self.nodes = [[MegaSDK sharedMegaSDK] getChildrenWithParent:self.root];
    }
    
    //Create two directories inside DocumentDirectory: thumbnails and previews
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
    
    NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
    NSString *fileName = [[node getBase64Handle] stringByAppendingString:@".jpg"];
    NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:@"thumbs"];
    destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
    
    if (!fileExists && [node getType] == MNodeTypeFile && [node hasThumbnail]) {
        NSLog(@"REQUEST: getThumbnailWithNode index path row: %ld", (long)indexPath.row);
        [[MegaSDK sharedMegaSDK] getThumbnailWithNode:node destinationFilePath:destinationFilePath delegate:self];
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

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    MNode *node = [self.nodes getNodeAtPosition:indexPath.row];
    switch ([node getType]) {
        case MNodeTypeFolder: {
            CloudDriveTableViewController *cv = [self.storyboard instantiateViewControllerWithIdentifier:@"drive"];
            [cv setRoot:node];
            [self.navigationController pushViewController:cv animated:YES];
            break;
        }
        case MNodeTypeFile: {
//            NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
//            NSString *fileName = [[node getBase64Handle] stringByAppendingString:@".jpg"];
//            NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:@"previews"];
//            destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
//            BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
//            
//            if (!fileExists) {
//                [[MegaSDK sharedMegaSDK] getPreviewWithNode:node destinationFilePath:destinationFilePath delegate:self];
//            }
        }
            
        default:
            break;
    }
}

- (IBAction)logout:(id)sender {
    [[MegaSDK sharedMegaSDK] logoutWithDelegate:self];
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
        NSLog(@"Error %@", [error getErrorString]);
        return;
    }
    
    switch ([request getType]) {
        case MRequestTypeLogout: {
            NSFileManager *fm = [NSFileManager defaultManager];
            NSString *directory = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
            NSError *error = nil;
            for (NSString *file in [fm contentsOfDirectoryAtPath:directory error:&error]) {
                BOOL success = [fm removeItemAtPath:[NSString stringWithFormat:@"%@/%@", directory, file] error:&error];
                if (!success || error) {
                    NSLog(@"remove file error %@", error);
                }
            }
            [SVProgressHUD dismiss];
            [self.navigationController popToRootViewControllerAnimated:NO];
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

@end
