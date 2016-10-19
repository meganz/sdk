/**
* @file OfflineTableViewController.swift
* @brief View controller that show offline files
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

class OfflineTableViewController: UITableViewController, MEGATransferDelegate {
    
    var offlineDocuments = [MEGANode]()
    
    let megaapi : MEGASdk! = (UIApplication.sharedApplication().delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
    }
    
    override func viewWillAppear(animated: Bool) {
        super.viewWillAppear(animated)
        
        reloadUI()
        megaapi.addMEGATransferDelegate(self)
        megaapi.retryPendingConnections()
    }
    
    override func viewWillDisappear(animated: Bool) {
        super.viewWillDisappear(animated)
        megaapi.removeMEGATransferDelegate(self)
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func reloadUI() {
        offlineDocuments = [MEGANode]()
        
        let documentDirectory = NSSearchPathForDirectoriesInDomains(NSSearchPathDirectory.DocumentDirectory, NSSearchPathDomainMask.UserDomainMask, true)[0] 
        
        if let directoryContent : Array = try? NSFileManager.defaultManager().contentsOfDirectoryAtPath(documentDirectory) {
            
            for i in 0  ..< directoryContent.count {
                let filename: String = String(directoryContent[i] as NSString)
                
                if !((filename.lowercaseString as NSString).pathExtension == "mega") {
                    if let node = megaapi.nodeForHandle(MEGASdk.handleForBase64Handle(filename)) {
                        offlineDocuments.append(node)
                    }
                }
            }
        }
        
        tableView.reloadData()
    }
    
    // MARK: - Table view data source
    
    override func numberOfSectionsInTableView(tableView: UITableView) -> Int {
        return 1
    }
    
    override func tableView(tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return offlineDocuments.count
    }
    
    
    override func tableView(tableView: UITableView, cellForRowAtIndexPath indexPath: NSIndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCellWithIdentifier("nodeCell", forIndexPath: indexPath) as! NodeTableViewCell
        let node = offlineDocuments[indexPath.row]
        
        cell.nameLabel.text = node.name
        
        let thumbnailFilePath = Helper.pathForNode(node, path:NSSearchPathDirectory.CachesDirectory, directory: "thumbs")
        let fileExists = NSFileManager.defaultManager().fileExistsAtPath(thumbnailFilePath)
        
        if !fileExists {
            cell.thumbnailImageView.image = Helper.imageForNode(node)
        } else {
            cell.thumbnailImageView.image = UIImage(named: thumbnailFilePath)
        }
        
        
        if node.isFile() {
            cell.subtitleLabel.text = NSByteCountFormatter().stringFromByteCount(node.size.longLongValue)
        } else {
            let files = megaapi.numberChildFilesForParent(node)
            let folders = megaapi.numberChildFoldersForParent(node)
            
            cell.subtitleLabel.text = "\(folders) folders, \(files) files"
            cell.thumbnailImageView.image = UIImage(named: "folder")
        }
        
        return cell
    }
    
    // MARK: - MEGA Transfer delegate
    
    func onTransferFinish(api: MEGASdk!, transfer: MEGATransfer!, error: MEGAError!) {
        reloadUI()
    }
    
}
