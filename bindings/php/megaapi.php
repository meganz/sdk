<?php

set_include_path(get_include_path() . PATH_SEPARATOR . ".libs");
dl('libmegaphp.so');

include 'mega.php';

interface MegaRequestListenerInterface
{
    public function onRequestStart($api, $request);
    public function onRequestFinish($api, $request, $error);
    public function onRequestTemporaryError($api, $request, $error);
}
	
interface MegaTransferListenerInterface
{
    public function onTransferStart($api, $transfer);
    public function onTransferFinish($api, $transfer, $error);
    public function onTransferUpdate($api, $transfer);
    public function onTransferTemporaryError($api, $request, $error);
}

interface MegaGlobalListenerInterface
{
    public function onUsersUpdate($api, $users);
    public function onNodesUpdate($api, $nodes);
    public function onReloadNeeded($api);
}

interface MegaListenerInterface extends MegaRequestListenerInterface, 
MegaTransferListenerInterface, MegaGlobalListenerInterface { }

class MegaListenerPHP extends MegaListener
{
    var $megaApi;
    var $listener;
	
    public function __construct($megaApi, $listener)
    {
        parent::__construct();
		
        $this->megaApi = $megaApi;
        $this->listener = $listener;
    }
	
    public function getUserListener()
    {
        return $this->listener;
    }

    public function onRequestStart($api, $request)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $megaRequest = $request->copy();
            $this->listener->onRequestStart($megaApi, $megaRequest);
        }
    }

    public function onRequestFinish($api, $request, $error)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $megaRequest = $request->copy();
            $megaError = $error->copy();

            $this->listener->onRequestFinish($megaApi, $megaRequest, $megaError);
        }
    }

    public function onRequestTemporaryError($api, $request, $error)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $megaRequest = $request->copy();
            $megaError = $error->copy();

            $this->listener->onRequestTemporaryError($megaApi, $megaRequest, $megaError);
        }
    }

    public function onTransferStart($api, $transfer)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $megaTransfer = $transfer->copy();

            $this->listener->onTransferStart($megaApi, $megaTransfer);
        }
    }

    public function onTransferFinish($api, $transfer, $error)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $megaTransfer = $transfer->copy();
            $megaError = $error->copy();

            $this->listener->onTransferFinish($megaApi, $megaTransfer, $megaError);
        }
    }

    public function onTransferUpdate($api, $transfer)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $megaTransfer = $transfer->copy();

            $this->listener->onTransferUpdate($megaApi, $megaTransfer);
        }
    }

    public function onTransferTemporaryError($api, $request, $error)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $megaTransfer = $transfer->copy();
            $megaError = $error->copy();

            $this->listener->onTransferTemporaryError($megaApi, $megaTransfer, $megaError);
        }
    }

    public function onUsersUpdate($api, $users)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            if ($users != null)
            {
                $megaUsers = $users->copy();
            }
            else
            {
                $megaUsers = null;
            }

            $this->listener->onUsersUpdate($megaApi, $megaUsers);
        }
    }

    public function onNodesUpdate($api, $nodes)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            if ($nodes != null)
            {
                $megaNodes = $nodes->copy();
            }
            else
            {
                $megaNodes = null;
            }

            $this->listener->onNodesUpdate($megaApi, $megaNodes);
        }
    }

    public function onReloadNeeded($api)
    {
        if ($this->listener != null)
        {
            $megaApi = $this->megaApi;
            $this->listener->onReloadNeeded ($megaApi);
        }
    }
}

class MegaRequestListenerPHP extends MegaRequestListener
{
	var $megaApi;
	var $listener;
	var $singleListener;

	public function __construct($megaApi, $listener, $single)
	{
            parent::__construct();

            $this->megaApi = $megaApi;
            $this->listener = $listener;
            $this->singleListener = $single;
	}
	
	public function getUserListener()
	{
            return $this->listener;
	}
	
	public function onRequestStart($api, $request)
	{
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $megaRequest = $request->copy();

                $this->listener->onRequestStart($megaApi, $megaRequest);
            }
	}
		
	public function onRequestFinish($api, $request, $error)
	{
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $megaRequest = $request->copy();
                $megaError = $error->copy();

                $this->listener->onRequestFinish($megaApi, $megaRequest, $megaError);

                if($this->singleListener == 1)
                {
                        $megaApi->privateFreeRequestListener($this);
                }
            }
	}

	public function onRequestTemporaryError($api, $request, $error)
	{	
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $megaRequest = $request->copy();
                $megaError = $error->copy();

                $this->listener->onRequestTemporaryError($megaApi, $megaRequest, $megaError);
            }
	}
}

