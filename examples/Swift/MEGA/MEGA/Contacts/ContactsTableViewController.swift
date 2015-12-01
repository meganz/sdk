/**
* @file ContactsTableViewController.swift
* @brief View controller that show your contacts
*
* (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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

import UIKit

class ContactsTableViewController: UITableViewController, MEGARequestDelegate {

    var users : MEGAUserList!
    var filterUsers = [MEGAUser]()
    
    let megaapi : MEGASdk! = (UIApplication.sharedApplication().delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        users = megaapi.contacts()
        
        for var i = 0 ; i < users.size.integerValue ; i++ {
            let user = users.userAtIndex(i)
            if user.access == MEGAUserVisibility.Visible {
                filterUsers.append(user)
            }
        }
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }

    // MARK: - Table view data source

    override func numberOfSectionsInTableView(tableView: UITableView) -> Int {
        return 1
    }

    override func tableView(tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return filterUsers.count
    }

    
    override func tableView(tableView: UITableView, cellForRowAtIndexPath indexPath: NSIndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCellWithIdentifier("contactCell", forIndexPath: indexPath) as! ContactTableViewCell
        let user = filterUsers[indexPath.row]
        
        cell.nameLabel.text = user.email
        
        let avatarFilePath = Helper.pathForUser(user, path: NSSearchPathDirectory.CachesDirectory, directory: "thumbs")
        let fileExists = NSFileManager.defaultManager().fileExistsAtPath(avatarFilePath)
        
        if fileExists {
            cell.avatarImageView.image = UIImage(named: avatarFilePath)
            cell.avatarImageView.layer.cornerRadius = cell.avatarImageView.frame.size.width/2
            cell.avatarImageView.layer.masksToBounds = true
        } else {
            megaapi.getAvatarUser(user, destinationFilePath: avatarFilePath, delegate: self)
        }
        
        let numFilesShares = megaapi.inSharesForUser(user).size.integerValue
        
        if numFilesShares == 0 {
            cell.shareLabel.text = "No folders shared"
        } else if numFilesShares == 1 {
            cell.shareLabel.text = "\(numFilesShares) folder shared"
        } else {
            cell.shareLabel.text = "\(numFilesShares) folders shared"
        }

        return cell
    }
    

    // MARK: - MEGA Request delegate
    
    func onRequestFinish(api: MEGASdk!, request: MEGARequest!, error: MEGAError!) {
        if error.type != MEGAErrorType.ApiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.GetAttrUser:
            for tableViewCell in tableView.visibleCells as! [ContactTableViewCell] {
                if request?.email == tableViewCell.nameLabel.text {
                    let filename = request.email
                    let avatarURL = NSFileManager.defaultManager().URLsForDirectory(.CachesDirectory, inDomains: .UserDomainMask)[0]
                    let avatarFilePath = avatarURL.URLByAppendingPathComponent("thumbs").URLByAppendingPathComponent(filename)
                    let fileExists = NSFileManager.defaultManager().fileExistsAtPath(avatarFilePath.path!)
                    
                    if fileExists {
                        tableViewCell.avatarImageView.image = UIImage(named: avatarFilePath.path!)
                        tableViewCell.avatarImageView.layer.cornerRadius = tableViewCell.avatarImageView.frame.size.width/2
                        tableViewCell.avatarImageView.layer.masksToBounds = true
                    }
                }
            }
        default:
            break
        }
    }

}
