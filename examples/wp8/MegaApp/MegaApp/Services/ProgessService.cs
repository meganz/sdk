using Microsoft.Phone.Shell;

namespace MegaApp.Services
{
    public static class ProgessService
    {
        public static void SetProgressIndicator(bool isVisible, string message = null)
        {
            if (SystemTray.ProgressIndicator == null)
                SystemTray.ProgressIndicator = new ProgressIndicator();
          
            SystemTray.ProgressIndicator.Text = message;
            SystemTray.ProgressIndicator.IsIndeterminate = isVisible;
            SystemTray.ProgressIndicator.IsVisible = isVisible;
            
            //SystemTray.IsVisible = isVisible;
        }
    }
}
