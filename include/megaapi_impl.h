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

#include "mega.h"
#include "mega/thread/qtthread.h"
#include "mega/thread/posixthread.h"
#include "mega/thread/win32thread.h"
#include "mega/gfx/external.h"
#include "mega/gfx/qt.h"
#include "mega/thread/cppthread.h"
#include "mega/proxy.h"
#include "megaapi.h"

#ifdef HAVE_LIBUV
#include "uv.h"
#include "mega/mega_http_parser.h"
#endif

#ifndef _WIN32
    #if (!defined(USE_CURL_PUBLIC_KEY_PINNING)) || defined(WINDOWS_PHONE)
    #include <openssl/ssl.h>
    #include <openssl/rand.h>
    #endif
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

#ifdef USE_QT
typedef QtThread MegaThread;
typedef QtMutex MegaMutex;
typedef QtSemaphore MegaSemaphore;
#elif USE_PTHREAD
typedef PosixThread MegaThread;
typedef PosixMutex MegaMutex;
typedef PosixSemaphore MegaSemaphore;
#elif defined(_WIN32) && !defined(WINDOWS_PHONE)
typedef Win32Thread MegaThread;
typedef Win32Mutex MegaMutex;
typedef Win32Semaphore MegaSemaphore;
#else
typedef CppThread MegaThread;
typedef CppMutex MegaMutex;
typedef CppSemaphore MegaSemaphore;
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
    #ifdef USE_CURL
    class MegaHttpIO : public CurlHttpIO {};
    #else
    class MegaHttpIO : public WinHttpIO {};
    #endif

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

#ifdef HAVE_LIBUV
class MegaHTTPServer;
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

class MegaTransferPrivate;
class MegaFolderUploadController : public MegaRequestListener, public MegaTransferListener
{
public:
    MegaFolderUploadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer);
    void start();

protected:
    void onFolderAvailable(MegaHandle handle);
    void checkCompletion();

    std::list<std::string> pendingFolders;
    std::list<MegaTransferPrivate *> pendingSkippedTransfers;

    MegaApiImpl *megaApi;
    MegaClient *client;
    MegaTransferPrivate *transfer;
    MegaTransferListener *listener;
    int recursive;
    int tag;
    int pendingTransfers;

public:
    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError *e);
    virtual void onTransferStart(MegaApi *api, MegaTransfer *transfer);
    virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);
    virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError *e);
};

class MegaFolderDownloadController : public MegaTransferListener
{
public:
    MegaFolderDownloadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer);
    void start();

protected:
    void downloadFolderNode(Node *node, string *path);
    void checkCompletion();

    MegaApiImpl *megaApi;
    MegaClient *client;
    MegaTransferPrivate *transfer;
    MegaTransferListener *listener;
    int recursive;
    int tag;
    int pendingTransfers;

public:
    virtual void onTransferStart(MegaApi *, MegaTransfer *t);
    virtual void onTransferUpdate(MegaApi *, MegaTransfer *t);
    virtual void onTransferFinish(MegaApi*, MegaTransfer *t, MegaError *e);
};

class MegaNodePrivate : public MegaNode, public Cachable
{
    public:
        MegaNodePrivate(const char *name, int type, int64_t size, int64_t ctime, int64_t mtime,
                        MegaHandle nodeMegaHandle, std::string *nodekey, std::string *attrstring,
                        const char *fingerprint, MegaHandle parentHandle = INVALID_HANDLE,
                        const char *privateauth = NULL, const char *publicauth = NULL, bool isPublic = true,
                        bool isForeign = false);

        MegaNodePrivate(MegaNode *node);
        virtual ~MegaNodePrivate();
        virtual int getType();
        virtual const char* getName();
        virtual const char* getFingerprint();
        virtual bool hasCustomAttrs();
        MegaStringList *getCustomAttrNames();
        virtual const char *getCustomAttr(const char* attrName);
        virtual int getDuration();
        virtual double getLatitude();
        virtual double getLongitude();
        virtual char *getBase64Handle();
        virtual int64_t getSize();
        virtual int64_t getCreationTime();
        virtual int64_t getModificationTime();
        virtual MegaHandle getHandle();
        virtual MegaHandle getParentHandle();
        virtual std::string* getNodeKey();
        virtual char *getBase64Key();
        virtual std::string* getAttrString();
        virtual int getTag();
        virtual int64_t getExpirationTime();
        virtual MegaHandle getPublicHandle();
        virtual MegaNode* getPublicNode();
        virtual char *getPublicLink();
        virtual bool isFile();
        virtual bool isFolder();
        virtual bool isRemoved();
        virtual bool hasChanged(int changeType);
        virtual int getChanges();
        virtual bool hasThumbnail();
        virtual bool hasPreview();
        virtual bool isPublic();
        virtual bool isExported();
        virtual bool isExpired();
        virtual bool isTakenDown();
        virtual bool isForeign();
        virtual std::string* getPrivateAuth();
        virtual void setPrivateAuth(const char *privateAuth);
        void setPublicAuth(const char *publicAuth);
        void setForeign(bool foreign);
        virtual std::string* getPublicAuth();
        virtual bool isShared();
        virtual bool isOutShare();
        virtual bool isInShare();
        std::string* getSharekey();


#ifdef ENABLE_SYNC
        virtual bool isSyncDeleted();
        virtual std::string getLocalPath();
#endif

