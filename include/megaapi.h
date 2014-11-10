#ifndef MEGAAPI_H
#define MEGAAPI_H

#include <string>
#include <vector>
#include <inttypes.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace mega
{   		
typedef uint64_t MegaHandle;
#ifdef WIN32
    const char MEGA_DEBRIS_FOLDER[] = "Rubbish";
#else
    const char MEGA_DEBRIS_FOLDER[] = ".debris";
#endif
    const MegaHandle INVALID_HANDLE = ~(MegaHandle)0;

class MegaListener;
class MegaRequestListener;
class MegaTransferListener;
class MegaGlobalListener;
class MegaTreeProcessor;
class MegaAccountDetails;
class MegaPricing;
class MegaTransfer;
class MegaNode;
class MegaUser;
class MegaShare;
class MegaError;
class MegaRequest;
class MegaTransfer;
class NodeList;
class UserList;
class ShareList;
class TransferList;
class MegaApi;

class MegaGfxProcessor
{
public:
	virtual bool readBitmap(const char* path);
	virtual int getWidth();
	virtual int getHeight();
	virtual int getBitmapDataSize(int w, int h, int px, int py, int rw, int rh);
	virtual bool getBitmapData(char *bitmapData, size_t size);
	virtual void freeBitmap();
	virtual ~MegaGfxProcessor();
};

class MegaProxy
{
public:
    enum {PROXY_NONE = 0, PROXY_AUTO = 1, PROXY_CUSTOM = 2};

    MegaProxy();
    virtual ~MegaProxy();
    void setProxyType(int proxyType);
    void setProxyURL(const char *proxyURL);
    void setCredentials(const char *username, const char *password);
    int getProxyType();
    const char *getProxyURL();
    bool credentialsNeeded();
    const char *getUsername();
    const char *getPassword();

protected:
    int proxyType;
    const char *proxyURL;
    const char *username;
    const char *password;
};

class MegaLogger
{
public:
    virtual void log(const char *time, int loglevel, const char *source, const char *message);
    virtual ~MegaLogger(){}
};

class MegaNode
{
    public:
		enum {
			TYPE_UNKNOWN = -1,
			TYPE_FILE = 0,
			TYPE_FOLDER,
			TYPE_ROOT,
			TYPE_INCOMING,
			TYPE_RUBBISH,
			TYPE_MAIL
		};

        virtual ~MegaNode() = 0;
        virtual MegaNode *copy() = 0;

        virtual int getType() = 0;
        virtual const char* getName() = 0;
        virtual const char* getBase64Handle() = 0;
        virtual int64_t getSize() = 0;
        virtual int64_t getCreationTime() = 0;
        virtual int64_t getModificationTime() = 0;
        virtual MegaHandle getHandle() = 0;
        virtual std::string* getNodeKey() = 0;
        virtual const char* getBase64Key() = 0;
        virtual std::string* getAttrString() = 0;
        virtual int getTag() = 0;
        virtual bool isFile() = 0;
        virtual bool isFolder() = 0;
        virtual bool isRemoved() = 0;
        virtual bool isSyncDeleted() = 0;
        virtual std::string getLocalPath() = 0;
        virtual bool hasThumbnail() = 0;
        virtual bool hasPreview() = 0;
        virtual bool isPublic() = 0;
};

class MegaUser
{
	public:
		enum {
			VISIBILITY_UNKNOWN = -1,
			VISIBILITY_HIDDEN = 0,
			VISIBILITY_VISIBLE,
			VISIBILITY_ME
		};

		virtual ~MegaUser() = 0;
		virtual MegaUser *copy() = 0;
		virtual const char* getEmail() = 0;
		virtual int getVisibility() = 0;
		virtual time_t getTimestamp() = 0;
};

class MegaShare
{
	public:
		enum {
			ACCESS_UNKNOWN = -1,
			ACCESS_READ = 0,
			ACCESS_READWRITE,
			ACCESS_FULL,
			ACCESS_OWNER
		};

		virtual ~MegaShare() = 0;
		virtual MegaShare *copy() = 0;
		virtual const char *getUser() = 0;
        virtual MegaHandle getNodeHandle() = 0;
		virtual int getAccess() = 0;
        virtual int64_t getTimestamp() = 0;
};

class NodeList
{
	public:
		virtual ~NodeList() = 0;
		virtual MegaNode* get(int i) = 0;
		virtual int size() = 0;
};

class UserList
{
	public:
		virtual ~UserList() = 0;
		virtual MegaUser* get(int i) = 0;
		virtual int size() = 0;
};

class ShareList
{
	public:
		virtual ~ShareList() = 0;
		virtual MegaShare* get(int i) = 0;
		virtual int size() = 0;
};

class TransferList
{
	public:
		virtual ~TransferList() = 0;
		virtual MegaTransfer* get(int i) = 0;
		virtual int size() = 0;
};

class MegaRequest
{
	public:
        enum {  TYPE_LOGIN, TYPE_MKDIR, TYPE_MOVE, TYPE_COPY,
                TYPE_RENAME, TYPE_REMOVE, TYPE_SHARE,
                TYPE_FOLDER_ACCESS, TYPE_IMPORT_LINK, TYPE_IMPORT_NODE,
                TYPE_EXPORT, TYPE_FETCH_NODES, TYPE_ACCOUNT_DETAILS,
                TYPE_CHANGE_PW, TYPE_UPLOAD, TYPE_LOGOUT, TYPE_FAST_LOGIN,
                TYPE_GET_PUBLIC_NODE, TYPE_GET_ATTR_FILE,
                TYPE_SET_ATTR_FILE, TYPE_GET_ATTR_USER,
                TYPE_SET_ATTR_USER, TYPE_RETRY_PENDING_CONNECTIONS,
                TYPE_ADD_CONTACT, TYPE_REMOVE_CONTACT, TYPE_CREATE_ACCOUNT, TYPE_FAST_CREATE_ACCOUNT,
                TYPE_CONFIRM_ACCOUNT, TYPE_FAST_CONFIRM_ACCOUNT,
                TYPE_QUERY_SIGNUP_LINK, TYPE_ADD_SYNC, TYPE_REMOVE_SYNC,
                TYPE_REMOVE_SYNCS, TYPE_PAUSE_TRANSFERS,
                TYPE_CANCEL_TRANSFER, TYPE_CANCEL_TRANSFERS,
                TYPE_DELETE, TYPE_REPORT_EVENT, TYPE_CANCEL_ATTR_FILE,
                TYPE_GET_PRICING, TYPE_GET_PAYMENT_URL};

		virtual ~MegaRequest() = 0;
		virtual MegaRequest *copy() = 0;
		virtual int getType() const = 0;
		virtual const char *getRequestString() const = 0;
		virtual const char* toString() const = 0;
		virtual const char* __str__() const = 0;
        virtual MegaHandle getNodeHandle() const = 0;
		virtual const char* getLink() const = 0;
        virtual MegaHandle getParentHandle() const = 0;
        virtual const char* getSessionKey() const = 0;
		virtual const char* getName() const = 0;
		virtual const char* getEmail() const = 0;
		virtual const char* getPassword() const = 0;
		virtual const char* getNewPassword() const = 0;
		virtual const char* getPrivateKey() const = 0;
		virtual int getAccess() const = 0;
		virtual const char* getFile() const = 0;
		virtual int getNumRetry() const = 0;
		virtual int getNextRetryDelay() const = 0;
        virtual MegaNode *getPublicNode() const = 0;
        virtual MegaNode *getPublicMegaNode() const = 0;
        virtual int getParamType() const = 0;
        virtual bool getFlag() const = 0;
        virtual long long getTransferredBytes() const = 0;
        virtual long long getTotalBytes() const = 0;
		virtual MegaRequestListener *getListener() const = 0;
		virtual MegaAccountDetails *getMegaAccountDetails() const = 0;
        virtual MegaPricing *getPricing() const = 0;
        virtual int getTransfer() const = 0;
		virtual int getNumDetails() const = 0;
};

class MegaTransfer
{
	public:
        enum {TYPE_DOWNLOAD, TYPE_UPLOAD};
        
		virtual ~MegaTransfer() = 0;
        virtual MegaTransfer *copy() = 0;
		virtual int getSlot() const = 0;
		virtual int getType() const = 0;
		virtual const char * getTransferString() const = 0;
		virtual const char* toString() const = 0;
		virtual const char* __str__() const = 0;
        virtual int64_t getStartTime() const = 0;
		virtual long long getTransferredBytes() const = 0;
		virtual long long getTotalBytes() const = 0;
		virtual const char* getPath() const = 0;
		virtual const char* getParentPath() const = 0;
        virtual MegaHandle getNodeHandle() const = 0;
        virtual MegaHandle getParentHandle() const = 0;
		virtual int getNumConnections() const = 0;
		virtual long long getStartPos() const = 0;
		virtual long long getEndPos() const = 0;
		virtual int getMaxSpeed() const = 0;
		virtual const char* getFileName() const = 0;
		virtual MegaTransferListener* getListener() const = 0;
		virtual int getNumRetry() const = 0;
		virtual int getMaxRetries() const = 0;
        virtual int64_t getTime() const = 0;
		virtual const char* getBase64Key() const = 0;
		virtual int getTag() const = 0;
		virtual long long getSpeed() const = 0;
		virtual long long getDeltaSize() const = 0;
        virtual int64_t getUpdateTime() const = 0;
        virtual MegaNode *getPublicNode() const = 0;
        virtual MegaNode *getPublicMegaNode() const = 0;
        virtual bool isSyncTransfer() const = 0;
        virtual bool isStreamingTransfer() const = 0;
		virtual char *getLastBytes() const = 0;
};

class MegaError
{
	public:
		// error codes
		enum {
			API_OK = 0,
			API_EINTERNAL = -1,		// internal error
			API_EARGS = -2,			// bad arguments
			API_EAGAIN = -3,		// request failed, retry with exponential backoff
			API_ERATELIMIT = -4,	// too many requests, slow down
			API_EFAILED = -5,		// request failed permanently
			API_ETOOMANY = -6,		// too many requests for this resource
			API_ERANGE = -7,		// resource access out of rage
			API_EEXPIRED = -8,		// resource expired
			API_ENOENT = -9,		// resource does not exist
			API_ECIRCULAR = -10,	// circular linkage
			API_EACCESS = -11,		// access denied
			API_EEXIST = -12,		// resource already exists
			API_EINCOMPLETE = -13,	// request incomplete
			API_EKEY = -14,			// cryptographic error
			API_ESID = -15,			// bad session ID
			API_EBLOCKED = -16,		// resource administratively blocked
			API_EOVERQUOTA = -17,	// quote exceeded
			API_ETEMPUNAVAIL = -18,	// resource temporarily not available
			API_ETOOMANYCONNECTIONS = -19, // too many connections on this resource
			API_EWRITE = -20,		// file could not be written to
			API_EREAD = -21,		// file could not be read from
			API_EAPPKEY = -22		// invalid or missing application key
		};

		MegaError(int errorCode);
		MegaError(const MegaError &megaError);
        virtual ~MegaError(){}
		MegaError* copy();
		int getErrorCode() const;
		const char* getErrorString() const;
        const char* toString() const;
		const char* __str__() const;
		long getNextAttempt() const;
		void setNextAttempt(long nextAttempt);
        static const char *getErrorString(int errorCode);

	protected:
        //< 0 = API error code, > 0 = http error, 0 = No error
		int errorCode;
		long nextAttempt;
};


class MegaTreeProcessor
{
    public:
        virtual bool processMegaNode(MegaNode* node);
        virtual ~MegaTreeProcessor();
};

//Request callbacks
class MegaRequestListener
{
    public:
        //Request callbacks
        virtual void onRequestStart(MegaApi* api, MegaRequest *request);
        virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);
        virtual void onRequestUpdate(MegaApi*, MegaRequest *);
        virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e);
        virtual ~MegaRequestListener();
};

