using MegaApp.Resources;

namespace MegaApp
{
    /// <summary>
    /// Provides access to string resources.
    /// </summary>
    public class LocalizedStrings
    {
        private static AppResources _localizedResources = new AppResources();
        private static UiResources _localizedUiResources = new UiResources();

        public AppResources LocalizedResources { get { return _localizedResources; } }
        public UiResources LocalizedUiResources { get { return _localizedUiResources; } }
    }
}