        static MegaNode *fromNode(Node *node);
        virtual MegaNode *copy();

        virtual bool serialize(string*);
        static MegaNodePrivate* unserialize(string*);

    protected:
        MegaNodePrivate(Node *node);
        int type;
        const char *name;
        const char *fingerprint;
        attr_map *customAttrs;
        int64_t size;
        int64_t ctime;
        int64_t mtime;
        MegaHandle nodehandle;
        MegaHandle parenthandle;
        std::string nodekey;
        std::string attrstring;
        std::string privateAuth;
        std::string publicAuth;
        int tag;
        int changed;
        struct {
            bool thumbnailAvailable : 1;
            bool previewAvailable : 1;
            bool isPublicNode : 1;
            bool outShares : 1;
            bool inShare : 1;
            bool foreign : 1;
        };
        PublicLink *plink;
        std::string *sharekey;   // for plinks of folders
        int duration;
        double latitude;
        double longitude;

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
        virtual MegaHandle getHandle();
        virtual int getVisibility();
        virtual int64_t getTimestamp();
        virtual bool hasChanged(int changeType);
        virtual int getChanges();
        virtual int isOwnChange();

	protected:
		const char *email;
        MegaHandle handle;
        int visibility;
        int64_t ctime;
        int changed;
        int tag;
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

class MegaTransferPrivate : public MegaTransfer, public Cachable
{
	public:
		MegaTransferPrivate(int type, MegaTransferListener *listener = NULL);
        MegaTransferPrivate(const MegaTransferPrivate *transfer);
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
        void setSourceFileTemporary(bool temporary);
        void setStreamingTransfer(bool streamingTransfer);
        void setLastBytes(char *lastBytes);
        void setLastError(MegaError e);
        void setFolderTransferTag(int tag);
        void setListener(MegaTransferListener *listener);

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
        virtual bool isSourceFileTemporary() const;
        virtual char *getLastBytes() const;
        virtual MegaError getLastError() const;
        virtual bool isFolderTransfer() const;
        virtual int getFolderTransferTag() const;
        virtual void setAppData(const char *data);
        virtual const char* getAppData() const;

        virtual bool serialize(string*);
        static MegaTransferPrivate* unserialize(string*);

	protected:		
		int type;
		int tag;

        struct
        {
            bool syncTransfer : 1;
            bool streamingTransfer : 1;
            bool temporarySourceFile : 1;
        };

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
        MegaError lastError;
        int folderTransferTag;
        const char* appData;
};

class MegaContactRequestPrivate : public MegaContactRequest
{
public:
    MegaContactRequestPrivate(PendingContactRequest *request);
    MegaContactRequestPrivate(const MegaContactRequest *request);
    virtual ~MegaContactRequestPrivate();

    static MegaContactRequest *fromContactRequest(PendingContactRequest *request);
    virtual MegaContactRequest *copy() const;

    virtual MegaHandle getHandle() const;
    virtual char* getSourceEmail() const;
    virtual char* getSourceMessage() const;
    virtual char* getTargetEmail() const;
    virtual int64_t getCreationTime() const;
    virtual int64_t getModificationTime() const;
    virtual int getStatus() const;
    virtual bool isOutgoing() const;

protected:
    MegaHandle handle;
    char* sourceEmail;
    char* sourceMessage;
    char* targetEmail;
    int64_t creationTime;
    int64_t modificationTime;
    int status;
    bool outgoing;
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
    MegaSyncPrivate(MegaSyncPrivate *sync);

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
        MegaRequestPrivate(MegaRequestPrivate *request);
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

        void setProxy(Proxy *proxy);
        Proxy *getProxy();

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
#ifdef ENABLE_CHAT
        virtual MegaTextChatPeerList *getMegaTextChatPeerList() const;
        void setMegaTextChatPeerList(MegaTextChatPeerList *chatPeers);
        virtual MegaTextChatList *getMegaTextChatList() const;
        void setMegaTextChatList(MegaTextChatList *chatList);
#endif
        virtual MegaStringMap *getMegaStringMap() const;
        void setMegaStringMap(const MegaStringMap *);

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
#ifdef ENABLE_SYNC
        MegaSyncListener *syncListener;
#endif
        int transfer;
		int numDetails;
        MegaNode* publicNode;
		int numRetry;
        int tag;
        Proxy *proxy;

#ifdef ENABLE_CHAT
        MegaTextChatPeerList *chatPeerList;
        MegaTextChatList *chatList;
#endif
        MegaStringMap *stringMap;
};

class MegaAccountBalancePrivate : public MegaAccountBalance
{
public:
    static MegaAccountBalance *fromAccountBalance(const AccountBalance *balance);
    virtual ~MegaAccountBalancePrivate() ;
    virtual MegaAccountBalance* copy();

    virtual double getAmount() const;
    virtual char* getCurrency() const;

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
    virtual char *getUserAgent() const;
    virtual char *getIP() const;
    virtual char *getCountry() const;
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
    virtual char *getHandle() const;
    virtual char *getCurrency() const;
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
    virtual char *getHandle() const;
    virtual char *getCurrency() const;
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
        virtual int64_t getProExpiration();
        virtual int getSubscriptionStatus();
        virtual int64_t getSubscriptionRenewTime();
        virtual char* getSubscriptionMethod();
        virtual char* getSubscriptionCycle();

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

