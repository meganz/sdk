"""
These are the mega API Python bindings, which were taken from the generated SWIG file. The bindings and its functionality has been compared with C++ documentation and arguments are given where required.
Deprecated functions as well as functions which were created specifically for other languages ie Java were removed. Some bindings (classes and functions) require clarification in terms of arguments and
its functionality as they are might be specifical helper functions and are not present in documentation. These are located in a seperate file.
"""
class MegaAccountDetails():
    '''Details about a MEGA account. 
    '''   
    def copy(self):
        '''Creates a copy of this MegaAccountDetails object.
        The resulting object is fully independent of the source MegaAccountDetails, it contains  a  copy of all internal attributes, so it will be 
        valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the object
        '''
    def getNumFiles(self, handle):
         '''Get the number of files in a node.
         Only root nodes are supported.
         handle	- Handle of the node to check 
         Returns number of files in a node.
         '''
    def getNumFolders(self, handle):
        '''Get the number of folders in a node.
        Only root nodes are supported.
        handle	- Handle of the node to check 
        Returns number of folders in a node.
        '''     
    
    def getProLevel(self):
        '''Get the PRO level of the MEGA account. 
        Returns PRO level of MEGA account. (ACCOUNT_TYPE_FREE = 0; ACCOUNT_TYPE_PROI = 1; ACCOUNT_TYPE_PROII = 2; ACCOUNT_TYPE_PROIII = 3, ACCOUNT_TYPE_LITE) 
        '''
        
    def getStorageMax(self):
        '''Get the maximum storage for the account (in bytes).
        Returns maximum storage for the account (in bytes)  
        '''
    
    def getStorageUsed(self):
        '''Get the used storage.
        Returns used storage for the account (in bytes)
        ''' 
        
    def getStorageUsed(self, handle):
        '''Get the used storage in for a node.
        Only root nodes are supported.
        handle- Handle of the node to check
        Returns used storage (in bytes).
        '''	
	
	def getTransferMax(self):
	    '''Get the maximum available bandwidth for the account. 
	    Returns maximum available bandwidth (in bytes) 
	    '''
	
	def getTransferOwnUsed(self):
	    '''Get the used bandwidth. 
	    Returns used bandwith in bytes.
	    '''  
    # Need clarification  def getProExpiration(self):
    
    def getSubscriptionStatus(self):
        '''Returns status of subscription(SUBSCRIPTION_STATUS_NONE, SUBSCRIPTION_STATUS_VALID, SUBSCRIPTION_STATUS_INVALID)
        '''
        
    # Need clarification  def getSubscriptionRenewTime(self):
    
    # Need clarification  def getSubscriptionMethod(self):
    
    # Need clarification  def getSubscriptionCycle(self):
    
    # Need clarification  def getNumUsageItems(self):
    
    # Need clarification  def getNumBalances(self):
    
   # Need clarification  def getBalance(self, *args):
      
   # Need clarification  def getNumSessions(self):
    
   # Need clarification  def getSession(self, *args): 
    
   # Need clarification  def getNumPurchases(self):
    
   # Need clarification  def getPurchase(self, *args):
    
   # Need clarification  def getNumTransactions(self):
    
   # Need clarification  def getTransaction(self, *args):
    
