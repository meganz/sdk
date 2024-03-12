/**
* @file DetailsNodeInfoViewController.swift
* @brief View controller that show details info about a node
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

class DetailsNodeInfoViewController: UIViewController, MEGADelegate, UIAlertViewDelegate {
    
    
    @IBOutlet weak var thumbnailImageView: UIImageView!
    @IBOutlet weak var nameLabel: UILabel!
    @IBOutlet weak var modificationTimeLabel: UILabel!
    @IBOutlet weak var sizeLabel: UILabel!
    @IBOutlet weak var downloadButton: UIButton!
    @IBOutlet weak var downloadProgressView: UIProgressView!
    @IBOutlet weak var saveLabel: UILabel!
    @IBOutlet weak var cancelButton: UIButton!
    
    var node : MEGANode!
    var currentTransfer : MEGATransfer!
    
    var renameAlertView : UIAlertView!
    var removeAlertView : UIAlertView!
    
    let megaapi = (UIApplication.shared.delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        // Do any additional setup after loading the view.
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
    
    func reloadUI() {
        if node.type == MEGANodeType.folder {
            downloadButton.isHidden = true
        }
        
        let thumbnailFilePath = Helper.pathForNode(node, path: FileManager.SearchPathDirectory.cachesDirectory, directory: "thumbs")
        let fileExists = FileManager.default.fileExists(atPath: thumbnailFilePath)
        
        if !fileExists {
            thumbnailImageView.image = Helper.imageForNode(node)
        } else {
            thumbnailImageView.image = UIImage(contentsOfFile: thumbnailFilePath)
        }
        
        title = node.name
        nameLabel.text = node.name
        
        let dateFormatter = DateFormatter()
        
        let theDateFormat = DateFormatter.Style.short
        let theTimeFormat = DateFormatter.Style.short
        
        dateFormatter.dateStyle = theDateFormat
        dateFormatter.timeStyle = theTimeFormat
        
        if node.isFile() {
            if let date = node.modificationTime {
                modificationTimeLabel.text = dateFormatter.string(from: date)
            }
            if let size = node.size {
                sizeLabel.text = ByteCountFormatter.string(fromByteCount: size.int64Value, countStyle: ByteCountFormatter.CountStyle.memory)
            }
        } else {
            if let date = node.creationTime {
                modificationTimeLabel.text = dateFormatter.string(from: date)
            }
            sizeLabel.text = ByteCountFormatter.string(fromByteCount: megaapi.size(for: node).int64Value, countStyle: ByteCountFormatter.CountStyle.memory)
        }
        
        let documentFilePath = Helper.pathForNode(node, path: FileManager.SearchPathDirectory.documentDirectory)
        let fileDocumentExists = FileManager.default.fileExists(atPath: documentFilePath)
        
        if fileDocumentExists {
            downloadProgressView.isHidden = true
            cancelButton.isHidden = true
            saveLabel.isHidden = false
            downloadButton.setImage(UIImage(named: "savedFile"), for: UIControl.State())
            saveLabel.text = "Saved for offline"
        } else if megaapi.transfers.size.int32Value > 0 {
            downloadProgressView.isHidden = true
            cancelButton.isHidden = true
            
            for i in 0  ..< megaapi.transfers.size.intValue {
                let transfer : MEGATransfer = megaapi.transfers.transfer(at: i)
                if transfer.nodeHandle == node.handle {
                    downloadProgressView.isHidden = false
                    cancelButton.isHidden = false
                    currentTransfer = transfer
                    
                    let progress = transfer.transferredBytes.floatValue / transfer.totalBytes.floatValue
                    downloadProgressView.setProgress(progress, animated: true)
                    continue
                }
            }
            
        } else {
            downloadProgressView.isHidden = true
            cancelButton.isHidden = true
        }
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    // MARK: - IBActions
    
    @IBAction func touchUpInsideDownload(_ sender: UIButton) {
        if node.type == MEGANodeType.file {
            let documentFilePath = Helper.pathForNode(node, path: FileManager.SearchPathDirectory.documentDirectory)
            let fileExists = FileManager.default.fileExists(atPath: documentFilePath)
            
            if !fileExists {
//                megaapi.startDownloadNode(node, localPath: documentFilePath, fileName: nil, appData: nil, startFirst: false, cancelToken: MEGACancelToken(), collisionCheck: CollisionCheckFingerprint, collisionResolution: CollisionResolutionNewWithN)
            }
        }
    }
    
    @IBAction func touchUpInsideGenerateLink(_ sender: UIButton) {
        megaapi.export(node)
    }
    
    @IBAction func touchUpInsideRename(_ sender: UIButton) {
        renameAlertView = UIAlertView(title: "Rename", message: "Enter the new name", delegate: self, cancelButtonTitle: "Cancel", otherButtonTitles: "Rename")
        renameAlertView.alertViewStyle = UIAlertViewStyle.plainTextInput
        guard let name = node.name else { return }
        let nameURL = URL(string: name)?.deletingPathExtension()
        renameAlertView.textField(at: 0)?.text = nameURL!.path
        renameAlertView.tag = 0
        renameAlertView.show()
    }
    
    @IBAction func touchUpInsideDelete(_ sender: UIButton) {
        removeAlertView = UIAlertView(title: "Remove node", message: "Are you sure?", delegate: self, cancelButtonTitle: "Cancel", otherButtonTitles: "Remove")
        removeAlertView.alertViewStyle = UIAlertViewStyle.default
        removeAlertView.tag = 1
        removeAlertView.show()
        
    }
    
    @IBAction func touchUpInsideCancelDownload(_ sender: AnyObject) {
        megaapi.cancelTransfer(currentTransfer)
    }
    
    // MARK: - AlertView delegate
    
    func alertView(_ alertView: UIAlertView, didDismissWithButtonIndex buttonIndex: Int) {
        if alertView.tag == 0 {
            guard let name = node.name else { return }
            let nameURL = URL(string: name)
            if buttonIndex == 1 {
                if nameURL!.pathExtension == "" {
                    megaapi.renameNode(node, newName: alertView.textField(at: 0)?.text ?? "")
                } else {
                    let newName = (alertView.textField(at: 0)?.text ?? "") + ("." + (nameURL?.pathExtension ?? ""))
                    nameLabel.text = newName
                    megaapi.renameNode(node, newName: newName)
                }
            }
        }
        if alertView.tag == 1 {
            if buttonIndex == 1 {
                megaapi.remove(node)
                navigationController?.popViewController(animated: true)
            }
        }
    }
    
    // MARK: - MEGA Request delegate
    
    func onRequestStart(_ api: MEGASdk, request: MEGARequest) {
        if request.type == MEGARequestType.MEGARequestTypeExport {
            SVProgressHUD.show(withStatus: "Generate link...")
        }
    }
    
    func onRequestFinish(_ api: MEGASdk, request: MEGARequest, error: MEGAError) {
        if error.type != MEGAErrorType.apiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.MEGARequestTypeGetAttrFile:
            if request.nodeHandle == node.handle {
                let node = megaapi.node(forHandle: request.nodeHandle)
                let thumbnailFilePath = Helper.pathForNode(node!, path: FileManager.SearchPathDirectory.cachesDirectory, directory: "thumbs")
                let fileExists = FileManager.default.fileExists(atPath: thumbnailFilePath)
                
                if fileExists {
                    thumbnailImageView.image = UIImage(contentsOfFile: thumbnailFilePath)
                }
            }
            
        case MEGARequestType.MEGARequestTypeExport:
            SVProgressHUD.showSuccess(withStatus: "Link Generate")
            SVProgressHUD.dismiss()
            let items = [request.link]
            let activity : UIActivityViewController = UIActivityViewController(activityItems: items as [Any], applicationActivities: nil)
            activity.excludedActivityTypes = [UIActivity.ActivityType.print, UIActivity.ActivityType.copyToPasteboard, UIActivity.ActivityType.assignToContact, UIActivity.ActivityType.saveToCameraRoll]
            self.present(activity, animated: true, completion: nil)
            
        default:
            break
        }
    }
    
    // MARK: - MEGA Global delegate
    
    func onNodesUpdate(_ api: MEGASdk, nodeList: MEGANodeList) {
        node = nodeList.node(at: 0)
    }
    
    // MARK: - MEGA Transfer delegate
    
    func onTransferStart(_ api: MEGASdk, transfer: MEGATransfer) {
        downloadProgressView.isHidden = false
        downloadProgressView.setProgress(0.0, animated: true)
        cancelButton.isHidden = false
        currentTransfer = transfer
    }
    
    func onTransferUpdate(_ api: MEGASdk, transfer: MEGATransfer) {
        if transfer.nodeHandle == node.handle {
            let progress = transfer.transferredBytes.floatValue / transfer.totalBytes.floatValue
            downloadProgressView.setProgress(progress, animated: true)
        } else {
            downloadProgressView.setProgress(0.0, animated: true)
        }
    }
    
    func onTransferFinish(_ api: MEGASdk, transfer: MEGATransfer, error: MEGAError) {
        downloadProgressView.isHidden = true
        cancelButton.isHidden = true
        
        if error.type == MEGAErrorType.apiEIncomplete {
            downloadProgressView.setProgress(0.0, animated: true)
        } else {
            downloadProgressView.setProgress(1.0, animated: true)
            saveLabel.isHidden = false
            downloadButton.setImage(UIImage(named: "savedFile"), for: UIControl.State())
            saveLabel.text = "Saved for offline"
        }
    }
}
