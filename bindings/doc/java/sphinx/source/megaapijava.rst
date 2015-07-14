===========
MegaApiJava
===========

java.lang.Object
nz.mega.sdk.MegaApiJava
Direct Known Subclasses:
MegaApiSwing

--------------------------

public class MegaApiJava
extends java.lang.Object
Allows to control a MEGA account or a shared folder You must provide an appKey to use this SDK. You can generate an appKey for your app for free here: - https://mega.co.nz/#sdk You can enable local node caching by passing a local path in the constructor of this class. That saves many data usage and many time starting your app because the entire filesystem won't have to be downloaded each time. The persistent node cache will only be loaded by logging in with a session key. To take advantage of this feature, apart of passing the local path to the constructor, your application have to save the session key after login (MegaApi::dumpSession) and use it to log in the next time. This is highly recommended also to enhance the security, because in this was the access password doesn't have to be stored by the application. To access MEGA using this SDK, you have to create an object of this class and use one of the MegaApi::login options (to log in to a MEGA account or a public folder). If the login request succeed, call MegaApi::fetchNodes to get the filesystem in MEGA. After that, you can use all other requests, manage the files and start transfers. After using MegaApi::logout you can reuse the same MegaApi object to log in to another MEGA account or a public folder.

-----------------------------------

-------------
Field Summary
-------------

+----------------------------------+---------------------------------------------------------------------------------------------------+
|Modifier and Type	           |             Field and Description                                                                 |
+==================================+===================================================================================================+
|(package private) static          |              java.util.Set<DelegateMegaGlobalListener>	activeGlobalListeners                  |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|(package private) static          |              java.util.Set<DelegateMegaListener>	        activeMegaListeners                    |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|(package private) static          |              java.util.Set<DelegateMegaTreeProcessor>	activeMegaTreeProcessors               |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|(package private) static          |              java.util.Set<DelegateMegaRequestListener>	activeRequestListeners                 |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|(package private) static          |              java.util.Set<DelegateMegaTransferListener>	activeTransferListeners                |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             EVENT_DEBUG                                                                           |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             EVENT_FEEDBACK                                                                        |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             EVENT_INVALID                                                                         |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|(package private) MegaGfxProcessor|	        gfxProcessor                                                                           |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             LOG_LEVEL_DEBUG                                                                       |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             LOG_LEVEL_ERROR                                                                       |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             LOG_LEVEL_FATAL                                                                       |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             LOG_LEVEL_INFO                                                                        |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             LOG_LEVEL_MAX                                                                         |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             LOG_LEVEL_WARNING                                                                     |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|(package private) static          |             DelegateMegaLogger  logger                                                            |
|                                  |              The logger.                                                                          |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|(package private) MegaApi	   |             megaApi                                                                               |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_ALPHABETICAL_ASC                                                                |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_ALPHABETICAL_DESC                                                               |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_CREATION_ASC                                                                    |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_CREATION_DESC                                                                   |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_DEFAULT_ASC                                                                     |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_DEFAULT_DESC                                                                    |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_MODIFICATION_ASC                                                                |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_MODIFICATION_DESC                                                               |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_NONE                                                                            |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_SIZE_ASC                                                                        |
+----------------------------------+---------------------------------------------------------------------------------------------------+
|static int	                   |             ORDER_SIZE_DESC                                                                       |
+----------------------------------+---------------------------------------------------------------------------------------------------+

-------------------------------

-------------------
Constructor Summary
-------------------

~~~~~~~~~~~~~~~~~~~~~~~~~~~
Constructor and Description
~~~~~~~~~~~~~~~~~~~~~~~~~~~

MegaApiJava(java.lang.String appKey)
Constructor suitable for most applications

MegaApiJava(java.lang.String appKey, java.lang.String basePath)
Constructor suitable for most applications

MegaApiJava(java.lang.String appKey, java.lang.String userAgent, java.lang.String basePath, MegaGfxProcessor gfxProcessor)
MegaApi Constructor that allows to use a custom GFX processor The SDK attach thumbnails and previews to all uploaded images.

-------------------------------------------------------------

--------------
Method Summary
--------------


