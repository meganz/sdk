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
package nz.mega.sdk;

import java.util.ArrayList;

/**
 * Listener to receive and send global events to the app.
 *
 * @see MegaGlobalListenerInterface
 * @see MegaGlobalListener
 */
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
     * <p>
     * The SDK retains the ownership of the MegaUserList in the second parameter. 
     * The list and all the MegaUser objects that it contains will be valid until this function returns. 
     * If you want to save the list, use MegaUserList.copy().
     * If you want to save only some of the MegaUser objects, use MegaUser.copy() for those objects.
     *  
     * @param api
     *            API object connected to account.
     * @param userList
     *            List that contains the new or updated contacts.
     * @see MegaGlobalListenerInterface#onUsersUpdate(MegaApiJava api, ArrayList<MegaUser> users)
     * @see MegaGlobalListener#onUsersUpdate(MegaApi api, MegaUserList users)
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
     * This function is called when there are new or updated user alerts in the account
     *
     * The SDK retains the ownership of the MegaUserAlertList in the second parameter. The list and all the
     * MegaUserAlert objects that it contains will be valid until this function returns. If you want to save the
     * list, use MegaUserAlertList::copy. If you want to save only some of the MegaUserAlert objects, use MegaUserAlert::copy
     * for those objects.
     *
     * @param api MegaApi object connected to the account
     * @param userAlertList List that contains the new or updated contacts
     */
    @Override
    public void onUserAlertsUpdate(MegaApi api, MegaUserAlertList userAlertList){
        if (listener != null){
            final ArrayList<MegaUserAlert> userAlerts = MegaApiJava.userAlertListToArray(userAlertList);
            megaApi.runCallback(new Runnable() {
                @Override
                public void run() {
                 listener.onUserAlertsUpdate(megaApi, userAlerts);
                }
            });
        }
    }

    /**
     * This function is called when there are new or updated nodes in the account.
     * <p>
     * When the full account is reloaded or a large number of server notifications arrives at once,
     * the second parameter will be null.
     * The SDK retains the ownership of the MegaNodeList in the second parameter. 
     * The list and all the MegaNode objects that it contains will be valid until this function returns. 
     * If you want to save the list, use MegaNodeList.copy().
     * If you want to save only some of the MegaNode objects, use MegaNode.copy() for those nodes.
     *
     * @param api
     *            API object connected to account.
     * @param nodeList
     *            List of new or updated Nodes.
     * @see MegaGlobalListenerInterface#onNodesUpdate(MegaApiJava api, ArrayList<MegaNode> nodes)
     * @see MegaGlobalListener#onNodesUpdate(MegaApi api, MegaNodeList nodes)
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
     * <p>
     * You should call MegaApiJava.fetchNodes() when this callback is received.
     *  
     * @param api
     *            API object connected to account.
     * @see MegaGlobalListenerInterface#onReloadNeeded(MegaApiJava api)
     * @see MegaGlobalListener#onReloadNeeded(MegaApi api)
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

    @Override
    public void onAccountUpdate(MegaApi api) {
        if (listener != null) {
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onAccountUpdate(megaApi);
                }
            });
        }
    }

    @Override
    public void onContactRequestsUpdate(MegaApi api, MegaContactRequestList contactRequestList) {
        if (listener != null) {
            final ArrayList<MegaContactRequest> requests = MegaApiJava.contactRequestListToArray(contactRequestList);
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onContactRequestsUpdate(megaApi, requests);
                }
            });
        }
    }

    @Override
    public void onEvent(MegaApi api, MegaEvent event){
        if (listener != null) {
            final MegaEvent megaEvent = event.copy();
            megaApi.runCallback(new Runnable() {
                public void run() {
                    listener.onEvent(megaApi, megaEvent);
                }
            });
        }
    }

    @Override
    public void onSetsUpdate(MegaApi api, MegaSetList setList) {
        if (listener != null) {
            final ArrayList<MegaSet> sets = MegaApiJava.megaSetListToArray(setList);
            megaApi.runCallback((new Runnable() {
                public void run() {
                    listener.onSetsUpdate(megaApi, sets);
                }
            }));
        }
    }
    
    @Override
    public void onSetElementsUpdate(MegaApi api, MegaSetElementList elementList) {
        if (listener != null) {
            final ArrayList<MegaSetElement> elements = MegaApiJava.megaSetElementListToArray(elementList);
            megaApi.runCallback((new Runnable() {
                public void run() {
                    listener.onSetElementsUpdate(megaApi, elements);
                }
            }));
        }
    }

    @Override
    public void onGlobalSyncStateChanged(MegaApi api) {
        if (listener != null) {
            megaApi.runCallback((new Runnable() {
                public void run() {
                    listener.onGlobalSyncStateChanged(megaApi);
                }
            }));
        }
    }
}
