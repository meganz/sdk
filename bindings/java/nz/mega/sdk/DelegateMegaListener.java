package nz.mega.sdk;

import java.util.ArrayList;

class DelegateMegaListener extends MegaListener {
    MegaApiJava megaApi;
    MegaListenerInterface listener;

    DelegateMegaListener(MegaApiJava megaApi, MegaListenerInterface listener) {
        this.megaApi = megaApi;
        this.listener = listener;
    }

    MegaListenerInterface getUserListener() {
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
            ;
            final MegaError megaError = e.copy();
            ;
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onRequestFinish(megaApi, megaRequest, megaError);
                }
            });
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

    /**
     * This function is called when a transfer is about to start being processed. 
     * The SDK retains the ownership of the transfer parameter. 
     * Don't use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *  
     * @param transfer
     *            Information about the transfer
     * @param api
     *            API that started the request
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
     * The SDK retains the ownership of the transfer and error parameters.
     * Don't use them after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     * There won't be more callbacks about this transfer. 
     * The last parameter provides the result of the transfer. 
     * If the transfer finished without problems, the error code will be API_OK
     *  
     * @param transfer
     *            Information about the transfer
     * @param api
     *            API that started the request
     * @param e
     *            Error Information
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
    }

    /**
     * This function is called to inform about the progress of a transfer. 
     * The SDK retains the ownership of the transfer parameter. Don't use it after this functions returns.
     * The api object is the one created by the application, it will be valid until the application deletes it.
     *  
     * @param transfer
     *            Information about the transfer
     * @param api
     *            API that started the request
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
     * The transfer continues after this callback, so expect more MegaTransferListener::onTransferTemporaryError 
     * or a MegaTransferListener::onTransferFinish callbackThe SDK retains the ownership of the transfer and error parameters.
     * Don't use them after this functions returns.
     *  
     * @param transfer
     *            Information about the transfer
     * @param api
     *            API that started the request
     * @param e
     *            Error Information
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
     * This function is called when there are new or updated contacts in the account. 
     * The SDK retains the ownership of the MegaUserList in the second parameter.
     * The list and all the MegaUser objects that it contains will be valid until this function returns. 
     * If you want to save the list, use MegaUserList::copy.
     * If you want to save only some of the MegaUser objects, use MegaUser::copy for those objects.
     *  
     * @param api
     *            API that started the request
     * @param userList
     *            List that contains new or updated contacts
     */
    @Override
    public void onUsersUpdate(MegaApi api, MegaUserList userList) {
        if (listener != null) {
            final ArrayList<MegaUser> users = MegaApiJava.userListToArray(userList);
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onUsersUpdate(megaApi, users);
                }
            });
        }
    }

   /**
     * This function is called when there are new or updated nodes in the account. 
     * When the full account is reloaded or a large number of server notifications arrives at once, the second parameter will be NULL.
     * The SDK retains the ownership of the MegaNodeList in the second parameter. 
     * The list and all the MegaNode objects that it contains will be valid until this function returns. 
     * If you want to save the list, use MegaNodeList::copy. 
     * If you want to save only some of the MegaNode objects, use MegaNode::copy for those nodes.
     *  
     * @param api
     *            API that started the request
     * @param nodeList
     *            List that contains new or updated nodes
     */
    @Override
    public void onNodesUpdate(MegaApi api, MegaNodeList nodeList) {
        if (listener != null) {
            final ArrayList<MegaNode> nodes = MegaApiJava.nodeListToArray(nodeList);
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onNodesUpdate(megaApi, nodes);
                }
            });
        }
    }
    
   /**
     * This function is called when an inconsistency is detected in the local cache. 
     * You should call MegaApi::fetchNodes when this callback is received
     *  
     * @param api
     *            API that started the request
     */
    @Override
    public void onReloadNeeded(MegaApi api) {
        if (listener != null) {
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onReloadNeeded(megaApi);
                }
            });
        }
    }
}
