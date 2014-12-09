package nz.mega.android.bindingsample;

import nz.mega.sdk.MegaApiAndroid;

import android.app.Application;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.util.Log;

public class DemoAndroidApplication extends Application{

	MegaApiAndroid megaApi;
	static final String APP_KEY = "l4cmkI7B";
	static final String USER_AGENT = "MEGA Androd Simple Demo SDK";
	
	@Override
	public void onCreate() {
		super.onCreate();
		
		MegaApiAndroid.setLoggerObject(new AndroidLogger());
		MegaApiAndroid.setLogLevel(MegaApiAndroid.LOG_LEVEL_MAX);
	}
	
	public MegaApiAndroid getMegaApi()
	{
		if(megaApi == null)
		{
			PackageManager m = getPackageManager();
			String s = getPackageName();
			PackageInfo p;
			String path = null;
			try
			{
				p = m.getPackageInfo(s, 0);
				path = p.applicationInfo.dataDir + "/";
			}
			catch (NameNotFoundException e)
			{
				e.printStackTrace();
			}
			
			megaApi = new MegaApiAndroid(DemoAndroidApplication.APP_KEY, DemoAndroidApplication.USER_AGENT, path);
		}
		
		return megaApi;
	}
	
}
