==========================
DelegateMegaGlobalListener
==========================

java.lang.Object
MegaGlobalListener
nz.mega.sdk.DelegateMegaGlobalListener

class DelegateMegaGlobalListener
extends MegaGlobalListener
The listener interface for receiving delegateMegaGlobal events. The class that is interested in processing a delegateMegaGlobal event implements this interface, and the object created with that class is registered with a component using the component's addDelegateMegaGlobalListener method. When the delegateMegaGlobal event occurs, that object's appropriate method is invoked.
See Also:
DelegateMegaGlobalEvent
Field Summary

Fields
Modifier and Type	Field and Description
(package private) MegaGlobalListenerInterface	listener
The listener.
(package private) MegaApiJava	megaApi
The mega api.
Constructor Summary

Constructors
Constructor and Description
DelegateMegaGlobalListener(MegaApiJava megaApi, MegaGlobalListenerInterface listener)
Instantiates a new delegate mega global listener.
Method Summary

All MethodsInstance MethodsConcrete Methods
Modifier and Type	Method and Description
(package private) MegaGlobalListenerInterface	getUserListener()
Gets the user listener.
void	onNodesUpdate(MegaApi api, MegaNodeList nodeList)
On nodes update.
void	onReloadNeeded(MegaApi api)
On reload needed.
void	onUsersUpdate(MegaApi api, MegaUserList userList)
On users update.
Methods inherited from class java.lang.Object
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait
Field Detail

megaApi
MegaApiJava megaApi
The mega api.

listener
MegaGlobalListenerInterface listener
The listener.
Constructor Detail

DelegateMegaGlobalListener
DelegateMegaGlobalListener(MegaApiJava megaApi, MegaGlobalListenerInterface listener)
Instantiates a new delegate mega global listener.
Parameters:
megaApi - the mega api
listener - the listener
Method Detail

getUserListener
MegaGlobalListenerInterface getUserListener()
Gets the user listener.
Returns:
the user listener
onUsersUpdate

public void onUsersUpdate(MegaApi api,MegaUserList userList)
On users update.
Parameters:
api - the api
userList - the user list
onNodesUpdate

public void onNodesUpdate(MegaApi api,MegaNodeList nodeList)
On nodes update.
Parameters:
api - the api
nodeList - the node list
onReloadNeeded

public void onReloadNeeded(MegaApi api)
On reload needed.
Parameters:
api - the api
