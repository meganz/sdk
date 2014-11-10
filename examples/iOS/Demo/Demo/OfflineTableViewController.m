//
//  OfflineTableViewController.m
//  Demo
//
//  Created by Javier Navarro on 03/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "OfflineTableViewController.h"
#import "NodeTableViewCell.h"
#import <MediaPlayer/MediaPlayer.h>

#define kBase64Handle @"kBase64Handle"
#define kCreationDate @"kCreationDate"
#define kIndex @"kIndex"

#define imagesSet   [[NSSet alloc] initWithObjects:@"gif", @"jpg", @"tif", @"jpeg", @"bmp", @"png",@"nef", nil]
#define isImage(n)  [imagesSet containsObject:n]
#define videoSet    [[NSSet alloc] initWithObjects:@"mp4", nil]
#define isVideo(n)  [videoSet containsObject:n]

@interface OfflineTableViewController ()

@property (nonatomic, strong) NSMutableArray *offlineDocuments;
@property (nonatomic, strong) NSMutableArray *offlineImages;

@end

@implementation OfflineTableViewController

- (void)viewDidLoad {
    [super viewDidLoad];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    
    [[MegaSDKManager sharedMegaSDK] addMTransferDelegate:self];
    [self reloadUI];
    [[MegaSDKManager sharedMegaSDK] retryPendingConnections];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [[MegaSDKManager sharedMegaSDK] removeTransferDelegate:self];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return [self.offlineDocuments count];
}


- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    NodeTableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"nodeCell" forIndexPath:indexPath];
    
    NSString *base64Handle = [[self.offlineDocuments objectAtIndex:indexPath.row] objectForKey:kBase64Handle];
    MNode *node = [[MegaSDKManager sharedMegaSDK] getNodeWithHandle:[MegaSDK base64ToHandle:base64Handle]];
    NSString *name = [node getName];
    
    cell.nameLabel.text = name;
    cell.creationLabel.text = [[self.offlineDocuments objectAtIndex:indexPath.row] objectForKey:kCreationDate];
    
    if (isImage(name.lowercaseString.pathExtension)) {
        NSString *extension = [@"." stringByAppendingString:[[node getName] pathExtension]];
        NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
        NSString *fileName = [[node getBase64Handle] stringByAppendingString:extension];
        NSString *destinationFilePath = [destinationPath stringByAppendingPathComponent:@"thumbs"];
        destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
        BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
        
        if (fileExists) {
            [cell.thumbnailImageView setImage:[UIImage imageNamed:destinationFilePath]];
        }
    } else if (isVideo(name.lowercaseString.pathExtension)){
        [cell.thumbnailImageView setImage:[UIImage imageNamed:@"video"]];
    } else {
        [cell.thumbnailImageView setImage:nil];
    }
    
    return cell;
}

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    NSString *base64Handle = [[self.offlineDocuments objectAtIndex:indexPath.row] objectForKey:kBase64Handle];
    MNode *node = [[MegaSDKManager sharedMegaSDK] getNodeWithHandle:[MegaSDK base64ToHandle:base64Handle]];
    NSString *name = [node getName];
    if (isImage(name.lowercaseString.pathExtension)) {
        MWPhotoBrowser *browser = [[MWPhotoBrowser alloc] initWithDelegate:self];
        
        browser.displayActionButton = YES;
        browser.displayNavArrows = YES;
        browser.displaySelectionButtons = NO;
        browser.zoomPhotosToFill = YES;
        browser.alwaysShowControls = YES;
        browser.enableGrid = YES;
        browser.startOnGrid = NO;
        
        // Optionally set the current visible photo before displaying
        //    [browser setCurrentPhotoIndex:1];
        
        [self.navigationController pushViewController:browser animated:YES];
        
        [browser showNextPhotoAnimated:YES];
        [browser showPreviousPhotoAnimated:YES];
        NSInteger selectedIndexPhoto = [[[self.offlineDocuments objectAtIndex:indexPath.row] objectForKey:kIndex] integerValue];
        [browser setCurrentPhotoIndex:selectedIndexPhoto];
        
    } else if (isVideo(name.lowercaseString.pathExtension)) {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *path = [paths objectAtIndex:0];
        NSString *filePath = [path stringByAppendingPathComponent:base64Handle];
        NSURL *fileURL = [NSURL fileURLWithPath:filePath];
        
        MPMoviePlayerViewController *videoPlayerView = [[MPMoviePlayerViewController alloc] initWithContentURL:fileURL];
        [self presentMoviePlayerViewControllerAnimated:videoPlayerView];
        [videoPlayerView.moviePlayer play];
    }
}

- (NSUInteger)numberOfPhotosInPhotoBrowser:(MWPhotoBrowser *)photoBrowser {
    return self.offlineImages.count;
}

- (id <MWPhoto>)photoBrowser:(MWPhotoBrowser *)photoBrowser photoAtIndex:(NSUInteger)index {
    if (index < self.offlineImages.count)
        return [self.offlineImages objectAtIndex:index];
    return nil;
}

#pragma mark - Private methods

- (void)reloadUI {
    
    
    self.offlineDocuments = [NSMutableArray new];
    self.offlineImages = [NSMutableArray new];
    
    int i;
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *path = [paths objectAtIndex:0];
    NSArray *directoryContent = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:path error:NULL];
    int offsetIndex = 0;
    
    for (i = 0; i < (int)[directoryContent count]; i++) {
        NSString *filePath = [path stringByAppendingPathComponent:[directoryContent objectAtIndex:i]];
        NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:filePath error:nil];
        NSMutableDictionary *tempDictionary = [NSMutableDictionary new];
        
        NSDate *date = (NSDate*)[attributes objectForKey: NSFileCreationDate];
        NSString *filename = [NSString stringWithFormat:@"%@", [directoryContent objectAtIndex:i]];
        
        [tempDictionary setValue:[NSNumber numberWithInt:offsetIndex] forKey:kIndex];
        if (![filename.lowercaseString.pathExtension isEqualToString:@"mega"]) {
            [tempDictionary setValue:filename forKey:kBase64Handle];
            NSString *dateString = [NSDateFormatter localizedStringFromDate:date
                                                                  dateStyle:NSDateFormatterShortStyle
                                                                  timeStyle:NSDateFormatterNoStyle];
            [tempDictionary setValue:dateString forKey:kCreationDate];
            [self.offlineDocuments addObject:tempDictionary];
            
            MNode *node = [[MegaSDKManager sharedMegaSDK] getNodeWithHandle:[MegaSDK base64ToHandle:filename]];
            NSString *nodeName = [node getName];
            
            if (isImage(nodeName.lowercaseString.pathExtension)) {
                offsetIndex++;
                [self.offlineImages addObject:[MWPhoto photoWithImage:[UIImage imageNamed:filePath]]];
                
            } 
        }
    }
    [self.tableView reloadData];
}

#pragma mark - MTransferDelegate

- (void)onTransferStart:(MegaSDK *)api transfer:(MTransfer *)transfer {
}

- (void)onTransferUpdate:(MegaSDK *)api transfer:(MTransfer *)transfer {
}

- (void)onTransferFinish:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error {
    [self reloadUI];
}

-(void)onTransferTemporaryError:(MegaSDK *)api transfer:(MTransfer *)transfer error:(MError *)error {
}

@end
