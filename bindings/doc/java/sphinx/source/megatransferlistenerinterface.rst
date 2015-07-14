=======================================
Interface MegaTransferListenerInterface
=======================================

All Known Subinterfaces:
MegaListenerInterface

public interface MegaTransferListenerInterface
The Interface MegaTransferListenerInterface.

----------------------------------------------

--------------
Method Summary
--------------

+--------------------+-----------------------------------------------------------------------------------+
|Modifier and Type   |  Method and Description                                                           |
+====================+===================================================================================+
| boolean	     |    onTransferData(MegaApiJava api, MegaTransfer transfer, byte[] buffer)          |
|                    |    On transfer data.                                                              |
+--------------------+-----------------------------------------------------------------------------------+
|void	             |   onTransferFinish(MegaApiJava api, MegaTransfer transfer, MegaError e)           |
|                    |    On transfer finish.                                                            |
+--------------------+-----------------------------------------------------------------------------------+
|void	             |   onTransferStart(MegaApiJava api, MegaTransfer transfer)                         |
|                    |    On transfer start.                                                             |
+--------------------+-----------------------------------------------------------------------------------+
|void	             |   onTransferTemporaryError(MegaApiJava api, MegaTransfer transfer, MegaError e)   |
|                    |    On transfer temporary error.                                                   |
+--------------------+-----------------------------------------------------------------------------------+
|void	             |   onTransferUpdate(MegaApiJava api, MegaTransfer transfer)                        |
|                    |    On transfer update.                                                            |
+--------------------+-----------------------------------------------------------------------------------+

-------------------------------------------------------

-------------
Method Detail
-------------

~~~~~~~~~~~~~~~
onTransferStart
~~~~~~~~~~~~~~~
void onTransferStart(MegaApiJava api,MegaTransfer transfer)
On transfer start.
Parameters:
api - the api
transfer - the transfer

~~~~~~~~~~~~~~~~
onTransferFinish
~~~~~~~~~~~~~~~~
void onTransferFinish(MegaApiJava api,MegaTransfer transfer,MegaError e)
On transfer finish.
Parameters:
api - the api
transfer - the transfer
e - the e

~~~~~~~~~~~~~~~~
onTransferUpdate
~~~~~~~~~~~~~~~~
void onTransferUpdate(MegaApiJava api,MegaTransfer transfer)
On transfer update.
Parameters:
api - the api
transfer - the transfer

~~~~~~~~~~~~~~~~~~~~~~~~
onTransferTemporaryError
~~~~~~~~~~~~~~~~~~~~~~~~
void onTransferTemporaryError(MegaApiJava api,MegaTransfer transfer,MegaError e)
On transfer temporary error.
Parameters:
api - the api
transfer - the transfer
e - the e

~~~~~~~~~~~~~~
onTransferData
~~~~~~~~~~~~~~
boolean onTransferData(MegaApiJava api,MegaTransfer transfer,byte[] buffer)
On transfer data.
Parameters:
api - the api
transfer - the transfer
buffer - the buffer
Returns:
true, if successful