class MegaApi():
    ''' Allows to control a MEGA account or a shared folder.
    You must provide an appKey to use this SDK. You can generate an appKey for your app for free here:
    https://mega.co.nz/#sdk
    You can enable local node caching by passing a local path in the constructor of this class. That saves many data usage and many time starting your app because the entire filesystem won't have to  be    downloaded each time. The persistent node cache will only be loaded by logging in with a session key. To take advantage of this feature, apart of passing the local path to the constructor, your  application have to save the session key after login (MegaApi::dumpSession) and use it to log in the next time. This is highly recommended also to enhance the security, because in this was the access     password doesn't have to be stored by the application.
To access MEGA using this SDK, you have to create an object of this class and use one of the MegaApi::login options (to log in to a MEGA account or a public folder). If the login request succeed, you must call MegaApi::fetchNodes to get the filesystem in MEGA. After successfully completing that request, you can use all other functions, manage the files and start transfers.
After using MegaApi::logout you can reuse the same MegaApi object to log in to another MEGA account or a public folder. 
    '''
    def addListener(self, listener): 
        '''Register a listener to receive all events (requests, transfers, global, synchronization)
        You can use MegaApi::removeListener to stop receiving events. 
        listener - Listener that will receive all events (requests, transfers, global, synchronization) 
        '''
    def addRequestListener(self, listener):
        '''Register a listener to receive all events about requests.
        You can use MegaApi::removeRequestListener to stop receiving events.
        listener - Listener that will receive all events about requests 
        ''' 
    def addTransferListener(self, listener): 
        '''Register a listener to receive all events about transfers.
        You can use MegaApi::removeTransferListener to stop receiving events.
        listener - Listener that will receive all events about transfers  
        '''
    def addGlobalListener(self, listener):
        '''Register a listener to receive global events. 
        You can use MegaApi::removeGlobalListener to stop receiving events.
        listener - Listener that will receive global events 
        ''' 
    def removeListener(self, listener):
        '''Unregister a listener. 
        This listener won't receive more events.
        listener - Object that is unregistered 
        ''' 
    def removeRequestListener(self, listener): 
        '''Unregister a MegaRequestListener. .
        This listener won't receive more events.
        listener - Object that is unregistered  
        '''
    def removeTransferListener(self, listener): 
        '''Unregister a MegaTransferListener. 
        This listener won't receive more events.
        listener - Object that is unregistered 
        '''
    def removeGlobalListener(self, listener): 
        '''Unregister a MegaGlobalListener. 
        This listener won't receive more events.
        listener - Object that is unregistered 
        '''
    # Need clarification def getCurrentRequest(self): 
        
    # Need clarification def getCurrentTransfer(self): 
        
    # Need clarification def getCurrentError(self):
         
    # Need clarification def getCurrentNodes(self): 
        
    # Need clarification def getCurrentUsers(self):
        
    def getBase64PwKey(self, password): 
        '''Generates a private key based on the access password. This is a time consuming operation (specially for low-end mobile devices).  Since the resulting key is required to log in, this function   allows to do this step in a separate function. You should run this function in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin. 
        You take the ownership of the returned value. 
        password - Access password 
        Returns - Base64-encoded private key 
        '''
    def getStringHash(self, base64pwkey, email): 
        '''Generates a hash based in the provided private key and email.
This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is required to log in, this function allows to do this step in a separate function. You should run this function in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin
        You take the ownership of the returned value.
        base64pwkey- Private key returned by MegaApi::getBase64PwKey
        email - Email to create the hash 
        Returns - Base64-encoded hash
        '''
    # Need clarification def getSessionTransferURL(self, *args): 
        
    def retryPendingConnections(self, disconnect=False, includexfers=False, listener=None):
       '''Retry all pending requests.
When requests fails they wait some time before being retried. That delay grows exponentially if the request fails again. For this reason, and since this request is very lightweight, it's recommended to call it with the default parameters on every user interaction with the application. This will prevent very big delays completing requests.
The associated request type with this request is MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS. Valid data in the MegaRequest object received on callbacks:
    MegaRequest::getFlag - Returns the first parameter
    MegaRequest::getNumber - Returns the second parameter
disconnect -true if you want to disconnect already connected requests It's not recommended to set this flag to true if you are not fully sure about what are you doing. If you send a request that needs some time to complete and you disconnect it in a loop without giving it enough time, it could be retrying forever.
includexfers - true to retry also transfers It's not recommended to set this flag. Transfer has a retry counter and are aborted after a number of retries MegaTransfer::getMaxRetries. Setting this flag to true, you will force more immediate retries and your transfers could fail faster.
listener - MegaRequestListener to track this request 
       ''' 
    def login(self, email,password, listener = None): 
        '''Log in to a MEGA account.
The associated request type with this request is MegaRequest::TYPE_LOGIN. Valid data in the MegaRequest object received on callbacks:
    MegaRequest::getEmail - Returns the first parameter
    MegaRequest::getPassword - Returns the second parameter
If the email/password aren't valid the error code provided in onRequestFinish is MegaError::API_ENOENT.
        email -Email of the user
        password - Password  
        listener - MegaRequestListener to track this request 
        '''
    def loginToFolder(self, megaFolderLink, listener = None): 
        '''Log in to a public folder using a folder link.
After a successful login, you should call MegaApi::fetchNodes to get filesystem and start working with the folder.
The associated request type with this request is MegaRequest::TYPE_LOGIN. Valid data in the MegaRequest object received on callbacks:
    MegaRequest::getEmail - Retuns the string "FOLDER"
    MegaRequest::getLink - Returns the public link to the folder
    megaFolderLink - Public link to a folder in MEGA
    listener - MegaRequestListener to track this request        
        '''
    def fastLogin(self, email, base64pwkey, name, listener = None): 
        '''Initialize the creation of a new MEGA account with precomputed keys.
The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT. Valid data in the MegaRequest object received on callbacks:
    MegaRequest::getEmail - Returns the email for the account
    MegaRequest::getPrivateKey - Returns the private key calculated with MegaApi::getBase64PwKey
    MegaRequest::getName - Returns the name of the user
If this request succeed, a confirmation email will be sent to the users. If an account with the same email already exists, you will get the error code MegaError::API_EEXIST in onRequestFinish
    email - Email for the account
    base64pwkey	- Private key calculated with MegaApi::getBase64PwKey
    name - Name of the user
    listener - MegaRequestListener to track this request     
        '''
    # Need clarification def killSession(self, *args): 

    # Need clarification def getUserData(self, *args): 
        
    def dumpSession(self): 
        '''Returns the current session key.
        You have to be logged in to get a valid session key. Otherwise, this function returns NULL.
        You take the ownership of the returned value.
        Returns current session key   
        '''
    # Need clarification def dumpXMPPSession(self): 
        
    def createAccount(self, email, password, name, listener = None): 
        '''Initialize the creation of a new MEGA account.
        The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT. Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getEmail - Returns the email for the account
            MegaRequest::getPassword - Returns the password for the account
            MegaRequest::getName - Returns the name of the user
        If this request succeed, a confirmation email will be sent to the users. If an account with the same email already exists, you will get the error code MegaError::API_EEXIST in onRequestFinish
        email - Email for the account
        password - Password for the account
        name - Name of the user
        listener - MegaRequestListener to track this request 
        '''
    def fastCreateAccount(self, email, base64pwkey, name, listener = None): 
        '''Initialize the creation of a new MEGA account with precomputed keys.
        The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT. Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getEmail - Returns the email for the account
            MegaRequest::getPrivateKey - Returns the private key calculated with MegaApi::getBase64PwKey
            MegaRequest::getName - Returns the name of the user
        If this request succeed, a confirmation email will be sent to the users. If an account with the same email already exists, you will get the error code MegaError::API_EEXIST in onRequestFinish
        email - Email for the account
        base64pwkey - Private key calculated with MegaApi::getBase64PwKey
        name - Name of the user
        listener - MegaRequestListener to track this request 
        '''
    def querySignupLink(self, link, listener = None): 
        '''Get information about a confirmation link.
        The associated request type with this request is MegaRequest::TYPE_QUERY_SIGNUP_LINK. Valid data in the MegaRequest object received on all callbacks:
            MegaRequest::getLink - Returns the confirmation link
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getEmail - Return the email associated with the confirmation link
            MegaRequest::getName - Returns the name associated with the confirmation link
        link - Confirmation link
        listener - MegaRequestListener to track this request    
        '''
    def confirmAccount(self, link, password, listener = None):
        '''Confirm a MEGA account using a confirmation link and the user password.
        The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT Valid data in the MegaRequest object received on callbacks:
        MegaRequest::getLink - Returns the confirmation link
        MegaRequest::getPassword - Returns the password
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getEmail - Email of the account
            MegaRequest::getName - Name of the user
        link - Confirmation link
        password - Password of the account
        listener - MegaRequestListener to track this request        
        ''' 
    def fastConfirmAccount(self, link, base64pwkey, listener = None):
        '''Confirm a MEGA account using a confirmation link and a precomputed key.
        The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getLink - Returns the confirmation link
            MegaRequest::getPrivateKey - Returns the base64pwkey parameter
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getEmail - Email of the account
            MegaRequest::getName - Name of the user
        link - Confirmation link
        base64pwkey - Private key precomputed with MegaApi::getBase64PwKey
        listener - MegaRequestListener to track this request         
        ''' 
    def setProxySettings(self, proxySettings): 
        '''Set proxy settings.
        The SDK will start using the provided proxy settings as soon as this function returns.
        proxySettings - Proxy settings 
        '''
    def getAutoProxySettings(self):
        '''Try to detect the system's proxy settings.
        Automatic proxy detection is currently supported on Windows only. On other platforms, this fuction will return a MegaProxy object of type MegaProxy::PROXY_NONE
        You take the ownership of the returned value.
        Returns MegaProxy object with the detected proxy settings.
        '''
    def isLoggedIn(self): 
        '''Check if the MegaApi object is logged in.
        Returns 0 if not logged in, else a number >= 0
        '''
    def getMyEmail(self):
        '''Retuns the email of the currently open account.
        If the MegaApi object isn't logged in or the email isn't available, this function returns None
        You take the ownership of the returned value
        Returns Email of the account
        ''' 
    def createFolder(self, name, parent, listener = None):
        '''Create a folder in the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_CREATE_FOLDER Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getParentHandle - Returns the handle of the parent folder
            MegaRequest::getName - Returns the name of the new folder
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getNodeHandle - Handle of the new folder
        name - Name of the new folder
        parent - Parent folder
        listener - MegaRequestListener to track this request         
        ''' 
    def moveNode(self, node, newParent, listener = None): 
        '''Move a node in the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_MOVE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node to move
            MegaRequest::getParentHandle - Returns the handle of the new parent for the node
        node - Node to move
        newParent - New parent for the node
        listener - MegaRequestListener to track this request 
        '''
    def copyNode(self, node, newParent, listener = None):
        '''Copy a node in the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node to copy
            MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
            MegaRequest::getPublicMegaNode - Returns the node to copy (if it is a public node)
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getNodeHandle - Handle of the new node
        node - Node to copy
        newParent - Parent for the new node
        listener - MegaRequestListener to track this request        
        ''' 
    def renameNode(self, node, newName, listener = None):
        '''Rename a node in the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_RENAME Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node to rename
            MegaRequest::getName - Returns the new name for the node
        node - Node to modify
        newName - New name for the node
        listener - MegaRequestListener to track this request     
        ''' 
    def remove(self, node, listener = None):
        '''Remove a node from the MEGA account.
        This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move the node to the Rubbish Bin use MegaApi::moveNode
        The associated request type with this request is MegaRequest::TYPE_REMOVE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node to remove
        node - Node to remove
        listener - MegaRequestListener to track this request         
        ''' 
    def sendFileToUser(self, node, user, listener = None):
        '''Send a node to the Inbox of another MEGA user using a MegaUser.
        The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node to send
            MegaRequest::getEmail - Returns the email of the user that receives the node
        node - Node to send
        user - User that receives the node
        listener - MegaRequestListener to track this request        
        ''' 
    def share(self, node, user, level, listener = None):
        '''Share or stop sharing a folder in MEGA with another user using a MegaUser.
        To share a folder with an user, set the desired access level in the level parameter. If you want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
        The associated request type with this request is MegaRequest::TYPE_COPY Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the folder to share
            MegaRequest::getEmail - Returns the email of the user that receives the shared folder
            MegaRequest::getAccess - Returns the access that is granted to the user
        node - The folder to share. It must be a non-root folder
        user - User that receives the shared folder
        level - Permissions that are granted to the user Valid values for this parameter:
            MegaShare::ACCESS_UNKNOWN = -1 Stop sharing a folder with this user
            MegaShare::ACCESS_READ = 0
            MegaShare::ACCESS_READWRITE = 1
            MegaShare::ACCESS_FULL = 2
            MegaShare::ACCESS_OWNER = 3
        listener - MegaRequestListener to track this request         
        ''' 
    def importFileLink(self, megaFileLink, parent, listener = None):
        '''Import a public link to the account.
        The associated request type with this request is MegaRequest::TYPE_IMPORT_LINK Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getLink - Returns the public link to the file
            MegaRequest::getParentHandle - Returns the folder that receives the imported file
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getNodeHandle - Handle of the new node in the account
        megaFileLink - Public link to a file in MEGA
        parent - Parent folder for the imported file
        listener - MegaRequestListener to track this request         
        ''' 
    def getPublicNode(self, megaFileLink, listener = None):
        '''Get a MegaNode from a public link to a file.
        A public node can be imported using MegaApi::copyNode or downloaded using MegaApi::startDownload
        The associated request type with this request is MegaRequest::TYPE_GET_PUBLIC_NODE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getLink - Returns the public link to the file
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getPublicMegaNode - Public MegaNode corresponding to the public link
        megaFileLink - Public link to a file in MEGA
        listener - MegaRequestListener to track this request 
        ''' 
    def getThumbnail(self, node,dstFilePath, listener = None):
        '''Get the thumbnail of a node.
        If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT error code
        The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getFile - Returns the destination path
            MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
        node - Node to get the thumbnail
        dstFilePath - Destination path for the thumbnail. If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
        listener - MegaRequestListener to track this request 
        ''' 
    def getPreview(self, node,dstFilePath, listener = None): 
        '''Get the preview of a node.
        If the node doesn't have a preview the request fails with the MegaError::API_ENOENT error code
        The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getFile - Returns the destination path
            MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
        node - Node to get the preview
        dstFilePath - Destination path for the preview. If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
        listener - MegaRequestListener to track this request         
        '''
    def getUserAvatar(self, user,dstFilePath, listener = None):
        '''Get the avatar of a MegaUser.
        The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getFile - Returns the destination path
            MegaRequest::getEmail - Returns the email of the user
        user - MegaUser to get the avatar
        dstFilePath - Destination path for the avatar. It has to be a path to a file, not to a folder. If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg") will be used as the file name inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
        listener - MegaRequestListener to track this request        
        ''' 
    # Need clarification def getUserAttribute(self, *args): 
    def cancelGetThumbnail(self, node, listener = None):
        '''Cancel the retrieval of a thumbnail.
        The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
        node - Node to cancel the retrieval of the preview
        listener - listener	MegaRequestListener to track this request 
        ''' 
    def cancelGetPreview(self, node, listener = None):
        '''Cancel the retrieval of a preview.
        The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
        node - Node to cancel the retrieval of the preview
        listener - listener	MegaRequestListener to track this request       
        ''' 
    def setThumbnail(self, node, srcFilePath, listener = None ):
        '''Set the thumbnail of a MegaNode.
        The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getFile - Returns the source path
            MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
        node - MegaNode to set the thumbnail
        srcFilePath - Source path of the file that will be set as thumbnail 
        listener - MegaRequestListener to track this request        
        ''' 
    def setPreview(self, node, srcFilePath, listener = None ): 
        '''Set the preview of a MegaNode.
        The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getFile - Returns the source path
            MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
        node - MegaNode to set the preview
        srcFilePath - Source path of the file that will be set as preview 
        listener - MegaRequestListener to track this request
        '''
    def setAvatar(self, srcFilePath, listener = None):
        '''Set the avatar of the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getFile - Returns the source path
        srcFilePath - Source path of the file that will be set as avatar 
        listener - MegaRequestListener to track this request
        ''' 
    # Need clarification def setUserAttribute(self, *args): 
    
    def exportNode(self, node, listener = None):
        '''Generate a public link of a file/folder in MEGA.
        The associated request type with this request is MegaRequest::TYPE_EXPORT Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getAccess - Returns true
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getLink - Public link
        node - MegaNode to get the public link
        listener - MegaRequestListener to track this request        
        ''' 
    def disableExport(self, node, listener = None):
        '''Stop sharing a file/folder.
        The associated request type with this request is MegaRequest::TYPE_EXPORT Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getNodeHandle - Returns the handle of the node
            MegaRequest::getAccess - Returns false
        node - MegaNode to stop sharing
        listener - MegaRequestListener to track this request
        ''' 
    def fetchNodes(self, listener=None):
        '''Fetch the filesystem in MEGA.
        The MegaApi object must be logged in in an account or a public folder to successfully complete this request.
        The associated request type with this request is MegaRequest::TYPE_FETCH_NODES
        listener - MegaRequestListener to track this request       
        ''' 
    def getAccountDetails(self, listener=None): 
        '''Get details about the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getMegaAccountDetails - Details of the MEGA account
        listener - MegaRequestListener to track this request
        '''
    # Need clarification def getExtendedAccountDetails(self, sessions=False, purchases=False, transactions=False, listener=None): 
    def getPricing(self, listener=None):
        '''Get the available pricing plans to upgrade a MEGA account.
        You can get a payment URL for any of the pricing plans provided by this function using MegaApi::getPaymentUrl
        The associated request type with this request is MegaRequest::TYPE_GET_PRICING
        Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
            MegaRequest::getPricing - MegaPricing object with all pricing plans
        listener - MegaRequestListener to track this request
        ''' 
    # Need clarification Same as getPaymentUrl ? def getPaymentId(self, *args): 
    # Need clarification def upgradeAccount(self, *args): 
    # Need clarification def submitPurchaseReceipt(self, *args): 
    # Need clarification def creditCardStore(self, *args): 
    # Need clarification def creditCardQuerySubscriptions(self, listener=None): 
    # Need clarification def creditCardCancelSubscriptions(self, *args): 
    # Need clarification def getPaymentMethods(self, listener=None): 
    def exportMasterKey(self): 
        '''Export the master key of the account.
        The returned value is a Base64-encoded string
        With the master key, it's possible to start the recovery of an account when the password is lost:
            https://mega.co.nz/#recovery
        You take the ownership of the returned value.
        Returns Base64-encoded master key
        '''
    def changePassword(self, oldPassword, newPassword, listener = None):
        '''Change the password of the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_CHANGE_PW Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getPassword - Returns the old password
            MegaRequest::getNewPassword - Returns the new password
        oldPassword - old password
        newPassword - new password
        listener - MegaRequestListener to track this request 
        ''' 
    def addContact(self, email, listener = None):
        '''Add a new contact to the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_ADD_CONTACT Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getEmail - Returns the email of the contact
        email - Email of the new contact
        listener - MegaRequestListener to track this request 
        ''' 
    def removeContact(self, user, listener = None):
        '''Remove a contact to the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_REMOVE_CONTACT Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getEmail - Returns the email of the contact
        user - 	MegaUser of the contact (see MegaApi::getContact) 
        listener - MegaRequestListener to track this request
        ''' 
    def logout(self, listener=None): 
        '''Logout of the MEGA account.
        The associated request type with this request is MegaRequest::TYPE_LOGOUT 
        listener - MegaRequestListener to track this request 
        '''
    # Need clarification def localLogout(self, listener=None):
 
    def startUpload(self, localPath, parent, listener = None):
        '''Upload a file. 
        localPath - Local path of the file 
        parent - Parent node for the file in the MEGA account
        listener - MegaTransferListener to track this transfer 
        ''' 
    def startDownload(self, node, localPath, listener = None):
        '''Download a file from MEGA.
        node - MegaNode that identifies the file 
        localPath - Destination path for the file If this path is a local folder, it must end with a '\' or '/' character and the file name in MEGA will be used to store a file inside that folder. If the path doesn't finish with one of these characters, the file will be downloaded to a file in that path.
        listener - MegaTransferListener to track this transfer 
        ''' 
    def startStreaming(self, node, startPos, size, listener):
        '''Start an streaming download. 
        Streaming downloads don't save the downloaded data into a local file. It is provided in MegaTransferListener::onTransferUpdate in a byte buffer. 
        Only the MegaTransferListener passed to this function will receive MegaTransferListener::onTransferData callbacks. MegaTransferListener objects registered with MegaApi::addTransferListener won't receive them for performance reasons
        node - MegaNode that identifies the file (public nodes aren't supported yet)
        startPos - First byte to download from the file
        size - Size of the data to download
        listener - MegaTransferListener to track this transfer 
        ''' 
    def cancelTransfer(self,  	transfer, listener = None):
        '''Cancel a transfer.
        When a transfer is cancelled, it will finish and will provide the error code MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and MegaListener::onTransferFinish
        The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
        transfer - MegaTransfer object that identifies the transfer You can get this object in any MegaTransferListener callback or any MegaListener callback related to transfers.
        listener - MegaRequestListener to track this request 
        '''
    # need clarification def cancelTransferByTag(self, *args):
     
    def cancelTransfers(self, type, listener = None):
        '''Cancel all transfers of the same type.
        The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFERS Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getParamType - Returns the first parameter
        type - 	Type of transfers to cancel. Valid values are:
            MegaTransfer::TYPE_DOWNLOAD = 0
            MegaTransfer::TYPE_UPLOAD = 1
        listener - MegaRequestListener to track this request 
        ''' 
    def pauseTransfers(self, pause, listener = None):
        '''Pause/resume all transfers.
        The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS Valid data in the MegaRequest object received on callbacks:
            MegaRequest::getFlag - Returns the first parameter
        pause - True to pause all transfers or False to resume all transfers
        listener - MegaRequestListener to track this request        
        ''' 
    # Need clarification def areTansfersPaused(self, *args): 
    def setUploadLimit(self, bpslimit):
        '''Set the upload speed limit.
        The limit will be applied on the server side when starting a transfer. Thus the limit won't be applied for already started uploads and it's applied per storage server.
        bpslimit - -1 to automatically select the limit, 0 for no limit, otherwise the speed limit in bytes per second 
        ''' 
    # Needs clarification def getTransferByTag(self, *args): 
    def getTransfers(self, type):
        '''Get all transfers of a specific type (downloads or uploads)
        If the parameter isn't MegaTransfer.TYPE_DOWNLOAD or MegaTransfer.TYPE_UPLOAD this function returns an empty list.
        You take the ownership of the returned value
        type - MegaTransfer.TYPE_DOWNLOAD or MegaTransfer.TYPE_UPLOAD
        Returns List with transfers of the desired type 
        '''
 
    def getNumChildren(self, parent): 
        '''Get the number of child nodes.
        If the node doesn't exist in MEGA or isn't a folder, this function returns 0
        This function doesn't search recursively, only returns the direct child nodes.
        parent - Parent node 
        Returns Number of child nodes
        '''
    def getNumChildFiles(self, parent):
        '''Get the number of child files of a node.
        If the node doesn't exist in MEGA or isn't a folder, this function returns 0
        This function doesn't search recursively, only returns the direct child files.
        parent - Parent node 
        Returns Number of child files
        ''' 
    def getNumChildFolders(self, parent): 
        '''Get the number of child folders of a node.
        If the node doesn't exist in MEGA or isn't a folder, this function returns 0
        This function doesn't search recursively, only returns the direct child folders.
        parent - Parent node 
        Returns Number of child folders
        '''
    def getChildren(self, parent, order = 1 ):
        '''Get all children of a MegaNode.
        If the parent node doesn't exist or it isn't a folder, this function returns None
        You take the ownership of the returned value 
        parent - parent node
        order Order of the returned list. Valid values are:
            MegaApi::ORDER_NONE = 0 Undefined order
            MegaApi::ORDER_DEFAULT_ASC = 1 Folders first in alphabetical order, then files in the same order
            MegaApi::ORDER_DEFAULT_DESC = 2 Files first in reverse alphabetical order, then folders in the same order
            MegaApi::ORDER_SIZE_ASC = 3 Sort by size, ascending
            MegaApi::ORDER_SIZE_DESC = 4 Sort by size, descending
            MegaApi::ORDER_CREATION_ASC = 5 Sort by creation time in MEGA, ascending
            MegaApi::ORDER_CREATION_DESC = 6 Sort by creation time in MEGA, descending
            MegaApi::ORDER_MODIFICATION_ASC = 7 Sort by modification time of the original file, ascending
            MegaApi::ORDER_MODIFICATION_DESC = 8 Sort by modification time of the original file, descending
            MegaApi::ORDER_ALPHABETICAL_ASC = 9 Sort in alphabetical order, ascending
            MegaApi::ORDER_ALPHABETICAL_DESC = 10 Sort in alphabetical order, descending
        '''
    def getIndex(self,  node, order = 1):
        '''Get the current index of the node in the parent folder for a specific sorting order.
        If the node doesn't exist or it doesn't have a parent node (because it's a root node) this function returns -1 
        node - Node to check
        order - Sorting order to use
        Returns index of the node in its parent folder
        '''
    def getChildNode(self,  parent, name):
        '''Get the child node with the provided name.
        If the node doesn't exist, this function returns None
        You take the ownership of the returned value
        parent - Parent node
        name - name of the node
        Returns The MegaNode that has the selected parent and name
        ''' 
    def getParentNode(self, node):
        '''Get the parent node of a MegaNode.
        If the node doesn't exist in the account or it is a root node, this function returns NULL
        You take the ownership of the returned value.
        node - MegaNode to get the parent
        Returns the parent of the provided node  
        ''' 
    def getNodePath(self,  	node):
        '''Get the path of a MegaNode.
        If the node doesn't exist, this function returns NULL. You can recoved the node later using MegaApi::getNodeByPath except if the path contains names with '/', '\' or ':' characters.
        You take the ownership of the returned value
        node - MegaNode for which the path will be returned 
        Returns the path of the node 
        ''' 
    def getNodeByPath(self, path, n = None):
        '''Get the MegaNode in a specific path in the MEGA account.
        The path separator character is '/' The Root node is / The Inbox root node is //in/ The Rubbish root node is //bin/
        Paths with names containing '/', '\' or ':' aren't compatible with this function.
        It is needed to be logged in and to have successfully completed a fetchNodes request before calling this function. Otherwise, it will return NULL.
        You take the ownership of the returned value
        path - Path to check
        n - Base node if the path is relative 
        Returns The MegaNode object in the path, otherwise None
        ''' 
    def getNodeByHandle(self, MegaHandler): 
        '''Get the MegaNode that has a specific handle. 
        You can get the handle of a MegaNode using MegaNode::getHandle. The same handle can be got in a Base64-encoded string using MegaNode::getBase64Handle. Conversions between these formats can be done using MegaApi::base64ToHandle and MegaApi::handleToBase64.
        It is needed to be logged in and to have successfully completed a fetchNodes request before calling this function. Otherwise, it will return None.
        You take the ownership of the returned value.
        MegaHandler - Node handle to check 
        Returns MegaNode object with the handle, otherwise None
        '''
    def getContacts(self):
        '''Get all contacts of this MEGA account.
        You take the ownership of the returned value
        Returns List of MegaUser object with all contacts of this account 
        ''' 
    def getContact(self, email):
        '''Get the MegaUser that has a specific email address.
        You can get the email of a MegaUser using MegaUser::getEmail
        You take the ownership of the returned value
        email - Email address to check 
        Returns MegaUser that has the email address, otherwise None 
        ''' 
    def getInShares(self, user):
        '''Get a list with all inbound sharings from one MegaUser.
        You take the ownership of the returned value
        user - MegaUser sharing folders with this account
        Returns List of MegaNode objects that this user is sharing with this account  
        ''' 
    def isShared(self, node):
        '''Check if a MegaNode is being shared.
        For nodes that are being shared, you can get a a list of MegaShare objects using MegaApi::getOutShares
        node - Node to check 
        Returns True if the MegaNode is being shared, otherwise false 
        ''' 
    def getOutShares(self, node): 
        '''Get a list with the active outbound sharings for a MegaNode.
        If the node doesn't exist in the account, this function returns an empty list.
        You take the ownership of the returned value
        node - MegaNode to check
        Returns List of MegaShare objects  
        '''
    def getAccess(self, node): 
        '''Get the access level of a MegaNode. 
        node - MegaNode to check 
        Returns Access level of the node Valid values are:           
            MegaShare::ACCESS_OWNER
            MegaShare::ACCESS_FULL
            MegaShare::ACCESS_READWRITE
            MegaShare::ACCESS_READ
            MegaShare::ACCESS_UNKNOWN
        ''' 
    def getSize(self, node): 
        '''Get the size of a node tree.
        If the MegaNode is a file, this function returns the size of the file. If it's a folder, this fuction returns the sum of the sizes of all nodes in the node tree.
        node - Parent node
        Returns size of the node tree 
        '''
    def getFingerprint(self, node):
        '''Get a Base64-encoded fingerprint for a node.
        If the node doesn't exist or doesn't have a fingerprint, this function returns None.
        You take the ownership of the returned value
        node - Node for which we want to get the fingerprint
        Returns Base64-encoded fingerprint for the file 
        '''
    def getNodeByFingerprint(self, fingerprint	):
        '''Returns a node with the provided fingerprint.
        If there isn't any node in the account with that fingerprint, this function returns None.
        You take the ownership of the returned value.
        fingerprint - Fingerprint to check
        Returns MegaNode object with the provided fingerprint
        ''' 
    def hasFingerprint(self, fingerprint):
        '''Check if the account already has a node with the provided fingerprint.
        A fingerprint for a local file can be generated using MegaApi::getFingerprint
        fingerprint - Fingerprint to check 
        Returns True if the account contains a node with the same fingerprint 
        '''
    # Need clarification def getCRC(self, *args): 
    # Need clarification def getNodeByCRC(self, *args): 
    def checkAccess(self, node, level):
        '''Check if a node has an access level. 
        node - Node to check
        level - Access level to check Valid values for this parameter are:        
            MegaShare::ACCESS_OWNER
            MegaShare::ACCESS_FULL
            MegaShare::ACCESS_READWRITE
            MegaShare::ACCESS_READ
        Returns MegaError object with the result: Valid values for the error code are:        
            MegaError::API_OK - The node can be moved to the target
            MegaError::API_EACCESS - The node can't be moved because of permissions problems
            MegaError::API_ECIRCULAR - The node can't be moved because that would create a circular linkage
            MegaError::API_ENOENT - The node or the target doesn't exist in the account
            MegaError::API_EARGS - Invalid parameters
        '''
    def checkMove(self, node, target ): 
        '''Check if a node can be moved to a target node.
        node - Node to check
        target - Target for the move operation
        Returns MegaError object with the result: Valid values for the error code are:        
            MegaError::API_OK - The node can be moved to the target
            MegaError::API_EACCESS - The node can't be moved because of permissions problems
            MegaError::API_ECIRCULAR - The node can't be moved because that would create a circular linkage
            MegaError::API_ENOENT - The node or the target doesn't exist in the account
            MegaError::API_EARGS - Invalid parameters
        '''
    def getRootNode(self):
        '''Returns the root node of the account.
        You take the ownership of the returned value
        If you haven't successfully called MegaApi::fetchNodes before, this function returns None
        Returns Root node of the account
        '''  
    def getInboxNode(self): 
        '''Returns the inbox node of the account.
        You take the ownership of the returned value
        If you haven't successfully called MegaApi::fetchNodes before, this function returns None
        Returns Inbox node of the account
        ''' 
    def getRubbishNode(self):
        '''Returns the rubbish node of the account.
        You take the ownership of the returned value
        If you haven't successfully called MegaApi::fetchNodes before, this function returns None
        Returns Rubbish node of the account
        ''' 
    def search(self, node, searchString, recursive = True):
        '''Search nodes containing a search string in their name.The search is case-insensitive.
        node - The parent node of the tree to explore
        searchString - Search string. The search is case-insensitive
        recursive - True if you want to seach recursively in the node tree. False if you want to    seach in the children of the node only
        Returns list of nodes that contain the desired string in their name
        ''' 
    def processMegaTree(self, node, processor, recursive = True):
        '''Process a node tree using a MegaTreeProcessor implementation. 
node - The parent node of the tree to explore
processor - MegaTreeProcessor that will receive callbacks for every node in the tree
recursive - True if you want to recursively process the whole node tree. False if you want to process the children of the node only
Returns True  if all nodes were processed. False otherwise (the operation can be cancelled by MegaTreeProcessor::processMegaNode()) 
        ''' 
    # Need clarification def getVersion(self): 
    # Need clarification def getUserAgent(self): 
    # Need clarification def changeApiUrl(self, *args): 
    # Need clarification def nameToLocal(self, *args): 
    # Need clarification def localToName(self, *args): 
    # Need clarification def createThumbnail(self, *args): 
    # Need clarification def createPreview(self, *args): 
    # Need clarification def MegaApi_base32ToHandle(*args):

    def MegaApi_base64ToHandle(base64Handle):
        '''Converts a Base64-encoded node handle to a MegaHandle.
The returned value can be used to recover a MegaNode using MegaApi::getNodeByHandle You can revert this operation using MegaApi::handleToBase64
        base64Handle - Base64-encoded node handle 
        Returns Node hanlde
        '''
    # Need clarification def MegaApi_handleToBase64(*args):


    # Need clarification def MegaApi_userHandleToBase64(*args):


    def MegaApi_addEntropy(data, size):
        '''Add entropy to internal random number generators.
It's recommended to call this function with random data specially to enhance security,
        data - Byte array with random data
        size - Size of the byte array (in bytes)        
        '''
    def MegaApi_setLogLevel(logLevel):
        '''Set the active log level.
This function sets the log level of the logging system. If you set a log listener using MegaApi::setLoggerObject, you will receive logs with the same or a lower level than the one passed to this function.
        logLevel - Active log level
These are the valid values for this parameter:
    MegaApi::LOG_LEVEL_FATAL = 0
    MegaApi::LOG_LEVEL_ERROR = 1
    MegaApi::LOG_LEVEL_WARNING = 2
    MegaApi::LOG_LEVEL_INFO = 3
    MegaApi::LOG_LEVEL_DEBUG = 4
    MegaApi::LOG_LEVEL_MAX = 5    
        '''
    def MegaApi_setLoggerObject(megaLogger):
        '''Set a MegaLogger implementation to receive SDK logs.
Logs received by this objects depends on the active log level. By default, it is MegaApi::LOG_LEVEL_INFO. You can change it using MegaApi::setLogLevel.
        megaLogger - MegaLogger implementation 
        '''
    def MegaApi_log(logLevel,message, filename = "", line = -1 ):
        '''Send a log to the logging system.
This log will be received by the active logger object (MegaApi::setLoggerObject) if the log level is the same or lower than the active log level (MegaApi::setLogLevel)
The third and the fouth parameget are optional. You may want to use FILE and LINE to complete them.
        logLevel - Log level for this message
        message - Message for the logging system
        filename - Origin of the log message
        line - Line of code where this message was generated 
        '''
    # Need clarification def MegaApi_base64ToBase32(*args):

    # Need clarification def MegaApi_base32ToBase64(*args):

    def MegaApi_strdup(buffer):
        '''Function to copy a buffer.
        The new buffer is allocated by new[] so you should release it with delete[].
        buffer - Character buffer to copy
        Returns Copy of the character buffer          
        '''
    # Need clarification def MegaApi_removeRecursively(path):
          
    	        
