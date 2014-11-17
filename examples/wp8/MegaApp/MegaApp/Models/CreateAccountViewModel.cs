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
                        this._megaSdk.createAccount(Email, Password, Name, this);
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
