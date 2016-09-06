/**
 * @file ContactsTableViewController.m
 * @brief View controller that show your contacts
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
#import "ContactsTableViewController.h"
#import "ContactTableViewCell.h"
#import "Helper.h"

@interface ContactsTableViewController ()

@property (nonatomic, strong) MEGAUserList *users;
@property (nonatomic, strong) NSMutableArray *usersArray;

@end

@implementation ContactsTableViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.usersArray = [NSMutableArray new];
    
    self.users = [[MEGASdkManager sharedMEGASdk] contacts];
    for (NSInteger i = 0; i < [[self.users size] integerValue] ; i++) {
        MEGAUser *u = [self.users userAtIndex:i];
        if ([u visibility] == MEGAUserVisibilityVisible)
        [self.usersArray addObject:u];
    }
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return  [self.usersArray count];
}


- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    ContactTableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"contactCell" forIndexPath:indexPath];
    
    MEGAUser *user = [self.usersArray objectAtIndex:indexPath.row];
    
    cell.nameLabel.text = [user email];
    
    NSString *avatarFilePath = [Helper pathForUser:user searchPath:NSCachesDirectory directory:@"thumbs"];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:avatarFilePath];
    
    if (fileExists) {
        [cell.avatarImageView setImage:[UIImage imageWithContentsOfFile:avatarFilePath]];
        cell.avatarImageView.layer.cornerRadius = cell.avatarImageView.frame.size.width/2;
        cell.avatarImageView.layer.masksToBounds = YES;
    } else {
        [[MEGASdkManager sharedMEGASdk] getAvatarUser:user destinationFilePath:avatarFilePath delegate:self];
    }
    
    int numFilesShares = [[[[MEGASdkManager sharedMEGASdk] inSharesForUser:user] size] intValue];
    if (numFilesShares == 0) {
        cell.shareLabel.text = NSLocalizedString(@"noFoldersShare", @"No folders shared");
    } else  if (numFilesShares == 1 ) {
        NSString *localizedString = NSLocalizedString(@"oneFolderShare", @" folder shared");
        cell.shareLabel.text = [NSString stringWithFormat:@"%d %@", numFilesShares, localizedString];
    } else {
        NSString *localizedString = NSLocalizedString(@"foldersShare", @" folders shared");
        cell.shareLabel.text = [NSString stringWithFormat:@"%d %@", numFilesShares, localizedString];
    }
    
    return cell;
}

#pragma mark - MEGARequestDelegate

- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request {
}

- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error {
    if ([error type]) {
        return;
    }
    
    switch ([request type]) {
        case MEGARequestTypeGetAttrUser: {
            for (ContactTableViewCell *ctvc in [self.tableView visibleCells]) {
                if ([[request email] isEqualToString:[ctvc.nameLabel text]]) {
                    NSString *fileName = [request email];                    
                    NSString *cacheDirectory = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
                    NSString *avatarFilePath = [cacheDirectory stringByAppendingPathComponent:@"thumbs"];
                    avatarFilePath = [avatarFilePath stringByAppendingPathComponent:fileName];
                    avatarFilePath = [avatarFilePath stringByAppendingString:@".png"];
                    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:avatarFilePath];
                    if (fileExists) {
                        [ctvc.avatarImageView setImage:[UIImage imageWithContentsOfFile:avatarFilePath]];
                        ctvc.avatarImageView.layer.cornerRadius = ctvc.avatarImageView.frame.size.width/2;
                        ctvc.avatarImageView.layer.masksToBounds = YES;
                    }
                }
            }
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

@end
