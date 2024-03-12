/**
* @file AppDelegate.swift
* @brief The AppDelegate of the app
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

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate, MEGARequestDelegate {
    
    var window: UIWindow?
    let megaapi = MEGASdk(appKey: "", userAgent: nil, basePath: FileManager.default.temporaryDirectory.path)!
    
    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        MEGASdk.setLogLevel(.max)
        MEGASdk.setLogToConsole(true)
        
        
        let storyboard = UIStoryboard(name: "Main", bundle: nil)
        if (SSKeychain.password(forService: "MEGA", account: "session") != nil) {
            megaapi.fastLogin(withSession: SSKeychain.password(forService: "MEGA", account: "session"), delegate: self)

            let tabBarC = storyboard.instantiateViewController(withIdentifier: "TabBarControllerID") as! UITabBarController
            window?.rootViewController = tabBarC
            
        } else {
            let loginVC = storyboard.instantiateViewController(withIdentifier: "LoginViewControllerID") 
            window?.rootViewController = loginVC
        }
        
        return true
    }
    
    func applicationWillResignActive(_ application: UIApplication) {
        // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
        // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
    }
    
    func applicationDidEnterBackground(_ application: UIApplication) {
        // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
        // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
    }
    
    func applicationWillEnterForeground(_ application: UIApplication) {
        // Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
    }
    
    func applicationDidBecomeActive(_ application: UIApplication) {
        // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
    }
    
    func applicationWillTerminate(_ application: UIApplication) {
        // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
    }
    
    // MARK: - MEGA Request delegate
    
    func onRequestStart(_ api: MEGASdk, request: MEGARequest) {
        if request.type == MEGARequestType.MEGARequestTypeFetchNodes {
            SVProgressHUD.show(withStatus: "Updating nodes...")
        }
    }
    
    func onRequestFinish(_ api: MEGASdk, request: MEGARequest, error: MEGAError) {
        if error.type != MEGAErrorType.apiOk {
            return
        }
        
        if request.type == MEGARequestType.MEGARequestTypeLogin {
            megaapi.fetchNodes(with: self)
        }
    }
    
}
