/**
 * @file LoginViewModel.cs
 * @brief Class for the login view model.
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
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Navigation;
using mega;
using MegaApp.Classes;
using MegaApp.Pages;
using MegaApp.Resources;
using MegaApp.Services;
using Microsoft.Devices.Sensors;
using Microsoft.Phone.Controls;

namespace MegaApp.Models
{
    class LoginViewModel : BaseRequestListenerViewModel
    {
        private readonly MegaSDK _megaSdk;

        public LoginViewModel(MegaSDK megaSdk)
        {
            this._megaSdk = megaSdk;
            this.ControlState = true;
            this.LoginCommand = new DelegateCommand(this.DoLogin);
            this.NavigateCreateAccountCommand = new DelegateCommand(NavigateCreateAccount);
        }

        #region Methods

        private void DoLogin(object obj)
        {
            if (CheckInputParameters())
            {
                this._megaSdk.login(Email, Password, this);
            }
            else
            {
                MessageBox.Show(AppMessages.RequiredFieldsLogin, AppMessages.RequiredFields_Title,
                        MessageBoxButton.OK);
            }
        }
        private static void NavigateCreateAccount(object obj)
        {
            NavigateService.NavigateTo(typeof(CreateAccountPage), NavigationParameter.Normal);
        }

        private bool CheckInputParameters()
        {
            return !String.IsNullOrEmpty(Email) && !String.IsNullOrEmpty(Password);
        }

        private static void SaveLoginData(string email, string session)
        {
            SettingsService.SaveMegaLoginData(email, session);
        }
        
        #endregion

        #region Commands

        public ICommand LoginCommand { get; set; }

        public ICommand NavigateCreateAccountCommand { get; set; }

        #endregion

        #region Properties

        public string Email { get; set; }
        public string Password { get; set; }
        public bool RememberMe { get; set; }
        public string SessionKey { get; private set; }

        #endregion

        #region  Base Properties

        protected override string ProgressMessage
        {
            get { return ProgressMessages.Login; }
        }

        protected override string ErrorMessage
        {
            get { return AppMessages.LoginFailed; }
        }

        protected override string ErrorMessageTitle
        {
            get { return AppMessages.LoginFailed_Title; }
        }

        protected override string SuccessMessage
        {
            get { throw new NotImplementedException(); }
        }

        protected override string SuccessMessageTitle
        {
            get { throw new NotImplementedException(); }
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
            get { return RememberMe; }
        }

        protected override Action SuccesAction
        {
            get { return new Action(() => SaveLoginData(Email, SessionKey)); }
        }

        protected override Type NavigateToPage
        {
            get { return typeof(MainPage); }
        }

        protected override NavigationParameter NavigationParameter
        {
            get { return NavigationParameter.Login; }
        }

        #endregion

        #region MRequestListenerInterface

        public override void onRequestFinish(MegaSDK api, MRequest request, MError e)
        {
            if (e.getErrorCode() == MErrorType.API_OK)
                SessionKey = api.dumpSession();

            base.onRequestFinish(api, request, e);
        }

        #endregion
        
    }
}