Modifier and Type	Method and Description
void	addContact(java.lang.String email)
Add a new contact to the MEGA account
void	addContact(java.lang.String email, MegaRequestListenerInterface listener)
Add a new contact to the MEGA account The associated request type with this request is MegaRequest::TYPE_ADD_CONTACT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the email of the contact
static void	addEntropy(java.lang.String data, long size)
Add entropy to internal random number generators It's recommended to call this function with random data specially to enhance security,
void	addGlobalListener(MegaGlobalListenerInterface listener)
Register a listener to receive global events You can use MegaApi::removeGlobalListener to stop receiving events.
void	addListener(MegaListenerInterface listener)
Register a listener to receive all events (requests, transfers, global, synchronization) You can use MegaApi::removeListener to stop receiving events.
void	addRequestListener(MegaRequestListenerInterface listener)
Register a listener to receive all events about requests You can use MegaApi::removeRequestListener to stop receiving events.
void	addTransferListener(MegaTransferListenerInterface listener)
Register a listener to receive all events about transfers You can use MegaApi::removeTransferListener to stop receiving events.
static java.lang.String	base32ToBase64(java.lang.String base32)
Convert a Base32 string to Base64 If the input pointer is NULL, this function will return NULL.
static long	base32ToHandle(java.lang.String base32Handle)
Converts a Base32-encoded user handle (JID) to a MegaHandle
static java.lang.String	base64ToBase32(java.lang.String base64)
Convert a Base64 string to Base32 If the input pointer is NULL, this function will return NULL.
static long	base64ToHandle(java.lang.String base64Handle)
Converts a Base64-encoded node handle to a MegaHandle The returned value can be used to recover a MegaNode using MegaApi::getNodeByHandle You can revert this operation using MegaApi::handleToBase64
void	cancelGetPreview(MegaNode node)
Cancel the retrieval of a preview
void	cancelGetPreview(MegaNode node, MegaRequestListenerInterface listener)
Cancel the retrieval of a preview The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
void	cancelGetThumbnail(MegaNode node)
Cancel the retrieval of a thumbnail
void	cancelGetThumbnail(MegaNode node, MegaRequestListenerInterface listener)
Cancel the retrieval of a thumbnail The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
void	cancelTransfer(MegaTransfer transfer)
Cancel a transfer
void	cancelTransfer(MegaTransfer transfer, MegaRequestListenerInterface listener)
Cancel a transfer When a transfer is cancelled, it will finish and will provide the error code MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and MegaListener::onTransferFinish The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
void	cancelTransferByTag(int transferTag)
Cancel the transfer with a specific tag
void	cancelTransferByTag(int transferTag, MegaRequestListenerInterface listener)
Cancel the transfer with a specific tag When a transfer is cancelled, it will finish and will provide the error code MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and MegaListener::onTransferFinish The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
void	cancelTransfers(int direction)
Cancel all transfers of the same type
void	cancelTransfers(int direction, MegaRequestListenerInterface listener)
Cancel all transfers of the same type The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFERS Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParamType - Returns the first parameter
void	changeApiUrl(java.lang.String apiURL) 
void	changeApiUrl(java.lang.String apiURL, boolean disablepkp) 
void	changePassword(java.lang.String oldPassword, java.lang.String newPassword)
Change the password of the MEGA account
void	changePassword(java.lang.String oldPassword, java.lang.String newPassword, MegaRequestListenerInterface listener)
Change the password of the MEGA account The associated request type with this request is MegaRequest::TYPE_CHANGE_PW Valid data in the MegaRequest object received on callbacks: - MegaRequest::getPassword - Returns the old password - MegaRequest::getNewPassword - Returns the new password
MegaError	checkAccess(MegaNode node, int level)
Check if a node has an access level
MegaError	checkMove(MegaNode node, MegaNode target)
Check if a node can be moved to a target node
void	confirmAccount(java.lang.String link, java.lang.String password)
Confirm a MEGA account using a confirmation link and the user password
void	confirmAccount(java.lang.String link, java.lang.String password, MegaRequestListenerInterface listener)
Confirm a MEGA account using a confirmation link and the user password The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the confirmation link - MegaRequest::getPassword - Returns the password Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getEmail - Email of the account - MegaRequest::getName - Name of the user
void	copyNode(MegaNode node, MegaNode newParent)
Copy a node in the MEGA account
void	copyNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener)
Copy a node in the MEGA account The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to copy - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node - MegaRequest::getPublicMegaNode - Returns the node to copy (if it is a public node) Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new node
void	copyNode(MegaNode node, MegaNode newParent, java.lang.String newName)
Copy a node in the MEGA account changing the file name
void	copyNode(MegaNode node, MegaNode newParent, java.lang.String newName, MegaRequestListenerInterface listener)
Copy a node in the MEGA account changing the file name The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to copy - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node - MegaRequest::getPublicMegaNode - Returns the node to copy - MegaRequest::getName - Returns the name for the new node Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new node
void	createAccount(java.lang.String email, java.lang.String password, java.lang.String name)
Initialize the creation of a new MEGA account
void	createAccount(java.lang.String email, java.lang.String password, java.lang.String name, MegaRequestListenerInterface listener)
Initialize the creation of a new MEGA account The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
void	createFolder(java.lang.String name, MegaNode parent)
Create a folder in the MEGA account
void	createFolder(java.lang.String name, MegaNode parent, MegaRequestListenerInterface listener)
Create a folder in the MEGA account The associated request type with this request is MegaRequest::TYPE_CREATE_FOLDER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParentHandle - Returns the handle of the parent folder - MegaRequest::getName - Returns the name of the new folder Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new folder
void	disableExport(MegaNode node)
Stop sharing a file/folder
void	disableExport(MegaNode node, MegaRequestListenerInterface listener)
Stop sharing a file/folder The associated request type with this request is MegaRequest::TYPE_EXPORT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getAccess - Returns false
java.lang.String	dumpSession()
Returns the current session key You have to be logged in to get a valid session key.
java.lang.String	dumpXMPPSession()
Returns the current XMPP session key You have to be logged in to get a valid session key.
java.lang.String	exportMasterKey()
Export the master key of the account The returned value is a Base64-encoded string With the master key, it's possible to start the recovery of an account when the password is lost: - https://mega.co.nz/#recovery
void	exportNode(MegaNode node)
Generate a public link of a file/folder in MEGA
void	exportNode(MegaNode node, MegaRequestListenerInterface listener)
Generate a public link of a file/folder in MEGA The associated request type with this request is MegaRequest::TYPE_EXPORT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getAccess - Returns true Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getLink - Public link
void	fastConfirmAccount(java.lang.String link, java.lang.String base64pwkey)
Confirm a MEGA account using a confirmation link and a precomputed key
void	fastConfirmAccount(java.lang.String link, java.lang.String base64pwkey, MegaRequestListenerInterface listener)
Confirm a MEGA account using a confirmation link and a precomputed key The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the confirmation link - MegaRequest::getPrivateKey - Returns the base64pwkey parameter Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getEmail - Email of the account - MegaRequest::getName - Name of the user
void	fastCreateAccount(java.lang.String email, java.lang.String base64pwkey, java.lang.String name)
Initialize the creation of a new MEGA account with precomputed keys
void	fastCreateAccount(java.lang.String email, java.lang.String base64pwkey, java.lang.String name, MegaRequestListenerInterface listener)
Initialize the creation of a new MEGA account with precomputed keys The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
void	fastLogin(java.lang.String session)
Log in to a MEGA account using a session key
void	fastLogin(java.lang.String session, MegaRequestListenerInterface listener)
Log in to a MEGA account using a session key The associated request type with this request is MegaRequest::TYPE_LOGIN.
void	fastLogin(java.lang.String email, java.lang.String stringHash, java.lang.String base64pwkey)
Log in to a MEGA account using precomputed keys
void	fastLogin(java.lang.String email, java.lang.String stringHash, java.lang.String base64pwkey, MegaRequestListenerInterface listener)
Log in to a MEGA account using precomputed keys The associated request type with this request is MegaRequest::TYPE_LOGIN.
void	fetchNodes()
Fetch the filesystem in MEGA The MegaApi object must be logged in in an account or a public folder to successfully complete this request.
void	fetchNodes(MegaRequestListenerInterface listener)
Fetch the filesystem in MEGA The MegaApi object must be logged in in an account or a public folder to successfully complete this request.
int	getAccess(MegaNode node)
Get the access level of a MegaNode
void	getAccountDetails()
Get details about the MEGA account
void	getAccountDetails(MegaRequestListenerInterface listener)
Get details about the MEGA account The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getMegaAccountDetails - Details of the MEGA account
MegaProxy	getAutoProxySettings()
Try to detect the system's proxy settings Automatic proxy detection is currently supported on Windows only.
java.lang.String	getBase64PwKey(java.lang.String password)
Generates a private key based on the access password This is a time consuming operation (specially for low-end mobile devices).
MegaNode	getChildNode(MegaNode parent, java.lang.String name)
Get the child node with the provided name If the node doesn't exist, this function returns NULL
java.util.ArrayList<MegaNode>	getChildren(MegaNode parent)
Get all children of a MegaNode If the parent node doesn't exist or it isn't a folder, this function returns NULL
java.util.ArrayList<MegaNode>	getChildren(MegaNode parent, int order)
Get all children of a MegaNode If the parent node doesn't exist or it isn't a folder, this function returns NULL
MegaUser	getContact(java.lang.String email)
Get the MegaUser that has a specific email address You can get the email of a MegaUser using MegaUser::getEmail
java.util.ArrayList<MegaUser>	getContacts()
Get all contacts of this MEGA account
void	getExtendedAccountDetails()
Get details about the MEGA account
void	getExtendedAccountDetails(boolean sessions)
Get details about the MEGA account This function allows to optionally get data about sessions related to the account.
void	getExtendedAccountDetails(boolean sessions, boolean purchases)
Get details about the MEGA account This function allows to optionally get data about sessions and purchases related to the account.
void	getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions)
Get details about the MEGA account This function allows to optionally get data about sessions, transactions and purchases related to the account.
void	getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions, MegaRequestListenerInterface listener)
Get details about the MEGA account This function allows to optionally get data about sessions, transactions and purchases related to the account.
java.lang.String	getFingerprint(MegaNode node)
Get a Base64-encoded fingerprint for a node If the node doesn't exist or doesn't have a fingerprint, this function returns null
java.lang.String	getFingerprint(java.lang.String filePath)
Get a Base64-encoded fingerprint for a local file The fingerprint is created taking into account the modification time of the file and file contents.
MegaNode	getInboxNode()
Returns the inbox node of the account If you haven't successfully called MegaApi::fetchNodes before, this function returns null
int	getIndex(MegaNode node)
Get the current index of the node in the parent folder If the node doesn't exist or it doesn't have a parent node (because it's a root node) this function returns -1
int	getIndex(MegaNode node, int order)
Get the current index of the node in the parent folder for a specific sorting order If the node doesn't exist or it doesn't have a parent node (because it's a root node) this function returns -1
java.util.ArrayList<MegaNode>	getInShares()
Get a list with all inboud sharings
java.util.ArrayList<MegaNode>	getInShares(MegaUser user)
Get a list with all inbound sharings from one MegaUser
java.lang.String	getMyEmail()
Retuns the email of the currently open account If the MegaApi object isn't logged in or the email isn't available, this function returns NULL
MegaNode	getNodeByFingerprint(java.lang.String fingerprint)
Returns a node with the provided fingerprint If there isn't any node in the account with that fingerprint, this function returns null.
MegaNode	getNodeByFingerprint(java.lang.String fingerprint, MegaNode preferredParent) 
MegaNode	getNodeByHandle(long handle)
Get the MegaNode that has a specific handle You can get the handle of a MegaNode using MegaNode::getHandle.
MegaNode	getNodeByPath(java.lang.String path)
Get the MegaNode in a specific path in the MEGA account The path separator character is '/' The Inbox root node is //in/ The Rubbish root node is //bin/ Paths with names containing '/', '\' or ':' aren't compatible with this function.
MegaNode	getNodeByPath(java.lang.String path, MegaNode baseFolder)
Get the MegaNode in a specific path in the MEGA account The path separator character is '/' The Inbox root node is //in/ The Rubbish root node is //bin/ Paths with names containing '/', '\' or ':' aren't compatible with this function.
java.lang.String	getNodePath(MegaNode node)
Get the path of a MegaNode If the node doesn't exist, this function returns NULL.
int	getNumChildFiles(MegaNode parent)
Get the number of child files of a node If the node doesn't exist in MEGA or isn't a folder, this function returns 0 This function doesn't search recursively, only returns the direct child files.
int	getNumChildFolders(MegaNode parent)
Get the number of child folders of a node If the node doesn't exist in MEGA or isn't a folder, this function returns 0 This function doesn't search recursively, only returns the direct child folders.
int	getNumChildren(MegaNode parent)
Get the number of child nodes If the node doesn't exist in MEGA or isn't a folder, this function returns 0 This function doesn't search recursively, only returns the direct child nodes.
int	getNumPendingDownloads()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
int	getNumPendingUploads()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
java.util.ArrayList<MegaShare>	getOutShares()
Get a list with all active outbound sharings
java.util.ArrayList<MegaShare>	getOutShares(MegaNode node)
Get a list with the active outbound sharings for a MegaNode If the node doesn't exist in the account, this function returns an empty list.
MegaNode	getParentNode(MegaNode node)
Get the parent node of a MegaNode If the node doesn't exist in the account or it is a root node, this function returns NULL
void	getPaymentId(long productHandle)
Get the payment URL for an upgrade
void	getPaymentId(long productHandle, MegaRequestListenerInterface listener)
Get the payment id for an upgrade The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the product Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getLink - Payment link
void	getPreview(MegaNode node, java.lang.String dstFilePath)
Get the preview of a node If the node doesn't have a preview the request fails with the MegaError::API_ENOENT error code
void	getPreview(MegaNode node, java.lang.String dstFilePath, MegaRequestListenerInterface listener)
Get the preview of a node If the node doesn't have a preview the request fails with the MegaError::API_ENOENT error code The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the destination path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
void	getPricing()
Get the available pricing plans to upgrade a MEGA account You can get a payment URL for any of the pricing plans provided by this function using MegaApi::getPaymentUrl
void	getPricing(MegaRequestListenerInterface listener)
Get the available pricing plans to upgrade a MEGA account You can get a payment URL for any of the pricing plans provided by this function using MegaApi::getPaymentUrl The associated request type with this request is MegaRequest::TYPE_GET_PRICING Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getPricing - MegaPricing object with all pricing plans
void	getPublicNode(java.lang.String megaFileLink)
Get a MegaNode from a public link to a file A public node can be imported using MegaApi::copy or downloaded using MegaApi::startDownload
void	getPublicNode(java.lang.String megaFileLink, MegaRequestListenerInterface listener)
Get a MegaNode from a public link to a file A public node can be imported using MegaApi::copy or downloaded using MegaApi::startDownload The associated request type with this request is MegaRequest::TYPE_GET_PUBLIC_NODE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the public link to the file Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getPublicMegaNode - Public MegaNode corresponding to the public link
MegaNode	getRootNode()
Returns the root node of the account If you haven't successfully called MegaApi::fetchNodes before, this function returns null
MegaNode	getRubbishNode()
Returns the rubbish node of the account If you haven't successfully called MegaApi::fetchNodes before, this function returns null
long	getSize(MegaNode node)
Get the size of a node tree If the MegaNode is a file, this function returns the size of the file.
java.lang.String	getStringHash(java.lang.String base64pwkey, java.lang.String inBuf)
Generates a hash based in the provided private key and email This is a time consuming operation (specially for low-end mobile devices).
void	getThumbnail(MegaNode node, java.lang.String dstFilePath)
Get the thumbnail of a node If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT error code
void	getThumbnail(MegaNode node, java.lang.String dstFilePath, MegaRequestListenerInterface listener)
Get the thumbnail of a node If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT error code The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the destination path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
long	getTotalDownloadedBytes()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
int	getTotalDownloads()
Deprecated. 
Function related to statistics will be reviewed in future updates. They could change or be removed in the current form.
long	getTotalUploadedBytes()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
int	getTotalUploads()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
MegaTransfer	getTransferByTag(int transferTag)
Get the transfer with a transfer tag That tag can be got using MegaTransfer::getTag
java.util.ArrayList<MegaTransfer>	getTransfers()
Get all active transfers
java.util.ArrayList<MegaTransfer>	getTransfers(int type)
Get all active transfers based on the type
java.lang.String	getUserAgent()
Get the User-Agent header used by the SDK
void	getUserAvatar(MegaUser user, java.lang.String dstFilePath)
Get the avatar of a MegaUser
void	getUserAvatar(MegaUser user, java.lang.String dstFilePath, MegaRequestListenerInterface listener)
Get the avatar of a MegaUser The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getFile - Returns the destination path - MegaRequest::getEmail - Returns the email of the user
void	getUserData()
Get data about the logged account
void	getUserData(MegaRequestListenerInterface listener)
Get data about the logged account The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
void	getUserData(MegaUser user)
Get data about a contact
void	getUserData(MegaUser user, MegaRequestListenerInterface listener)
Get data about a contact The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
java.lang.String	getVersion()
Get the SDK version
static java.lang.String	handleToBase64(long handle)
Converts a MegaHandle to a Base64-encoded string You can revert this operation using MegaApi::base64ToHandle
boolean	hasFingerprint(java.lang.String fingerprint)
Check if the account already has a node with the provided fingerprint A fingerprint for a local file can be generated using MegaApi::getFingerprint
void	importFileLink(java.lang.String megaFileLink, MegaNode parent)
Import a public link to the account
void	importFileLink(java.lang.String megaFileLink, MegaNode parent, MegaRequestListenerInterface listener)
Import a public link to the account The associated request type with this request is MegaRequest::TYPE_IMPORT_LINK Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the public link to the file - MegaRequest::getParentHandle - Returns the folder that receives the imported file Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new node in the account
int	isLoggedIn()
Check if the MegaApi object is logged in
boolean	isShared(MegaNode node)
Check if a MegaNode is being shared For nodes that are being shared, you can get a a list of MegaShare objects using MegaApi::getOutShares
boolean	isWaiting()
Check if the SDK is waiting for the server
void	killSession(long sessionHandle)
Close a MEGA session All clients using this session will be automatically logged out.
void	killSession(long sessionHandle, MegaRequestListenerInterface listener)
Close a MEGA session All clients using this session will be automatically logged out.
void	localLogout()
Logout of the MEGA account without invalidating the session
void	localLogout(MegaRequestListenerInterface listener) 
java.lang.String	localToName(java.lang.String localName) 
static void	log(int logLevel, java.lang.String message)
Send a log to the logging system This log will be received by the active logger object (MegaApi::setLoggerObject) if the log level is the same or lower than the active log level (MegaApi::setLogLevel)
static void	log(int logLevel, java.lang.String message, java.lang.String filename)
Send a log to the logging system This log will be received by the active logger object (MegaApi::setLoggerObject) if the log level is the same or lower than the active log level (MegaApi::setLogLevel)
static void	log(int logLevel, java.lang.String message, java.lang.String filename, int line)
Send a log to the logging system This log will be received by the active logger object (MegaApi::setLoggerObject) if the log level is the same or lower than the active log level (MegaApi::setLogLevel)
void	login(java.lang.String email, java.lang.String password)
Log in to a MEGA account
void	login(java.lang.String email, java.lang.String password, MegaRequestListenerInterface listener)
Log in to a MEGA account The associated request type with this request is MegaRequest::TYPE_LOGIN.
void	loginToFolder(java.lang.String megaFolderLink)
Log in to a public folder using a folder link After a successful login, you should call MegaApi::fetchNodes to get filesystem and start working with the folder.
void	loginToFolder(java.lang.String megaFolderLink, MegaRequestListenerInterface listener)
Log in to a public folder using a folder link After a successful login, you should call MegaApi::fetchNodes to get filesystem and start working with the folder.
void	logout()
Logout of the MEGA account
void	logout(MegaRequestListenerInterface listener)
Logout of the MEGA account The associated request type with this request is MegaRequest::TYPE_LOGOUT
void	moveNode(MegaNode node, MegaNode newParent)
Move a node in the MEGA account
void	moveNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener)
Move a node in the MEGA account The associated request type with this request is MegaRequest::TYPE_MOVE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to move - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
java.lang.String	nameToLocal(java.lang.String name)
Make a name suitable for a file name in the local filesystem This function escapes (%xx) forbidden characters in the local filesystem if needed.
(package private) static java.util.ArrayList<MegaNode>	nodeListToArray(MegaNodeList nodeList) 
void	pauseTransfers(boolean pause)
Pause/resume all transfers
void	pauseTransfers(boolean pause, MegaRequestListenerInterface listener)
Pause/resume all transfers The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS Valid data in the MegaRequest object received on callbacks: - MegaRequest::getFlag - Returns the first parameter
(package private) void	privateFreeRequestListener(DelegateMegaRequestListener listener) 
(package private) void	privateFreeTransferListener(DelegateMegaTransferListener listener) 
boolean	processMegaTree(MegaNode parent, MegaTreeProcessorInterface processor)
Process a node tree using a MegaTreeProcessor implementation
boolean	processMegaTree(MegaNode parent, MegaTreeProcessorInterface processor, boolean recursive)
Process a node tree using a MegaTreeProcessor implementation
void	querySignupLink(java.lang.String link)
Get information about a confirmation link
void	querySignupLink(java.lang.String link, MegaRequestListenerInterface listener)
Get information about a confirmation link The associated request type with this request is MegaRequest::TYPE_QUERY_SIGNUP_LINK.
void	reconnect()
Reconnect and retry also transfers
void	remove(MegaNode node)
Remove a node from the MEGA account
void	remove(MegaNode node, MegaRequestListenerInterface listener)
Remove a node from the MEGA account This function doesn't move the node to the Rubbish Bin, it fully removes the node.
void	removeContact(MegaUser user)
Remove a contact to the MEGA account
void	removeContact(MegaUser user, MegaRequestListenerInterface listener)
Remove a contact to the MEGA account The associated request type with this request is MegaRequest::TYPE_REMOVE_CONTACT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the email of the contact
void	removeGlobalListener(MegaGlobalListenerInterface listener)
Unregister a MegaGlobalListener This listener won't receive more events.
void	removeListener(MegaListenerInterface listener)
Unregister a listener This listener won't receive more events.
static void	removeRecursively(java.lang.String localPath) 
void	removeRequestListener(MegaRequestListenerInterface listener)
Unregister a MegaRequestListener This listener won't receive more events.
void	removeTransferListener(MegaTransferListenerInterface listener)
Unregister a MegaTransferListener This listener won't receive more events.
void	renameNode(MegaNode node, java.lang.String newName)
Rename a node in the MEGA account
void	renameNode(MegaNode node, java.lang.String newName, MegaRequestListenerInterface listener)
Rename a node in the MEGA account The associated request type with this request is MegaRequest::TYPE_RENAME Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to rename - MegaRequest::getName - Returns the new name for the node
void	reportDebugEvent(java.lang.String text)
Deprecated. 
This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
void	reportDebugEvent(java.lang.String text, MegaRequestListenerInterface listener)
Deprecated. 
This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
void	resetTotalDownloads()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
void	resetTotalUploads()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
void	retryPendingConnections()
Retry all pending requests When requests fails they wait some time before being retried.
(package private) void	runCallback(java.lang.Runnable runnable) 
java.util.ArrayList<MegaNode>	search(MegaNode parent, java.lang.String searchString)
Search nodes containing a search string in their name The search is case-insensitive.
java.util.ArrayList<MegaNode>	search(MegaNode parent, java.lang.String searchString, boolean recursive)
Search nodes containing a search string in their name The search is case-insensitive.
void	sendFileToUser(MegaNode node, MegaUser user)
Send a node to the Inbox of another MEGA user using a MegaUser
void	sendFileToUser(MegaNode node, MegaUser user, MegaRequestListenerInterface listener)
Send a node to the Inbox of another MEGA user using a MegaUser The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to send - MegaRequest::getEmail - Returns the email of the user that receives the node
void	setAvatar(java.lang.String srcFilePath)
Set the avatar of the MEGA account
void	setAvatar(java.lang.String srcFilePath, MegaRequestListenerInterface listener)
Set the avatar of the MEGA account The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getFile - Returns the source path
static void	setLoggerObject(MegaLoggerInterface megaLogger)
Set a MegaLogger implementation to receive SDK logs Logs received by this objects depends on the active log level.
static void	setLogLevel(int logLevel)
Set the active log level This function sets the log level of the logging system.
void	setPreview(MegaNode node, java.lang.String srcFilePath)
Set the preview of a MegaNode
void	setPreview(MegaNode node, java.lang.String srcFilePath, MegaRequestListenerInterface listener)
Set the preview of a MegaNode The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the source path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
void	setProxySettings(MegaProxy proxySettings)
Set proxy settings The SDK will start using the provided proxy settings as soon as this function returns.
void	setThumbnail(MegaNode node, java.lang.String srcFilePath)
Set the thumbnail of a MegaNode
void	setThumbnail(MegaNode node, java.lang.String srcFilePath, MegaRequestListenerInterface listener)
Set the thumbnail of a MegaNode The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the source path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
void	setUploadLimit(int bpslimit)
Set the upload speed limit The limit will be applied on the server side when starting a transfer.
void	setUserAttribute(int type, java.lang.String value)
Set an attribute of the current user
void	setUserAttribute(int type, java.lang.String value, MegaRequestListenerInterface listener)
Set an attribute of the current user The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParamType - Returns the attribute type - MegaRequest::getFile - Returns the new value for the attribute
void	share(MegaNode node, MegaUser user, int level)
Share or stop sharing a folder in MEGA with another user using a MegaUser To share a folder with an user, set the desired access level in the level parameter.
void	share(MegaNode node, MegaUser user, int level, MegaRequestListenerInterface listener)
Share or stop sharing a folder in MEGA with another user using a MegaUser To share a folder with an user, set the desired access level in the level parameter.
void	share(MegaNode node, java.lang.String email, int level)
Share or stop sharing a folder in MEGA with another user using his email To share a folder with an user, set the desired access level in the level parameter.
void	share(MegaNode node, java.lang.String email, int level, MegaRequestListenerInterface listener)
Share or stop sharing a folder in MEGA with another user using his email To share a folder with an user, set the desired access level in the level parameter.
(package private) static java.util.ArrayList<MegaShare>	shareListToArray(MegaShareList shareList) 
void	startDownload(MegaNode node, java.lang.String localPath)
Download a file from MEGA
void	startDownload(MegaNode node, java.lang.String localPath, MegaTransferListenerInterface listener)
Download a file from MEGA
void	startStreaming(MegaNode node, long startPos, long size, MegaTransferListenerInterface listener)
Start an streaming download Streaming downloads don't save the downloaded data into a local file.
void	startUnbufferedDownload(MegaNode node, long startOffset, long size, java.io.OutputStream outputStream, MegaTransferListenerInterface listener) 
void	startUnbufferedDownload(MegaNode node, java.io.OutputStream outputStream, MegaTransferListenerInterface listener) 
void	startUpload(java.lang.String localPath, MegaNode parent)
Upload a file
void	startUpload(java.lang.String localPath, MegaNode parent, long mtime)
Upload a file with a custom modification time
void	startUpload(java.lang.String localPath, MegaNode parent, long mtime, MegaTransferListenerInterface listener)
Upload a file with a custom modification time
void	startUpload(java.lang.String localPath, MegaNode parent, MegaTransferListenerInterface listener)
Upload a file
void	startUpload(java.lang.String localPath, MegaNode parent, java.lang.String fileName)
Upload a file with a custom name
void	startUpload(java.lang.String localPath, MegaNode parent, java.lang.String fileName, long mtime)
Upload a file with a custom name and a custom modification time
void	startUpload(java.lang.String localPath, MegaNode parent, java.lang.String fileName, long mtime, MegaTransferListenerInterface listener)
Upload a file with a custom name and a custom modification time
void	startUpload(java.lang.String localPath, MegaNode parent, java.lang.String fileName, MegaTransferListenerInterface listener)
Upload a file with a custom name
void	submitFeedback(int rating, java.lang.String comment)
Deprecated. 
This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
void	submitFeedback(int rating, java.lang.String comment, MegaRequestListenerInterface listener)
Deprecated. 
This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
void	submitPurchaseReceipt(java.lang.String receipt)
Send the Google Play receipt after a correct purchase of a subscription
void	submitPurchaseReceipt(java.lang.String receipt, MegaRequestListenerInterface listener)
Send the Google Play receipt after a correct purchase of a subscription
(package private) static java.util.ArrayList<MegaTransfer>	transferListToArray(MegaTransferList transferList) 
void	update()
Deprecated. 
This function is only here for debugging purposes. It will probably be removed in future updates
void	updateStats()
Deprecated. 
Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
static java.lang.String	userHandleToBase64(long handle)
Converts a MegaHandle to a Base64-encoded string You take the ownership of the returned value You can revert this operation using MegaApi::base64ToHandle
(package private) static java.util.ArrayList<MegaUser>	userListToArray(MegaUserList userList)



-----------------------------------------------------

 
Methods inherited from class java.lang.Object
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

--------------------------------------------

------------
Field Detail
------------

megaApi
MegaApi megaApi
gfxProcessor
MegaGfxProcessor gfxProcessor
logger
static DelegateMegaLogger logger
The logger.
activeRequestListeners
static java.util.Set<DelegateMegaRequestListener> activeRequestListeners
activeTransferListeners
static java.util.Set<DelegateMegaTransferListener> activeTransferListeners
activeGlobalListeners
static java.util.Set<DelegateMegaGlobalListener> activeGlobalListeners
activeMegaListeners
static java.util.Set<DelegateMegaListener> activeMegaListeners
activeMegaTreeProcessors
static java.util.Set<DelegateMegaTreeProcessor> activeMegaTreeProcessors
ORDER_NONE
public static final int ORDER_NONE
ORDER_DEFAULT_ASC
public static final int ORDER_DEFAULT_ASC
ORDER_DEFAULT_DESC
public static final int ORDER_DEFAULT_DESC
ORDER_SIZE_ASC
public static final int ORDER_SIZE_ASC
ORDER_SIZE_DESC
public static final int ORDER_SIZE_DESC
ORDER_CREATION_ASC
public static final int ORDER_CREATION_ASC
ORDER_CREATION_DESC
public static final int ORDER_CREATION_DESC
ORDER_MODIFICATION_ASC
public static final int ORDER_MODIFICATION_ASC
ORDER_MODIFICATION_DESC
public static final int ORDER_MODIFICATION_DESC
ORDER_ALPHABETICAL_ASC
public static final int ORDER_ALPHABETICAL_ASC
ORDER_ALPHABETICAL_DESC
public static final int ORDER_ALPHABETICAL_DESC
LOG_LEVEL_FATAL
public static final int LOG_LEVEL_FATAL
See Also:
Constant Field Values
LOG_LEVEL_ERROR
public static final int LOG_LEVEL_ERROR
See Also:
Constant Field Values
LOG_LEVEL_WARNING
public static final int LOG_LEVEL_WARNING
See Also:
Constant Field Values
LOG_LEVEL_INFO
public static final int LOG_LEVEL_INFO
See Also:
Constant Field Values
LOG_LEVEL_DEBUG
public static final int LOG_LEVEL_DEBUG
See Also:
Constant Field Values
LOG_LEVEL_MAX
public static final int LOG_LEVEL_MAX
See Also:
Constant Field Values
EVENT_FEEDBACK
public static final int EVENT_FEEDBACK
See Also:
Constant Field Values
EVENT_DEBUG
public static final int EVENT_DEBUG
See Also:
Constant Field Values
EVENT_INVALID
public static final int EVENT_INVALID
See Also:
Constant Field Values
Constructor Detail

MegaApiJava
public MegaApiJava(java.lang.String appKey,
                   java.lang.String basePath)
Constructor suitable for most applications
Parameters:
appKey - AppKey of your application You can generate your AppKey for free here: - https://mega.co.nz/#sdk
basePath - Base path to store the local cache If you pass NULL to this parameter, the SDK won't use any local cache.
MegaApiJava
public MegaApiJava(java.lang.String appKey,
                   java.lang.String userAgent,
                   java.lang.String basePath,
                   MegaGfxProcessor gfxProcessor)
MegaApi Constructor that allows to use a custom GFX processor The SDK attach thumbnails and previews to all uploaded images. To generate them, it needs a graphics processor. You can build the SDK with one of the provided built-in graphics processors. If none of them is available in your app, you can implement the MegaGfxProcessor interface to provide your custom processor. Please read the documentation of MegaGfxProcessor carefully to ensure that your implementation is valid.
Parameters:
appKey - AppKey of your application You can generate your AppKey for free here: - https://mega.co.nz/#sdk
userAgent - User agent to use in network requests If you pass NULL to this parameter, a default user agent will be used
basePath - Base path to store the local cache If you pass NULL to this parameter, the SDK won't use any local cache.
gfxProcessor - Image processor. The SDK will use it to generate previews and thumbnails If you pass NULL to this parameter, the SDK will try to use the built-in image processors.
MegaApiJava
public MegaApiJava(java.lang.String appKey)
Constructor suitable for most applications
Parameters:
appKey - AppKey of your application You can generate your AppKey for free here: - https://mega.co.nz/#sdk
Method Detail

runCallback
void runCallback(java.lang.Runnable runnable)
addListener
public void addListener(MegaListenerInterface listener)
Register a listener to receive all events (requests, transfers, global, synchronization) You can use MegaApi::removeListener to stop receiving events.
Parameters:
listener - Listener that will receive all events (requests, transfers, global, synchronization)
addRequestListener
public void addRequestListener(MegaRequestListenerInterface listener)
Register a listener to receive all events about requests You can use MegaApi::removeRequestListener to stop receiving events.
Parameters:
listener - Listener that will receive all events about requests
addTransferListener
public void addTransferListener(MegaTransferListenerInterface listener)
Register a listener to receive all events about transfers You can use MegaApi::removeTransferListener to stop receiving events.
Parameters:
listener - Listener that will receive all events about transfers
addGlobalListener
public void addGlobalListener(MegaGlobalListenerInterface listener)
Register a listener to receive global events You can use MegaApi::removeGlobalListener to stop receiving events.
Parameters:
listener - Listener that will receive global events
removeListener
public void removeListener(MegaListenerInterface listener)
Unregister a listener This listener won't receive more events.
Parameters:
listener - Object that is unregistered
removeRequestListener
public void removeRequestListener(MegaRequestListenerInterface listener)
Unregister a MegaRequestListener This listener won't receive more events.
Parameters:
listener - Object that is unregistered
removeTransferListener
public void removeTransferListener(MegaTransferListenerInterface listener)
Unregister a MegaTransferListener This listener won't receive more events.
Parameters:
listener - Object that is unregistered
removeGlobalListener
public void removeGlobalListener(MegaGlobalListenerInterface listener)
Unregister a MegaGlobalListener This listener won't receive more events.
Parameters:
listener - Object that is unregistered
getBase64PwKey
public java.lang.String getBase64PwKey(java.lang.String password)
Generates a private key based on the access password This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is required to log in, this function allows to do this step in a separate function. You should run this function in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin
Parameters:
password - Access password
Returns:
Base64-encoded private key
getStringHash
public java.lang.String getStringHash(java.lang.String base64pwkey,
                                      java.lang.String inBuf)
Generates a hash based in the provided private key and email This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is required to log in, this function allows to do this step in a separate function. You should run this function in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin
Parameters:
base64pwkey - Private key returned by MegaApi::getBase64PwKey
Returns:
Base64-encoded hash
base32ToHandle
public static long base32ToHandle(java.lang.String base32Handle)
Converts a Base32-encoded user handle (JID) to a MegaHandle
Parameters:
base32Handle - Base32-encoded handle (JID)
Returns:
User handle
base64ToHandle
public static long base64ToHandle(java.lang.String base64Handle)
Converts a Base64-encoded node handle to a MegaHandle The returned value can be used to recover a MegaNode using MegaApi::getNodeByHandle You can revert this operation using MegaApi::handleToBase64
Parameters:
base64Handle - Base64-encoded node handle
Returns:
Node handle
handleToBase64
public static java.lang.String handleToBase64(long handle)
Converts a MegaHandle to a Base64-encoded string You can revert this operation using MegaApi::base64ToHandle
Parameters:
handle - to be converted
Returns:
Base64-encoded node handle
userHandleToBase64
public static java.lang.String userHandleToBase64(long handle)
Converts a MegaHandle to a Base64-encoded string You take the ownership of the returned value You can revert this operation using MegaApi::base64ToHandle
Parameters:
User - handle to be converted
Returns:
Base64-encoded user handle
addEntropy
public static void addEntropy(java.lang.String data,
                              long size)
Add entropy to internal random number generators It's recommended to call this function with random data specially to enhance security,
Parameters:
data - Byte array with random data
size - Size of the byte array (in bytes)
reconnect
public void reconnect()
Reconnect and retry also transfers
Parameters:
listener - MegaRequestListener to track this request
retryPendingConnections
public void retryPendingConnections()
Retry all pending requests When requests fails they wait some time before being retried. That delay grows exponentially if the request fails again. For this reason, and since this request is very lightweight, it's recommended to call it with the default parameters on every user interaction with the application. This will prevent very big delays completing requests.
login
public void login(java.lang.String email,
                  java.lang.String password,
                  MegaRequestListenerInterface listener)
Log in to a MEGA account The associated request type with this request is MegaRequest::TYPE_LOGIN. Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the first parameter - MegaRequest::getPassword - Returns the second parameter If the email/password aren't valid the error code provided in onRequestFinish is MegaError::API_ENOENT.
Parameters:
email - Email of the user
password - Password
listener - MegaRequestListener to track this request
login
public void login(java.lang.String email,
                  java.lang.String password)
Log in to a MEGA account
Parameters:
email - Email of the user
password - Password
loginToFolder
public void loginToFolder(java.lang.String megaFolderLink,
                          MegaRequestListenerInterface listener)
Log in to a public folder using a folder link After a successful login, you should call MegaApi::fetchNodes to get filesystem and start working with the folder. The associated request type with this request is MegaRequest::TYPE_LOGIN. Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Retuns the string "FOLDER" - MegaRequest::getLink - Returns the public link to the folder
Parameters:
Public - link to a folder in MEGA
listener - MegaRequestListener to track this request
loginToFolder
public void loginToFolder(java.lang.String megaFolderLink)
Log in to a public folder using a folder link After a successful login, you should call MegaApi::fetchNodes to get filesystem and start working with the folder.
Parameters:
Public - link to a folder in MEGA
fastLogin
public void fastLogin(java.lang.String email,
                      java.lang.String stringHash,
                      java.lang.String base64pwkey,
                      MegaRequestListenerInterface listener)
Log in to a MEGA account using precomputed keys The associated request type with this request is MegaRequest::TYPE_LOGIN. Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the first parameter - MegaRequest::getPassword - Returns the second parameter - MegaRequest::getPrivateKey - Returns the third parameter If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is MegaError::API_ENOENT.
Parameters:
email - Email of the user
stringHash - Hash of the email returned by MegaApi::getStringHash
base64pwkey - Private key calculated using MegaApi::getBase64PwKey
listener - MegaRequestListener to track this request
fastLogin
public void fastLogin(java.lang.String email,
                      java.lang.String stringHash,
                      java.lang.String base64pwkey)
Log in to a MEGA account using precomputed keys
Parameters:
email - Email of the user
stringHash - Hash of the email returned by MegaApi::getStringHash
base64pwkey - Private key calculated using MegaApi::getBase64PwKey
fastLogin
public void fastLogin(java.lang.String session,
                      MegaRequestListenerInterface listener)
Log in to a MEGA account using a session key The associated request type with this request is MegaRequest::TYPE_LOGIN. Valid data in the MegaRequest object received on callbacks: - MegaRequest::getSessionKey - Returns the session key
Parameters:
session - Session key previously dumped with MegaApi::dumpSession
listener - MegaRequestListener to track this request
fastLogin
public void fastLogin(java.lang.String session)
Log in to a MEGA account using a session key
Parameters:
session - Session key previously dumped with MegaApi::dumpSession
killSession
public void killSession(long sessionHandle,
                        MegaRequestListenerInterface listener)
Close a MEGA session All clients using this session will be automatically logged out. You can get session information using MegaApi::getExtendedAccountDetails. Then use MegaAccountDetails::getNumSessions and MegaAccountDetails::getSession to get session info. MegaAccountSession::getHandle provides the handle that this function needs. If you use mega::INVALID_HANDLE, all sessions except the current one will be closed
Parameters:
Handle - of the session. Use mega::INVALID_HANDLE to cancel all sessions except the current one
listener - MegaRequestListenerInterface to track this request
killSession
public void killSession(long sessionHandle)
Close a MEGA session All clients using this session will be automatically logged out. You can get session information using MegaApi::getExtendedAccountDetails. Then use MegaAccountDetails::getNumSessions and MegaAccountDetails::getSession to get session info. MegaAccountSession::getHandle provides the handle that this function needs. If you use mega::INVALID_HANDLE, all sessions except the current one will be closed
Parameters:
Handle - of the session. Use mega::INVALID_HANDLE to cancel all sessions except the current one
getUserData
public void getUserData(MegaRequestListenerInterface listener)
Get data about the logged account The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA. Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getName - Returns the name of the logged user - MegaRequest::getPassword - Returns the the public RSA key of the account, Base64-encoded - MegaRequest::getPrivateKey - Returns the private RSA key of the account, Base64-encoded
Parameters:
listener - MegaRequestListenerInterface to track this request
getUserData
public void getUserData()
Get data about the logged account
getUserData
public void getUserData(MegaUser user,
                        MegaRequestListenerInterface listener)
Get data about a contact The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA. Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the email of the contact Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getText - Returns the XMPP ID of the contact - MegaRequest::getPassword - Returns the public RSA key of the contact, Base64-encoded
Parameters:
user - Contact to get the data
listener - MegaRequestListenerInterface to track this request
getUserData
public void getUserData(MegaUser user)
Get data about a contact
Parameters:
user - Contact to get the data
dumpSession
public java.lang.String dumpSession()
Returns the current session key You have to be logged in to get a valid session key. Otherwise, this function returns NULL.
Returns:
Current session key
dumpXMPPSession
public java.lang.String dumpXMPPSession()
Returns the current XMPP session key You have to be logged in to get a valid session key. Otherwise, this function returns NULL.
Returns:
Current XMPP session key
createAccount
public void createAccount(java.lang.String email,
                          java.lang.String password,
                          java.lang.String name,
                          MegaRequestListenerInterface listener)
Initialize the creation of a new MEGA account The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT. Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the email for the account - MegaRequest::getPassword - Returns the password for the account - MegaRequest::getName - Returns the name of the user If this request succeed, a confirmation email will be sent to the users. If an account with the same email already exists, you will get the error code MegaError::API_EEXIST in onRequestFinish
Parameters:
email - Email for the account
password - Password for the account
name - Name of the user
listener - MegaRequestListener to track this request
createAccount
public void createAccount(java.lang.String email,
                          java.lang.String password,
                          java.lang.String name)
Initialize the creation of a new MEGA account
Parameters:
email - Email for the account
password - Password for the account
name - Name of the user
fastCreateAccount
public void fastCreateAccount(java.lang.String email,
                              java.lang.String base64pwkey,
                              java.lang.String name,
                              MegaRequestListenerInterface listener)
Initialize the creation of a new MEGA account with precomputed keys The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT. Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the email for the account - MegaRequest::getPrivateKey - Returns the private key calculated with MegaApi::getBase64PwKey - MegaRequest::getName - Returns the name of the user If this request succeed, a confirmation email will be sent to the users. If an account with the same email already exists, you will get the error code MegaError::API_EEXIST in onRequestFinish
Parameters:
email - Email for the account
base64pwkey - Private key calculated with MegaApi::getBase64PwKey
name - Name of the user
listener - MegaRequestListener to track this request
fastCreateAccount
public void fastCreateAccount(java.lang.String email,
                              java.lang.String base64pwkey,
                              java.lang.String name)
Initialize the creation of a new MEGA account with precomputed keys
Parameters:
email - Email for the account
base64pwkey - Private key calculated with MegaApi::getBase64PwKey
name - Name of the user
querySignupLink
public void querySignupLink(java.lang.String link,
                            MegaRequestListenerInterface listener)
Get information about a confirmation link The associated request type with this request is MegaRequest::TYPE_QUERY_SIGNUP_LINK. Valid data in the MegaRequest object received on all callbacks: - MegaRequest::getLink - Returns the confirmation link Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getEmail - Return the email associated with the confirmation link - MegaRequest::getName - Returns the name associated with the confirmation link
Parameters:
link - Confirmation link
listener - MegaRequestListener to track this request
querySignupLink
public void querySignupLink(java.lang.String link)
Get information about a confirmation link
Parameters:
link - Confirmation link
confirmAccount
public void confirmAccount(java.lang.String link,
                           java.lang.String password,
                           MegaRequestListenerInterface listener)
Confirm a MEGA account using a confirmation link and the user password The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the confirmation link - MegaRequest::getPassword - Returns the password Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getEmail - Email of the account - MegaRequest::getName - Name of the user
Parameters:
link - Confirmation link
password - Password for the account
listener - MegaRequestListener to track this request
confirmAccount
public void confirmAccount(java.lang.String link,
                           java.lang.String password)
Confirm a MEGA account using a confirmation link and the user password
Parameters:
link - Confirmation link
password - Password for the account
fastConfirmAccount
public void fastConfirmAccount(java.lang.String link,
                               java.lang.String base64pwkey,
                               MegaRequestListenerInterface listener)
Confirm a MEGA account using a confirmation link and a precomputed key The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the confirmation link - MegaRequest::getPrivateKey - Returns the base64pwkey parameter Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getEmail - Email of the account - MegaRequest::getName - Name of the user
Parameters:
link - Confirmation link
base64pwkey - Private key precomputed with MegaApi::getBase64PwKey
listener - MegaRequestListener to track this request
fastConfirmAccount
public void fastConfirmAccount(java.lang.String link,
                               java.lang.String base64pwkey)
Confirm a MEGA account using a confirmation link and a precomputed key
Parameters:
link - Confirmation link
base64pwkey - Private key precomputed with MegaApi::getBase64PwKey
setProxySettings
public void setProxySettings(MegaProxy proxySettings)
Set proxy settings The SDK will start using the provided proxy settings as soon as this function returns.
Parameters:
Proxy - settings
See Also:
MegaProxy
getAutoProxySettings
public MegaProxy getAutoProxySettings()
Try to detect the system's proxy settings Automatic proxy detection is currently supported on Windows only. On other platforms, this fuction will return a MegaProxy object of type MegaProxy::PROXY_NONE
Returns:
MegaProxy object with the detected proxy settings
isLoggedIn
public int isLoggedIn()
Check if the MegaApi object is logged in
Returns:
0 if not logged in, Otherwise, a number >= 0
getMyEmail
public java.lang.String getMyEmail()
Retuns the email of the currently open account If the MegaApi object isn't logged in or the email isn't available, this function returns NULL
Returns:
Email of the account
setLogLevel
public static void setLogLevel(int logLevel)
Set the active log level This function sets the log level of the logging system. If you set a log listener using MegaApi::setLoggerObject, you will receive logs with the same or a lower level than the one passed to this function.
Parameters:
logLevel - Active log level These are the valid values for this parameter: - MegaApi::LOG_LEVEL_FATAL = 0 - MegaApi::LOG_LEVEL_ERROR = 1 - MegaApi::LOG_LEVEL_WARNING = 2 - MegaApi::LOG_LEVEL_INFO = 3 - MegaApi::LOG_LEVEL_DEBUG = 4 - MegaApi::LOG_LEVEL_MAX = 5
setLoggerObject
public static void setLoggerObject(MegaLoggerInterface megaLogger)
Set a MegaLogger implementation to receive SDK logs Logs received by this objects depends on the active log level. By default, it is MegaApi::LOG_LEVEL_INFO. You can change it using MegaApi::setLogLevel.
Parameters:
megaLogger - MegaLogger implementation
log
public static void log(int logLevel,
                       java.lang.String message,
                       java.lang.String filename,
                       int line)
Send a log to the logging system This log will be received by the active logger object (MegaApi::setLoggerObject) if the log level is the same or lower than the active log level (MegaApi::setLogLevel)
Parameters:
logLevel - Log level for this message
message - Message for the logging system
filename - Origin of the log message
line - Line of code where this message was generated
log
public static void log(int logLevel,
                       java.lang.String message,
                       java.lang.String filename)
Send a log to the logging system This log will be received by the active logger object (MegaApi::setLoggerObject) if the log level is the same or lower than the active log level (MegaApi::setLogLevel)
Parameters:
logLevel - Log level for this message
message - Message for the logging system
filename - Origin of the log message
log
public static void log(int logLevel,
                       java.lang.String message)
Send a log to the logging system This log will be received by the active logger object (MegaApi::setLoggerObject) if the log level is the same or lower than the active log level (MegaApi::setLogLevel)
Parameters:
logLevel - Log level for this message
message - Message for the logging system
createFolder
public void createFolder(java.lang.String name,
                         MegaNode parent,
                         MegaRequestListenerInterface listener)
Create a folder in the MEGA account The associated request type with this request is MegaRequest::TYPE_CREATE_FOLDER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParentHandle - Returns the handle of the parent folder - MegaRequest::getName - Returns the name of the new folder Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new folder
Parameters:
name - Name of the new folder
parent - Parent folder
listener - MegaRequestListener to track this request
createFolder
public void createFolder(java.lang.String name,
                         MegaNode parent)
Create a folder in the MEGA account
Parameters:
name - Name of the new folder
parent - Parent folder
moveNode
public void moveNode(MegaNode node,
                     MegaNode newParent,
                     MegaRequestListenerInterface listener)
Move a node in the MEGA account The associated request type with this request is MegaRequest::TYPE_MOVE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to move - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
Parameters:
node - Node to move
newParent - New parent for the node
listener - MegaRequestListener to track this request
moveNode
public void moveNode(MegaNode node,
                     MegaNode newParent)
Move a node in the MEGA account
Parameters:
node - Node to move
newParent - New parent for the node
copyNode
public void copyNode(MegaNode node,
                     MegaNode newParent,
                     MegaRequestListenerInterface listener)
Copy a node in the MEGA account The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to copy - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node - MegaRequest::getPublicMegaNode - Returns the node to copy (if it is a public node) Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new node
Parameters:
node - Node to copy
newParent - Parent for the new node
listener - MegaRequestListener to track this request
copyNode
public void copyNode(MegaNode node,
                     MegaNode newParent)
Copy a node in the MEGA account
Parameters:
node - Node to copy
newParent - Parent for the new node
copyNode
public void copyNode(MegaNode node,
                     MegaNode newParent,
                     java.lang.String newName,
                     MegaRequestListenerInterface listener)
Copy a node in the MEGA account changing the file name The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to copy - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node - MegaRequest::getPublicMegaNode - Returns the node to copy - MegaRequest::getName - Returns the name for the new node Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new node
Parameters:
node - Node to copy
newParent - Parent for the new node
newName - Name for the new node This parameter is only used if the original node is a file and it isn't a public node, otherwise, it's ignored.
listener - MegaRequestListenerInterface to track this request
copyNode
public void copyNode(MegaNode node,
                     MegaNode newParent,
                     java.lang.String newName)
Copy a node in the MEGA account changing the file name
Parameters:
node - Node to copy
newParent - Parent for the new node
newName - Name for the new node This parameter is only used if the original node is a file and it isn't a public node, otherwise, it's ignored.
renameNode
public void renameNode(MegaNode node,
                       java.lang.String newName,
                       MegaRequestListenerInterface listener)
Rename a node in the MEGA account The associated request type with this request is MegaRequest::TYPE_RENAME Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to rename - MegaRequest::getName - Returns the new name for the node
Parameters:
node - Node to modify
newName - New name for the node
listener - MegaRequestListener to track this request
renameNode
public void renameNode(MegaNode node,
                       java.lang.String newName)
Rename a node in the MEGA account
Parameters:
node - Node to modify
newName - New name for the node
remove
public void remove(MegaNode node,
                   MegaRequestListenerInterface listener)
Remove a node from the MEGA account This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move the node to the Rubbish Bin use MegaApi::moveNode The associated request type with this request is MegaRequest::TYPE_REMOVE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to remove
Parameters:
node - Node to remove
listener - MegaRequestListener to track this request
remove
public void remove(MegaNode node)
Remove a node from the MEGA account
Parameters:
node - Node to remove
sendFileToUser
public void sendFileToUser(MegaNode node,
                           MegaUser user,
                           MegaRequestListenerInterface listener)
Send a node to the Inbox of another MEGA user using a MegaUser The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node to send - MegaRequest::getEmail - Returns the email of the user that receives the node
Parameters:
node - Node to send
user - User that receives the node
listener - MegaRequestListener to track this request
sendFileToUser
public void sendFileToUser(MegaNode node,
                           MegaUser user)
Send a node to the Inbox of another MEGA user using a MegaUser
Parameters:
node - Node to send
user - User that receives the node
share
public void share(MegaNode node,
                  MegaUser user,
                  int level,
                  MegaRequestListenerInterface listener)
Share or stop sharing a folder in MEGA with another user using a MegaUser To share a folder with an user, set the desired access level in the level parameter. If you want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the folder to share - MegaRequest::getEmail - Returns the email of the user that receives the shared folder - MegaRequest::getAccess - Returns the access that is granted to the user
Parameters:
node - The folder to share. It must be a non-root folder
user - User that receives the shared folder
level - Permissions that are granted to the user Valid values for this parameter: - MegaShare::ACCESS_UNKNOWN = -1 Stop sharing a folder with this user - MegaShare::ACCESS_READ = 0 - MegaShare::ACCESS_READWRITE = 1 - MegaShare::ACCESS_FULL = 2 - MegaShare::ACCESS_OWNER = 3
listener - MegaRequestListener to track this request
share
public void share(MegaNode node,
                  MegaUser user,
                  int level)
Share or stop sharing a folder in MEGA with another user using a MegaUser To share a folder with an user, set the desired access level in the level parameter. If you want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
Parameters:
node - The folder to share. It must be a non-root folder
user - User that receives the shared folder
level - Permissions that are granted to the user Valid values for this parameter: - MegaShare::ACCESS_UNKNOWN = -1 Stop sharing a folder with this user - MegaShare::ACCESS_READ = 0 - MegaShare::ACCESS_READWRITE = 1 - MegaShare::ACCESS_FULL = 2 - MegaShare::ACCESS_OWNER = 3
share
public void share(MegaNode node,
                  java.lang.String email,
                  int level,
                  MegaRequestListenerInterface listener)
Share or stop sharing a folder in MEGA with another user using his email To share a folder with an user, set the desired access level in the level parameter. If you want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the folder to share - MegaRequest::getEmail - Returns the email of the user that receives the shared folder - MegaRequest::getAccess - Returns the access that is granted to the user
Parameters:
node - The folder to share. It must be a non-root folder
email - Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway and the user will be invited to register an account.
level - Permissions that are granted to the user Valid values for this parameter: - MegaShare::ACCESS_UNKNOWN = -1 Stop sharing a folder with this user - MegaShare::ACCESS_READ = 0 - MegaShare::ACCESS_READWRITE = 1 - MegaShare::ACCESS_FULL = 2 - MegaShare::ACCESS_OWNER = 3
listener - MegaRequestListener to track this request
share
public void share(MegaNode node,
                  java.lang.String email,
                  int level)
Share or stop sharing a folder in MEGA with another user using his email To share a folder with an user, set the desired access level in the level parameter. If you want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
Parameters:
node - The folder to share. It must be a non-root folder
email - Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway and the user will be invited to register an account.
level - Permissions that are granted to the user Valid values for this parameter: - MegaShare::ACCESS_UNKNOWN = -1 Stop sharing a folder with this user - MegaShare::ACCESS_READ = 0 - MegaShare::ACCESS_READWRITE = 1 - MegaShare::ACCESS_FULL = 2 - MegaShare::ACCESS_OWNER = 3
importFileLink
public void importFileLink(java.lang.String megaFileLink,
                           MegaNode parent,
                           MegaRequestListenerInterface listener)
Import a public link to the account The associated request type with this request is MegaRequest::TYPE_IMPORT_LINK Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the public link to the file - MegaRequest::getParentHandle - Returns the folder that receives the imported file Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getNodeHandle - Handle of the new node in the account
Parameters:
megaFileLink - Public link to a file in MEGA
parent - Parent folder for the imported file
listener - MegaRequestListener to track this request
importFileLink
public void importFileLink(java.lang.String megaFileLink,
                           MegaNode parent)
Import a public link to the account
Parameters:
megaFileLink - Public link to a file in MEGA
parent - Parent folder for the imported file
getPublicNode
public void getPublicNode(java.lang.String megaFileLink,
                          MegaRequestListenerInterface listener)
Get a MegaNode from a public link to a file A public node can be imported using MegaApi::copy or downloaded using MegaApi::startDownload The associated request type with this request is MegaRequest::TYPE_GET_PUBLIC_NODE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getLink - Returns the public link to the file Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getPublicMegaNode - Public MegaNode corresponding to the public link
Parameters:
megaFileLink - Public link to a file in MEGA
listener - MegaRequestListener to track this request
getPublicNode
public void getPublicNode(java.lang.String megaFileLink)
Get a MegaNode from a public link to a file A public node can be imported using MegaApi::copy or downloaded using MegaApi::startDownload
Parameters:
megaFileLink - Public link to a file in MEGA
getThumbnail
public void getThumbnail(MegaNode node,
                         java.lang.String dstFilePath,
                         MegaRequestListenerInterface listener)
Get the thumbnail of a node If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT error code The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the destination path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
Parameters:
node - Node to get the thumbnail
dstFilePath - Destination path for the thumbnail. If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
listener - MegaRequestListener to track this request
getThumbnail
public void getThumbnail(MegaNode node,
                         java.lang.String dstFilePath)
Get the thumbnail of a node If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT error code
Parameters:
node - Node to get the thumbnail
dstFilePath - Destination path for the thumbnail. If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
getPreview
public void getPreview(MegaNode node,
                       java.lang.String dstFilePath,
                       MegaRequestListenerInterface listener)
Get the preview of a node If the node doesn't have a preview the request fails with the MegaError::API_ENOENT error code The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the destination path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
Parameters:
node - Node to get the preview
dstFilePath - Destination path for the preview. If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
listener - MegaRequestListener to track this request
getPreview
public void getPreview(MegaNode node,
                       java.lang.String dstFilePath)
Get the preview of a node If the node doesn't have a preview the request fails with the MegaError::API_ENOENT error code
Parameters:
node - Node to get the preview
dstFilePath - Destination path for the preview. If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
getUserAvatar
public void getUserAvatar(MegaUser user,
                          java.lang.String dstFilePath,
                          MegaRequestListenerInterface listener)
Get the avatar of a MegaUser The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getFile - Returns the destination path - MegaRequest::getEmail - Returns the email of the user
Parameters:
user - MegaUser to get the avatar
dstFilePath - Destination path for the avatar. It has to be a path to a file, not to a folder. If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
listener - MegaRequestListener to track this request
getUserAvatar
public void getUserAvatar(MegaUser user,
                          java.lang.String dstFilePath)
Get the avatar of a MegaUser
Parameters:
user - MegaUser to get the avatar
dstFilePath - Destination path for the avatar. It has to be a path to a file, not to a folder. If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
cancelGetThumbnail
public void cancelGetThumbnail(MegaNode node,
                               MegaRequestListenerInterface listener)
Cancel the retrieval of a thumbnail The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
Parameters:
node - Node to cancel the retrieval of the thumbnail
listener - MegaRequestListener to track this request
See Also:
MegaApi::getThumbnail
cancelGetThumbnail
public void cancelGetThumbnail(MegaNode node)
Cancel the retrieval of a thumbnail
Parameters:
node - Node to cancel the retrieval of the thumbnail
See Also:
MegaApi::getThumbnail
cancelGetPreview
public void cancelGetPreview(MegaNode node,
                             MegaRequestListenerInterface listener)
Cancel the retrieval of a preview The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
Parameters:
node - Node to cancel the retrieval of the preview
listener - MegaRequestListener to track this request
See Also:
MegaApi::getPreview
cancelGetPreview
public void cancelGetPreview(MegaNode node)
Cancel the retrieval of a preview
Parameters:
node - Node to cancel the retrieval of the preview
See Also:
MegaApi::getPreview
setThumbnail
public void setThumbnail(MegaNode node,
                         java.lang.String srcFilePath,
                         MegaRequestListenerInterface listener)
Set the thumbnail of a MegaNode The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the source path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
Parameters:
node - MegaNode to set the thumbnail
srcFilePath - Source path of the file that will be set as thumbnail
listener - MegaRequestListener to track this request
setThumbnail
public void setThumbnail(MegaNode node,
                         java.lang.String srcFilePath)
Set the thumbnail of a MegaNode
Parameters:
node - MegaNode to set the thumbnail
srcFilePath - Source path of the file that will be set as thumbnail
setPreview
public void setPreview(MegaNode node,
                       java.lang.String srcFilePath,
                       MegaRequestListenerInterface listener)
Set the preview of a MegaNode The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getFile - Returns the source path - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
Parameters:
node - MegaNode to set the preview
srcFilePath - Source path of the file that will be set as preview
listener - MegaRequestListener to track this request
setPreview
public void setPreview(MegaNode node,
                       java.lang.String srcFilePath)
Set the preview of a MegaNode
Parameters:
node - MegaNode to set the preview
srcFilePath - Source path of the file that will be set as preview
setAvatar
public void setAvatar(java.lang.String srcFilePath,
                      MegaRequestListenerInterface listener)
Set the avatar of the MEGA account The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getFile - Returns the source path
Parameters:
srcFilePath - Source path of the file that will be set as avatar
listener - MegaRequestListener to track this request
setAvatar
public void setAvatar(java.lang.String srcFilePath)
Set the avatar of the MEGA account
Parameters:
srcFilePath - Source path of the file that will be set as avatar
setUserAttribute
public void setUserAttribute(int type,
                             java.lang.String value,
                             MegaRequestListenerInterface listener)
Set an attribute of the current user The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParamType - Returns the attribute type - MegaRequest::getFile - Returns the new value for the attribute
Parameters:
type - Attribute type Valid values are: USER_ATTR_FIRSTNAME = 1 Change the firstname of the user USER_ATTR_LASTNAME = 2 Change the lastname of the user
value - New attribute value
listener - MegaRequestListenerInterface to track this request
setUserAttribute
public void setUserAttribute(int type,
                             java.lang.String value)
Set an attribute of the current user
Parameters:
type - Attribute type Valid values are: USER_ATTR_FIRSTNAME = 1 Change the firstname of the user USER_ATTR_LASTNAME = 2 Change the lastname of the user
value - New attribute value
exportNode
public void exportNode(MegaNode node,
                       MegaRequestListenerInterface listener)
Generate a public link of a file/folder in MEGA The associated request type with this request is MegaRequest::TYPE_EXPORT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getAccess - Returns true Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getLink - Public link
Parameters:
node - MegaNode to get the public link
listener - MegaRequestListener to track this request
exportNode
public void exportNode(MegaNode node)
Generate a public link of a file/folder in MEGA
Parameters:
node - MegaNode to get the public link
disableExport
public void disableExport(MegaNode node,
                          MegaRequestListenerInterface listener)
Stop sharing a file/folder The associated request type with this request is MegaRequest::TYPE_EXPORT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the node - MegaRequest::getAccess - Returns false
Parameters:
node - MegaNode to stop sharing
listener - MegaRequestListener to track this request
disableExport
public void disableExport(MegaNode node)
Stop sharing a file/folder
Parameters:
node - MegaNode to stop sharing
fetchNodes
public void fetchNodes(MegaRequestListenerInterface listener)
Fetch the filesystem in MEGA The MegaApi object must be logged in in an account or a public folder to successfully complete this request. The associated request type with this request is MegaRequest::TYPE_FETCH_NODES
Parameters:
listener - MegaRequestListener to track this request
fetchNodes
public void fetchNodes()
Fetch the filesystem in MEGA The MegaApi object must be logged in in an account or a public folder to successfully complete this request.
getAccountDetails
public void getAccountDetails(MegaRequestListenerInterface listener)
Get details about the MEGA account The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getMegaAccountDetails - Details of the MEGA account
Parameters:
listener - MegaRequestListener to track this request
getAccountDetails
public void getAccountDetails()
Get details about the MEGA account
getExtendedAccountDetails
public void getExtendedAccountDetails(boolean sessions,
                                      boolean purchases,
                                      boolean transactions,
                                      MegaRequestListenerInterface listener)
Get details about the MEGA account This function allows to optionally get data about sessions, transactions and purchases related to the account. The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getMegaAccountDetails - Details of the MEGA account
Parameters:
listener - MegaRequestListener to track this request
getExtendedAccountDetails
public void getExtendedAccountDetails(boolean sessions,
                                      boolean purchases,
                                      boolean transactions)
Get details about the MEGA account This function allows to optionally get data about sessions, transactions and purchases related to the account.
getExtendedAccountDetails
public void getExtendedAccountDetails(boolean sessions,
                                      boolean purchases)
Get details about the MEGA account This function allows to optionally get data about sessions and purchases related to the account.
getExtendedAccountDetails
public void getExtendedAccountDetails(boolean sessions)
Get details about the MEGA account This function allows to optionally get data about sessions related to the account.
getExtendedAccountDetails
public void getExtendedAccountDetails()
Get details about the MEGA account
getPricing
public void getPricing(MegaRequestListenerInterface listener)
Get the available pricing plans to upgrade a MEGA account You can get a payment URL for any of the pricing plans provided by this function using MegaApi::getPaymentUrl The associated request type with this request is MegaRequest::TYPE_GET_PRICING Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getPricing - MegaPricing object with all pricing plans
Parameters:
listener - MegaRequestListener to track this request
See Also:
MegaApi::getPaymentUrl
getPricing
public void getPricing()
Get the available pricing plans to upgrade a MEGA account You can get a payment URL for any of the pricing plans provided by this function using MegaApi::getPaymentUrl
See Also:
MegaApi::getPaymentUrl
getPaymentId
public void getPaymentId(long productHandle,
                         MegaRequestListenerInterface listener)
Get the payment id for an upgrade The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID Valid data in the MegaRequest object received on callbacks: - MegaRequest::getNodeHandle - Returns the handle of the product Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK: - MegaRequest::getLink - Payment link
Parameters:
productHandle - Handle of the product (see MegaApi::getPricing)
listener - MegaRequestListener to track this request
See Also:
MegaApi::getPricing
getPaymentId
public void getPaymentId(long productHandle)
Get the payment URL for an upgrade
Parameters:
productHandle - Handle of the product (see MegaApi::getPricing)
See Also:
MegaApi::getPricing
submitPurchaseReceipt
public void submitPurchaseReceipt(java.lang.String receipt,
                                  MegaRequestListenerInterface listener)
Send the Google Play receipt after a correct purchase of a subscription
Parameters:
receipt - String The complete receipt from Google Play
listener - MegaRequestListener to track this request
submitPurchaseReceipt
public void submitPurchaseReceipt(java.lang.String receipt)
Send the Google Play receipt after a correct purchase of a subscription
Parameters:
receipt - String The complete receipt from Google Play
exportMasterKey
public java.lang.String exportMasterKey()
Export the master key of the account The returned value is a Base64-encoded string With the master key, it's possible to start the recovery of an account when the password is lost: - https://mega.co.nz/#recovery
Returns:
Base64-encoded master key
changePassword
public void changePassword(java.lang.String oldPassword,
                           java.lang.String newPassword,
                           MegaRequestListenerInterface listener)
Change the password of the MEGA account The associated request type with this request is MegaRequest::TYPE_CHANGE_PW Valid data in the MegaRequest object received on callbacks: - MegaRequest::getPassword - Returns the old password - MegaRequest::getNewPassword - Returns the new password
Parameters:
oldPassword - Old password
newPassword - New password
listener - MegaRequestListener to track this request
changePassword
public void changePassword(java.lang.String oldPassword,
                           java.lang.String newPassword)
Change the password of the MEGA account
Parameters:
oldPassword - Old password
newPassword - New password
addContact
public void addContact(java.lang.String email,
                       MegaRequestListenerInterface listener)
Add a new contact to the MEGA account The associated request type with this request is MegaRequest::TYPE_ADD_CONTACT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the email of the contact
Parameters:
email - Email of the new contact
listener - MegaRequestListener to track this request
addContact
public void addContact(java.lang.String email)
Add a new contact to the MEGA account
Parameters:
email - Email of the new contact
removeContact
public void removeContact(MegaUser user,
                          MegaRequestListenerInterface listener)
Remove a contact to the MEGA account The associated request type with this request is MegaRequest::TYPE_REMOVE_CONTACT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getEmail - Returns the email of the contact
Parameters:
email - Email of the contact
listener - MegaRequestListener to track this request
removeContact
public void removeContact(MegaUser user)
Remove a contact to the MEGA account
Parameters:
email - Email of the contact
logout
public void logout(MegaRequestListenerInterface listener)
Logout of the MEGA account The associated request type with this request is MegaRequest::TYPE_LOGOUT
Parameters:
listener - MegaRequestListener to track this request
logout
public void logout()
Logout of the MEGA account
localLogout
public void localLogout(MegaRequestListenerInterface listener)
Parameters:
listener - MegaRequestListener to track this request
localLogout
public void localLogout()
Logout of the MEGA account without invalidating the session
submitFeedback
public void submitFeedback(int rating,
                           java.lang.String comment,
                           MegaRequestListenerInterface listener)
Deprecated. This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
Submit feedback about the app The User-Agent is used to identify the app. It can be set in MegaApi::MegaApi The associated request type with this request is MegaRequest::TYPE_REPORT_EVENT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParamType - Returns MegaApi::EVENT_FEEDBACK - MegaRequest::getText - Retuns the comment about the app - MegaRequest::getNumber - Returns the rating for the app
Parameters:
rating - Integer to rate the app. Valid values: from 1 to 5.
comment - Comment about the app
listener - MegaRequestListener to track this request
submitFeedback
public void submitFeedback(int rating,
                           java.lang.String comment)
Deprecated. This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
Submit feedback about the app The User-Agent is used to identify the app. It can be set in MegaApi::MegaApi
Parameters:
rating - Integer to rate the app. Valid values: from 1 to 5.
comment - Comment about the app
reportDebugEvent
public void reportDebugEvent(java.lang.String text,
                             MegaRequestListenerInterface listener)
Deprecated. This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
Send a debug report The User-Agent is used to identify the app. It can be set in MegaApi::MegaApi The associated request type with this request is MegaRequest::TYPE_REPORT_EVENT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParamType - Returns MegaApi::EVENT_DEBUG - MegaRequest::getText - Retuns the debug message
Parameters:
text - Debug message
listener - MegaRequestListener to track this request
reportDebugEvent
public void reportDebugEvent(java.lang.String text)
Deprecated. This function is for internal usage of MEGA apps. This feedback is sent to MEGA servers.
Send a debug report The User-Agent is used to identify the app. It can be set in MegaApi::MegaApi The associated request type with this request is MegaRequest::TYPE_REPORT_EVENT Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParamType - Returns MegaApi::EVENT_DEBUG - MegaRequest::getText - Retuns the debug message
Parameters:
text - Debug message
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent,
                        MegaTransferListenerInterface listener)