class MegaGfxProcessor():
    '''Interface to provide an external GFX processor. You can implement this interface to provide a graphics
    processor to the SDK in the MegaApi::MegaApi constructor. That way, SDK will use your implementation to
    generate thumbnails/previews when needed. Images will be sequentially processed. At first the SDK will call
    MegaGfxProcessor::readBitmap with the path of the file. Then, it will call MegaGfxProcessor::getWidth and
    MegaGfxProcessor::getHeight to get the dimensions of the file (in pixels). After that, the SDK will call 
    MegaGfxProcessorLLgetBitmapDataSize and MegaGfxProcessor::getBitmapData in a loop to get thumbnails/previews
    of different sizes. Finally, the SDK will call MegaGfxProcessor::freeBitmap to let you free the resources 
    required to process the current file. If the image has EXIF data, it should be roatated/mirrored before doing any other
    processing. MegaGfxProcessor::GetWidth, MegaGfxProcessor::getHeight and all other coordinates in this interface are expressed
    over the image after the required transformation based on EXIF data. Generated images must be in JPG format.
    '''
    def __init__(self): 
        if self.__class__ == MegaGfxProcessor:
            _self = None
        else:
            _self = self
        this = _mega.new_MegaGfxProcessor(_self, )
        try: self.this.append(this)
        except: self.this = this

    def freeBitmap(self):
        '''Free resources associated with the processing of the current image. With a call of this
        function, the processing of the image started with a call to MegaGfxProcesor::readBitmap ends. No other
        functions will be called to continue processing the current image, so you can free all related resources.
        '''
    def getBitmapData(self, bitmapData, size):
        '''Copy the thumbnail/preview data to a buffer provided by the SDK. The SDJ uses this function
        immediately after MegaGfxProcessor::getBitmapDataSize when that function succeed. The implementation of this
        function must copy the data of the image in the buffer provided by the SDK. The size of this buffer will be
        the same as the value returned in the previous call to MegaGfxProcessor::getBitmapDataSize. The size is
        provided in the second parameter.
        bitmapData - preallocated buffer in which the implementation must write the generated image.
        size - size of the buffer. It will be the same that the previous return value of MegaGfxProcessor::getBitmapDataSize.
        Returns true if success else false.
        '''
    def getBitmapDataSize(self, width, height, px, py, rw, rh):
        '''Generates a thumbnail/preview image. This function provides the parameters that the SDK wants to generate.
        If the implementation can create it, it has to provide the size of the buffer (in bytes) that it needs to
        store the generated JPG image. Otherwise it shoud return a number <= 0. The implementation of this
        function has to scale the image to the size (width, height) and then extract the rectangle starting
        at the point (px, py) with size (rw, rh). (px, py, rw, and rh) are expressed in pixels over
        the scaled image, being the point (0, 0) the upper-left corner of the scaled image, with the X-axis growing
        to the right and the Y-axis growing to the bottom.
        width - width of the scaled image from which the thumbnail/preview image will be extracted.
        height - height of the scaled image from which the thumbnail/preview image will be extracted.
        px - X coordinate of the starting point of the desired image (in pixels over the scaled image).
        py - Y coordinate of the starting point of the desired image (in pixels over the scaled image).
        rw - width of the desired image (in pixels over the scaled image)
        rh - height of the desired image (in pixels over the scaled image)
        Returns the size of the buffer required ti store the image (in bytes) or a number <=0 if it is not possible
        to generate it.
        '''
    def getWidth(self):
        '''Returns the width of the image. This function must return the width of the image at the path provided
        in MegaGfxProcessor::readBitmap. If a number <= 0 is returned, the image wont be processed.
        '''
    def getHeight(self):
        '''Returns the height of the image. This function must return the height of the image at the path provided
        in MegaGfxProcessor::readBitmap. If a number <= 0 is returned, the image wont be processed.
        '''
    def readBitmap(self, path):
        '''Read the image file and check if it can be processed. This is the function that will be called to process
        an image. No other functions of this interface will be called before this one. The recommended implementation
        is to read the file, check if it is an image and get its dimensions. If everything is ok, the function should
        return True. if the file isn't an image or can't be processed, the function should return False. The SDK will
        call this function with all files so it is recommended to check the extension before trying to open those files.
        path - path of the file that is going to be processed.
        Returns True if the implementation is able to manage the file, else False.
        '''
