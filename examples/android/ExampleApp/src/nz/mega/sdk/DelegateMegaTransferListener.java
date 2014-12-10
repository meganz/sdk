/**
 * DelegateMegaTransferListener.java
 * Delegation pattern for MegaTransferListener
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

class DelegateMegaTransferListener extends MegaTransferListener
{
	MegaApiJava megaApi;
	MegaTransferListenerInterface listener;
	boolean singleListener;
	
	DelegateMegaTransferListener(MegaApiJava megaApi, MegaTransferListenerInterface listener, boolean singleListener)
	{
		this.megaApi = megaApi;
		this.listener = listener;
		this.singleListener = singleListener;
	}

	MegaTransferListenerInterface getUserListener()
	{
		return listener;
	}
	
	@Override
	public void onTransferStart(MegaApi api, MegaTransfer transfer)
	{
		if(listener != null)
		{
			final MegaTransfer megaTransfer = transfer.copy();
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
					listener.onTransferStart(megaApi, megaTransfer);
			    }
			});
		}
	}
	
	@Override
	public void onTransferFinish(MegaApi api, MegaTransfer transfer, MegaError e)
	{
		if(listener != null)
		{
			final MegaTransfer megaTransfer = transfer.copy();
			final MegaError megaError = e.copy();
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
					listener.onTransferFinish(megaApi, megaTransfer, megaError);
			    }
			});
		}
		if (singleListener){
			megaApi.privateFreeTransferListener(this);
		}
	}
	
	@Override
	public void onTransferUpdate(MegaApi api, MegaTransfer transfer)
	{
		if(listener != null)
		{
			final MegaTransfer megaTransfer = transfer.copy();
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
					listener.onTransferUpdate(megaApi, megaTransfer);
			    }
			});
		}
	}
	
	@Override
	public void onTransferTemporaryError(MegaApi api, MegaTransfer transfer, MegaError e)
	{
		if(listener != null)
		{
			final MegaTransfer megaTransfer = transfer.copy();
			final MegaError megaError = e.copy();
			megaApi.runCallback(new Runnable()
			{
			    public void run() 
			    {
					listener.onTransferTemporaryError(megaApi, megaTransfer, megaError);
			    }
			});
		}
	}
	
	public boolean onTransferData(MegaApi api, MegaTransfer transfer, byte[] buffer)
	{
		if(listener != null)
		{
			final MegaTransfer megaTransfer = transfer.copy();
			return listener.onTransferData(megaApi, megaTransfer, buffer);
		}	  
		return false;
	}
}