Upload a file
Parameters:
Local - path of the file
Parent - node for the file in the MEGA account
listener - MegaTransferListener to track this transfer
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent)
Upload a file
Parameters:
Local - path of the file
Parent - node for the file in the MEGA account
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent,
                        long mtime,
                        MegaTransferListenerInterface listener)
Upload a file with a custom modification time
Parameters:
localPath - Local path of the file
parent - Parent node for the file in the MEGA account
mtime - Custom modification time for the file in MEGA (in seconds since the epoch)
listener - MegaTransferListener to track this transfer
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent,
                        long mtime)
Upload a file with a custom modification time
Parameters:
localPath - Local path of the file
parent - Parent node for the file in the MEGA account
mtime - Custom modification time for the file in MEGA (in seconds since the epoch)
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent,
                        java.lang.String fileName,
                        MegaTransferListenerInterface listener)
Upload a file with a custom name
Parameters:
localPath - Local path of the file
parent - Parent node for the file in the MEGA account
fileName - Custom file name for the file in MEGA
listener - MegaTransferListener to track this transfer
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent,
                        java.lang.String fileName)
Upload a file with a custom name
Parameters:
localPath - Local path of the file
parent - Parent node for the file in the MEGA account
fileName - Custom file name for the file in MEGA
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent,
                        java.lang.String fileName,
                        long mtime,
                        MegaTransferListenerInterface listener)
