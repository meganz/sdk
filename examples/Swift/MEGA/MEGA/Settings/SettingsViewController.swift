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
import MEGASdk

class SettingsViewController: UIViewController, MEGARequestDelegate {
    
    @IBOutlet weak var avatarImageView: UIImageView!
    @IBOutlet weak var emailLabel: UILabel!
    @IBOutlet weak var spaceUsedProgressView: UIProgressView!
    @IBOutlet weak var spaceUsedLabel: UILabel!
    @IBOutlet weak var accountTypeLabel: UILabel!
    
    let megaapi = (UIApplication.shared.delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        emailLabel.text = megaapi.myEmail
        setUserAvatar()
        megaapi.getAccountDetails(with: self)
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func setUserAvatar() {
        guard let user = megaapi.myUser else { return }
        let avatarFilePath = Helper.pathForUser(user, path: FileManager.SearchPathDirectory.cachesDirectory, directory: "thumbs")
        let fileExists = FileManager.default.fileExists(atPath: avatarFilePath)
        
        if !fileExists {
            megaapi.getAvatarUser(user, destinationFilePath: avatarFilePath, delegate: self)
        } else {
            avatarImageView.image = UIImage(named: avatarFilePath)
            avatarImageView.layer.cornerRadius = avatarImageView.frame.size.width / 2
            avatarImageView.layer.masksToBounds = true
        }
    }
    
    // MARK: - IBAction
    
    @IBAction func logout(_ sender: UIBarButtonItem) {
        megaapi.logout(with: self)
    }
    
    
    // MARK: - MEGA Request delegate
    
    func onRequestStart(_ api: MEGASdk, request: MEGARequest) {
        if request.type == MEGARequestType.MEGARequestTypeLogout {
            SVProgressHUD.show(withStatus: "Logout")
        }
    }
    
    func onRequestFinish(_ api: MEGASdk, request: MEGARequest, error: MEGAError) {
        if error.type != MEGAErrorType.apiOk {
            return
        }
        
        switch request.type {
        case MEGARequestType.MEGARequestTypeLogout:
            SSKeychain.deletePassword(forService: "MEGA", account: "session")
            
            let thumbsURL = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            let thumbsDirectory = thumbsURL.appendingPathComponent("thumbs")
            
            var error : NSError?
            var success: Bool
            do {
                try FileManager.default.removeItem(atPath: thumbsDirectory.path)
                success = true
            } catch let error1 as NSError {
                error = error1
                success = false
            }
            if (!success || error != nil) {
                print("(Cache) Remove file error: \(error!)")
            }
            
            let documentDirectory : String = NSSearchPathForDirectoriesInDomains(FileManager.SearchPathDirectory.documentDirectory, FileManager.SearchPathDomainMask.userDomainMask, true)[0] 
            do {
                try FileManager.default.removeItem(atPath: documentDirectory)
                success = true
            } catch let error1 as NSError {
                error = error1
                success = false
            }
            if (!success || error != nil) {
                print("(Document) Remove file error: \(error!)")
            }
            
            SVProgressHUD.dismiss()
            let storyboard = UIStoryboard(name: "Main", bundle: nil)
            let lvc = storyboard.instantiateViewController(withIdentifier: "LoginViewControllerID") as! LoginViewController
            present(lvc, animated: true, completion: nil)
            
        case MEGARequestType.MEGARequestTypeGetAttrUser:
            setUserAvatar()
            
        case MEGARequestType.MEGARequestTypeAccountDetails:
            guard let megaAccountDetails = request.megaAccountDetails else { return }
            spaceUsedLabel.text = "\(ByteCountFormatter.string(fromByteCount: megaAccountDetails.storageUsed, countStyle: ByteCountFormatter.CountStyle.memory)) of \(ByteCountFormatter.string(fromByteCount: megaAccountDetails.storageMax, countStyle: ByteCountFormatter.CountStyle.memory))"
            let progress = Float(megaAccountDetails.storageUsed / megaAccountDetails.storageMax)
            spaceUsedProgressView.setProgress(progress, animated: true)
            
            switch megaAccountDetails.type {
            case MEGAAccountType.free:
                accountTypeLabel.text = "Account Type: FREE"
                
            case MEGAAccountType.proI:
                accountTypeLabel.text = "Account Type: PRO I"
                
            case MEGAAccountType.proII:
                accountTypeLabel.text = "Account Type: PRO II"
                
            case MEGAAccountType.proIII:
                accountTypeLabel.text = "Account Type: PRO III"
                
            default:
                break
            }
            
        default:
            break
            
        }
    }
}
