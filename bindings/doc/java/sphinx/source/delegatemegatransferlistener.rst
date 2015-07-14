============================
DelegateMegaTransferListener
============================

class DelegateMegaTransferListener
extends MegaTransferListener

The listener interface for receiving delegateMegaTransfer events. The class that is interested in processing a delegateMegaTransfer event implements this interface, and the object created with that class is registered with a component using the component's addDelegateMegaTransferListener method. When the delegateMegaTransfer event occurs, that object's appropriate method is invoked.

--------------------------------------------

-------------
Field Summary
-------------

+-----------------------------------------------+------------------------------------+
|Modifier and Type	                        | Field and Description              |
+===============================================+====================================+
|(package private) MegaTransferListenerInterface|  listener                          |
|                                               |  The listener.                     |
+-----------------------------------------------+------------------------------------+
|(package private) MegaApiJava	                |  megaApi                           |
|                                               |  The mega api.                     |
+-----------------------------------------------+------------------------------------+
|(package private) boolean	                |  singleListener                    |
|                                               |  The single listener.              |
+-----------------------------------------------+------------------------------------+

------------------------------

-------------------
Constructor Summary
-------------------

DelegateMegaTransferListener(MegaApiJava megaApi, MegaTransferListenerInterface listener, boolean singleListener)
Instantiates a new delegate mega transfer listener.

-------------------------------

--------------
Method Summary
--------------

+-----------------------------------------------+-----------------------------------------------------------------------------+
|Modifier and Type	                        |   Method and Description                                                    |
+===============================================+=============================================================================+
|(package private) MegaTransferListenerInterface|   getUserListener()                                                         |
|                                               |   Gets the user listener.                                                   |
+-----------------------------------------------+-----------------------------------------------------------------------------+
|boolean	                                |    onTransferData(MegaApi api, MegaTransfer transfer, byte[] buffer)        |
|                                               |    On transfer data.                                                        |
+-----------------------------------------------+-----------------------------------------------------------------------------+
|void	                                        |   onTransferFinish(MegaApi api, MegaTransfer transfer, MegaError e)         |
|                                               |    On transfer finish.                                                      |
+-----------------------------------------------+-----------------------------------------------------------------------------+
|void	                                        |   onTransferStart(MegaApi api, MegaTransfer transfer)                       |
|                                               |    On transfer start.                                                       |
+-----------------------------------------------+-----------------------------------------------------------------------------+
|void	                                        |   onTransferTemporaryError(MegaApi api, MegaTransfer transfer, MegaError e) |
|                                               |    On transfer temporary error.                                             |
+-----------------------------------------------+-----------------------------------------------------------------------------+
|void	                                        |   onTransferUpdate(MegaApi api, MegaTransfer transfer)                      |
|                                               |    On transfer update.                                                      |
+-----------------------------------------------+-----------------------------------------------------------------------------+

------------------

---------------------------------------------
Methods inherited from class java.lang.Object
---------------------------------------------
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

------------------

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
MegaTransferListenerInterface listener
The listener.

~~~~~~~~~~~~~~
singleListener
~~~~~~~~~~~~~~
boolean singleListener
The single listener.

---------------------------------------

------------------
Constructor Detail
------------------

DelegateMegaTransferListener
DelegateMegaTransferListener(MegaApiJava megaApi,MegaTransferListenerInterface listener,boolean singleListener)
Instantiates a new delegate mega transfer listener.
Parameters:
megaApi - the mega api
listener - the listener
singleListener - the single listener

-------------------------------------------

-------------
Method Detail
-------------

~~~~~~~~~~~~~~~
getUserListener
~~~~~~~~~~~~~~~
MegaTransferListenerInterface getUserListener()
Gets the user listener.
Returns:
the user listener

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

~~~~~~~~~~~~~~
onTransferData
~~~~~~~~~~~~~~
public boolean onTransferData(MegaApi api,MegaTransfer transfer,byte[] buffer)
On transfer data.
Parameters:
api - the api
transfer - the transfer
buffer - the buffer
Returns:
true, if successful