Upload a file with a custom name and a custom modification time
Parameters:
localPath - Local path of the file
parent - Parent node for the file in the MEGA account
fileName - Custom file name for the file in MEGA
mtime - Custom modification time for the file in MEGA (in seconds since the epoch)
listener - MegaTransferListener to track this transfer
startUpload
public void startUpload(java.lang.String localPath,
                        MegaNode parent,
                        java.lang.String fileName,
                        long mtime)
Upload a file with a custom name and a custom modification time
Parameters:
localPath - Local path of the file
parent - Parent node for the file in the MEGA account
fileName - Custom file name for the file in MEGA
mtime - Custom modification time for the file in MEGA (in seconds since the epoch)
startDownload
public void startDownload(MegaNode node,
                          java.lang.String localPath,
                          MegaTransferListenerInterface listener)
Download a file from MEGA
Parameters:
node - MegaNode that identifies the file
localPath - Destination path for the file If this path is a local folder, it must end with a '\' or '/' character and the file name in MEGA will be used to store a file inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
listener - MegaTransferListener to track this transfer
startDownload
public void startDownload(MegaNode node,
                          java.lang.String localPath)
Download a file from MEGA
Parameters:
node - MegaNode that identifies the file
localPath - Destination path for the file If this path is a local folder, it must end with a '\' or '/' character and the file name in MEGA will be used to store a file inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
startStreaming
public void startStreaming(MegaNode node,
                           long startPos,
                           long size,
                           MegaTransferListenerInterface listener)
