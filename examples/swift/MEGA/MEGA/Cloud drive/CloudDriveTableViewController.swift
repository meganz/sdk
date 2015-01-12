/**
* @file CloudDriveTableViewController.swift
* @brief View controller that show MEGA nodes and allow navigate through folders
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

class CloudDriveTableViewController: UITableViewController, MEGADelegate {
    
    @IBOutlet weak var headerView: UIView!
    @IBOutlet weak var filesFolderLabel: UILabel!
    
    var parentNode : MEGANode!
    var nodes : MEGANodeList!
    
    let megaapi : MEGASdk! = (UIApplication.sharedApplication().delegate as AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        let thumbsDirectory = NSSearchPathForDirectoriesInDomains(.CachesDirectory, .UserDomainMask, true)[0].stringByAppendingPathComponent("thumbs")
        
        if !NSFileManager.defaultManager().fileExistsAtPath(thumbsDirectory) {
            var error : NSError?
            if !NSFileManager.defaultManager().createDirectoryAtPath(thumbsDirectory, withIntermediateDirectories: false, attributes: nil, error: &error) {
                println("Create directory error \(error?.localizedDescription)")
            }
        }
    }
    
    override func viewWillAppear(animated: Bool) {
        super.viewWillAppear(animated)
        megaapi.addMEGADelegate(self)
        megaapi.retryPendingConnections()
        reloadUI()
    }
    
    override func viewWillDisappear(animated: Bool) {
        megaapi.removeMEGADelegate(self)
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func reloadUI() {
        var filesAndFolders : String!
        
        if parentNode == nil {
            navigationItem.title = "Cloud drive"
            nodes = megaapi.childrenForParent(megaapi.rootNode)
            
            let files = megaapi.numberChildFilesForParent(megaapi.rootNode)
            let folders = megaapi.numberChildFoldersForParent(megaapi.rootNode)
            
            if files == 0 || files > 1 {
                if folders == 0 || folders > 1 {
                    filesAndFolders = "\(folders) folders, \(files) files"
                } else if folders == 0 {
                    filesAndFolders = "\(folders) folder, \(files) files"
                }
            } else if files == 1 {
                if folders == 0 || folders > 1 {
                    filesAndFolders = "\(folders) folders, \(files) file"
                } else if folders == 1 {
                    filesAndFolders = "\(folders) folder, \(files) file"
                }
            }
            
        } else {
            navigationItem.title = parentNode.name
            nodes = megaapi.childrenForParent(parentNode)
            
            let files = megaapi.numberChildFilesForParent(parentNode)
            let folders = megaapi.numberChildFoldersForParent(parentNode)
            
            if files == 0 || files > 1 {
                if folders == 0 || folders > 1 {
                    filesAndFolders = "\(folders) folders, \(files) files"
                } else if folders == 0 {
                    filesAndFolders = "\(folders) folder, \(files) files"
                }
            } else if files == 1 {
                if folders == 0 || folders > 1 {
                    filesAndFolders = "\(folders) folders, \(files) file"
                } else if folders == 1 {
                    filesAndFolders = "\(folders) folder, \(files) file"
                }
            }
        }
        
        filesFolderLabel.text = filesAndFolders
        tableView.reloadData()
    }
    
    // MARK: - Table view data source
    
    override func numberOfSectionsInTableView(tableView: UITableView) -> Int {
        return 1
    }
    
    override func tableView(tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return nodes.size.integerValue
    }
    
    override func tableView(tableView: UITableView, cellForRowAtIndexPath indexPath: NSIndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCellWithIdentifier("nodeCell", forIndexPath: indexPath) as NodeTableViewCell
        let node = nodes.nodeAtIndex(indexPath.row)
        
        cell.nameLabel.text = node.name
        
        let thumbnailFilePath = Helper.pathForNode(node, path:NSSearchPathDirectory.CachesDirectory, directory: "thumbs")
        let fileExists = NSFileManager.defaultManager().fileExistsAtPath(thumbnailFilePath)
        
        if !fileExists && node.hasThumbnail() {
            megaapi.getThumbnailNode(node, destinationFilePath: thumbnailFilePath)
        }
        
        if !fileExists {
            cell.thumbnailImageView.image = Helper.imageForNode(node)
        } else {
            cell.thumbnailImageView.image = UIImage(named: thumbnailFilePath)
        }
        
        if node.isFile() {
            cell.subtitleLabel.text = NSByteCountFormatter.stringFromByteCount(node.size.longLongValue, countStyle: NSByteCountFormatterCountStyle.Memory)
        } else {
            let files = megaapi.numberChildFilesForParent(node)
            let folders = megaapi.numberChildFoldersForParent(node)
            
            cell.subtitleLabel.text = "\(folders) folders, \(files) files"
            cell.thumbnailImageView.image = UIImage(named: "folder")
        }
        
        cell.nodeHandle = node.handle
        
        return cell
    }
    
    
    override func tableView(tableView: UITableView, canEditRowAtIndexPath indexPath: NSIndexPath) -> Bool {
        return true
    }
    
    override func tableView(tableView: UITableView, viewForHeaderInSection section: Int) -> UIView? {
        return headerView
    }
    
    override func tableView(tableView: UITableView, heightForHeaderInSection section: Int) -> CGFloat {
        return 20.0
    }
    
    override func tableView(tableView: UITableView, commitEditingStyle editingStyle: UITableViewCellEditingStyle, forRowAtIndexPath indexPath: NSIndexPath) {
        if editingStyle == .Delete {
            let node = nodes.nodeAtIndex(indexPath.row)
            megaapi.removeNode(node)
        }
    }
    
    override func tableView(tableView: UITableView, didSelectRowAtIndexPath indexPath: NSIndexPath) {
        let node = nodes.nodeAtIndex(indexPath.row)
        
        if node.isFolder() {
            let storyboard = UIStoryboard(name: "Cloud", bundle: nil)
            let cdtvc = storyboard.instantiateViewControllerWithIdentifier("CloudDriveID") as CloudDriveTableViewController
            cdtvc.parentNode = node
            self.navigationController?.pushViewController(cdtvc, animated: true)
        }
    }
    
    override func tableView(tableView: UITableView, accessoryButtonTappedForRowWithIndexPath indexPath: NSIndexPath) {
        let node = nodes.nodeAtIndex(indexPath.row)
        
        let storyboard = UIStoryboard(name: "Cloud", bundle: nil)
        let nidvc = storyboard.instantiateViewControllerWithIdentifier("nodeInfoDetails") as DetailsNodeInfoViewController
        nidvc.node = node
        self.navigationController?.pushViewController(nidvc, animated: true)
    }
    
    // MARK: - MEGA Request delegate
    
    func onRequestStart(api: MEGASdk!, request: MEGARequest!) {
        
    }
    
    func onRequestFinish(api: MEGASdk!, request: MEGARequest!, error: MEGAError!) {
        if error.type != MEGAErrorType.ApiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.GetAttrFile:
            for tableViewCell in tableView.visibleCells() as [NodeTableViewCell] {
                if request?.nodeHandle == tableViewCell.nodeHandle {
                    let node = megaapi.nodeForHandle(request.nodeHandle)
                    let thumbnailFilePath = Helper.pathForNode(node, path: NSSearchPathDirectory.CachesDirectory, directory: "thumbs")
                    let fileExists = NSFileManager.defaultManager().fileExistsAtPath(thumbnailFilePath)
                    
                    if fileExists {
                        tableViewCell.thumbnailImageView.image = UIImage(named: thumbnailFilePath)
                    }
                }
            }
            break
            
        default:
            break
        }
    }
    
    // MARK: - MEGA Global delegate
    
    func onNodesUpdate(api: MEGASdk!, nodeList: MEGANodeList!) {
        reloadUI()
    }
    
}
