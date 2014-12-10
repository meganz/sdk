/**
 * DelegateMegaRequestListener.java
 * Delegation pattern for MegaRequestListener
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

class DelegateMegaRequestListener extends MegaRequestListener {

	MegaApiJava megaApi;
	MegaRequestListenerInterface listener;
	
	DelegateMegaRequestListener(MegaApiJava megaApi, MegaRequestListenerInterface listener)
	{
		this.megaApi = megaApi;
		this.listener = listener;
	}
	
	MegaRequestListenerInterface getUserListener()
	{
		return listener;
	}
	
	@Override
	public void onRequestStart(MegaApi api, MegaRequest request) 
	{
		if(listener != null)
		{
			final MegaRequest megaRequest = request.copy();
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
					listener.onRequestStart(megaApi, megaRequest);
			    }
			});
		}
	}
	
	@Override
	public void onRequestUpdate(MegaApi api, MegaRequest request) 
	{
		if(listener != null)
		{
			final MegaRequest megaRequest = request.copy();
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
					listener.onRequestUpdate(megaApi, megaRequest);
			    }
			});
		}
	}

	@Override
	public void onRequestFinish(MegaApi api, MegaRequest request, MegaError e) 
	{
		if(listener != null)
		{
			final MegaRequest megaRequest = request.copy();
			final MegaError megaError = e.copy();			
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
			    	listener.onRequestFinish(megaApi, megaRequest, megaError);
			    }
			});
		}
		megaApi.privateFreeRequestListener(this);
	}

	@Override
	public void onRequestTemporaryError(MegaApi api, MegaRequest request, MegaError e) 
	{
		if(listener != null)
		{
			final MegaRequest megaRequest = request.copy();
			final MegaError megaError = e.copy();
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
			    	listener.onRequestTemporaryError(megaApi, megaRequest, megaError);
			    }
			});
		}
	}
}
