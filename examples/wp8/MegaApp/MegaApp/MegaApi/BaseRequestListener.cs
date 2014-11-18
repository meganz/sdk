using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using mega;
using MegaApp.Classes;
using MegaApp.Services;

namespace MegaApp.MegaApi
{
    abstract class BaseRequestListener: MRequestListenerInterface
    {
        #region Properties

        abstract protected string ProgressMessage { get; }
        abstract protected string ErrorMessage { get; }
        abstract protected string ErrorMessageTitle { get; }
        abstract protected string SuccessMessage { get; }
        abstract protected string SuccessMessageTitle { get; }
        abstract protected bool ShowSuccesMessage { get; }
        abstract protected bool NavigateOnSucces { get; }
        abstract protected bool ActionOnSucces { get; }
        abstract protected Action SuccesAction { get; }
        abstract protected Type NavigateToPage { get; }
        abstract protected NavigationParameter NavigationParameter { get; }

        #endregion

        #region MRequestListenerInterface

        public virtual void onRequestFinish(MegaSDK api, MRequest request, MError e)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() =>
            {
                ProgessService.SetProgressIndicator(false);

                //this.ControlState = true;

                if (e.getErrorCode() == MErrorType.API_OK)
                {
                    if (ShowSuccesMessage)
                        MessageBox.Show(SuccessMessage, SuccessMessageTitle, MessageBoxButton.OK);

                    if (ActionOnSucces)
                        OnSuccesAction(request);

                    if (NavigateOnSucces)
                        NavigateService.NavigateTo(NavigateToPage, NavigationParameter);
                }
                else
                    MessageBox.Show(String.Format(ErrorMessage, e.getErrorString()), ErrorMessageTitle, MessageBoxButton.OK);
            });
        }

        public virtual void onRequestStart(MegaSDK api, MRequest request)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() =>
            {
                //this.ControlState = false;
                ProgessService.SetProgressIndicator(true, ProgressMessage);
            });
        }

        public virtual void onRequestTemporaryError(MegaSDK api, MRequest request, MError e)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() =>
            {
                ProgessService.SetProgressIndicator(false);
                MessageBox.Show(String.Format(ErrorMessage, e.getErrorString()), ErrorMessageTitle, MessageBoxButton.OK);
            });
        }

        public virtual void onRequestUpdate(MegaSDK api, MRequest request)
        {
            // No update status necessary
        }

        #endregion

        #region Virtual Methods

        protected virtual void OnSuccesAction(MRequest request)
        {
            // No standard succes action
        }

        #endregion
    }
}