class MegaTransferListenerPHP extends MegaTransferListener
{
	var $megaApi;
	var $listener;
	var $singleListener;

	public function __construct($megaApi, $listener, $single)
	{
            parent::__construct();

            $this->megaApi = $megaApi;
            $this->listener = $listener;
            $this->singleListener = $single;
	}
	
	public function getUserListener()
	{
            return $this->listener;
	}
	
	public function onTransferStart($api, $transfer)
	{
            if($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $megaTransfer = $transfer->copy();

                $this->listener->onTransferStart($megaApi, $megaTransfer);
            }
	}
	
	public function onTransferFinish($api, $transfer, $error)
	{		
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $megaTransfer = $transfer->copy();
                $megaError = $error->copy();

                $this->listener->onTransferFinish($megaApi, $megaTransfer, $megaError);

                if ($this->singleListener == 1)
                {
                    $megaApi->privateFreeTransferListener($this);
                }
            }
	}
	
	public function onTransferUpdate($api, $transfer)
	{
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $megaTransfer = $transfer->copy();

                $this->listener->onTransferUpdate($megaApi, $megaTransfer);
            }
	}
			
	public function onTransferTemporaryError($api, $request, $error)
	{		
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $megaTransfer = $transfer->copy();
                $megaError = $error->copy();

                $this->listener->onTransferTemporaryError($megaApi, $megaTransfer, $megaError);
            }
	}
}

class MegaGlobalListenerPHP extends MegaGlobalListener
{
	var $megaApi;
	var $listener;

	public function __construct($megaApi, $listener)
	{
            parent::__construct();

            $this->megaApi = $megaApi;
            $this->listener = $listener;
	}
	
	public function getUserListener()
	{
            return $this->listener;
	}
	
	public function onUsersUpdate($api, $users)
	{
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                if($users != null)
                {
                        $megaUsers = $users->copy();
                }
                else
                {
                        $megaUsers = null;
                }

                $this->listener->onUsersUpdate($megaApi, $megaUsers);
            }
	}

	public function onNodesUpdate($api, $nodes)
	{
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                if ($nodes != null)
                {
                        $megaNodes = $nodes->copy();
                }
                else
                {
                        $megaNodes = null;
                }

                $this->listener->onNodesUpdate ($megaApi, $megaNodes);
            }
	}
	
	public function onReloadNeeded($api)
	{
            if ($this->listener != null)
            {
                $megaApi = $this->megaApi;
                $this->listener->onReloadNeeded ($megaApi);
            }
	}
}

class MegaApiPHP
{	
    private $megaApi;
    private $activeMegaListeners = array();
    private $activeMegaRequestListeners = array();
    private $activeMegaTransferListeners = array();
    private $activeMegaGlobalListeners = array();
    private $semaphore;

    public function __construct($appKey, $userAgent, $basePath)
    {
        $this->megaApi = new MegaApi($appKey, $basePath, $userAgent);
        $this->semaphore = sem_get("32462", 1, 0666, 1);
    }

    private function createDelegateMegaListener($listener)
    {
        if ($listener == null)
        {
            return null;
        }

        $delegateListener = new MegaListenerPHP($this, $listener);

        sem_acquire($this->semaphore);
        array_push($this->activeMegaListeners, $delegateListener);
        sem_release($this->semaphore);

        return $delegateListener;
    }

    private function createDelegateRequestListener($listener, $singleListener = 1)
    {
        if ($listener == null)
        {
            return null;
        }

        $delegateListener = new MegaRequestListenerPHP($this, $listener, $singleListener);

        sem_acquire($this->semaphore);
        array_push($this->activeMegaRequestListeners, $delegateListener);
        sem_release($this->semaphore);

        return $delegateListener;
    }

    private function createDelegateTransferListener($listener, $singleListener = 1)
    {
        if ($listener == null)
        {
            return null;
        }

        $delegateListener = new MegaTransferListenerPHP($this, $listener, $singleListener);

        sem_acquire($this->semaphore);
        array_push($this->activeMegaTransferListeners, $delegateListener);
        sem_release($this->semaphore);

        return $delegateListener;
    }

