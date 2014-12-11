/**
 * @file CloudDriveViewModel.cs
 * @brief Class for the cloud drive view model.
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
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using mega;
using MegaApp.Classes;
using MegaApp.MegaApi;
using MegaApp.Pages;
using MegaApp.Resources;
using MegaApp.Services;

namespace MegaApp.Models
{
    public class CloudDriveViewModel : BaseViewModel
    {
        private readonly MegaSDK _megaSdk;

        public CloudDriveViewModel(MegaSDK megaSdk)
        {
            this._megaSdk = megaSdk;
            this.CurrentRootNode = null;
            this.ChildNodes = new ObservableCollection<NodeViewModel>();            
        }

        #region Commands

        public ICommand RemoveItemCommand { get; set; }
        public ICommand MoveItemCommand { get; set; }
        public ICommand GetPreviewLinkItemCommand { get; set; }
        public ICommand DownloadItemCommand { get; set; }

        #endregion

        #region Public Methods
        
        public void GoFolderUp()
        {
            MNode parentNode = this._megaSdk.getParentNode(this.CurrentRootNode.GetBaseNode());

            if (parentNode == null || parentNode.getType() == MNodeType.TYPE_UNKNOWN )
                parentNode = this._megaSdk.getRootNode();
            
            this.CurrentRootNode = new NodeViewModel(App.MegaSdk, parentNode);
        }

        public void LoadNodes()
        {
            this.ChildNodes.Clear();

            MNodeList nodeList = this._megaSdk.getChildren(this.CurrentRootNode.GetBaseNode());

            for (int i = 0; i < nodeList.size(); i++)
            {
                ChildNodes.Add(new NodeViewModel(this._megaSdk, nodeList.get(i)));
            }
        }

        public void OnNodeTap(NodeViewModel node)
        {
            switch (node.Type)
            {
                case MNodeType.TYPE_FOLDER:
                    {
                        SelectFolder(node);
                        break;
                    }
            }
        }
        
        public void FetchNodes()
        {
            this.ChildNodes.Clear();

            var fetchNodesRequestListener = new FetchNodesRequestListener(this);
            this._megaSdk.fetchNodes(fetchNodesRequestListener);
        }

        public void SelectFolder(NodeViewModel selectedNode)
        {
            this.CurrentRootNode = selectedNode;
            // Create unique uri string to navigate
            NavigateService.NavigateTo(typeof(MainPage), NavigationParameter.Browsing, new Dictionary<string, string> {{"Id", Guid.NewGuid().ToString("N")}});
        }

        #endregion

        #region Private Methods

        private bool IsUserOnline()
        {
            return Convert.ToBoolean(this._megaSdk.isLoggedIn());
        }

        #endregion

        #region Properties

        public ObservableCollection<NodeViewModel> ChildNodes { get; set; }

        public NodeViewModel CurrentRootNode { get; set; }

        public NodeViewModel FocusedNode { get; set; }
        
        #endregion
      
    }
}
