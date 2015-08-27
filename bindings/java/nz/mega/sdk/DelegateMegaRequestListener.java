package nz.mega.sdk;

class DelegateMegaRequestListener extends MegaRequestListener {

    MegaApiJava megaApi;
    MegaRequestListenerInterface listener;
    boolean singleListener;

    DelegateMegaRequestListener(MegaApiJava megaApi, MegaRequestListenerInterface listener, boolean singleListener) {
        this.megaApi = megaApi;
        this.listener = listener;
        this.singleListener = singleListener;
    }

    MegaRequestListenerInterface getUserListener() {
        return listener;
    }

    /**
     * This function is called when a request is about to start being processed.
     * The SDK retains the ownership of the request parameter. 
     * Don't use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *  
     * @param request
     *            Information about the request
     * @param api
     *            API that started the request
     */
    @Override
    public void onRequestStart(MegaApi api, MegaRequest request) {
        if (listener != null) {
            final MegaRequest megaRequest = request.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onRequestStart(megaApi, megaRequest);
                }
            });
        }
    }

    /**
     * This function is called to inform about the progres of a request. 
     * Currently, this callback is only used for fetchNodes (MegaRequest::TYPE_FETCH_NODES)
     * requestsThe SDK retains the ownership of the request parameter. 
     * Don't use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *  
     * @param request
     *            Information about the request
     * @param api
     *            API that started the request
     */
    @Override
    public void onRequestUpdate(MegaApi api, MegaRequest request) {
        if (listener != null) {
            final MegaRequest megaRequest = request.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onRequestUpdate(megaApi, megaRequest);
                }
            });
        }
    }

    /**
     * This function is called when a request has finished.
     * There won't be more callbacks about this request.
     * The last parameter provides the result of the request.
     * If the request finished without problems, the error code will be API_OKThe SDK retains the ownership of the request and error parameters. 
     * Don't use them after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *  
     * @param request
     *            Information about the request
     * @param api
     *            API that started the request
     * @param e
     *            Error Information
     */
    @Override
    public void onRequestFinish(MegaApi api, MegaRequest request, MegaError e) {
        if (listener != null) {
            final MegaRequest megaRequest = request.copy();
            final MegaError megaError = e.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onRequestFinish(megaApi, megaRequest, megaError);
                }
            });
        }
        if (singleListener) {
            megaApi.privateFreeRequestListener(this);
        }
    }

    /**
     * This function is called when there is a temporary error processing a request. 
     * The request continues after this callback, so expect more MegaRequestListener::onRequestTemporaryError 
     * or a MegaRequestListener::onRequestFinish callbackThe SDK retains the ownership of the request and error parameters. 
     * Don't use them after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *  
     * @param request
     *            Information about the request
     * @param api
     *            API that started the request
     * @param e
     *            Error Information
     */
    @Override
    public void onRequestTemporaryError(MegaApi api, MegaRequest request, MegaError e) {
        if (listener != null) {
            final MegaRequest megaRequest = request.copy();
            final MegaError megaError = e.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onRequestTemporaryError(megaApi, megaRequest, megaError);
                }
            });
        }
    }
}