class MegaProxy():
    '''Contains the information related to the proxy server. Pass an object of this class to MegaApi:setProxySettings
    to start using a proxy server. Currently, only HTTP proxies are allowed. The proxy server should support HTTP request
    and tunneling for HTTPS.
    '''

    def __init__(self): 
        this = _mega.new_MegaProxy()
        try: self.this.append(this)
        except: self.this = this

    def setProxyType(self, proxyType):
        '''Sets the type of the proxy, the allowed values in the current version are:
        PROXY_NONE no proxy
        PROXY_AUTO automatic detaction(default)
        PROXY_CUSTOM a proxy with a settings provided by the user
        Note: PROXY_AUTO is currently supported only on WINDOWS, for other platforms PROXY_NONE will be used as
        default detection value.
        proxyType sets the type of proxy
        '''
    def setProxyUrl(self, proxyURL):
        '''Sets the URL of the proxy. That URL MUST follow this format: "<scheme>://hostname|ip>:<port>". An example
        of this : http://127.0.0.1:8080
        proxyURL url of the proxy
        '''
    def setCredentials(self, username, password):
        '''Set the credentials needed to use the proxy. If you don't need to use any credentials, DO NOT use this
        function or pass NULL as the username parameter.
        username - username to access the proxy or NULL if credentials aren't needed.
        password - password to access the proxy
        '''
    def getUsername(self):
        '''Return the username required to access the proxy. The MegaProxy object retains the ownership of this
        value. It will be valid until the MegaProxy::setCredentials is called (that will delete the previous value)
        or until the MegaProxy object is deleted.
        Returns the username required to access the proxy.
        '''
    def getProxyURL(self):
        '''Returns the URL of the proxy, previously set with MegaProxy::setProxyURL. The MegaProxy object
        retains the ownership of the returned value. It will be valid intil the MegaProxy::setProxyURL is called
        (that will delete the previous value) or until the MegaProxy object is deleted.
        Returns the URL of the proxy
        '''
    def getProxyType(self):
        '''Return the current proxy type of the object from the allowed (PROXY_NONE, PROXY_AUTO, PROXY_CUSTOM).
        Returns current proxy type.
        '''
    def getPassword(self):
        '''Return the username required to access the proxy. The MegaProxy object retains the ownership of the
        returned value. It will be valid until the MegaProxy::setCredentials is called (that will delete the previous
        value) or until the MegaProxy object is deleted.
        Returns password required to access the proxy
        '''
    def credentialsNeeded(self):
        '''Returns True if credentials are needed to access the proxy, else False. The default value of this function
        is false. It will return True after calling MegaProxy::setCredentials with a non Null parameter.
        Returns True if credentials are needed to access the proxy, else return False.
        '''
