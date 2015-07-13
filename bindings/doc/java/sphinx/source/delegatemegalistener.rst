====================
DelegateMegaListener
====================

The listener interface for receiving delegateMega events. The class that is interested in processing a delegateMega event implements this interface, and the object created with that class is registered with a component using the component's addDelegateMegaListener method. When the delegateMega event occurs, that object's appropriate method is invoked.

-------------------------------

-------------
Field Summary
-------------
+---------------------------------------------+-----------------------------+
| Modifier and Type	                      |  Field and Description      |
+=============================================+=============================+
|(package private) MegaListenerInterface      |	 listener                   |
|                                             |   The listener.             |
+---------------------------------------------+-----------------------------+
|(package private) MegaApiJava	              |  megaApi                    |
|                                             |  The mega api.              |
+---------------------------------------------+-----------------------------+

-----------------------------

-------------------
Constructor Summary
-------------------
DelegateMegaListener(MegaApiJava megaApi, MegaListenerInterface listener)
Instantiates a new delegate mega listener.

----------------------------

--------------
Method Summary
--------------
+---------------------------------------------+----------------------------------------------------------------------------+
| Modifier and Type	                      | Method and Description                                                     |
+=============================================+============================================================================+
| (package private) MegaListenerInterface     |	 getUserListener()                                                         |
|                                             |  Gets the user listener.                                                   |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onNodesUpdate(MegaApi api, MegaNodeList nodeList)                         |
|                                             |  On nodes update.                                                          |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onReloadNeeded(MegaApi api)                                               |
|                                             |  On reload needed.                                                         |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onRequestFinish(MegaApi api, MegaRequest request, MegaError e)            |
|                                             |  On request finish.                                                        |
+---------------------------------------------+----------------------------------------------------------------------------+
|void	                                      |  onRequestStart(MegaApi api, MegaRequest request)                          |
|                                             |  On request start.                                                         |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onRequestTemporaryError(MegaApi api, MegaRequest request, MegaError e)    |
|                                             |  On request temporary error.                                               |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onTransferFinish(MegaApi api, MegaTransfer transfer, MegaError e)         |
|                                             |   On transfer finish.                                                      |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onTransferStart(MegaApi api, MegaTransfer transfer)                       |
|                                             |  On transfer start.                                                        |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onTransferTemporaryError(MegaApi api, MegaTransfer transfer, MegaError e) |
|                                             |  On transfer temporary error.                                              |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onTransferUpdate(MegaApi api, MegaTransfer transfer)                      |
|                                             |  On transfer update.                                                       |
+---------------------------------------------+----------------------------------------------------------------------------+
| void	                                      |  onUsersUpdate(MegaApi api, MegaUserList userList)                         |
|                                             |  On users update.                                                          |
+---------------------------------------------+----------------------------------------------------------------------------+

------------------

---------------------------------------------
Methods inherited from class java.lang.Object
---------------------------------------------
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

----------------------

------------
Field Detail
------------
~~~~~~~
megaApi
~~~~~~~
MegaApiJava megaApi   
The mega api.   

~~~~~~~~
listener
~~~~~~~~
MegaListenerInterface listener   
The listener.   

----------------

------------------
Constructor Detail
------------------

~~~~~~~~~~~~~~~~~~~~
DelegateMegaListener
~~~~~~~~~~~~~~~~~~~~
DelegateMegaListener(MegaApiJava megaApi,MegaListenerInterface listener)  
Instantiates a new delegate mega listener.  
Parameters:  
megaApi - the mega api  
listener - the listener  

---------------------

-------------
Method Detail
-------------

~~~~~~~~~~~~~~~
getUserListener
~~~~~~~~~~~~~~~

MegaListenerInterface getUserListener()
Gets the user listener.
Returns:
the user listener

~~~~~~~~~~~~~~
onRequestStart
~~~~~~~~~~~~~~

public void onRequestStart(MegaApi api,MegaRequest request)
On request start.
Parameters:
api - the api
request - the request

~~~~~~~~~~~~~~~
onRequestFinish
~~~~~~~~~~~~~~~

public void onRequestFinish(MegaApi api,MegaRequest request,MegaError e)
On request finish.
Parameters:
api - the api
request - the request
e - the e

~~~~~~~~~~~~~~~~~~~~~~~
onRequestTemporaryError
~~~~~~~~~~~~~~~~~~~~~~~

public void onRequestTemporaryError(MegaApi api,MegaRequest request,MegaError e)
On request temporary error.
Parameters:
api - the api
request - the request
e - the e

~~~~~~~~~~~~~~~
onTransferStart
~~~~~~~~~~~~~~~

public void onTransferStart(MegaApi api,MegaTransfer transfer)
On transfer start.
Parameters:
api - the api
transfer - the transfer

~~~~~~~~~~~~~~~~
onTransferFinish
~~~~~~~~~~~~~~~~

public void onTransferFinish(MegaApi api,MegaTransfer transfer,MegaError e)
On transfer finish.
Parameters:
api - the api
transfer - the transfer
e - the e

~~~~~~~~~~~~~~~~
onTransferUpdate
~~~~~~~~~~~~~~~~

public void onTransferUpdate(MegaApi api,MegaTransfer transfer)
On transfer update.
Parameters:
api - the api
transfer - the transfer

~~~~~~~~~~~~~~~~~~~~~~~~
onTransferTemporaryError
~~~~~~~~~~~~~~~~~~~~~~~~

public void onTransferTemporaryError(MegaApi api,MegaTransfer transfer,MegaError e)
On transfer temporary error.
Parameters:
api - the api
transfer - the transfer
e - the e

~~~~~~~~~~~~~
onUsersUpdate
~~~~~~~~~~~~~

public void onUsersUpdate(MegaApi api,MegaUserList userList)
On users update.
Parameters:
api - the api
userList - the user list

~~~~~~~~~~~~~
onNodesUpdate
~~~~~~~~~~~~~

public void onNodesUpdate(MegaApi api,MegaNodeList nodeList)
On nodes update.
Parameters:
api - the api
nodeList - the node list

~~~~~~~~~~~~~~
onReloadNeeded
~~~~~~~~~~~~~~

public void onReloadNeeded(MegaApi api)
On reload needed.
Parameters:
api - the api
