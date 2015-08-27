package nz.mega.sdk;

public interface MegaRequestListenerInterface {
    
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
    public void onRequestStart(MegaApiJava api, MegaRequest request);

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
    public void onRequestUpdate(MegaApiJava api, MegaRequest request);

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
    public void onRequestFinish(MegaApiJava api, MegaRequest request, MegaError e);

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
    public void onRequestTemporaryError(MegaApiJava api, MegaRequest request, MegaError e);
}