        virtual int getTemporalBandwidthInterval();
        virtual long long getTemporalBandwidth();
        virtual bool isTemporalBandwidthValid();

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

#ifdef ENABLE_CHAT
class MegaTextChatPeerListPrivate : public MegaTextChatPeerList
{
public:
    MegaTextChatPeerListPrivate();
    MegaTextChatPeerListPrivate(userpriv_vector *);

    virtual ~MegaTextChatPeerListPrivate();
    virtual MegaTextChatPeerList *copy() const;
    virtual void addPeer(MegaHandle h, int priv);
    virtual MegaHandle getPeerHandle(int i) const;
    virtual int getPeerPrivilege(int i) const;
    virtual int size() const;

    // returns the list of user-privilege (this object keeps the ownership)
    const userpriv_vector * getList() const;

private:
    userpriv_vector list;
};

class MegaTextChatPrivate : public MegaTextChat
{
public:
    MegaTextChatPrivate(const MegaTextChat *);
    MegaTextChatPrivate(handle id, int priv, string url, int shard, const MegaTextChatPeerList *peers, bool group, handle ou);

    virtual ~MegaTextChatPrivate();
    virtual MegaHandle getHandle() const;
    virtual int getOwnPrivilege() const;
    virtual const char *getUrl() const;
    virtual void setUrl(const char *);
    virtual int getShard() const;
    virtual const MegaTextChatPeerList *getPeerList() const;
    virtual bool isGroup() const;
    virtual MegaHandle getOriginatingUser() const;

private:
    handle id;
    int priv;
    string url;
    int shard;
    MegaTextChatPeerList *peers;
    bool group;
    handle ou;
};

class MegaTextChatListPrivate : public MegaTextChatList
{
public:
    MegaTextChatListPrivate();
    MegaTextChatListPrivate(textchat_vector *list);

    virtual ~MegaTextChatListPrivate();
    virtual MegaTextChatList *copy() const;
    virtual const MegaTextChat *get(unsigned int i) const;
    virtual MegaTextChat *get(unsigned int i);
    virtual int size() const;

    void addChat(MegaTextChatPrivate*);

private:
    MegaTextChatListPrivate(const MegaTextChatListPrivate*);
    vector<MegaTextChat*> list;
};

#endif

class MegaStringMapPrivate : public MegaStringMap
{
public:
    MegaStringMapPrivate();
    MegaStringMapPrivate(const string_map *map, bool toBase64 = false);
    virtual ~MegaStringMapPrivate();
    virtual MegaStringMap *copy() const;
    virtual const char *get(const char* key) const;
    virtual MegaStringList *getKeys() const;
    virtual void set(const char *key, const char *value);
    virtual int size() const;

protected:
    MegaStringMapPrivate(const MegaStringMapPrivate *megaStringMap);
    string_map strMap;
};


class MegaStringListPrivate : public MegaStringList
{
public:
    MegaStringListPrivate();
    MegaStringListPrivate(char **newlist, int size);
    virtual ~MegaStringListPrivate();
    virtual MegaStringList *copy();
    virtual const char* get(int i);
    virtual int size();


protected:
    MegaStringListPrivate(MegaStringListPrivate *stringList);
    const char** list;
    int s;
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
        MegaNodeListPrivate(MegaNodeListPrivate *nodeList);
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
        MegaUserListPrivate(MegaUserListPrivate *userList);
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

class MegaContactRequestListPrivate : public MegaContactRequestList
{
    public:
        MegaContactRequestListPrivate();
        MegaContactRequestListPrivate(PendingContactRequest ** newlist, int size);
        virtual ~MegaContactRequestListPrivate();
        virtual MegaContactRequestList *copy();
        virtual MegaContactRequest* get(int i);
        virtual int size();

    protected:
        MegaContactRequestListPrivate(MegaContactRequestListPrivate *requestList);
        MegaContactRequest** list;
        int s;
};

struct MegaFile : public File
{
    // app-internal sequence number for queue management
    int seqno;
    static int nextseqno;
    MegaFile();

    void setTransfer(MegaTransferPrivate *transfer);
    MegaTransferPrivate *getTransfer();
    virtual bool serialize(string*);

    static MegaFile* unserialize(string*);

protected:
    MegaTransferPrivate *megaTransfer;
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

    virtual bool serialize(string*);
    static MegaFileGet* unserialize(string*);

private:
    MegaFileGet() {}
};

struct MegaFilePut : public MegaFile
{
    void completed(Transfer* t, LocalNode*);
    void terminated();
    MegaFilePut(MegaClient *client, string* clocalname, string *filename, handle ch, const char* ctargetuser, int64_t mtime = -1, bool isSourceTemporary = false);
    ~MegaFilePut() {}

    virtual bool serialize(string*);
    static MegaFilePut* unserialize(string*);

protected:
    int64_t customMtime;

private:
    MegaFilePut() {}
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

class PendingOutShareProcessor : public TreeProcessor
{
    public:
        PendingOutShareProcessor();
        virtual bool processNode(Node* node);
        virtual ~PendingOutShareProcessor() {}
        vector<Share *> &getShares();
        vector<handle> &getHandles();

    protected:
        vector<Share *> shares;
        vector<handle> handles;
};

class PublicLinkProcessor : public TreeProcessor
{
    public:
        PublicLinkProcessor();
        virtual bool processNode(Node* node);
        virtual ~PublicLinkProcessor();
        vector<Node *> &getNodes();

