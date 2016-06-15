/**
 * @file CreateAccountViewModel.cs
 * @brief Class for the create account view model.
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

using mega;
using MegaApp.Classes;
using MegaApp.Pages;
using MegaApp.Resources;
using Microsoft.Phone.Tasks;
using System;
using System.Windows;
using System.Windows.Input;

namespace MegaApp.Models
{
    class CreateAccountViewModel : BaseRequestListenerViewModel 
    {
        private readonly MegaSDK _megaSdk;

        public CreateAccountViewModel(MegaSDK megaSdk)
        {
            this._megaSdk = megaSdk;
            this.ControlState = true;
            this.CreateAccountCommand = new DelegateCommand(this.CreateAccount);
            this.NavigateTermsOfUseCommand = new DelegateCommand(NavigateTermsOfUse);
        }

        #region Methods

        private void CreateAccount(object obj)
        {
            if (CheckInputParameters())
            {
                if (CheckPassword())
                {
                    if (TermOfUse)
                    {
                        this._megaSdk.createAccount(Email, Password, Name, null, this);
                    }
                    else
                        MessageBox.Show(AppMessages.AgreeTermsOfUse, AppMessages.AgreeTermsOfUse_Title,
                            MessageBoxButton.OK);
                }
                else
                {
                    MessageBox.Show(AppMessages.PasswordsDoNotMatch, AppMessages.PasswordsDoNotMatch_Title,
                        MessageBoxButton.OK);
                }
            }
            else
            {
                MessageBox.Show(AppMessages.RequiredFieldsCreateAccount, AppMessages.RequiredFields_Title,
                        MessageBoxButton.OK);
            }
            
        }
        private static void NavigateTermsOfUse(object obj)
        {
            var webBrowserTask = new WebBrowserTask {Uri = new Uri(AppResources.TermsOfUseUrl)};
            webBrowserTask.Show();
        }

        private bool CheckInputParameters()
        {
            return !String.IsNullOrEmpty(Email) && !String.IsNullOrEmpty(Password) && !String.IsNullOrEmpty(Name) && !String.IsNullOrEmpty(ConfirmPassword);
        }

        private bool CheckPassword()
        {
            return Password.Equals(ConfirmPassword, StringComparison.InvariantCulture);
        }

        #endregion

        #region Commands

        public ICommand CreateAccountCommand { get; set; }

        public ICommand NavigateTermsOfUseCommand { get; set; }

        #endregion

        #region Properties

        public string Email { get; set; }
        public string Password { get; set; }
        public string ConfirmPassword { get; set; }
        public string Name { get; set; }
        public bool TermOfUse { get; set; }
        
        #endregion

        #region Base Properties

        protected override string ProgressMessage
        {
            get { return ProgressMessages.CreateAccount; }
        }

        protected override string ErrorMessage
        {
            get { return AppMessages.CreateAccountFailed; }
        }

        protected override string ErrorMessageTitle
        {
            get { return AppMessages.CreateAccountFailed_Title; }
        }

        protected override string SuccessMessage
        {
            get { return AppMessages.ConfirmNeeded; }
        }

        protected override string SuccessMessageTitle
        {
            get { return AppMessages.ConfirmNeeded_Title; }
        }

        protected override bool ShowSuccesMessage
        {
            get { return true; }
        }

        protected override bool NavigateOnSucces
        {
            get { return true; }
        }

        protected override bool ActionOnSucces
        {
            get { return false; }
        }

        protected override Action SuccesAction
        {
            get { throw new NotImplementedException(); }
        }

        protected override Type NavigateToPage
        {
            get { return typeof (LoginPage); }
        }

        protected override NavigationParameter NavigationParameter
        {
            get { return NavigationParameter.Normal; }
        }

        #endregion
        
    }
}
