using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using mega;
using MegaApp.Extensions;
using MegaApp.Models;
using MegaApp.Services;

namespace MegaApp.MegaApi
{
    class FetchNodesRequestListener: MRequestListenerInterface
    {
        private readonly CloudDriveViewModel _cloudDriveViewModel;
        public FetchNodesRequestListener(CloudDriveViewModel cloudDriveViewModel)
        {
            this._cloudDriveViewModel = cloudDriveViewModel;
        }

        #region MRequestListenerInterface

        public void onRequestFinish(MegaSDK api, MRequest request, MError e)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() =>
            {
                if (e.getErrorCode() == MErrorType.API_OK)
                {
                    _cloudDriveViewModel.CurrentRootNode = new NodeViewModel(api, api.getRootNode());
                   _cloudDriveViewModel.LoadNodes();
                }
                else
                {
                    MessageBox.Show(e.getErrorString());
                }

                ProgessService.SetProgressIndicator(false);
            });

        }

        public void onRequestStart(MegaSDK api, MRequest request)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() => ProgessService.SetProgressIndicator(true,
                String.Format("Fetching files & folders...[{0}/{1}]",
                request.getTransferredBytes().ToStringAndSuffix(),
                request.getTotalBytes().ToStringAndSuffix())));
        }

        public void onRequestTemporaryError(MegaSDK api, MRequest request, MError e)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() => MessageBox.Show(e.getErrorString()));
        }

        public void onRequestUpdate(MegaSDK api, MRequest request)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() => ProgessService.SetProgressIndicator(true, 
                String.Format("Fetching files & folders...[{0}/{1}]", 
                request.getTransferredBytes().ToStringAndSuffix(), 
                request.getTotalBytes().ToStringAndSuffix())));
           
        }

        #endregion
    }
}
