package nz.mega.sdk;

import java.io.IOException;
import java.io.OutputStream;

/**
 * The listener interface for receiving delegateOutputMegaTransfer events.
 * <p>
 * The class that is interested in processing
 * a delegateOutputMegaTransfer event implements this interface, and the object created with that class is registered
 * with a component using the component's addDelegateOutputMegaTransferListener method. When the
 * delegateOutputMegaTransfer event occurs, that object's appropriate method is invoked.
 */
public class DelegateOutputMegaTransferListener extends DelegateMegaTransferListener {
    OutputStream outputStream;

    /**
     * Instantiates a new delegate output mega transfer listener.
     *
     * @param megaApi
     *              the mega api
     * @param outputStream
     *              the output stream
     * @param listener
     *              the listener
     * @param singleListener
     *              the single listener
     */
    public DelegateOutputMegaTransferListener(MegaApiJava megaApi, OutputStream outputStream, MegaTransferListenerInterface listener,
            boolean singleListener) {
        super(megaApi, listener, singleListener);
        this.outputStream = outputStream;
    }

    /**
     * Provides the last read bytes of streaming downloads.
     *
     * @param api
     *              MegaApi object that started the transfer
     * @param transfer
     *              Information about the transfer
     * @param buffer
     *              Buffer with the last read bytes
     * @return
     *              true, if successful
     */
    public boolean onTransferData(MegaApi api, MegaTransfer transfer, byte[] buffer) {
        if (outputStream != null) {
            try {
                outputStream.write(buffer);
                return true;
            } catch (IOException e) {
            }
        }
        return false;
    }
}
