/**
* @file LoginViewController.swift
* @brief View controller that allow login in your MEGA account
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

class LoginViewController: UIViewController, MEGARequestDelegate {
    
    @IBOutlet weak var emailTextField: UITextField!
    @IBOutlet weak var passwordTextField: UITextField!
    @IBOutlet weak var loginProgressView: UIProgressView!
    @IBOutlet weak var loginButton: UIButton!
    @IBOutlet weak var informationLabel: UILabel!
    
    let megaapi = (UIApplication.shared.delegate as! AppDelegate).megaapi
    
    override func viewDidLoad() {
        super.viewDidLoad()
        emailTextField.becomeFirstResponder()
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    // MARK: - IBActions
    
    @IBAction func touchUpInsideLogin(_ sender: UIButton) {
        if validateForm() {
            passwordTextField.resignFirstResponder()
            emailTextField.resignFirstResponder()
            megaapi.login(withEmail: emailTextField.text ?? "", password: passwordTextField.text ?? "", delegate: self)
        }
    }
    
    // MARK: - Validate methods
    
    func validateForm() -> Bool {
        if !validateEmail(emailTextField.text!) {
            let alertController = UIAlertController(title: "Invalid email", message: "Enter a valid email", preferredStyle: UIAlertController.Style.alert)
            alertController.addAction(UIAlertAction(title: "Dismiss", style: UIAlertAction.Style.default,handler: nil))
            self.present(alertController, animated: true, completion: nil)
            
            emailTextField.becomeFirstResponder()
            
            return false
        } else if !validatePassword(passwordTextField.text!) {
            let alertController = UIAlertController(title: "Invalid password", message: "Enter a valid password", preferredStyle: UIAlertController.Style.alert)
            alertController.addAction(UIAlertAction(title: "Dismiss", style: UIAlertAction.Style.default,handler: nil))
            self.present(alertController, animated: true, completion: nil)
            
            passwordTextField.becomeFirstResponder()
            
            return false
        }
        return true
    }
    
    func validatePassword(_ password : String) -> Bool {
        return (password.isEmpty) ? false : true
    }
    
    func validateEmail(_ email : String) -> Bool {
        let emailRegex = "(?:[a-z0-9!#$%\\&'*+/=?\\^_`{|}~-]+(?:\\.[a-z0-9!#$%\\&'*+/=?\\^_`{|}~-]+)*|\"(?:[\\x01-\\x08\\x0b\\x0c\\x0e-\\x1f\\x21\\x23-\\x5b\\x5d-\\x7f]|\\\\[\\x01-\\x09\\x0b\\x0c\\x0e-\\x7f])*\")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?|[a-z0-9-]*[a-z0-9]:(?:[\\x01-\\x08\\x0b\\x0c\\x0e-\\x1f\\x21-\\x5a\\x53-\\x7f]|\\\\[\\x01-\\x09\\x0b\\x0c\\x0e-\\x7f])+)\\])"
        
        let resultPredicate : NSPredicate = NSPredicate(format: "SELF MATCHES[c] %@", emailRegex)
        
        return resultPredicate.evaluate(with: email)
    }
    
    // MARK: - MEGA Request delegate
    
    func onRequestStart(_ api: MEGASdk, request: MEGARequest) {
        if request.type == MEGARequestType.MEGARequestTypeLogin {
            loginButton.isEnabled = false
            loginProgressView.isHidden = false
        }
        
    }
    
    func onRequestFinish(_ api: MEGASdk, request: MEGARequest, error: MEGAError) {
        if error.type != MEGAErrorType.apiOk {
            loginButton.isEnabled = true
            loginProgressView.isHidden = true
            informationLabel.isHidden = true
            
            switch error.type {
            case MEGAErrorType.apiEArgs:
                let alertController = UIAlertController(title: "Error", message: "Email or password invalid", preferredStyle: UIAlertController.Style.alert)
                alertController.addAction(UIAlertAction(title: "Dismiss", style: UIAlertAction.Style.default,handler: nil))
                self.present(alertController, animated: true, completion: nil)
                
            case MEGAErrorType.apiENoent:
                let alertController = UIAlertController(title: "Error", message: "User does not exist", preferredStyle: UIAlertController.Style.alert)
                alertController.addAction(UIAlertAction(title: "Dismiss", style: UIAlertAction.Style.default,handler: nil))
                self.present(alertController, animated: true, completion: nil)
                
            default:
                break
            }
            
            return
        }
        
        switch request.type {
        case MEGARequestType.MEGARequestTypeLogin:
            let session = megaapi.dumpSession()
            SSKeychain.setPassword(session, forService: "MEGA", account: "session")
            api.fetchNodes(with: self)
            
        case MEGARequestType.MEGARequestTypeFetchNodes:
            self.performSegue(withIdentifier: "showCloudDrive", sender: self)
            
        default:
            break
        }
    }
    
    func onRequestUpdate(_ api: MEGASdk, request: MEGARequest) {
        if request.type == MEGARequestType.MEGARequestTypeFetchNodes {
            let progress = Float(request.transferredBytes / request.totalBytes)
            if progress > 0 && progress < 0.99 {
                informationLabel.text = "Fectching nodes"
                loginProgressView.setProgress(progress, animated: true)
            } else if progress > 0.99 || progress < 0 {
                informationLabel.text = "Preparing nodes"
                loginProgressView.setProgress(1.0, animated: true)
            }
        }
    }
    
}
