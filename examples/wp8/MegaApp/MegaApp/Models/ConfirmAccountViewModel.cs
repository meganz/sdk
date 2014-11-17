using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using mega;
using MegaApp.Classes;
using MegaApp.Pages;
using MegaApp.Resources;
using MegaApp.Services;

namespace MegaApp.Models
{
    class ConfirmAccountViewModel: BaseRequestListenerViewModel
    {
        private readonly MegaSDK _megaSdk;

        public ConfirmAccountViewModel(MegaSDK megaSdk)
        {
            this.ControlState = true;
            this._megaSdk = megaSdk;
            this.ConfirmAccountCommand = new DelegateCommand(this.ConfirmAccount);
        }

        #region Methods

        private void ConfirmAccount(object obj)
        {
            if (String.IsNullOrEmpty(ConfirmCode))
                return;
            else
            {
                if (String.IsNullOrEmpty(Password))
                    MessageBox.Show(AppMessages.RequiredFieldsConfirmAccount, AppMessages.RequiredFields_Title,
                        MessageBoxButton.OK);
                else
                {
                    this._megaSdk.confirmAccount(ConfirmCode, Password, this);
                }
            }
        }

        #endregion

        #region Commands

        public ICommand ConfirmAccountCommand { get; set; }

        #endregion

        #region Properties

        public string ConfirmCode { get; set; }
        public string Password { get; set; }

        #endregion

        #region Base Properties

        protected override string ProgressMessage
        {
            get { return ProgressMessages.ConfirmAccount; }
        }

        protected override string ErrorMessage
        {
            get { return AppMessages.ConfirmAccountFailed; }
        }

        protected override string ErrorMessageTitle
        {
            get { return AppMessages.ConfirmAccountFailed_Title; }
        }

        protected override string SuccessMessage
        {
            get { return AppMessages.ConfirmAccountSucces; }
        }

        protected override string SuccessMessageTitle
        {
            get { return AppMessages.ConfirmAccountSucces_Title; }
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
            get { return typeof(LoginPage); }
        }

        protected override NavigationParameter NavigationParameter
        {
            get { return NavigationParameter.Normal; }
        }

        #endregion
    }
}
