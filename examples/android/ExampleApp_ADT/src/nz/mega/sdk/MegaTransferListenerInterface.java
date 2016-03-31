/**
 * MegaTransferListenerInterface.java
 * Interface to MegaTransferListener
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

public interface MegaTransferListenerInterface 
{
	public void onTransferStart(MegaApiJava api, MegaTransfer transfer);
	public void onTransferFinish(MegaApiJava api, MegaTransfer transfer, MegaError e);
	public void onTransferUpdate(MegaApiJava api, MegaTransfer transfer);
	public void onTransferTemporaryError(MegaApiJava api, MegaTransfer transfer, MegaError e);
	public boolean onTransferData(MegaApiJava api, MegaTransfer transfer, byte[] buffer);
}