    private function createDelegateGlobalListener($listener)
    {
        if ($listener == null)
        {
            return null;
        }

        $delegateListener = new MegaGlobalListenerPHP($this, $listener);

        sem_acquire($this->semaphore);
        array_push($this->activeMegaGlobalListeners, $delegateListener);
        sem_release($this->semaphore);

        return $delegateListener;
    }

    function addListener($listener)
    {
        $this->megaApi->addListener($this->createDelegateMegaListener($listener));
    }

    function addRequestListener($listener)
    {
        $this->megaApi->addRequestListener($this->createDelegateRequestListener($listener, 0));
    }

    function addTransferListener($listener)
    {
        $this->megaApi->addTransferListener($this->createDelegateTransferListener($listener, 0));
    }

    function addGlobalListener($listener)
    {
        $this->megaApi->addGlobalListener($this->createDelegateGlobalListener($listener));
    }


    function removeListener($listener)
    {
        sem_acquire($this->semaphore);
        foreach ($this->activeMegaListeners as $delegate)
        {
            if($delegate->getUserListener() == $listener)
            {
                $this->megaApi->removeListener($delegate);
                unset($this->activeMegaListeners[$delegate]);
                break;
            }
        }
        sem_release($this->semaphore);
    }

    function removeRequestListener($listener)
    {
        sem_acquire($this->semaphore);
        foreach ($this->activeMegaRequestListeners as $delegate)
        {
            if ($delegate->getUserListener() == $listener)
            {
                $this->megaApi->removeRequestListener($delegate);
                unset($this->activeMegaRequestListeners[$delegate]);
                break;
            }
        }
        sem_release($this->semaphore);
    }

    function removeTransferListener($listener)
    {
        sem_acquire($this->semaphore);
        foreach ($this->activeMegaTransferListeners as $delegate)
        {
            if ($delegate->getUserListener() == $listener)
            {
                $this->megaApi->removeTransferListener($delegate);
                unset($this->activeMegaTransferListeners[$delegate]);
                break;
            }
        }
        sem_release($this->semaphore);
    }

    function removeGlobalListener($listener)
    {
        sem_acquire($this->semaphore);
        foreach ($this->activeMegaGlobalListeners as $delegate)
        {
            if ($delegate->getUserListener() == $listener)
            {
                    $this->megaApi->removeGlobalListener($delegate);
                    unset($this->activeMegaGlobalListeners[$delegate]);
                    break;
            }
        }
        sem_release($this->semaphore);
    }

    function getBase64PwKey($password)
    {
        return $this->megaApi->getBase64PwKey($password);
    }

    function getStringHash($base64pwkey, $email)
    {
        return $this->megaApi->getStringHash($base64pwkey, $email);
    }
    
    function addEntropy($data, $size)
    {
        MegaApi::addEntropy($data, $size);
    }

    static function base64ToHandle($base64Handle)
    {
        return MegaApi::base64ToHandle($handle);
    }

    static function handleToBase64($handle)
    {
        return MegaApi::handleToBase64($handle);
    }

    static function userHandleToBase64($handle)
    {
        return MegaApi::userHandleToBase64($handle);
    }

    function retryPendingConnections($disconnect = false, $includexfers = false, $listener = null)
    {
        $this->megaApi->retryPendingConnections($disconnect, $includexfers, $this->createDelegateRequestListener($listener));
    }

    function login($email, $password, $listener = null)
    {
        $this->megaApi->login($email, $password, $this->createDelegateRequestListener($listener));
    }

    function loginToFolder($megaFolderLink, $listener = null)
    {
        $this->megaApi->loginToFolder($megaFolderLink, $this->createDelegateRequestListener($listener));
    }

    function dumpSession()
    {
        return $this->megaApi->dumpSession();
    }

    function dumpXMPPSession()
    {
        return $this->megaApi->dumpXMPPSession();
    }

    function createAccount($email, $password, $firstname, $lastname, $listener = null)
    {
        $this->megaApi->createAccount($email, $password, $firstname, $lastname, $this->createDelegateRequestListener($listener));
    }

    function fastCreateAccount($email, $base64pwkey, $name, $listener = null)
    {
        $this->megaApi->fastCreateAccount($email, $base64pwkey, $name, $this->createDelegateRequestListener($listener));
    }

    function querySignupLink($link, $listener = null)
    {
        $this->megaApi->querySignupLink($link, $this->createDelegateRequestListener($listener));
    }

