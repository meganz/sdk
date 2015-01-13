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
    
    let megaapi : MEGASdk! = (UIApplication.sharedApplication().delegate as AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        // Do any additional setup after loading the view.
    }
    
    override func viewWillAppear(animated: Bool) {
        super.viewWillAppear(animated)
        reloadUI()
        megaapi.addMEGADelegate(self)
        megaapi.retryPendingConnections()
    }
    
    override func viewWillDisappear(animated: Bool) {
        super.viewWillDisappear(animated)
        megaapi.removeMEGADelegate(self)
    }
    
    func reloadUI() {
        if node.type == MEGANodeType.Folder {
            downloadButton.hidden = true
        }
        
        let thumbnailFilePath = Helper.pathForNode(node, path: NSSearchPathDirectory.CachesDirectory, directory: "thumbs")
        let fileExists = NSFileManager.defaultManager().fileExistsAtPath(thumbnailFilePath)
        
        if !fileExists {
            thumbnailImageView.image = Helper.imageForNode(node)
        } else {
            thumbnailImageView.image = UIImage(contentsOfFile: thumbnailFilePath)
        }
        
        title = node.name
        nameLabel.text = node.name
        
        let dateFormatter = NSDateFormatter()
        
        var theDateFormat = NSDateFormatterStyle.ShortStyle
        let theTimeFormat = NSDateFormatterStyle.ShortStyle
        
        dateFormatter.dateStyle = theDateFormat
        dateFormatter.timeStyle = theTimeFormat
        
        if node.isFile() {
            modificationTimeLabel.text = dateFormatter.stringFromDate(node.modificationTime)
            sizeLabel.text = NSByteCountFormatter.stringFromByteCount(node.size.longLongValue, countStyle: NSByteCountFormatterCountStyle.Memory)
        } else {
            modificationTimeLabel.text = dateFormatter.stringFromDate(node.creationTime)
            sizeLabel.text = NSByteCountFormatter.stringFromByteCount(megaapi.sizeForNode(node).longLongValue, countStyle: NSByteCountFormatterCountStyle.Memory)
        }
        
        let documentFilePath = Helper.pathForNode(node, path: NSSearchPathDirectory.DocumentDirectory)
        let fileDocumentExists = NSFileManager.defaultManager().fileExistsAtPath(documentFilePath)
        
        if fileDocumentExists {
            downloadProgressView.hidden = true
            cancelButton.hidden = true
            saveLabel.hidden = false
            downloadButton.setImage(UIImage(named: "savedFile"), forState: UIControlState.Normal)
            saveLabel.text = "Saved for offline"
        } else if megaapi.transfers.size.intValue > 0 {
            downloadProgressView.hidden = true
            cancelButton.hidden = true
            
            var i : Int
            for i = 0 ; i < megaapi.transfers.size.integerValue ; i++ {
                let transfer : MEGATransfer = megaapi.transfers.transferAtIndex(i)
                if transfer.nodeHandle == node.handle {
                    downloadProgressView.hidden = false
                    cancelButton.hidden = false
                    currentTransfer = transfer
                    
                    let progress = transfer.transferredBytes.floatValue / transfer.totalBytes.floatValue
                    downloadProgressView.setProgress(progress, animated: true)
                    continue
                }
            }
            
        } else {
            downloadProgressView.hidden = true
            cancelButton.hidden = true
        }
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    // MARK: - IBActions
    
    @IBAction func touchUpInsideDownload(sender: UIButton) {
        if node.type == MEGANodeType.File {
            let documentFilePath = Helper.pathForNode(node, path: NSSearchPathDirectory.DocumentDirectory)
            let fileExists = NSFileManager.defaultManager().fileExistsAtPath(documentFilePath)
            
            if !fileExists {
                megaapi.startDownloadNode(node, localPath: documentFilePath)
            }
        }
    }
    
    @IBAction func touchUpInsideGenerateLink(sender: UIButton) {
        megaapi.exportNode(node)
    }
    
    @IBAction func touchUpInsideRename(sender: UIButton) {
        renameAlertView = UIAlertView(title: "Rename", message: "Enter the new name", delegate: self, cancelButtonTitle: "Cancel", otherButtonTitles: "Rename")
        renameAlertView.alertViewStyle = UIAlertViewStyle.PlainTextInput
        renameAlertView.textFieldAtIndex(0)?.text = node.name.lastPathComponent.stringByDeletingPathExtension
        renameAlertView.tag = 0
        renameAlertView.show()
    }
    
    @IBAction func touchUpInsideDelete(sender: UIButton) {
        removeAlertView = UIAlertView(title: "Remove node", message: "Are you sure?", delegate: self, cancelButtonTitle: "Cancel", otherButtonTitles: "Remove")
        removeAlertView.alertViewStyle = UIAlertViewStyle.Default
        removeAlertView.tag = 1
        removeAlertView.show()
        
    }
    
    @IBAction func touchUpInsideCancelDownload(sender: AnyObject) {
        megaapi.cancelTransfer(currentTransfer)
    }
    
    // MARK: - AlertView delegate
    
    func alertView(alertView: UIAlertView, didDismissWithButtonIndex buttonIndex: Int) {
        if alertView.tag == 0 {
            if buttonIndex == 1 {
                if node.name.pathExtension == "" {
                    megaapi.renameNode(node, newName: alertView.textFieldAtIndex(0)?.text)
                } else {
                    let newName = alertView.textFieldAtIndex(0)?.text.stringByAppendingString(".".stringByAppendingString(node.name.pathExtension))
                    nameLabel.text = newName
                    megaapi.renameNode(node, newName: newName)
                }
            }
        }
        if alertView.tag == 1 {
            if buttonIndex == 1 {
                megaapi.removeNode(node)
                navigationController?.popViewControllerAnimated(true)
            }
        }
    }
    
    // MARK: - MEGA Request delegate
    
    func onRequestStart(api: MEGASdk!, request: MEGARequest!) {
        if request.type == MEGARequestType.Export {
            SVProgressHUD.showWithStatus("Generate link...")
        }
    }
    
    func onRequestFinish(api: MEGASdk!, request: MEGARequest!, error: MEGAError!) {
        if error.type != MEGAErrorType.ApiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.GetAttrFile:
            if request.nodeHandle == node.handle {
                let node = megaapi.nodeForHandle(request.nodeHandle)
                let thumbnailFilePath = Helper.pathForNode(node, path: NSSearchPathDirectory.CachesDirectory, directory: "thumbs")
                let fileExists = NSFileManager.defaultManager().fileExistsAtPath(thumbnailFilePath)
                
                if fileExists {
                    thumbnailImageView.image = UIImage(contentsOfFile: thumbnailFilePath)
                }
            }
            
        case MEGARequestType.Export:
            SVProgressHUD.showSuccessWithStatus("Link Generate")
            SVProgressHUD.dismiss()
            let items = [request.link]
            let activity : UIActivityViewController = UIActivityViewController(activityItems: items, applicationActivities: nil)
            activity.excludedActivityTypes = [UIActivityTypePrint, UIActivityTypeCopyToPasteboard, UIActivityTypeAssignToContact, UIActivityTypeSaveToCameraRoll]
            self.presentViewController(activity, animated: true, completion: nil)
            
        default:
            break
        }
    }
    
    // MARK: - MEGA Global delegate
    
    func onNodesUpdate(api: MEGASdk!, nodeList: MEGANodeList!) {
        node = nodeList.nodeAtIndex(0)
    }
    
    // MARK: - MEGA Transfer delegate
    
    func onTransferStart(api: MEGASdk!, transfer: MEGATransfer!) {
        downloadProgressView.hidden = false
        downloadProgressView.setProgress(0.0, animated: true)
        cancelButton.hidden = false
        currentTransfer = transfer
    }
    
    func onTransferUpdate(api: MEGASdk!, transfer: MEGATransfer!) {
        if transfer.nodeHandle == node.handle {
            let progress = transfer.transferredBytes.floatValue / transfer.totalBytes.floatValue
            downloadProgressView.setProgress(progress, animated: true)
        } else {
            downloadProgressView.setProgress(0.0, animated: true)
        }
    }
    
    func onTransferFinish(api: MEGASdk!, transfer: MEGATransfer!, error: MEGAError!) {
        downloadProgressView.hidden = true
        cancelButton.hidden = true
        
        if error.type == MEGAErrorType.ApiEIncomplete {
            downloadProgressView.setProgress(0.0, animated: true)
        } else {
            downloadProgressView.setProgress(1.0, animated: true)
            saveLabel.hidden = false
            downloadButton.setImage(UIImage(named: "savedFile"), forState: UIControlState.Normal)
            saveLabel.text = "Saved for offline"
        }
    }
}