    protected:
        vector<Node *> nodes;
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
        void removeListener(MegaTransferListener *listener);
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
        char *getBase64PwKey(const char *password);
        char *getStringHash(const char* base64pwkey, const char* inBuf);
        void getSessionTransferURL(const char *path, MegaRequestListener *listener);
        static MegaHandle base32ToHandle(const char* base32Handle);
        static handle base64ToHandle(const char* base64Handle);
        static handle base64ToUserHandle(const char* base64Handle);
        static char *handleToBase64(MegaHandle handle);
        static char *userHandleToBase64(MegaHandle handle);
        static const char* ebcEncryptKey(const char* encryptionKey, const char* plainKey);
        void retryPendingConnections(bool disconnect = false, bool includexfers = false, MegaRequestListener* listener = NULL);
        static void addEntropy(char* data, unsigned int size);
        static string userAttributeToString(int);
        static char userAttributeToScope(int);
        static void setStatsID(const char *id);

        //API requests
        void login(const char* email, const char* password, MegaRequestListener *listener = NULL);
        char *dumpSession();
        char *dumpXMPPSession();
        char *getAccountAuth();
        void setAccountAuth(const char* auth);

        void fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener = NULL);
        void fastLogin(const char* session, MegaRequestListener *listener = NULL);
        void killSession(MegaHandle sessionHandle, MegaRequestListener *listener = NULL);
        void getUserData(MegaRequestListener *listener = NULL);
        void getUserData(MegaUser *user, MegaRequestListener *listener = NULL);
        void getUserData(const char *user, MegaRequestListener *listener = NULL);
        void getAccountDetails(bool storage, bool transfer, bool pro, bool sessions, bool purchases, bool transactions, MegaRequestListener *listener = NULL);
        void createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener = NULL);
        void createAccount(const char* email, const char* password, const char* firstname, const char* lastname, MegaRequestListener *listener = NULL);
        void fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener = NULL);
        void querySignupLink(const char* link, MegaRequestListener *listener = NULL);
        void confirmAccount(const char* link, const char *password, MegaRequestListener *listener = NULL);
        void fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener = NULL);
        void resetPassword(const char *email, bool hasMasterKey, MegaRequestListener *listener = NULL);
        void queryRecoveryLink(const char *link, MegaRequestListener *listener = NULL);
        void confirmResetPasswordLink(const char *link, const char *newPwd, const char *masterKey = NULL, MegaRequestListener *listener = NULL);
        void cancelAccount(MegaRequestListener *listener = NULL);
        void confirmCancelAccount(const char *link, const char *pwd, MegaRequestListener *listener = NULL);
        void changeEmail(const char *email, MegaRequestListener *listener = NULL);
        void confirmChangeEmail(const char *link, const char *pwd, MegaRequestListener *listener = NULL);
        void setProxySettings(MegaProxy *proxySettings);
        MegaProxy *getAutoProxySettings();
        int isLoggedIn();
        char* getMyEmail();
        char* getMyUserHandle();
        MegaUser *getMyUser();
        char* getMyXMPPJid();
#ifdef ENABLE_CHAT
        char* getMyFingerprint();
#endif
        static void setLogLevel(int logLevel);
        static void setLoggerClass(MegaLogger *megaLogger);
        static void log(int logLevel, const char* message, const char *filename = NULL, int line = -1);