    function confirmAccount($link, $password, $listener = null)
    {
        $this->megaApi->confirmAccount($link, $password, $this->createDelegateRequestListener($listener));
    }

    function fastConfirmAccount($link, $base64pwkey, $listener = null)
    {
        $this->megaApi->fastConfirmAccount($link, $base64pwkey, $this->createDelegateRequestListener($listener));
    }

    function setProxySettings($proxySettings)
    {
        $this->megaApi->setProxySettings($proxySettings);
    }

    function getAutoProxySettings()
    {
        return $this->megaApi->getAutoProxySettings();
    }

    function isLoggedIn()
    {
        return $this->megaApi->isLoggedIn();
    }

    function getMyEmail()
    {
        return $this->megaApi->getMyEmail();
    }

    static function setLogLevel($logLevel)
    {
        MegaApi::setLogLevel($logLevel);
    }

    static function setLoggerObject($megaLogger)
    {
        MegaApi::setLoggerObject($megaLogger);
    }

    static function log($logLevel, $message, $filename = "", $line = -1)
    {
        MegaApi::log($logLevel, $message, $filename, $line);
    }

    function createFolder($name, $parent, $listener = null)
    {
        $this->megaApi->createFolder($name, $parent, $this->createDelegateRequestListener($listener));
    }

    function moveNode($node, $newParent, $listener = null)
    {
        $this->megaApi->moveNode($node, $newParent, $this->createDelegateRequestListener($listener));
    }

    function copyNode($node, $newParent, $listener = null)
    {
        $this->megaApi->copyNode($node, $newParent, $this->createDelegateRequestListener($listener));
    }

    function renameNode($node, $newName, $listener = null)
    {
        $this->megaApi->renameNode($node, $newName, $this->createDelegateRequestListener($listener));
    }

    function remove($node, $listener = null)
    {
        $this->megaApi->remove($node, $this->createDelegateRequestListener($listener));
    }

    function sendFileToUser($node, $user, $listener = null)
    {
        $this->megaApi->sendFileToUser($node, $user, $this->createDelegateRequestListener($listener));
    }

    function share($node, $user_or_email, $level, $listener = null)
    {
        $this->megaApi->share($node, $user_or_email, $level, $this->createDelegateRequestListener($listener));
    }

    function importFileLink($megaFileLink, $parent, $listener = null)
    {
        $this->megaApi->importFileLink($megaFileLink, $parent, $this->createDelegateRequestListener($listener));
    }

    function getPublicNode($megaFileLink, $listener = null)
    {
        $this->megaApi->getPublicNode($megaFileLink, $this->createDelegateRequestListener($listener));
    }

    function getThumbnail($node, $dstFilePath, $listener = null)
    {
        $this->megaApi->getThumbnail($node, $dstFilePath, $this->createDelegateRequestListener($listener));
    }

    function getPreview($node, $dstFilePath, $listener = null)
    {
        $this->megaApi->getPreview($node, $dstFilePath, $this->createDelegateRequestListener($listener));
    }

    function getUserAvatar($user, $dstFilePath, $listener=null)
    {
        $this->megaApi->getUserAvatar($user, $dstFilePath, $this->createDelegateRequestListener($listener));
    }

    function cancelGetThumbnail($node, $listener = null)
    {
        $this->megaApi->cancelGetThumbnail($node, $this->createDelegateRequestListener($listener));
    }

    function cancelGetPreview($node, $listener = null)
    {
        $this->megaApi->cancelGetPreview($node, $this->createDelegateRequestListener($listener));
    }

    function setThumbnail($node, $srcFilePath, $listener = null)
    {
        $this->megaApi->setThumbnail($node, $srcFilePath, $this->createDelegateRequestListener($listener));
    }

    function setPreview($node, $srcFilePath, $listener = null)
    {
        $this->megaApi->setPreview($node, $srcFilePath, $this->createDelegateRequestListener($listener));
    }

    function setAvatar($srcFilePath, $listener = null)
    {
        $this->megaApi->setAvatar($srcFilePath, $this->createDelegateRequestListener($listener));
    }

    function exportNode($node, $listener = null)
    {
        $this->megaApi->exportNode($node, $this->createDelegateRequestListener($listener));
    }

    function disableExport($node, $listener = null)
    {
        $this->megaApi->disableExport($node, $this->createDelegateRequestListener($listener));
    }

    function fetchNodes($listener = null)
    {
        $this->megaApi->fetchNodes($this->createDelegateRequestListener($listener));
    }