class MegaListener():
    '''Interface to get all information related to a MEGA account. Implementations of this interface can receive
    all events (request, transfer, global) and two additional events related to the synchronization engine. The
    SDK will provide a new interface to get synchronization events separately in future updates.
    '''
    def __init__(self): 
        if self.__class__ == MegaListener:
            _self = None
        else:
            _self = self
        this = _mega.new_MegaListener(_self, )
        try: self.this.append(this)
        except: self.this = this

    def onRequestStart(self, api, request):
        '''This function is called when a request is about to start being processed.
        The SDK retains the ownership of the request parameter. Don't use it after this function returns.
        The api object is the one created by the application, it will be valid untill the application
        deletes it.
        api - object that started the request.
        request - info about the request.
        '''   
    def onRequestFinish(self, api, request, error):
        ''' This function is called when a request has finished. There won't be more callbacks
        about this request. The last parameter provides the result of the request. If the request
        finished without problems, the error code will be API_OK. The SDK retains the ownership
        of the request and error parameters. Don't use them after this function returns. The API
        object is the one created by the application, it will be valid until the application 
        deletes it.
        api - object that started the request.
        request - info about the request.
        error - info about error
        '''
    def onRequestUpdate(self, api, request):
        '''This function is called to inform about the progress of a request. Currently, this
        callback is only used for fetchNodes(MegaRequest:TYPE_FETCH_NODES) requests. The SDK
        retains the ownership of the request parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application
        deletes it.
        api - object that started the request.
        request - info about the request.
        error - info about error
        ''' 
    def onRequestTemporaryError(self, api, request, error):
        '''This function is called when there is a temporary error processing a request. The request
        continues after this callback, so expect more MegaRequestListenere::onRequestTemporaryError
        or a MegaRequestListener::onRequestFinish callback. The SDK
        retains the ownership of the request parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application
        deletes it.
        api - object that started the request.
        request - info about the request.
        error - info about error
        ''' 
    def onTransferStart(self, api, transfer):
        '''This function is called when a transfer is about to start being processed. The SDK
        retains the ownership of the request parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application
        deletes it.
        api - object that started the request.
        transfer - info about transfer.   
        '''
    def onTransferFinish(self, api, transfer, error):
        '''This function is called when a transfer has finished. The SDK
        retains the ownership of the request parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application
        deletes it.
        api - object that started the request.
        transfer - info about transfer.
        error - info about error.
        '''
    def onTransferUpdate(self, api, transfer):
        '''This function is called to informa about the progress of a transfer. The SDK retains
        the ownershio of the transfer parameter. Don't use it after the function retuns. The api
        object is the one created by the application, it will be valid until the application
        deletes it.
        api - object that started the request.
        transfer - info about transfer.
        '''
    def onTransferTemporaryError(self, api, transfer, error):
        '''This function is called when there is a temporary error processing a transfer. The transfer
        continues after this callback, so expect more MegaTransferListener::onTransferTemporaryError or
        a MegaTransferListener::onTransferFinish callback. The SDK
        retains the ownership of the request parameter. Don't use it after this functions returns.
        api - object that started the request.
        transfer - info about transfer.
        error - info about error.
        '''
    def onUsersUpdate(self, api, users):
        '''This function is called when there are new or updated contacts in the account. The SDK retains
        the ownership of the MegaUserList in the second parameter. The list and all the MegaUser objects 
        that it contains will be valid until the function returns. If you want to save the list, use
        MegaUserList::copy(). If you want to save only some of the MegaUser objects, use MegaUser::copy() for
        those object.
        api - object that started the request
        users - list that ontains the new or updated contacts.
        '''
    def onNodesUpdate(self, api, nodes):
        '''This function is called when there are new or updated nodes in the account. When the full
        account is reloaded or a large number of server notification arrives at once, the second parameter
        will be NULL. The SDK retains the ownership of the MegaNodeList in the second parameter. The list
        and all the MegaNode objects that it contains will be valid until this function returns. If you want
        to save the list, use MegaNodeList::copy(). If you want to save only some of the MegaNode objects, use
        MegaNode::copy() for those particular nodes.
        api - object connected to the account
        nodes - list that contains the new or updated nodes.
        '''
    # Need clarification  def onAccountUpdate(): 
    
    def onReloadNeeded(self, api):
        '''This function is called when an inconsistencty is detected in the local cache. You
        should call MegaApi::fetchNodes() when this callback is received.
        api- object connected to the account.
        ''' 
class MegaLogger:
    '''Interface to receive SDK logs.
    You can implement this class and pass an object of your subclass to MegaApi::setLoggerClass to receive SDK logs. You will have to use also MegaApi::setLogLevel to select the level of the logs that you    want to receive. 
    '''
    def log(self, time, loglevel, source, message):
        '''This function will be called with all logs with level <= your selected level of logging (by default it is MegaApi::LOG_LEVEL_INFO) 
        time - Readable string representing the current time.
        loglevel - Log level of this message. Valid values are:            
            MegaApi::LOG_LEVEL_FATAL = 0
            MegaApi::LOG_LEVEL_ERROR = 1
            MegaApi::LOG_LEVEL_WARNING = 2
            MegaApi::LOG_LEVEL_INFO = 3
            MegaApi::LOG_LEVEL_DEBUG = 4
            MegaApi::LOG_LEVEL_MAX = 5
        source - Location where this log was generated. For logs generated inside the SDK, this will contain the source file and the line of code. The SDK retains the ownership of this string, it won't be valid after this funtion returns.
        message -Log message
        '''
class MegaPricing():
    '''Details about pricing plans.
    Use MegaApi::getPricing to get the pricing plans to upgrade MEGA accounts 
    '''
    def getNumProducts(self):
        '''Get the number of available products to upgrade the account. 
        Returns the number of available products.
        ''' 
    def getHandle(self, productIndex):
        '''Get the handle of a product. 
        productIndex - Product index (from 0 to MegaPricing::getNumProducts)
        Returns handle of the product.
        ''' 
    def getProLevel(self, productIndex):
        '''Get the PRO level associated with the product. 
        productIndex - Product index (from 0 to MegaPricing::getNumProducts)
        Returns PRO level associated with the product: Valid values are:            
            MegaAccountDetails::ACCOUNT_TYPE_FREE = 0
            MegaAccountDetails::ACCOUNT_TYPE_PROI = 1
            MegaAccountDetails::ACCOUNT_TYPE_PROII = 2
            MegaAccountDetails::ACCOUNT_TYPE_PROIII = 3
        ''' 
    def getGBStorage(self, productIndex):
        '''Get the number of GB of storage associated with the product. 
        productIndex - Product index (from 0 to MegaPricing::getNumProducts) 
        Returns number of GB of storage
        ''' 
    def getGBTransfer(self, productIndex): 
        '''Get the number of GB of bandwidth associated with the product. 
        productIndex - Product index (from 0 to MegaPricing::getNumProducts) 
        Returns number of GB of bandwith
        '''
    def getMonths(self, productIndex):
        '''Get the duration of the product (in months) 
        productIndex - Product index (from 0 to MegaPricing::getNumProducts)
        Returns duration of the product (in months) 
        ''' 
    def getAmount(self, productIndex):
        '''Get the price of the product (in cents) 
        productIndex - Product index (from 0 to MegaPricing::getNumProducts)
        Returns price of the product in cents.
        ''' 
    def getCurrency(self, productIndex): 
        '''Get the currency associated with MegaPricing::getAmount.
        The SDK retains the ownership of the returned value.
        productIndex - Product index (from 0 to MegaPricing::getNumProducts)
        Returns currency associated with MegaPricing::getAmount 
        '''
    def getDescription(self, productIndex): 
        '''Get the description of the product.
        productIndex - Product index (from 0 to MegaPricing::getNumProducts)
        Returns description of the product
        '''
    # Need clarification def getIosID(self, *args): 
    # Need clarification def getAndroidID(self, *args): 
    def copy(self):
        '''Creates a copy of this MegaPricing object.
        The resulting object is fully independent of the source MegaPricing, it contains a copy of all internal attributes, so it will be valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the MegaPricing object.
        ''' 
class MegaGlobalListener():
    '''Interface to get information about global events.
    You can implement this interface and start receiving events calling MegaApi::addGlobalListener
    MegaListener objects can also receive global events     
    ''' 
    def onNodesUpdate(self, api, nodes):
        '''This function is called when there are new or updated nodes in the account.
        When the full account is reloaded or a large number of server notifications arrives at once, the second parameter will be NULL.
        The SDK retains the ownership of the MegaNodeList in the second parameter. The list and all the MegaNode objects that it contains will be valid until this function returns. If you want to save the list, use MegaNodeList::copy. If you want to save only some of the MegaNode objects, use MegaNode::copy for those nodes.
        api - MegaApi object connected to the account
        nodes - List that contains the new or updated nodes 
        ''' 
    def onUsersUpdate(self, api, users): 
        '''This function is called when there are new or updated contacts in the account.
        The SDK retains the ownership of the MegaUserList in the second parameter. The list and all the MegaUser objects that it contains will be valid until this function returns. If you want to save the list, use MegaUserList::copy. If you want to save only some of the MegaUser objects, use MegaUser::copy for those objects.
        api - MegaApi object connected to the account
        users - List that contains the new or updated contacts 
        '''
    # Need clarification def onAccountUpdate(self, *args): 
    def onReloadNeeded(self, api):
        '''This function is called when an inconsistency is detected in the local cache.
        You should call MegaApi::fetchNodes when this callback is received  
        api - MegaApi object connected to the account 
        '''
