/**
 * @file megaapi_impl.h
 * @brief Private header file of the intermediate layer for the MEGA C++ SDK.
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGAAPI_IMPL_H
#define MEGAAPI_IMPL_H

#include <inttypes.h>

#include "mega.h"
#include "mega/thread/posixthread.h"
#include "mega/thread/qtthread.h"
#include "mega/thread/win32thread.h"
#include "mega/gfx/external.h"
#include "mega/gfx/qt.h"
#include "mega/thread/cppthread.h"
#include "mega/proxy.h"
#include "megaapi.h"
#include "mega/userAttributes.h"

#ifndef _WIN32
    #if (!defined(USE_CURL_PUBLIC_KEY_PINNING)) || defined(WINDOWS_PHONE)
    #include <openssl/ssl.h>
    #include <openssl/rand.h>
    #endif
#include <curl/curl.h>
#include <fcntl.h>
#endif

#if TARGET_OS_IPHONE
#include "mega/gfx/GfxProcCG.h"
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
#elif defined(_WIN32) && !defined(WINDOWS_PHONE)
typedef Win32Thread MegaThread;
typedef Win32Mutex MegaMutex;
#else
typedef CppThread MegaThread;
typedef CppMutex MegaMutex;
#endif

#ifdef USE_QT
class MegaGfxProc : public GfxProcQT {};
#elif USE_FREEIMAGE
class MegaGfxProc : public GfxProcFreeImage {};
#elif TARGET_OS_IPHONE
class MegaGfxProc : public GfxProcCG {};
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

class ExternalLogger : public Logger
{
public:
    ExternalLogger();
    void setMegaLogger(MegaLogger *logger);
    void setLogLevel(int logLevel);
    void postLog(int logLevel, const char *message, const char *filename, int line);
    virtual void log(const char *time, int loglevel, const char *source, const char *message);

private:
    MegaMutex mutex;
    MegaLogger *megaLogger;
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
        bool isRemoved();
        virtual bool hasChanged(int changeType);
        virtual int getChanges();
		virtual bool hasThumbnail();
		virtual bool hasPreview();
        virtual bool isPublic();

#ifdef ENABLE_SYNC
        virtual bool isSyncDeleted();
        virtual std::string getLocalPath();
#endif

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
		int tag;
        int changed;
		bool thumbnailAvailable;
		bool previewAvailable;
        bool isPublicNode;

#ifdef ENABLE_SYNC
        bool syncdeleted;
        std::string localPath;
#endif
};


class MegaUserPrivate : public MegaUser
{
	public:
		MegaUserPrivate(User *user);
		MegaUserPrivate(MegaUser *user);
		static MegaUser *fromUser(User *user);
		virtual MegaUser *copy();

		~MegaUserPrivate();
		virtual const char* getEmail();
		virtual int getVisibility();
		virtual time_t getTimestamp();

	protected:
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
		void setNumRetry(int retry);
		void setMaxRetries(int retry);
        void setTime(int64_t time);
		void setFileName(const char* fileName);
		void setSlot(int id);
		void setTag(int tag);
		void setSpeed(long long speed);
		void setDeltaSize(long long deltaSize);
        void setUpdateTime(int64_t updateTime);
        void setPublicNode(MegaNode *publicNode);
        void setSyncTransfer(bool syncTransfer);
        void setLastBytes(char *lastBytes);
        void setLastErrorCode(error errorCode);

		virtual int getType() const;
		virtual const char * getTransferString() const;
		virtual const char* toString() const;
		virtual const char* __str__() const;
		virtual const char* __toString() const;
        virtual int64_t getStartTime() const;
		virtual long long getTransferredBytes() const;
		virtual long long getTotalBytes() const;
		virtual const char* getPath() const;
		virtual const char* getParentPath() const;
        virtual MegaHandle getNodeHandle() const;
        virtual MegaHandle getParentHandle() const;
		virtual long long getStartPos() const;
		virtual long long getEndPos() const;
		virtual const char* getFileName() const;
		virtual MegaTransferListener* getListener() const;
		virtual int getNumRetry() const;
		virtual int getMaxRetries() const;
        virtual int64_t getTime() const;
		virtual int getTag() const;
		virtual long long getSpeed() const;
		virtual long long getDeltaSize() const;
        virtual int64_t getUpdateTime() const;
        virtual MegaNode *getPublicNode() const;
        virtual MegaNode *getPublicMegaNode() const;
        virtual bool isSyncTransfer() const;
        virtual bool isStreamingTransfer() const;
        virtual char *getLastBytes() const;
        virtual error getLastErrorCode() const;

	protected:		
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
        char *lastBytes;
        MegaNode *publicNode;
		long long startPos;
		long long endPos;
		int retry;
		int maxRetries;

		MegaTransferListener *listener;
        Transfer *transfer;
        error lastError;
};

#ifdef ENABLE_SYNC

class MegaSyncEventPrivate: public MegaSyncEvent
{
public:
    MegaSyncEventPrivate(int type);
    virtual ~MegaSyncEventPrivate();

    virtual MegaSyncEvent *copy();

    virtual int getType() const;
    virtual const char *getPath() const;
    virtual MegaHandle getNodeHandle() const;
    virtual const char *getNewPath() const;
    virtual const char* getPrevName() const;
    virtual MegaHandle getPrevParent() const;

    void setPath(const char* path);
    void setNodeHandle(MegaHandle nodeHandle);
    void setNewPath(const char* newPath);
    void setPrevName(const char* prevName);
    void setPrevParent(MegaHandle prevParent);

protected:
    int type;
    const char* path;
    const char* newPath;
    const char* prevName;
    MegaHandle nodeHandle;
    MegaHandle prevParent;
};

class MegaSyncPrivate : public MegaSync
{  
public:
    MegaSyncPrivate(Sync *sync);
    MegaSyncPrivate(MegaSyncPrivate &sync);

    virtual ~MegaSyncPrivate();

    virtual MegaSync *copy();

    virtual MegaHandle getMegaHandle() const;
    void setMegaHandle(MegaHandle handle);
    virtual const char* getLocalFolder() const;
    void setLocalFolder(const char*path);
    virtual long long getLocalFingerprint() const;
    void setLocalFingerprint(long long fingerprint);
    virtual int getTag() const;
    void setTag(int tag);
    void setListener(MegaSyncListener *listener);
    MegaSyncListener *getListener();
    virtual int getState() const;
    void setState(int state);

protected:
    MegaHandle megaHandle;
    string localFolder;
    int tag;
    long long fingerprint;
    MegaSyncListener *listener;
    int state;
};

#endif


class MegaPricingPrivate;
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
        void setNumber(long long number);
        void setFlag(bool flag);
        void setTransferTag(int transfer);
        void setListener(MegaRequestListener *listener);
        void setTotalBytes(long long totalBytes);
        void setTransferredBytes(long long transferredBytes);
        void setTag(int tag);
        void addProduct(handle product, int proLevel, int gbStorage, int gbTransfer,
                        int months, int amount, const char *currency, const char *description, const char *iosid, const char *androidid);

        // ATTR
        void setAttributeMap(TLV*, unsigned int tlvLen, int);
        void setAttributeMap(ValueMap map, int);
        void setAttributeName(const char *an);

        virtual int getType() const;
		virtual const char *getRequestString() const;
		virtual const char* toString() const;
		virtual const char* __str__() const;
		virtual const char* __toString() const;
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
        virtual MegaNode *getPublicNode() const;
        virtual MegaNode *getPublicMegaNode() const;
        virtual int getParamType() const;
        virtual const char *getText() const;
        virtual long long getNumber() const;
        virtual bool getFlag() const;
        virtual long long getTransferredBytes() const;
        virtual long long getTotalBytes() const;
		virtual MegaRequestListener *getListener() const;
		virtual MegaAccountDetails *getMegaAccountDetails() const;
        virtual int getTransferTag() const;
        virtual int getNumDetails() const;
        virtual int getTag() const;
        virtual MegaPricing *getPricing() const;
	    AccountDetails * getAccountDetails() const;

	    // ATTR
	    virtual void getUserAttributeMap(TLV**, unsigned int*) const;

	    // ATTR
	    virtual ValueMap getAttributeMap() const;
	    virtual const char *getAttributeName() const;
	    virtual int getAttributeVis() const;
#ifdef ENABLE_SYNC
        void setSyncListener(MegaSyncListener *syncListener);
        MegaSyncListener *getSyncListener() const;
#endif

    protected:
        AccountDetails *accountDetails;
        MegaPricingPrivate *megaPricing;
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
        long long number;
		int access;
		const char* file;
		int attrType;
        bool flag;
        long long totalBytes;
        long long transferredBytes;
		MegaRequestListener *listener;

		// ATTR
		const char *attributeName;
		ValueMap userAttributes;
		int priv;
#ifdef ENABLE_SYNC
        MegaSyncListener *syncListener;
#endif
        int transfer;
		int numDetails;
        MegaNode* publicNode;
		int numRetry;
        int tag;
};

class MegaAccountBalancePrivate : public MegaAccountBalance
{
public:
    static MegaAccountBalance *fromAccountBalance(const AccountBalance *balance);
    virtual ~MegaAccountBalancePrivate() ;
    virtual MegaAccountBalance* copy();

    virtual double getAmount() const;
    virtual const char* getCurrency() const;

protected:
    MegaAccountBalancePrivate(const AccountBalance *balance);
    AccountBalance balance;
};

class MegaAccountSessionPrivate : public MegaAccountSession
{
public:
    static MegaAccountSession *fromAccountSession(const AccountSession *session);
    virtual ~MegaAccountSessionPrivate() ;
    virtual MegaAccountSession* copy();

    virtual int64_t getCreationTimestamp() const;
    virtual int64_t getMostRecentUsage() const;
    virtual const char *getUserAgent() const;
    virtual const char *getIP() const;
    virtual const char *getCountry() const;
    virtual bool isCurrent() const;
    virtual bool isAlive() const;
    virtual MegaHandle getHandle() const;

private:
    MegaAccountSessionPrivate(const AccountSession *session);
    AccountSession session;
};

class MegaAccountPurchasePrivate : public MegaAccountPurchase
{
public:   
    static MegaAccountPurchase *fromAccountPurchase(const AccountPurchase *purchase);
    virtual ~MegaAccountPurchasePrivate() ;
    virtual MegaAccountPurchase* copy();

    virtual int64_t getTimestamp() const;
    virtual const char *getHandle() const;
    virtual const char *getCurrency() const;
    virtual double getAmount() const;
    virtual int getMethod() const;

private:
    MegaAccountPurchasePrivate(const AccountPurchase *purchase);
    AccountPurchase purchase;
};

class MegaAccountTransactionPrivate : public MegaAccountTransaction
{
public:
    static MegaAccountTransaction *fromAccountTransaction(const AccountTransaction *transaction);
    virtual ~MegaAccountTransactionPrivate() ;
    virtual MegaAccountTransaction* copy();

    virtual int64_t getTimestamp() const;
    virtual const char *getHandle() const;
    virtual const char *getCurrency() const;
    virtual double getAmount() const;

private:
    MegaAccountTransactionPrivate(const AccountTransaction *transaction);
    AccountTransaction transaction;
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

        virtual int getNumUsageItems();
		virtual long long getStorageUsed(MegaHandle handle);
		virtual long long getNumFiles(MegaHandle handle);
		virtual long long getNumFolders(MegaHandle handle);
	
		virtual MegaAccountDetails* copy();

        virtual int getNumBalances() const;
        virtual MegaAccountBalance* getBalance(int i) const;

        virtual int getNumSessions() const;
        virtual MegaAccountSession* getSession(int i) const;

        virtual int getNumPurchases() const;
        virtual MegaAccountPurchase* getPurchase(int i) const;

        virtual int getNumTransactions() const;
        virtual MegaAccountTransaction* getTransaction(int i) const;

	private:
		MegaAccountDetailsPrivate(AccountDetails *details);
        AccountDetails details;
};

class MegaPricingPrivate : public MegaPricing
{
public:
    virtual ~MegaPricingPrivate();
    virtual int getNumProducts();
    virtual MegaHandle getHandle(int productIndex);
    virtual int getProLevel(int productIndex);
    virtual int getGBStorage(int productIndex);
    virtual int getGBTransfer(int productIndex);
    virtual int getMonths(int productIndex);
    virtual int getAmount(int productIndex);
    virtual const char* getCurrency(int productIndex);
    virtual const char* getDescription(int productIndex);
    virtual const char* getIosID(int productIndex);
    virtual const char* getAndroidID(int productIndex);
    virtual MegaPricing *copy();

    void addProduct(handle product, int proLevel, int gbStorage, int gbTransfer,
                    int months, int amount, const char *currency, const char *description, const char *iosid, const char *androidid);
private:
    vector<handle> handles;
    vector<int> proLevel;
    vector<int> gbStorage;
    vector<int> gbTransfer;
    vector<int> months;
    vector<int> amount;
    vector<const char *> currency;
    vector<const char *> description;
    vector<const char *> iosId;
    vector<const char *> androidId;
};

class MegaNodeListPrivate : public MegaNodeList
{
	public:
        MegaNodeListPrivate();
        MegaNodeListPrivate(Node** newlist, int size);
        virtual ~MegaNodeListPrivate();
		virtual MegaNodeList *copy();
		virtual MegaNode* get(int i);
		virtual int size();
	
	protected:
		MegaNodeListPrivate(MegaNodeListPrivate& nodeList);
		MegaNode** list;
		int s;
};

class MegaUserListPrivate : public MegaUserList
{
	public:
        MegaUserListPrivate();
        MegaUserListPrivate(User** newlist, int size);
        virtual ~MegaUserListPrivate();
		virtual MegaUserList *copy();
		virtual MegaUser* get(int i);
		virtual int size();
	
	protected:
		MegaUserListPrivate(MegaUserListPrivate &userList);
		MegaUser** list;
		int s;
};

class MegaShareListPrivate : public MegaShareList
{
	public:
        MegaShareListPrivate();
        MegaShareListPrivate(Share** newlist, MegaHandle *MegaHandlelist, int size);
        virtual ~MegaShareListPrivate();
		virtual MegaShare* get(int i);
		virtual int size();
		
	protected:
		MegaShare** list;
		int s;
};

class MegaTransferListPrivate : public MegaTransferList
{
	public:
        MegaTransferListPrivate();
        MegaTransferListPrivate(MegaTransfer** newlist, int size);
        virtual ~MegaTransferListPrivate();
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
    MegaFilePut(MegaClient *client, string* clocalname, string *filename, handle ch, const char* ctargetuser, int64_t mtime = -1);
    ~MegaFilePut() {}

protected:
    int64_t customMtime;
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

class OutShareProcessor : public TreeProcessor
{
    public:
        OutShareProcessor();
        virtual bool processNode(Node* node);
        virtual ~OutShareProcessor() {}
        vector<Share *> &getShares();
        vector<handle> &getHandles();

    protected:
        vector<Share *> shares;
        vector<handle> handles;
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
#ifdef ENABLE_SYNC
        void removeListener(MegaSyncListener *listener);
#endif
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
#ifdef ENABLE_SYNC
        void addSyncListener(MegaSyncListener *listener);
        void removeSyncListener(MegaSyncListener *listener);
#endif
        void removeListener(MegaListener* listener);
        void removeRequestListener(MegaRequestListener* listener);
        void removeTransferListener(MegaTransferListener* listener);
        void removeGlobalListener(MegaGlobalListener* listener);

        MegaRequest *getCurrentRequest();
        MegaTransfer *getCurrentTransfer();
        MegaError *getCurrentError();
        MegaNodeList *getCurrentNodes();
        MegaUserList *getCurrentUsers();

        //Utils
        const char* getBase64PwKey(const char *password);
        const char* getStringHash(const char* base64pwkey, const char* inBuf);
        static MegaHandle base32ToHandle(const char* base32Handle);
        static handle base64ToHandle(const char* base64Handle);
        static const char* handleToBase64(MegaHandle handle);
        static const char* userHandleToBase64(MegaHandle handle);
        static const char* ebcEncryptKey(const char* encryptionKey, const char* plainKey);
        void retryPendingConnections(bool disconnect = false, bool includexfers = false, MegaRequestListener* listener = NULL);
        static void addEntropy(char* data, unsigned int size);

        //API requests
        void login(const char* email, const char* password, MegaRequestListener *listener = NULL);
        const char *dumpSession();
        const char *dumpXMPPSession();
        void fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener = NULL);
        void fastLogin(const char* session, MegaRequestListener *listener = NULL);
        void killSession(MegaHandle sessionHandle, MegaRequestListener *listener = NULL);
        void getUserData(MegaRequestListener *listener = NULL);
        void getUserData(MegaUser *user, MegaRequestListener *listener = NULL);
        void getUserData(const char *user, MegaRequestListener *listener = NULL);

        // ATTR
        void putGenericUserAttribute(const char *user, const char *attrName,
                       TLV *tlvArray, unsigned int tlvLen,
                       int priv, int nonhistoric,
                       MegaRequestListener *listener = NULL);

        void getGenericUserAttribute(const char *user, const char *an,
               MegaRequestListener *listener = NULL);

        void getOwnStaticKeys(MegaRequestListener *listener = NULL);

        void getPublicStaticKey(const char *user, MegaRequestListener *listener = NULL);


        /////////////////

        void getAccountDetails(bool storage, bool transfer, bool pro, bool sessions, bool purchases, bool transactions, MegaRequestListener *listener = NULL);
        void createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener = NULL);
        void fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener = NULL);
        void querySignupLink(const char* link, MegaRequestListener *listener = NULL);
        void confirmAccount(const char* link, const char *password, MegaRequestListener *listener = NULL);
        void fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener = NULL);
        void setProxySettings(MegaProxy *proxySettings);
        MegaProxy *getAutoProxySettings();
        int isLoggedIn();
        const char* getMyEmail();
        static void setLogLevel(int logLevel);
        static void setLoggerClass(MegaLogger *megaLogger);
        static void log(int logLevel, const char* message, const char *filename = NULL, int line = -1);

        void createFolder(const char* name, MegaNode *parent, MegaRequestListener *listener = NULL);
        void moveNode(MegaNode* node, MegaNode* newParent, MegaRequestListener *listener = NULL);
        void copyNode(MegaNode* node, MegaNode *newParent, MegaRequestListener *listener = NULL);
        void copyNode(MegaNode* node, MegaNode *newParent, const char* newName, MegaRequestListener *listener = NULL);
        void renameNode(MegaNode* node, const char* newName, MegaRequestListener *listener = NULL);
        void remove(MegaNode* node, MegaRequestListener *listener = NULL);
        void sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener = NULL);
        void sendFileToUser(MegaNode *node, const char* email, MegaRequestListener *listener = NULL);
        void share(MegaNode *node, MegaUser* user, int level, MegaRequestListener *listener = NULL);
        void share(MegaNode* node, const char* email, int level, MegaRequestListener *listener = NULL);
        void loginToFolder(const char* megaFolderLink, MegaRequestListener *listener = NULL);
        void importFileLink(const char* megaFileLink, MegaNode* parent, MegaRequestListener *listener = NULL);
        void getPublicNode(const char* megaFileLink, MegaRequestListener *listener = NULL);
        void getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener = NULL);
        void setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetPreview(MegaNode* node, MegaRequestListener *listener = NULL);
        void setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void setAvatar(const char *dstFilePath, MegaRequestListener *listener = NULL);
		void setUserAttribute(int type, const char* value, MegaRequestListener *listener = NULL);
        void exportNode(MegaNode *node, MegaRequestListener *listener = NULL);
        void disableExport(MegaNode *node, MegaRequestListener *listener = NULL);
        void fetchNodes(MegaRequestListener *listener = NULL);
        void getPricing(MegaRequestListener *listener = NULL);
        void getPaymentId(handle productHandle, MegaRequestListener *listener = NULL);
        void submitPurchaseReceipt(const char* receipt, MegaRequestListener *listener = NULL);

        const char *exportMasterKey();

        void changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener = NULL);
        void addContact(const char* email, MegaRequestListener* listener=NULL);
        void removeContact(MegaUser *user, MegaRequestListener* listener=NULL);
        void logout(MegaRequestListener *listener = NULL);
        void localLogout(MegaRequestListener *listener = NULL);
        void submitFeedback(int rating, const char *comment, MegaRequestListener *listener = NULL);
        void reportEvent(int event, const char *details = NULL, MegaRequestListener *listener = NULL);

        //Transfers
        void startUpload(const char* localPath, MegaNode *parent, MegaTransferListener *listener=NULL);
        void startUpload(const char* localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener=NULL);
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener = NULL);
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName,  int64_t mtime, MegaTransferListener *listener = NULL);
        void startDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void startStreaming(MegaNode* node, m_off_t startPos, m_off_t size, MegaTransferListener *listener);
        void startPublicDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void cancelTransfer(MegaTransfer *transfer, MegaRequestListener *listener=NULL);
        void cancelTransferByTag(int transferTag, MegaRequestListener *listener = NULL);
        void cancelTransfers(int direction, MegaRequestListener *listener=NULL);
        void pauseTransfers(bool pause, int direction, MegaRequestListener* listener=NULL);
        bool areTansfersPaused(int direction);
        void setUploadLimit(int bpslimit);
        MegaTransferList *getTransfers();
        MegaTransfer* getTransferByTag(int transferTag);
        MegaTransferList *getTransfers(int type);

#ifdef ENABLE_SYNC
        //Sync
        int syncPathState(string *path);
        MegaNode *getSyncedNode(string *path);
        void syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRequestListener* listener = NULL);
        void resumeSync(const char *localFolder, long long localfp, MegaNode *megaFolder, MegaRequestListener *listener = NULL);
        void removeSync(handle nodehandle, MegaRequestListener *listener=NULL);
        void disableSync(handle nodehandle, MegaRequestListener *listener=NULL);
        int getNumActiveSyncs();
        void stopSyncs(MegaRequestListener *listener=NULL);
        bool isSynced(MegaNode *n);
        void setExcludedNames(vector<string> *excludedNames);
        void setExclusionLowerSizeLimit(long long limit);
        void setExclusionUpperSizeLimit(long long limit);
        bool moveToLocalDebris(const char *path);
        string getLocalPath(MegaNode *node);
        bool is_syncable(const char* name);
        bool is_syncable(long long size);
        bool isIndexing();
#endif
        void update();
        bool isWaiting();

        //Statistics
        int getNumPendingUploads();
        int getNumPendingDownloads();
        int getTotalUploads();
        int getTotalDownloads();
        void resetTotalDownloads();
        void resetTotalUploads();
        void updateStats();
        long long getTotalDownloadedBytes();
        long long getTotalUploadedBytes();

        //Filesystem
		int getNumChildren(MegaNode* parent);
		int getNumChildFiles(MegaNode* parent);
		int getNumChildFolders(MegaNode* parent);
        MegaNodeList* getChildren(MegaNode *parent, int order=1);
        int getIndex(MegaNode* node, int order=1);
        MegaNode *getChildNode(MegaNode *parent, const char* name);
        MegaNode *getParentNode(MegaNode *node);
        const char* getNodePath(MegaNode *node);
        MegaNode *getNodeByPath(const char *path, MegaNode *n = NULL);
        MegaNode *getNodeByHandle(handle handler);
        MegaUserList* getContacts();
        MegaUser* getContact(const char* email);
        MegaNodeList *getInShares(MegaUser* user);
        MegaNodeList *getInShares();
        bool isShared(MegaNode *node);
        MegaShareList *getOutShares();
        MegaShareList *getOutShares(MegaNode *node);
        int getAccess(MegaNode* node);
        long long getSize(MegaNode *node);
        static void removeRecursively(const char *path);

        //Fingerprint
        const char* getFingerprint(const char *filePath);
        const char *getFingerprint(MegaNode *node);
        MegaNode *getNodeByFingerprint(const char* fingerprint);
        MegaNode *getNodeByFingerprint(const char *fingerprint, MegaNode* parent);

        bool hasFingerprint(const char* fingerprint);

        //Permissions
        MegaError checkAccess(MegaNode* node, int level);
        MegaError checkMove(MegaNode* node, MegaNode* target);

        MegaNode *getRootNode();
        MegaNode* getInboxNode();
        MegaNode *getRubbishNode();
        MegaNodeList* search(MegaNode* node, const char* searchString, bool recursive = 1);
        bool processMegaTree(MegaNode* node, MegaTreeProcessor* processor, bool recursive = 1);
        void loadBalancing(const char* service, MegaRequestListener *listener = NULL);

        const char *getVersion();
        const char *getUserAgent();

        void changeApiUrl(const char *apiURL, bool disablepkp = false);

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

        const char* nameToLocal(const char *name);
        const char* localToName(const char*localName);

protected:
        void init(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath = NULL, const char *userAgent = NULL, int fseventsfd = -1);

        static void *threadEntryPoint(void *param);
        static ExternalLogger *externalLogger;

        void fireOnRequestStart(MegaRequestPrivate *request);
        void fireOnRequestFinish(MegaRequestPrivate *request, MegaError e);
        void fireOnRequestUpdate(MegaRequestPrivate *request);
        void fireOnRequestTemporaryError(MegaRequestPrivate *request, MegaError e);
        void fireOnTransferStart(MegaTransferPrivate *transfer);
        void fireOnTransferFinish(MegaTransferPrivate *transfer, MegaError e);
        void fireOnTransferUpdate(MegaTransferPrivate *transfer);
        bool fireOnTransferData(MegaTransferPrivate *transfer);
        void fireOnTransferTemporaryError(MegaTransferPrivate *transfer, MegaError e);
        void fireOnUsersUpdate(MegaUserList *users);
        void fireOnNodesUpdate(MegaNodeList *nodes);
        void fireOnReloadNeeded();

#ifdef ENABLE_SYNC
        void fireOnGlobalSyncStateChanged();
        void fireOnSyncStateChanged(MegaSyncPrivate *sync);
        void fireOnSyncEvent(MegaSyncPrivate *sync, MegaSyncEvent *event);
        void fireOnFileSyncStateChanged(MegaSyncPrivate *sync, const char *filePath, int newState);
#endif

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

#ifdef ENABLE_SYNC
        map<int, MegaSyncPrivate *> syncMap;
#endif

        int pendingUploads;
        int pendingDownloads;
        int totalUploads;
        int totalDownloads;
        long long totalDownloadedBytes;
        long long totalUploadedBytes;
        set<MegaRequestListener *> requestListeners;
        set<MegaTransferListener *> transferListeners;

#ifdef ENABLE_SYNC
        set<MegaSyncListener *> syncListeners;
#endif

        set<MegaGlobalListener *> globalListeners;
        set<MegaListener *> listeners;
        bool waiting;
        bool waitingRequest;
        vector<string> excludedNames;
        long long syncLowerSizeLimit;
        long long syncUpperSizeLimit;
        MegaMutex sdkMutex;
        MegaTransferPrivate *currentTransfer;
        MegaRequestPrivate *activeRequest;
        MegaTransferPrivate *activeTransfer;
        MegaError *activeError;
        MegaNodeList *activeNodes;
        MegaUserList *activeUsers;

        int threadExit;
        dstime pausetime;
        void loop();

        int maxRetries;

        // a request-level error occurred
        virtual void request_error(error);
        virtual void request_response_progress(m_off_t, m_off_t);

        // login result
        virtual void login_result(error);
        virtual void logout_result(error);
        virtual void userdata_result(string*, string*, string*, handle, error);
        virtual void pubkey_result(User *, error e);

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
        virtual void enumeratequotaitems_result(handle product, unsigned prolevel, unsigned gbstorage, unsigned gbtransfer,
                                                unsigned months, unsigned amount, const char* currency, const char* description, const char* iosid, const char* androidid);
        virtual void enumeratequotaitems_result(error e);
        virtual void additem_result(error);
        virtual void submitpurchasereceipt_result(error);

        virtual void checkfile_result(handle h, error e);
        virtual void checkfile_result(handle h, error e, byte* filekey, m_off_t size, m_time_t ts, m_time_t tm, string* filename, string* fingerprint, string* fileattrstring);

        // user invites/attributes
        virtual void invite_result(error);
        virtual void putua_result(error);
        virtual void getua_result(error);
        virtual void getua_result(byte*, unsigned);

        // ATTR

        virtual void putguattr_result(error);
        virtual void getguattr_result(ValueMap, error);

        virtual void verifyrsasig_result(error);

        virtual void verifykeyfp_result(error);

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
        virtual void loadbalancing_result(string*, error);
        virtual void sessions_killed(handle sessionid, error e);

#ifdef ENABLE_SYNC
        // sync status updates and events
        virtual void syncupdate_state(Sync*, syncstate_t);
        virtual void syncupdate_scanning(bool scanning);
        virtual void syncupdate_local_folder_addition(Sync* sync, LocalNode *localNode, const char *path);
        virtual void syncupdate_local_folder_deletion(Sync* sync, LocalNode *localNode);
        virtual void syncupdate_local_file_addition(Sync* sync, LocalNode* localNode, const char *path);
        virtual void syncupdate_local_file_deletion(Sync* sync, LocalNode* localNode);
        virtual void syncupdate_local_file_change(Sync* sync, LocalNode* localNode, const char *path);
        virtual void syncupdate_local_move(Sync* sync, LocalNode* localNode, const char* path);
        virtual void syncupdate_get(Sync* sync, Node *node, const char* path);
        virtual void syncupdate_put(Sync* sync, LocalNode *localNode, const char*);
        virtual void syncupdate_remote_file_addition(Sync *sync, Node* n);
        virtual void syncupdate_remote_file_deletion(Sync *sync, Node* n);
        virtual void syncupdate_remote_folder_addition(Sync *sync, Node* n);
        virtual void syncupdate_remote_folder_deletion(Sync* sync, Node* n);
        virtual void syncupdate_remote_copy(Sync*, const char*);
        virtual void syncupdate_remote_move(Sync *sync, Node *n, Node* prevparent);
        virtual void syncupdate_remote_rename(Sync*sync, Node* n, const char* prevname);
        virtual void syncupdate_treestate(LocalNode*);
        virtual bool sync_syncable(Node*);
        virtual bool sync_syncable(const char*name, string*, string*);
        virtual void syncupdate_local_lockretry(bool);
#endif

        // suggest reload due to possible race condition with other clients
        virtual void reload(const char*);

        // wipe all users, nodes and shares
        virtual void clearing();

        // failed request retry notification
        virtual void notify_retry(dstime);

        void sendPendingRequests();
        void sendPendingTransfers();
        char *stringToArray(string &buffer);

        //Internal
        Node* getNodeByFingerprintInternal(const char *fingerprint);
        Node *getNodeByFingerprintInternal(const char *fingerprint, Node *parent);

        bool processTree(Node* node, TreeProcessor* processor, bool recursive = 1);
        MegaNodeList* search(Node* node, const char* searchString, bool recursive = 1);
        void getNodeAttribute(MegaNode* node, int type, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetNodeAttribute(MegaNode *node, int type, MegaRequestListener *listener = NULL);
        void setNodeAttribute(MegaNode* node, int type, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void getUserAttribute(MegaUser* user, int type, const char *dstFilePath, MegaRequestListener *listener = NULL);
        void setUserAttr(int type, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void startDownload(MegaNode *node, const char* target, long startPos, long endPos, MegaTransferListener *listener);
};

class MegaHashSignatureImpl
{
	public:
		MegaHashSignatureImpl(const char *base64Key);
		~MegaHashSignatureImpl();
		void init();
		void add(const char *data, unsigned size);
        bool checkSignature(const char *base64Signature);

	protected:    
		HashSignature *hashSignature;
		AsymmCipher* asymmCypher;
};

}

#endif //MEGAAPI_IMPL_H