        void createFolder(const char* name, MegaNode *parent, MegaRequestListener *listener = NULL);
        void moveNode(MegaNode* node, MegaNode* newParent, MegaRequestListener *listener = NULL);
        void copyNode(MegaNode* node, MegaNode *newParent, MegaRequestListener *listener = NULL);
        void copyNode(MegaNode* node, MegaNode *newParent, const char* newName, MegaRequestListener *listener = NULL);
        void renameNode(MegaNode* node, const char* newName, MegaRequestListener *listener = NULL);
        void remove(MegaNode* node, MegaRequestListener *listener = NULL);
        void cleanRubbishBin(MegaRequestListener *listener = NULL);
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
        void getUserAvatar(const char *email_or_handle, const char *dstFilePath, MegaRequestListener *listener = NULL);
        char* getUserAvatarColor(MegaUser *user);
        char *getUserAvatarColor(const char *userhandle);
        void getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener = NULL);
        void getUserAttribute(const char* email_or_handle, int type, MegaRequestListener *listener = NULL);
        void setUserAttribute(int type, const char* value, MegaRequestListener *listener = NULL);
        void setUserAttribute(int type, const MegaStringMap* value, MegaRequestListener *listener = NULL);
        void setCustomNodeAttribute(MegaNode *node, const char *attrName, const char *value, MegaRequestListener *listener = NULL);
        void setNodeDuration(MegaNode *node, int secs, MegaRequestListener *listener = NULL);
        void setNodeCoordinates(MegaNode *node, double latitude, double longitude, MegaRequestListener *listener = NULL);
        void exportNode(MegaNode *node, int64_t expireTime, MegaRequestListener *listener = NULL);
        void disableExport(MegaNode *node, MegaRequestListener *listener = NULL);
        void fetchNodes(MegaRequestListener *listener = NULL);
        void getPricing(MegaRequestListener *listener = NULL);
        void getPaymentId(handle productHandle, MegaRequestListener *listener = NULL);
        void upgradeAccount(MegaHandle productHandle, int paymentMethod, MegaRequestListener *listener = NULL);
        void submitPurchaseReceipt(int gateway, const char* receipt, MegaRequestListener *listener = NULL);
        void creditCardStore(const char* address1, const char* address2, const char* city,
                             const char* province, const char* country, const char *postalcode,
                             const char* firstname, const char* lastname, const char* creditcard,
                             const char* expire_month, const char* expire_year, const char* cv2,
                             MegaRequestListener *listener = NULL);

        void creditCardQuerySubscriptions(MegaRequestListener *listener = NULL);
        void creditCardCancelSubscriptions(const char* reason, MegaRequestListener *listener = NULL);
        void getPaymentMethods(MegaRequestListener *listener = NULL);

        char *exportMasterKey();

        void changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener = NULL);
        void inviteContact(const char* email, const char* message, int action, MegaRequestListener* listener = NULL);
        void replyContactRequest(MegaContactRequest *request, int action, MegaRequestListener* listener = NULL);
        void respondContactRequest();

        void removeContact(MegaUser *user, MegaRequestListener* listener=NULL);
        void logout(MegaRequestListener *listener = NULL);
        void localLogout(MegaRequestListener *listener = NULL);
        void submitFeedback(int rating, const char *comment, MegaRequestListener *listener = NULL);
        void reportEvent(const char *details = NULL, MegaRequestListener *listener = NULL);
        void sendEvent(int eventType, const char* message, MegaRequestListener *listener = NULL);

        void useHttpsOnly(bool httpsOnly);
        bool usingHttpsOnly();

        //Transfers
        void startUpload(const char* localPath, MegaNode *parent, MegaTransferListener *listener=NULL);
        void startUpload(const char* localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener=NULL);
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener = NULL);
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName,  int64_t mtime, int folderTransferTag = 0, const char *appData = NULL, bool isSourceFileTemporary = false, MegaTransferListener *listener = NULL);
        void startDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void startDownload(MegaNode *node, const char* target, long startPos, long endPos, int folderTransferTag, const char *appData, MegaTransferListener *listener);
        void startStreaming(MegaNode* node, m_off_t startPos, m_off_t size, MegaTransferListener *listener);
        void startPublicDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);
        void cancelTransfer(MegaTransfer *transfer, MegaRequestListener *listener=NULL);
        void cancelTransferByTag(int transferTag, MegaRequestListener *listener = NULL);
        void cancelTransfers(int direction, MegaRequestListener *listener=NULL);
        void pauseTransfers(bool pause, int direction, MegaRequestListener* listener=NULL);
        void enableTransferResumption(const char* loggedOutId);
        void disableTransferResumption(const char* loggedOutId);
        bool areTransfersPaused(int direction);
        void setUploadLimit(int bpslimit);
        void setDownloadMethod(int method);
        void setUploadMethod(int method);
        int getDownloadMethod();
        int getUploadMethod();
        MegaTransferList *getTransfers();
        MegaTransferList *getStreamingTransfers();
        MegaTransfer* getTransferByTag(int transferTag);
        MegaTransferList *getTransfers(int type);
        MegaTransferList *getChildTransfers(int transferTag);

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
        long long getNumLocalNodes();
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
        long long getNumNodes();
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
        char *getNodePath(MegaNode *node);
        MegaNode *getNodeByPath(const char *path, MegaNode *n = NULL);
        MegaNode *getNodeByHandle(handle handler);
        MegaContactRequest *getContactRequestByHandle(MegaHandle handle);
        MegaUserList* getContacts();
        MegaUser* getContact(const char* email);
        MegaNodeList *getInShares(MegaUser* user);
        MegaNodeList *getInShares();
        MegaShareList *getInSharesList();
        bool isPendingShare(MegaNode *node);
        MegaShareList *getOutShares();
        MegaShareList *getOutShares(MegaNode *node);
        MegaShareList *getPendingOutShares();
        MegaShareList *getPendingOutShares(MegaNode *megaNode);
        MegaNodeList *getPublicLinks();
        MegaContactRequestList *getIncomingContactRequests();
        MegaContactRequestList *getOutgoingContactRequests();

        int getAccess(MegaNode* node);
        long long getSize(MegaNode *node);
        static void removeRecursively(const char *path);

        //Fingerprint
        char *getFingerprint(const char *filePath);
        char *getFingerprint(MegaNode *node);
        char *getFingerprint(MegaInputStream *inputStream, int64_t mtime);
        MegaNode *getNodeByFingerprint(const char* fingerprint);
        MegaNodeList *getNodesByFingerprint(const char* fingerprint);
        MegaNode *getExportableNodeByFingerprint(const char *fingerprint, const char *name = NULL);
        MegaNode *getNodeByFingerprint(const char *fingerprint, MegaNode* parent);
        bool hasFingerprint(const char* fingerprint);

        //CRC
        char *getCRC(const char *filePath);
        char *getCRCFromFingerprint(const char *fingerprint);
        char *getCRC(MegaNode *node);
        MegaNode* getNodeByCRC(const char *crc, MegaNode* parent);

        //Permissions
        MegaError checkAccess(MegaNode* node, int level);
        MegaError checkMove(MegaNode* node, MegaNode* target);

        bool isFilesystemAvailable();
        MegaNode *getRootNode();
        MegaNode* getInboxNode();
        MegaNode *getRubbishNode();

        void setDefaultFilePermissions(int permissions);
        int getDefaultFilePermissions();
        void setDefaultFolderPermissions(int permissions);
        int getDefaultFolderPermissions();

        long long getBandwidthOverquotaDelay();

        MegaNodeList* search(MegaNode* node, const char* searchString, bool recursive = 1);
        bool processMegaTree(MegaNode* node, MegaTreeProcessor* processor, bool recursive = 1);
        MegaNodeList* search(const char* searchString);

        MegaNode *createForeignFileNode(MegaHandle handle, const char *key, const char *name, m_off_t size, m_off_t mtime,
                                       MegaHandle parentHandle, const char *privateauth, const char *publicauth);
        MegaNode *createForeignFolderNode(MegaHandle handle, const char *name, MegaHandle parentHandle,
                                         const char *privateauth, const char *publicauth);

        MegaNode *authorizeNode(MegaNode *node);

        const char *getVersion();
        const char *getUserAgent();

        void changeApiUrl(const char *apiURL, bool disablepkp = false);
        void retrySSLerrors(bool enable);
        void setPublicKeyPinning(bool enable);

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

        char* escapeFsIncompatible(const char *filename);
        char* unescapeFsIncompatible(const char* name);

        bool createThumbnail(const char* imagePath, const char *dstPath);
        bool createPreview(const char* imagePath, const char *dstPath);

        bool isOnline();

