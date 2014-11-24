#import "ContactsTableViewController.h"
#import "ContactTableViewCell.h"

@interface ContactsTableViewController () {
    NSString *cacheDirectory;
}

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
        if ([u access] == MEGAUserVisibilityVisible)
        [self.usersArray addObject:u];
    }
    cacheDirectory = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
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
    return  [self.usersArray count];
}


- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    ContactTableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"contactCell" forIndexPath:indexPath];
    
    MEGAUser *user = [self.usersArray objectAtIndex:indexPath.row];
    
    cell.nameLabel.text = [user email];
    
    NSString *fileName = [user email];
    NSString *destinationFilePath = [cacheDirectory stringByAppendingPathComponent:@"thumbs"];
    destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
    
    if (fileExists) {
        [cell.avatarImageView setImage:[UIImage imageWithContentsOfFile:destinationFilePath]];
        cell.avatarImageView.layer.cornerRadius = cell.avatarImageView.frame.size.width/2;
        cell.avatarImageView.layer.masksToBounds = YES;
    } else {
        [[MEGASdkManager sharedMEGASdk] getAvatarUser:user destinationFilePath:destinationFilePath delegate:self];
    }
    
    NSInteger numFilesShares = [[[[MEGASdkManager sharedMEGASdk] inSharesForUser:user] size] integerValue];
    if (numFilesShares == 0) {
        cell.shareLabel.text = @"No folders share";
    } else  if (numFilesShares == 1 ) {
        cell.shareLabel.text = [NSString stringWithFormat:@"%d folder share", numFilesShares];
    } else {
        cell.shareLabel.text = [NSString stringWithFormat:@"%d folders share", numFilesShares];
    }
    
    return cell;
}


/*
// Override to support conditional editing of the table view.
- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    // Return NO if you do not want the specified item to be editable.
    return YES;
}
*/

/*
// Override to support editing the table view.
- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath {
    if (editingStyle == UITableViewCellEditingStyleDelete) {
        // Delete the row from the data source
        [tableView deleteRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationFade];
    } else if (editingStyle == UITableViewCellEditingStyleInsert) {
        // Create a new instance of the appropriate class, insert it into the array, and add a new row to the table view
    }   
}
*/

/*
// Override to support rearranging the table view.
- (void)tableView:(UITableView *)tableView moveRowAtIndexPath:(NSIndexPath *)fromIndexPath toIndexPath:(NSIndexPath *)toIndexPath {
}
*/

/*
// Override to support conditional rearranging of the table view.
- (BOOL)tableView:(UITableView *)tableView canMoveRowAtIndexPath:(NSIndexPath *)indexPath {
    // Return NO if you do not want the item to be re-orderable.
    return YES;
}
*/

/*
#pragma mark - Navigation

// In a storyboard-based application, you will often want to do a little preparation before navigation
- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    // Get the new view controller using [segue destinationViewController].
    // Pass the selected object to the new view controller.
}
*/

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
                    NSString *destinationFilePath = [cacheDirectory stringByAppendingPathComponent:@"thumbs"];
                    destinationFilePath = [destinationFilePath stringByAppendingPathComponent:fileName];
                    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:destinationFilePath];
                    if (fileExists) {
                        [ctvc.avatarImageView setImage:[UIImage imageWithContentsOfFile:destinationFilePath]];
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
