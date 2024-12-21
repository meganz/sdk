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
import MEGASdk

class ContactsTableViewController: UITableViewController, MEGARequestDelegate {

    var users : MEGAUserList!
    var filterUsers = [MEGAUser]()
    
    let megaapi = (UIApplication.shared.delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        users = megaapi.contacts()
        
        for i in 0  ..< users.size  {
            let user = users.user(at: i)
            if user?.visibility == MEGAUserVisibility.visible {
                filterUsers.append(user!)
            }
        }
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }

    // MARK: - Table view data source

    override func numberOfSections(in tableView: UITableView) -> Int {
        return 1
    }

    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return filterUsers.count
    }

    
    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "contactCell", for: indexPath) as! ContactTableViewCell
        let user = filterUsers[indexPath.row]
        
        cell.nameLabel.text = user.email
        
        let avatarFilePath = Helper.pathForUser(user, path: FileManager.SearchPathDirectory.cachesDirectory, directory: "thumbs")
        let fileExists = FileManager.default.fileExists(atPath: avatarFilePath)
        
        if fileExists {
            cell.avatarImageView.image = UIImage(named: avatarFilePath)
            cell.avatarImageView.layer.cornerRadius = cell.avatarImageView.frame.size.width/2
            cell.avatarImageView.layer.masksToBounds = true
        } else {
            megaapi.getAvatarUser(user, destinationFilePath: avatarFilePath, delegate: self)
        }
        
        let numFilesShares = megaapi.inShares(for: user).size
        
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
    
    func onRequestFinish(_ api: MEGASdk, request: MEGARequest, error: MEGAError) {
        if error.type != MEGAErrorType.apiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.MEGARequestTypeGetAttrUser:
            guard let cells = tableView.visibleCells as? [ContactTableViewCell] else { break }
            for tableViewCell in cells where request.email == tableViewCell.nameLabel.text {
                let filename = request.email
                let avatarURL = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
                let avatarFilePath = avatarURL.appendingPathComponent("thumbs").appendingPathComponent(filename!)
                let fileExists = FileManager.default.fileExists(atPath: avatarFilePath.path)
                
                if fileExists {
                    tableViewCell.avatarImageView.image = UIImage(named: avatarFilePath.path)
                    tableViewCell.avatarImageView.layer.cornerRadius = tableViewCell.avatarImageView.frame.size.width/2
                    tableViewCell.avatarImageView.layer.masksToBounds = true
                }
            }
        default:
            break
        }
    }

}
