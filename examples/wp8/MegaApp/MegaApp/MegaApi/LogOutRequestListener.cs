/**
 * @file LogOutRequestListener.cs
 * @brief Class for the request listener for logout.
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using mega;
using MegaApp.Classes;

using MegaApp.Pages;
using MegaApp.Resources;
using MegaApp.Services;

namespace MegaApp.MegaApi
{
    class LogOutRequestListener: BaseRequestListener
    {
        protected override string ProgressMessage
        {
            get { return ProgressMessages.Logout; }
        }
                
        protected override string ErrorMessage
        {
            get { return AppMessages.LogoutFailed; }
        }

        protected override string ErrorMessageTitle
        {
            get { return AppMessages.LogoutFailed_Title; }
        }
                
        protected override string SuccessMessage
        {
            get { return AppMessages.LoggedOut; }
        }

        protected override string SuccessMessageTitle
        {
            get { return AppMessages.LoggedOut_Title; }
        }

        protected override bool ShowSuccesMessage
        {
            get { return false; }
        }

        protected override bool NavigateOnSucces
        {
            get { return true; }
        }

        protected override bool ActionOnSucces
        {
            get { return true; }
        }

        protected override Action SuccesAction 
        {
            get { throw new NotImplementedException(); }
        }

        protected override Type NavigateToPage
        {
            get { return typeof(LoginPage); }
        }

        protected override NavigationParameter NavigationParameter
        {
            get { return NavigationParameter.Normal; }
        }

        protected override void OnSuccesAction(MRequest request)
        {
            SettingsService.ClearMegaLoginData();
            App.CloudDrive.ChildNodes.Clear();            
        }        
    }
}
