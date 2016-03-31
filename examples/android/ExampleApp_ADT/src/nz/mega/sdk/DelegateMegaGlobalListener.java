/**
 * DelegateMegaGlobalListener.java
 * Delegation pattern for MegaGlobalListener
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
package nz.mega.sdk;

import java.util.ArrayList;

class DelegateMegaGlobalListener extends MegaGlobalListener
{
	MegaApiJava megaApi;
	MegaGlobalListenerInterface listener;
	
	DelegateMegaGlobalListener(MegaApiJava megaApi, MegaGlobalListenerInterface listener)
	{
		this.megaApi = megaApi;
		this.listener = listener;
	}
	
	MegaGlobalListenerInterface getUserListener()
	{
		return listener;
	}
	
	@Override
	public void onUsersUpdate(MegaApi api, MegaUserList userList) 
	{
		if(listener != null)
		{
			final ArrayList<MegaUser> users = MegaApiJava.userListToArray(userList);
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
			    	listener.onUsersUpdate(megaApi, users);
			    }
			});
		}
	}

	@Override
	public void onNodesUpdate(MegaApi api, MegaNodeList nodeList) 
	{
		if(listener != null)
		{
			final ArrayList<MegaNode> nodes = MegaApiJava.nodeListToArray(nodeList);
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
			    	listener.onNodesUpdate(megaApi, nodes);
			    }
			});
		}
	}

	@Override
	public void onReloadNeeded(MegaApi api)
	{
		if(listener != null)
		{
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
			    	listener.onReloadNeeded(megaApi);
			    }
			});
		}
	}
}