    function getAccountDetails($listener = null)
    {
        $this->megaApi->getAccountDetails($this->createDelegateRequestListener($listener));
    }

    function getPricing($listener = null)
    {
        $this->megaApi->getPricing($this->createDelegateRequestListener($listener));
    }

    function getPaymentUrl($productHandle, $listener = null)
    {
        $this->megaApi->getPaymentUrl($productHandle, $this->createDelegateRequestListener($listener));
    }

    function exportMasterKey()
    {
        return $this->megaApi->exportMasterKey();
    }

    function changePassword($oldPassword, $newPassword, $listener = null)
    {
        $this->megaApi->changePassword($oldPassword, $newPassword, $this->createDelegateRequestListener($listener));
    }

    function inviteContact($user, $message, $action, $listener = null)
    {
        $this->megaApi->inviteContact($user, $message, $action, $this->createDelegateRequestListener($listener));
    }

    function removeContact($user, $listener = null)
    {
        $this->megaApi->removeContact($user, $this->createDelegateRequestListener($listener));
    }

    function logout($listener = null)
    {
        $this->megaApi->logout($this->createDelegateRequestListener($listener));
    }

    function submitFeedback($rating, $comment, $listener = null)
    {
        $this->megaApi->submitFeedback($rating, $comment, $this->createDelegateRequestListener($listener));
    }

    function reportDebugEvent($text, $listener = null)
    {
        $this->megaApi->reportDebugEvent($text, $this->createDelegateRequestListener($listener));
    }

    function startUpload($localPath, $parent, $listener = null)
    {
        $this->megaApi->startUpload($localPath, $parent, null, -1, null, false, false, null, $this->createDelegateTransferListener($listener));
    }

    function startDownload($node, $localPath, $listener = null)
    {
        $this->megaApi->startDownload($node, $localPath, null, null, false, null, 3, 2, false, $this->createDelegateTransferListener($listener));
    }

    function cancelTransfer($transfer, $listener = null)
    {
        $this->megaApi->cancelTransfer($transfer, $this->createDelegateRequestListener($listener));
    }

    function cancelTransfers($type, $listener = null)
    {
        $this->megaApi->cancelTransfers($type, $this->createDelegateRequestListener($listener));
    }

    function pauseTransfers($pause, $listener = null)
    {
        $this->megaApi->pauseTransfers($pause, $this->createDelegateRequestListener($listener));
    }

    function setUploadLimit($bpslimit)
    {
        $this->megaApi->setUploadLimit($bpslimit);
    }

    function getTransfers($type = null)
    {
        if ($type == null)
            return $this->listToArray($this->megaApi->getTransfersAll());
        else
            return $this->listToArray($this->megaApi->getTransfers($type));
    }

    function update()
    {
        $this->megaApi->update();
    }

    function isWaiting()
    {
        return $this->megaApi->isWaiting();
    }

    function getNumPendingUploads()
    {
        return $this->megaApi->getNumPendingUploads();
    }

    function getNumPendingDownloads()
    {
        return $this->megaApi->getNumPendingDownloads();
    }

    function getTotalUploads()
    {
        return $this->megaApi->getTotalUploads();
    }

    function getTotalDownloads()
    {
        return $this->megaApi->getTotalDownloads();
    }

    function resetTotalDownloads()
    {
        $this->megaApi->resetTotalDownloads();
    }

    function resetTotalUploads()
    {
        $this->megaApi->resetTotalUploads();
    }

    function getTotalDownloadedBytes()
    {
        return $this->megaApi->getTotalDownloadedBytes();
    }

    function getTotalUploadedBytes()
    {
        return $this->megaApi->getTotalUploadedBytes();
    }

    function updateStats()
    {
        $this->megaApi->updateStats();
    }

    const ORDER_NONE = 0;
    const ORDER_DEFAULT_ASC = MegaApi::ORDER_DEFAULT_ASC;
    const ORDER_DEFAULT_DESC = MegaApi::ORDER_DEFAULT_DESC;
    const ORDER_SIZE_ASC = MegaApi::ORDER_SIZE_ASC;
    const ORDER_SIZE_DESC = MegaApi::ORDER_SIZE_DESC;
    const ORDER_CREATION_ASC = MegaApi::ORDER_CREATION_ASC;
    const ORDER_CREATION_DESC = MegaApi::ORDER_CREATION_DESC;
    const ORDER_MODIFICATION_ASC = MegaApi::ORDER_MODIFICATION_ASC;
    const ORDER_MODIFICATION_DESC = MegaApi::ORDER_MODIFICATION_DESC;
    const ORDER_ALPHABETICAL_ASC = MegaApi::ORDER_ALPHABETICAL_ASC;
    const ORDER_ALPHABETICAL_DESC = MegaApi::ORDER_ALPHABETICAL_DESC;

