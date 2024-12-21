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
import AssetsLibrary
import MEGASdk

class CloudDriveTableViewController: UITableViewController, MEGADelegate, UIActionSheetDelegate, UIAlertViewDelegate, UINavigationControllerDelegate, UIImagePickerControllerDelegate {
    
    @IBOutlet weak var headerView: UIView!
    @IBOutlet weak var filesFolderLabel: UILabel!
    
    @IBOutlet weak var addBarButtonItem: UIBarButtonItem!
    
    var parentNode : MEGANode!
    var nodes : MEGANodeList!
    
    let megaapi = (UIApplication.shared.delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        let thumbsURL = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        let thumbsDirectory = thumbsURL.appendingPathComponent("thumbs")
        
        if !FileManager.default.fileExists(atPath: thumbsDirectory.path) {
            do {
                try FileManager.default.createDirectory(atPath: thumbsDirectory.path, withIntermediateDirectories: true, attributes: nil)
            } catch let error as NSError {
                NSLog("Unable to create directory \(error.debugDescription)")
            }
        }
    }
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        megaapi.add(self)
        megaapi.retryPendingConnections()
        reloadUI()
    }
    
    override func viewWillDisappear(_ animated: Bool) {
        megaapi.remove(self)
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func reloadUI() {
        var filesAndFolders : String!
        
        if parentNode == nil {
            navigationItem.title = "Cloud Drive"
            guard let rootNode = megaapi.rootNode else { return }
            nodes = megaapi.children(forParent: rootNode)
            
            let files = megaapi.numberChildFiles(forParent: rootNode)
            let folders = megaapi.numberChildFolders(forParent: rootNode)
            
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
            nodes = megaapi.children(forParent: parentNode)
            
            let files = megaapi.numberChildFiles(forParent: parentNode)
            let folders = megaapi.numberChildFolders(forParent: parentNode)
            
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
    
    override func numberOfSections(in tableView: UITableView) -> Int {
        return 1
    }
    
    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        nodes.size
    }
    
    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "nodeCell", for: indexPath) as! NodeTableViewCell
        guard let node = nodes.node(at: indexPath.row) else { return UITableViewCell() }
        
        cell.nameLabel.text = node.name
        
        let thumbnailFilePath = Helper.pathForNode(node, path:FileManager.SearchPathDirectory.cachesDirectory, directory: "thumbs")
        let fileExists = FileManager.default.fileExists(atPath: thumbnailFilePath)
        
        if !fileExists {
            if node.hasThumbnail() {
                megaapi.getThumbnailNode(node, destinationFilePath: thumbnailFilePath)
            } else {
                cell.thumbnailImageView.image = Helper.imageForNode(node)
            }
        } else {
            cell.thumbnailImageView.image = UIImage(named: thumbnailFilePath)
        }
        
        if (node.isFile()) {
            cell.subtitleLabel.text = ByteCountFormatter.string(fromByteCount: (node.size?.int64Value)!, countStyle: ByteCountFormatter.CountStyle.memory)
        } else {
            let files = megaapi.numberChildFiles(forParent: node)
            let folders = megaapi.numberChildFolders(forParent: node)
            
            cell.subtitleLabel.text = "\(folders) folders, \(files) files"
        }
        
        cell.nodeHandle = node.handle
        
        return cell
    }
    
    
    override func tableView(_ tableView: UITableView, canEditRowAt indexPath: IndexPath) -> Bool {
        return true
    }
    
    override func tableView(_ tableView: UITableView, viewForHeaderInSection section: Int) -> UIView? {
        return headerView
    }
    
    override func tableView(_ tableView: UITableView, heightForHeaderInSection section: Int) -> CGFloat {
        return 20.0
    }
    
    override func tableView(_ tableView: UITableView, commit editingStyle: UITableViewCell.EditingStyle, forRowAt indexPath: IndexPath) {
        if editingStyle == .delete {
            guard let node = nodes.node(at: indexPath.row) else { return }
            megaapi.remove(node)
        }
    }
    
    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        let node = nodes.node(at: indexPath.row)
        
        if (node?.isFolder())! {
            let storyboard = UIStoryboard(name: "Cloud", bundle: nil)
            let cdtvc = storyboard.instantiateViewController(withIdentifier: "CloudDriveID") as!CloudDriveTableViewController
            cdtvc.parentNode = node
            self.navigationController?.pushViewController(cdtvc, animated: true)
        }
    }
    
    override func tableView(_ tableView: UITableView, accessoryButtonTappedForRowWith indexPath: IndexPath) {
        let node = nodes.node(at: indexPath.row)
        
        let storyboard = UIStoryboard(name: "Cloud", bundle: nil)
        let nidvc = storyboard.instantiateViewController(withIdentifier: "nodeInfoDetails") as! DetailsNodeInfoViewController
        nidvc.node = node
        self.navigationController?.pushViewController(nidvc, animated: true)
    }
    
    // MARK: - IBActions
    
    @IBAction func addOption(_ sender: UIBarButtonItem) {
        let actionSheet = UIActionSheet(title: nil, delegate: self, cancelButtonTitle: "Cancel", destructiveButtonTitle: nil, otherButtonTitles: "Create folder", "Upload photo")
        actionSheet.show(from: (self.tabBarController?.tabBar)!)
    }
    
    // MARK: - UIActionSheetDelegate
    
    func actionSheet(_ actionSheet: UIActionSheet, clickedButtonAt buttonIndex: Int) {
        if buttonIndex == 1 { //Create new folder
            let folderAlertView = UIAlertView(title: "Create new folder", message: "Name of the new folder", delegate: self, cancelButtonTitle: "Cancel", otherButtonTitles: "Create")
            folderAlertView.alertViewStyle = UIAlertViewStyle.plainTextInput
            folderAlertView.textField(at: 0)?.text = ""
            folderAlertView.tag = 1
            folderAlertView.show()
        } else if buttonIndex == 2 { //Upload photo
            let imagePickerController = UIImagePickerController()
            imagePickerController.modalPresentationStyle = UIModalPresentationStyle.currentContext
            imagePickerController.sourceType = UIImagePickerController.SourceType.photoLibrary
            imagePickerController.delegate = self
            
            self.tabBarController?.present(imagePickerController, animated: true, completion: nil)
        }
    }
    
    // MARK: - UIAlertViewDelegate
    
    func alertView(_ alertView: UIAlertView, didDismissWithButtonIndex buttonIndex: Int) {
        if alertView.tag == 1 {
            if buttonIndex == 1 {
                if parentNode != nil {
                    megaapi.createFolder(withName: (alertView.textField(at: 0)?.text ?? ""), parent: parentNode)
                } else {
                    guard let rootNode = megaapi.rootNode else { return }
                    megaapi.createFolder(withName: (alertView.textField(at: 0)?.text) ?? "", parent: rootNode)
                }
            }
        }
    }
    
    // MARK: - UIImagePickerControllerDelegate
    func imagePickerController(_ picker: UIImagePickerController, didFinishPickingMediaWithInfo info: [UIImagePickerController.InfoKey : Any]) {
        // Local variable inserted by Swift 4.2 migrator.
        let info = convertFromUIImagePickerControllerInfoKeyDictionary(info)

        let assetURL = info[convertFromUIImagePickerControllerInfoKey(UIImagePickerController.InfoKey.referenceURL)] as! URL
        let library = ALAssetsLibrary()
        library.asset(for: assetURL, resultBlock: { (asset: ALAsset!) in
            let name: String = asset.defaultRepresentation().filename()
            let modificationTime: Date = asset.value(forProperty: ALAssetPropertyDate) as! Date
            let imageView = UIImageView()
            imageView.image = (info[convertFromUIImagePickerControllerInfoKey(UIImagePickerController.InfoKey.originalImage)] as! UIImage)
            let webData: Data = imageView.image!.jpegData(compressionQuality: 0.9)!
            
            let localFileURL = URL(fileURLWithPath:NSTemporaryDirectory())
            let localFilePath = localFileURL.appendingPathComponent(name)
            try? webData.write(to: URL(fileURLWithPath: localFilePath.path), options: [.atomic])
            
            let attributes = [FileAttributeKey.modificationDate : modificationTime]
            try? FileManager.default.setAttributes(attributes, ofItemAtPath: localFilePath.path)
            
            if let parentNode = self.parentNode {
                self.megaapi.startUpload(withLocalPath: localFilePath.path, parent: parentNode, fileName: nil, appData: nil, isSourceTemporary: false, startFirst: false, cancelToken: MEGACancelToken())
            }
            
            self.dismiss(animated: true, completion: nil)
            
            }, failureBlock: nil)
    }
    
    func imagePickerControllerDidCancel(_ picker: UIImagePickerController) {
        self.dismiss(animated: true, completion: nil)
    }
    
    // MARK: - MEGA Request delegate
    
    func onRequestFinish(_ api: MEGASdk, request: MEGARequest, error: MEGAError) {
        if error.type != MEGAErrorType.apiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.MEGARequestTypeFetchNodes:
            SVProgressHUD.dismiss()
            
        case MEGARequestType.MEGARequestTypeGetAttrFile:
            guard let cells = tableView.visibleCells as? [NodeTableViewCell] else { break }
            for tableViewCell in cells where request.nodeHandle == tableViewCell.nodeHandle {
                let node = megaapi.node(forHandle: request.nodeHandle)
                let thumbnailFilePath = Helper.pathForNode(node!, path: FileManager.SearchPathDirectory.cachesDirectory, directory: "thumbs")
                let fileExists = FileManager.default.fileExists(atPath: thumbnailFilePath)
                
                if fileExists {
                    tableViewCell.thumbnailImageView.image = UIImage(named: thumbnailFilePath)
                }
            }
            
        default:
            break
        }
    }
    
    // MARK: - MEGA Global delegate
    
    func onNodesUpdate(_ api: MEGASdk, nodeList: MEGANodeList) {
        reloadUI()
    }
    
}

// Helper function inserted by Swift 4.2 migrator.
fileprivate func convertFromUIImagePickerControllerInfoKeyDictionary(_ input: [UIImagePickerController.InfoKey: Any]) -> [String: Any] {
	return Dictionary(uniqueKeysWithValues: input.map {key, value in (key.rawValue, value)})
}

// Helper function inserted by Swift 4.2 migrator.
fileprivate func convertFromUIImagePickerControllerInfoKey(_ input: UIImagePickerController.InfoKey) -> String {
	return input.rawValue
}
