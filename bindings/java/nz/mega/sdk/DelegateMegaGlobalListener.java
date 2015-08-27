package nz.mega.sdk;

import java.util.ArrayList;

class DelegateMegaGlobalListener extends MegaGlobalListener {
    MegaApiJava megaApi;
    MegaGlobalListenerInterface listener;

    DelegateMegaGlobalListener(MegaApiJava megaApi,
            MegaGlobalListenerInterface listener) {
        this.megaApi = megaApi;
        this.listener = listener;
    }

    MegaGlobalListenerInterface getUserListener() {
        return listener;
    }


    /**
     * This function is called when there are new or updated contacts in the account. 
     * The SDK retains the ownership of the MegaUserList in the second parameter. 
     * The list and all the MegaUser objects that it contains will be valid until this function returns. 
     * If you want to save the list, use MegaUserList::copy. 
     * If you want to save only some of the MegaUser objects, use MegaUser::copy for those objects.
     *  
     * @param userList
     *            List of new or updated Contacts
     * @param api
     *            Mega Java API connected to account
     */
    @Override
    public void onUsersUpdate(MegaApi api, MegaUserList userList) {
        if (listener != null) {
            final ArrayList<MegaUser> users = MegaApiJava
                    .userListToArray(userList);
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
     * @param nodeList
     *            List of new or updated Nodes
     * @param api
     *            API connected to account
     */
    @Override
    public void onNodesUpdate(MegaApi api, MegaNodeList nodeList) {
        if (listener != null) {
            final ArrayList<MegaNode> nodes = MegaApiJava
                    .nodeListToArray(nodeList);
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
     *            API connected to account
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