class MegaTransferListener():
    '''Interface to receive information about transfers.
    All transfers allows to pass a pointer to an implementation of this interface in the last parameter. You can also get information about all transfers using MegaApi::addTransferListener
    MegaListener objects can also receive information about transfers .
    '''
    def onTransferStart(self, api, transfer):
        '''This function is called when a transfer is about to start being processed.
        The SDK retains the ownership of the transfer parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application deletes it.
        api - MegaApi object that started the transfer
        transfer - Information about the transfer 
        ''' 
    def onTransferFinish(self, api, transfer, error):
        '''This function is called when a transfer has finished.
        The SDK retains the ownership of the transfer and error parameters. Don't use them after this functions returns.
        The api object is the one created by the application, it will be valid until the application deletes it.
        There won't be more callbacks about this transfer. The last parameter provides the result of the transfer. If the transfer finished without problems, the error code will be API_OK
        api - MegaApi object that started the transfer
        transfer - Information about the transfer
        error - Error information 
        ''' 
    def onTransferUpdate(self, api, transfer):
        '''This function is called to inform about the progress of a transfer.
        The SDK retains the ownership of the transfer parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application deletes it.
        api - MegaApi object that started the transfer
        transfer - Information about the transfer
        ''' 
    def onTransferTemporaryError(self, api, transfer, error):
        '''This function is called when there is a temporary error processing a transfer.
        The transfer continues after this callback, so expect more MegaTransferListener::onTransferTemporaryError or a MegaTransferListener::onTransferFinish callback
        The SDK retains the ownership of the transfer and error parameters. Don't use them after this functions returns.
        api - MegaApi object that started the transfer
        transfer - Information about the transfer
        error - Error information
        ''' 
    def onTransferData(self, api, transfer, buffer, size):
        '''This function is called to provide the last readed bytes of streaming downloads.
        This function won't be called for non streaming downloads. You can get the same buffer provided by this function in MegaTransferListener::onTransferUpdate, using MegaTransfer::getLastBytes        MegaTransfer::getDeltaSize.
        The SDK retains the ownership of the transfer and buffer parameters. Don't use them after this functions returns.
        api - MegaApi object that started the transfer
        transfer - Information about the transfer
        buffer - Buffer with the last readed bytes
        size - Size of the buffer 
        Returns True to continue the transfer, False to cancel it
        ''' 
class MegaRequestListener():
    '''Interface to receive information about requests.
    All requests allows to pass a pointer to an implementation of this interface in the last parameter. You can also get information about all requests using MegaApi::addRequestListener
    MegaListener objects can also receive information about requests
    This interface uses MegaRequest objects to provide information of requests. Take into account that not all fields of MegaRequest objects are valid for all requests. See the documentation about each request to know which fields contain useful information for each one.     
    ''' 
    def onRequestStart(self, api, request): 
        '''This function is called when a request is about to start being processed.
        The SDK retains the ownership of the request parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application deletes it.
        api - MegaApi object that started the request
        request - Information about the request         
        '''
    def onRequestFinish(self, api, request, error): 
        '''This function is called when a request has finished.
        There won't be more callbacks about this request. The last parameter provides the result of the request. If the request finished without problems, the error code will be API_OK
        The SDK retains the ownership of the request and error parameters. Don't use them after this functions returns.
        The api object is the one created by the application, it will be valid until the application deletes it.
        api - MegaApi object that started the request
        request - Information about the request
        error - Error information         
        '''
    def onRequestUpdate(self, api, request): 
        '''This function is called to inform about the progres of a request.
        Currently, this callback is only used for fetchNodes (MegaRequest::TYPE_FETCH_NODES) requests
        The SDK retains the ownership of the request parameter. Don't use it after this functions returns.
        The api object is the one created by the application, it will be valid until the application deletes it.
        api - MegaApi object that started the request
        request - Information about the request         
        '''
    def onRequestTemporaryError(self, api, request, error): 
        '''This function is called when there is a temporary error processing a request.
        The request continues after this callback, so expect more MegaRequestListener::onRequestTemporaryError or a MegaRequestListener::onRequestFinish callback
        The SDK retains the ownership of the request and error parameters. Don't use them after this functions returns.
        The api object is the one created by the application, it will be valid until the application deletes it.
        api - MegaApi object that started the request
        request - Information about the request
        error - Error information         
        ''' 
        
class MegaTreeProcessor():
    '''Interface to process node trees.
    An implementation of this class can be used to process a node tree passing a pointer to MegaApi::processMegaTree     
    '''
    def processMegaNode(self, node):
        '''Function that will be called for all nodes in a node tree.
        node - Node to be processed
        Returns True to continue processing nodes, False to stop 
        '''
class MegaError():
    '''Provides information about an error.     
    '''
    def copy(self): 
        '''Creates a copy of this MegaError object.
        The resulting object is fully independent of the source MegaError, it contains a copy of all internal attributes, so it will be valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the MegaError object         
        '''
    def getErrorCode(self):
        '''Returns the error code associated with this MegaError.
        Returns error code associated with this MegaError         
        ''' 
    def __str__(self): 
        '''Returns a readable description of the error.
        This function returns a pointer to a statically allocated buffer. You don't have to free the returned pointer
        This function provides exactly the same result as MegaError::getErrorString. It's provided for a better Java compatibility
        Returns readible description of the error.
        ''' 
    # Helper function? Need clarification def __toString(self): 
    # Helper function? Need clarification def MegaError_getErrorString(*args):
    
class MegaTransfer():
    '''Provides information about a transfer.
        Developers can use listeners (MegaListener, MegaTransferListener) to track the progress of each transfer. MegaTransfer objects are provided in callbacks sent to these listeners and allow developers to know the state of the transfers, their parameters and their results.
    Objects of this class aren't live, they are snapshots of the state of the transfer when the object is created, they are immutable.     
    '''
    def copy(self):
        '''Creates a copy of this MegaTransfer object.
        The resulting object is fully independent of the source MegaTransfer, it contains a copy of all internal attributes, so it will be valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the MegaTransfer object        
        ''' 
    def getType(self):
        '''Returns the type of the transfer (TYPE_DOWNLOAD, TYPE_UPLOAD)
        Returns the type of the transfer (TYPE_DOWNLOAD, TYPE_UPLOAD)         
        ''' 
    def getTransferString(self): 
        '''Returns a readable string showing the type of transfer (UPLOAD, DOWNLOAD)
        Returns readable string showing the type of transfer (UPLOAD, DOWNLOAD)    
        '''    
    def __str__(self): 
        '''Returns a readable string that shows the type of the transfer.
        Returns readable string showing the type of transfer (UPLOAD, DOWNLOAD)       
        '''
    # Need clarification def __toString(self): 
    def getStartTime(self): 
        '''Returns the starting time of the request (in deciseconds)
        The returned value is a monotonic time since some unspecified starting point expressed in deciseconds.
        Returns starting time of the transfer (in deciseconds) 
        '''
    def getTransferredBytes(self): 
        '''Returns the number of transferred bytes during this request.
        Returns transferred bytes during this transfer 
        '''
    def getTotalBytes(self): 
        '''Returns the total bytes to be transferred to complete the transfer.
        Returns total bytes to be transferred to complete the transfer 
        '''
    def getPath(self): 
        '''Returns the local path related to this request.
        For uploads, this function returns the path to the source file. For downloads, it returns the path of the destination file.
        Returns local path related to this transfer 
        '''
    def getParentPath(self):     
        '''Returns the parent path related to this request.
        For uploads, this function returns the path to the folder containing the source file. For downloads, it returns that path to the folder containing the destination file.
        Returns parent path related to this transfer 
        '''
    def getNodeHandle(self): 
        '''Returns the handle related to this transfer.
        For downloads, this function returns the handle of the source node.
        For uploads, it returns the handle of the new node in MegaTransferListener::onTransferFinish and MegaListener::onTransferFinish when the error code is API_OK. Otherwise, it returns mega::INVALID_HANDLE.
        Returns the handle related to the transfer. 
        '''
    def getParentHandle(self): 
        '''Returns the handle of the parent node related to this transfer.
        For downloads, this function returns always mega::INVALID_HANDLE. For uploads, it returns the handle of the destination node (folder) for the uploaded file.
        Returns the handle of the destination folder for uploads, or mega::INVALID_HANDLE for downloads. 
        '''

    def getStartPos(self): 
        '''Returns the starting position of the transfer for streaming downloads.
        The return value of this fuction will be 0 if the transfer isn't a streaming download (MegaApi::startStreaming)
        Returns starting position of the transfer for streaming downloads, otherwise 0 
        '''
    def getEndPos(self):   
        '''Returns the end position of the transfer for streaming downloads.
        The return value of this fuction will be 0 if the transfer isn't a streaming download (MegaApi::startStreaming)
        Returns end position of the transfer for streaming downloads, otherwise 0 
        ''' 
    def getFileName(self): 
        '''Returns the name of the file that is being transferred.
        It's possible to upload a file with a different name (MegaApi::startUpload). In that case, this function returns the destination name.
        Returns name of the file that is being transferred 
        '''
    def getNumRetry(self): 
        '''Return the number of times that a transfer has temporarily failed.
        Returns number of times that a transfer has temporarily failed 
        '''
    def getMaxRetries(self): 
        '''Returns the maximum number of times that the transfer will be retried.
        Returns maximum number of times that the transfer will be retried 
        '''
    def getTag(self):
        '''Returns an integer that identifies this transfer.
        Returns integer that identifies this transfer 
        ''' 
    def getSpeed(self): 
        '''Returns the average speed of this transfer.
        Returns average speed of this transfer 
        '''
    def getDeltaSize(self): 
        '''Returns the number of bytes transferred since the previous callback.
        Returns number of bytes transferred since the previous callback 
        '''
    def getUpdateTime(self): 
        '''Returns the timestamp when the last data was received (in deciseconds)
        This timestamp doesn't have a defined starting point. Use the difference between the return value of this function and MegaTransfer::getStartTime to know how much time the transfer has been running.
        Returns timestamp when the last data was received (in deciseconds) 
        '''
    def getPublicMegaNode(self): 
        '''Returns a public node related to the transfer.
        The return value is only valid for downloads of public nodes You take the ownership of the returned value.
        Returns public node related to the transfer 
        '''
    def isSyncTransfer(self):     
        '''Returns true if this transfer belongs to the synchronization engine.
        A single transfer can upload/download several files with exactly the same contents. If some of these files are being transferred by the synchonization engine, but there is at least one file started by the application, this function returns false.
        This data is important to know if the transfer is cancellable. Regular transfers are cancellable but synchronization transfers aren't.
        Returns True if this transfer belongs to the synchronization engine, otherwise False 
        '''
    def isStreamingTransfer(self): 
        '''Returns true is this is a streaming transfer.
        Returns True if this is a streaming transfer, False otherwise 
        '''
    def getLastBytes(self):
        '''Returns the received bytes since the last callback.
        The returned value is only valid for streaming transfers (MegaApi::startStreaming).        
        Returns received bytes since the last callback 
        '''
