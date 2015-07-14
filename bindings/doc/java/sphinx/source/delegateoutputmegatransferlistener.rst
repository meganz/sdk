==================================
DelegateOutputMegaTransferListener
==================================

public class DelegateOutputMegaTransferListener
extends DelegateMegaTransferListener

The listener interface for receiving delegateOutputMegaTransfer events. The class that is interested in processing a delegateOutputMegaTransfer event implements this interface, and the object created with that class is registered with a component using the component's addDelegateOutputMegaTransferListener method. When the delegateOutputMegaTransfer event occurs, that object's appropriate method is invoked.

----------------------------------

-------------
Field Summary
-------------

+---------------------------------------+-----------------------------------------------+
| Modifier and Type	                |   Field and Description                       |
+=======================================+===============================================+
|(package private) java.io.OutputStream	|   outputStream                                |
|                                       |    The output stream.                         |
+---------------------------------------+-----------------------------------------------+

------------------------------------

--------------------------------------------------------------------
Fields inherited from class nz.mega.sdk.DelegateMegaTransferListener
--------------------------------------------------------------------
listener, megaApi, singleListener

--------------------------------------

-------------------
Constructor Summary
-------------------
DelegateOutputMegaTransferListener(MegaApiJava megaApi, java.io.OutputStream outputStream, MegaTransferListenerInterface listener, boolean singleListener)
Instantiates a new delegate output mega transfer listener.

-----------------------------------------

--------------
Method Summary
--------------
+---------------------------------------+-------------------------------------------------------------------+
| Modifier and Type	                |     Method and Description                                        |
+=======================================+===================================================================+
|boolean                                | onTransferData(MegaApi api, MegaTransfer transfer, byte[] buffer) |
|                                       | On transfer data.                                                 |
+---------------------------------------+-------------------------------------------------------------------+

-----------------------------------------------

---------------------------------------------------------------------
Methods inherited from class nz.mega.sdk.DelegateMegaTransferListener
---------------------------------------------------------------------
getUserListener, onTransferFinish, onTransferStart, onTransferTemporaryError, onTransferUpdate

------------------------------------------------

---------------------------------------------
Methods inherited from class java.lang.Object
---------------------------------------------
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

---------------------------------------------------

------------
Field Detail
------------

~~~~~~~~~~~~
outputStream
~~~~~~~~~~~~
java.io.OutputStream outputStream
The output stream.

----------------------------------------------------

------------------
Constructor Detail
------------------

DelegateOutputMegaTransferListener
public DelegateOutputMegaTransferListener(MegaApiJava megaApi, java.io.OutputStream outputStream,                              MegaTransferListenerInterface listener, boolean singleListener)
Instantiates a new delegate output mega transfer listener.
Parameters:
megaApi - the mega api
outputStream - the output stream
listener - the listener
singleListener - the single listener

-------------------------------------------------------------

-------------
Method Detail
-------------

onTransferData
public boolean onTransferData(MegaApi api,MegaTransfer transfer,byte[] buffer)
On transfer data.
Overrides:
onTransferData in class DelegateMegaTransferListener
Parameters:
api - the api
transfer - the transfer
buffer - the buffer
Returns:
true, if successful
