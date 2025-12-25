/**
 * DemoAndroidApplication.kt
 * Application class
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
package nz.mega.android.bindingsample.presentation

import android.app.Application
import android.content.pm.PackageInfo
import android.content.pm.PackageManager.NameNotFoundException
import nz.mega.android.bindingsample.util.AndroidLogger
import nz.mega.sdk.MegaApiAndroid

class DemoAndroidApplication : Application() {

    private var megaApi: MegaApiAndroid? = null

    companion object {
        const val APP_KEY = "l4cmkI7B"
        const val USER_AGENT = "MEGA Androd Simple Demo SDK"
    }

    override fun onCreate() {
        super.onCreate()

        MegaApiAndroid.addLoggerObject(AndroidLogger())
        MegaApiAndroid.setLogLevel(MegaApiAndroid.LOG_LEVEL_MAX)
    }

    fun getMegaApi(): MegaApiAndroid {
        if (megaApi == null) {
            val m = packageManager
            val s = packageName
            var path: String? = null
            try {
                val p: PackageInfo = m.getPackageInfo(s, 0)
                path = p.applicationInfo?.dataDir?.let { "$it/" }
            } catch (e: NameNotFoundException) {
                e.printStackTrace()
            }

            megaApi = MegaApiAndroid(APP_KEY, USER_AGENT, path)
        }

        return megaApi!!
    }
}

