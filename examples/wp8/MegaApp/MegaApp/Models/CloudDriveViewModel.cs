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

            this.RemoveItemCommand = new DelegateCommand(this.RemoveItem);
        }

        #region Commands

        public ICommand RemoveItemCommand { get; set; }
        public ICommand MoveItemCommand { get; set; }
        public ICommand GetPreviewLinkItemCommand { get; set; }
        public ICommand DownloadItemCommand { get; set; }

        #endregion

        #region Public Methods

        private void RemoveItem(object obj)
        {
            if (MessageBox.Show("Are you sure you want to delete this item", "Delete item", MessageBoxButton.OKCancel) ==
                MessageBoxResult.Cancel) return;

            this._megaSdk.moveNode(FocusedNode.GetBaseNode(), this._megaSdk.getRubbishNode(), new RemoveNodeRequestListener(this));
        }

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

        //private NodeViewModel _currentCloudDriveRootNode;
        //public NodeViewModel CurrentCloudDriveRootNode
        //{
        //    get { return _currentCloudDriveRootNode; }
        //    set
        //    {
        //        _currentCloudDriveRootNode = value;
        //        OnPropertyChanged("CurrentCloudDriveRootNode");
        //    }

        //}

        #endregion
      
    }
}