Start an streaming download Streaming downloads don't save the downloaded data into a local file. It is provided in MegaTransferListener::onTransferUpdate in a byte buffer. The pointer is returned by MegaTransfer::getLastBytes and the size of the buffer in MegaTransfer::getDeltaSize The same byte array is also provided in the callback MegaTransferListener::onTransferData for compatibility with other programming languages. Only the MegaTransferListener passed to this function will receive MegaTransferListener::onTransferData callbacks. MegaTransferListener objects registered with MegaApi::addTransferListener won't receive them for performance reasons
Parameters:
node - MegaNode that identifies the file (public nodes aren't supported yet)
startPos - First byte to download from the file
size - Size of the data to download
listener - MegaTransferListener to track this transfer
cancelTransfer
public void cancelTransfer(MegaTransfer transfer,
                           MegaRequestListenerInterface listener)
Cancel a transfer When a transfer is cancelled, it will finish and will provide the error code MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and MegaListener::onTransferFinish The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
Parameters:
transfer - MegaTransfer object that identifies the transfer You can get this object in any MegaTransferListener callback or any MegaListener callback related to transfers.
listener - MegaRequestListener to track this request
cancelTransfer
public void cancelTransfer(MegaTransfer transfer)
Cancel a transfer
Parameters:
transfer - MegaTransfer object that identifies the transfer You can get this object in any MegaTransferListener callback or any MegaListener callback related to transfers.
cancelTransferByTag
public void cancelTransferByTag(int transferTag,
                                MegaRequestListenerInterface listener)
Cancel the transfer with a specific tag When a transfer is cancelled, it will finish and will provide the error code MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and MegaListener::onTransferFinish The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER Valid data in the MegaRequest object received on callbacks: - MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
Parameters:
transferTag - tag that identifies the transfer You can get this tag using MegaTransfer::getTag
listener - MegaRequestListener to track this request
cancelTransferByTag
public void cancelTransferByTag(int transferTag)
Cancel the transfer with a specific tag
Parameters:
transferTag - tag that identifies the transfer You can get this tag using MegaTransfer::getTag
cancelTransfers
public void cancelTransfers(int direction,
                            MegaRequestListenerInterface listener)
Cancel all transfers of the same type The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFERS Valid data in the MegaRequest object received on callbacks: - MegaRequest::getParamType - Returns the first parameter
Parameters:
type - Type of transfers to cancel. Valid values are: - MegaTransfer::TYPE_DOWNLOAD = 0 - MegaTransfer::TYPE_UPLOAD = 1
listener - MegaRequestListener to track this request
cancelTransfers
public void cancelTransfers(int direction)
Cancel all transfers of the same type
Parameters:
type - Type of transfers to cancel. Valid values are: - MegaTransfer::TYPE_DOWNLOAD = 0 - MegaTransfer::TYPE_UPLOAD = 1
pauseTransfers
public void pauseTransfers(boolean pause,
                           MegaRequestListenerInterface listener)
Pause/resume all transfers The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS Valid data in the MegaRequest object received on callbacks: - MegaRequest::getFlag - Returns the first parameter
Parameters:
pause - true to pause all transfers / false to resume all transfers
listener - MegaRequestListener to track this request
pauseTransfers
public void pauseTransfers(boolean pause)
Pause/resume all transfers
Parameters:
pause - true to pause all transfers / false to resume all transfers
setUploadLimit
public void setUploadLimit(int bpslimit)
Set the upload speed limit The limit will be applied on the server side when starting a transfer. Thus the limit won't be applied for already started uploads and it's applied per storage server.
Parameters:
bpslimit - -1 to automatically select the limit, 0 for no limit, otherwise the speed limit in bytes per second
getTransfers
public java.util.ArrayList<MegaTransfer> getTransfers()
Get all active transfers
Returns:
List with all active transfers
getTransferByTag
public MegaTransfer getTransferByTag(int transferTag)
Get the transfer with a transfer tag That tag can be got using MegaTransfer::getTag
Parameters:
Transfer - tag to check
Returns:
MegaTransfer object with that tag, or NULL if there isn't any active transfer with it
getTransfers
public java.util.ArrayList<MegaTransfer> getTransfers(int type)
Get all active transfers based on the type
Parameters:
type - MegaTransfer.TYPE_DOWNLOAD || MegaTransfer.TYPE_UPLOAD
Returns:
List with all active download or upload transfers
update
public void update()
Deprecated. This function is only here for debugging purposes. It will probably be removed in future updates
Force a loop of the SDK thread
isWaiting
public boolean isWaiting()
Check if the SDK is waiting for the server
Returns:
true if the SDK is waiting for the server to complete a request
getNumPendingUploads
public int getNumPendingUploads()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Get the number of pending uploads
Returns:
Pending uploads
getNumPendingDownloads
public int getNumPendingDownloads()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Get the number of pending downloads
Returns:
Pending downloads
getTotalUploads
public int getTotalUploads()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Get the number of queued uploads since the last call to MegaApi::resetTotalUploads
Returns:
Number of queued uploads since the last call to MegaApi::resetTotalUploads
getTotalDownloads
public int getTotalDownloads()
Deprecated. Function related to statistics will be reviewed in future updates. They could change or be removed in the current form.
Get the number of queued uploads since the last call to MegaApi::resetTotalDownloads
Returns:
Number of queued uploads since the last call to MegaApi::resetTotalDownloads
resetTotalDownloads
public void resetTotalDownloads()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Reset the number of total downloads This function resets the number returned by MegaApi::getTotalDownloads
resetTotalUploads
public void resetTotalUploads()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Reset the number of total uploads This function resets the number returned by MegaApi::getTotalUploads
getTotalDownloadedBytes
public long getTotalDownloadedBytes()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Get the total downloaded bytes since the creation of the MegaApi object
Returns:
Total downloaded bytes since the creation of the MegaApi object
getTotalUploadedBytes
public long getTotalUploadedBytes()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Get the total uploaded bytes since the creation of the MegaApi object
Returns:
Total uploaded bytes since the creation of the MegaApi object
updateStats
public void updateStats()
Deprecated. Function related to statistics will be reviewed in future updates to provide more data and avoid race conditions. They could change or be removed in the current form.
Update the number of pending downloads/uploads This function forces a count of the pending downloads/uploads. It could affect the return value of MegaApi::getNumPendingDownloads and MegaApi::getNumPendingUploads.
startUnbufferedDownload
public void startUnbufferedDownload(MegaNode node,
                                    long startOffset,
                                    long size,
                                    java.io.OutputStream outputStream,
                                    MegaTransferListenerInterface listener)
startUnbufferedDownload
public void startUnbufferedDownload(MegaNode node,
                                    java.io.OutputStream outputStream,
                                    MegaTransferListenerInterface listener)
getNumChildren
public int getNumChildren(MegaNode parent)
Get the number of child nodes If the node doesn't exist in MEGA or isn't a folder, this function returns 0 This function doesn't search recursively, only returns the direct child nodes.
Parameters:
parent - Parent node
Returns:
Number of child nodes
getNumChildFiles
public int getNumChildFiles(MegaNode parent)
Get the number of child files of a node If the node doesn't exist in MEGA or isn't a folder, this function returns 0 This function doesn't search recursively, only returns the direct child files.
Parameters:
parent - Parent node
Returns:
Number of child files
getNumChildFolders
public int getNumChildFolders(MegaNode parent)
Get the number of child folders of a node If the node doesn't exist in MEGA or isn't a folder, this function returns 0 This function doesn't search recursively, only returns the direct child folders.
Parameters:
parent - Parent node
Returns:
Number of child folders
getChildren
public java.util.ArrayList<MegaNode> getChildren(MegaNode parent,
                                                 int order)
Get all children of a MegaNode If the parent node doesn't exist or it isn't a folder, this function returns NULL
Parameters:
parent - Parent node
order - Order for the returned list Valid values for this parameter are: - MegaApi::ORDER_NONE = 0 Undefined order - MegaApi::ORDER_DEFAULT_ASC = 1 Folders first in alphabetical order, then files in the same order - MegaApi::ORDER_DEFAULT_DESC = 2 Files first in reverse alphabetical order, then folders in the same order - MegaApi::ORDER_SIZE_ASC = 3 Sort by size, ascending - MegaApi::ORDER_SIZE_DESC = 4 Sort by size, descending - MegaApi::ORDER_CREATION_ASC = 5 Sort by creation time in MEGA, ascending - MegaApi::ORDER_CREATION_DESC = 6 Sort by creation time in MEGA, descending - MegaApi::ORDER_MODIFICATION_ASC = 7 Sort by modification time of the original file, ascending - MegaApi::ORDER_MODIFICATION_DESC = 8 Sort by modification time of the original file, descending - MegaApi::ORDER_ALPHABETICAL_ASC = 9 Sort in alphabetical order, ascending - MegaApi::ORDER_ALPHABETICAL_DESC = 10 Sort in alphabetical order, descending
Returns:
List with all child MegaNode objects
getChildren
public java.util.ArrayList<MegaNode> getChildren(MegaNode parent)
Get all children of a MegaNode If the parent node doesn't exist or it isn't a folder, this function returns NULL
Parameters:
parent - Parent node
Returns:
List with all child MegaNode objects
getIndex
public int getIndex(MegaNode node,
                    int order)
Get the current index of the node in the parent folder for a specific sorting order If the node doesn't exist or it doesn't have a parent node (because it's a root node) this function returns -1
Parameters:
node - Node to check
order - Sorting order to use
Returns:
Index of the node in its parent folder
getIndex
public int getIndex(MegaNode node)
Get the current index of the node in the parent folder If the node doesn't exist or it doesn't have a parent node (because it's a root node) this function returns -1
Parameters:
node - Node to check
Returns:
Index of the node in its parent folder
getChildNode
public MegaNode getChildNode(MegaNode parent,
                             java.lang.String name)
Get the child node with the provided name If the node doesn't exist, this function returns NULL
Parameters:
Parent - node
Name - of the node
Returns:
The MegaNode that has the selected parent and name
getParentNode
public MegaNode getParentNode(MegaNode node)
Get the parent node of a MegaNode If the node doesn't exist in the account or it is a root node, this function returns NULL
Parameters:
node - MegaNode to get the parent
Returns:
The parent of the provided node
getNodePath
public java.lang.String getNodePath(MegaNode node)
Get the path of a MegaNode If the node doesn't exist, this function returns NULL. You can recoved the node later unsing MegaApi::getNodeByPath except if the path contains names with '/', '\' or ':' characters.
Parameters:
node - MegaNode for which the path will be returned
Returns:
The path of the node
getNodeByPath
public MegaNode getNodeByPath(java.lang.String path,
                              MegaNode baseFolder)
Get the MegaNode in a specific path in the MEGA account The path separator character is '/' The Inbox root node is //in/ The Rubbish root node is //bin/ Paths with names containing '/', '\' or ':' aren't compatible with this function.
Parameters:
path - Path to check
n - Base node if the path is relative
Returns:
The MegaNode object in the path, otherwise NULL
getNodeByPath
public MegaNode getNodeByPath(java.lang.String path)
Get the MegaNode in a specific path in the MEGA account The path separator character is '/' The Inbox root node is //in/ The Rubbish root node is //bin/ Paths with names containing '/', '\' or ':' aren't compatible with this function.
Parameters:
path - Path to check
Returns:
The MegaNode object in the path, otherwise NULL
getNodeByHandle
public MegaNode getNodeByHandle(long handle)
Get the MegaNode that has a specific handle You can get the handle of a MegaNode using MegaNode::getHandle. The same handle can be got in a Base64-encoded string using MegaNode::getBase64Handle. Conversions between these formats can be done using MegaApi::base64ToHandle and MegaApi::handleToBase64
Parameters:
MegaHandler - Node handle to check
Returns:
MegaNode object with the handle, otherwise NULL
getContacts
public java.util.ArrayList<MegaUser> getContacts()
Get all contacts of this MEGA account
Returns:
List of MegaUser object with all contacts of this account
getContact
public MegaUser getContact(java.lang.String email)
Get the MegaUser that has a specific email address You can get the email of a MegaUser using MegaUser::getEmail
Parameters:
email - Email address to check
Returns:
MegaUser that has the email address, otherwise NULL
getInShares
public java.util.ArrayList<MegaNode> getInShares(MegaUser user)
Get a list with all inbound sharings from one MegaUser
Parameters:
user - MegaUser sharing folders with this account
Returns:
List of MegaNode objects that this user is sharing with this account
getInShares
public java.util.ArrayList<MegaNode> getInShares()
Get a list with all inboud sharings
Returns:
List of MegaNode objects that other users are sharing with this account
isShared
public boolean isShared(MegaNode node)
Check if a MegaNode is being shared For nodes that are being shared, you can get a a list of MegaShare objects using MegaApi::getOutShares
Parameters:
node - Node to check
Returns:
true is the MegaNode is being shared, otherwise false
getOutShares
public java.util.ArrayList<MegaShare> getOutShares()
Get a list with all active outbound sharings
Returns:
List of MegaShare objects
getOutShares
public java.util.ArrayList<MegaShare> getOutShares(MegaNode node)
Get a list with the active outbound sharings for a MegaNode If the node doesn't exist in the account, this function returns an empty list.
Parameters:
node - MegaNode to check
Returns:
List of MegaShare objects
getAccess
public int getAccess(MegaNode node)
Get the access level of a MegaNode
Parameters:
node - MegaNode to check
Returns:
Access level of the node Valid values are: - MegaShare::ACCESS_OWNER - MegaShare::ACCESS_FULL - MegaShare::ACCESS_READWRITE - MegaShare::ACCESS_READ - MegaShare::ACCESS_UNKNOWN
getSize
public long getSize(MegaNode node)
Get the size of a node tree If the MegaNode is a file, this function returns the size of the file. If it's a folder, this fuction returns the sum of the sizes of all nodes in the node tree.
Parameters:
node - Parent node
Returns:
Size of the node tree
getFingerprint
public java.lang.String getFingerprint(java.lang.String filePath)
Get a Base64-encoded fingerprint for a local file The fingerprint is created taking into account the modification time of the file and file contents. This fingerprint can be used to get a corresponding node in MEGA using MegaApi::getNodeByFingerprint If the file can't be found or can't be opened, this function returns null
Parameters:
filePath - Local file path
Returns:
Base64-encoded fingerprint for the file
getFingerprint
public java.lang.String getFingerprint(MegaNode node)
Get a Base64-encoded fingerprint for a node If the node doesn't exist or doesn't have a fingerprint, this function returns null
Parameters:
node - Node for which we want to get the fingerprint
Returns:
Base64-encoded fingerprint for the file
getNodeByFingerprint
public MegaNode getNodeByFingerprint(java.lang.String fingerprint)
Returns a node with the provided fingerprint If there isn't any node in the account with that fingerprint, this function returns null.
Parameters:
fingerprint - Fingerprint to check
Returns:
MegaNode object with the provided fingerprint
getNodeByFingerprint
public MegaNode getNodeByFingerprint(java.lang.String fingerprint,
                                     MegaNode preferredParent)
hasFingerprint
public boolean hasFingerprint(java.lang.String fingerprint)
Check if the account already has a node with the provided fingerprint A fingerprint for a local file can be generated using MegaApi::getFingerprint
Parameters:
fingerprint - Fingerprint to check
Returns:
true if the account contains a node with the same fingerprint
checkAccess
public MegaError checkAccess(MegaNode node,
                             int level)
Check if a node has an access level
Parameters:
node - Node to check
level - Access level to check Valid values for this parameter are: - MegaShare::ACCESS_OWNER - MegaShare::ACCESS_FULL - MegaShare::ACCESS_READWRITE - MegaShare::ACCESS_READ
Returns:
MegaError object with the result. Valid values for the error code are: - MegaError::API_OK - The node has the required access level - MegaError::API_EACCESS - The node doesn't have the required access level - MegaError::API_ENOENT - The node doesn't exist in the account - MegaError::API_EARGS - Invalid parameters
checkMove
public MegaError checkMove(MegaNode node,
                           MegaNode target)
Check if a node can be moved to a target node
Parameters:
node - Node to check
target - Target for the move operation
Returns:
MegaError object with the result: Valid values for the error code are: - MegaError::API_OK - The node can be moved to the target - MegaError::API_EACCESS - The node can't be moved because of permissions problems - MegaError::API_ECIRCULAR - The node can't be moved because that would create a circular linkage - MegaError::API_ENOENT - The node or the target doesn't exist in the account - MegaError::API_EARGS - Invalid parameters
getRootNode
public MegaNode getRootNode()
Returns the root node of the account If you haven't successfully called MegaApi::fetchNodes before, this function returns null
Returns:
Root node of the account
getInboxNode
public MegaNode getInboxNode()
Returns the inbox node of the account If you haven't successfully called MegaApi::fetchNodes before, this function returns null
Returns:
Inbox node of the account
getRubbishNode
public MegaNode getRubbishNode()
Returns the rubbish node of the account If you haven't successfully called MegaApi::fetchNodes before, this function returns null
Returns:
Rubbish node of the account
search
public java.util.ArrayList<MegaNode> search(MegaNode parent,
                                            java.lang.String searchString,
                                            boolean recursive)
Search nodes containing a search string in their name The search is case-insensitive.
Parameters:
node - The parent node of the tree to explore
searchString - Search string. The search is case-insensitive
recursive - True if you want to seach recursively in the node tree. False if you want to seach in the children of the node only
Returns:
List of nodes that contain the desired string in their name
search
public java.util.ArrayList<MegaNode> search(MegaNode parent,
                                            java.lang.String searchString)
Search nodes containing a search string in their name The search is case-insensitive.
Parameters:
node - The parent node of the tree to explore
searchString - Search string. The search is case-insensitive
Returns:
List of nodes that contain the desired string in their name
processMegaTree
public boolean processMegaTree(MegaNode parent,
                               MegaTreeProcessorInterface processor,
                               boolean recursive)
Process a node tree using a MegaTreeProcessor implementation
Parameters:
node - The parent node of the tree to explore
processor - MegaTreeProcessor that will receive callbacks for every node in the tree
recursive - True if you want to recursively process the whole node tree. False if you want to process the children of the node only
Returns:
True if all nodes were processed. False otherwise (the operation can be cancelled by MegaTreeProcessor::processMegaNode())
processMegaTree
public boolean processMegaTree(MegaNode parent,
                               MegaTreeProcessorInterface processor)
Process a node tree using a MegaTreeProcessor implementation
Parameters:
node - The parent node of the tree to explore
processor - MegaTreeProcessor that will receive callbacks for every node in the tree
Returns:
True if all nodes were processed. False otherwise (the operation can be cancelled by MegaTreeProcessor::processMegaNode())
getVersion
public java.lang.String getVersion()
Get the SDK version
Returns:
SDK version
getUserAgent
public java.lang.String getUserAgent()
Get the User-Agent header used by the SDK
Returns:
User-Agent used by the SDK
changeApiUrl
public void changeApiUrl(java.lang.String apiURL,
                         boolean disablepkp)
changeApiUrl
public void changeApiUrl(java.lang.String apiURL)
nameToLocal
public java.lang.String nameToLocal(java.lang.String name)
Make a name suitable for a file name in the local filesystem This function escapes (%xx) forbidden characters in the local filesystem if needed. You can revert this operation using MegaApi::localToName
Parameters:
name - Name to convert
Returns:
Converted name
localToName
public java.lang.String localToName(java.lang.String localName)
Parameters:
name - Escaped name to convert
Returns:
Converted name
base64ToBase32
public static java.lang.String base64ToBase32(java.lang.String base64)
Convert a Base64 string to Base32 If the input pointer is NULL, this function will return NULL. If the input character array isn't a valid base64 string the effect is undefined
Parameters:
base64 - NULL-terminated Base64 character array
Returns:
NULL-terminated Base32 character array
base32ToBase64
public static java.lang.String base32ToBase64(java.lang.String base32)
Convert a Base32 string to Base64 If the input pointer is NULL, this function will return NULL. If the input character array isn't a valid base32 string the effect is undefined
Parameters:
base32 - NULL-terminated Base32 character array
Returns:
NULL-terminated Base64 character array
removeRecursively
public static void removeRecursively(java.lang.String localPath)
privateFreeRequestListener
void privateFreeRequestListener(DelegateMegaRequestListener listener)
privateFreeTransferListener
void privateFreeTransferListener(DelegateMegaTransferListener listener)
nodeListToArray
static java.util.ArrayList<MegaNode> nodeListToArray(MegaNodeList nodeList)
shareListToArray
static java.util.ArrayList<MegaShare> shareListToArray(MegaShareList shareList)
transferListToArray
static java.util.ArrayList<MegaTransfer> transferListToArray(MegaTransferList transferList)
userListToArray
static java.util.ArrayList<MegaUser> userListToArray(MegaUserList userList)