//Transfer callbacks
class MegaTransferListener
{
    public:
        //Transfer callbacks
        virtual void onTransferStart(MegaApi *api, MegaTransfer *transfer);
        virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e);
        virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);
        virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e);
        virtual ~MegaTransferListener();

        //For streaming downloads only
        virtual bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size);
};

//Global callbacks
class MegaGlobalListener
{
    public:
        //Global callbacks
    #if defined(__ANDROID__) || defined(WINDOWS_PHONE) || defined(TARGET_OS_IPHONE)
        virtual void onUsersUpdate(MegaApi* api);
        virtual void onNodesUpdate(MegaApi* api);
    #else
        virtual void onUsersUpdate(MegaApi* api, UserList *users);
        virtual void onNodesUpdate(MegaApi* api, NodeList *nodes);
    #endif
        virtual void onReloadNeeded(MegaApi* api);
        virtual ~MegaGlobalListener();
};

//All callbacks (no multiple inheritance because it isn't available in other programming languages)
class MegaListener
{
    public:
        virtual void onRequestStart(MegaApi* api, MegaRequest *request);
        virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);
        virtual void onRequestUpdate(MegaApi* api, MegaRequest *request);
        virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e);
        virtual void onTransferStart(MegaApi *api, MegaTransfer *transfer);
        virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e);
        virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);
        virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e);
    #if defined(__ANDROID__) || defined(WINDOWS_PHONE) || defined(TARGET_OS_IPHONE)
        virtual void onUsersUpdate(MegaApi* api);
        virtual void onNodesUpdate(MegaApi* api);
    #else
        virtual void onUsersUpdate(MegaApi* api, UserList *users);
        virtual void onNodesUpdate(MegaApi* api, NodeList *nodes);
    #endif
        virtual void onReloadNeeded(MegaApi* api);
        virtual void onSyncFileStateChanged(MegaApi *api, const char *filePath, int newState);
        virtual void onSyncStateChanged(MegaApi *api);

        virtual ~MegaListener();
};

