/**
 * @file FetchNodesRequestListener.cs
 * @brief Class for the request listener for the fetch nodes process.
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
