/**
 * @file SettingsService.cs
 * @brief Class for the settings service.
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
using System.IO.IsolatedStorage;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using MegaApp.Resources;

namespace MegaApp.Services
{
    static class SettingsService
    {
        public static void SaveSetting<T>(string key, T value)
        {
            var settings = IsolatedStorageSettings.ApplicationSettings;

            settings.Add(key, value);

            settings.Save();
        }

        public static T LoadSetting<T>(string key)
        {
            var settings = IsolatedStorageSettings.ApplicationSettings;

            if (settings.Contains(key))
                return (T) settings[key];
            else
                return default(T);
        }

        public static void DeleteSetting(string key)
        {
            var settings = IsolatedStorageSettings.ApplicationSettings;

            if (!settings.Contains(key)) return;

            settings.Remove(key);
            settings.Save();
        }

        public static void SaveMegaLoginData(string email, string session)
        {
            SettingsService.SaveSetting(SettingsResources.RememberMe, true);
            SettingsService.SaveSetting(SettingsResources.UserMegaEmailAddress, email);
            SettingsService.SaveSetting(SettingsResources.UserMegaSession, session);
        }

        public static void ClearMegaLoginData()
        {
            SettingsService.DeleteSetting(SettingsResources.RememberMe);
            SettingsService.DeleteSetting(SettingsResources.UserMegaEmailAddress);
            SettingsService.DeleteSetting(SettingsResources.UserMegaSession);
        }
    }
}
