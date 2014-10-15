//
//  CloudDriveTableViewController.m
//  Demo
//
//  Created by Javier Navarro on 15/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "CloudDriveTableViewController.h"
#import "SVProgressHUD.h"

@interface CloudDriveTableViewController ()

@property MNodeList *nodes;

@end

@implementation CloudDriveTableViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self.megaSDK fetchNodesWithListener:self];
    self.navigationItem.title = @"Cloud drive";
    self.navigationItem.hidesBackButton = YES;
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
    return [[self.nodes size] integerValue];
}


- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"nodeCell" forIndexPath:indexPath];
    
    MNode *node = [self.nodes getNodeAtPosition:indexPath.row];
    cell.textLabel.text = [node getName];
    
    NSString *dateString = [NSDateFormatter localizedStringFromDate:[node getCreationTime]
                                                          dateStyle:NSDateFormatterShortStyle
                                                          timeStyle:NSDateFormatterNoStyle];
    
    cell.detailTextLabel.text = dateString;
    
    return cell;
}

#pragma mark - MRequestDelegate

- (void)onRequestStart:(MegaSDK *)api request:(MRequest *)transfer {
}

- (void)onRequestFinish:(MegaSDK *)api request:(MRequest *)request error:(MError *)error {
    [SVProgressHUD dismiss];
    if ([error getErrorCode]) {
        return;
    }
    
    switch ([request getType]) {
        case MRequestTypeFetchNodes: {
            self.nodes = [self.megaSDK getChildrenWithParent:[self.megaSDK getRootNode]];
            [self.tableView reloadData];
            break;
        }
            
        default:
            break;
    }
}

- (void)onRequestUpdate:(MegaSDK *)api request:(MRequest *)request {
    float progress = [[request getTransferredBytes] floatValue] / [[request getTotalBytes] floatValue];
    if (progress > 0 && progress <0.99) {
        [SVProgressHUD showProgress:progress status:@"Fetching nodes"];
    } else if (progress > 0.99 || progress < 0) {
        [SVProgressHUD showProgress:1 status:@"Preparing nodes"];
    }
}

- (void)onRequestTemporaryError:(MegaSDK *)api request:(MRequest *)request error:(MError *)error {
}

@end
