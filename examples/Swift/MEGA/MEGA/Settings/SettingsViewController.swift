/**
* @file SettingsViewController.swift
* @brief View controller that show the settings of the user
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

class SettingsViewController: UIViewController, MEGARequestDelegate {
    
    @IBOutlet weak var avatarImageView: UIImageView!
    @IBOutlet weak var emailLabel: UILabel!
    @IBOutlet weak var spaceUsedProgressView: UIProgressView!
    @IBOutlet weak var spaceUsedLabel: UILabel!
    @IBOutlet weak var accountTypeLabel: UILabel!
    
    let megaapi : MEGASdk! = (UIApplication.sharedApplication().delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        emailLabel.text = megaapi.myEmail
        setUserAvatar()
        megaapi.getAccountDetailsWithDelegate(self)
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func setUserAvatar() {
        let user = megaapi.contactForEmail(megaapi.myEmail)
        let avatarFilePath = Helper.pathForUser(user, path: NSSearchPathDirectory.CachesDirectory, directory: "thumbs")
        let fileExists = NSFileManager.defaultManager().fileExistsAtPath(avatarFilePath)
        
        if !fileExists {
            megaapi.getAvatarUser(user, destinationFilePath: avatarFilePath, delegate: self)
        } else {
            avatarImageView.image = UIImage(named: avatarFilePath)
            avatarImageView.layer.cornerRadius = avatarImageView.frame.size.width / 2
            avatarImageView.layer.masksToBounds = true
        }
    }
    
    // MARK: - IBAction
    
    @IBAction func logout(sender: UIBarButtonItem) {
        megaapi.logoutWithDelegate(self)
    }
    
    
    // MARK: - MEGA Request delegate
    
    func onRequestStart(api: MEGASdk!, request: MEGARequest!) {
        if request.type == MEGARequestType.Logout {
            SVProgressHUD.showWithStatus("Logout")
        }
    }
    
    func onRequestFinish(api: MEGASdk!, request: MEGARequest!, error: MEGAError!) {
        if error.type != MEGAErrorType.ApiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.Logout:
            SSKeychain.deletePasswordForService("MEGA", account: "session")
            
            let thumbsURL = NSFileManager.defaultManager().URLsForDirectory(.CachesDirectory, inDomains: .UserDomainMask)[0]
            let thumbsDirectory = thumbsURL.URLByAppendingPathComponent("thumbs")
            
            var error : NSError?
            var success: Bool
            do {
                try NSFileManager.defaultManager().removeItemAtPath(thumbsDirectory.path!)
                success = true
            } catch let error1 as NSError {
                error = error1
                success = false
            }
            if (!success || error != nil) {
                print("(Cache) Remove file error: \(error)")
            }
            
            let documentDirectory : String = NSSearchPathForDirectoriesInDomains(NSSearchPathDirectory.DocumentDirectory, NSSearchPathDomainMask.UserDomainMask, true)[0] 
            do {
                try NSFileManager.defaultManager().removeItemAtPath(documentDirectory)
                success = true
            } catch let error1 as NSError {
                error = error1
                success = false
            }
            if (!success || error != nil) {
                print("(Document) Remove file error: \(error)")
            }
            
            SVProgressHUD.dismiss()
            let storyboard = UIStoryboard(name: "Main", bundle: nil)
            let lvc = storyboard.instantiateViewControllerWithIdentifier("LoginViewControllerID") as! LoginViewController
            presentViewController(lvc, animated: true, completion: nil)
            
        case MEGARequestType.GetAttrUser:
            setUserAvatar()
            
        case MEGARequestType.AccountDetails:
            spaceUsedLabel.text = "\(NSByteCountFormatter.stringFromByteCount(request.megaAccountDetails.storageUsed.longLongValue, countStyle: NSByteCountFormatterCountStyle.Memory)) of \(NSByteCountFormatter.stringFromByteCount(request.megaAccountDetails.storageMax.longLongValue, countStyle: NSByteCountFormatterCountStyle.Memory))"
            let progress = request.megaAccountDetails.storageUsed.floatValue / request.megaAccountDetails.storageMax.floatValue
            spaceUsedProgressView.setProgress(progress, animated: true)
            
            switch request.megaAccountDetails.type {
            case MEGAAccountType.Free:
                accountTypeLabel.text = "Account Type: FREE"
                
            case MEGAAccountType.ProI:
                accountTypeLabel.text = "Account Type: PRO I"
                
            case MEGAAccountType.ProII:
                accountTypeLabel.text = "Account Type: PRO II"
                
            case MEGAAccountType.ProIII:
                accountTypeLabel.text = "Account Type: PRO III"
                
            default:
                break
            }
            
        default:
            break
            
        }
    }
}