#ifdef HAVE_LIBUV
        // start/stop
        bool httpServerStart(bool localOnly = true, int port = 4443);
        void httpServerStop();
        int httpServerIsRunning();

        // management
        char *httpServerGetLocalLink(MegaNode *node);
        void httpServerSetMaxBufferSize(int bufferSize);
        int httpServerGetMaxBufferSize();
        void httpServerSetMaxOutputSize(int outputSize);
        int httpServerGetMaxOutputSize();

        // permissions
        void httpServerEnableFileServer(bool enable);
        bool httpServerIsFileServerEnabled();
        void httpServerEnableFolderServer(bool enable);
        bool httpServerIsFolderServerEnabled();
        void httpServerSetRestrictedMode(int mode);
        int httpServerGetRestrictedMode();
        void httpServerEnableSubtitlesSupport(bool enable);
        bool httpServerIsSubtitlesSupportEnabled();
        bool httpServerIsLocalOnly();

        void httpServerAddListener(MegaTransferListener *listener);
        void httpServerRemoveListener(MegaTransferListener *listener);

        void fireOnStreamingStart(MegaTransferPrivate *transfer);
        void fireOnStreamingTemporaryError(MegaTransferPrivate *transfer, MegaError e);
        void fireOnStreamingFinish(MegaTransferPrivate *transfer, MegaError e);
#endif

#ifdef ENABLE_CHAT
        void createChat(bool group, MegaTextChatPeerList *peers, MegaRequestListener *listener = NULL);
        void inviteToChat(MegaHandle chatid, MegaHandle uh, int privilege, MegaRequestListener *listener = NULL);
        void removeFromChat(MegaHandle chatid, MegaHandle uh = INVALID_HANDLE, MegaRequestListener *listener = NULL);
        void getUrlChat(MegaHandle chatid, MegaRequestListener *listener = NULL);
        void grantAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener = NULL);
        void removeAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener = NULL);
        void updateChatPermissions(MegaHandle chatid, MegaHandle uh, int privilege, MegaRequestListener *listener = NULL);
        void truncateChat(MegaHandle chatid, MegaHandle messageid, MegaRequestListener *listener = NULL);
#endif

        void fireOnTransferStart(MegaTransferPrivate *transfer);
        void fireOnTransferFinish(MegaTransferPrivate *transfer, MegaError e);
        void fireOnTransferUpdate(MegaTransferPrivate *transfer);
        void fireOnTransferTemporaryError(MegaTransferPrivate *transfer, MegaError e);
        map<int, MegaTransferPrivate *> transferMap;

        MegaClient *getMegaClient();
        static FileFingerprint *getFileFingerprintInternal(const char *fingerprint);

protected:
        static const unsigned int MAX_SESSION_LENGTH;

        void init(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath = NULL, const char *userAgent = NULL, int fseventsfd = -1);

        static void *threadEntryPoint(void *param);
        static ExternalLogger *externalLogger;

        void fireOnRequestStart(MegaRequestPrivate *request);
        void fireOnRequestFinish(MegaRequestPrivate *request, MegaError e);
        void fireOnRequestUpdate(MegaRequestPrivate *request);
        void fireOnRequestTemporaryError(MegaRequestPrivate *request, MegaError e);
        bool fireOnTransferData(MegaTransferPrivate *transfer);
        void fireOnUsersUpdate(MegaUserList *users);
        void fireOnNodesUpdate(MegaNodeList *nodes);
        void fireOnAccountUpdate();
        void fireOnContactRequestsUpdate(MegaContactRequestList *requests);
        void fireOnReloadNeeded();

#ifdef ENABLE_SYNC
        void fireOnGlobalSyncStateChanged();
        void fireOnSyncStateChanged(MegaSyncPrivate *sync);
        void fireOnSyncEvent(MegaSyncPrivate *sync, MegaSyncEvent *event);
        void fireOnFileSyncStateChanged(MegaSyncPrivate *sync, const char *filePath, int newState);
#endif

#ifdef ENABLE_CHAT
        void fireOnChatsUpdate(MegaTextChatList *chats);