class MegaApiImpl;
class MegaApi
{
    public:
    	enum
		{
			STATE_NONE = 0,
			STATE_SYNCED,
			STATE_PENDING,
			STATE_SYNCING,
			STATE_IGNORED
		};

        enum
        {
            EVENT_FEEDBACK = 0,
            EVENT_DEBUG,
            EVENT_INVALID
        };

        enum {
            LOG_LEVEL_FATAL = 0,   // Very severe error event that will presumably lead the application to abort.
            LOG_LEVEL_ERROR,   // Error information but will continue application to keep running.
            LOG_LEVEL_WARNING, // Information representing errors in application but application will keep running
            LOG_LEVEL_INFO,    // Mainly useful to represent current progress of application.
            LOG_LEVEL_DEBUG,   // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
            LOG_LEVEL_MAX
        };

        MegaApi(const char *appKey, MegaGfxProcessor* processor, const char *basePath = NULL, const char *userAgent = NULL);
        MegaApi(const char *appKey, const char *basePath = NULL, const char *userAgent = NULL);
        MegaApi(const char *appKey, const char *basePath, const char *userAgent, int fseventsfd);
        virtual ~MegaApi();

        //Multiple listener management.
        void addListener(MegaListener* listener);
        void addRequestListener(MegaRequestListener* listener);
        void addTransferListener(MegaTransferListener* listener);
        void addGlobalListener(MegaGlobalListener* listener);
        void removeListener(MegaListener* listener);
        void removeRequestListener(MegaRequestListener* listener);
        void removeTransferListener(MegaTransferListener* listener);
        void removeGlobalListener(MegaGlobalListener* listener);

