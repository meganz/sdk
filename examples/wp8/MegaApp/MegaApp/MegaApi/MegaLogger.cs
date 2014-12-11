/**
 * @file MegaLogger.cs
 * @brief Class to show debug log messages.
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

//#define DEBUG
//Uncomment this line to show debug logs even on Release builds
//Only debug levels FATAL, ERROR, WARNING and INFO can be shown in Release builds
//DEBUG and MAX are reserved for Debug builds

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Windows;
using mega;

namespace MegaApp.MegaApi
{
    class MegaLogger : MLoggerInterface
    {
        public virtual void log(string time, int loglevel, string source, string message)
        {
            String logLevelString;
            switch((MLogLevel)loglevel)
            {
                case MLogLevel.LOG_LEVEL_DEBUG:
                    logLevelString = " (debug): ";
                    break;
                case MLogLevel.LOG_LEVEL_ERROR:
                    logLevelString = " (error): ";
                    break;
                case MLogLevel.LOG_LEVEL_FATAL:
                    logLevelString = " (fatal): ";
                    break;
                case MLogLevel.LOG_LEVEL_INFO:
                    logLevelString = " (info):  ";
                    break;
                case MLogLevel.LOG_LEVEL_MAX:
                    logLevelString = " (verb):  ";
                    break;
                case MLogLevel.LOG_LEVEL_WARNING:
                    logLevelString =  " (warn):  ";
                    break;
                default:
                    logLevelString = " (none):  ";
                    break;
            }

            if(!string.IsNullOrEmpty(source))
            {
                int index = source.LastIndexOf('\\');
                if(index >=0 && source.Length > (index + 1))
                {
                   source = source.Substring(index + 1);
                }
                message += " (" + source + ")"; 
            }

            Debug.WriteLine("{0}{1}{2}", time, logLevelString, message);
        }
    }
}