class MegaRequest():
    '''Provides information about an asynchronous request.
    Most functions in this API are asynchonous, except the ones that never require to contact MEGA servers. Developers can use listeners (MegaListener, MegaRequestListener) to track the progress of each request. MegaRequest objects are provided in callbacks sent to these listeners and allow developers to know the state of the request, their parameters and their results.
    Objects of this class aren't live, they are snapshots of the state of the request when the object is created, they are immutable.
    These objects have a high number of 'getters', but only some of them return valid values for each type of request. Documentation of each request specify which fields are valid.     
    '''
    def copy(self): 
        '''Creates a copy of this MegaRequest object.
        The resulting object is fully independent of the source MegaRequest, it contains a copy of all internal attributes, so it will be valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the MegaRequest object 
        '''
    def getType(self):
        '''Returns the type of request associated with the object.
        Returns type of request associated with the object 
        ''' 
    def getRequestString(self): 
        '''Returns a readable string that shows the type of request.
        This function returns a pointer to a statically allocated buffer. You don't have to free the returned pointer
        Returns readable string showing the type of request 
        '''
    def __str__(self): 
        '''Returns a readable string that shows the type of request.
        Returns readable string showing the type of request 
        '''

    # Need clarification def __toString(self): 
    
    def getNodeHandle(self): 
        '''Returns the handle of a node related to the request.
        This value is valid for these requests:
            MegaApi::moveNode - Returns the handle of the node to move
            MegaApi::copyNode - Returns the handle of the node to copy
            MegaApi::renameNode - Returns the handle of the node to rename
            MegaApi::remove - Returns the handle of the node to remove
            MegaApi::sendFileToUser - Returns the handle of the node to send
            MegaApi::share - Returns the handle of the folder to share
            MegaApi::getThumbnail - Returns the handle of the node to get the thumbnail
            MegaApi::getPreview - Return the handle of the node to get the preview
            MegaApi::cancelGetThumbnail - Return the handle of the node
            MegaApi::cancelGetPreview - Returns the handle of the node
            MegaApi::setThumbnail - Returns the handle of the node
            MegaApi::setPreview - Returns the handle of the node
            MegaApi::exportNode - Returns the handle of the node
            MegaApi::disableExport - Returns the handle of the node
            MegaApi::getPaymentUrl - Returns the handle of the product
            MegaApi::syncFolder - Returns the handle of the folder in MEGA
            MegaApi::resumeSync - Returns the handle of the folder in MEGA
            MegaApi::removeSync - Returns the handle of the folder in MEGA
        This value is valid for these requests in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::createFolder - Returns the handle of the new folder
            MegaApi::copyNode - Returns the handle of the new node
            MegaApi::importFileLink - Returns the handle of the new node
        Returns handle of a node related to the request 
        '''
    def getLink(self): 
        '''Returns a link related to the request.
        This value is valid for these requests:
            MegaApi::querySignupLink - Returns the confirmation link
            MegaApi::confirmAccount - Returns the confirmation link
            MegaApi::fastConfirmAccount - Returns the confirmation link
            MegaApi::loginToFolder - Returns the link to the folder
            MegaApi::importFileLink - Returns the link to the file to import
            MegaApi::getPublicNode - Returns the link to the file
        This value is valid for these requests in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::exportNode - Returns the public link
            MegaApi::getPaymentUrl - Returns the payment link
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.
        Returns link related to the request 
        '''
    def getParentHandle(self): 
        '''Returns the handle of a parent node related to the request.
        This value is valid for these requests:
            MegaApi::createFolder - Returns the handle of the parent folder
            MegaApi::moveNode - Returns the handle of the new parent for the node
            MegaApi::copyNode - Returns the handle of the parent for the new node
            MegaApi::importFileLink - Returns the handle of the node that receives the imported file
        This value is valid for these requests in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::syncFolder - Returns a fingerprint of the local folder, to resume the sync with (MegaApi::resumeSync)
        Returns handle of a parent node related to the request 
        '''
    def getSessionKey(self): 
        '''Returns a session key related to the request.
        This value is valid for these requests:
            MegaApi::fastLogin - Returns session key used to access the account
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.
        Returns  Session key related to the request 
        '''
    def getName(self): 
        '''Returns a name related to the request.
        This value is valid for these requests:
            MegaApi::createAccount - Returns the name of the user
            MegaApi::fastCreateAccount - Returns the name of the user
            MegaApi::createFolder - Returns the name of the new folder
            MegaApi::renameNode - Returns the new name for the node
        This value is valid for these request in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::querySignupLink - Returns the name of the user
            MegaApi::confirmAccount - Returns the name of the user
            MegaApi::fastConfirmAccount - Returns the name of the user
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.

        Returns name related to the request 
        '''
    def getEmail(self): 
        '''Returns an email related to the request.
        This value is valid for these requests:
            MegaApi::login - Returns the email of the account
            MegaApi::fastLogin - Returns the email of the account
            MegaApi::loginToFolder - Returns the string "FOLDER"
            MegaApi::createAccount - Returns the email for the account
            MegaApi::fastCreateAccount - Returns the email for the account
            MegaApi::sendFileToUser - Returns the email of the user that receives the node
            MegaApi::share - Returns the email that receives the shared folder
            MegaApi::getUserAvatar - Returns the email of the user to get the avatar
            MegaApi::addContact - Returns the email of the contact
            MegaApi::removeContact - Returns the email of the contact
        This value is valid for these request in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::querySignupLink - Returns the email of the account
            MegaApi::confirmAccount - Returns the email of the account
            MegaApi::fastConfirmAccount - Returns the email of the account
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.
        Returns email related to the request 
        '''
    def getPassword(self):
        '''Returns a password related to the request.
        This value is valid for these requests:
            MegaApi::login - Returns the password of the account
            MegaApi::fastLogin - Returns the hash of the email
            MegaApi::createAccount - Returns the password for the account
            MegaApi::confirmAccount - Returns the password for the account
            MegaApi::changePassword - Returns the old password of the account (first parameter)
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.
        Returns password related to the request 
        '''
    def getNewPassword(self): 
        '''Returns a new password related to the request.
        This value is valid for these requests:
            MegaApi::changePassword - Returns the new password for the account
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.
        Returns new password related to the request 
        '''
    def getPrivateKey(self): 
        '''Returns a private key related to the request.
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.
        This value is valid for these requests:
            MegaApi::fastLogin - Returns the base64pwKey parameter
            MegaApi::fastCreateAccount - Returns the base64pwKey parameter
            MegaApi::fastConfirmAccount - Returns the base64pwKey parameter
        Returns private key related to the request 
        '''
    def getAccess(self): 
        '''Returns an access level related to the request.
        This value is valid for these requests:
            MegaApi::share - Returns the access level for the shared folder
            MegaApi::exportNode - Returns true
            MegaApi::disableExport - Returns false
        Returns access level related to the request 
        '''
    def getFile(self): 
        '''Returns the path of a file related to the request.
        The SDK retains the ownership of the returned value. It will be valid until the MegaRequest object is deleted.
        This value is valid for these requests:
            MegaApi::getThumbnail - Returns the destination path for the thumbnail
            MegaApi::getPreview - Returns the destination path for the preview
            MegaApi::getUserAvatar - Returns the destination path for the avatar
            MegaApi::setThumbnail - Returns the source path for the thumbnail
            MegaApi::setPreview - Returns the source path for the preview
            MegaApi::setAvatar - Returns the source path for the avatar
            MegaApi::syncFolder - Returns the path of the local folder
            MegaApi::resumeSync - Returns the path of the local folder
        Returns path of a file related to the request 
        '''
    def getNumRetry(self): 
        '''Return the number of times that a request has temporarily failed.
        Returns number of times that a request has temporarily failed 
        '''
    def getPublicMegaNode(self): 
        '''Returns a public node related to the request.
        You take the ownership of the returned value.
        This value is valid for these requests:
            MegaApi::copyNode - Returns the node to copy (if it is a public node)
        This value is valid for these request in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::getPublicNode - Returns the public node
        Returns public node related to the request 
        '''
    def getParamType(self): 
        '''Returns the type of parameter related to the request.
        This value is valid for these requests:
            MegaApi::getThumbnail - Returns MegaApi::ATTR_TYPE_THUMBNAIL
            MegaApi::getPreview - Returns MegaApi::ATTR_TYPE_PREVIEW
            MegaApi::cancelGetThumbnail - Returns MegaApi::ATTR_TYPE_THUMBNAIL
            MegaApi::cancelGetPreview - Returns MegaApi::ATTR_TYPE_PREVIEW
            MegaApi::setThumbnail - Returns MegaApi::ATTR_TYPE_THUMBNAIL
            MegaApi::setPreview - Returns MegaApi::ATTR_TYPE_PREVIEW
            MegaApi::submitFeedback - Returns MegaApi::EVENT_FEEDBACK
            MegaApi::reportDebugEvent - Returns MegaApi::EVENT_DEBUG
            MegaApi::cancelTransfers - Returns MegaTransfer::TYPE_DOWNLOAD if downloads are cancelled or MegaTransfer::TYPE_UPLOAD if uploads are cancelled
            Returns type of parameter related to the request 
        '''
    def getText(self):
        '''Returns a text relative to this request.
        This value is valid for these requests:
            MegaApi::submitFeedback - Returns the comment about the app
            MegaApi::reportDebugEvent - Returns the debug message
        Returns text relative to this request 
        ''' 
    def getNumber(self): 
        '''Returns a number related to this request.
        This value is valid for these requests:
            MegaApi::retryPendingConnections - Returns if transfers are retried
            MegaApi::submitFeedback - Returns the rating for the app
        This value is valid for these request in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::resumeSync - Returns the fingerprint of the local file
        Returns number related to this request 
        '''
    def getFlag(self): 
        '''Returns a flag related to the request.
        This value is valid for these requests:
            MegaApi::retryPendingConnections - Returns if request are disconnected
            MegaApi::pauseTransfers - Returns true if transfers were paused, false if they were resumed
        Returns flag related to the request 
        '''
    def getTransferredBytes(self): 
        '''Returns the number of transferred bytes during the request.
        Returns number of transferred bytes during the request 
        '''
    def getTotalBytes(self): 
        '''Returns the number of bytes that the SDK will have to transfer to finish the request.
        Returns number of bytes that the SDK will have to transfer to finish the request 
        '''
    def getMegaAccountDetails(self): 
        '''Returns details related to the MEGA account.
        This value is valid for these request in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::getAccountDetails - Details of the MEGA account
        Returns details related to the MEGA account 
        '''
    def getPricing(self): 
        '''Returns available pricing plans to upgrade a MEGA account.
        This value is valid for these request in onRequestFinish when the error code is MegaError::API_OK:
            MegaApi::getPricing - Returns the available pricing plans
        Returns available pricing plans to upgrade a MEGA account 
        '''

    def getTransferTag(self): 
        '''Returns the tag of a transfer related to the request.
        This value is valid for these requests:
            MegaApi::cancelTransfer - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
        Returns tag of a transfer related to the request 
        '''
    def getNumDetails(self): 
        '''Returns the number of details related to this request.
        Returns number of details related to this request 
        '''
