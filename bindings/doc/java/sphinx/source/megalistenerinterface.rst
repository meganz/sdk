===============================
Interface MegaListenerInterface
===============================

All Superinterfaces:
MegaGlobalListenerInterface, MegaRequestListenerInterface, MegaTransferListenerInterface

---------------------------------------

public interface MegaListenerInterface
extends MegaRequestListenerInterface, MegaGlobalListenerInterface, MegaTransferListenerInterface
The Interface MegaListenerInterface.

----------------------------------------

--------------
Method Summary
--------------

Methods inherited from interface nz.mega.sdk.MegaRequestListenerInterface
onRequestFinish, onRequestStart, onRequestTemporaryError, onRequestUpdate

-------------------------------

Methods inherited from interface nz.mega.sdk.MegaGlobalListenerInterface
onNodesUpdate, onReloadNeeded, onUsersUpdate

--------------------------------

Methods inherited from interface nz.mega.sdk.MegaTransferListenerInterface
onTransferData, onTransferFinish, onTransferStart, onTransferTemporaryError, onTransferUpdate