    function getNumChildren($parent)
    {
        return $this->megaApi->getNumChildren($parent);
    }

    function getNumChildFiles($parent)
    {
        return $this->megaApi->getNumChildFiles($parent);
    }

    function getNumChildFolders($parent)
    {
        return $this->megaApi->getNumChildFolders($parent);
    }

    function getChildren($parent, $order = 1)
    {
        return $this->listToArray($this->megaApi->getChildren($parent, $order));
    }

    function getChildNode($parent, $name)
    {
        return $this->megaApi->getChildNode($parent, $name);
    }

    function getParentNode($node)
    {
        return $this->megaApi->getParentNode($node);
    }

    function getNodePath($node)
    {
        return $this->megaApi->getNodePath($node);
    }

    function getNodeByPath($path, $base = null)
    {
        return $this->megaApi->getNodeByPath($path, $base);
    }

    function getNodeByHandle($handle)
    {
        return $this->megaApi->getNodeByHandle($handle);
    }

    function getContacts()
    {
        return $this->listToArray($this->megaApi->getContacts());
    }

    function getContact($email)
    {
        return $this->megaApi->getContact($email);
    }

    function getInShares($user = null)
    {
        if ($user == null)
            return $this->listToArray($this->megaApi->getInSharesAll());
        else
            return $this->listToArray($this->megaApi->getInShares($user));
    }

    function isShared($node)
    {
        return $this->megaApi->isShared($node);
    }

    function getOutShares($node = null)
    {
        if ($node == null)
            return $this->listToArray($this->megaApi->getOutSharesAll());
        else
            return $this->listToArray($this->megaApi->getOutShares($node));
    }

    function getAccess($node)
    {
        return $this->megaApi->getAccess($node) ;
    }

    function getSize($node)
    {
        return $this->megaApi->getSize($node) ;
    }

    function getFingerprint($filePath_or_node)
    {
        return $this->megaApi->getFingerprint($filePath_or_node);
    }

    function getNodeByFingerprint($fingerprint)
    {
        return $this->megaApi->getNodeByFingerprint($fingerprint);
    }

    function hasFingerprint($fingerprint)
    {
        return $this->megaApi->hasFingerprint($fingerprint);
    }

    function checkAccess($node, $level)
    {
        return $this->megaApi->checkAccess($node, $level);
    }

    function checkMove($node, $target)
    {
        return $this->megaApi->checkMove($node, $target);
    }

    function getRootNode()
    {
        return $this->megaApi->getRootNode();
    }

    function getInboxNode()
    {
        return $this->megaApi->getInboxNode();
    }

    function getRubbishNode()
    {
        return $this->megaApi->getRubbishNode();
    }

    function search($node, $searchString, $recursive = true)
    {
        return $this->listToArray($this->megaApi->search($node, $searchString, $recursive));
    }

    private function listToArray($list)
    {
        if ($list == null)
        {
            return array();
        }

        $result = array();
        for ($i = 0; $i < $list->size(); $i++)
        {
            array_push($result, $list->get($i)->copy());
        }

        return $result;
    }

    function privateFreeRequestListener($listener)
    {
        sem_acquire($this->semaphore);
        activeRequestListeners.remove(listener);
        sem_release($this->semaphore);
    }

    function privateFreeTransferListener($listener)
    {
        sem_acquire($this->semaphore);
        activeTransferListeners.remove(listener);
        sem_release($this->semaphore);
    }

    public function __destruct()
    {
        foreach ($this->activeMegaListeners as $listener)
        {
            $this->megaApi->removeListener($listener);
        }

        foreach ($this->activeMegaRequestListeners as $listener)
        {
            $this->megaApi->removeRequestListener($listener);
        }

        foreach ($this->activeMegaTransferListeners as $listener)
        {
            $this->megaApi->removeTransferListener($listener);
        }

        foreach ($this->activeMegaGlobalListeners as $listener)
        {
            $this->megaApi->removeGlobalListener($listener);
        }

        sem_remove($this->semaphore);
    }
}
?>
