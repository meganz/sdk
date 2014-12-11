/**
 * @file MainPage.xaml.cs
 * @brief Associated class to the main page.
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
using System.Net;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Navigation;
using mega;
using MegaApp.Classes;
using MegaApp.MegaApi;
using MegaApp.Models;
using MegaApp.Resources;
using MegaApp.Services;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Shell;

namespace MegaApp.Pages
{
    public partial class MainPage : PhoneApplicationPage
    {
        public MainPage()
        {
            this.DataContext = App.CloudDrive;
            InitializeComponent();
        }
        
        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            NavigationParameter navParam = NavigateService.ProcessQueryString(NavigationContext.QueryString);

            if (e.NavigationMode == NavigationMode.Back)
            {
                App.CloudDrive.GoFolderUp();
                navParam = NavigationParameter.Browsing;
            }

            switch (navParam)
            {
                case NavigationParameter.Browsing:
                {
                    App.CloudDrive.LoadNodes();
                    break;
                }
                case NavigationParameter.Login:
                {
                    // Remove the login page from the stack. If user presses back button it will then exit the application
                    NavigationService.RemoveBackEntry();
                    
                    App.CloudDrive.FetchNodes();
                    break;
                }
                case NavigationParameter.Unknown:
                {
                    if (!SettingsService.LoadSetting<bool>(SettingsResources.RememberMe))
                    {
                        NavigateService.NavigateTo(typeof(LoginPage), NavigationParameter.Normal);
                        return;
                    }
                    else
                    {
                        App.MegaSdk.fastLogin(SettingsService.LoadSetting<string>(SettingsResources.UserMegaSession), new FastLoginRequestListener());
                        App.CloudDrive.FetchNodes();
                    }
                    break;
                }
            }

            base.OnNavigatedTo(e);
        }

        protected override void OnBackKeyPress(CancelEventArgs e)
        {
            if (!NavigationService.CanGoBack)
            {
                if (App.MegaSdk.getParentNode(App.CloudDrive.CurrentRootNode.GetBaseNode()) != null)
                {
                    App.CloudDrive.GoFolderUp();
                    App.CloudDrive.LoadNodes();
                    e.Cancel = true;
                }
            }
            base.OnBackKeyPress(e);
        }

        private void OnItemTap(object sender, SelectionChangedEventArgs e)
        {
            ListBoxItem selectedItem = this.LstNodes.ItemContainerGenerator.ContainerFromItem(this.LstNodes.SelectedItem) as ListBoxItem;
            if (selectedItem == null) return;
            
            NodeViewModel node = selectedItem.DataContext as NodeViewModel;
            if (node == null) return;
            
            App.CloudDrive.OnNodeTap(node);
        }

        private void OnLogoutClick(object sender, EventArgs e)
        {
            App.MegaSdk.logout(new LogOutRequestListener());
        }        
    }
}