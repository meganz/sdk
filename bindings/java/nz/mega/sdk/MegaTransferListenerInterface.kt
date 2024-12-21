/*
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,\
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * @copyright Simplified (2-clause) BSD License.
 * You should have received a copy of the license along with this
 * program.
 */
package nz.mega.sdk

/**
 * Interface to receive information about transfers.
 *
 *
 * All transfers are able to pass a pointer to an implementation of this interface in the last parameter.
 * You can also get information about all transfers using MegaApi.addTransferListener().
 * MegaListener objects can also receive information about transfers.
 */
interface MegaTransferListenerInterface {
    /**
     * This function is called when a transfer is about to start being processed.
     *
     *
     * The SDK retains the ownership of the transfer parameter. Do not use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *
     * @param api
     * MegaApi object that started the transfer.
     * @param transfer
     * Information about the transfer.
     */
    fun onTransferStart(api: MegaApiJava, transfer: MegaTransfer)

    /**
     * This function is called when a transfer has finished.
     *
     *
     * The SDK retains the ownership of the transfer and error parameters. Do not use them after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     * There will not be further callbacks relating to this transfer. The last parameter provides the result of the
     * transfer. If the transfer finished without problems, the error code will be API_OK.
     *
     * @param api
     * MegaApi object that started the transfer.
     * @param transfer
     * Information about the transfer.
     * @param e
     * Error information.
     */
    fun onTransferFinish(api: MegaApiJava, transfer: MegaTransfer, e: MegaError)

    /**
     * This function is called to get details about the progress of a transfer.
     *
     *
     * The SDK retains the ownership of the transfer parameter. Do not use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *
     * @param api
     * MegaApi object that started the transfer.
     * @param transfer
     * Information about the transfer.
     */
    fun onTransferUpdate(api: MegaApiJava, transfer: MegaTransfer)

    /**
     * This function is called when there is a temporary error processing a transfer.
     *
     *
     * The transfer continues after this callback, so expect more MegaTransferListener.onTransferTemporaryError
     * or a MegaTransferListener.onTransferFinish callback. The SDK retains the ownership of the transfer and
     * error parameters. Do not use them after this function returns.
     *
     * @param api
     * MegaApi object that started the transfer.
     * @param transfer
     * Information about the transfer.
     * @param e
     * Error information.
     */
    fun onTransferTemporaryError(api: MegaApiJava, transfer: MegaTransfer, e: MegaError)

    /**
     * This function is called to provide the last read bytes of streaming downloads.
     *
     *
     * This function will not be called for non streaming downloads. You can get the same buffer provided by this
     * function in MegaTransferListener.onTransferUpdate, using MegaTransfer.getLastBytes() and
     * MegaTransfer.getDeltaSize(). The SDK retains the ownership of the transfer and buffer parameters.
     * Do not use them after this functions returns. This callback is mainly provided for compatibility with other
     * programming languages.
     *
     * @param api
     * MegaApi object that started the transfer.
     * @param transfer
     * Information about the transfer.
     * @param buffer
     * Buffer with the last read bytes.
     * @return
     * Size of the buffer.
     */
    fun onTransferData(api: MegaApiJava, transfer: MegaTransfer, buffer: ByteArray): Boolean

    /**
     * @brief This function is called to inform about the progress of a folder transfer
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * This callback is only made for folder transfers, and only to the listener for that
     * transfer, not for any globally registered listeners.  The callback is only made
     * during the scanning phase.
     *
     * This function can be used to give feedback to the user as to how scanning is progressing,
     * since scanning may take a while and the application may be showing a modal dialog during
     * this time.
     *
     * Note that this function could be called from a variety of threads during the
     * overall operation, so proper thread safety should be observed.
     *
     * @param api MEGASdk object that started the transfer
     * @param transfer Information about the transfer
     * @param stage MEGATransferStageScan or a later value in that enum
     * @param folderCount The count of folders scanned so far
     * @param createdFolderCount The count of folders created so far (only relevant in MEGATransferStageCreateTree)
     * @param fileCount The count of files scanned (and fingerprinted) so far.  0 if not in scanning stage
     * @param currentFolder The path of the folder currently being scanned (nil except in the scan stage)
     * @param currentFileLeafName The leaft name of the file currently being fingerprinted (can be nil for the first call in a new folder, and when not scanning anymore)
     */
    fun onFolderTransferUpdate(
        api: MegaApiJava,
        transfer: MegaTransfer,
        stage: Int,
        folderCount: Long,
        createdFolderCount: Long,
        fileCount: Long,
        currentFolder: String?,
        currentFileLeafName: String?,
    )
}