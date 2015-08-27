package nz.mega.sdk;

import java.util.ArrayList;

public interface MegaGlobalListenerInterface {
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
    public void onUsersUpdate(MegaApiJava api, ArrayList<MegaUser> users);

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
    public void onNodesUpdate(MegaApiJava api, ArrayList<MegaNode> nodes);

    /**
     * This function is called when an inconsistency is detected in the local cache. 
     * You should call MegaApi::fetchNodes when this callback is received
     *  
     * @param api
     *            API connected to account
     */
    public void onReloadNeeded(MegaApiJava api);
}