#endif

        MegaApi *api;
        MegaThread thread;
        MegaClient *client;
        MegaHttpIO *httpio;
        MegaWaiter *waiter;
        MegaFileSystemAccess *fsAccess;
        MegaDbAccess *dbAccess;
        GfxProc *gfxAccess;

#ifdef HAVE_LIBUV
        MegaHTTPServer *httpServer;
        int httpServerMaxBufferSize;
        int httpServerMaxOutputSize;
        bool httpServerEnableFiles;
        bool httpServerEnableFolders;
        int httpServerRestrictedMode;
        bool httpServerSubtitlesSupportEnabled;
        set<MegaTransferListener *> httpServerListeners;
#endif
		
        RequestQueue requestQueue;
        TransferQueue transferQueue;
        map<int, MegaRequestPrivate *> requestMap;

        vector<m_time_t> downloadTimes;
        vector<int64_t> downloadBytes;
        int64_t downloadPartialBytes;
        int64_t downloadSpeed;

        vector<m_time_t> uploadTimes;
        vector<int64_t> uploadBytes;
        int64_t uploadPartialBytes;
        int64_t uploadSpeed;

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
        MegaContactRequestList *activeContactRequests;

        int threadExit;
        void loop();

        int maxRetries;

        // a request-level error occurred
        virtual void request_error(error);
        virtual void request_response_progress(m_off_t, m_off_t);

        // login result
        virtual void login_result(error);
        virtual void logout_result(error);
        virtual void userdata_result(string*, string*, string*, handle, error);
        virtual void pubkey_result(User *);

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
        virtual void account_updated();
        virtual void pcrs_updated(PendingContactRequest**, int);

        // password change result
        virtual void changepw_result(error);

        // user attribute update notification
        virtual void userattr_update(User*, int, const char*);


        virtual void fetchnodes_result(error);
        virtual void putnodes_result(error, targettype_t, NewNode*);

        // share update result
        virtual void share_result(error);
        virtual void share_result(int, error);

        // contact request results
        void setpcr_result(handle, error, opcactions_t);
        void updatepcr_result(error, ipcactions_t);

        // file attribute fetch result
        virtual void fa_complete(Node*, fatype, const char*, uint32_t);
        virtual int fa_failed(handle, fatype, int, error);

        // file attribute modification result
        virtual void putfa_result(handle, fatype, error);
        virtual void putfa_result(handle, fatype, const char*);

        // purchase transactions
        virtual void enumeratequotaitems_result(handle product, unsigned prolevel, unsigned gbstorage, unsigned gbtransfer,
                                                unsigned months, unsigned amount, const char* currency, const char* description, const char* iosid, const char* androidid);
        virtual void enumeratequotaitems_result(error e);
        virtual void additem_result(error);
        virtual void checkout_result(const char*, error);
        virtual void submitpurchasereceipt_result(error);
        virtual void creditcardstore_result(error);
        virtual void creditcardquerysubscriptions_result(int, error);
        virtual void creditcardcancelsubscriptions_result(error);
        virtual void getpaymentmethods_result(int, error);
        virtual void copysession_result(string*, error);

        virtual void userfeedbackstore_result(error);
        virtual void sendevent_result(error);

        virtual void checkfile_result(handle h, error e);
        virtual void checkfile_result(handle h, error e, byte* filekey, m_off_t size, m_time_t ts, m_time_t tm, string* filename, string* fingerprint, string* fileattrstring);

        // user invites/attributes
        virtual void removecontact_result(error);
        virtual void putua_result(error);
        virtual void getua_result(error);
        virtual void getua_result(byte*, unsigned);
        virtual void getua_result(TLVstore *);
#ifdef DEBUG
        virtual void delua_result(error);
#endif

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
        virtual void transfer_failed(Transfer*, error error, dstime timeleft);
        virtual void transfer_update(Transfer*);
        virtual void transfer_complete(Transfer*);
        virtual void transfer_resume(string*);

        virtual dstime pread_failure(error, int, void*, dstime);
        virtual bool pread_data(byte*, m_off_t, m_off_t, void*);

        virtual void reportevent_result(error);
        virtual void sessions_killed(handle sessionid, error e);

        virtual void cleanrubbishbin_result(error);

        virtual void getrecoverylink_result(error);
        virtual void queryrecoverylink_result(error);
        virtual void queryrecoverylink_result(int type, const char *email, const char *ip, time_t ts, handle uh, const vector<string> *emails);
        virtual void getprivatekey_result(error, const byte *privk = NULL, const size_t len_privk = 0);
        virtual void confirmrecoverylink_result(error);
        virtual void confirmcancellink_result(error);
        virtual void validatepassword_result(error);
        virtual void getemaillink_result(error);
        virtual void confirmemaillink_result(error);

#ifdef ENABLE_CHAT
        // chat-related commandsresult
        virtual void chatcreate_result(TextChat *, error);
        virtual void chatinvite_result(error);
        virtual void chatremove_result(error);
        virtual void chaturl_result(error);
        virtual void chaturl_result(string*, error);
        virtual void chatgrantaccess_result(error);
        virtual void chatremoveaccess_result(error);
        virtual void chatupdatepermissions_result(error);
        virtual void chattruncate_result(error);

        virtual void chats_updated(textchat_vector *);
#endif

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
        void getUserAttr(const char* email_or_handle, int type, const char *dstFilePath, MegaRequestListener *listener = NULL);
        void setUserAttr(int type, const char *value, MegaRequestListener *listener = NULL);
        char *getAvatarColor(handle userhandle);
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