        //Utils
        const char* getBase64PwKey(const char *password);
        const char* getStringHash(const char* base64pwkey, const char* inBuf);
        static MegaHandle base64ToHandle(const char* base64Handle);
        static const char* handleToBase64(MegaHandle handle);
        static const char* ebcEncryptKey(const char* encryptionKey, const char* plainKey);
        void retryPendingConnections(bool disconnect = false, bool includexfers = false, MegaRequestListener* listener = NULL);
        static void addEntropy(char* data, unsigned int size);

        //API requests
        void login(const char* email, const char* password, MegaRequestListener *listener = NULL);
        const char *dumpSession();
        void fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener = NULL);
        void fastLogin(const char* session, MegaRequestListener *listener = NULL);
        void createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener = NULL);
        void fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener = NULL);
        void querySignupLink(const char* link, MegaRequestListener *listener = NULL);
        void confirmAccount(const char* link, const char *password, MegaRequestListener *listener = NULL);
        void fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener = NULL);
        void setProxySettings(MegaProxy *proxySettings);
        MegaProxy *getAutoProxySettings();
        int isLoggedIn();
        const char* getMyEmail();

        //Logging
        static void setLogLevel(int logLevel);
        static void setLoggerClass(MegaLogger *megaLogger);
        static void log(int logLevel, const char* message, const char *filename = "", int line = -1);

        void createFolder(const char* name, MegaNode *parent, MegaRequestListener *listener = NULL);
        void moveNode(MegaNode* node, MegaNode* newParent, MegaRequestListener *listener = NULL);
        void copyNode(MegaNode* node, MegaNode *newParent, MegaRequestListener *listener = NULL);
        void renameNode(MegaNode* node, const char* newName, MegaRequestListener *listener = NULL);
        void remove(MegaNode* node, MegaRequestListener *listener = NULL);
        void sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener = NULL);
        void sendFileToUser(MegaNode *node, const char* email, MegaRequestListener *listener = NULL);
        void share(MegaNode *node, MegaUser* user, int level, MegaRequestListener *listener = NULL);
        void share(MegaNode* node, const char* email, int level, MegaRequestListener *listener = NULL);
        void folderAccess(const char* megaFolderLink, MegaRequestListener *listener = NULL);
        void importFileLink(const char* megaFileLink, MegaNode* parent, MegaRequestListener *listener = NULL);
        void importPublicNode(MegaNode *publicNode, MegaNode *parent, MegaRequestListener *listener = NULL);
        void getPublicNode(const char* megaFileLink, MegaRequestListener *listener = NULL);
        void getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener = NULL);
        void setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetPreview(MegaNode* node, MegaRequestListener *listener = NULL);
        void setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void setAvatar(const char *dstFilePath, MegaRequestListener *listener = NULL);
        void exportNode(MegaNode *node, MegaRequestListener *listener = NULL);
        void disableExport(MegaNode *node, MegaRequestListener *listener = NULL);
        void fetchNodes(MegaRequestListener *listener = NULL);
        void getAccountDetails(MegaRequestListener *listener = NULL);
        void getPricing(MegaRequestListener *listener = NULL);
        void getPaymentUrl(MegaHandle productHandle, MegaRequestListener *listener = NULL);
        const char *exportMasterKey();

        void changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener = NULL);
        void addContact(const char* email, MegaRequestListener* listener=NULL);
        void removeContact(const char* email, MegaRequestListener* listener=NULL);
        void logout(MegaRequestListener *listener = NULL);
        void submitFeedback(int rating, const char *comment, MegaRequestListener *listener = NULL);
        void reportDebugEvent(const char *text, MegaRequestListener *listener = NULL);

        //Transfers
        void startUpload(const char* localPath, MegaNode *parent, MegaTransferListener *listener=NULL);
        void startUpload(const char* localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener=NULL);
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener = NULL);
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, int64_t mtime, MegaTransferListener *listener = NULL);
        void startDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void startStreaming(MegaNode* node, int64_t startPos, int64_t size, MegaTransferListener *listener);
        void startPublicDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void cancelTransfer(MegaTransfer *transfer, MegaRequestListener *listener=NULL);
        void cancelTransfers(int direction, MegaRequestListener *listener=NULL);
        void pauseTransfers(bool pause, MegaRequestListener* listener=NULL);
        void setUploadLimit(int bpslimit);
        TransferList *getTransfers();

        //Sync
        int syncPathState(std::string *path);
        MegaNode *getSyncedNode(std::string *path);
        void syncFolder(const char *localFolder, MegaNode *megaFolder);
        void resumeSync(const char *localFolder, long long localfp, MegaNode *megaFolder);
        void removeSync(MegaHandle nodeMegaHandle, MegaRequestListener *listener=NULL);
        int getNumActiveSyncs();
        void stopSyncs(MegaRequestListener *listener=NULL);
        void update();
        bool isIndexing();
        bool isWaiting();
        bool isSynced(MegaNode *n);
        void setExcludedNames(std::vector<std::string> *excludedNames);
        bool moveToLocalDebris(const char *path);
        bool isSyncable(const char *name);

        //Statistics
        int getNumPendingUploads();
        int getNumPendingDownloads();
        int getTotalUploads();
        int getTotalDownloads();
        void resetTotalDownloads();
        void resetTotalUploads();
        void updateStatics();
        long long getTotalDownloadedBytes();
        long long getTotalUploadedBytes();

        //Filesystem
        enum {	ORDER_NONE, ORDER_DEFAULT_ASC, ORDER_DEFAULT_DESC,
            ORDER_SIZE_ASC, ORDER_SIZE_DESC,
            ORDER_CREATION_ASC, ORDER_CREATION_DESC,
            ORDER_MODIFICATION_ASC, ORDER_MODIFICATION_DESC,
            ORDER_ALPHABETICAL_ASC, ORDER_ALPHABETICAL_DESC};

		int getNumChildren(MegaNode* parent);
		int getNumChildFiles(MegaNode* parent);
		int getNumChildFolders(MegaNode* parent);
        NodeList* getChildren(MegaNode *parent, int order=1);
        MegaNode *getChildNode(MegaNode *parent, const char* name);
        MegaNode *getParentNode(MegaNode *node);
        const char* getNodePath(MegaNode *node);
        MegaNode *getNodeByPath(const char *path, MegaNode *n = NULL);
        MegaNode *getNodeByHandle(MegaHandle MegaHandler);
        UserList* getContacts();
        MegaUser* getContact(const char* email);
        NodeList *getInShares(MegaUser* user);
        NodeList *getInShares();
        bool isShared(MegaNode *node);
        ShareList *getOutShares();
        ShareList *getOutShares(MegaNode *node);
        int getAccess(MegaNode* node);
        long long getSize(MegaNode *node);
        std::string getLocalPath(MegaNode *node);
        static void removeRecursively(const char *path);

        //Fingerprint
        const char* getFingerprint(const char *filePath);
        const char *getFingerprint(MegaNode *node);
        MegaNode *getNodeByFingerprint(const char* fingerprint);
        bool hasFingerprint(const char* fingerprint);

        //Permissions
        MegaError checkAccess(MegaNode* node, int level);
        MegaError checkMove(MegaNode* node, MegaNode* target);

        MegaNode *getRootNode();
        MegaNode* getInboxNode();
        MegaNode *getRubbishNode();

        NodeList* search(MegaNode* node, const char* searchString, bool recursive = 1);
        bool processMegaTree(MegaNode* node, MegaTreeProcessor* processor, bool recursive = 1);

	#ifdef _WIN32
        static void utf16ToUtf8(const wchar_t* utf16data, int utf16size, std::string* utf8string);
        static void utf8ToUtf16(const char* utf8data, std::string* utf16string);
    #endif
        static char* strdup(const char* buffer);

