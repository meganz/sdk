===========================
MegaGlobalListenerInterface
===========================

The MegaGlobalListenerInterface is an Interface used to gather information about global events.  

-----------------------
Public Member Functions
-----------------------
+-------------------+----------------------------------------------------------------------------------+
| Modifier and Type |	Method and Description                                                         |
+===================+==================================================================================+
| void	            |    onNodesUpdate(MegaApiJava api, java.util.ArrayList<MegaNode> nodes)           |
|                   |    This function is called when there are new or updated nodes in the account.   |
+-------------------+----------------------------------------------------------------------------------+
| void	            |   onReloadNeeded(MegaApiJava api)                                                |
|                   |   This function is called when an inconsistency is detected in the local cache.  |
+-------------------+----------------------------------------------------------------------------------+
| void	            |   onUsersUpdate(MegaApiJava api, java.util.ArrayList<MegaUser> users)            |
|                   |   This function is called when there are new or updated contacts in the account. |
+-------------------+----------------------------------------------------------------------------------+

------------------------------------------------------------------------------------------------------------------------------

--------------------
Detailed Description
--------------------

~~~~~~~~~~~~~
onNodesUpdate
~~~~~~~~~~~~~                                                                                        
+------------------------------------------------------------------------------------------------------+
| onNodesUpdate(MegaApiJava api, java.util.ArrayList<MegaNode> nodes)                                  |
+------------------------------------------------------------------------------------------------------+

This function is called when there are new or updated nodes in the account.

When the full account is reloaded or a large number of server notifications arrives at once, the second parameter will be NULL.

The SDK retains the ownership of the MegaNodeList in the second parameter. The list and all the MegaNode objects that it contains will be valid until this function returns. If you want to save the list, use MegaNodeList::copy. If you want to save only some of the MegaNode objects, use MegaNode::copy for those nodes.

Parameters
api	MegaApi object connected to the account
nodes	List that contains the new or updated nodes

------------------------------------------------------------------------------------------------------------------------------

~~~~~~~~~~~~~~
onReloadNeeded
~~~~~~~~~~~~~~                                                                                       
+------------------------------------------------------------------------------------------------------+
| onReloadNeeded(MegaApiJava api)                                                                      |
+------------------------------------------------------------------------------------------------------+

This function is called when an inconsistency is detected in the local cache.

You should call MegaApi::fetchNodes when this callback is received

Parameters
api	MegaApi object connected to the account

------------------------------------------------------------------------------------------------------------------------------

~~~~~~~~~~~~~~
onReloadNeeded
~~~~~~~~~~~~~~                                                                                       
+------------------------------------------------------------------------------------------------------+
|OnUsersUpdateMegaApiJavaapi,java.util.ArrayList<MegaUser>users)                                       |
+------------------------------------------------------------------------------------------------------+

This function is called when there are new or updated contacts in the account.

The SDK retains the ownership of the MegaUserList in the second parameter. The list and all the MegaUser objects that it contains will be valid until this function returns. If you want to save the list, use MegaUserList::copy. If you want to save only some of the MegaUser objects, use MegaUser::copy for those objects.

Parameters
api	MegaApi object connected to the account
users	List that contains the new or updated contacts


