/*

MEGA SDK sample application for the gcc/POSIX environment, using cURL for HTTP I/O,
GNU Readline for console I/O and FreeImage for thumbnail creation

(c) 2013 by Mega Limited, Wellsford, New Zealand

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#ifndef MEGAAPI_IMPL_H
#define MEGAAPI_IMPL_H

#include <inttypes.h>

#include "mega.h"
#include "mega/thread/posixthread.h"
#include "mega/thread/qtthread.h"
#include "mega/gfx/external.h"
#include "mega/gfx/qt.h"
#include "mega/thread/cppthread.h"
#include "mega/proxy.h"
#include "megaapi.h"

#ifndef _WIN32
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <curl/curl.h>
#include <fcntl.h>
#endif

////////////////////////////// SETTINGS //////////////////////////////
////////// Support for threads and mutexes
//Choose one of these options.
//Otherwise, C++11 threads and mutexes will be used
//#define USE_PTHREAD
//#define USE_QT

////////// Support for thumbnails and previews.
//If you selected QT for threads and mutexes, it will be also used for thumbnails and previews
//You can create a subclass of MegaGfxProcessor and pass it to the constructor of MegaApi
//#define USE_FREEIMAGE

//Define WINDOWS_PHONE if you want to build the MEGA SDK for Windows Phone
//#define WINDOWS_PHONE
/////////////////////////// END OF SETTINGS ///////////////////////////

namespace mega
{

#ifdef USE_PTHREAD
typedef PosixThread MegaThread;
typedef PosixMutex MegaMutex;
#elif USE_QT
typedef QtThread MegaThread;
typedef QtMutex MegaMutex;
#else
typedef CppThread MegaThread;
typedef CppMutex MegaMutex;
#endif

#ifdef USE_QT
class MegaGfxProc : public GfxProcQT {};
#elif USE_FREEIMAGE
class MegaGfxProc : public GfxProcFreeImage {};
#else
class MegaGfxProc : public GfxProcExternal {};
#endif

#ifdef WIN32
    #ifndef WINDOWS_PHONE
    class MegaHttpIO : public WinHttpIO {};
    class MegaFileSystemAccess : public WinFileSystemAccess {};
    class MegaWaiter : public WinWaiter {};
    #else
    class MegaHttpIO : public CurlHttpIO {};
    class MegaFileSystemAccess : public WinFileSystemAccess {};
    class MegaWaiter : public WinPhoneWaiter {};
    #endif
#else
    #ifdef __APPLE__
    typedef CurlHttpIO MegaHttpIO;
    typedef PosixFileSystemAccess MegaFileSystemAccess;
    typedef PosixWaiter MegaWaiter;
    #else
    class MegaHttpIO : public CurlHttpIO {};
    class MegaFileSystemAccess : public PosixFileSystemAccess {};
    class MegaWaiter : public PosixWaiter {};
    #endif
#endif

class MegaDbAccess : public SqliteDbAccess
{
	public:
		MegaDbAccess(string *basePath = NULL) : SqliteDbAccess(basePath){}
};


class MegaNodePrivate : public MegaNode
{
	public:
		MegaNodePrivate(const char *name, int type, int64_t size, int64_t ctime, int64_t mtime, MegaHandle nodeMegaHandle, std::string *nodekey, std::string *attrstring);
		MegaNodePrivate(MegaNode *node);
		virtual ~MegaNodePrivate();
		virtual int getType();
		virtual const char* getName();
		virtual const char *getBase64Handle();
		virtual int64_t getSize();
		virtual int64_t getCreationTime();
		virtual int64_t getModificationTime();
		virtual MegaHandle getHandle();
		virtual std::string* getNodeKey();
        virtual const char *getBase64Key();
		virtual std::string* getAttrString();
		virtual int getTag();
		virtual bool isFile();
		virtual bool isFolder();
		virtual bool isRemoved();
		virtual bool isSyncDeleted();
		virtual std::string getLocalPath();
		virtual bool hasThumbnail();
		virtual bool hasPreview();
        virtual bool isPublic();

		static MegaNode *fromNode(Node *node);
		virtual MegaNode *copy();

	protected:
		MegaNodePrivate(Node *node);
		int type;
		const char *name;
		int64_t size;
		int64_t ctime;
		int64_t mtime;
		MegaHandle nodehandle;
		std::string nodekey;
		std::string attrstring;
		std::string localPath;
		int tag;
		bool removed;
		bool syncdeleted;
		bool thumbnailAvailable;
		bool previewAvailable;
        bool isPublicNode;
};


class MegaUserPrivate : public MegaUser
{
	public:
		static MegaUser *fromUser(User *user);
		virtual MegaUser *copy();

		~MegaUserPrivate();
		virtual const char* getEmail();
		virtual int getVisibility();
		virtual time_t getTimestamp();

	protected:
		MegaUserPrivate(User *user);
		MegaUserPrivate(MegaUser *user);
		const char *email;
		int visibility;
		time_t ctime;
};

class MegaSharePrivate : public MegaShare
{
	public:
		static MegaShare *fromShare(MegaHandle nodeMegaHandle, Share *share);
		virtual MegaShare *copy();
		virtual ~MegaSharePrivate();
		virtual const char *getUser();
		virtual MegaHandle getNodeHandle();
		virtual int getAccess();
		virtual int64_t getTimestamp();

	protected:
		MegaSharePrivate(MegaHandle nodehandle, Share *share);
		MegaSharePrivate(MegaShare *share);

		MegaHandle nodehandle;
		const char *user;
		int access;
		int64_t ts;
};

class MegaTransferPrivate : public MegaTransfer
{
	public:
		MegaTransferPrivate(int type, MegaTransferListener *listener = NULL);
        MegaTransferPrivate(const MegaTransferPrivate &transfer);
        virtual ~MegaTransferPrivate();
        
        virtual MegaTransfer *copy();
	    Transfer *getTransfer() const;
        void setTransfer(Transfer *transfer); 
        void setStartTime(int64_t startTime);
		void setTransferredBytes(long long transferredBytes);
		void setTotalBytes(long long totalBytes);
		void setPath(const char* path);
		void setParentPath(const char* path);
        void setNodeHandle(MegaHandle nodeHandle);
        void setParentHandle(MegaHandle parentHandle);
		void setNumConnections(int connections);
		void setStartPos(long long startPos);
		void setEndPos(long long endPos);
		void setMaxSpeed(int maxSpeed);
		void setNumRetry(int retry);
		void setMaxRetries(int retry);
        void setTime(int64_t time);
		void setFileName(const char* fileName);
		void setSlot(int id);
		void setBase64Key(const char* base64Key);
		void setTag(int tag);
		void setSpeed(long long speed);
		void setDeltaSize(long long deltaSize);
        void setUpdateTime(int64_t updateTime);
        void setPublicNode(MegaNode *publicNode);
        void setSyncTransfer(bool syncTransfer);
		void setLastBytes(char *lastBytes);
  
		virtual int getSlot() const;
		virtual int getType() const;
		virtual const char * getTransferString() const;
		virtual const char* toString() const;
		virtual const char* __str__() const;
        virtual int64_t getStartTime() const;
		virtual long long getTransferredBytes() const;
		virtual long long getTotalBytes() const;
		virtual const char* getPath() const;
		virtual const char* getParentPath() const;
        virtual MegaHandle getNodeHandle() const;
        virtual MegaHandle getParentHandle() const;
		virtual int getNumConnections() const;
		virtual long long getStartPos() const;
		virtual long long getEndPos() const;
		virtual int getMaxSpeed() const;
		virtual const char* getFileName() const;
		virtual MegaTransferListener* getListener() const;
		virtual int getNumRetry() const;
		virtual int getMaxRetries() const;
        virtual int64_t getTime() const;
		virtual const char* getBase64Key() const;
		virtual int getTag() const;
		virtual long long getSpeed() const;
		virtual long long getDeltaSize() const;
        virtual int64_t getUpdateTime() const;
        virtual MegaNode *getPublicNode() const;
        virtual bool isSyncTransfer() const;
        virtual bool isStreamingTransfer() const;
		virtual char *getLastBytes() const;
		
	protected:		
		int slot;
		int type;
		int tag;
        bool syncTransfer;
        int64_t startTime;
        int64_t updateTime;
        int64_t time;
		long long transferredBytes;
		long long totalBytes;
		long long speed;
		long long deltaSize;
        MegaHandle nodeHandle;
        MegaHandle parentHandle;
		const char* path;
		const char* parentPath;
		const char* fileName;
		const char* base64Key;
		char *lastBytes;
        MegaNode *publicNode;
		int numConnections;
		long long startPos;
		long long endPos;
		int maxSpeed;
		int retry;
		int maxRetries;
		MegaTransferListener *listener;
		Transfer *transfer;     
};

class MegaRequestPrivate : public MegaRequest
{
	public:
		MegaRequestPrivate(int type, MegaRequestListener *listener = NULL);
		MegaRequestPrivate(MegaRequestPrivate &request);
		virtual ~MegaRequestPrivate();
		MegaRequest *copy();
		void setNodeHandle(MegaHandle nodeHandle);
		void setLink(const char* link);
        void setParentHandle(MegaHandle parentHandle);
        void setSessionKey(const char* sessionKey);
		void setName(const char* name);
		void setEmail(const char* email);
    	void setPassword(const char* email);
    	void setNewPassword(const char* email);
		void setPrivateKey(const char* privateKey);
		void setAccess(int access);
		void setNumRetry(int ds);
		void setNextRetryDelay(int delay);
        void setPublicNode(MegaNode* publicNode);
		void setNumDetails(int numDetails);
		void setFile(const char* file);
        void setParamType(int type);
        void setText(const char* text);
        void setNumber(int number);
        void setFlag(bool flag);
        void setTransfer(int transfer);
        void setListener(MegaRequestListener *listener);
        void setTotalBytes(long long totalBytes);
        void setTransferredBytes(long long transferredBytes);
        void setTag(int tag);

        virtual int getType() const;
		virtual const char *getRequestString() const;
		virtual const char* toString() const;
		virtual const char* __str__() const;
        virtual MegaHandle getNodeHandle() const;
		virtual const char* getLink() const;
        virtual MegaHandle getParentHandle() const;
        virtual const char* getSessionKey() const;
		virtual const char* getName() const;
		virtual const char* getEmail() const;
		virtual const char* getPassword() const;
		virtual const char* getNewPassword() const;
		virtual const char* getPrivateKey() const;
		virtual int getAccess() const;
		virtual const char* getFile() const;
		virtual int getNumRetry() const;
		virtual int getNextRetryDelay() const;
        virtual MegaNode *getPublicNode();
        virtual int getParamType() const;
        virtual const char *getText() const;
        virtual int getNumber() const;
        virtual bool getFlag() const;
        virtual long long getTransferredBytes() const;
        virtual long long getTotalBytes() const;
		virtual MegaRequestListener *getListener() const;
		virtual MegaAccountDetails *getMegaAccountDetails() const;
        virtual int getTransfer() const;
		virtual int getNumDetails() const;
        virtual int getTag() const;
	    AccountDetails * getAccountDetails() const;
        
    protected:
        AccountDetails *accountDetails;        
		int type;
        MegaHandle nodeHandle;
		const char* link;
		const char* name;
        MegaHandle parentHandle;
        const char* sessionKey;
		const char* email;
		const char* password;
		const char* newPassword;
		const char* privateKey;
        const char* text;
        int number;
		int access;
		const char* file;
		int attrType;
        bool flag;
        long long totalBytes;
        long long transferredBytes;
		MegaRequestListener *listener;
        int transfer;
		int numDetails;
        MegaNode* publicNode;
		int numRetry;
		int nextRetryDelay;
        int tag;
};

class MegaAccountDetailsPrivate : public MegaAccountDetails
{
	public:
		static MegaAccountDetails *fromAccountDetails(AccountDetails *details);
		virtual ~MegaAccountDetailsPrivate() ;

		virtual int getProLevel();
		virtual long long getStorageMax();
		virtual long long getStorageUsed();
		virtual long long getTransferMax();
		virtual long long getTransferOwnUsed();

		virtual long long getStorageUsed(MegaHandle handle);
		virtual long long getNumFiles(MegaHandle handle);
		virtual long long getNumFolders(MegaHandle handle);
	
		virtual MegaAccountDetails* copy();

	private:
		MegaAccountDetailsPrivate(AccountDetails *details);
		AccountDetails *details;
};

class NodeListPrivate : public NodeList
{
	public:
		NodeListPrivate();
		NodeListPrivate(Node** newlist, int size);
		virtual ~NodeListPrivate();
		virtual MegaNode* get(int i);
		virtual int size();
	
	protected:
		MegaNode** list;
		int s;
};

class UserListPrivate : public UserList
{
	public:
		UserListPrivate();
		UserListPrivate(User** newlist, int size);
		virtual ~UserListPrivate();
		virtual MegaUser* get(int i);
		virtual int size();
	
	protected:
		MegaUser** list;
		int s;
};

class ShareListPrivate : public ShareList
{
	public:
		ShareListPrivate();
		ShareListPrivate(Share** newlist, MegaHandle *MegaHandlelist, int size);
		virtual ~ShareListPrivate();
		virtual MegaShare* get(int i);
		virtual int size();
		
	protected:
		MegaShare** list;
		int s;
};

class TransferListPrivate : public TransferList
{
	public:
		TransferListPrivate();
		TransferListPrivate(MegaTransfer** newlist, int size);
		virtual ~TransferListPrivate();
		virtual MegaTransfer* get(int i);
		virtual int size();
	
	protected:
		MegaTransfer** list;
		int s;
};

struct MegaFile : public File
{
    // app-internal sequence number for queue management
    int seqno;
    static int nextseqno;
    bool failed(error e);
    MegaFile();
};

struct MegaFileGet : public MegaFile
{
    void prepare();
    void updatelocalname();
    void progress();
    void completed(Transfer*, LocalNode*);
    void terminated();
	MegaFileGet(MegaClient *client, Node* n, string dstPath);
    MegaFileGet(MegaClient *client, MegaNode* n, string dstPath);
	~MegaFileGet() {}
};

struct MegaFilePut : public MegaFile
{
    void completed(Transfer* t, LocalNode*);
    void terminated();
    MegaFilePut(MegaClient *client, string* clocalname, string *filename, handle ch, const char* ctargetuser);
    ~MegaFilePut() {}
};

class TreeProcessor
{
    public:
        virtual bool processNode(Node* node);
        virtual ~TreeProcessor();
};

class SearchTreeProcessor : public TreeProcessor
{
    public:
        SearchTreeProcessor(const char *search);
        virtual bool processNode(Node* node);
        virtual ~SearchTreeProcessor() {}
        vector<Node *> &getResults();

    protected:
        const char *search;
        vector<Node *> results;
};

class SizeProcessor : public TreeProcessor
{
    protected:
        long long totalBytes;

    public:
        SizeProcessor();
        virtual bool processNode(Node* node);
        long long getTotalBytes();
};

//Thread safe request queue
class RequestQueue
{
    protected:
        std::deque<MegaRequestPrivate *> requests;
        MegaMutex mutex;

    public:
        RequestQueue();
        void push(MegaRequestPrivate *request);
        void push_front(MegaRequestPrivate *request);
        MegaRequestPrivate * pop();
        void removeListener(MegaRequestListener *listener);
};


//Thread safe transfer queue
class TransferQueue
{
    protected:
        std::deque<MegaTransferPrivate *> transfers;
        MegaMutex mutex;

    public:
        TransferQueue();
        void push(MegaTransferPrivate *transfer);
        void push_front(MegaTransferPrivate *transfer);
        MegaTransferPrivate * pop();
};

class MegaApiImpl : public MegaApp
{
    public:
        MegaApiImpl(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath = NULL, const char *userAgent = NULL);
        MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath = NULL, const char *userAgent = NULL);
        MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath, const char *userAgent, int fseventsfd);
        virtual ~MegaApiImpl();

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
        static handle base64ToHandle(const char* base64Handle);
        static const char* handleToBase64(MegaHandle handle);
        static const char* ebcEncryptKey(const char* encryptionKey, const char* plainKey);
        void retryPendingConnections(bool disconnect = false, bool includexfers = false, MegaRequestListener* listener = NULL);
        static void addEntropy(unsigned char* data, unsigned int size);

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
        void changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener = NULL);
        void addContact(const char* email, MegaRequestListener* listener=NULL);
        void removeContact(const char* email, MegaRequestListener* listener=NULL);
        void logout(MegaRequestListener *listener = NULL);
        void submitFeedback(int rating, const char *comment, MegaRequestListener *listener = NULL);
        void reportEvent(int event, const char *details = NULL, MegaRequestListener *listener = NULL);

        //Transfers
        void startUpload(const char* localPath, MegaNode *parent, MegaTransferListener *listener=NULL);
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener = NULL);
        void startDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void startStreaming(MegaNode* node, m_off_t startPos, m_off_t size, MegaTransferListener *listener);
        void startPublicDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void cancelTransfer(MegaTransfer *transfer, MegaRequestListener *listener=NULL);
        void cancelTransfers(int direction, MegaRequestListener *listener=NULL);
        void pauseTransfers(bool pause, MegaRequestListener* listener=NULL);
        void setUploadLimit(int bpslimit);
        TransferList *getTransfers();

        //Sync
        int syncPathState(string *path);
        MegaNode *getSyncedNode(string *path);
        void syncFolder(const char *localFolder, MegaNode *megaFolder);
        void resumeSync(const char *localFolder, long long localfp, MegaNode *megaFolder);
        void removeSync(handle nodehandle, MegaRequestListener *listener=NULL);
        int getNumActiveSyncs();
        void stopSyncs(MegaRequestListener *listener=NULL);
        void update();
        bool isIndexing();
        bool isWaiting();
        bool isSynced(MegaNode *n);
        void setExcludedNames(vector<string> *excludedNames);
        bool moveToLocalDebris(const char *path);
        bool is_syncable(const char* name);

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
		int getNumChildren(MegaNode* parent);
		int getNumChildFiles(MegaNode* parent);
		int getNumChildFolders(MegaNode* parent);
        NodeList* getChildren(MegaNode *parent, int order=1);
        MegaNode *getChildNode(MegaNode *parent, const char* name);
        MegaNode *getParentNode(MegaNode *node);
        const char* getNodePath(MegaNode *node);
        MegaNode *getNodeByPath(const char *path, MegaNode *n = NULL);
        MegaNode *getNodeByHandle(handle handler);
        UserList* getContacts();
        MegaUser* getContact(const char* email);
        NodeList *getInShares(MegaUser* user);
        NodeList *getInShares();
        ShareList *getOutShares(MegaNode *node);
        int getAccess(MegaNode* node);
        long long getSize(MegaNode *node);
        string getLocalPath(MegaNode *node);
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

        static bool nodeComparatorDefaultASC  (Node *i, Node *j);
        static bool nodeComparatorDefaultDESC (Node *i, Node *j);
        static bool nodeComparatorSizeASC  (Node *i, Node *j);
        static bool nodeComparatorSizeDESC (Node *i, Node *j);
        static bool nodeComparatorCreationASC  (Node *i, Node *j);
        static bool nodeComparatorCreationDESC  (Node *i, Node *j);
        static bool nodeComparatorModificationASC  (Node *i, Node *j);
        static bool nodeComparatorModificationDESC  (Node *i, Node *j);
        static bool nodeComparatorAlphabeticalASC  (Node *i, Node *j);
        static bool nodeComparatorAlphabeticalDESC  (Node *i, Node *j);
        static bool userComparatorDefaultASC (User *i, User *j);

protected:
        void init(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath = NULL, const char *userAgent = NULL, int fseventsfd = -1);

        static void *threadEntryPoint(void *param);

        void fireOnRequestStart(MegaRequestPrivate *request);
        void fireOnRequestFinish(MegaRequestPrivate *request, MegaError e);
        void fireOnRequestUpdate(MegaRequestPrivate *request);
        void fireOnRequestTemporaryError(MegaRequestPrivate *request, MegaError e);
        void fireOnTransferStart(MegaTransferPrivate *transfer);
        void fireOnTransferFinish(MegaTransferPrivate *transfer, MegaError e);
        void fireOnTransferUpdate(MegaTransferPrivate *transfer);
        bool fireOnTransferData(MegaTransferPrivate *transfer);
        void fireOnTransferTemporaryError(MegaTransferPrivate *transfer, MegaError e);
        void fireOnUsersUpdate(UserList *users);
        void fireOnNodesUpdate(NodeList *nodes);
        void fireOnReloadNeeded();
        void fireOnSyncStateChanged();
        void fireOnFileSyncStateChanged(const char *filePath, int newState);

        MegaApi *api;
        MegaThread thread;
        MegaClient *client;
        MegaHttpIO *httpio;
        MegaWaiter *waiter;
        MegaFileSystemAccess *fsAccess;
        MegaDbAccess *dbAccess;
        GfxProc *gfxAccess;
		
        RequestQueue requestQueue;
        TransferQueue transferQueue;
        map<int, MegaRequestPrivate *> requestMap;
        map<int, MegaTransferPrivate *> transferMap;
        int pendingUploads;
        int pendingDownloads;
        int totalUploads;
        int totalDownloads;
        long long totalDownloadedBytes;
        long long totalUploadedBytes;
        set<MegaRequestListener *> requestListeners;
        set<MegaTransferListener *> transferListeners;
        set<MegaGlobalListener *> globalListeners;
        set<MegaListener *> listeners;
        bool waiting;
        bool waitingRequest;
        vector<string> excludedNames;
        MegaMutex sdkMutex;
        MegaTransferPrivate *currentTransfer;
        int threadExit;
        dstime pausetime;
        void loop();

        int maxRetries;

        // a request-level error occurred
        virtual void request_error(error);
        virtual void request_response_progress(m_off_t, m_off_t);

        // login result
        virtual void login_result(error);

        // ephemeral session creation/resumption result
        virtual void ephemeral_result(error);
        virtual void ephemeral_result(handle, const byte*);

        // account creation
        virtual void sendsignuplink_result(error);
        virtual void querysignuplink_result(error);
        virtual void querysignuplink_result(handle, const char*, const char*, const byte*, const byte*, const byte*, size_t);
        virtual void confirmsignuplink_result(error);
        virtual void setkeypair_result(error);

        // account credentials, properties and history
        virtual void account_details(AccountDetails*,  bool, bool, bool, bool, bool, bool);
        virtual void account_details(AccountDetails*, error);

        virtual void setattr_result(handle, error);
        virtual void rename_result(handle, error);
        virtual void unlink_result(handle, error);
        virtual void nodes_updated(Node**, int);
        virtual void users_updated(User**, int);

        // password change result
        virtual void changepw_result(error);

        // user attribute update notification
        virtual void userattr_update(User*, int, const char*);


        virtual void fetchnodes_result(error);
        virtual void putnodes_result(error, targettype_t, NewNode*);

        // share update result
        virtual void share_result(error);
        virtual void share_result(int, error);

        // file attribute fetch result
        virtual void fa_complete(Node*, fatype, const char*, uint32_t);
        virtual int fa_failed(handle, fatype, int);

        // file attribute modification result
        virtual void putfa_result(handle, fatype, error);

        // purchase transactions
        virtual void enumeratequotaitems_result(handle, unsigned, unsigned, unsigned, unsigned, unsigned, const char*) { }
        virtual void enumeratequotaitems_result(error) { }
        virtual void additem_result(error) { }
        virtual void checkout_result(error) { }
        virtual void checkout_result(const char*) { }

        virtual void checkfile_result(handle h, error e);
        virtual void checkfile_result(handle h, error e, byte* filekey, m_off_t size, m_time_t ts, m_time_t tm, string* filename, string* fingerprint, string* fileattrstring);

        // user invites/attributes
        virtual void invite_result(error);
        virtual void putua_result(error);
        virtual void getua_result(error);
        virtual void getua_result(byte*, unsigned);

        // file node export result
        virtual void exportnode_result(error);
        virtual void exportnode_result(handle, handle);

        // exported link access result
        virtual void openfilelink_result(error);
        virtual void openfilelink_result(handle, const byte*, m_off_t, string*, string*, int);

        // global transfer queue updates (separate signaling towards the queued objects)
        virtual void transfer_added(Transfer*);
        virtual void transfer_removed(Transfer*);
        virtual void transfer_prepare(Transfer*);
        virtual void transfer_failed(Transfer*, error error);
        virtual void transfer_update(Transfer*);
        virtual void transfer_limit(Transfer*);
        virtual void transfer_complete(Transfer*);

        virtual dstime pread_failure(error, int, void*);
        virtual bool pread_data(byte*, m_off_t, m_off_t, void*);

        virtual void reportevent_result(error);

        // sync status updates and events
        virtual void syncupdate_state(Sync*, syncstate_t);
        virtual void syncupdate_scanning(bool scanning);
        virtual void syncupdate_stuck(string*);
        virtual void syncupdate_local_folder_addition(Sync*, const char*);
        virtual void syncupdate_local_folder_deletion(Sync*, const char*);
        virtual void syncupdate_local_file_addition(Sync*, const char*);
        virtual void syncupdate_local_file_deletion(Sync*, const char*);
        virtual void syncupdate_get(Sync*, const char*);
        virtual void syncupdate_put(Sync*, const char*);
        virtual void syncupdate_remote_file_addition(Node*);
        virtual void syncupdate_remote_file_deletion(Node*);
        virtual void syncupdate_remote_folder_addition(Node*);
        virtual void syncupdate_remote_folder_deletion(Node*);
        virtual void syncupdate_remote_copy(Sync*, const char*);
        virtual void syncupdate_remote_move(string*, string*);
        virtual void syncupdate_treestate(LocalNode*);
        virtual bool sync_syncable(Node*);
        virtual bool sync_syncable(const char*name, string*, string*);
        virtual void syncupdate_local_lockretry(bool);

        // suggest reload due to possible race condition with other clients
        virtual void reload(const char*);

        // wipe all users, nodes and shares
        virtual void clearing();

        // failed request retry notification
        virtual void notify_retry(dstime);

        // generic debug logging
        virtual void debug_log(const char*);
        //////////

        void sendPendingRequests();
        void sendPendingTransfers();
        char *stringToArray(string &buffer);

        //Internal
        Node* getChildNodeInternal(Node *parent, const char* name);
        Node* getNodeByFingerprintInternal(const char *fingerprint);
        bool processTree(Node* node, TreeProcessor* processor, bool recursive = 1);
        NodeList* search(Node* node, const char* searchString, bool recursive = 1);
        void getAccountDetails(bool storage, bool transfer, bool pro, bool transactions, bool purchases, bool sessions, MegaRequestListener *listener = NULL);
        void getNodeAttribute(MegaNode* node, int type, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetNodeAttribute(MegaNode *node, int type, MegaRequestListener *listener = NULL);
        void setNodeAttribute(MegaNode* node, int type, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void getUserAttribute(MegaUser* user, int type, const char *dstFilePath, MegaRequestListener *listener = NULL);
        void setUserAttribute(int type, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void startUpload(const char* localPath, MegaNode* parent, int connections, int maxSpeed, const char* fileName, MegaTransferListener *listener);
        void startUpload(const char* localPath, MegaNode* parent, int maxSpeed, MegaTransferListener *listener = NULL);
        void startDownload(handle nodehandle, const char* target, int connections, long startPos, long endPos, const char* base64key, MegaTransferListener *listener);
};

class MegaHashSignatureImpl
{
	public:
		MegaHashSignatureImpl(const char *base64Key);
		~MegaHashSignatureImpl();
		void init();
		void add(const char *data, unsigned size);
		bool check(const char *base64Signature);

	protected:    
		HashSignature *hashSignature;
		AsymmCipher* asymmCypher;
};

}

#endif //MEGAAPI_IMPL_H