class MegaNode():
    '''Represents a node (file/folder) in the MEGA account.
    It allows to get all data related to a file/folder in MEGA. It can be also used to start SDK requests (MegaApi::renameNode, MegaApi::moveNode, etc.)
    Objects of this class aren't live, they are snapshots of the state of a node in MEGA when the object is created, they are immutable.
    Do not inherit from this class. You can inspect the MEGA filesystem and get these objects using MegaApi::getChildren, MegaApi::getChildNode and other MegaApi functions.
    '''
    def copy(self):
        '''Creates a copy of this MegaNode object.
        The resulting object is fully independent of the source MegaNode, it contains a copy of all internal attributes, so it will be valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the MegaNode object 
        ''' 
    def getType(self): 
        '''Returns the type of the node.
        Valid values are:
            TYPE_UNKNOWN = -1, Unknown node type
            TYPE_FILE = 0, The MegaNode object represents a file in MEGA
            TYPE_FOLDER = 1 The MegaNode object represents a folder in MEGA
            TYPE_ROOT = 2 The MegaNode object represents root of the MEGA Cloud Drive
            TYPE_INCOMING = 3 The MegaNode object represents root of the MEGA Inbox
            TYPE_RUBBISH = 4 The MegaNode object represents root of the MEGA Rubbish Bin
        Returns type of the node 
        '''
    def getName(self): 
        '''Returns the name of the node.
        The name is only valid for nodes of type TYPE_FILE or TYPE_FOLDER. For other MegaNode types, the name is undefined.
        The MegaNode object retains the ownership of the returned string. It will be valid until the MegaNode object is deleted.
        Returns name of the node 
        '''
    def getBase64Handle(self): 
        '''Returns the handle of this MegaNode in a Base64-encoded string.
        You take the ownership of the returned string.
        Returns Base64-encoded handle of the node 
        '''
    def getSize(self): 
        '''Returns the size of the node.
        The returned value is only valid for nodes of type TYPE_FILE.
        Returns size of the node 
        '''
    def getCreationTime(self):
        '''Returns the creation time of the node in MEGA (in seconds since the epoch)
        The returned value is only valid for nodes of type TYPE_FILE or TYPE_FOLDER.
        Returns creation time of the node (in seconds since the epoch) 
        '''
    def getModificationTime(self): 
        '''Returns the modification time of the file that was uploaded to MEGA (in seconds since the epoch)
        The returned value is only valid for nodes of type TYPE_FILE.
        Returns modification time of the file that was uploaded to MEGA (in seconds since the epoch) 
        '''
    def getHandle(self): 
        '''Returns a handle to identify this MegaNode.
        You can use MegaApi::getNodeByHandle to recover the node later.
        Returns handle that identifies this MegaNode 
        '''
    def getBase64Key(self): 
        '''Returns the key of the node in a Base64-encoded string.
        The return value is only valid for nodes of type TYPE_FILE
        You take the ownership of the returned string.
        Returns the key of the node. 
        '''
    def getTag(self):     
        '''Returns the tag of the operation that created/modified this node in MEGA.
        Every request and every transfer has a tag that identifies it. When a request creates or modifies a node, the tag is associated with the node at runtime, this association is lost after a reload of the filesystem or when the SDK is closed.
        This tag is specially useful to know if a node reported in MegaListener::onNodesUpdate or MegaGlobalListener::onNodesUpdate was modified by a local operation (tag != 0) or by an external operation, made by another MEGA client (tag == 0).
    If the node hasn't been created/modified during the current execution, this function returns 0
    Returns the tag associated with the node. 
        '''
    def isFile(self): 
        '''Returns True if this node represents a file (type == TYPE_FILE)
        Returns True if this node represents a file, otherwise False 
        '''
    def isFolder(self): 
        '''Returns True this node represents a folder or a root node.
        Returns True this node represents a folder or a root node 
        '''
    def isRemoved(self): 
        '''Returns True if this node has been removed from the MEGA account.
        This value is only useful for nodes notified by MegaListener::onNodesUpdate or MegaGlobalListener::onNodesUpdate that can notify about deleted nodes.
        In other cases, the return value of this function will be always false.
        Returns True if this node has been removed from the MEGA account 
        '''
    # Need clarifications def hasChanged(self, *args): 
    # Need clarifications def getChanges(self): 
    def hasThumbnail(self): 
        '''Returns True if the node has an associated thumbnail.
        Returns True if the node has an associated thumbnail 
        '''
    def hasPreview(self): 
        '''Returns True if the node has an associated preview.
        Returns True if the node has an associated preview 
        '''
    def isPublic(self): 
        '''Returns True if this is a public node.
        Only MegaNode objects generated with MegaApi::getPublicMegaNode will return true.
        Returns True if this is a public node 
        '''
class MegaUser():
    '''Represents an user in MEGA.
    It allows to get all data related to an user in MEGA. It can be also used to start SDK requests (MegaApi::share MegaApi::removeContact, etc.)
    Objects of this class aren't live, they are snapshots of the state of an user in MEGA when the object is created, they are immutable.
    Do not inherit from this class. You can get the contacts of an account using MegaApi::getContacts and MegaApi::getContact.     
    '''
    def copy(self):
        '''Creates a copy of this MegaUser object.
        The resulting object is fully independent of the source MegaUser, it contains a copy of all internal attributes, so it will be valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the MegaUser object 
        '''
    def getEmail(self): 
        '''Returns the email associated with the contact.
        The email can be used to recover the MegaUser object later using MegaApi::getContact
        The MegaUser object retains the ownership of the returned string, it will be valid until the MegaUser object is deleted.
        Returns the email associated with the contact. 
        '''
    def getVisibility(self): 
        '''Get the current visibility of the contact.
        The returned value will be one of these:
            VISIBILITY_UNKNOWN = -1 The visibility of the contact isn't know
            VISIBILITY_HIDDEN = 0 The contact is currently hidden
            VISIBILITY_VISIBLE = 1 The contact is currently visible
            VISIBILITY_ME = 2 The contact is the owner of the account being used by the SDK
        Returns current visibility of the contact 
        '''
    def getTimestamp(self): 
        '''Returns the timestamp when the contact was added to the contact list (in seconds since the epoch)
        Returns timestamp when the contact was added to the contact list (in seconds since the epoch) 
        '''
class MegaShare():
    '''Represents the outbound sharing of a folder with an user in MEGA.
    It allows to get all data related to the sharing. You can start sharing a folder with a contact or cancel an existing sharing using MegaApi::share. A public link of a folder is also considered a sharing and can be cancelled.
    Objects of this class aren't live, they are snapshots of the state of the sharing in MEGA when the object is created, they are immutable.
    Do not inherit from this class. You can get current active sharings using MegaApi::getOutShares     
    ''' 
    def copy(self): 
        '''Creates a copy of this MegaShare object.
        The resulting object is fully independent of the source MegaShare, it contains a copy of all internal attributes, so it will be valid after the original object is deleted.
        You are the owner of the returned object
        Returns copy of the MegaShare object 
        '''
    def getUser(self): 
        '''Returns the email of the user with whom we are sharing the folder.
        For public shared folders, this function return None
        Returns the email of the user with whom we share the folder, or None if it's a public folder 
        '''
    def getNodeHandle(self): 
        '''Returns the handle of the folder that is being shared.
        Returns the handle of the folder that is being shared 
        '''
    def getAccess(self): 
        '''Returns the access level of the sharing.
        Possible return values are:
            ACCESS_UNKNOWN = -1 It means that the access level is unknown
            ACCESS_READ = 0 The user can read the folder only
            ACCESS_READWRITE = 1 The user can read and write the folder
            ACCESS_FULL = 2 The user has full permissions over the folder
            ACCESS_OWNER = 3 The user is the owner of the folder
        Returns the access level of the sharing 
        '''
    def getTimestamp(self):
        '''Returns the timestamp when the sharing was created (in seconds since the epoch)
        Returns the timestamp when the sharing was created (in seconds since the epoch) 
        '''
class MegaUserList():
    '''List of MegaUser objects.
    A MegaUserList has the ownership of the MegaUser objects that it contains, so they will be only valid until the MegaUserList is deleted. If you want to retain a MegaUser returned by a MegaUserList, use MegaUser::copy.
    Objects of this class are immutable.    
    ''' 
    def copy(self):
        '''Supposed to create a copy of the MegaUserList object. Need clarification on this.
        ''' 
    def get(self, i):
        '''Returns the MegaUser at the position i in the MegaUserList.
        The MegaUserList retains the ownership of the returned MegaUser. It will be only valid until the MegaUserList is deleted.
        If the index is >= the size of the list, this function returns NULL.
        i position of the MegaUser that we want to get for the list
        Returns MegaUser at the position i in the list 
        '''
    def size(self): 
        '''Returns the number of MegaUser objects in the list.
        Returns number of MegaUser objects in the list 
        '''
class MegaShareList():
    '''List of MegaShare objects.
    A MegaShareList has the ownership of the MegaShare objects that it contains, so they will be only valid until the MegaShareList is deleted. If you want to retain a MegaShare returned by a MegaShareList, use MegaShare::copy.
    Objects of this class are immutable.    
    '''
    def get(self, i):
        '''Returns the MegaShare at the position i in the MegaShareList.
        The MegaShareList retains the ownership of the returned MegaShare. It will be only valid until the MegaShareList is deleted.
        If the index is >= the size of the list, this function returns None.
        i - position of the MegaShare that we want to get for the list
        Returns MegaShare at the position i in the list         
        ''' 
    def size(self): 
        '''Returns the number of MegaShare objects in the list.
        Returns number of MegaShare objects in the list         
        '''
class MegaTransferList():
    '''List of MegaTransfer objects.
    A MegaTransferList has the ownership of the MegaTransfer objects that it contains, so they will be only valid until the MegaTransferList is deleted. If you want to retain a MegaTransfer returned by a MegaTransferList, use MegaTransfer::copy.
    Objects of this class are immutable.    
    '''
    def get(self, i): 
        '''Returns the MegaTransfer at the position i in the MegaTransferList.
        The MegaShareList retains the ownership of the returned MegaTransfer. It will be only valid until the MegaTransferList is deleted.
        If the index is >= the size of the list, this function returns None.
        i - position of the MegaTransfer that we want to get for the list
        Returns MegaTransfer at the position i in the list         
        ''' 
    def size(self): 
        '''Returns the number of MegaTransfer objects in the list.
        Returns number of MegaTransfer objects in the list         
        '''
# Need clarification class MegaAccountBalance():
    # Need clarification def getAmount(self): 
    # Need clarification def getCurrency(self):

# Need clarificationclass MegaAccountSession():
    # Need clarification def getCreationTimestamp(self): 
    # Need clarification def getMostRecentUsage(self): 
    # Need clarification def getUserAgent(self): 
    # Need clarification def getIP(self): 
    # Need clarification def getCountry(self): 
    # Need clarification def isCurrent(self): 
    # Need clarification def isAlive(self): 
    # Need clarification def getHandle(self): 
    
# Need clarificationclass MegaAccountPurchase():
    # Need clarification def getTimestamp(self): 
    # Need clarification def getHandle(self): 
    # Need clarification def getCurrency(self): 
    # Need clarification def getAmount(self): 
    # Need clarification def getMethod(self):
     
# Need clarificationclass MegaAccountTransaction():
    # Need clarification def getTimestamp(self):
    # Need clarification def getHandle(self): 
    # Need clarification def getCurrency(self): 
    # Need clarification def getAmount(self):
    
           