private:
        MegaApiImpl *pImpl;
};


class MegaHashSignatureImpl;
class MegaHashSignature
{
public:
    MegaHashSignature(const char *base64Key);
    ~MegaHashSignature();
    void init();
    void add(const char *data, unsigned size);
    bool check(const char *base64Signature);

private:
	MegaHashSignatureImpl *pImpl;    
};

class MegaAccountDetails
{
public:
	virtual ~MegaAccountDetails() = 0;
    virtual int getProLevel() = 0;
    virtual long long getStorageMax() = 0;
    virtual long long getStorageUsed() = 0;
    virtual long long getTransferMax() = 0;
    virtual long long getTransferOwnUsed() = 0;
    virtual long long getStorageUsed(MegaHandle handle) = 0;
    virtual long long getNumFiles(MegaHandle handle) = 0;
    virtual long long getNumFolders(MegaHandle handle) = 0;
	virtual MegaAccountDetails* copy() = 0;
};

class MegaPricing
{
public:
    virtual ~MegaPricing() = 0;
    virtual int getNumProducts() = 0;
    virtual MegaHandle getHandle(int productIndex) = 0;
    virtual int getProLevel(int productIndex) = 0;
    virtual int getGBStorage(int productIndex) = 0;
    virtual int getGBTransfer(int productIndex) = 0;
    virtual int getMonths(int productIndex) = 0;
    virtual int getAmount(int productIndex) = 0;
    virtual const char* getCurrency(int productIndex) = 0;
    virtual MegaPricing *copy() = 0;
};

}

#endif //MEGAAPI_H
