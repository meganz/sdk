package nz.mega.sdk;

/**
 * Interface to receive information about transfers.
 * <p>
 * All transfers are able to pass a pointer to an implementation of this interface in the singleListener parameter.
 * You can also get information about all transfers using MegaApi.addTransferListener().
 * MegaListener objects can also receive information about transfers.
 */
class DelegateMegaTransferListener extends MegaTransferListener {
    MegaApiJava megaApi;
    MegaTransferListenerInterface listener;
    boolean singleListener;

    DelegateMegaTransferListener(MegaApiJava megaApi, MegaTransferListenerInterface listener, boolean singleListener) {
        this.megaApi = megaApi;
        this.listener = listener;
        this.singleListener = singleListener;
    }

    MegaTransferListenerInterface getUserListener() {
        return listener;
    }

    /**
     * This function is called when a transfer is about to start being processed.
     * <p>
     * The SDK retains the ownership of the transfer parameter. Do not use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *
     * @param api
     *              MegaApi object that started the transfer
     * @param transfer
     *              Information about the transfer
     */
    @Override
    public void onTransferStart(MegaApi api, MegaTransfer transfer) {
        if (listener != null) {
            final MegaTransfer megaTransfer = transfer.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onTransferStart(megaApi, megaTransfer);
                }
            });
        }
    }

    /**
     * This function is called when a transfer has finished.
     * <p>
     * The SDK retains the ownership of the transfer and error parameters. Do not use them after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     * There will not be further callbacks relating to this transfer. The last parameter provides the result of the
     * transfer. If the transfer finished without problems, the error code will be API_OK.
     *
     * @param api
     *          MegaApi object that started the transfer
     * @param transfer
     *          Information about the transfer
     * @param e
     *          Error information
     */
    @Override
    public void onTransferFinish(MegaApi api, MegaTransfer transfer, MegaError e) {
        if (listener != null) {
            final MegaTransfer megaTransfer = transfer.copy();
            final MegaError megaError = e.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onTransferFinish(megaApi, megaTransfer, megaError);
                }
            });
        }
        if (singleListener) {
            megaApi.privateFreeTransferListener(this);
        }
    }

    /**
     * This function is called to get details about the progress of a transfer.
     * <p>
     * The SDK retains the ownership of the transfer parameter. Do not use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *
     * @param api
     *          MegaApi object that started the transfer
     * @param transfer
     *          Information about the transfer
     */
    @Override
    public void onTransferUpdate(MegaApi api, MegaTransfer transfer) {
        if (listener != null) {
            final MegaTransfer megaTransfer = transfer.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onTransferUpdate(megaApi, megaTransfer);
                }
            });
        }
    }

    /**
     * This function is called when there is a temporary error processing a transfer.
     * <p>
     * The transfer continues after this callback, so expect more MegaTransferListener.onTransferTemporaryError
     * or a MegaTransferListener.onTransferFinish callback. The SDK retains the ownership of the transfer and
     * error parameters. Do not use them after this function returns.
     *
     * @param api
     *          MegaApi object that started the transfer
     * @param transfer
     *          Information about the transfer
     * @param e
     *          Error information
     */
    @Override
    public void onTransferTemporaryError(MegaApi api, MegaTransfer transfer, MegaError e) {
        if (listener != null) {
            final MegaTransfer megaTransfer = transfer.copy();
            final MegaError megaError = e.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onTransferTemporaryError(megaApi, megaTransfer, megaError);
                }
            });
        }
    }

    /**
     * This function is called to provide the last read bytes of streaming downloads.
     * <p>
     * This function will not be called for non streaming downloads. You can get the same buffer provided by this
     * function in MegaTransferListener.onTransferUpdate, using MegaTransfer.getLastBytes() and
     * MegaTransfer.getDeltaSize(). The SDK retains the ownership of the transfer and buffer parameters.
     * Do not use them after this functions returns. This callback is mainly provided for compatibility with other
     * programming languages.
     *
     * @param api
     *          MegaApi object that started the transfer
     * @param transfer
     *          Information about the transfer
     * @param buffer
     *          Buffer with the last read bytes
     * @return
     *          Size of the buffer
     */
    public boolean onTransferData(MegaApi api, MegaTransfer transfer, byte[] buffer) {
        if (listener != null) {
            final MegaTransfer megaTransfer = transfer.copy();
            return listener.onTransferData(megaApi, megaTransfer, buffer);
        }
        return false;
    }
}