class ExternalInputStream : public InputStreamAccess
{
    MegaInputStream *inputStream;

public:
    ExternalInputStream(MegaInputStream *inputStream);
    virtual m_off_t size();
    virtual bool read(byte *buffer, unsigned size);
};

class FileInputStream : public InputStreamAccess
{
    FileAccess *fileAccess;
    m_off_t offset;

public:
    FileInputStream(FileAccess *fileAccess);
    virtual m_off_t size();
    virtual bool read(byte *buffer, unsigned size);
    virtual ~FileInputStream();
};

#ifdef HAVE_LIBUV
class StreamingBuffer
{
public:
    StreamingBuffer();
    ~StreamingBuffer();
    void init(unsigned int capacity);
    unsigned int append(const char *buf, unsigned int len);
    unsigned int availableData();
    unsigned int availableSpace();
    unsigned int availableCapacity();
    uv_buf_t nextBuffer();
    void freeData(unsigned int len);
    void setMaxBufferSize(unsigned int bufferSize);
    void setMaxOutputSize(unsigned int outputSize);

    static const unsigned int MAX_BUFFER_SIZE = 2097152;
    static const unsigned int MAX_OUTPUT_SIZE = 16384;

protected:
    char *buffer;
    unsigned int capacity;
    unsigned int size;
    unsigned int free;
    unsigned int inpos;
    unsigned int outpos;
    unsigned int maxBufferSize;
    unsigned int maxOutputSize;
};

class MegaHTTPServer;
class MegaHTTPContext : public MegaTransferListener, public MegaRequestListener
{
public:
    MegaHTTPContext();

    // Connection management
    MegaHTTPServer *server;
    StreamingBuffer streamingBuffer;
    MegaTransferPrivate *transfer;
    uv_tcp_t tcphandle;
    uv_async_t asynchandle;
    http_parser parser;
    uv_mutex_t mutex;
    MegaApiImpl *megaApi;
    m_off_t bytesWritten;
    m_off_t size;
    char *lastBuffer;
    int lastBufferLen;
    bool nodereceived;
    bool finished;
    bool failed;
    bool pause;

    // Request information
    bool range;
    m_off_t rangeStart;
    m_off_t rangeEnd;
    m_off_t rangeWritten;
    MegaNode *node;
    std::string path;
    std::string nodehandle;
    std::string nodekey;
    std::string nodename;
    int resultCode;

    virtual void onTransferStart(MegaApi *, MegaTransfer *transfer);
    virtual bool onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size);
    virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError *e);
    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError *e);
};

class MegaHTTPServer
{
protected:
    static void *threadEntryPoint(void *param);
    static http_parser_settings parsercfg;

    set<handle> allowedHandles;
    handle lastHandle;
    list<MegaHTTPContext*> connections;
    uv_async_t exit_handle;
    MegaApiImpl *megaApi;
    uv_sem_t semaphore;
    MegaThread thread;
    uv_tcp_t server;
    int maxBufferSize;
    int maxOutputSize;
    bool fileServerEnabled;
    bool folderServerEnabled;
    bool subtitlesSupportEnabled;
    int restrictedMode;
    bool localOnly;
    bool started;
    int port;

    // libuv callbacks
    static void onNewClient(uv_stream_t* server_handle, int status);
    static void onDataReceived(uv_stream_t* tcp, ssize_t nread, const uv_buf_t * buf);
    static void allocBuffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t* buf);
    static void onClose(uv_handle_t* handle);
    static void onAsyncEventClose(uv_handle_t* handle);
    static void onAsyncEvent(uv_async_t* handle);
    static void onCloseRequested(uv_async_t* handle);
    static void onWriteFinished(uv_write_t* req, int status);

    // HTTP parser callback
    static int onMessageBegin(http_parser* parser);
    static int onHeadersComplete(http_parser* parser);
    static int onUrlReceived(http_parser* parser, const char* url, size_t length);
    static int onHeaderField(http_parser* parser, const char* at, size_t length);
    static int onHeaderValue(http_parser* parser, const char* at, size_t length);
    static int onBody(http_parser* parser, const char* at, size_t length);
    static int onMessageComplete(http_parser* parser);

    void run();
    static void sendHeaders(MegaHTTPContext *httpctx, string *headers);
    static void sendNextBytes(MegaHTTPContext *httpctx);
    static int streamNode(MegaHTTPContext *httpctx);

public:
    MegaHTTPServer(MegaApiImpl *megaApi);
    virtual ~MegaHTTPServer();
    bool start(int port, bool localOnly = true);
    void stop();
    int getPort();
    bool isLocalOnly();
    void setMaxBufferSize(int bufferSize);
    void setMaxOutputSize(int outputSize);
    int getMaxBufferSize();
    int getMaxOutputSize();
    void enableFileServer(bool enable);
    void enableFolderServer(bool enable);
    void setRestrictedMode(int mode);
    bool isFileServerEnabled();
    bool isFolderServerEnabled();
    int getRestrictedMode();
    bool isHandleAllowed(handle h);
    void clearAllowedHandles();
    char* getLink(MegaNode *node);
    bool isSubtitlesSupportEnabled();
    void enableSubtitlesSupport(bool enable);
};
#endif

}

#endif //MEGAAPI_IMPL_H
