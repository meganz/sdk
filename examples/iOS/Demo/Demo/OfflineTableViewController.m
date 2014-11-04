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

#define kName @"kName"
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
            [tempDictionary setValue:filename forKey:kName];
            NSString *dateString = [NSDateFormatter localizedStringFromDate:date
                                           dateStyle:NSDateFormatterShortStyle
                                           timeStyle:NSDateFormatterNoStyle];
            [tempDictionary setValue:dateString forKey:kCreationDate];
            [self.offlineDocuments addObject:tempDictionary];
            
            if (isImage(filename.lowercaseString.pathExtension)) {
                offsetIndex++;
                [self.offlineImages addObject:[MWPhoto photoWithImage:[UIImage imageNamed:filePath]]];

            } 
        }
    }
    [self.tableView reloadData];
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
    
    NSString *name = [[self.offlineDocuments objectAtIndex:indexPath.row] objectForKey:kName];
    
    cell.nameLabel.text = name;
    cell.creationLabel.text = [[self.offlineDocuments objectAtIndex:indexPath.row] objectForKey:kCreationDate];
    
    if (isImage(name.lowercaseString.pathExtension)) {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *path = [paths objectAtIndex:0];
        NSString *filePath = [path stringByAppendingPathComponent:name];
        UIImage *resizeImage = [self imageByScalingAndCroppingForSize:CGSizeMake(110, 110) image:[UIImage imageWithContentsOfFile:filePath]];
        
        [cell.thumbnailImageView setImage:resizeImage];
    } else if (isVideo(name.lowercaseString.pathExtension)){
        [cell.thumbnailImageView setImage:[UIImage imageNamed:@"video"]];
    } else {
        [cell.thumbnailImageView setImage:nil];
    }
    
    return cell;
}

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    NSString *name = [[self.offlineDocuments objectAtIndex:indexPath.row] objectForKey:kName];
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
        NSString *filePath = [path stringByAppendingPathComponent:name];
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

- (UIImage*)imageByScalingAndCroppingForSize:(CGSize)targetSize image:(UIImage *)image {
    UIImage *newImage = nil;
    CGSize imageSize = image.size;
    CGFloat width = imageSize.width;
    CGFloat height = imageSize.height;
    CGFloat targetWidth = targetSize.width;
    CGFloat targetHeight = targetSize.height;
    CGFloat scaleFactor = 0.0;
    CGFloat scaledWidth = targetWidth;
    CGFloat scaledHeight = targetHeight;
    CGPoint thumbnailPoint = CGPointMake(0.0,0.0);
    
    if (CGSizeEqualToSize(imageSize, targetSize) == NO) {
        CGFloat widthFactor = targetWidth / width;
        CGFloat heightFactor = targetHeight / height;
        
        if (widthFactor > heightFactor) {
            scaleFactor = widthFactor;
        }
        else {
            scaleFactor = heightFactor;
        }
        
        scaledWidth  = width * scaleFactor;
        scaledHeight = height * scaleFactor;
        
        if (widthFactor > heightFactor) {
            thumbnailPoint.y = (targetHeight - scaledHeight) * 0.5;
        }
        else {
            if (widthFactor < heightFactor) {
                thumbnailPoint.x = (targetWidth - scaledWidth) * 0.5;
            }
        }
    }
    
    UIGraphicsBeginImageContext(targetSize);
    
    CGRect thumbnailRect = CGRectZero;
    thumbnailRect.origin = thumbnailPoint;
    thumbnailRect.size.width  = scaledWidth;
    thumbnailRect.size.height = scaledHeight;
    
    [image drawInRect:thumbnailRect];
    
    newImage = UIGraphicsGetImageFromCurrentImageContext();
    
    if(newImage == nil) {
        NSLog(@"could not scale image");
    }
    
    UIGraphicsEndImageContext();
    
    return newImage;
}

@end
