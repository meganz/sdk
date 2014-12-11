/**
 * @file LocalizedStrings.cs
 * @brief Class for search localized strings into the application resources.
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