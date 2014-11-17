using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using mega;
using MegaApp.Classes;
using MegaApp.Extensions;
using MegaApp.Models;
using MegaApp.Resources;
using MegaApp.Services;

namespace MegaApp.MegaApi
{
    class CreateFolderRequestListener: BaseRequestListener
    {
        private readonly CloudDriveViewModel _cloudDriveViewModel;
        public CreateFolderRequestListener(CloudDriveViewModel cloudDriveViewModel)
        {
            this._cloudDriveViewModel = cloudDriveViewModel;
        }

        #region Base Properties

        protected override string ProgressMessage
        {
            get { return ProgressMessages.CreateFolder; }
        }

        protected override string ErrorMessage
        {
            get { return AppMessages.CreateFolderFailed; }
        }

        protected override string ErrorMessageTitle
        {
            get { return AppMessages.CreateFolderFailed_Title; }
        }

        protected override string SuccessMessage
        {
            get { return AppMessages.CreateFolderSuccess; }
        }

        protected override string SuccessMessageTitle
        {
            get { return AppMessages.CreateFolderSuccess_Title; }
        }

        protected override bool ShowSuccesMessage
        {
            get { return true; }
        }

        protected override bool NavigateOnSucces
        {
            get { return false; }
        }

        protected override bool ActionOnSucces
        {
            get { return true; }
        }

        protected override Action SuccesAction
        {
            get { return () => _cloudDriveViewModel.LoadNodes(); }
        }

        protected override Type NavigateToPage
        {
            get { throw new NotImplementedException(); }
        }

        protected override NavigationParameter NavigationParameter
        {
            get { throw new NotImplementedException(); }
        }

        #endregion
    }
}
