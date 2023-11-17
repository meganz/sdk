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
import MEGASdk

class OfflineTableViewController: UITableViewController, MEGATransferDelegate {
    
    var offlineDocuments = [MEGANode]()
    
    let megaapi = (UIApplication.shared.delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
    }
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        
        reloadUI()
        megaapi.add(self)
        megaapi.retryPendingConnections()
    }
    
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        megaapi.remove(self)
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func reloadUI() {
        offlineDocuments = [MEGANode]()
        
        let documentDirectory = NSSearchPathForDirectoriesInDomains(FileManager.SearchPathDirectory.documentDirectory, FileManager.SearchPathDomainMask.userDomainMask, true)[0] 
        
        if let directoryContent : Array = try? FileManager.default.contentsOfDirectory(atPath: documentDirectory) {
            
            for i in 0  ..< directoryContent.count {
                let filename: String = String(directoryContent[i] as NSString)
                
                if !((filename.lowercased() as NSString).pathExtension == "mega") {
                    if let node = megaapi.node(forHandle: MEGASdk.handle(forBase64Handle: filename)) {
                        offlineDocuments.append(node)
                    }
                }
            }
        }
        
        tableView.reloadData()
    }
    
    // MARK: - Table view data source
    
    override func numberOfSections(in tableView: UITableView) -> Int {
        return 1
    }
    
    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return offlineDocuments.count
    }
    
    
    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "nodeCell", for: indexPath) as! NodeTableViewCell
        let node = offlineDocuments[indexPath.row]
        
        cell.nameLabel.text = node.name
        
        let thumbnailFilePath = Helper.pathForNode(node, path:FileManager.SearchPathDirectory.cachesDirectory, directory: "thumbs")
        let fileExists = FileManager.default.fileExists(atPath: thumbnailFilePath)
        
        if !fileExists {
            cell.thumbnailImageView.image = Helper.imageForNode(node)
        } else {
            cell.thumbnailImageView.image = UIImage(named: thumbnailFilePath)
        }
        
        
        if node.isFile() {
            cell.subtitleLabel.text = ByteCountFormatter().string(fromByteCount: node.size?.int64Value ?? 0)
        } else {
            let files = megaapi.numberChildFiles(forParent: node)
            let folders = megaapi.numberChildFolders(forParent: node)
            
            cell.subtitleLabel.text = "\(folders) folders, \(files) files"
            cell.thumbnailImageView.image = UIImage(named: "folder")
        }
        
        return cell
    }
    
    // MARK: - MEGA Transfer delegate
    
    func onTransferFinish(_ api: MEGASdk, transfer: MEGATransfer, error: MEGAError) {
        reloadUI()
    }
    
}
