============
MegaApiSwing
============

public class MegaApiSwing
extends MegaApiJava
The Class MegaApiSwing.

-------------------------------------

-------------
Field Summary
-------------

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Fields inherited from class nz.mega.sdk.MegaApiJava
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

activeGlobalListeners, activeMegaListeners, activeMegaTreeProcessors, activeRequestListeners, activeTransferListeners, EVENT_DEBUG, EVENT_FEEDBACK, EVENT_INVALID, gfxProcessor, LOG_LEVEL_DEBUG, LOG_LEVEL_ERROR, LOG_LEVEL_FATAL, LOG_LEVEL_INFO, LOG_LEVEL_MAX, LOG_LEVEL_WARNING, logger, megaApi, ORDER_ALPHABETICAL_ASC, ORDER_ALPHABETICAL_DESC, ORDER_CREATION_ASC, ORDER_CREATION_DESC, ORDER_DEFAULT_ASC, ORDER_DEFAULT_DESC, ORDER_MODIFICATION_ASC, ORDER_MODIFICATION_DESC, ORDER_NONE, ORDER_SIZE_ASC, ORDER_SIZE_DESC

------------------------------------------

-------------------
Constructor Summary
-------------------

MegaApiSwing(java.lang.String appKey, java.lang.String userAgent, java.lang.String path)
Instantiates a new mega api swing.

-----------------------------------------------

--------------
Method Summary
--------------

+----------------------+------------------------------------------------------+
|Modifier and Type     | Method and Description                               |
+======================+======================================================+
|(package private) void| runCallback(java.lang.Runnable runnable)             |
|                      | Run callback.                                        |
+-------------------+---------------------------------------------------------+

------------------------------------------

----------------------------------------------------
Methods inherited from class nz.mega.sdk.MegaApiJava
----------------------------------------------------
addContact, addContact, addEntropy, addGlobalListener, addListener, addRequestListener, addTransferListener, base32ToBase64, base32ToHandle, base64ToBase32, base64ToHandle, cancelGetPreview, cancelGetPreview, cancelGetThumbnail, cancelGetThumbnail, cancelTransfer, cancelTransfer, cancelTransferByTag, cancelTransferByTag, cancelTransfers, cancelTransfers, changeApiUrl, changeApiUrl, changePassword, changePassword, checkAccess, checkMove, confirmAccount, confirmAccount, copyNode, copyNode, copyNode, copyNode, createAccount, createAccount, createFolder, createFolder, disableExport, disableExport, dumpSession, dumpXMPPSession, exportMasterKey, exportNode, exportNode, fastConfirmAccount, fastConfirmAccount, fastCreateAccount, fastCreateAccount, fastLogin, fastLogin, fastLogin, fastLogin, fetchNodes, fetchNodes, getAccess, getAccountDetails, getAccountDetails, getAutoProxySettings, getBase64PwKey, getChildNode, getChildren, getChildren, getContact, getContacts, getExtendedAccountDetails, getExtendedAccountDetails, getExtendedAccountDetails, getExtendedAccountDetails, getExtendedAccountDetails, getFingerprint, getFingerprint, getInboxNode, getIndex, getIndex, getInShares, getInShares, getMyEmail, getNodeByFingerprint, getNodeByFingerprint, getNodeByHandle, getNodeByPath, getNodeByPath, getNodePath, getNumChildFiles, getNumChildFolders, getNumChildren, getNumPendingDownloads, getNumPendingUploads, getOutShares, getOutShares, getParentNode, getPaymentId, getPaymentId, getPreview, getPreview, getPricing, getPricing, getPublicNode, getPublicNode, getRootNode, getRubbishNode, getSize, getStringHash, getThumbnail, getThumbnail, getTotalDownloadedBytes, getTotalDownloads, getTotalUploadedBytes, getTotalUploads, getTransferByTag, getTransfers, getTransfers, getUserAgent, getUserAvatar, getUserAvatar, getUserData, getUserData, getUserData, getUserData, getVersion, handleToBase64, hasFingerprint, importFileLink, importFileLink, isLoggedIn, isShared, isWaiting, killSession, killSession, localLogout, localLogout, localToName, log, log, log, login, login, loginToFolder, loginToFolder, logout, logout, moveNode, moveNode, nameToLocal, nodeListToArray, pauseTransfers, pauseTransfers, privateFreeRequestListener, privateFreeTransferListener, processMegaTree, processMegaTree, querySignupLink, querySignupLink, reconnect, remove, remove, removeContact, removeContact, removeGlobalListener, removeListener, removeRecursively, removeRequestListener, removeTransferListener, renameNode, renameNode, reportDebugEvent, reportDebugEvent, resetTotalDownloads, resetTotalUploads, retryPendingConnections, search, search, sendFileToUser, sendFileToUser, setAvatar, setAvatar, setLoggerObject, setLogLevel, setPreview, setPreview, setProxySettings, setThumbnail, setThumbnail, setUploadLimit, setUserAttribute, setUserAttribute, share, share, share, share, shareListToArray, startDownload, startDownload, startStreaming, startUnbufferedDownload, startUnbufferedDownload, startUpload, startUpload, startUpload, startUpload, startUpload, startUpload, startUpload, startUpload, submitFeedback, submitFeedback, submitPurchaseReceipt, submitPurchaseReceipt, transferListToArray, update, updateStats, userHandleToBase64, userListToArray

--------------------------------------------------

---------------------------------------------
Methods inherited from class java.lang.Object
---------------------------------------------
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

----------------------------------------------------

------------------
Constructor Detail
------------------

MegaApiSwing
public MegaApiSwing(java.lang.String appKey,java.lang.String userAgent,java.lang.String path)
Instantiates a new mega api swing.
Parameters:
appKey - the app key
userAgent - the user agent
path - the path

---------------------------------------------

-------------
Method Detail
-------------

runCallback
void runCallback(java.lang.Runnable runnable)
Run callback.
Overrides:
runCallback in class MegaApiJava
Parameters:
runnable - the runnable
