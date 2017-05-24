/**
* @file MainTabBarController.swift
* @brief Main tab bar of the app
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

class MainTabBarViewController: UITabBarController {
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        let viewControllerArray : NSMutableArray = NSMutableArray(capacity: 5)
        
        viewControllerArray.add(UIStoryboard(name: "Cloud", bundle: nil).instantiateInitialViewController()!)
        viewControllerArray.add(UIStoryboard(name: "Offline", bundle: nil).instantiateInitialViewController()!)
        viewControllerArray.add(UIStoryboard(name: "Contacts", bundle: nil).instantiateInitialViewController()!)
        viewControllerArray.add(UIStoryboard(name: "Settings", bundle: nil).instantiateInitialViewController()!)
        
        let viewControllers = viewControllerArray.copy()
        self.setViewControllers(viewControllers as? [UIViewController], animated: false)
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
}
