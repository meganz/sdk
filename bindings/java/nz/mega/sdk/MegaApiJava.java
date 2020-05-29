/*
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,\
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * @copyright Simplified (2-clause) BSD License.
 * You should have received a copy of the license along with this
 * program.
 */
package nz.mega.sdk;

import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.Set;

import mega.privacy.android.app.MegaApplication;
import mega.privacy.android.app.R;

import static nz.mega.sdk.MegaError.*;

/**
 * Java Application Programming Interface (API) to access MEGA SDK services on a MEGA account or shared public folder.
 * <p>
 * An appKey must be specified to use the MEGA SDK. Generate an appKey for free here: <br>
 * - https://mega.co.nz/#sdk
 * <p>
 * Save on data usage and start up time by enabling local node caching. This can be enabled by passing a local path
 * in the constructor. Local node caching prevents the need to download the entire file system each time the MegaApiJava
 * object is logged in.
 * <p>
 * To take advantage of local node caching, the application needs to save the session key after login
 * (MegaApiJava.dumpSession()) and use it to login during the next session. A persistent local node cache will only be
 * loaded if logging in with a session key.
 * Local node caching is also recommended in order to enhance security as it prevents the account password from being
 * stored by the application.
 * <p>
 * To access MEGA services using the MEGA SDK, an object of this class (MegaApiJava) needs to be created and one of the
 * MegaApiJava.login() options used to log into a MEGA account or a public folder. If the login request succeeds,
 * call MegaApiJava.fetchNodes() to get the account's file system from MEGA. Once the file system is retrieved, all other
 * requests including file management and transfers can be used.
 * <p>
 * After using MegaApiJava.logout() you can reuse the same MegaApi object to log in to another MEGA account or a public
 * folder.
 */
public class MegaApiJava {
    MegaApi megaApi;
    MegaGfxProcessor gfxProcessor;

    void runCallback(Runnable runnable) {
        runnable.run();
    }

    static Set<DelegateMegaRequestListener> activeRequestListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaRequestListener>());
    static Set<DelegateMegaTransferListener> activeTransferListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTransferListener>());
    static Set<DelegateMegaGlobalListener> activeGlobalListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaGlobalListener>());
    static Set<DelegateMegaListener> activeMegaListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaListener>());
    static Set<DelegateMegaLogger> activeMegaLoggers = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaLogger>());
    static Set<DelegateMegaTreeProcessor> activeMegaTreeProcessors = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTreeProcessor>());
    static Set<DelegateMegaTransferListener> activeHttpServerListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTransferListener>());

    /**
     * INVALID_HANDLE Invalid value for a handle
     *
     * This value is used to represent an invalid handle. Several MEGA objects can have
     * a handle but it will never be INVALID_HANDLE.
     */
    public final static long INVALID_HANDLE = ~(long)0;

    // Very severe error event that will presumably lead the application to abort.
    public final static int LOG_LEVEL_FATAL = MegaApi.LOG_LEVEL_FATAL;
    // Error information but application will continue run.
    public final static int LOG_LEVEL_ERROR = MegaApi.LOG_LEVEL_ERROR;
    // Information representing errors in application but application will keep running
    public final static int LOG_LEVEL_WARNING = MegaApi.LOG_LEVEL_WARNING;
    // Mainly useful to represent current progress of application.
    public final static int LOG_LEVEL_INFO = MegaApi.LOG_LEVEL_INFO;
    // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
    public final static int LOG_LEVEL_DEBUG = MegaApi.LOG_LEVEL_DEBUG;
    public final static int LOG_LEVEL_MAX = MegaApi.LOG_LEVEL_MAX;

    public final static int ATTR_TYPE_THUMBNAIL = MegaApi.ATTR_TYPE_THUMBNAIL;
    public final static int ATTR_TYPE_PREVIEW = MegaApi.ATTR_TYPE_PREVIEW;
    
    public final static int USER_ATTR_AVATAR = MegaApi.USER_ATTR_AVATAR;
    public final static int USER_ATTR_FIRSTNAME = MegaApi.USER_ATTR_FIRSTNAME;
    public final static int USER_ATTR_LASTNAME = MegaApi.USER_ATTR_LASTNAME;
    public final static int USER_ATTR_AUTHRING = MegaApi.USER_ATTR_AUTHRING;
    public final static int USER_ATTR_LAST_INTERACTION = MegaApi.USER_ATTR_LAST_INTERACTION;
    public final static int USER_ATTR_ED25519_PUBLIC_KEY = MegaApi.USER_ATTR_ED25519_PUBLIC_KEY;
    public final static int USER_ATTR_CU25519_PUBLIC_KEY = MegaApi.USER_ATTR_CU25519_PUBLIC_KEY;
    public final static int USER_ATTR_KEYRING = MegaApi.USER_ATTR_KEYRING;
    public final static int USER_ATTR_SIG_RSA_PUBLIC_KEY = MegaApi.USER_ATTR_SIG_RSA_PUBLIC_KEY;
    public final static int USER_ATTR_SIG_CU255_PUBLIC_KEY = MegaApi.USER_ATTR_SIG_CU255_PUBLIC_KEY;
    public final static int USER_ATTR_LANGUAGE = MegaApi.USER_ATTR_LANGUAGE;
    public final static int USER_ATTR_PWD_REMINDER = MegaApi.USER_ATTR_PWD_REMINDER;
    public final static int USER_ATTR_DISABLE_VERSIONS = MegaApi.USER_ATTR_DISABLE_VERSIONS;
    public final static int USER_ATTR_CONTACT_LINK_VERIFICATION = MegaApi.USER_ATTR_CONTACT_LINK_VERIFICATION;
    public final static int USER_ATTR_RICH_PREVIEWS = MegaApi.USER_ATTR_RICH_PREVIEWS;
    public final static int USER_ATTR_RUBBISH_TIME = MegaApi.USER_ATTR_RUBBISH_TIME;
    public final static int USER_ATTR_LAST_PSA = MegaApi.USER_ATTR_LAST_PSA;
    public final static int USER_ATTR_STORAGE_STATE = MegaApi.USER_ATTR_STORAGE_STATE;
    public final static int USER_ATTR_GEOLOCATION = MegaApi.USER_ATTR_GEOLOCATION;
    public final static int USER_ATTR_CAMERA_UPLOADS_FOLDER = MegaApi.USER_ATTR_CAMERA_UPLOADS_FOLDER;
    public final static int USER_ATTR_MY_CHAT_FILES_FOLDER = MegaApi.USER_ATTR_MY_CHAT_FILES_FOLDER;
    public final static int USER_ATTR_PUSH_SETTINGS = MegaApi.USER_ATTR_PUSH_SETTINGS;
    public final static int USER_ATTR_ALIAS = MegaApi.USER_ATTR_ALIAS;

    public final static int NODE_ATTR_DURATION = MegaApi.NODE_ATTR_DURATION;
    public final static int NODE_ATTR_COORDINATES = MegaApi.NODE_ATTR_COORDINATES;

    public final static int PAYMENT_METHOD_BALANCE = MegaApi.PAYMENT_METHOD_BALANCE;
    public final static int PAYMENT_METHOD_PAYPAL = MegaApi.PAYMENT_METHOD_PAYPAL;
    public final static int PAYMENT_METHOD_ITUNES = MegaApi.PAYMENT_METHOD_ITUNES;
    public final static int PAYMENT_METHOD_GOOGLE_WALLET = MegaApi.PAYMENT_METHOD_GOOGLE_WALLET;
    public final static int PAYMENT_METHOD_BITCOIN = MegaApi.PAYMENT_METHOD_BITCOIN;
    public final static int PAYMENT_METHOD_UNIONPAY = MegaApi.PAYMENT_METHOD_UNIONPAY;
    public final static int PAYMENT_METHOD_FORTUMO = MegaApi.PAYMENT_METHOD_FORTUMO;
    public final static int PAYMENT_METHOD_CREDIT_CARD = MegaApi.PAYMENT_METHOD_CREDIT_CARD;
    public final static int PAYMENT_METHOD_CENTILI = MegaApi.PAYMENT_METHOD_CENTILI;
    public final static int PAYMENT_METHOD_WINDOWS_STORE = MegaApi.PAYMENT_METHOD_WINDOWS_STORE;
	
    public final static int TRANSFER_METHOD_NORMAL = MegaApi.TRANSFER_METHOD_NORMAL;
    public final static int TRANSFER_METHOD_ALTERNATIVE_PORT = MegaApi.TRANSFER_METHOD_ALTERNATIVE_PORT;
    public final static int TRANSFER_METHOD_AUTO = MegaApi.TRANSFER_METHOD_AUTO;
    public final static int TRANSFER_METHOD_AUTO_NORMAL = MegaApi.TRANSFER_METHOD_AUTO_NORMAL;
    public final static int TRANSFER_METHOD_AUTO_ALTERNATIVE = MegaApi.TRANSFER_METHOD_AUTO_ALTERNATIVE;

    public final static int PUSH_NOTIFICATION_ANDROID = MegaApi.PUSH_NOTIFICATION_ANDROID;
    public final static int PUSH_NOTIFICATION_IOS_VOIP = MegaApi.PUSH_NOTIFICATION_IOS_VOIP;
    public final static int PUSH_NOTIFICATION_IOS_STD = MegaApi.PUSH_NOTIFICATION_IOS_STD;

    public final static int PASSWORD_STRENGTH_VERYWEAK = MegaApi.PASSWORD_STRENGTH_VERYWEAK;
    public final static int PASSWORD_STRENGTH_WEAK = MegaApi.PASSWORD_STRENGTH_WEAK;
    public final static int PASSWORD_STRENGTH_MEDIUM = MegaApi.PASSWORD_STRENGTH_MEDIUM;
    public final static int PASSWORD_STRENGTH_GOOD = MegaApi.PASSWORD_STRENGTH_GOOD;
    public final static int PASSWORD_STRENGTH_STRONG = MegaApi.PASSWORD_STRENGTH_STRONG;

    public final static int RETRY_NONE = MegaApi.RETRY_NONE;
    public final static int RETRY_CONNECTIVITY = MegaApi.RETRY_CONNECTIVITY;
    public final static int RETRY_SERVERS_BUSY = MegaApi.RETRY_SERVERS_BUSY;
    public final static int RETRY_API_LOCK = MegaApi.RETRY_API_LOCK;
    public final static int RETRY_RATE_LIMIT = MegaApi.RETRY_RATE_LIMIT;
    public final static int RETRY_LOCAL_LOCK = MegaApi.RETRY_LOCAL_LOCK;
    public final static int RETRY_UNKNOWN = MegaApi.RETRY_UNKNOWN;

    public final static int KEEP_ALIVE_CAMERA_UPLOADS = MegaApi.KEEP_ALIVE_CAMERA_UPLOADS;

    public final static int STORAGE_STATE_UNKNOWN = MegaApi.STORAGE_STATE_UNKNOWN;
    public final static int STORAGE_STATE_GREEN = MegaApi.STORAGE_STATE_GREEN;
    public final static int STORAGE_STATE_ORANGE = MegaApi.STORAGE_STATE_ORANGE;
    public final static int STORAGE_STATE_RED = MegaApi.STORAGE_STATE_RED;
    public final static int STORAGE_STATE_CHANGE = MegaApi.STORAGE_STATE_CHANGE;

    public final static int BUSINESS_STATUS_EXPIRED = MegaApi.BUSINESS_STATUS_EXPIRED;
    public final static int BUSINESS_STATUS_INACTIVE = MegaApi.BUSINESS_STATUS_INACTIVE;
    public final static int BUSINESS_STATUS_ACTIVE = MegaApi.BUSINESS_STATUS_ACTIVE;
    public final static int BUSINESS_STATUS_GRACE_PERIOD = MegaApi.BUSINESS_STATUS_GRACE_PERIOD;

    public final static int AFFILIATE_TYPE_INVALID = MegaApi.AFFILIATE_TYPE_INVALID;
    public final static int AFFILIATE_TYPE_ID = MegaApi.AFFILIATE_TYPE_ID;
    public final static int AFFILIATE_TYPE_FILE_FOLDER = MegaApi.AFFILIATE_TYPE_FILE_FOLDER;
    public final static int AFFILIATE_TYPE_CHAT = MegaApi.AFFILIATE_TYPE_CHAT;
    public final static int AFFILIATE_TYPE_CONTACT = MegaApi.AFFILIATE_TYPE_CONTACT;

    public final static int ORDER_NONE = MegaApi.ORDER_NONE;
    public final static int ORDER_DEFAULT_ASC = MegaApi.ORDER_DEFAULT_ASC;
    public final static int ORDER_DEFAULT_DESC = MegaApi.ORDER_DEFAULT_DESC;
    public final static int ORDER_SIZE_ASC = MegaApi.ORDER_SIZE_ASC;
    public final static int ORDER_SIZE_DESC = MegaApi.ORDER_SIZE_DESC;
    public final static int ORDER_CREATION_ASC = MegaApi.ORDER_CREATION_ASC;
    public final static int ORDER_CREATION_DESC = MegaApi.ORDER_CREATION_DESC;
    public final static int ORDER_MODIFICATION_ASC = MegaApi.ORDER_MODIFICATION_ASC;
    public final static int ORDER_MODIFICATION_DESC = MegaApi.ORDER_MODIFICATION_DESC;
    public final static int ORDER_ALPHABETICAL_ASC = MegaApi.ORDER_ALPHABETICAL_ASC;
    public final static int ORDER_ALPHABETICAL_DESC = MegaApi.ORDER_ALPHABETICAL_DESC;
    public final static int ORDER_PHOTO_ASC = MegaApi.ORDER_PHOTO_ASC;
    public final static int ORDER_PHOTO_DESC = MegaApi.ORDER_PHOTO_DESC;
    public final static int ORDER_VIDEO_ASC = MegaApi.ORDER_VIDEO_ASC;
    public final static int ORDER_VIDEO_DESC = MegaApi.ORDER_VIDEO_DESC;
    public final static int ORDER_LINK_CREATION_ASC = MegaApi.ORDER_LINK_CREATION_ASC;
    public final static int ORDER_LINK_CREATION_DESC = MegaApi.ORDER_LINK_CREATION_DESC;

    public final static int TCP_SERVER_DENY_ALL = MegaApi.TCP_SERVER_DENY_ALL;
    public final static int TCP_SERVER_ALLOW_ALL = MegaApi.TCP_SERVER_ALLOW_ALL;
    public final static int TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS = MegaApi.TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    public final static int TCP_SERVER_ALLOW_LAST_LOCAL_LINK = MegaApi.TCP_SERVER_ALLOW_LAST_LOCAL_LINK;

    public final static int HTTP_SERVER_DENY_ALL = MegaApi.HTTP_SERVER_DENY_ALL;
    public final static int HTTP_SERVER_ALLOW_ALL = MegaApi.HTTP_SERVER_ALLOW_ALL;
    public final static int HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS = MegaApi.HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    public final static int HTTP_SERVER_ALLOW_LAST_LOCAL_LINK = MegaApi.HTTP_SERVER_ALLOW_LAST_LOCAL_LINK;



    MegaApi getMegaApi()
    {
        return megaApi;
    }

    /**
     * Constructor suitable for most applications.
     * 
     * @param appKey
     *            AppKey of your application.
     *            Generate an AppKey for free here: https://mega.co.nz/#sdk
     * 
     * @param basePath
     *            Base path to store the local cache.
     *            If you pass null to this parameter, the SDK won't use any local cache.
     */
    public MegaApiJava(String appKey, String basePath) {
        megaApi = new MegaApi(appKey, basePath);
    }

    /**
     * MegaApi Constructor that allows use of a custom GFX processor.
     * <p>
     * The SDK attaches thumbnails and previews to all uploaded images. To generate them, it needs a graphics processor.
     * You can build the SDK with one of the provided built-in graphics processors. If none are available
     * in your app, you can implement the MegaGfxProcessor interface to provide a custom processor. Please
     * read the documentation of MegaGfxProcessor carefully to ensure that your implementation is valid.
     * 
     * @param appKey
     *            AppKey of your application.
     *            Generate an AppKey for free here: https://mega.co.nz/#sdk
     * 
     * @param userAgent
     *            User agent to use in network requests.
     *            If you pass null to this parameter, a default user agent will be used.
     * 
     * @param basePath
     *            Base path to store the local cache.
     *            If you pass null to this parameter, the SDK won't use any local cache.
     * 
     * @param gfxProcessor
     *            Image processor. The SDK will use it to generate previews and thumbnails.
     *            If you pass null to this parameter, the SDK will try to use the built-in image processors.
     * 
     */
    public MegaApiJava(String appKey, String userAgent, String basePath, MegaGfxProcessor gfxProcessor) {
        this.gfxProcessor = gfxProcessor;
        megaApi = new MegaApi(appKey, gfxProcessor, basePath, userAgent);
    }

    /**
     * Constructor suitable for most applications.
     * 
     * @param appKey
     *            AppKey of your application.
     *            Generate an AppKey for free here: https://mega.co.nz/#sdk
     */
    public MegaApiJava(String appKey) {
        megaApi = new MegaApi(appKey);
    }

    /****************************************************************************************************/
    // LISTENER MANAGEMENT
    /****************************************************************************************************/

    /**
     * Register a listener to receive all events (requests, transfers, global, synchronization).
     * <p>
     * You can use MegaApiJava.removeListener() to stop receiving events.
     * 
     * @param listener
     *            Listener that will receive all events (requests, transfers, global, synchronization).
     */
    public void addListener(MegaListenerInterface listener) {
        megaApi.addListener(createDelegateMegaListener(listener));
    }

    /**
     * Register a listener to receive all events about requests.
     * <p>
     * You can use MegaApiJava.removeRequestListener() to stop receiving events.
     * 
     * @param listener
     *            Listener that will receive all events about requests.
     */
    public void addRequestListener(MegaRequestListenerInterface listener) {
        megaApi.addRequestListener(createDelegateRequestListener(listener, false));
    }

    /**
     * Register a listener to receive all events about transfers.
     * <p>
     * You can use MegaApiJava.removeTransferListener() to stop receiving events.
     * 
     * @param listener
     *            Listener that will receive all events about transfers.
     */
    public void addTransferListener(MegaTransferListenerInterface listener) {
        megaApi.addTransferListener(createDelegateTransferListener(listener, false));
    }

    /**
     * Register a listener to receive global events.
     * <p>
     * You can use MegaApiJava.removeGlobalListener() to stop receiving events.
     * 
     * @param listener
     *            Listener that will receive global events.
     */
    public void addGlobalListener(MegaGlobalListenerInterface listener) {
        megaApi.addGlobalListener(createDelegateGlobalListener(listener));
    }

    /**
     * Unregister a listener.
     * <p>
     * Stop receiving events from the specified listener.
     * 
     * @param listener
     *            Object that is unregistered.
     */
    public void removeListener(MegaListenerInterface listener) {
    	ArrayList<DelegateMegaListener> listenersToRemove = new ArrayList<DelegateMegaListener>();
    	
        synchronized (activeMegaListeners) {
            Iterator<DelegateMegaListener> it = activeMegaListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                	listenersToRemove.add(delegate);
                    it.remove();
                }
            }
        }
        
        for (int i=0;i<listenersToRemove.size();i++){
        	megaApi.removeListener(listenersToRemove.get(i));
        }        	
    }

    /**
     * Unregister a MegaRequestListener.
     * <p>
     * Stop receiving events from the specified listener.
     * 
     * @param listener
     *            Object that is unregistered.
     */
    public void removeRequestListener(MegaRequestListenerInterface listener) {
    	ArrayList<DelegateMegaRequestListener> listenersToRemove = new ArrayList<DelegateMegaRequestListener>();
        synchronized (activeRequestListeners) {
            Iterator<DelegateMegaRequestListener> it = activeRequestListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaRequestListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                	listenersToRemove.add(delegate);
                    it.remove();
                }
            }
        }
        
        for (int i=0;i<listenersToRemove.size();i++){
        	megaApi.removeRequestListener(listenersToRemove.get(i));
        }
    }

    /**
     * Unregister a MegaTransferListener.
     * <p>
     * Stop receiving events from the specified listener.
     * 
     * @param listener
     *            Object that is unregistered.
     */
    public void removeTransferListener(MegaTransferListenerInterface listener) {
    	ArrayList<DelegateMegaTransferListener> listenersToRemove = new ArrayList<DelegateMegaTransferListener>();
    	
        synchronized (activeTransferListeners) {
            Iterator<DelegateMegaTransferListener> it = activeTransferListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaTransferListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                	listenersToRemove.add(delegate);
                    it.remove();
                }
            }
        }
        
        for (int i=0;i<listenersToRemove.size();i++){
        	megaApi.removeTransferListener(listenersToRemove.get(i));
        }
    }

    /**
     * Unregister a MegaGlobalListener.
     * <p>
     * Stop receiving events from the specified listener.
     * 
     * @param listener
     *            Object that is unregistered.
     */
    public void removeGlobalListener(MegaGlobalListenerInterface listener) {
    	ArrayList<DelegateMegaGlobalListener> listenersToRemove = new ArrayList<DelegateMegaGlobalListener>();
    	
        synchronized (activeGlobalListeners) {
            Iterator<DelegateMegaGlobalListener> it = activeGlobalListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaGlobalListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                	listenersToRemove.add(delegate);
                    it.remove();
                }
            }
        }
        
        for (int i=0;i<listenersToRemove.size();i++){
            megaApi.removeGlobalListener(listenersToRemove.get(i));
        }
    }

    /****************************************************************************************************/
    // UTILS
    /****************************************************************************************************/

    /**
     * Generates a hash based in the provided private key and email.
     * <p>
     * This is a time consuming operation (especially for low-end mobile devices). Since the resulting key is
     * required to log in, this function allows to do this step in a separate function. You should run this function
     * in a background thread, to prevent UI hangs. The resulting key can be used in MegaApiJava.fastLogin().
     * 
     * @param base64pwkey
     *            Private key returned by MegaApiJava.getBase64PwKey().
     * @return Base64-encoded hash.
     * @deprecated Legacy function soon to be removed.
     */
    @Deprecated public String getStringHash(String base64pwkey, String inBuf) {
        return megaApi.getStringHash(base64pwkey, inBuf);
    }

    /**
     * Get an URL to transfer the current session to the webclient
     *
     * This function creates a new session for the link so logging out in the web client won't log out
     * the current session.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_SESSION_TRANSFER_URL
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - URL to open the desired page with the same account
     *
     * You take the ownership of the returned value.
     *
     * @param path Path inside https://mega.nz/# that we want to open with the current session
     *
     * For example, if you want to open https://mega.nz/#pro, the parameter of this function should be "pro".
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getSessionTransferURL(String path, MegaRequestListenerInterface listener){
        megaApi.getSessionTransferURL(path, createDelegateRequestListener(listener));
    }

    /**
     * Get an URL to transfer the current session to the webclient
     *
     * This function creates a new session for the link so logging out in the web client won't log out
     * the current session.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_SESSION_TRANSFER_URL
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - URL to open the desired page with the same account
     *
     * You take the ownership of the returned value.
     *
     * @param path Path inside https://mega.nz/# that we want to open with the current session
     *
     * For example, if you want to open https://mega.nz/#pro, the parameter of this function should be "pro".
     */
    public void getSessionTransferURL(String path){
        megaApi.getSessionTransferURL(path);
    }

    /**
     * Converts a Base32-encoded user handle (JID) to a MegaHandle.
     * <p>
     * @param base32Handle
     *            Base32-encoded handle (JID).
     * @return User handle.
     */
    public static long base32ToHandle(String base32Handle) {
        return MegaApi.base32ToHandle(base32Handle);
    }

    /**
     * Converts a Base64-encoded node handle to a MegaHandle.
     * <p>
     * The returned value can be used to recover a MegaNode using MegaApiJava.getNodeByHandle().
     * You can revert this operation using MegaApiJava.handleToBase64().
     * 
     * @param base64Handle
     *            Base64-encoded node handle.
     * @return Node handle.
     */
    public static long base64ToHandle(String base64Handle) {
        return MegaApi.base64ToHandle(base64Handle);
    }

    /**
     * Converts a Base64-encoded user handle to a MegaHandle
     *
     * You can revert this operation using MegaApi::userHandleToBase64
     *
     * @param base64Handle Base64-encoded node handle
     * @return Node handle
     */
    public static long base64ToUserHandle(String base64Handle){
        return MegaApi.base64ToUserHandle(base64Handle);
    }

    /**
     * Converts a MegaHandle to a Base64-encoded string.
     * <p>
     * You can revert this operation using MegaApiJava.base64ToHandle().
     * 
     * @param handle
     *            to be converted.
     * @return Base64-encoded node handle.
     */
    public static String handleToBase64(long handle) {
        return MegaApi.handleToBase64(handle);
    }

    /**
     * Converts a MegaHandle to a Base64-encoded string.
     * <p>
     * You take the ownership of the returned value.
     * You can revert this operation using MegaApiJava.base64ToHandle().
     * 
     * @param handle
     *            handle to be converted.
     * @return Base64-encoded user handle.
     */
    public static String userHandleToBase64(long handle) {
        return MegaApi.userHandleToBase64(handle);
    }

    /**
     * Add entropy to internal random number generators.
     * <p>
     * It's recommended to call this function with random data to
     * enhance security.
     * 
     * @param data
     *            Byte array with random data.
     * @param size
     *            Size of the byte array (in bytes).
     */
    public void addEntropy(String data, long size) {
        megaApi.addEntropy(data, size);
    }

    /**
     * Reconnect and retry all transfers.
     */
    public void reconnect() {
        megaApi.retryPendingConnections(true, true);
    }

    /**
     * Retry all pending requests.
     * <p>
     * When requests fails they wait some time before being retried. That delay grows exponentially if the request
     * fails again. For this reason, and since this request is very lightweight, it's recommended to call it with
     * the default parameters on every user interaction with the application. This will prevent very big delays
     * completing requests.
     */
    public void retryPendingConnections() {
        megaApi.retryPendingConnections();
    }

    /**
     * Check if server-side Rubbish Bin autopurging is enabled for the current account
     *
     * Before using this function, it's needed to:
     *  - If you are logged-in: call to MegaApi::login and MegaApi::fetchNodes.
     *
     * @return True if this feature is enabled. Otherwise false.
     */
    public boolean serverSideRubbishBinAutopurgeEnabled(){
        return megaApi.serverSideRubbishBinAutopurgeEnabled();
    }

    /**
     * Check if the new format for public links is enabled
     *
     * Before using this function, it's needed to:
     *  - If you are logged-in: call to MegaApi::login and MegaApi::fetchNodes.
     *  - If you are not logged-in: call to MegaApi::getMiscFlags.
     *
     * @return True if this feature is enabled. Otherwise, false.
     */
    public boolean newLinkFormatEnabled() {
        return megaApi.newLinkFormatEnabled();
    }

    /**
     * Check if multi-factor authentication can be enabled for the current account.
     *
     * Before using this function, it's needed to:
     *  - If you are logged-in: call to MegaApi::login and MegaApi::fetchNodes.
     *  - If you are not logged-in: call to MegaApi::getMiscFlags.
     *
     * @return True if multi-factor authentication can be enabled for the current account, otherwise false.
     */
    public boolean multiFactorAuthAvailable () {
        return megaApi.multiFactorAuthAvailable();
    }

    /**
     * Check if multi-factor authentication is enabled for an account
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_CHECK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email sent in the first parameter
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if multi-factor authentication is enabled or false if it's disabled.
     *
     * @param email Email to check
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthCheck(String email, MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthCheck(email, createDelegateRequestListener(listener));
    }

    /**
     * Check if multi-factor authentication is enabled for an account
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_CHECK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email sent in the first parameter
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if multi-factor authentication is enabled or false if it's disabled.
     *
     * @param email Email to check
     */
    public void multiFactorAuthCheck(String email){
        megaApi.multiFactorAuthCheck(email);
    }

    /**
     * Get the secret code of the account to enable multi-factor authentication
     * The MegaApi object must be logged into an account to successfully use this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the Base32 secret code needed to configure multi-factor authentication.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthGetCode(MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthGetCode(createDelegateRequestListener(listener));
    }

    /**
     * Get the secret code of the account to enable multi-factor authentication
     * The MegaApi object must be logged into an account to successfully use this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the Base32 secret code needed to configure multi-factor authentication.
     */
    public void multiFactorAuthGetCode(){
        megaApi.multiFactorAuthGetCode();
    }

    /**
     * Enable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns true
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin Valid pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthEnable(String pin, MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthEnable(pin, createDelegateRequestListener(listener));
    }

    /**
     * Enable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns true
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin Valid pin code for multi-factor authentication
     */
    public void multiFactorAuthEnable(String pin){
        megaApi.multiFactorAuthEnable(pin);
    }

    /**
     * Disable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns false
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin Valid pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthDisable(String pin, MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthDisable(pin, createDelegateRequestListener(listener));
    }

    /**
     * Disable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns false
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin Valid pin code for multi-factor authentication
     */
    public void multiFactorAuthDisable(String pin){
        megaApi.multiFactorAuthDisable(pin);
    }

    /**
     * Log in to a MEGA account with multi-factor authentication enabled
     *
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the first parameter
     * - MegaRequest::getPassword - Returns the second parameter
     * - MegaRequest::getText - Returns the third parameter
     *
     * If the email/password aren't valid the error code provided in onRequestFinish is
     * MegaError::API_ENOENT.
     *
     * @param email Email of the user
     * @param password Password
     * @param pin Pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthLogin(String email, String password, String pin, MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthLogin(email, password, pin, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account with multi-factor authentication enabled
     *
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the first parameter
     * - MegaRequest::getPassword - Returns the second parameter
     * - MegaRequest::getText - Returns the third parameter
     *
     * If the email/password aren't valid the error code provided in onRequestFinish is
     * MegaError::API_ENOENT.
     *
     * @param email Email of the user
     * @param password Password
     * @param pin Pin code for multi-factor authentication
     */
    public void multiFactorAuthLogin(String email, String password, String pin){
        megaApi.multiFactorAuthLogin(email, password, pin);
    }

    /**
     * Change the password of a MEGA account with multi-factor authentication enabled
     *
     * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getPassword - Returns the old password (if it was passed as parameter)
     * - MegaRequest::getNewPassword - Returns the new password
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * @param oldPassword Old password (optional, it can be NULL to not check the old password)
     * @param newPassword New password
     * @param pin Pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthChangePassword(String oldPassword, String newPassword, String pin, MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthChangePassword(oldPassword, newPassword, pin, createDelegateRequestListener(listener));
    }

    /**
     * Change the password of a MEGA account with multi-factor authentication enabled
     *
     * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getPassword - Returns the old password (if it was passed as parameter)
     * - MegaRequest::getNewPassword - Returns the new password
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * @param oldPassword Old password (optional, it can be NULL to not check the old password)
     * @param newPassword New password
     * @param pin Pin code for multi-factor authentication
     */
    public void multiFactorAuthChangePassword(String oldPassword, String newPassword, String pin){
        megaApi.multiFactorAuthChangePassword(oldPassword, newPassword, pin);
    }

    /**
     * Initialize the change of the email address associated to an account with multi-factor authentication enabled.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * If this request succeeds, a change-email link will be sent to the specified email address.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param email The new email to be associated to the account.
     * @param pin Pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthChangeEmail(String email, String pin, MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthChangeEmail(email, pin, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the change of the email address associated to an account with multi-factor authentication enabled.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * If this request succeeds, a change-email link will be sent to the specified email address.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param email The new email to be associated to the account.
     * @param pin Pin code for multi-factor authentication
     */
    public void multiFactorAuthChangeEmail(String email, String pin){
        megaApi.multiFactorAuthChangeEmail(email, pin);
    }


    /**
     * Initialize the cancellation of an account.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
     *
     * If this request succeeds, a cancellation link will be sent to the email address of the user.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @see MegaApi::confirmCancelAccount
     *
     * @param pin Pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthCancelAccount(String pin, MegaRequestListenerInterface listener){
        megaApi.multiFactorAuthCancelAccount(pin, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the cancellation of an account.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
     *
     * If this request succeeds, a cancellation link will be sent to the email address of the user.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @see MegaApi::confirmCancelAccount
     *
     * @param pin Pin code for multi-factor authentication
     */
    public void multiFactorAuthCancelAccount(String pin){
        megaApi.multiFactorAuthCancelAccount(pin);
    }

    /**
     * Fetch details related to time zones and the current default
     *
     * The associated request type with this request is MegaRequest::TYPE_FETCH_TIMEZONE.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaTimeZoneDetails - Returns details about timezones and the current default
     *
     * @param listener MegaRequestListener to track this request
     */
    void fetchTimeZone(MegaRequestListenerInterface listener){
        megaApi.fetchTimeZone(createDelegateRequestListener(listener));
    }

    /**
     * Fetch details related to time zones and the current default
     *
     * The associated request type with this request is MegaRequest::TYPE_FETCH_TIMEZONE.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaTimeZoneDetails - Returns details about timezones and the current default
     *
     */
    void fetchTimeZone(){
        megaApi.fetchTimeZone();
    }

    /**
     * Log in to a MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail() - Returns the first parameter. <br>
     * - MegaRequest.getPassword() - Returns the second parameter.
     * <p>
     * If the email/password are not valid the error code provided in onRequestFinish() is
     * MegaError.API_ENOENT.
     * 
     * @param email
     *            Email of the user.
     * @param password
     *            Password.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void login(String email, String password, MegaRequestListenerInterface listener) {
        megaApi.login(email, password, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account.
     * <p>
     * @param email
     *            Email of the user.
     * @param password
     *            Password.
     */
    public void login(String email, String password) {
        megaApi.login(email, password);
    }

    /**
     * Log in to a public folder using a folder link.
     * <p>
     * After a successful login, you should call MegaApiJava.fetchNodes() to get filesystem and
     * start working with the folder.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail() - Returns the string "FOLDER". <br>
     * - MegaRequest.getLink() - Returns the public link to the folder.
     * 
     * @param megaFolderLink
     *            link to a folder in MEGA.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void loginToFolder(String megaFolderLink, MegaRequestListenerInterface listener) {
        megaApi.loginToFolder(megaFolderLink, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a public folder using a folder link.
     * <p>
     * After a successful login, you should call MegaApiJava.fetchNodes() to get filesystem and
     * start working with the folder.
     * 
     * @param megaFolderLink
     *            link to a folder in MEGA.
     */
    public void loginToFolder(String megaFolderLink) {
        megaApi.loginToFolder(megaFolderLink);
    }

    /**
     * Log in to a MEGA account using precomputed keys.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail() - Returns the first parameter. <br>
     * - MegaRequest.getPassword() - Returns the second parameter. <br>
     * - MegaRequest.getPrivateKey() - Returns the third parameter.
     * <p>
     * If the email/stringHash/base64pwKey are not valid the error code provided in onRequestFinish() is
     * MegaError.API_ENOENT.
     * 
     * @param email
     *            Email of the user.
     * @param stringHash
     *            Hash of the email returned by MegaApiJava.getStringHash().
     * @param base64pwkey
     *            Private key calculated using MegaApiJava.getBase64PwKey().
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void fastLogin(String email, String stringHash, String base64pwkey, MegaRequestListenerInterface listener) {
        megaApi.fastLogin(email, stringHash, base64pwkey, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account using precomputed keys.
     * 
     * @param email
     *            Email of the user.
     * @param stringHash
     *            Hash of the email returned by MegaApiJava.getStringHash().
     * @param base64pwkey
     *            Private key calculated using MegaApiJava.getBase64PwKey().
     */
    public void fastLogin(String email, String stringHash, String base64pwkey) {
        megaApi.fastLogin(email, stringHash, base64pwkey);
    }

    /**
     * Log in to a MEGA account using a session key.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getSessionKey() - Returns the session key.
     * 
     * @param session
     *            Session key previously dumped with MegaApiJava.dumpSession().
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void fastLogin(String session, MegaRequestListenerInterface listener) {
        megaApi.fastLogin(session, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account using a session key.
     * 
     * @param session
     *            Session key previously dumped with MegaApiJava.dumpSession().
     */
    public void fastLogin(String session) {
        megaApi.fastLogin(session);
    }

    /**
     * Close a MEGA session.
     * 
     * All clients using this session will be automatically logged out.
     * <p>
     * You can get session information using MegaApiJava.getExtendedAccountDetails().
     * Then use MegaAccountDetails.getNumSessions and MegaAccountDetails.getSession
     * to get session info.
     * MegaAccountSession.getHandle provides the handle that this function needs.
     * <p>
     * If you use mega.INVALID_HANDLE, all sessions except the current one will be closed.
     * 
     * @param sessionHandle
     *            of the session. Use mega.INVALID_HANDLE to cancel all sessions except the current one.
     * @param listener
     *            MegaRequestListenerInterface to track this request.
     */
    public void killSession(long sessionHandle, MegaRequestListenerInterface listener) {
        megaApi.killSession(sessionHandle, createDelegateRequestListener(listener));
    }

    /**
     * Close a MEGA session.
     * <p>
     * All clients using this session will be automatically logged out.
     * <p>
     * You can get session information using MegaApiJava.getExtendedAccountDetails().
     * Then use MegaAccountDetails.getNumSessions and MegaAccountDetails.getSession
     * to get session info.
     * MegaAccountSession.getHandle provides the handle that this function needs.
     * <p>
     * If you use mega.INVALID_HANDLE, all sessions except the current one will be closed.
     * 
     * @param sessionHandle
     *            of the session. Use mega.INVALID_HANDLE to cancel all sessions except the current one.
     */
    public void killSession(long sessionHandle) {
        megaApi.killSession(sessionHandle);
    }

    /**
     * Get data about the logged account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_USER_DATA.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getName() - Returns the name of the logged user. <br>
     * - MegaRequest.getPassword() - Returns the the public RSA key of the account, Base64-encoded. <br>
     * - MegaRequest.getPrivateKey() - Returns the private RSA key of the account, Base64-encoded.
     * 
     * @param listener
     *            MegaRequestListenerInterface to track this request.
     */
    public void getUserData(MegaRequestListenerInterface listener) {
        megaApi.getUserData(createDelegateRequestListener(listener));
    }

    /**
     * Get data about the logged account.
     * 
     */
    public void getUserData() {
        megaApi.getUserData();
    }

    /**
     * Get data about a contact.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_USER_DATA.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest.getEmail - Returns the email of the contact
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getText() - Returns the XMPP ID of the contact. <br>
     * - MegaRequest.getPassword() - Returns the public RSA key of the contact, Base64-encoded.
     * 
     * @param user
     *            Contact to get the data.
     * @param listener
     *            MegaRequestListenerInterface to track this request.
     */
    public void getUserData(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.getUserData(user, createDelegateRequestListener(listener));
    }

    /**
     * Get data about a contact.
     * 
     * @param user
     *            Contact to get the data.
     */
    public void getUserData(MegaUser user) {
        megaApi.getUserData(user);
    }

    /**
     * Get data about a contact.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_USER_DATA.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail() - Returns the email or the Base64 handle of the contact.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getText() - Returns the XMPP ID of the contact. <br>
     * - MegaRequest.getPassword() - Returns the public RSA key of the contact, Base64-encoded.
     * 
     * @param user
     *            Email or Base64 handle of the contact.
     * @param listener
     *            MegaRequestListenerInterface to track this request.
     */
    public void getUserData(String user, MegaRequestListenerInterface listener) {
        megaApi.getUserData(user, createDelegateRequestListener(listener));
    }

    /**
     * Get data about a contact.
     * 
     * @param user
     *            Email or Base64 handle of the contact.
     */
    public void getUserData(String user) {
        megaApi.getUserData(user);
    }

    /**
     * Fetch miscellaneous flags when not logged in
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_MISC_FLAGS.
     *
     * When onRequestFinish is called with MegaError::API_OK, the global flags are available.
     * If you are logged in into an account, the error code provided in onRequestFinish is
     * MegaError::API_EACCESS.
     *
     * @see MegaApi::multiFactorAuthAvailable
     * @see MegaApi::newLinkFormatEnabled
     * @see MegaApi::smsAllowedState
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getMiscFlags(MegaRequestListenerInterface listener) {
        megaApi.getMiscFlags(createDelegateRequestListener(listener));
    }

    /**
     * Fetch miscellaneous flags when not logged in
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_MISC_FLAGS.
     *
     * When onRequestFinish is called with MegaError::API_OK, the global flags are available.
     * If you are logged in into an account, the error code provided in onRequestFinish is
     * MegaError::API_EACCESS.
     *
     * @see MegaApi::multiFactorAuthAvailable
     * @see MegaApi::newLinkFormatEnabled
     * @see MegaApi::smsAllowedState
     */
    public void getMiscFlags() {
        megaApi.getMiscFlags();
    }

    /**
     * Returns the current session key.
     * <p>
     * You have to be logged in to get a valid session key. Otherwise,
     * this function returns null.
     * 
     * @return Current session key.
     */
    public String dumpSession() {
        return megaApi.dumpSession();
    }

    /**
     * Initialize the creation of a new MEGA account, with firstname and lastname
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     *
     * If this request succeeds, a new ephemeral session will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     *
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email Email for the account
     * @param password Password for the account
     * @param firstname Firstname of the user
     * @param lastname Lastname of the user
     * @param listener MegaRequestListener to track this request
     */
    public void createAccount(String email, String password, String firstname, String lastname, MegaRequestListenerInterface listener){
    	megaApi.createAccount(email, password, firstname, lastname, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the creation of a new MEGA account, with firstname and lastname
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     *
     * If this request succeeds, a new ephemeral session will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     *
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email Email for the account
     * @param password Password for the account
     * @param firstname Firstname of the user
     * @param lastname Lastname of the user
     */
    public void createAccount(String email, String password, String firstname, String lastname){
        megaApi.createAccount(email, password, firstname, lastname);
    }

    /**
     * Initialize the creation of a new MEGA account, with firstname and lastname
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     * - MegaRequest::getAccess - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     *
     * If this request succeeds, a new ephemeral session will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     *
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email Email for the account
     * @param password Password for the account
     * @param firstname Firstname of the user
     * @param lastname Lastname of the user
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *      - MegaApi::AFFILIATE_TYPE_ID = 1
     *      - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *      - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *      - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     *
     * @param lastAccessTimestamp Timestamp of the last access
     * @param listener MegaRequestListener to track this request
     */
    public void createAccount(String email, String password, String firstname, String lastname,
                              long lastPublicHandle, int lastPublicHandleType, long lastAccessTimestamp,
                              MegaRequestListenerInterface listener) {
        megaApi.createAccount(email, password, firstname,
                lastname, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp,
                createDelegateRequestListener(listener));
    }

    /**
     * Initialize the creation of a new MEGA account, with firstname and lastname
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     * - MegaRequest::getAccess - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     *
     * If this request succeeds, a new ephemeral session will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     *
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email Email for the account
     * @param password Password for the account
     * @param firstname Firstname of the user
     * @param lastname Lastname of the user
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *      - MegaApi::AFFILIATE_TYPE_ID = 1
     *      - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *      - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *      - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     *
     * @param lastAccessTimestamp Timestamp of the last access
     */
    public void createAccount(String email, String password, String firstname, String lastname,
                              long lastPublicHandle, int lastPublicHandleType, long lastAccessTimestamp) {
        megaApi.createAccount(email, password, firstname, lastname, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp);
    }

    /**
     * Resume a registration process
     *
     * When a user begins the account registration process by calling MegaApi::createAccount,
     * an ephemeral account is created.
     *
     * Until the user successfully confirms the signup link sent to the provided email address,
     * you can resume the ephemeral session in order to change the email address, resend the
     * signup link (@see MegaApi::sendSignupLink) and also to receive notifications in case the
     * user confirms the account using another client (MegaGlobalListener::onAccountUpdate or
     * MegaListener::onAccountUpdate).
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * - MegaRequest::getParamType - Returns the value 1
     *
     * In case the account is already confirmed, the associated request will fail with
     * error MegaError::API_EARGS.
     *
     * @param sid Session id valid for the ephemeral account (@see MegaApi::createAccount)
     * @param listener MegaRequestListener to track this request
     */
    public void resumeCreateAccount(String sid, MegaRequestListenerInterface listener) {
        megaApi.resumeCreateAccount(sid, createDelegateRequestListener(listener));
    }

    /**
     * Resume a registration process
     *
     * When a user begins the account registration process by calling MegaApi::createAccount,
     * an ephemeral account is created.
     *
     * Until the user successfully confirms the signup link sent to the provided email address,
     * you can resume the ephemeral session in order to change the email address, resend the
     * signup link (@see MegaApi::sendSignupLink) and also to receive notifications in case the
     * user confirms the account using another client (MegaGlobalListener::onAccountUpdate or
     * MegaListener::onAccountUpdate).
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * - MegaRequest::getParamType - Returns the value 1
     *
     * In case the account is already confirmed, the associated request will fail with
     * error MegaError::API_EARGS.
     *
     * @param sid Session id valid for the ephemeral account (@see MegaApi::createAccount)
     */
    public void resumeCreateAccount(String sid) {
        megaApi.resumeCreateAccount(sid);
    }

    /**
     * Sends the confirmation email for a new account
     *
     * This function is useful to send the confirmation link again or to send it to a different
     * email address, in case the user mistyped the email at the registration form.
     *
     * @param email Email for the account
     * @param name Firstname of the user
     * @param password Password for the account
     * @param listener MegaRequestListener to track this request
     */
    public void sendSignupLink(String email, String name, String password, MegaRequestListenerInterface listener) {
        megaApi.sendSignupLink(email, name, password, createDelegateRequestListener(listener));
    }

    /**
     * Sends the confirmation email for a new account
     *
     * This function is useful to send the confirmation link again or to send it to a different
     * email address, in case the user mistyped the email at the registration form.
     *
     * @param email Email for the account
     * @param name Firstname of the user
     * @param password Password for the account
     */
    public void sendSignupLink(String email, String name, String password) {
        megaApi.sendSignupLink(email, name, password);
    }

    /**
     * Get information about a confirmation link.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_QUERY_SIGNUP_LINK.
     * Valid data in the MegaRequest object received on all callbacks: <br>
     * - MegaRequest.getLink() - Returns the confirmation link.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getEmail() - Return the email associated with the confirmation link. <br>
     * - MegaRequest.getName() - Returns the name associated with the confirmation link.
     * 
     * @param link
     *            Confirmation link.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void querySignupLink(String link, MegaRequestListenerInterface listener) {
        megaApi.querySignupLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a confirmation link.
     * 
     * @param link
     *            Confirmation link.
     */
    public void querySignupLink(String link) {
        megaApi.querySignupLink(link);
    }

    /**
     * Confirm a MEGA account using a confirmation link and the user password.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CONFIRM_ACCOUNT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getLink() - Returns the confirmation link. <br>
     * - MegaRequest.getPassword() - Returns the password.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getEmail() - Email of the account. <br>
     * - MegaRequest.getName() - Name of the user.
     * 
     * @param link
     *            Confirmation link.
     * @param password
     *            Password for the account.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void confirmAccount(String link, String password, MegaRequestListenerInterface listener) {
        megaApi.confirmAccount(link, password, createDelegateRequestListener(listener));
    }

    /**
     * Confirm a MEGA account using a confirmation link and the user password.
     * 
     * @param link
     *            Confirmation link.
     * @param password
     *            Password for the account.
     */
    public void confirmAccount(String link, String password) {
        megaApi.confirmAccount(link, password);
    }

    /**
     * Confirm a MEGA account using a confirmation link and a precomputed key.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CONFIRM_ACCOUNT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getLink() - Returns the confirmation link. <br>
     * - MegaRequest.getPrivateKey() - Returns the base64pwkey parameter.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getEmail() - Email of the account. <br>
     * - MegaRequest.getName() - Name of the user.
     * 
     * @param link
     *            Confirmation link.
     * @param base64pwkey
     *            Private key precomputed with MegaApiJava.getBase64PwKey().
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void fastConfirmAccount(String link, String base64pwkey, MegaRequestListenerInterface listener) {
        megaApi.fastConfirmAccount(link, base64pwkey, createDelegateRequestListener(listener));
    }

    /**
     * Confirm a MEGA account using a confirmation link and a precomputed key.
     * 
     * @param link
     *            Confirmation link.
     * @param base64pwkey
     *            Private key precomputed with MegaApiJava.getBase64PwKey().
     */
    public void fastConfirmAccount(String link, String base64pwkey) {
        megaApi.fastConfirmAccount(link, base64pwkey);
    }

    /**
     * Initialize the reset of the existing password, with and without the Master Key.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_RECOVERY_LINK.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getFlag - Returns whether the user has a backup of the master key or not.
     *
     * If this request succeed, a recovery link will be sent to the user.
     * If no account is registered under the provided email, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email Email used to register the account whose password wants to be reset.
     * @param hasMasterKey True if the user has a backup of the master key. Otherwise, false.
     * @param listener MegaRequestListener to track this request
     */

    public void resetPassword(String email, boolean hasMasterKey, MegaRequestListenerInterface listener){
        megaApi.resetPassword(email, hasMasterKey, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a recovery link created by MegaApi::resetPassword.
     *
     * The associated request type with this request is MegaRequest::TYPE_QUERY_RECOVERY_LINK
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the recovery link
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * - MegaRequest::getFlag - Return whether the link requires masterkey to reset password.
     *
     * @param link Recovery link (#recover)
     * @param listener MegaRequestListener to track this request
     */
    public void queryResetPasswordLink(String link, MegaRequestListenerInterface listener){
        megaApi.queryResetPasswordLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Set a new password for the account pointed by the recovery link.
     *
     * Recovery links are created by calling MegaApi::resetPassword and may or may not
     * require to provide the Master Key.
     *
     * @see The flag of the MegaRequest::TYPE_QUERY_RECOVERY_LINK in MegaApi::queryResetPasswordLink.
     *
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the recovery link
     * - MegaRequest::getPassword - Returns the new password
     * - MegaRequest::getPrivateKey - Returns the Master Key, when provided
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * - MegaRequest::getFlag - Return whether the link requires masterkey to reset password.
     *
     * @param link The recovery link sent to the user's email address.
     * @param newPwd The new password to be set.
     * @param masterKey Base64-encoded string containing the master key (optional).
     * @param listener MegaRequestListener to track this request
     */

    public void confirmResetPassword(String link, String newPwd, String masterKey, MegaRequestListenerInterface listener){
        megaApi.confirmResetPassword(link, newPwd, masterKey, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the cancellation of an account.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
     *
     * If this request succeed, a cancellation link will be sent to the email address of the user.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @see MegaApi::confirmCancelAccount
     *
     * @param listener MegaRequestListener to track this request
     */
    public void cancelAccount(MegaRequestListenerInterface listener){
        megaApi.cancelAccount(createDelegateRequestListener(listener));
    }

    /**
     * Get information about a cancel link created by MegaApi::cancelAccount.
     *
     * The associated request type with this request is MegaRequest::TYPE_QUERY_RECOVERY_LINK
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the cancel link
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     *
     * @param link Cancel link (#cancel)
     * @param listener MegaRequestListener to track this request
     */
    public void queryCancelLink(String link, MegaRequestListenerInterface listener){
        megaApi.queryCancelLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Effectively parks the user's account without creating a new fresh account.
     *
     * The contents of the account will then be purged after 60 days. Once the account is
     * parked, the user needs to contact MEGA support to restore the account.
     *
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_CANCEL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the recovery link
     * - MegaRequest::getPassword - Returns the new password
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     *
     * @param link Cancellation link sent to the user's email address;
     * @param pwd Password for the account.
     * @param listener MegaRequestListener to track this request
     */

    public void confirmCancelAccount(String link, String pwd, MegaRequestListenerInterface listener){
        megaApi.confirmCancelAccount(link, pwd, createDelegateRequestListener(listener));
    }

    /**
     * Allow to resend the verification email for Weak Account Protection
     *
     * The verification email will be resent to the same address as it was previously sent to.
     *
     * This function can be called if the the reason for being blocked is:
     *      700: the account is supended for Weak Account Protection.
     *
     * If the logged in account is not suspended or is suspended for some other reason,
     * onRequestFinish will be called with the error code MegaError::API_EACCESS.
     *
     * If the logged in account has not been sent the unlock email before,
     * onRequestFinish will be called with the error code MegaError::API_EARGS.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void resendVerificationEmail(MegaRequestListenerInterface listener) {
        megaApi.resendVerificationEmail(createDelegateRequestListener(listener));
    }

    /**
     * Allow to resend the verification email for Weak Account Protection
     *
     * The verification email will be resent to the same address as it was previously sent to.
     *
     * This function can be called if the the reason for being blocked is:
     *      700: the account is supended for Weak Account Protection.
     *
     * If the logged in account is not suspended or is suspended for some other reason,
     * onRequestFinish will be called with the error code MegaError::API_EACCESS.
     *
     * If the logged in account has not been sent the unlock email before,
     * onRequestFinish will be called with the error code MegaError::API_EARGS.
     */
    public void resendVerificationEmail() {
        megaApi.resendVerificationEmail();
    }

    /**
     * Initialize the change of the email address associated to the account.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     *
     * If this request succeed, a change-email link will be sent to the specified email address.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param email The new email to be associated to the account.
     * @param listener MegaRequestListener to track this request
     */

    public void changeEmail(String email, MegaRequestListenerInterface listener){
        megaApi.changeEmail(email, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a change-email link created by MegaApi::changeEmail.
     *
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     *
     * The associated request type with this request is MegaRequest::TYPE_QUERY_RECOVERY_LINK
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the change-email link
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     *
     * @param link Change-email link (#verify)
     * @param listener MegaRequestListener to track this request
     */

    public void queryChangeEmailLink(String link, MegaRequestListenerInterface listener){
        megaApi.queryChangeEmailLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Effectively changes the email address associated to the account.
     *
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the change-email link
     * - MegaRequest::getPassword - Returns the password
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     *
     * @param link Change-email link sent to the user's email address.
     * @param pwd Password for the account.
     * @param listener MegaRequestListener to track this request
     */
    public void confirmChangeEmail(String link, String pwd, MegaRequestListenerInterface listener){
        megaApi.confirmChangeEmail(link, pwd, createDelegateRequestListener(listener));
    }

    /**
     * Set proxy settings.
     * <p>
     * The SDK will start using the provided proxy settings as soon as this function returns.
     * 
     * @param proxySettings
     *            settings.
     * @see MegaProxy
     */
    public void setProxySettings(MegaProxy proxySettings) {
        megaApi.setProxySettings(proxySettings);
    }

    /**
     * Try to detect the system's proxy settings.
     * 
     * Automatic proxy detection is currently supported on Windows only.
     * On other platforms, this function will return a MegaProxy object
     * of type MegaProxy.PROXY_NONE.
     * 
     * @return MegaProxy object with the detected proxy settings.
     */
    public MegaProxy getAutoProxySettings() {
        return megaApi.getAutoProxySettings();
    }

    /**
     * Check if the MegaApi object is logged in.
     * 
     * @return 0 if not logged in. Otherwise, a number >= 0.
     */
    public int isLoggedIn() {
        return megaApi.isLoggedIn();
    }

    /**
     * Check the reason of being blocked.
     *
     * The associated request type with this request is MegaRequest::TYPE_WHY_AM_I_BLOCKED.
     *
     * This request can be sent internally at anytime (whenever an account gets blocked), so
     * a MegaGlobalListener should process the result, show the reason and logout.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the reason string (in English)
     * - MegaRequest::getNumber - Returns the reason code. Possible values:
     *     0: The account is not blocked
     *     200: suspension message for any type of suspension, but copyright suspension.
     *     300: suspension only for multiple copyright violations.
     *     400: the subuser account has been disabled.
     *     401: the subuser account has been removed.
     *     500: The account needs to be verified by an SMS code.
     *     700: the account is supended for Weak Account Protection.
     *
     * If the error code in the MegaRequest object received in onRequestFinish
     * is MegaError::API_OK, the user is not blocked.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void whyAmIBlocked(MegaRequestListenerInterface listener) {
        megaApi.whyAmIBlocked(createDelegateRequestListener(listener));
    }

    /**
     * Check the reason of being blocked.
     *
     * The associated request type with this request is MegaRequest::TYPE_WHY_AM_I_BLOCKED.
     *
     * This request can be sent internally at anytime (whenever an account gets blocked), so
     * a MegaGlobalListener should process the result, show the reason and logout.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the reason string (in English)
     * - MegaRequest::getNumber - Returns the reason code. Possible values:
     *     0: The account is not blocked
     *     200: suspension message for any type of suspension, but copyright suspension.
     *     300: suspension only for multiple copyright violations.
     *     400: the subuser account has been disabled.
     *     401: the subuser account has been removed.
     *     500: The account needs to be verified by an SMS code.
     *     700: the account is supended for Weak Account Protection.
     *
     * If the error code in the MegaRequest object received in onRequestFinish
     * is MegaError::API_OK, the user is not blocked.
     */
    public void whyAmIBlocked() {
        megaApi.whyAmIBlocked();
    }

    /**
     * Create a contact link
     *
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_CREATE.
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getFlag - Returns the value of \c renew parameter
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Return the handle of the new contact link
     *
     * @param renew True to invalidate the previous contact link (if any).
     * @param listener MegaRequestListener to track this request
     */
    public void contactLinkCreate(boolean renew, MegaRequestListenerInterface listener){
        megaApi.contactLinkCreate(renew, createDelegateRequestListener(listener));
    }

    /**
     * Create a contact link
     *
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_CREATE.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Return the handle of the new contact link
     *
     */
    public void contactLinkCreate(){
        megaApi.contactLinkCreate();
    }

    /**
     * Get information about a contact link
     *
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_QUERY.
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getName - Returns the first name of the contact
     * - MegaRequest::getText - Returns the last name of the contact
     *
     * @param handle Handle of the contact link to check
     * @param listener MegaRequestListener to track this request
     */
    public void contactLinkQuery(long handle, MegaRequestListenerInterface listener){
        megaApi.contactLinkQuery(handle, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a contact link
     *
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_QUERY.
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getName - Returns the first name of the contact
     * - MegaRequest::getText - Returns the last name of the contact
     *
     * @param handle Handle of the contact link to check
     */
    public void contactLinkQuery(long handle){
        megaApi.contactLinkQuery(handle);
    }

    /**
     * Delete a contact link
     *
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_DELETE.
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     *
     * @param handle Handle of the contact link to delete
     * If the parameter is INVALID_HANDLE, the active contact link is deleted
     *
     * @param listener MegaRequestListener to track this request
     */
    public void contactLinkDelete(long handle, MegaRequestListenerInterface listener){
        megaApi.contactLinkDelete(handle, createDelegateRequestListener(listener));
    }

    /**
     * Delete a contact link
     *
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_DELETE.
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     *
     * @param handle Handle of the contact link to delete
     */
    public void contactLinkDelete(long handle){
        megaApi.contactLinkDelete(handle);
    }

    /**
     * Get the next PSA (Public Service Announcement) that should be shown to the user
     *
     * After the PSA has been accepted or dismissed by the user, app should
     * use MegaApi::setPSA to notify API servers about this event and
     * do not get the same PSA again in the next call to this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PSA.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Returns the id of the PSA (useful to call MegaApi::setPSA later)
     * - MegaRequest::getName - Returns the title of the PSA
     * - MegaRequest::getText - Returns the text of the PSA
     * - MegaRequest::getFile - Returns the URL of the image of the PSA
     * - MegaRequest::getPassword - Returns the text for the possitive button (or an empty string)
     * - MegaRequest::getLink - Returns the link for the possitive button (or an empty string)
     *
     * If there isn't any new PSA to show, onRequestFinish will be called with the error
     * code MegaError::API_ENOENT
     *
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::setPSA
     */
    void getPSA(MegaRequestListenerInterface listener){
        megaApi.getPSA(createDelegateRequestListener(listener));
    }

    /**
     * Get the next PSA (Public Service Announcement) that should be shown to the user
     *
     * After the PSA has been accepted or dismissed by the user, app should
     * use MegaApi::setPSA to notify API servers about this event and
     * do not get the same PSA again in the next call to this function.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PSA.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Returns the id of the PSA (useful to call MegaApi::setPSA later)
     * - MegaRequest::getName - Returns the title of the PSA
     * - MegaRequest::getText - Returns the text of the PSA
     * - MegaRequest::getFile - Returns the URL of the image of the PSA
     * - MegaRequest::getPassword - Returns the text for the possitive button (or an empty string)
     * - MegaRequest::getLink - Returns the link for the possitive button (or an empty string)
     *
     * If there isn't any new PSA to show, onRequestFinish will be called with the error
     * code MegaError::API_ENOENT
     *
     * @see MegaApi::setPSA
     */
    void getPSA(){
        megaApi.getPSA();
    }

    /**
     * Notify API servers that a PSA (Public Service Announcement) has been already seen
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER.
     *
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_LAST_PSA
     * - MegaRequest::getText - Returns the id passed in the first parameter (as a string)
     *
     * @param id Identifier of the PSA
     * @param listener MegaRequestListener to track this request
     *
     * @see MegaApi::getPSA
     */
    void setPSA(int id, MegaRequestListenerInterface listener){
        megaApi.setPSA(id, createDelegateRequestListener(listener));
    }

    /**
     * Notify API servers that a PSA (Public Service Announcement) has been already seen
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER.
     *
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_LAST_PSA
     * - MegaRequest::getText - Returns the id passed in the first parameter (as a string)
     *
     * @param id Identifier of the PSA
     *
     * @see MegaApi::getPSA
     */
    void setPSA(int id){
        megaApi.setPSA(id);
    }

    /**
     * Command to acknowledge user alerts.
     *
     * Other clients will be notified that alerts to this point have been seen.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void acknowledgeUserAlerts(MegaRequestListenerInterface listener){
        megaApi.acknowledgeUserAlerts(createDelegateRequestListener(listener));
    }

    /**
     * Command to acknowledge user alerts.
     *
     * Other clients will be notified that alerts to this point have been seen.
     *
     * @see MegaApi::getUserAlerts
     */
    public void acknowledgeUserAlerts(){
        megaApi.acknowledgeUserAlerts();
    }

    /**
     * Returns the email of the currently open account.
     * 
     * If the MegaApi object is not logged in or the email is not available,
     * this function returns null.
     * 
     * @return Email of the account.
     */
    public String getMyEmail() {
        return megaApi.getMyEmail();
    }
    
    /**
     * Returns the user handle of the currently open account
     *
     * If the MegaApi object isn't logged in,
     * this function returns null
     *
     * @return User handle of the account
     */
    public String getMyUserHandle() {
    	return megaApi.getMyUserHandle();
    }

    /**
     * Returns the user handle of the currently open account
     *
     * If the MegaApi object isn't logged in,
     * this function returns INVALID_HANDLE
     *
     * @return User handle of the account
     */
    public long getMyUserHandleBinary(){
        return megaApi.getMyUserHandleBinary();
    }
    
    /**
     * Get the MegaUser of the currently open account
     *
     * If the MegaApi object isn't logged in, this function returns NULL.
     *
     * You take the ownership of the returned value
     *
     * @note The visibility of your own user is unhdefined and shouldn't be used.
     * @return MegaUser of the currently open account, otherwise NULL
     */
    public MegaUser getMyUser(){
    	return megaApi.getMyUser();
    }

    /**
     * Returns whether MEGA Achievements are enabled for the open account
     * @return True if enabled, false otherwise.
     */
    public boolean isAchievementsEnabled() {
        return megaApi.isAchievementsEnabled();
    }

    /**
     * Check if the account is a business account.
     * @return returns true if it's a business account, otherwise false
     */
    public boolean isBusinessAccount() {
        return megaApi.isBusinessAccount();
    }

    /**
     * Check if the account is a master account.
     *
     * When a business account is a sub-user, not the master, some user actions will be blocked.
     * In result, the API will return the error code MegaError::API_EMASTERONLY. Some examples of
     * requests that may fail with this error are:
     *  - MegaApi::cancelAccount
     *  - MegaApi::changeEmail
     *  - MegaApi::remove
     *  - MegaApi::removeVersion
     *
     * @return returns true if it's a master account, false if it's a sub-user account
     */
    public boolean isMasterBusinessAccount() {
        return megaApi.isMasterBusinessAccount();
    }

    /**
     * Check if the business account is active or not.
     *
     * When a business account is not active, some user actions will be blocked. In result, the API
     * will return the error code MegaError::API_EBUSINESSPASTDUE. Some examples of requests
     * that may fail with this error are:
     *  - MegaApi::startDownload
     *  - MegaApi::startUpload
     *  - MegaApi::copyNode
     *  - MegaApi::share
     *  - MegaApi::cleanRubbishBin
     *
     * @return returns true if the account is active, otherwise false
     */
    public boolean isBusinessAccountActive() {
        return megaApi.isBusinessAccountActive();
    }

    /**
     * Get the status of a business account.
     * @return Returns the business account status, possible values:
     *      MegaApi::BUSINESS_STATUS_EXPIRED = -1
     *      MegaApi::BUSINESS_STATUS_INACTIVE = 0
     *      MegaApi::BUSINESS_STATUS_ACTIVE = 1
     *      MegaApi::BUSINESS_STATUS_GRACE_PERIOD = 2
     */
    public int getBusinessStatus() {
        return megaApi.getBusinessStatus();
    }

    /**
     * Check if the password is correct for the current account
     * @param password Password to check
     * @return True if the password is correct for the current account, otherwise false.
     */
    public boolean checkPassword(String password){
        return megaApi.checkPassword(password);
    }

    /**
     * Set the active log level.
     * <p>
     * This function sets the log level of the logging system. If you set a log listener using
     * MegaApiJava.setLoggerObject(), you will receive logs with the same or a lower level than
     * the one passed to this function.
     * 
     * @param logLevel
     *            Active log level. These are the valid values for this parameter: <br>
     *            - MegaApiJava.LOG_LEVEL_FATAL = 0. <br>
     *            - MegaApiJava.LOG_LEVEL_ERROR = 1. <br>
     *            - MegaApiJava.LOG_LEVEL_WARNING = 2. <br>
     *            - MegaApiJava.LOG_LEVEL_INFO = 3. <br>
     *            - MegaApiJava.LOG_LEVEL_DEBUG = 4. <br>
     *            - MegaApiJava.LOG_LEVEL_MAX = 5.
     */
    public static void setLogLevel(int logLevel) {
        MegaApi.setLogLevel(logLevel);
    }

    /**
     * Add a MegaLogger implementation to receive SDK logs
     *
     * Logs received by this objects depends on the active log level.
     * By default, it is MegaApi::LOG_LEVEL_INFO. You can change it
     * using MegaApi::setLogLevel.
     *
     * You can remove the existing logger by using MegaApi::removeLoggerObject.
     *
     * @param megaLogger MegaLogger implementation
     */
    public static void addLoggerObject(MegaLoggerInterface megaLogger){
        MegaApi.addLoggerObject(createDelegateMegaLogger(megaLogger));
    }

    /**
     * Remove a MegaLogger implementation to stop receiving SDK logs
     *
     * If the logger was registered in the past, it will stop receiving log
     * messages after the call to this function.
     *
     * @param megaLogger Previously registered MegaLogger implementation
     */
    public static void removeLoggerObject(MegaLoggerInterface megaLogger){
        ArrayList<DelegateMegaLogger> listenersToRemove = new ArrayList<DelegateMegaLogger>();

        synchronized (activeMegaLoggers) {
            Iterator<DelegateMegaLogger> it = activeMegaLoggers.iterator();
            while (it.hasNext()) {
                DelegateMegaLogger delegate = it.next();
                if (delegate.getUserListener() == megaLogger) {
                    listenersToRemove.add(delegate);
                    it.remove();
                }
            }
        }

        for (int i=0;i<listenersToRemove.size();i++){
            MegaApi.removeLoggerObject(listenersToRemove.get(i));
        }
    }

    /**
     * Send a log to the logging system.
     * <p>
     * This log will be received by the active logger object (MegaApiJava.setLoggerObject()) if
     * the log level is the same or lower than the active log level (MegaApiJava.setLogLevel()).
     * 
     * @param logLevel
     *            Log level for this message.
     * @param message
     *            Message for the logging system.
     * @param filename
     *            Origin of the log message.
     * @param line
     *            Line of code where this message was generated.
     */
    public static void log(int logLevel, String message, String filename, int line) {
        MegaApi.log(logLevel, message, filename, line);
    }

    /**
     * Send a log to the logging system.
     * <p>
     * This log will be received by the active logger object (MegaApiJava.setLoggerObject()) if
     * the log level is the same or lower than the active log level (MegaApiJava.setLogLevel()).
     * 
     * @param logLevel
     *            Log level for this message.
     * @param message
     *            Message for the logging system.
     * @param filename
     *            Origin of the log message.
     */
    public static void log(int logLevel, String message, String filename) {
        MegaApi.log(logLevel, message, filename);
    }

    /**
     * Send a log to the logging system.
     * <p>
     * This log will be received by the active logger object (MegaApiJava.setLoggerObject()) if
     * the log level is the same or lower than the active log level (MegaApiJava.setLogLevel()).
     * 
     * @param logLevel
     *            Log level for this message.
     * @param message
     *            Message for the logging system.
     */
    public static void log(int logLevel, String message) {
        MegaApi.log(logLevel, message);
    }

    /**
     * Create a folder in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_FOLDER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns the handle of the parent folder
     * - MegaRequest::getName - Returns the name of the new folder
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new folder
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param name Name of the new folder
     * @param parent Parent folder
     * @param listener MegaRequestListener to track this request
     */
    public void createFolder(String name, MegaNode parent, MegaRequestListenerInterface listener) {
        megaApi.createFolder(name, parent, createDelegateRequestListener(listener));
    }

    /**
     * Create a folder in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_FOLDER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns the handle of the parent folder
     * - MegaRequest::getName - Returns the name of the new folder
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new folder
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param name Name of the new folder
     * @param parent Parent folder
     */
    public void createFolder(String name, MegaNode parent) {
        megaApi.createFolder(name, parent);
    }

    /**
     * Move a node in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to move
     * @param newParent New parent for the node
     * @param listener MegaRequestListener to track this request
     */
    public void moveNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener) {
        megaApi.moveNode(node, newParent, createDelegateRequestListener(listener));
    }

    /**
     * Move a node in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to move
     * @param newParent New parent for the node
     */
    public void moveNode(MegaNode node, MegaNode newParent) {
        megaApi.moveNode(node, newParent);
    }

    /**
     * Move a node in the MEGA account changing the file name
     *
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     * - MegaRequest::getName - Returns the name for the new node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to move
     * @param newParent New parent for the node
     * @param newName Name for the new node
     * @param listener MegaRequestListener to track this request
     */
    void moveNode(MegaNode node, MegaNode newParent, String newName, MegaRequestListenerInterface listener) {
        megaApi.moveNode(node, newParent, newName, createDelegateRequestListener(listener));
    }

    /**
     * Move a node in the MEGA account changing the file name
     *
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     * - MegaRequest::getName - Returns the name for the new node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to move
     * @param newParent New parent for the node
     * @param newName Name for the new node
     */
    void moveNode(MegaNode node, MegaNode newParent, String newName) {
        megaApi.moveNode(node, newParent, newName);
    }

    /**
     * Copy a node in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy (if it is a public node)
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     *
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to copy
     * @param newParent Parent for the new node
     * @param listener MegaRequestListener to track this request
     */
    public void copyNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener) {
        megaApi.copyNode(node, newParent, createDelegateRequestListener(listener));
    }

    /**
     * Copy a node in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy (if it is a public node)
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     *
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to copy
     * @param newParent Parent for the new node
     */
    public void copyNode(MegaNode node, MegaNode newParent) {
        megaApi.copyNode(node, newParent);
    }

    /**
     * Copy a node in the MEGA account changing the file name
     *
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy
     * - MegaRequest::getName - Returns the name for the new node
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     *
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to copy
     * @param newParent Parent for the new node
     * @param newName Name for the new node
     * @param listener MegaRequestListener to track this request
     */
    public void copyNode(MegaNode node, MegaNode newParent, String newName, MegaRequestListenerInterface listener) {
        megaApi.copyNode(node, newParent, newName, createDelegateRequestListener(listener));
    }

    /**
     * Copy a node in the MEGA account changing the file name
     *
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy
     * - MegaRequest::getName - Returns the name for the new node
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     *
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to copy
     * @param newParent Parent for the new node
     * @param newName Name for the new node
     */
    public void copyNode(MegaNode node, MegaNode newParent, String newName) {
        megaApi.copyNode(node, newParent, newName);
    }

    /**
     * Rename a node in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_RENAME
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to rename
     * - MegaRequest::getName - Returns the new name for the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to modify
     * @param newName New name for the node
     * @param listener MegaRequestListener to track this request
     */
    public void renameNode(MegaNode node, String newName, MegaRequestListenerInterface listener) {
        megaApi.renameNode(node, newName, createDelegateRequestListener(listener));
    }

    /**
     * Rename a node in the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_RENAME
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to rename
     * - MegaRequest::getName - Returns the new name for the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to modify
     * @param newName New name for the node
     */
    public void renameNode(MegaNode node, String newName) {
        megaApi.renameNode(node, newName);
    }

    /**
     * Remove a node from the MEGA account
     *
     * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
     * the node to the Rubbish Bin use MegaApi::moveNode
     *
     * If the node has previous versions, they will be deleted too
     *
     * The associated request type with this request is MegaRequest::TYPE_REMOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
     * - MegaRequest::getFlag - Returns false because previous versions won't be preserved
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param node Node to remove
     * @param listener MegaRequestListener to track this request
     */
    public void remove(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.remove(node, createDelegateRequestListener(listener));
    }

    /**
     * Remove a node from the MEGA account
     *
     * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
     * the node to the Rubbish Bin use MegaApi::moveNode
     *
     * If the node has previous versions, they will be deleted too
     *
     * The associated request type with this request is MegaRequest::TYPE_REMOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
     * - MegaRequest::getFlag - Returns false because previous versions won't be preserved
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param node Node to remove
     */
    public void remove(MegaNode node) {
        megaApi.remove(node);
    }

    /**
     * Remove all versions from the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_VERSIONS
     *
     * When the request finishes, file versions might not be deleted yet.
     * Deletions are notified using onNodesUpdate callbacks.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void removeVersions(MegaRequestListenerInterface listener){
        megaApi.removeVersions(createDelegateRequestListener(listener));
    }

    /**
     * Remove a version of a file from the MEGA account
     *
     * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
     * the node to the Rubbish Bin use MegaApi::moveNode.
     *
     * If the node has previous versions, they won't be deleted.
     *
     * The associated request type with this request is MegaRequest::TYPE_REMOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
     * - MegaRequest::getFlag - Returns true because previous versions will be preserved
     *
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param node Node to remove
     * @param listener MegaRequestListener to track this request
     */
    public void removeVersion(MegaNode node, MegaRequestListenerInterface listener){
        megaApi.removeVersion(node, createDelegateRequestListener(listener));
    }

    /**
     * Restore a previous version of a file
     *
     * Only versions of a file can be restored, not the current version (because it's already current).
     * The node will be copied and set as current. All the version history will be preserved without changes,
     * being the old current node the previous version of the new current node, and keeping the restored
     * node also in its previous place in the version history.
     *
     * The associated request type with this request is MegaRequest::TYPE_RESTORE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to restore
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param version Node with the version to restore
     * @param listener MegaRequestListener to track this request
     */
    public void restoreVersion(MegaNode version, MegaRequestListenerInterface listener){
        megaApi.restoreVersion(version, createDelegateRequestListener(listener));
    }

    /**
     * Clean the Rubbish Bin in the MEGA account
     *
     * This function effectively removes every node contained in the Rubbish Bin. In order to
     * avoid accidental deletions, you might want to warn the user about the action.
     *
     * The associated request type with this request is MegaRequest::TYPE_CLEAN_RUBBISH_BIN. This
     * request returns MegaError::API_ENOENT if the Rubbish bin is already empty.
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void cleanRubbishBin(MegaRequestListenerInterface listener) {
    	megaApi.cleanRubbishBin(createDelegateRequestListener(listener));
    }

    /**
     * Clean the Rubbish Bin in the MEGA account
     *
     * This function effectively removes every node contained in the Rubbish Bin. In order to
     * avoid accidental deletions, you might want to warn the user about the action.
     *
     * The associated request type with this request is MegaRequest::TYPE_CLEAN_RUBBISH_BIN. This
     * request returns MegaError::API_ENOENT if the Rubbish bin is already empty.
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     */
    public void cleanRubbishBin() {
    	megaApi.cleanRubbishBin();
    }

    /**
     * Send a node to the Inbox of another MEGA user using a MegaUser
     *
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to send
     * - MegaRequest::getEmail - Returns the email of the user that receives the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to send
     * @param user User that receives the node
     * @param listener MegaRequestListener to track this request
     */
    public void sendFileToUser(MegaNode node, MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.sendFileToUser(node, user, createDelegateRequestListener(listener));
    }

    /**
     * Send a node to the Inbox of another MEGA user using a MegaUser
     *
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to send
     * - MegaRequest::getEmail - Returns the email of the user that receives the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to send
     * @param user User that receives the node
     */
    public void sendFileToUser(MegaNode node, MegaUser user) {
        megaApi.sendFileToUser(node, user);
    }

    /**
     * Send a node to the Inbox of another MEGA user using his email
     *
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to send
     * - MegaRequest::getEmail - Returns the email of the user that receives the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to send
     * @param email Email of the user that receives the node
     * @param listener MegaRequestListener to track this request
     */
    public void sendFileToUser(MegaNode node, String email, MegaRequestListenerInterface listener){
    	megaApi.sendFileToUser(node, email, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using a MegaUser
     *
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     *
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node The folder to share. It must be a non-root folder
     * @param user User that receives the shared folder
     * @param level Permissions that are granted to the user
     * Valid values for this parameter:
     * - MegaShare::ACCESS_UNKNOWN = -1
     * Stop sharing a folder with this user
     *
     * - MegaShare::ACCESS_READ = 0
     * - MegaShare::ACCESS_READWRITE = 1
     * - MegaShare::ACCESS_FULL = 2
     * - MegaShare::ACCESS_OWNER = 3
     *
     * @param listener MegaRequestListener to track this request
     */
    public void share(MegaNode node, MegaUser user, int level, MegaRequestListenerInterface listener) {
        megaApi.share(node, user, level, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using a MegaUser
     *
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     *
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node The folder to share. It must be a non-root folder
     * @param user User that receives the shared folder
     * @param level Permissions that are granted to the user
     * Valid values for this parameter:
     * - MegaShare::ACCESS_UNKNOWN = -1
     * Stop sharing a folder with this user
     *
     * - MegaShare::ACCESS_READ = 0
     * - MegaShare::ACCESS_READWRITE = 1
     * - MegaShare::ACCESS_FULL = 2
     * - MegaShare::ACCESS_OWNER = 3
     */
    public void share(MegaNode node, MegaUser user, int level) {
        megaApi.share(node, user, level);
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using his email
     *
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     *
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node The folder to share. It must be a non-root folder
     * @param email Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
     * and the user will be invited to register an account.
     *
     * @param level Permissions that are granted to the user
     * Valid values for this parameter:
     * - MegaShare::ACCESS_UNKNOWN = -1
     * Stop sharing a folder with this user
     *
     * - MegaShare::ACCESS_READ = 0
     * - MegaShare::ACCESS_READWRITE = 1
     * - MegaShare::ACCESS_FULL = 2
     * - MegaShare::ACCESS_OWNER = 3
     *
     * @param listener MegaRequestListener to track this request
     */
    public void share(MegaNode node, String email, int level, MegaRequestListenerInterface listener) {
        megaApi.share(node, email, level, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using his email
     *
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     *
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node The folder to share. It must be a non-root folder
     * @param email Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
     * and the user will be invited to register an account.
     *
     * @param level Permissions that are granted to the user
     * Valid values for this parameter:
     * - MegaShare::ACCESS_UNKNOWN = -1
     * Stop sharing a folder with this user
     *
     * - MegaShare::ACCESS_READ = 0
     * - MegaShare::ACCESS_READWRITE = 1
     * - MegaShare::ACCESS_FULL = 2
     * - MegaShare::ACCESS_OWNER = 3
     */
    public void share(MegaNode node, String email, int level) {
        megaApi.share(node, email, level);
    }

    /**
     * Import a public link to the account
     *
     * The associated request type with this request is MegaRequest::TYPE_IMPORT_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     * - MegaRequest::getParentHandle - Returns the folder that receives the imported file
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node in the account
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     * @param parent Parent folder for the imported file
     * @param listener MegaRequestListener to track this request
     */
    public void importFileLink(String megaFileLink, MegaNode parent, MegaRequestListenerInterface listener) {
        megaApi.importFileLink(megaFileLink, parent, createDelegateRequestListener(listener));
    }

    /**
     * Import a public link to the account
     *
     * The associated request type with this request is MegaRequest::TYPE_IMPORT_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     * - MegaRequest::getParentHandle - Returns the folder that receives the imported file
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node in the account
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     * @param parent Parent folder for the imported file
     */
    public void importFileLink(String megaFileLink, MegaNode parent) {
        megaApi.importFileLink(megaFileLink, parent);
    }

    /**
     * Decrypt password-protected public link
     *
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the encrypted public link to the file/folder
     * - MegaRequest::getPassword - Returns the password to decrypt the link
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Decrypted public link
     *
     * @param link Password/protected public link to a file/folder in MEGA
     * @param password Password to decrypt the link
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void decryptPasswordProtectedLink(String link, String password, MegaRequestListenerInterface listener) {
        megaApi.decryptPasswordProtectedLink(link, password, createDelegateRequestListener(listener));
    }

    /**
     * Decrypt password-protected public link
     *
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the encrypted public link to the file/folder
     * - MegaRequest::getPassword - Returns the password to decrypt the link
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Decrypted public link
     *
     * @param link Password/protected public link to a file/folder in MEGA
     * @param password Password to decrypt the link
     */
    public void decryptPasswordProtectedLink(String link, String password){
        megaApi.decryptPasswordProtectedLink(link, password);
    }

    /**
     * Encrypt public link with password
     *
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to be encrypted
     * - MegaRequest::getPassword - Returns the password to encrypt the link
     * - MegaRequest::getFlag - Returns true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Encrypted public link
     *
     * @param link Public link to be encrypted, including encryption key for the link
     * @param password Password to encrypt the link
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void encryptLinkWithPassword(String link, String password, MegaRequestListenerInterface listener) {
        megaApi.encryptLinkWithPassword(link, password, createDelegateRequestListener(listener));
    }

    /**
     * Encrypt public link with password
     *
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to be encrypted
     * - MegaRequest::getPassword - Returns the password to encrypt the link
     * - MegaRequest::getFlag - Returns true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Encrypted public link
     *
     * @param link Public link to be encrypted, including encryption key for the link
     * @param password Password to encrypt the link
     */
    public void encryptLinkWithPassword(String link, String password) {
        megaApi.encryptLinkWithPassword(link, password);
    }

    /**
     * Get a MegaNode from a public link to a file
     *
     * A public node can be imported using MegaApi::copyNode or downloaded using MegaApi::startDownload
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PUBLIC_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPublicMegaNode - Public MegaNode corresponding to the public link
     * - MegaRequest::getFlag - Return true if the provided key along the link is invalid.
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     * @param listener MegaRequestListener to track this request
     */
    public void getPublicNode(String megaFileLink, MegaRequestListenerInterface listener) {
        megaApi.getPublicNode(megaFileLink, createDelegateRequestListener(listener));
    }

    /**
     * Get a MegaNode from a public link to a file
     *
     * A public node can be imported using MegaApi::copyNode or downloaded using MegaApi::startDownload
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PUBLIC_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPublicMegaNode - Public MegaNode corresponding to the public link
     * - MegaRequest::getFlag - Return true if the provided key along the link is invalid.
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     */
    public void getPublicNode(String megaFileLink) {
        megaApi.getPublicNode(megaFileLink);
    }

    /**
     * Get the thumbnail of a node.
     * <p>
     * If the node does not have a thumbnail, the request fails with the MegaError.API_ENOENT
     * error code.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle() - Returns the handle of the node. <br>
     * - MegaRequest.getFile() - Returns the destination path. <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.ATTR_TYPE_THUMBNAIL.
     * 
     * @param node
     *            Node to get the thumbnail.
     * @param dstFilePath
     *            Destination path for the thumbnail.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
     *            will be used as the file name inside that folder. If the path does not finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void getThumbnail(MegaNode node, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getThumbnail(node, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the thumbnail of a node.
     * <p>
     * If the node does not have a thumbnail the request fails with the MegaError.API_ENOENT
     * error code.
     * 
     * @param node
     *            Node to get the thumbnail.
     * @param dstFilePath
     *            Destination path for the thumbnail.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
     *            will be used as the file name inside that folder. If the path does not finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     */
    public void getThumbnail(MegaNode node, String dstFilePath) {
        megaApi.getThumbnail(node, dstFilePath);
    }

    /**
     * Get the preview of a node.
     * <p>
     * If the node does not have a preview the request fails with the MegaError.API_ENOENT
     * error code.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle() - Returns the handle of the node. <br>
     * - MegaRequest.getFile() - Returns the destination path. <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.ATTR_TYPE_PREVIEW.
     * 
     * @param node
     *            Node to get the preview.
     * @param dstFilePath
     *            Destination path for the preview.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg")
     *            will be used as the file name inside that folder. If the path does not finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void getPreview(MegaNode node, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getPreview(node, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the preview of a node.
     * <p>
     * If the node does not have a preview the request fails with the MegaError.API_ENOENT
     * error code.
     * 
     * @param node
     *            Node to get the preview.
     * @param dstFilePath
     *            Destination path for the preview.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg")
     *            will be used as the file name inside that folder. If the path does not finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     */
    public void getPreview(MegaNode node, String dstFilePath) {
        megaApi.getPreview(node, dstFilePath);
    }

    /**
     * Get the avatar of a MegaUser.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getFile() - Returns the destination path. <br>
     * - MegaRequest.getEmail() - Returns the email of the user.
     * 
     * @param user
     *            MegaUser to get the avatar.
     * @param dstFilePath
     *            Destination path for the avatar. It has to be a path to a file, not to a folder.
     *            If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *            will be used as the file name inside that folder. If the path does not finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void getUserAvatar(MegaUser user, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getUserAvatar(user, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the avatar of a MegaUser.
     * 
     * @param user
     *            MegaUser to get the avatar.
     * @param dstFilePath
     *            Destination path for the avatar. It has to be a path to a file, not to a folder.
     *            If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *            will be used as the file name inside that folder. If the path does not finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     */
    public void getUserAvatar(MegaUser user, String dstFilePath) {
        megaApi.getUserAvatar(user, dstFilePath);
    }
    
    /**
     * Get the avatar of any user in MEGA
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
     *
     * @param user email_or_user Email or user handle (Base64 encoded) to get the avatar. If this parameter is
     * set to null, the avatar is obtained for the active account
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     * will be used as the file name inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     *
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void getUserAvatar(String email_or_handle, String dstFilePath, MegaRequestListenerInterface listener) {
    	megaApi.getUserAvatar(email_or_handle, dstFilePath, createDelegateRequestListener(listener));
    }
    
    /**
     * Get the avatar of any user in MEGA
     *
     * @param user email_or_user Email or user handle (Base64 encoded) to get the avatar. If this parameter is
     * set to null, the avatar is obtained for the active account
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     * will be used as the file name inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     */
    public void getUserAvatar(String email_or_handle, String dstFilePath) {
    	megaApi.getUserAvatar(email_or_handle, dstFilePath);
    }
    
    /**
     * Get the avatar of the active account
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email of the user
     *
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     * will be used as the file name inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     *
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void getUserAvatar(String dstFilePath, MegaRequestListenerInterface listener) {
    	megaApi.getUserAvatar(dstFilePath, createDelegateRequestListener(listener));
    }
    
    /**
     * Get the avatar of the active account
     *
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     * will be used as the file name inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     */
    public void getUserAvatar(String dstFilePath) {
    	megaApi.getUserAvatar(dstFilePath);
    }

    /**
     * Get the default color for the avatar.
     *
     * This color should be used only when the user doesn't have an avatar.
     *
     * You take the ownership of the returned value.
     *
     * @param user MegaUser to get the color of the avatar. If this parameter is set to NULL, the color
     *  is obtained for the active account.
     * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
     * If the user is not found, this function returns NULL.
     */
    public String getUserAvatarColor(MegaUser user){
        return megaApi.getUserAvatarColor(user);
    }

    /**
     * Get the default color for the avatar.
     *
     * This color should be used only when the user doesn't have an avatar.
     *
     * You take the ownership of the returned value.
     *
     * @param userhandle User handle (Base64 encoded) to get the avatar. If this parameter is
     * set to NULL, the avatar is obtained for the active account
     * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
     * If the user is not found (invalid userhandle), this function returns NULL.
     */
    public String getUserAvatarColor(String userhandle){
        return megaApi.getUserAvatarColor(userhandle);
    }

    /**
     * Get an attribute of a MegaUser.
     *
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param user MegaUser to get the attribute. If this parameter is set to NULL, the attribute
     * is obtained for the active account
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Get the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Get the lastname of the user (public)
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Get the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Get the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     * Get the signature of RSA public key of the user (public)
     * MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     * Get the signature of Cu25519 public key of the user (public)
     * MegaApi::USER_ATTR_LANGUAGE = 14
     * Get the preferred language of the user (private, non-encrypted)
     * MegaApi::USER_ATTR_PWD_REMINDER = 15
     * Get the password-reminder-dialog information (private, non-encrypted)
     * MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     * Get whether user has versions disabled or enabled (private, non-encrypted)
     * MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     * Get whether user generates rich-link messages or not (private)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_STORAGE_STATE = 21
     * Get the state of the storage (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Get whether the user has enabled send geolocation messages (private)
     * MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     * Get the settings for push notifications (private non-encrypted)
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getUserAttribute(MegaUser user, int type, MegaRequestListenerInterface listener) {
        megaApi.getUserAttribute(user, type, createDelegateRequestListener(listener));
    }

    /**
     * Get an attribute of a MegaUser.
     *
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param user MegaUser to get the attribute. If this parameter is set to NULL, the attribute
     * is obtained for the active account
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Get the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Get the lastname of the user (public)
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Get the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Get the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     * Get the signature of RSA public key of the user (public)
     * MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     * Get the signature of Cu25519 public key of the user (public)
     * MegaApi::USER_ATTR_LANGUAGE = 14
     * Get the preferred language of the user (private, non-encrypted)
     * MegaApi::USER_ATTR_PWD_REMINDER = 15
     * Get the password-reminder-dialog information (private, non-encrypted)
     * MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     * Get whether user has versions disabled or enabled (private, non-encrypted)
     * MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     * Get whether user generates rich-link messages or not (private)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_STORAGE_STATE = 21
     * Get the state of the storage (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Get whether the user has enabled send geolocation messages (private)
     * MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     * Get the settings for push notifications (private non-encrypted)
     */
    public void getUserAttribute(MegaUser user, int type) {
        megaApi.getUserAttribute(user, type);
    }

    /**
     * Get an attribute of any user in MEGA.
     *
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param email_or_handle Email or user handle (Base64 encoded) to get the attribute.
     * If this parameter is set to NULL, the attribute is obtained for the active account.
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Get the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Get the lastname of the user (public)
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Get the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Get the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     * Get the signature of RSA public key of the user (public)
     * MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     * Get the signature of Cu25519 public key of the user (public)
     * MegaApi::USER_ATTR_LANGUAGE = 14
     * Get the preferred language of the user (private, non-encrypted)
     * MegaApi::USER_ATTR_PWD_REMINDER = 15
     * Get the password-reminder-dialog information (private, non-encrypted)
     * MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     * Get whether user has versions disabled or enabled (private, non-encrypted)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_STORAGE_STATE = 21
     * Get the state of the storage (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Get whether the user has enabled send geolocation messages (private)
     * MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     * Get the settings for push notifications (private non-encrypted)
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getUserAttribute(String email_or_handle, int type, MegaRequestListenerInterface listener) {
    	megaApi.getUserAttribute(email_or_handle, type, createDelegateRequestListener(listener));
    }

    /**
     * Get an attribute of any user in MEGA.
     *
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param email_or_handle Email or user handle (Base64 encoded) to get the attribute.
     * If this parameter is set to NULL, the attribute is obtained for the active account.
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Get the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Get the lastname of the user (public)
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Get the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Get the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     * Get the signature of RSA public key of the user (public)
     * MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     * Get the signature of Cu25519 public key of the user (public)
     * MegaApi::USER_ATTR_LANGUAGE = 14
     * Get the preferred language of the user (private, non-encrypted)
     * MegaApi::USER_ATTR_PWD_REMINDER = 15
     * Get the password-reminder-dialog information (private, non-encrypted)
     * MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     * Get whether user has versions disabled or enabled (private, non-encrypted)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_STORAGE_STATE = 21
     * Get the state of the storage (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Get whether the user has enabled send geolocation messages (private)
     * MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     * Get the settings for push notifications (private non-encrypted)
     *
     */
    public void getUserAttribute(String email_or_handle, int type) {
    	megaApi.getUserAttribute(email_or_handle, type);
    }

    /**
     * Get an attribute of the current account.
     *
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Get the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Get the lastname of the user (public)
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Get the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Get the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     * Get the signature of RSA public key of the user (public)
     * MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     * Get the signature of Cu25519 public key of the user (public)
     * MegaApi::USER_ATTR_LANGUAGE = 14
     * Get the preferred language of the user (private, non-encrypted)
     * MegaApi::USER_ATTR_PWD_REMINDER = 15
     * Get the password-reminder-dialog information (private, non-encrypted)
     * MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     * Get whether user has versions disabled or enabled (private, non-encrypted)
     * MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     * Get whether user generates rich-link messages or not (private)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_STORAGE_STATE = 21
     * Get the state of the storage (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Get whether the user has enabled send geolocation messages (private)
     * MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     * Get the settings for push notifications (private non-encrypted)
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getUserAttribute(int type, MegaRequestListenerInterface listener) {
        megaApi.getUserAttribute(type, createDelegateRequestListener(listener));
    }

    /**
     * Get an attribute of the current account.
     *
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Get the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Get the lastname of the user (public)
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Get the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Get the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     * Get the signature of RSA public key of the user (public)
     * MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     * Get the signature of Cu25519 public key of the user (public)
     * MegaApi::USER_ATTR_LANGUAGE = 14
     * Get the preferred language of the user (private, non-encrypted)
     * MegaApi::USER_ATTR_PWD_REMINDER = 15
     * Get the password-reminder-dialog information (private, non-encrypted)
     * MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     * Get whether user has versions disabled or enabled (private, non-encrypted)
     * MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     * Get whether user generates rich-link messages or not (private)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_STORAGE_STATE = 21
     * Get the state of the storage (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Get whether the user has enabled send geolocation messages (private)
     * MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     * Get the settings for push notifications (private non-encrypted)
     *
     */
    public void getUserAttribute(int type) {
        megaApi.getUserAttribute(type);
    }

    /**
     * Cancel the retrieval of a thumbnail.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_ATTR_FILE.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle() - Returns the handle of the node. <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.ATTR_TYPE_THUMBNAIL.
     * 
     * @param node
     *            Node to cancel the retrieval of the thumbnail.
     * @param listener
     *            MegaRequestListener to track this request.
     * @see #getThumbnail(MegaNode node, String dstFilePath)
     */
    public void cancelGetThumbnail(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.cancelGetThumbnail(node, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the retrieval of a thumbnail.
     * 
     * @param node
     *            Node to cancel the retrieval of the thumbnail.
     * @see #getThumbnail(MegaNode node, String dstFilePath)
     */
    public void cancelGetThumbnail(MegaNode node) {
        megaApi.cancelGetThumbnail(node);
    }

    /**
     * Cancel the retrieval of a preview.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node. <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.ATTR_TYPE_PREVIEW.
     * 
     * @param node
     *            Node to cancel the retrieval of the preview.
     * @param listener
     *            MegaRequestListener to track this request.
     * @see MegaApi#getPreview(MegaNode node, String dstFilePath)
     */
    public void cancelGetPreview(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.cancelGetPreview(node, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the retrieval of a preview.
     * 
     * @param node
     *            Node to cancel the retrieval of the preview.
     * @see MegaApi#getPreview(MegaNode node, String dstFilePath)
     */
    public void cancelGetPreview(MegaNode node) {
        megaApi.cancelGetPreview(node);
    }

    /**
     * Set the thumbnail of a MegaNode.
     * 
     * The associated request type with this request is MegaRequest.TYPE_SET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle() - Returns the handle of the node. <br>
     * - MegaRequest.getFile() - Returns the source path. <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.ATTR_TYPE_THUMBNAIL.
     * 
     * @param node
     *            MegaNode to set the thumbnail.
     * @param srcFilePath
     *            Source path of the file that will be set as thumbnail.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void setThumbnail(MegaNode node, String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setThumbnail(node, srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the thumbnail of a MegaNode.
     * 
     * @param node
     *            MegaNode to set the thumbnail.
     * @param srcFilePath
     *            Source path of the file that will be set as thumbnail.
     */
    public void setThumbnail(MegaNode node, String srcFilePath) {
        megaApi.setThumbnail(node, srcFilePath);
    }

    /**
     * Set the preview of a MegaNode.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_SET_ATTR_FILE.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle() - Returns the handle of the node. <br>
     * - MegaRequest.getFile() - Returns the source path. <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.ATTR_TYPE_PREVIEW.
     * 
     * @param node
     *            MegaNode to set the preview.
     * @param srcFilePath
     *            Source path of the file that will be set as preview.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void setPreview(MegaNode node, String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setPreview(node, srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the preview of a MegaNode.
     * 
     * @param node
     *            MegaNode to set the preview.
     * @param srcFilePath
     *            Source path of the file that will be set as preview.
     */
    public void setPreview(MegaNode node, String srcFilePath) {
        megaApi.setPreview(node, srcFilePath);
    }

    /**
     * Set the avatar of the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_SET_ATTR_USER.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getFile() - Returns the source path.
     * 
     * @param srcFilePath
     *            Source path of the file that will be set as avatar.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void setAvatar(String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setAvatar(srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the avatar of the MEGA account.
     * 
     * @param srcFilePath
     *            Source path of the file that will be set as avatar.
     */
    public void setAvatar(String srcFilePath) {
        megaApi.setAvatar(srcFilePath);
    }

    /**
     * Set a public attribute of the current user
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Set the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Set the lastname of the user (public)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Set the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Set the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *
     * If the MEGA account is a sub-user business account, and the value of the parameter
     * type is equal to MegaApi::USER_ATTR_FIRSTNAME or MegaApi::USER_ATTR_LASTNAME
     * onRequestFinish will be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param value New attribute value
     * @param listener MegaRequestListener to track this request
     */
    public void setUserAttribute(int type, String value, MegaRequestListenerInterface listener) {
        megaApi.setUserAttribute(type, value, createDelegateRequestListener(listener));
    }

    /**
     * Set a public attribute of the current user
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_FIRSTNAME = 1
     * Set the firstname of the user (public)
     * MegaApi::USER_ATTR_LASTNAME = 2
     * Set the lastname of the user (public)
     * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     * Set the public key Ed25519 of the user (public)
     * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     * Set the public key Cu25519 of the user (public)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *
     * If the MEGA account is a sub-user business account, and the value of the parameter
     * type is equal to MegaApi::USER_ATTR_FIRSTNAME or MegaApi::USER_ATTR_LASTNAME
     * onRequestFinish will be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param value New attribute value
     */
    public void setUserAttribute(int type, String value) {
        megaApi.setUserAttribute(type, value);
    }

    /**
     * Set a custom attribute for the node
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getName - Returns the name of the custom attribute
     * - MegaRequest::getText - Returns the text for the attribute
     * - MegaRequest::getFlag - Returns false (not official attribute)
     *
     * The attribute name must be an UTF8 string with between 1 and 7 bytes
     * If the attribute already has a value, it will be replaced
     * If value is NULL, the attribute will be removed from the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node that will receive the attribute
     * @param attrName Name of the custom attribute.
     * The length of this parameter must be between 1 and 7 UTF8 bytes
     * @param value Value for the attribute
     * @param listener MegaRequestListener to track this request
     */
    public void setCustomNodeAttribute(MegaNode node, String attrName, String value, MegaRequestListenerInterface listener) {
    	megaApi.setCustomNodeAttribute(node, attrName, value, createDelegateRequestListener(listener));
    }

    /**
     * Set a custom attribute for the node
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getName - Returns the name of the custom attribute
     * - MegaRequest::getText - Returns the text for the attribute
     * - MegaRequest::getFlag - Returns false (not official attribute)
     *
     * The attribute name must be an UTF8 string with between 1 and 7 bytes
     * If the attribute already has a value, it will be replaced
     * If value is NULL, the attribute will be removed from the node
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node that will receive the attribute
     * @param attrName Name of the custom attribute.
     * The length of this parameter must be between 1 and 7 UTF8 bytes
     * @param value Value for the attribute
     */
    public void setCustomNodeAttribute(MegaNode node, String attrName, String value) {
    	megaApi.setCustomNodeAttribute(node, attrName, value);
    }

    /**
     * Set the GPS coordinates of image files as a node attribute.
     *
     * To remove the existing coordinates, set both the latitude and longitude to
     * the value MegaNode::INVALID_COORDINATE.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_COORDINATES
     * - MegaRequest::getNumDetails - Returns the longitude, scaled to integer in the range of [0, 2^24]
     * - MegaRequest::getTransferTag() - Returns the latitude, scaled to integer in the range of [0, 2^24)
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node that will receive the information.
     * @param latitude Latitude in signed decimal degrees notation
     * @param longitude Longitude in signed decimal degrees notation
     * @param listener MegaRequestListener to track this request
     */
    public void setNodeCoordinates(MegaNode node, double latitude, double longitude,  MegaRequestListenerInterface listener){
        megaApi.setNodeCoordinates(node, latitude, longitude, createDelegateRequestListener(listener));
    }

    /**
     * Generate a public link of a file/folder in MEGA
     *
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node MegaNode to get the public link
     * @param listener MegaRequestListener to track this request
     */
    public void exportNode(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.exportNode(node, createDelegateRequestListener(listener));
    }

    /**
     * Generate a public link of a file/folder in MEGA
     *
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node MegaNode to get the public link
     */
    public void exportNode(MegaNode node) {
        megaApi.exportNode(node);
    }

    /**
     * Generate a temporary public link of a file/folder in MEGA
     *
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node MegaNode to get the public link
     * @param expireTime Unix timestamp until the public link will be valid
     * @param listener MegaRequestListener to track this request
     *
     * A Unix timestamp represents the number of seconds since 00:00 hours, Jan 1, 1970 UTC
     */
    public void exportNode(MegaNode node, int expireTime, MegaRequestListenerInterface listener) {
        megaApi.exportNode(node, expireTime, createDelegateRequestListener(listener));
    }

    /**
     * Generate a temporary public link of a file/folder in MEGA
     *
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node MegaNode to get the public link
     * @param expireTime Unix timestamp until the public link will be valid
     *
     * A Unix timestamp represents the number of seconds since 00:00 hours, Jan 1, 1970 UTC
     */
    public void exportNode(MegaNode node, int expireTime) {
        megaApi.exportNode(node, expireTime);
    }

    /**
     * Stop sharing a file/folder
     *
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns false
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node MegaNode to stop sharing
     * @param listener MegaRequestListener to track this request
     */
    public void disableExport(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.disableExport(node, createDelegateRequestListener(listener));
    }

    /**
     * Stop sharing a file/folder
     *
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns false
     *
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node MegaNode to stop sharing
     */
    public void disableExport(MegaNode node) {
        megaApi.disableExport(node);
    }

    /**
     * Fetch the filesystem in MEGA.
     * <p>
     * The MegaApi object must be logged in in an account or a public folder
     * to successfully complete this request.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_FETCH_NODES.
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void fetchNodes(MegaRequestListenerInterface listener) {
        megaApi.fetchNodes(createDelegateRequestListener(listener));
    }

    /**
     * Fetch the filesystem in MEGA.
     * <p>
     * The MegaApi object must be logged in in an account or a public folder
     * to successfully complete this request.
     */
    public void fetchNodes() {
        megaApi.fetchNodes();
    }

    /**
     * Get details about the MEGA account
     *
     * Only basic data will be available. If you can get more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     *
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     *
     * The available flags are:
     *  - storage quota: (numDetails & 0x01)
     *  - transfer quota: (numDetails & 0x02)
     *  - pro level: (numDetails & 0x04)
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getAccountDetails(MegaRequestListenerInterface listener) {
        megaApi.getAccountDetails(createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account
     *
     * Only basic data will be available. If you can get more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     *
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     *
     * The available flags are:
     *  - storage quota: (numDetails & 0x01)
     *  - transfer quota: (numDetails & 0x02)
     *  - pro level: (numDetails & 0x04)
     */
    public void getAccountDetails() {
        megaApi.getAccountDetails();
    }

    /**
     * Get details about the MEGA account
     *
     * Only basic data will be available. If you need more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     *
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     *
     * Use this version of the function to get just the details you need, to minimise server load
     * and keep the system highly available for all. At least one flag must be set.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     *
     * The available flags are:
     *  - storage quota: (numDetails & 0x01)
     *  - transfer quota: (numDetails & 0x02)
     *  - pro level: (numDetails & 0x04)
     *
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param storage If true, account storage details are requested
     * @param transfer If true, account transfer details are requested
     * @param pro If true, pro level of account is requested
     * @param listener MegaRequestListener to track this request
     */
    public void getSpecificAccountDetails(boolean storage, boolean transfer, boolean pro, MegaRequestListenerInterface listener) {
        megaApi.getSpecificAccountDetails(storage, transfer, pro, -1, createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account
     *
     * Only basic data will be available. If you need more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     *
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     *
     * Use this version of the function to get just the details you need, to minimise server load
     * and keep the system highly available for all. At least one flag must be set.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     *
     * The available flags are:
     *  - storage quota: (numDetails & 0x01)
     *  - transfer quota: (numDetails & 0x02)
     *  - pro level: (numDetails & 0x04)
     *
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param storage If true, account storage details are requested
     * @param transfer If true, account transfer details are requested
     * @param pro If true, pro level of account is requested
     */
    public void getSpecificAccountDetails(boolean storage, boolean transfer, boolean pro) {
        megaApi.getSpecificAccountDetails(storage, transfer, pro, -1);
    }

    /**
     * Get details about the MEGA account
     *
     * This function allows to optionally get data about sessions, transactions and purchases related to the account.
     *
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     *
     * The available flags are:
     *  - transactions: (numDetails & 0x08)
     *  - purchases: (numDetails & 0x10)
     *  - sessions: (numDetails & 0x020)
     *
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param sessions If true, sessions are requested
     * @param purchases If true, purchases are requested
     * @param transactions If true, transactions are requested
     * @param listener MegaRequestListener to track this request
     */
    public void getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions, MegaRequestListenerInterface listener) {
        megaApi.getExtendedAccountDetails(sessions, purchases, transactions, createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account
     *
     * This function allows to optionally get data about sessions, transactions and purchases related to the account.
     *
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     *
     * The available flags are:
     *  - transactions: (numDetails & 0x08)
     *  - purchases: (numDetails & 0x10)
     *  - sessions: (numDetails & 0x020)
     *
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param sessions If true, sessions are requested
     * @param purchases If true, purchases are requested
     * @param transactions If true, transactions are requested
     */
    public void getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions) {
        megaApi.getExtendedAccountDetails(sessions, purchases, transactions);
    }

    /**
     * Get the available pricing plans to upgrade a MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_PRICING.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getPricing() - MegaPricing object with all pricing plans.
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void getPricing(MegaRequestListenerInterface listener) {
        megaApi.getPricing(createDelegateRequestListener(listener));
    }

    /**
     * Get the available pricing plans to upgrade a MEGA account.
     */
    public void getPricing() {
        megaApi.getPricing();
    }

    /**
     * Get the payment URL for an upgrade
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     * @param listener MegaRequestListener to track this request
     *
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, MegaRequestListenerInterface listener) {
        megaApi.getPaymentId(productHandle, createDelegateRequestListener(listener));
    }

    /**
     * Get the payment URL for an upgrade
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     *
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle) {
        megaApi.getPaymentId(productHandle);
    }

    /**
     * Get the payment URL for an upgrade
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param listener MegaRequestListener to track this request
     *
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle, MegaRequestListenerInterface listener) {
        megaApi.getPaymentId(productHandle, lastPublicHandle, createDelegateRequestListener(listener));
    }

    /**
     * Get the payment URL for an upgrade
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     *
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle) {
        megaApi.getPaymentId(productHandle, lastPublicHandle);
    }

    /**
     * Get the payment URL for an upgrade
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *      - MegaApi::AFFILIATE_TYPE_ID = 1
     *      - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *      - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *      - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     *
     * @param lastAccessTimestamp Timestamp of the last access
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle, int lastPublicHandleType,
                             long lastAccessTimestamp, MegaRequestListenerInterface listener) {
        megaApi.getPaymentId(productHandle, lastPublicHandle, lastPublicHandleType,
                lastAccessTimestamp, createDelegateRequestListener(listener));
    }

    /**
     * Get the payment URL for an upgrade
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *      - MegaApi::AFFILIATE_TYPE_ID = 1
     *      - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *      - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *      - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     *
     * @param lastAccessTimestamp Timestamp of the last access
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle, int lastPublicHandleType, long lastAccessTimestamp) {
        megaApi.getPaymentId(productHandle, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp);
    }

    /**
     * Upgrade an account.
     *
     * @param productHandle Product handle to purchase.
     * It is possible to get all pricing plans with their product handles using
     * MegaApi.getPricing().
     *
     * The associated request type with this request is MegaRequest.TYPE_UPGRADE_ACCOUNT.
     *
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle() - Returns the handle of the product. <br>
     * - MegaRequest.getNumber() - Returns the payment method.
     *
     * @param paymentMethod Payment method.
     * Valid values are: <br>
     * - MegaApi.PAYMENT_METHOD_BALANCE = 0.
     * Use the account balance for the payment. <br>
     *
     * - MegaApi.PAYMENT_METHOD_CREDIT_CARD = 8.
     * Complete the payment with your credit card. Use MegaApi.creditCardStore to add
     * a credit card to your account.
     *
     * @param listener MegaRequestListener to track this request.
     */
    public void upgradeAccount(long productHandle, int paymentMethod, MegaRequestListenerInterface listener) {
        megaApi.upgradeAccount(productHandle, paymentMethod, createDelegateRequestListener(listener));
    }

    /**
     * Upgrade an account.
     *
     * @param productHandle Product handle to purchase.
     * It is possible to get all pricing plans with their product handles using
     * MegaApi.getPricing().
     *
     * @param paymentMethod Payment method.
     * Valid values are: <br>
     * - MegaApi.PAYMENT_METHOD_BALANCE = 0.
     * Use the account balance for the payment. <br>
     *
     * - MegaApi.PAYMENT_METHOD_CREDIT_CARD = 8.
     * Complete the payment with your credit card. Use MegaApi.creditCardStore() to add
     * a credit card to your account.
     */
    public void upgradeAccount(long productHandle, int paymentMethod) {
        megaApi.upgradeAccount(productHandle, paymentMethod);
    }

    /**
     * Submit a purchase receipt for verification
     *
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     *
     * @param gateway Payment gateway
     * Currently supported payment gateways are:
     * - MegaApi::PAYMENT_METHOD_ITUNES = 2
     * - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     * - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     *
     * @param receipt Purchase receipt
     * @param listener MegaRequestListener to track this request
     */
    public void submitPurchaseReceipt(int gateway, String receipt, MegaRequestListenerInterface listener) {
        megaApi.submitPurchaseReceipt(gateway, receipt, createDelegateRequestListener(listener));
    }

    /**
     * Submit a purchase receipt for verification
     *
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     *
     * @param gateway Payment gateway
     * Currently supported payment gateways are:
     * - MegaApi::PAYMENT_METHOD_ITUNES = 2
     * - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     * - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     *
     * @param receipt Purchase receipt
     */
    public void submitPurchaseReceipt(int gateway, String receipt) {
        megaApi.submitPurchaseReceipt(gateway, receipt);
    }

    /**
     * Submit a purchase receipt for verification
     *
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     *
     * @param gateway Payment gateway
     * Currently supported payment gateways are:
     * - MegaApi::PAYMENT_METHOD_ITUNES = 2
     * - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     * - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     *
     * @param receipt Purchase receipt
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param listener MegaRequestListener to track this request
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle, MegaRequestListenerInterface listener) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle, createDelegateRequestListener(listener));
    }

    /**
     * Submit a purchase receipt for verification
     *
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     *
     * @param gateway Payment gateway
     * Currently supported payment gateways are:
     * - MegaApi::PAYMENT_METHOD_ITUNES = 2
     * - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     * - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     *
     * @param receipt Purchase receipt
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle);
    }

    /**
     * Submit a purchase receipt for verification
     *
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param gateway Payment gateway
     * Currently supported payment gateways are:
     * - MegaApi::PAYMENT_METHOD_ITUNES = 2
     * - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     * - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     *
     * @param receipt Purchase receipt
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *      - MegaApi::AFFILIATE_TYPE_ID = 1
     *      - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *      - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *      - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     *
     * @param lastAccessTimestamp Timestamp of the last access
     * @param listener MegaRequestListener to track this request
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle, int lastPublicHandleType,
                               long lastAccessTimestamp, MegaRequestListenerInterface listener) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle, lastPublicHandleType,
                lastAccessTimestamp, createDelegateRequestListener(listener));
    }

    /**
     * Submit a purchase receipt for verification
     *
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param gateway Payment gateway
     * Currently supported payment gateways are:
     * - MegaApi::PAYMENT_METHOD_ITUNES = 2
     * - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     * - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     *
     * @param receipt Purchase receipt
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *      - MegaApi::AFFILIATE_TYPE_ID = 1
     *      - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *      - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *      - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     *
     * @param lastAccessTimestamp Timestamp of the last access
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle, int lastPublicHandleType, long lastAccessTimestamp) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp);
    }

    /**
     * Store a credit card.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CREDIT_CARD_STORE.
     *
     * @param address1 Billing address.
     * @param address2 Second line of the billing address (optional).
     * @param city City of the billing address.
     * @param province Province of the billing address.
     * @param country Country of the billing address.
     * @param postalcode Postal code of the billing address.
     * @param firstname Firstname of the owner of the credit card.
     * @param lastname Lastname of the owner of the credit card.
     * @param creditcard Credit card number. Only digits, no spaces nor dashes.
     * @param expire_month Expire month of the credit card. Must have two digits ("03" for example).
     * @param expire_year Expire year of the credit card. Must have four digits ("2010" for example).
     * @param cv2 Security code of the credit card (3 digits).
     * @param listener MegaRequestListener to track this request.
     */
    public void creditCardStore(String address1, String address2, String city, String province, String country, String postalcode, String firstname, String lastname, String creditcard, String expire_month, String expire_year, String cv2, MegaRequestListenerInterface listener) {
        megaApi.creditCardStore(address1, address2, city, province, country, postalcode, firstname, lastname, creditcard, expire_month, expire_year, cv2, createDelegateRequestListener(listener));
    }

    /**
     * Store a credit card.
     *
     * @param address1 Billing address.
     * @param address2 Second line of the billing address (optional).
     * @param city City of the billing address.
     * @param province Province of the billing address.
     * @param country Country of the billing address.
     * @param postalcode Postal code of the billing address.
     * @param firstname Firstname of the owner of the credit card.
     * @param lastname Lastname of the owner of the credit card.
     * @param creditcard Credit card number. Only digits, no spaces nor dashes.
     * @param expire_month Expire month of the credit card. Must have two digits ("03" for example).
     * @param expire_year Expire year of the credit card. Must have four digits ("2010" for example).
     * @param cv2 Security code of the credit card (3 digits).
     */
    public void creditCardStore(String address1, String address2, String city, String province, String country, String postalcode, String firstname, String lastname, String creditcard, String expire_month, String expire_year, String cv2) {
        megaApi.creditCardStore(address1, address2, city, province, country, postalcode, firstname, lastname, creditcard, expire_month, expire_year, cv2);
    }

    /**
     * Get the credit card subscriptions of the account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getNumber() - Number of credit card subscriptions.
     *
     * @param listener MegaRequestListener to track this request.
     */
    public void creditCardQuerySubscriptions(MegaRequestListenerInterface listener) {
        megaApi.creditCardQuerySubscriptions(createDelegateRequestListener(listener));
    }

    /**
     * Get the credit card subscriptions of the account.
     *
     */
    public void creditCardQuerySubscriptions() {
        megaApi.creditCardQuerySubscriptions();
    }

    /**
     * Cancel credit card subscriptions of the account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS.
     *
     * @param reason Reason for the cancellation. It can be null.
     * @param listener MegaRequestListener to track this request.
     */
    public void creditCardCancelSubscriptions(String reason, MegaRequestListenerInterface listener) {
        megaApi.creditCardCancelSubscriptions(reason, createDelegateRequestListener(listener));
    }

    /**
     * Cancel credit card subscriptions of the account.
     *
     * @param reason Reason for the cancellation. It can be null.
     *
     */
    public void creditCardCancelSubscriptions(String reason) {
        megaApi.creditCardCancelSubscriptions(reason);
    }

    /**
     * Get the available payment methods.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_PAYMENT_METHODS.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish() when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getNumber() - Bitfield with available payment methods.
     *
     * To identify if a payment method is available, the following check can be performed: <br>
     * (request.getNumber() & (1 << MegaApiJava.PAYMENT_METHOD_CREDIT_CARD) != 0).
     *
     * @param listener MegaRequestListener to track this request.
     */
    public void getPaymentMethods(MegaRequestListenerInterface listener) {
        megaApi.getPaymentMethods(createDelegateRequestListener(listener));
    }

    /**
     * Get the available payment methods.
     */
    public void getPaymentMethods() {
        megaApi.getPaymentMethods();
    }

    /**
     * Export the master key of the account.
     * <p>
     * The returned value is a Base64-encoded string.
     * <p>
     * With the master key, it's possible to start the recovery of an account when the
     * password is lost: <br>
     * - https://mega.co.nz/#recovery.
     * 
     * @return Base64-encoded master key.
     */
    public String exportMasterKey() {
        return megaApi.exportMasterKey();
    }

    /**
     * Notify the user has exported the master key
     *
     * This function should be called when the user exports the master key by
     * clicking on "Copy" or "Save file" options.
     *
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember the user has a backup of his/her master key. In consequence,
     * MEGA will not ask the user to remind the password for the account.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void masterKeyExported(MegaRequestListenerInterface listener){
        megaApi.masterKeyExported(createDelegateRequestListener(listener));
    }

    /**
     * Check if the master key has been exported
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getAccess - Returns true if the master key has been exported
     *
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */

    public void isMasterKeyExported (MegaRequestListenerInterface listener) {
        megaApi.isMasterKeyExported(createDelegateRequestListener(listener));
    }

    /**
     * Notify the user has successfully checked his password
     *
     * This function should be called when the user demonstrates that he remembers
     * the password to access the account
     *
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember this event. In consequence, MEGA will not continue asking the user
     * to remind the password for the account in a short time.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void passwordReminderDialogSucceeded(MegaRequestListenerInterface listener){
        megaApi.passwordReminderDialogSucceeded(createDelegateRequestListener(listener));
    }

    /**
     * Notify the user has successfully skipped the password check
     *
     * This function should be called when the user skips the verification of
     * the password to access the account
     *
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember this event. In consequence, MEGA will not continue asking the user
     * to remind the password for the account in a short time.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void passwordReminderDialogSkipped(MegaRequestListenerInterface listener){
        megaApi.passwordReminderDialogSkipped(createDelegateRequestListener(listener));
    }

    /**
     * Notify the user wants to totally disable the password check
     *
     * This function should be called when the user rejects to verify that he remembers
     * the password to access the account and doesn't want to see the reminder again.
     *
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember this event. In consequence, MEGA will not ask the user
     * to remind the password for the account again.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void passwordReminderDialogBlocked(MegaRequestListenerInterface listener){
        megaApi.passwordReminderDialogBlocked(createDelegateRequestListener(listener));
    }

    /**
     * Check if the app should show the password reminder dialog to the user
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if the password reminder dialog should be shown
     *
     * @param atLogout True if the check is being done just before a logout
     * @param listener MegaRequestListener to track this request
     */
    public void shouldShowPasswordReminderDialog(boolean atLogout, MegaRequestListenerInterface listener){
        megaApi.shouldShowPasswordReminderDialog(atLogout, createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable the generation of rich previews
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param enable True to enable the generation of rich previews
     * @param listener MegaRequestListener to track this request
     */
    public void enableRichPreviews(boolean enable, MegaRequestListenerInterface listener){
        megaApi.enableRichPreviews(enable, createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable the generation of rich previews
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param enable True to enable the generation of rich previews
     */
    public void enableRichPreviews(boolean enable){
        megaApi.enableRichPreviews(enable);
    }

    /**
     * Check if rich previews are automatically generated
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns zero
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if generation of rich previews is enabled
     * - MegaRequest::getMegaStringMap - Returns the raw content of the atribute: [<key><value>]*
     *
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (false).
     *
     * @param listener MegaRequestListener to track this request
     */
    public void isRichPreviewsEnabled(MegaRequestListenerInterface listener){
        megaApi.isRichPreviewsEnabled(createDelegateRequestListener(listener));
    }

    /**
     * Check if rich previews are automatically generated
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns zero
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if generation of rich previews is enabled
     * - MegaRequest::getMegaStringMap - Returns the raw content of the atribute: [<key><value>]*
     *
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (false).
     *
     */
    public void isRichPreviewsEnabled(){
        megaApi.isRichPreviewsEnabled();
    }

    /**
     * Check if the app should show the rich link warning dialog to the user
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns one
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if it is necessary to show the rich link warning
     * - MegaRequest::getNumber - Returns the number of times that user has indicated that doesn't want
     * modify the message with a rich link. If number is bigger than three, the extra option "Never"
     * must be added to the warning dialog.
     * - MegaRequest::getMegaStringMap - Returns the raw content of the atribute: [<key><value>]*
     *
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (true).
     *
     * @param listener MegaRequestListener to track this request
     */
    public void shouldShowRichLinkWarning(MegaRequestListenerInterface listener){
        megaApi.shouldShowRichLinkWarning(createDelegateRequestListener(listener));
    }

    /**
     * Check if the app should show the rich link warning dialog to the user
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns one
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if it is necessary to show the rich link warning
     * - MegaRequest::getNumber - Returns the number of times that user has indicated that doesn't want
     * modify the message with a rich link. If number is bigger than three, the extra option "Never"
     * must be added to the warning dialog.
     * - MegaRequest::getMegaStringMap - Returns the raw content of the atribute: [<key><value>]*
     *
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (true).
     *
     */
    public void shouldShowRichLinkWarning(){

        megaApi.shouldShowRichLinkWarning();
    }

    /**
     * Set the number of times "Not now" option has been selected in the rich link warning dialog
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param value Number of times "Not now" option has been selected
     * @param listener MegaRequestListener to track this request
     */
    public void setRichLinkWarningCounterValue(int value, MegaRequestListenerInterface listener){
        megaApi.setRichLinkWarningCounterValue(value, createDelegateRequestListener(listener));
    }

    /**
     * Set the number of times "Not now" option has been selected in the rich link warning dialog
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param value Number of times "Not now" option has been selected
     */
    public void setRichLinkWarningCounterValue(int value){
        megaApi.setRichLinkWarningCounterValue(value);
    }

    /**
     * Enable the sending of geolocation messages
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_GEOLOCATION
     *
     * @param listener MegaRequestListener to track this request
     */
    public void enableGeolocation(MegaRequestListenerInterface listener){
        megaApi.enableGeolocation(createDelegateRequestListener(listener));
    }

    /**
     * Check if the sending of geolocation messages is enabled
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_GEOLOCATION
     *
     * Sending a Geolocation message is enabled if the MegaRequest object, received in onRequestFinish,
     * has error code MegaError::API_OK. In other cases, send geolocation messages is not enabled and
     * the application has to answer before send a message of this type.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void isGeolocationEnabled(MegaRequestListenerInterface listener){
        megaApi.isGeolocationEnabled(createDelegateRequestListener(listener));
    }

    /**
     * Set My Chat Files target folder.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER
     * - MegaRequest::getMegaStringMap - Returns a MegaStringMap.
     * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
     *
     * @param nodehandle MegaHandle of the node to be used as target folder
     * @param listener MegaRequestListener to track this request
     */
    public void setMyChatFilesFolder(long nodehandle, MegaRequestListenerInterface listener) {
        megaApi.setMyChatFilesFolder(nodehandle, createDelegateRequestListener(listener));
    }

    /**
     * Gets My chat files target folder.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodehandle - Returns the handle of the node where My Chat Files are stored
     *
     * If the folder is not set, the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getMyChatFilesFolder(MegaRequestListenerInterface listener){
        megaApi.getMyChatFilesFolder(createDelegateRequestListener(listener));
    }

    /**
     * Set Camera Uploads primary target folder.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getFlag - Returns false
     * - MegaRequest::getNodehandle - Returns the provided node handle
     * - MegaRequest::getMegaStringMap - Returns a MegaStringMap.
     * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
     *
     * @param nodehandle MegaHandle of the node to be used as primary target folder
     * @param listener MegaRequestListener to track this request
     */
    public void setCameraUploadsFolder(long nodehandle, MegaRequestListenerInterface listener){
        megaApi.setCameraUploadsFolder(nodehandle, createDelegateRequestListener(listener));
    }

    /**
     * Set Camera Uploads secondary target folder.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getFlag - Returns true
     * - MegaRequest::getNodehandle - Returns the provided node handle
     * - MegaRequest::getMegaStringMap - Returns a MegaStringMap.
     * The key "sh" in the map contains the nodehandle specified as parameter encoded in B64
     *
     * @param nodehandle MegaHandle of the node to be used as secondary target folder
     * @param listener MegaRequestListener to track this request
     */

    public void setCameraUploadsFolderSecondary(long nodehandle, MegaRequestListenerInterface listener){
        megaApi.setCameraUploadsFolderSecondary(nodehandle, createDelegateRequestListener(listener));
    }

    /**
     * Set Camera Uploads for both primary and secondary target folder.
     *
     * If only one of the target folders wants to be set, simply pass a INVALID_HANDLE to
     * as the other target folder and it will remain untouched.
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getNodehandle - Returns the provided node handle for primary folder
     * - MegaRequest::getParentHandle - Returns the provided node handle for secondary folder
     *
     * @param primaryFolder MegaHandle of the node to be used as primary target folder
     * @param secondaryFolder MegaHandle of the node to be used as secondary target folder
     * @param listener MegaRequestListener to track this request
     */
    public void setCameraUploadsFolders(long primaryFolder, long secondaryFolder, MegaRequestListenerInterface listener) {
        megaApi.setCameraUploadsFolders(primaryFolder, secondaryFolder, createDelegateRequestListener(listener));
    }

    /**
     * Gets Camera Uploads primary target folder.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getFlag - Returns false
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodehandle - Returns the handle of the primary node where Camera Uploads files are stored
     *
     * If the folder is not set, the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getCameraUploadsFolder(MegaRequestListenerInterface listener){
        megaApi.getCameraUploadsFolder(createDelegateRequestListener(listener));
    }

    /**
     * Gets Camera Uploads secondary target folder.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getFlag - Returns true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodehandle - Returns the handle of the secondary node where Camera Uploads files are stored
     *
     * If the secondary folder is not set, the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getCameraUploadsFolderSecondary(MegaRequestListenerInterface listener){
        megaApi.getCameraUploadsFolderSecondary(createDelegateRequestListener(listener));
    }

    /**
     * Gets the alias for an user
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_ALIAS
     * - MegaRequest::getNodeHandle - user handle in binary
     * - MegaRequest::getText - user handle encoded in B64
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns the user alias
     *
     * If the user alias doesn't exists the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param uh handle of the user in binary
     * @param listener MegaRequestListener to track this request
     */
    public void getUserAlias(long uh, MegaRequestListenerInterface listener) {
        megaApi.getUserAlias(uh, createDelegateRequestListener(listener));
    }
    /**
     * Set or reset an alias for a user
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_ALIAS
     * - MegaRequest::getNodeHandle - Returns the user handle in binary
     * - MegaRequest::getText - Returns the user alias
     *
     * @param uh handle of the user in binary
     * @param alias the user alias, or null to reset the existing
     * @param listener MegaRequestListener to track this request
     */
    public void setUserAlias(long uh, String alias, MegaRequestListenerInterface listener) {
        megaApi.setUserAlias(uh, alias, createDelegateRequestListener(listener));
    }

    /**
     * Get the number of days for rubbish-bin cleaning scheduler
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RUBBISH_TIME
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler.
     * Zero means that the rubbish-bin cleaning scheduler is disabled (only if the account is PRO)
     * Any negative value means that the configured value is invalid.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getRubbishBinAutopurgePeriod(MegaRequestListenerInterface listener){
        megaApi.getRubbishBinAutopurgePeriod(createDelegateRequestListener(listener));
    }

    /**
     * Set the number of days for rubbish-bin cleaning scheduler
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RUBBISH_TIME
     * - MegaRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler passed as parameter
     *
     * @param days Number of days for rubbish-bin cleaning scheduler. It must be >= 0.
     * The value zero disables the rubbish-bin cleaning scheduler (only for PRO accounts).
     *
     * @param listener MegaRequestListener to track this request
     */
    public void setRubbishBinAutopurgePeriod(int days, MegaRequestListenerInterface listener){
        megaApi.setRubbishBinAutopurgePeriod(days, createDelegateRequestListener(listener));
    }

    /**
     * Change the password of the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getPassword - Returns the old password (if it was passed as parameter)
     * - MegaRequest::getNewPassword - Returns the new password
     *
     * @param oldPassword Old password (optional, it can be NULL to not check the old password)
     * @param newPassword New password
     * @param listener MegaRequestListener to track this request
     */
    public void changePassword(String oldPassword, String newPassword, MegaRequestListenerInterface listener) {
        megaApi.changePassword(oldPassword, newPassword, createDelegateRequestListener(listener));
    }

    /**
     * Change the password of the MEGA account
     *
     * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getPassword - Returns the old password (if it was passed as parameter)
     * - MegaRequest::getNewPassword - Returns the new password
     *
     * @param oldPassword Old password (optional, it can be NULL to not check the old password)
     * @param newPassword New password
     */
    public void changePassword(String oldPassword, String newPassword) {
        megaApi.changePassword(oldPassword, newPassword);
    }

    /**
     * Invite another person to be your MEGA contact.
     * <p>
     * The user does not need to be registered with MEGA. If the email is not associated with
     * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_INVITE_CONTACT.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail() - Returns the email of the contact. <br>
     * - MegaRequest.getText() - Returns the text of the invitation.
     *
     * @param email Email of the new contact.
     * @param message Message for the user (can be null).
     * @param action Action for this contact request. Valid values are: <br>
     * - MegaContactRequest.INVITE_ACTION_ADD = 0. <br>
     * - MegaContactRequest.INVITE_ACTION_DELETE = 1. <br>
     * - MegaContactRequest.INVITE_ACTION_REMIND = 2.
     *
     * @param listener MegaRequestListenerInterface to track this request.
     */
    public void inviteContact(String email, String message, int action, MegaRequestListenerInterface listener) {
        megaApi.inviteContact(email, message, action, createDelegateRequestListener(listener));
    }

    /**
     * Invite another person to be your MEGA contact.
     * <p>
     * The user does not need to be registered on MEGA. If the email is not associated with
     * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
     *
     * @param email Email of the new contact.
     * @param message Message for the user (can be null).
     * @param action Action for this contact request. Valid values are: <br>
     * - MegaContactRequest.INVITE_ACTION_ADD = 0. <br>
     * - MegaContactRequest.INVITE_ACTION_DELETE = 1. <br>
     * - MegaContactRequest.INVITE_ACTION_REMIND = 2.
     */
    public void inviteContact(String email, String message, int action) {
        megaApi.inviteContact(email, message, action);
    }

    /**
     * Invite another person to be your MEGA contact using a contact link handle
     *
     * The associated request type with this request is MegaRequest::TYPE_INVITE_CONTACT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getText - Returns the text of the invitation
     * - MegaRequest::getNumber - Returns the action
     * - MegaRequest::getNodeHandle - Returns the contact link handle
     *
     * Sending a reminder within a two week period since you started or your last reminder will
     * fail the API returning the error code MegaError::API_EACCESS.
     *
     * @param email Email of the new contact
     * @param message Message for the user (can be NULL)
     * @param action Action for this contact request. Valid values are:
     * - MegaContactRequest::INVITE_ACTION_ADD = 0
     * - MegaContactRequest::INVITE_ACTION_DELETE = 1
     * - MegaContactRequest::INVITE_ACTION_REMIND = 2
     * @param contactLink Contact link handle of the other account. This parameter is considered only if the
     * \c action is MegaContactRequest::INVITE_ACTION_ADD. Otherwise, it's ignored and it has no effect.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void inviteContact(String email, String message, int action, long contactLink, MegaRequestListenerInterface listener){
        megaApi.inviteContact(email, message, action, contactLink, createDelegateRequestListener(listener));
    }

    /**
     * Reply to a contact request.
     *
     * @param request Contact request. You can get your pending contact requests using
     *                MegaApi.getIncomingContactRequests().
     * @param action Action for this contact request. Valid values are: <br>
     * - MegaContactRequest.REPLY_ACTION_ACCEPT = 0. <br>
     * - MegaContactRequest.REPLY_ACTION_DENY = 1. <br>
     * - MegaContactRequest.REPLY_ACTION_IGNORE = 2. <br>
     *
     * The associated request type with this request is MegaRequest.TYPE_REPLY_CONTACT_REQUEST.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle() - Returns the handle of the contact request. <br>
     * - MegaRequest.getNumber() - Returns the action. <br>
     *
     * @param listener MegaRequestListenerInterface to track this request.
     */
    public void replyContactRequest(MegaContactRequest request, int action, MegaRequestListenerInterface listener) {
        megaApi.replyContactRequest(request, action, createDelegateRequestListener(listener));
    }

    /**
     * Reply to a contact request.
     *
     * @param request Contact request. You can get your pending contact requests using MegaApi.getIncomingContactRequests()
     * @param action Action for this contact request. Valid values are: <br>
     * - MegaContactRequest.REPLY_ACTION_ACCEPT = 0. <br>
     * - MegaContactRequest.REPLY_ACTION_DENY = 1. <br>
     * - MegaContactRequest.REPLY_ACTION_IGNORE = 2.
     */
    public void replyContactRequest(MegaContactRequest request, int action) {
        megaApi.replyContactRequest(request, action);
    }
    
    /**
     * Remove a contact to the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REMOVE_CONTACT.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail() - Returns the email of the contact.
     * 
     * @param user
     *            Email of the contact.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void removeContact(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.removeContact(user, createDelegateRequestListener(listener));
    }

    /**
     * Remove a contact to the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REMOVE_CONTACT.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail() - Returns the email of the contact.
     * @param user
     *            Email of the contact.
     */
    public void removeContact(MegaUser user) {
        megaApi.removeContact(user);
    }

    /**
     * Logout of the MEGA account.
     * 
     * The associated request type with this request is MegaRequest.TYPE_LOGOUT
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void logout(MegaRequestListenerInterface listener) {
        megaApi.logout(createDelegateRequestListener(listener));
    }

    /**
     * Logout of the MEGA account.
     */
    public void logout() {
        megaApi.logout();
    }

    /**
     * Logout of the MEGA account without invalidating the session.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGOUT.
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void localLogout(MegaRequestListenerInterface listener) {
        megaApi.localLogout(createDelegateRequestListener(listener));
    }

    /**
     * Logout of the MEGA account without invalidating the session.
     * 
     */
    public void localLogout() {
        megaApi.localLogout();
    }

    /**
     * Invalidate the existing cache and create a fresh one
     */
    public void invalidateCache(){
        megaApi.invalidateCache();
    }

    /**
     * Estimate the strength of a password
     *
     * Possible return values are:
     * - PASSWORD_STRENGTH_VERYWEAK = 0
     * - PASSWORD_STRENGTH_WEAK = 1
     * - PASSWORD_STRENGTH_MEDIUM = 2
     * - PASSWORD_STRENGTH_GOOD = 3
     * - PASSWORD_STRENGTH_STRONG = 4
     *
     * @param password Password to check
     * @return Estimated strength of the password
     */
    public int getPasswordStrength(String password){
        return megaApi.getPasswordStrength(password);
    }

    /**
     * Submit feedback about the app.
     * <p>
     * The User-Agent is used to identify the app. It can be set in MegaApiJava.MegaApi().
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REPORT_EVENT.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.EVENT_FEEDBACK. <br>
     * - MegaRequest.getText() - Returns the comment about the app. <br>
     * - MegaRequest.getNumber() - Returns the rating for the app.
     * 
     * @param rating
     *            Integer to rate the app. Valid values: from 1 to 5.
     * @param comment
     *            Comment about the app.
     * @param listener
     *            MegaRequestListener to track this request.
     * @deprecated This function is for internal usage of MEGA apps. This feedback
     *             is sent to MEGA servers.
     * 
     */
    @Deprecated public void submitFeedback(int rating, String comment, MegaRequestListenerInterface listener) {
        megaApi.submitFeedback(rating, comment, createDelegateRequestListener(listener));
    }

    /**
     * Submit feedback about the app.
     * <p>
     * The User-Agent is used to identify the app. It can be set in MegaApiJava.MegaApi().
     * 
     * @param rating
     *            Integer to rate the app. Valid values: from 1 to 5.
     * @param comment
     *            Comment about the app.
     * @deprecated This function is for internal usage of MEGA apps. This feedback
     *             is sent to MEGA servers.
     * 
     */
    @Deprecated public void submitFeedback(int rating, String comment) {
        megaApi.submitFeedback(rating, comment);
    }

    /**
     * Send a debug report.
     * <p>
     * The User-Agent is used to identify the app. It can be set in MegaApiJava.MegaApi()
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REPORT_EVENT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.EVENT_DEBUG. <br>
     * - MegaRequest.getText() - Returns the debug message.
     * 
     * @param text
     *            Debug message
     * @param listener
     *            MegaRequestListener to track this request.
     * @deprecated This function is for internal usage of MEGA apps. This feedback
     *             is sent to MEGA servers.
     */
    @Deprecated public void reportDebugEvent(String text, MegaRequestListenerInterface listener) {
        megaApi.reportDebugEvent(text, createDelegateRequestListener(listener));
    }

    /**
     * Send a debug report.
     * <p>
     * The User-Agent is used to identify the app. It can be set in MegaApiJava.MegaApi().
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REPORT_EVENT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParamType() - Returns MegaApiJava.EVENT_DEBUG. <br>
     * - MegaRequest.getText() - Returns the debug message.
     * 
     * @param text
     *            Debug message.
     * @deprecated This function is for internal usage of MEGA apps. This feedback
     *             is sent to MEGA servers.
     */
    @Deprecated public void reportDebugEvent(String text) {
        megaApi.reportDebugEvent(text);
    }
    
    /**
     * Use HTTPS communications only
     *
     * The default behavior is to use HTTP for transfers and the persistent connection
     * to wait for external events. Those communications don't require HTTPS because
     * all transfer data is already end-to-end encrypted and no data is transmitted
     * over the connection to wait for events (it's just closed when there are new events).
     *
     * This feature should only be enabled if there are problems to contact MEGA servers
     * through HTTP because otherwise it doesn't have any benefit and will cause a
     * higher CPU usage.
     *
     * See MegaApi::usingHttpsOnly
     *
     * @param httpsOnly True to use HTTPS communications only
     */
    public void useHttpsOnly(boolean httpsOnly) {
    	megaApi.useHttpsOnly(httpsOnly);
    }
    
    /**
     * Check if the SDK is using HTTPS communications only
     *
     * The default behavior is to use HTTP for transfers and the persistent connection
     * to wait for external events. Those communications don't require HTTPS because
     * all transfer data is already end-to-end encrypted and no data is transmitted
     * over the connection to wait for events (it's just closed when there are new events).
     *
     * See MegaApi::useHttpsOnly
     *
     * @return True if the SDK is using HTTPS communications only. Otherwise false.
     */
    public boolean usingHttpsOnly() {
    	return megaApi.usingHttpsOnly();
    }

    /****************************************************************************************************/
    // TRANSFERS
    /****************************************************************************************************/

    /**
     * Upload a file or a folder
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     * @param listener MegaTransferListener to track this transfer
     */
    public void startUpload(String localPath, MegaNode parent, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or a folder
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     */
    public void startUpload(String localPath, MegaNode parent) {
        megaApi.startUpload(localPath, parent);
    }

    /**
     * Upload a file or a folder with a custom modification time
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file
     * @param parent Parent node for the file in the MEGA account
     * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
     * @param listener MegaTransferListener to track this transfer
     *
     * The custom modification time will be only applied for file transfers. If a folder
     * is transferred using this function, the custom modification time won't have any effect,
     */
    public void startUpload(String localPath, MegaNode parent, long mtime, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, mtime, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or a folder with a custom modification time
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file
     * @param parent Parent node for the file in the MEGA account
     * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
     *
     * The custom modification time will be only applied for file transfers. If a folder
     * is transferred using this function, the custom modification time won't have any effect,
     */
    public void startUpload(String localPath, MegaNode parent, long mtime) {
        megaApi.startUpload(localPath, parent, mtime);
    }

    /**
     * Upload a file or folder with a custom name
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     * @param fileName Custom file name for the file or folder in MEGA
     * @param listener MegaTransferListener to track this transfer
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, fileName, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or folder with a custom name
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     * @param fileName Custom file name for the file or folder in MEGA
     */
    public void startUpload(String localPath, MegaNode parent, String fileName) {
        megaApi.startUpload(localPath, parent, fileName);
    }

    /**
     * Upload a file or a folder with a custom name and a custom modification time
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file
     * @param parent Parent node for the file in the MEGA account
     * @param fileName Custom file name for the file in MEGA
     * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
     * @param listener MegaTransferListener to track this transfer
     *
     * The custom modification time will be only applied for file transfers. If a folder
     * is transferred using this function, the custom modification time won't have any effect
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, long mtime, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, fileName, mtime, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or a folder with a custom name and a custom modification time
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file
     * @param parent Parent node for the file in the MEGA account
     * @param fileName Custom file name for the file in MEGA
     * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
     *
     * The custom modification time will be only applied for file transfers. If a folder
     * is transferred using this function, the custom modification time won't have any effect
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, long mtime) {
        megaApi.startUpload(localPath, parent, fileName, mtime);
    }

    /**
     * Upload a file or a folder, saving custom app data during the transfer
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     * @param appData Custom app data to save in the MegaTransfer object
     * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     * related to the transfer. If a transfer is started with exactly the same data
     * (local path and target parent) as another one in the transfer queue, the new transfer
     * fails with the error API_EEXISTS and the appData of the new transfer is appended to
     * the appData of the old transfer, using a '!' separator if the old transfer had already
     * appData.
     * @param listener MegaTransferListener to track this transfer
     */
    public void startUploadWithData(String localPath, MegaNode parent, String appData, MegaTransferListenerInterface listener){
        megaApi.startUploadWithData(localPath, parent, appData, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or a folder, saving custom app data during the transfer
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     * @param appData Custom app data to save in the MegaTransfer object
     * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     * related to the transfer. If a transfer is started with exactly the same data
     * (local path and target parent) as another one in the transfer queue, the new transfer
     * fails with the error API_EEXISTS and the appData of the new transfer is appended to
     * the appData of the old transfer, using a '!' separator if the old transfer had already
     * appData.
     */
    public void startUploadWithData(String localPath, MegaNode parent, String appData){
        megaApi.startUploadWithData(localPath, parent, appData);
    }

    /**
     * Upload a file or a folder, putting the transfer on top of the upload queue
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     * @param appData Custom app data to save in the MegaTransfer object
     * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     * related to the transfer. If a transfer is started with exactly the same data
     * (local path and target parent) as another one in the transfer queue, the new transfer
     * fails with the error API_EEXISTS and the appData of the new transfer is appended to
     * the appData of the old transfer, using a '!' separator if the old transfer had already
     * appData.
     * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
     * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
     * Use this parameter with caution. Set it to true only if you are sure about what are you doing.
     * @param listener MegaTransferListener to track this transfer
     */
    public void startUploadWithTopPriority(String localPath, MegaNode parent, String appData, boolean isSourceTemporary, MegaTransferListenerInterface listener){
        megaApi.startUploadWithTopPriority(localPath, parent, appData, isSourceTemporary, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or a folder, putting the transfer on top of the upload queue
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath Local path of the file or folder
     * @param parent Parent node for the file or folder in the MEGA account
     * @param appData Custom app data to save in the MegaTransfer object
     * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     * related to the transfer. If a transfer is started with exactly the same data
     * (local path and target parent) as another one in the transfer queue, the new transfer
     * fails with the error API_EEXISTS and the appData of the new transfer is appended to
     * the appData of the old transfer, using a '!' separator if the old transfer had already
     * appData.
     * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
     * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
     * Use this parameter with caution. Set it to true only if you are sure about what are you doing.
     */
    public void startUploadWithTopPriority(String localPath, MegaNode parent, String appData, boolean isSourceTemporary){
        megaApi.startUploadWithTopPriority(localPath, parent, appData, isSourceTemporary);
    }

    /**
     * Download a file or a folder from MEGA
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param node MegaNode that identifies the file or folder
     * @param localPath Destination path for the file or folder
     * If this path is a local folder, it must end with a '\' or '/' character and the file name
     * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     *
     * @param listener MegaTransferListener to track this transfer
     */
    public void startDownload(MegaNode node, String localPath, MegaTransferListenerInterface listener) {
        megaApi.startDownload(node, localPath, createDelegateTransferListener(listener));
    }

    /**
     * Download a file or a folder from MEGA
     *
     *If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param node MegaNode that identifies the file or folder
     * @param localPath Destination path for the file or folder
     * If this path is a local folder, it must end with a '\' or '/' character and the file name
     * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     */
    public void startDownload(MegaNode node, String localPath) {
        megaApi.startDownload(node, localPath);
    }

    /**
     * Download a file or a folder from MEGA, saving custom app data during the transfer
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param node MegaNode that identifies the file or folder
     * @param localPath Destination path for the file or folder
     * If this path is a local folder, it must end with a '\' or '/' character and the file name
     * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     * @param appData Custom app data to save in the MegaTransfer object
     * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     * related to the transfer.
     * @param listener MegaTransferListener to track this transfer
     */
    public void startDownloadWithData(MegaNode node, String localPath, String appData, MegaTransferListenerInterface listener){
        megaApi.startDownloadWithData(node, localPath, appData, createDelegateTransferListener(listener));
    }

    /**
     * Download a file or a folder from MEGA, putting the transfer on top of the download queue.
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param node MegaNode that identifies the file or folder
     * @param localPath Destination path for the file or folder
     * If this path is a local folder, it must end with a '\' or '/' character and the file name
     * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     * one of these characters, the file will be downloaded to a file in that path.
     * @param appData Custom app data to save in the MegaTransfer object
     * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     * related to the transfer.
     * @param listener MegaTransferListener to track this transfer
     */
    public void startDownloadWithTopPriority(MegaNode node, String localPath, String appData, MegaTransferListenerInterface listener){
        megaApi.startDownloadWithTopPriority(node, localPath, appData, createDelegateTransferListener(listener));
    }

    /**
     * Start an streaming download for a file in MEGA
     *
     * Streaming downloads don't save the downloaded data into a local file. It is provided
     * in MegaTransferListener::onTransferUpdate in a byte buffer. The pointer is returned by
     * MegaTransfer::getLastBytes and the size of the buffer in MegaTransfer::getDeltaSize
     *
     * The same byte array is also provided in the callback MegaTransferListener::onTransferData for
     * compatibility with other programming languages. Only the MegaTransferListener passed to this function
     * will receive MegaTransferListener::onTransferData callbacks. MegaTransferListener objects registered
     * with MegaApi::addTransferListener won't receive them for performance reasons
     *
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param node MegaNode that identifies the file
     * @param startPos First byte to download from the file
     * @param size Size of the data to download
     * @param listener MegaTransferListener to track this transfer
     */
    public void startStreaming(MegaNode node, long startPos, long size, MegaTransferListenerInterface listener) {
        megaApi.startStreaming(node, startPos, size, createDelegateTransferListener(listener));
    }

    /**
     * Cancel a transfer.
     * <p>
     * When a transfer is cancelled, it will finish and will provide the error code
     * MegaError.API_EINCOMPLETE in MegaTransferListener.onTransferFinish() and
     * MegaListener.onTransferFinish().
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_TRANSFER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getTransferTag() - Returns the tag of the cancelled transfer (MegaTransfer.getTag).
     * 
     * @param transfer
     *            MegaTransfer object that identifies the transfer.
     *            You can get this object in any MegaTransferListener callback or any MegaListener callback
     *            related to transfers.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void cancelTransfer(MegaTransfer transfer, MegaRequestListenerInterface listener) {
        megaApi.cancelTransfer(transfer, createDelegateRequestListener(listener));
    }

    /**
     * Cancel a transfer.
     * 
     * @param transfer
     *            MegaTransfer object that identifies the transfer.
     *            You can get this object in any MegaTransferListener callback or any MegaListener callback
     *            related to transfers.
     */
    public void cancelTransfer(MegaTransfer transfer) {
        megaApi.cancelTransfer(transfer);
    }

    /**
     * Cancel the transfer with a specific tag.
     * <p>
     * When a transfer is cancelled, it will finish and will provide the error code
     * MegaError.API_EINCOMPLETE in MegaTransferListener.onTransferFinish() and
     * MegaListener.onTransferFinish().
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_TRANSFER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getTransferTag() - Returns the tag of the cancelled transfer (MegaTransfer.getTag).
     * 
     * @param transferTag
     *            tag that identifies the transfer.
     *            You can get this tag using MegaTransfer.getTag().
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void cancelTransferByTag(int transferTag, MegaRequestListenerInterface listener) {
        megaApi.cancelTransferByTag(transferTag, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the transfer with a specific tag.
     * 
     * @param transferTag
     *            tag that identifies the transfer.
     *            You can get this tag using MegaTransfer.getTag().
     */
    public void cancelTransferByTag(int transferTag) {
        megaApi.cancelTransferByTag(transferTag);
    }

    /**
     * Cancel all transfers of the same type.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParamType() - Returns the first parameter.
     * 
     * @param direction
     *            Type of transfers to cancel.
     *            Valid values are: <br>
     *            - MegaTransfer.TYPE_DOWNLOAD = 0. <br>
     *            - MegaTransfer.TYPE_UPLOAD = 1.
     * 
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void cancelTransfers(int direction, MegaRequestListenerInterface listener) {
        megaApi.cancelTransfers(direction, createDelegateRequestListener(listener));
    }

    /**
     * Cancel all transfers of the same type.
     * 
     * @param direction
     *            Type of transfers to cancel.
     *            Valid values are: <br>
     *            - MegaTransfer.TYPE_DOWNLOAD = 0. <br>
     *            - MegaTransfer.TYPE_UPLOAD = 1.
     */
    public void cancelTransfers(int direction) {
        megaApi.cancelTransfers(direction);
    }

    /**
     * Pause/resume all transfers.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_PAUSE_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getFlag() - Returns the first parameter.
     * 
     * @param pause
     *            true to pause all transfers / false to resume all transfers.
     * @param listener
     *            MegaRequestListener to track this request.
     */
    public void pauseTransfers(boolean pause, MegaRequestListenerInterface listener) {
        megaApi.pauseTransfers(pause, createDelegateRequestListener(listener));
    }

    /**
     * Pause/resume all transfers.
     * 
     * @param pause
     *            true to pause all transfers / false to resume all transfers.
     */
    public void pauseTransfers(boolean pause) {
        megaApi.pauseTransfers(pause);
    }
    
    /**
     * Pause/resume all transfers in one direction (uploads or downloads)
     *
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns the first parameter
     * - MegaRequest::getNumber - Returns the direction of the transfers to pause/resume
     *
     * @param pause true to pause transfers / false to resume transfers
     * @param direction Direction of transfers to pause/resume
     * Valid values for this parameter are:
     * - MegaTransfer::TYPE_DOWNLOAD = 0
     * - MegaTransfer::TYPE_UPLOAD = 1
     *
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void pauseTransfers(boolean pause, int direction, MegaRequestListenerInterface listener) {
    	megaApi.pauseTransfers(pause, direction, createDelegateRequestListener(listener));
    }
    
    /**
     * Pause/resume all transfers in one direction (uploads or downloads)
     * 
     * @param pause true to pause transfers / false to resume transfers
     * @param direction Direction of transfers to pause/resume
     * Valid values for this parameter are:
     * - MegaTransfer::TYPE_DOWNLOAD = 0
     * - MegaTransfer::TYPE_UPLOAD = 1
     */
    public void pauseTransfers(boolean pause, int direction) {
    	megaApi.pauseTransfers(pause, direction);
    }

    /**
     * Pause/resume a transfer
     *
     * The request finishes with MegaError::API_OK if the state of the transfer is the
     * desired one at that moment. That means that the request succeed when the transfer
     * is successfully paused or resumed, but also if the transfer was already in the
     * desired state and it wasn't needed to change anything.
     *
     * Resumed transfers don't necessarily continue just after the resumption. They
     * are tagged as queued and are processed according to its position on the request queue.
     *
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to pause or resume
     * - MegaRequest::getFlag - Returns true if the transfer has to be pause or false if it has to be resumed
     *
     * @param transfer Transfer to pause or resume
     * @param pause True to pause the transfer or false to resume it
     * @param listener MegaRequestListener to track this request
     */
    public void pauseTransfer(MegaTransfer transfer, boolean pause, MegaRequestListenerInterface listener){
        megaApi.pauseTransfer(transfer, pause, createDelegateRequestListener(listener));
    }

    /**
     * Pause/resume a transfer
     *
     * The request finishes with MegaError::API_OK if the state of the transfer is the
     * desired one at that moment. That means that the request succeed when the transfer
     * is successfully paused or resumed, but also if the transfer was already in the
     * desired state and it wasn't needed to change anything.
     *
     * Resumed transfers don't necessarily continue just after the resumption. They
     * are tagged as queued and are processed according to its position on the request queue.
     *
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to pause or resume
     * - MegaRequest::getFlag - Returns true if the transfer has to be pause or false if it has to be resumed
     *
     * @param transferTag Tag of the transfer to pause or resume
     * @param pause True to pause the transfer or false to resume it
     * @param listener MegaRequestListener to track this request
     */
    public void pauseTransferByTag(int transferTag, boolean pause, MegaRequestListenerInterface listener){
        megaApi.pauseTransferByTag(transferTag, pause, createDelegateRequestListener(listener));
    }


    /**
     * Enable the resumption of transfers
     *
     * This function enables the cache of transfers, so they can be resumed later.
     * Additionally, if a previous cache already exists (from previous executions),
     * then this function also resumes the existing cached transfers.
     *
     * Cached downloads expire after 10 days since the last time they were active.
     * Cached uploads expire after 24 hours since the last time they were active.
     * Cached transfers related to files that have been modified since they were
     * added to the cache are discarded, since the file has changed.
     *
     * A log in or a log out automatically disables this feature.
     *
     * When the MegaApi object is logged in, the cache of transfers is identified
     * and protected using the session and the master key, so transfers won't
     * be resumable using a different session or a different account. The
     * recommended way of using this function to resume transfers for an account
     * is calling it in the callback onRequestFinish related to MegaApi::fetchNodes
     *
     * When the MegaApi object is not logged in, it's still possible to use this
     * feature. However, since there isn't any available data to identify
     * and protect the cache, a default identifier and key are used. To improve
     * the protection of the transfer cache and allow the usage of this feature
     * with several non logged in instances of MegaApi at once without clashes,
     * it's possible to set a custom identifier for the transfer cache in the
     * optional parameter of this function. If that parameter is used, the
     * encryption key for the transfer cache will be derived from it.
     *
     */
    public void enableTransferResumption(){
        megaApi.enableTransferResumption();
    }

    /**
     * Enable the resumption of transfers
     *
     * This function enables the cache of transfers, so they can be resumed later.
     * Additionally, if a previous cache already exists (from previous executions),
     * then this function also resumes the existing cached transfers.
     *
     * Cached downloads expire after 10 days since the last time they were active.
     * Cached uploads expire after 24 hours since the last time they were active.
     * Cached transfers related to files that have been modified since they were
     * added to the cache are discarded, since the file has changed.
     *
     * A log in or a log out automatically disables this feature.
     *
     * When the MegaApi object is logged in, the cache of transfers is identified
     * and protected using the session and the master key, so transfers won't
     * be resumable using a different session or a different account. The
     * recommended way of using this function to resume transfers for an account
     * is calling it in the callback onRequestFinish related to MegaApi::fetchNodes
     *
     * When the MegaApi object is not logged in, it's still possible to use this
     * feature. However, since there isn't any available data to identify
     * and protect the cache, a default identifier and key are used. To improve
     * the protection of the transfer cache and allow the usage of this feature
     * with several non logged in instances of MegaApi at once without clashes,
     * it's possible to set a custom identifier for the transfer cache in the
     * optional parameter of this function. If that parameter is used, the
     * encryption key for the transfer cache will be derived from it.
     *
     * @param loggedOutId Identifier for a non logged in instance of MegaApi.
     * It doesn't have any effect if MegaApi is logged in.
     */
    public void enableTransferResumption(String loggedOutId){
        megaApi.enableTransferResumption(loggedOutId);
    }

    /**
     * Disable the resumption of transfers
     *
     * This function disables the resumption of transfers and also deletes
     * the transfer cache if it exists. See also MegaApi.enableTransferResumption.
     *
     */
    public void disableTransferResumption(){
        megaApi.disableTransferResumption();
    }

    /**
     * Disable the resumption of transfers
     *
     * This function disables the resumption of transfers and also deletes
     * the transfer cache if it exists. See also MegaApi.enableTransferResumption.
     *
     * @param loggedOutId Identifier for a non logged in instance of MegaApi.
     * It doesn't have any effect if MegaApi is logged in.
     */
    public void disableTransferResumption(String loggedOutId){
        megaApi.disableTransferResumption(loggedOutId);
    }

    /**
     * Returns the state (paused/unpaused) of transfers
     * @param direction Direction of transfers to check
     * Valid values for this parameter are:
     * - MegaTransfer::TYPE_DOWNLOAD = 0
     * - MegaTransfer::TYPE_UPLOAD = 1
     *
     * @return true if transfers on that direction are paused, false otherwise
     */
    public boolean areTransfersPaused(int direction) {
    	return megaApi.areTransfersPaused(direction);
    }
    

    /**
     * Set the upload speed limit.
     * <p>
     * The limit will be applied on the server side when starting a transfer. Thus the limit won't be
     * applied for already started uploads and it's applied per storage server.
     * 
     * @param bpslimit
     *            -1 to automatically select the limit, 0 for no limit, otherwise the speed limit
     *            in bytes per second.
     */
    public void setUploadLimit(int bpslimit) {
        megaApi.setUploadLimit(bpslimit);
    }
    
    /**
     * Set the transfer method for downloads
     *
     * Valid methods are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     *
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     *
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     *
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     *
     *  - TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
     * The SDK selects the transfer method automatically starting with alternative port 8080.
     *
     * @param method Selected transfer method for downloads
     */
    public void setDownloadMethod(int method) {
    	megaApi.setDownloadMethod(method);
    }
    
    /**
     * Set the transfer method for uploads
     *
     * Valid methods are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     *
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     *
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     *
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     *
     * - TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
     * The SDK selects the transfer method automatically starting with alternative port 8080.
     *
     * @param method Selected transfer method for uploads
     */
    public void setUploadMethod(int method) {
    	megaApi.setUploadMethod(method);
    }
    
    /**
     * Get the active transfer method for downloads
     *
     * Valid values for the return parameter are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     *
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     *
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     *
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     *
     * - TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
     * The SDK selects the transfer method automatically starting with alternative port 8080.
     *
     * @return Active transfer method for downloads
     */
    public int getDownloadMethod() {
    	return megaApi.getDownloadMethod();
    }
    
    /**
     * Get the active transfer method for uploads
     *
     * Valid values for the return parameter are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     *
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     *
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     *
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     *
     * - TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
     * The SDK selects the transfer method automatically starting with alternative port 8080.
     *
     * @return Active transfer method for uploads
     */
    public int getUploadMethod() {
    	return megaApi.getUploadMethod();
    }

    /**
     * Get all active transfers.
     * 
     * @return List with all active transfers.
     */
    public ArrayList<MegaTransfer> getTransfers() {
        return transferListToArray(megaApi.getTransfers());
    }
    
    /**
     * Get all active transfers based on the type.
     * 
     * @param type
     *            MegaTransfer.TYPE_DOWNLOAD || MegaTransfer.TYPE_UPLOAD.
     * 
     * @return List with all active download or upload transfers.
     */
    public ArrayList<MegaTransfer> getTransfers(int type) {
        return transferListToArray(megaApi.getTransfers(type));
    }

    /**
     * Get the transfer with a transfer tag.
     * <p>
     * MegaTransfer.getTag() can be used to get the transfer tag.
     * 
     * @param transferTag
     *            tag to check.
     * @return MegaTransfer object with that tag, or null if there is not any
     *         active transfer with it.
     * 
     */
    public MegaTransfer getTransferByTag(int transferTag) {
        return megaApi.getTransferByTag(transferTag);
    }

    /**
     * Get the maximum download speed in bytes per second
     *
     * The value 0 means unlimited speed
     *
     * @return Download speed in bytes per second
     */
    public int getMaxDownloadSpeed(){
        return megaApi.getMaxDownloadSpeed();
    }

    /**
     * Get the maximum upload speed in bytes per second
     *
     * The value 0 means unlimited speed
     *
     * @return Upload speed in bytes per second
     */
    public int getMaxUploadSpeed(){
        return megaApi.getMaxUploadSpeed();
    }

    /**
     * Return the current download speed
     * @return Download speed in bytes per second
     */
    public int getCurrentDownloadSpeed(){
        return megaApi.getCurrentDownloadSpeed();
    }

    /**
     * Return the current download speed
     * @return Download speed in bytes per second
     */
    public int getCurrentUploadSpeed(){
        return megaApi.getCurrentUploadSpeed();
    }

    /**
     * Return the current transfer speed
     * @param type Type of transfer to get the speed.
     * Valid values are MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD
     * @return Transfer speed for the transfer type, or 0 if the parameter is invalid
     */
    public int getCurrentSpeed(int type){
        return megaApi.getCurrentSpeed(type);
    }


    /**
     * Get information about transfer queues
     * @param listener MegaTransferListener to start receiving information about transfers
     * @return Information about transfer queues
     */
    public MegaTransferData getTransferData(MegaTransferListenerInterface listener){
        return megaApi.getTransferData(createDelegateTransferListener(listener, false));
    }

    /**
     * Get the first transfer in a transfer queue
     *
     * You take the ownership of the returned value.
     *
     * @param type queue to get the first transfer (MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD)
     * @return MegaTransfer object related to the first transfer in the queue or NULL if there isn't any transfer
     */
    public MegaTransfer getFirstTransfer(int type){
        return megaApi.getFirstTransfer(type);
    }

    /**
     * Force an onTransferUpdate callback for the specified transfer
     *
     * The callback will be received by transfer listeners registered to receive all
     * callbacks related to callbacks and additionally by the listener in the last
     * parameter of this function, if it's not NULL.
     *
     * @param transfer Transfer that will be provided in the onTransferUpdate callback
     * @param listener Listener that will receive the callback
     */
    public void notifyTransfer(MegaTransfer transfer, MegaTransferListenerInterface listener){
        megaApi.notifyTransfer(transfer, createDelegateTransferListener(listener));
    }

    /**
     * Force an onTransferUpdate callback for the specified transfer
     *
     * The callback will be received by transfer listeners registered to receive all
     * callbacks related to callbacks and additionally by the listener in the last
     * parameter of this function, if it's not NULL.
     *
     * @param transferTag Tag of the transfer that will be provided in the onTransferUpdate callback
     * @param listener Listener that will receive the callback
     */
    public void notifyTransferByTag(int transferTag, MegaTransferListenerInterface listener){
        megaApi.notifyTransferByTag(transferTag, createDelegateTransferListener(listener));
    }

    /**
     * Get a list of transfers that belong to a folder transfer
     *
     * This function provides the list of transfers started in the context
     * of a folder transfer.
     *
     * If the tag in the parameter doesn't belong to a folder transfer,
     * this function returns an empty list.
     *
     * The transfers provided by this function are the ones that are added to the
     * transfer queue when this function is called. Finished transfers, or transfers
     * not added to the transfer queue yet (for example, uploads that are waiting for
     * the creation of the parent folder in MEGA) are not returned by this function.
     *
     * @param transferTag Tag of the folder transfer to check
     * @return List of transfers in the context of the selected folder transfer
     * @see MegaTransfer::isFolderTransfer, MegaTransfer::getFolderTransferTag
     */
    public ArrayList<MegaTransfer> getChildTransfers(int transferTag) {
    	return transferListToArray(megaApi.getChildTransfers(transferTag));
    }

    /**
     * Force a loop of the SDK thread.
     * 
     * @deprecated This function is only here for debugging purposes. It will probably
     *             be removed in future updates.
     */
    @Deprecated public void update() {
        megaApi.update();
    }

    /**
     * Check if the SDK is waiting for the server.
     * 
     * @return true if the SDK is waiting for the server to complete a request.
     */
    public int isWaiting() {
        return megaApi.isWaiting();
    }

    /**
     * Check if the SDK is waiting for the server
     * @return true if the SDK is waiting for the server to complete a request
     */
    public int areServersBusy(){
        return megaApi.areServersBusy();
    }

    /**
     * Get the number of pending uploads
     *
     * @return Pending uploads
     *
     * Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    public int getNumPendingUploads() {
        return megaApi.getNumPendingUploads();
    }

    /**
     * Get the number of pending downloads
     * @return Pending downloads
     *
     * Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    public int getNumPendingDownloads() {
        return megaApi.getNumPendingDownloads();
    }

    /**
     * Get the number of queued uploads since the last call to MegaApi::resetTotalUploads
     * @return Number of queued uploads since the last call to MegaApi::resetTotalUploads
     *
     * Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    public int getTotalUploads() {
        return megaApi.getTotalUploads();
    }

    /**
     * Get the number of queued uploads since the last call to MegaApiJava.resetTotalDownloads().
     * 
     * @return Number of queued uploads since the last call to MegaApiJava.resetTotalDownloads().
     * Function related to statistics will be reviewed in future updates. They
     *             could change or be removed in the current form.
     */
    public int getTotalDownloads() {
        return megaApi.getTotalDownloads();
    }

    /**
     * Reset the number of total downloads.
     * <p>
     * This function resets the number returned by MegaApiJava.getTotalDownloads().
     * 
     * Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     * 
     */
    public void resetTotalDownloads() {
        megaApi.resetTotalDownloads();
    }

    /**
     * Reset the number of total uploads.
     * <p>
     * This function resets the number returned by MegaApiJava.getTotalUploads().
     * 
     * Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    public void resetTotalUploads() {
        megaApi.resetTotalUploads();
    }

    /**
     * Get the total downloaded bytes
     * @return Total downloaded bytes
     *
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalDownloads
     * or just before a log in or a log out.
     *
     * Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    public long getTotalDownloadedBytes() {
        return megaApi.getTotalDownloadedBytes();
    }

    /**
     * Get the total uploaded bytes
     * @return Total uploaded bytes
     *
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalUploads
     * or just before a log in or a log out.
     *
     * Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     *
     */
    public long getTotalUploadedBytes() {
        return megaApi.getTotalUploadedBytes();
    }

    /**
     * @brief Get the total bytes of started downloads
     * @return Total bytes of started downloads
     *
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalDownloads
     * or just before a log in or a log out.
     *
     * Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    public long getTotalDownloadBytes(){
        return megaApi.getTotalDownloadBytes();
    }

    /**
     * Get the total bytes of started uploads
     * @return Total bytes of started uploads
     *
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalUploads
     * or just before a log in or a log out.
     *
     * Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     *
     */
    public long getTotalUploadBytes(){
        return megaApi.getTotalUploadBytes();
    }

    /**
     * Update the number of pending downloads/uploads.
     * <p>
     * This function forces a count of the pending downloads/uploads. It could
     * affect the return value of MegaApiJava.getNumPendingDownloads() and
     * MegaApiJava.getNumPendingUploads().
     * 
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     * 
     */
    public void updateStats() {
        megaApi.updateStats();
    }

    /**
     * Starts an unbuffered download of a node (file) from the user's MEGA account.
     *
     * @param node The MEGA node to download.
     * @param startOffset long. The byte to start from.
     * @param size long. Size of the download.
     * @param outputStream The output stream object to use for this download.
     * @param listener MegaRequestListener to track this request.
     */
    public void startUnbufferedDownload(MegaNode node, long startOffset, long size, OutputStream outputStream, MegaTransferListenerInterface listener) {
        DelegateMegaTransferListener delegateListener = new DelegateOutputMegaTransferListener(this, outputStream, listener, true);
        activeTransferListeners.add(delegateListener);
        megaApi.startStreaming(node, startOffset, size, delegateListener);
    }

    /**
     * Starts an unbuffered download of a node (file) from the user's MEGA account.
     *
     * @param node The MEGA node to download.
     * @param outputStream The output stream object to use for this download.
     * @param listener MegaRequestListener to track this request.
     */
    public void startUnbufferedDownload(MegaNode node, OutputStream outputStream, MegaTransferListenerInterface listener) {
        startUnbufferedDownload(node, 0, node.getSize(), outputStream, listener);
    }

    /****************************************************************************************************/
    // FILESYSTEM METHODS
    /****************************************************************************************************/

    /**
     * Get the number of child nodes.
     * <p>
     * If the node does not exist in MEGA or is not a folder,
     * this function returns 0.
     * <p>
     * This function does not search recursively, only returns the direct child nodes.
     * 
     * @param parent
     *            Parent node.
     * @return Number of child nodes.
     */
    public int getNumChildren(MegaNode parent) {
        return megaApi.getNumChildren(parent);
    }

    /**
     * Get the number of child files of a node.
     * <p>
     * If the node does not exist in MEGA or is not a folder,
     * this function returns 0.
     * <p>
     * This function does not search recursively, only returns the direct child files.
     * 
     * @param parent
     *            Parent node.
     * @return Number of child files.
     */
    public int getNumChildFiles(MegaNode parent) {
        return megaApi.getNumChildFiles(parent);
    }

    /**
     * Get the number of child folders of a node.
     * <p>
     * If the node does not exist in MEGA or is not a folder,
     * this function returns 0.
     * <p>
     * This function does not search recursively, only returns the direct child folders.
     * 
     * @param parent
     *            Parent node.
     * @return Number of child folders.
     */
    public int getNumChildFolders(MegaNode parent) {
        return megaApi.getNumChildFolders(parent);
    }

    /**
     * Get all children of a MegaNode.
     * <p>
     * If the parent node does not exist or it is not a folder, this function
     * returns null.
     * 
     * @param parent
     *            Parent node.
     * @param order
     *            Order for the returned list.
     *            Valid values for this parameter are: <br>
     *            - MegaApiJava.ORDER_NONE = 0.
     *            Undefined order. <br>
     * 
     *            - MegaApiJava.ORDER_DEFAULT_ASC = 1.
     *            Folders first in alphabetical order, then files in the same order. <br>
     * 
     *            - MegaApiJava.ORDER_DEFAULT_DESC = 2.
     *            Files first in reverse alphabetical order, then folders in the same order. <br>
     * 
     *            - MegaApiJava.ORDER_SIZE_ASC = 3.
     *            Sort by size, ascending. <br>
     * 
     *            - MegaApiJava.ORDER_SIZE_DESC = 4.
     *            Sort by size, descending. <br>
     * 
     *            - MegaApiJava.ORDER_CREATION_ASC = 5.
     *            Sort by creation time in MEGA, ascending. <br>
     * 
     *            - MegaApiJava.ORDER_CREATION_DESC = 6
     *            Sort by creation time in MEGA, descending <br>
     * 
     *            - MegaApiJava.ORDER_MODIFICATION_ASC = 7.
     *            Sort by modification time of the original file, ascending. <br>
     * 
     *            - MegaApiJava.ORDER_MODIFICATION_DESC = 8.
     *            Sort by modification time of the original file, descending. <br>
     * 
     *            - MegaApiJava.ORDER_ALPHABETICAL_ASC = 9.
     *            Sort in alphabetical order, ascending. <br>
     * 
     *            - MegaApiJava.ORDER_ALPHABETICAL_DESC = 10.
     *            Sort in alphabetical order, descending.
     * @return List with all child MegaNode objects.
     */
    public ArrayList<MegaNode> getChildren(MegaNode parent, int order) {
        return nodeListToArray(megaApi.getChildren(parent, order));
    }

    /**
     * Get all versions of a file
     * @param node Node to check
     * @return List with all versions of the node, including the current version
     */
    public ArrayList<MegaNode> getVersions(MegaNode node){
        return nodeListToArray(megaApi.getVersions(node));
    }

    /**
     * Get the number of versions of a file
     * @param node Node to check
     * @return Number of versions of the node, including the current version
     */
    public int getNumVersions(MegaNode node){
        return megaApi.getNumVersions(node);
    }

    /**
     * Check if a file has previous versions
     * @param node Node to check
     * @return true if the node has any previous version
     */
    public boolean hasVersions(MegaNode node){
        return megaApi.hasVersions(node);
    }

    /**
     * Get information about the contents of a folder
     *
     * The associated request type with this request is MegaRequest::TYPE_FOLDER_INFO
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaFolderInfo - MegaFolderInfo object with the information related to the folder
     *
     * @param node Folder node to inspect
     * @param listener MegaRequestListener to track this request
     */
    public void getFolderInfo(MegaNode node, MegaRequestListenerInterface listener){
        megaApi.getFolderInfo(node, createDelegateRequestListener(listener));
    }

    /**
     * Get file and folder children of a MegaNode separatedly
     *
     * If the parent node doesn't exist or it isn't a folder, this function
     * returns NULL
     *
     * You take the ownership of the returned value
     *
     * @param parent Parent node
     * @param order Order for the returned lists
     * Valid values for this parameter are:
     * - MegaApi::ORDER_NONE = 0
     * Undefined order
     *
     * - MegaApi::ORDER_DEFAULT_ASC = 1
     * Folders first in alphabetical order, then files in the same order
     *
     * - MegaApi::ORDER_DEFAULT_DESC = 2
     * Files first in reverse alphabetical order, then folders in the same order
     *
     * - MegaApi::ORDER_SIZE_ASC = 3
     * Sort by size, ascending
     *
     * - MegaApi::ORDER_SIZE_DESC = 4
     * Sort by size, descending
     *
     * - MegaApi::ORDER_CREATION_ASC = 5
     * Sort by creation time in MEGA, ascending
     *
     * - MegaApi::ORDER_CREATION_DESC = 6
     * Sort by creation time in MEGA, descending
     *
     * - MegaApi::ORDER_MODIFICATION_ASC = 7
     * Sort by modification time of the original file, ascending
     *
     * - MegaApi::ORDER_MODIFICATION_DESC = 8
     * Sort by modification time of the original file, descending
     *
     * - MegaApi::ORDER_ALPHABETICAL_ASC = 9
     * Sort in alphabetical order, ascending
     *
     * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
     * Sort in alphabetical order, descending
     *
     * @return MegaChildren object with two ArrayLists: fileList and FolderList
     */
    public MegaChildren getFileFolderChildren(MegaNode parent, int order){
        MegaChildren children = new MegaChildren();

        MegaChildrenLists childrenList = megaApi.getFileFolderChildren(parent, order);

        children.setFileList(nodeListToArray(childrenList.getFileList()));
        children.setFolderList(nodeListToArray(childrenList.getFolderList()));

        return children;
    }

    /**
     * Get all children of a MegaNode.
     * <p>
     * If the parent node does not exist or if it is not a folder, this function.
     * returns null.
     * 
     * @param parent
     *            Parent node.
     * 
     * @return List with all child MegaNode objects.
     */
    public ArrayList<MegaNode> getChildren(MegaNode parent) {
        return nodeListToArray(megaApi.getChildren(parent));
    }

    /**
     * Returns true if the node has children
     * @return true if the node has children
     */
    public boolean hasChildren(MegaNode parent){
        return megaApi.hasChildren(parent);
    }

    /**
     * Get the child node with the provided name.
     * <p>
     * If the node does not exist, this function returns null.
     * 
     * @param parent
     *            node.
     * @param name
     *            of the node.
     * @return The MegaNode that has the selected parent and name.
     */
    public MegaNode getChildNode(MegaNode parent, String name) {
        return megaApi.getChildNode(parent, name);
    }

    /**
     * Get the parent node of a MegaNode.
     * <p>
     * If the node does not exist in the account or
     * it is a root node, this function returns null.
     * 
     * @param node
     *            MegaNode to get the parent.
     * @return The parent of the provided node.
     */
    public MegaNode getParentNode(MegaNode node) {
        return megaApi.getParentNode(node);
    }

    /**
     * Get the path of a MegaNode.
     * <p>
     * If the node does not exist, this function returns null.
     * You can recover the node later using MegaApi.getNodeByPath()
     * unless the path contains names with '/', '\' or ':' characters.
     * 
     * @param node
     *            MegaNode for which the path will be returned.
     * @return The path of the node.
     */
    public String getNodePath(MegaNode node) {
        return megaApi.getNodePath(node);
    }

    /**
     * Get the MegaNode in a specific path in the MEGA account.
     * <p>
     * The path separator character is '/'. <br>
     * The Inbox root node is //in/. <br>
     * The Rubbish root node is //bin/.
     * <p>
     * Paths with names containing '/', '\' or ':' are not compatible
     * with this function.
     * 
     * @param path
     *            Path to check.
     * @param baseFolder
     *            Base node if the path is relative.
     * @return The MegaNode object in the path, otherwise null.
     */
    public MegaNode getNodeByPath(String path, MegaNode baseFolder) {
        return megaApi.getNodeByPath(path, baseFolder);
    }

    /**
     * Get the MegaNode in a specific path in the MEGA account.
     * <p>
     * The path separator character is '/'. <br>
     * The Inbox root node is //in/. <br>
     * The Rubbish root node is //bin/.
     * <p>
     * Paths with names containing '/', '\' or ':' are not compatible
     * with this function.
     * 
     * @param path
     *            Path to check.
     * 
     * @return The MegaNode object in the path, otherwise null.
     */
    public MegaNode getNodeByPath(String path) {
        return megaApi.getNodeByPath(path);
    }

    /**
     * Get the MegaNode that has a specific handle.
     * <p>
     * You can get the handle of a MegaNode using MegaNode.getHandle(). The same handle
     * can be got in a Base64-encoded string using MegaNode.getBase64Handle(). Conversions
     * between these formats can be done using MegaApiJava.base64ToHandle() and MegaApiJava.handleToBase64().
     * 
     * @param handle
     *            Node handle to check.
     * @return MegaNode object with the handle, otherwise null.
     */
    public MegaNode getNodeByHandle(long handle) {
        return megaApi.getNodeByHandle(handle);
    }

    /**
     * Get the MegaContactRequest that has a specific handle.
     * <p>
     * You can get the handle of a MegaContactRequest using MegaContactRequestgetHandle().
     * You take the ownership of the returned value.
     *
     * @param handle Contact request handle to check.
     * @return MegaContactRequest object with the handle, otherwise null.
     */
    public MegaContactRequest getContactRequestByHandle(long handle) {
        return megaApi.getContactRequestByHandle(handle);
    }

    /**
     * Get all contacts of this MEGA account.
     * 
     * @return List of MegaUser object with all contacts of this account.
     */
    public ArrayList<MegaUser> getContacts() {
        return userListToArray(megaApi.getContacts());
    }

    /**
     * Get the MegaUser that has a specific email address.
     * <p>
     * You can get the email of a MegaUser using MegaUser.getEmail().
     * 
     * @param email
     *            Email address to check.
     * @return MegaUser that has the email address, otherwise null.
     */
    public MegaUser getContact(String email) {
        return megaApi.getContact(email);
    }

    /**
     * Get all MegaUserAlerts for the logged in user
     *
     * You take the ownership of the returned value
     *
     * @return List of MegaUserAlert objects
     */
    public ArrayList<MegaUserAlert> getUserAlerts(){
        return userAlertListToArray(megaApi.getUserAlerts());
    }

    /**
     * Get the number of unread user alerts for the logged in user
     *
     * @return Number of unread user alerts
     */
    public int getNumUnreadUserAlerts(){
        return megaApi.getNumUnreadUserAlerts();
    }

    /**
     * Get a list with all inbound shares from one MegaUser.
     * 
     * @param user MegaUser sharing folders with this account.
     * @return List of MegaNode objects that this user is sharing with this account.
     */
    public ArrayList<MegaNode> getInShares(MegaUser user) {
        return nodeListToArray(megaApi.getInShares(user));
    }

    /**
     * Get a list with all inbound shares from one MegaUser.
     *
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     *
     * You take the ownership of the returned value
     *
     * @param user MegaUser sharing folders with this account.
     * @param order Sorting order to use
     * @return List of MegaNode objects that this user is sharing with this account.
     */
    public ArrayList<MegaNode> getInShares(MegaUser user, int order) {
        return nodeListToArray(megaApi.getInShares(user, order));
    }

    /**
     * Get a list with all inbound shares.
     * 
     * @return List of MegaNode objects that other users are sharing with this account.
     */
    public ArrayList<MegaNode> getInShares() {
        return nodeListToArray(megaApi.getInShares());
    }

    /**
     * Get a list with all inbound shares.
     *
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     *
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaNode objects that other users are sharing with this account.
     */
    public ArrayList<MegaNode> getInShares(int order) {
        return nodeListToArray(megaApi.getInShares(order));
    }
    
    /**
     * Get a list with all active inboud sharings
     *
     * You take the ownership of the returned value
     *
     * @return List of MegaShare objects that other users are sharing with this account
     */
    public ArrayList<MegaShare> getInSharesList() {
    	return shareListToArray(megaApi.getInSharesList());
    }

    /**
     * Get a list with all active inboud sharings
     *
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     *
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaShare objects that other users are sharing with this account
     */
    public ArrayList<MegaShare> getInSharesList(int order) {
        return shareListToArray(megaApi.getInSharesList(order));
    }

    /**
     * Get the user relative to an incoming share
     *
     * This function will return NULL if the node is not found.
     *
     * If recurse is true, it will return NULL if the root corresponding to
     * the node received as argument doesn't represent the root of an incoming share.
     * Otherwise, it will return NULL if the node doesn't represent
     * the root of an incoming share.
     *
     * You take the ownership of the returned value
     *
     * @param node Node to look for inshare user.
     * @return MegaUser relative to the incoming share
     */
    public MegaUser getUserFromInShare(MegaNode node) {
        return megaApi.getUserFromInShare(node);
    }

    /**
     * Get the user relative to an incoming share
     *
     * This function will return NULL if the node is not found.
     *
     * When recurse is true and the root of the specified node is not an incoming share,
     * this function will return NULL.
     * When recurse is false and the specified node doesn't represent the root of an
     * incoming share, this function will return NULL.
     *
     * You take the ownership of the returned value
     *
     * @param node Node to look for inshare user.
     * @param recurse use root node corresponding to the node passed
     * @return MegaUser relative to the incoming share
     */
    public MegaUser getUserFromInShare(MegaNode node, boolean recurse) {
        return megaApi.getUserFromInShare(node, recurse);
    }

    /**
     * Check if a MegaNode is being shared by/with your own user
     *
     * For nodes that are being shared, you can get a a list of MegaShare
     * objects using MegaApiJava.getOutShares(), or a list of MegaNode objects
     * using MegaApi::getInShares
     * 
     * @param node Node to check.
     * @return true is the MegaNode is being shared, otherwise false.
     * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
     * Use MegaNode::isShared instead
     */
    public boolean isShared(MegaNode node) {
        return megaApi.isShared(node);
    }
    
    /**
     * Check if a MegaNode is being shared with other users
     *
     * For nodes that are being shared, you can get a list of MegaShare
     * objects using MegaApi::getOutShares
     *
     * @param node Node to check
     * @return true is the MegaNode is being shared, otherwise false
     * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
     * Use MegaNode::isOutShare instead
     */
    public boolean isOutShare(MegaNode node) {
    	return megaApi.isOutShare(node);
    }
    
    /**
     * Check if a MegaNode belong to another User, but it is shared with you
     *
     * For nodes that are being shared, you can get a list of MegaNode
     * objects using MegaApi::getInShares
     *
     * @param node Node to check
     * @return true is the MegaNode is being shared, otherwise false
     * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
     * Use MegaNode::isInShare instead
     */
    public boolean isInShare(MegaNode node) {
    	return megaApi.isInShare(node);
    }
    
    /**
     * Check if a MegaNode is pending to be shared with another User. This situation
     * happens when a node is to be shared with a User which is not a contact yet.
     *
     * For nodes that are pending to be shared, you can get a list of MegaNode
     * objects using MegaApi::getPendingShares
     *
     * @param node Node to check
     * @return true is the MegaNode is pending to be shared, otherwise false
     */
    public boolean isPendingShare(MegaNode node) {
    	return megaApi.isPendingShare(node);
    }

    /**
     * Get a list with all active and pending outbound sharings
     * 
     * @return List of MegaShare objects.
     */
    public ArrayList<MegaShare> getOutShares() {
        return shareListToArray(megaApi.getOutShares());
    }

    /**
     * Get a list with the active and pending outbound sharings for a MegaNode
     *
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     *
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaShare objects.
     */
    public ArrayList<MegaShare> getOutShares(int order) {
        return shareListToArray(megaApi.getOutShares(order));
    }

    /**
     * Get a list with the active and pending outbound sharings for a MegaNode
     *
     * If the node doesn't exist in the account, this function returns an empty list.
     *
     * You take the ownership of the returned value
     *
     * @param node MegaNode to check.
     * @return List of MegaShare objects.
     */
    public ArrayList<MegaShare> getOutShares(MegaNode node) {
        return shareListToArray(megaApi.getOutShares(node));
    }

    /**
     * Get a list with all pending outbound sharings
     *
     * You take the ownership of the returned value
     *
     * @return List of MegaShare objects
     * @deprecated Use MegaNode::getOutShares instead of this function
     */
    public ArrayList<MegaShare> getPendingOutShares() {
        return shareListToArray(megaApi.getPendingOutShares());
    }

    /**
     * Get a list with all pending outbound sharings
     *
     * You take the ownership of the returned value
     *
     * @param node MegaNode to check.
     * @return List of MegaShare objects.
     * @deprecated Use MegaNode::getOutShares instead of this function
     */
    public ArrayList<MegaShare> getPendingOutShares(MegaNode node) {
        return shareListToArray(megaApi.getPendingOutShares(node));
    }
    
    /**
     * Get a list with all public links
     *
     * You take the ownership of the returned value
     *
     * @return List of MegaNode objects that are shared with everyone via public link
     */
    public ArrayList<MegaNode> getPublicLinks() {
    	return nodeListToArray(megaApi.getPublicLinks());
    }

    /**
     * Get a list with all public links
     *
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC, MegaApi::ORDER_LINK_CREATION_ASC,
     * MegaApi::ORDER_LINK_CREATION_DESC
     *
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaNode objects that are shared with everyone via public link
     */
    public ArrayList<MegaNode> getPublicLinks(int order) {
        return nodeListToArray(megaApi.getPublicLinks(order));
    }
    
    /**
     * Get a list with all incoming contact requests.
     *
     * You take the ownership of the returned value
     *
     * @return List of MegaContactRequest objects.
     */
    public ArrayList<MegaContactRequest> getIncomingContactRequests() {
        return contactRequestListToArray(megaApi.getIncomingContactRequests());
    }

    /**
     * Get a list with all outgoing contact requests.
     *
     * You take the ownership of the returned value
     *
     * @return List of MegaContactRequest objects.
     */
    public ArrayList<MegaContactRequest> getOutgoingContactRequests() {
        return contactRequestListToArray(megaApi.getOutgoingContactRequests());
    }

    /**
     * Get the access level of a MegaNode.
     * 
     * @param node
     *            MegaNode to check.
     * @return Access level of the node.
     *         Valid values are: <br>
     *         - MegaShare.ACCESS_OWNER. <br>
     *         - MegaShare.ACCESS_FULL. <br>
     *         - MegaShare.ACCESS_READWRITE. <br>
     *         - MegaShare.ACCESS_READ. <br>
     *         - MegaShare.ACCESS_UNKNOWN.
     */
    public int getAccess(MegaNode node) {
        return megaApi.getAccess(node);
    }

    /**
     * Get the size of a node tree.
     * <p>
     * If the MegaNode is a file, this function returns the size of the file.
     * If it's a folder, this function returns the sum of the sizes of all nodes
     * in the node tree.
     * 
     * @param node
     *            Parent node.
     * @return Size of the node tree.
     */
    public long getSize(MegaNode node) {
        return megaApi.getSize(node);
    }

    /**
     * Get a Base64-encoded fingerprint for a local file.
     * <p>
     * The fingerprint is created taking into account the modification time of the file
     * and file contents. This fingerprint can be used to get a corresponding node in MEGA
     * using MegaApiJava.getNodeByFingerprint().
     * <p>
     * If the file can't be found or can't be opened, this function returns null.
     * 
     * @param filePath
     *            Local file path.
     * @return Base64-encoded fingerprint for the file.
     */
    public String getFingerprint(String filePath) {
        return megaApi.getFingerprint(filePath);
    }

    /**
     * Get a Base64-encoded fingerprint for a node.
     * <p>
     * If the node does not exist or does not have a fingerprint, this function returns null.
     * 
     * @param node
     *            Node for which we want to get the fingerprint.
     * @return Base64-encoded fingerprint for the file.
     */
    public String getFingerprint(MegaNode node) {
        return megaApi.getFingerprint(node);
    }

    /**
     * Returns a node with the provided fingerprint.
     * <p>
     * If there is not any node in the account with that fingerprint, this function returns null.
     * 
     * @param fingerprint
     *            Fingerprint to check.
     * @return MegaNode object with the provided fingerprint.
     */
    public MegaNode getNodeByFingerprint(String fingerprint) {
        return megaApi.getNodeByFingerprint(fingerprint);
    }

    /**
     * Returns a node with the provided fingerprint in a preferred parent folder.
     * <p>
     * If there is not any node in the account with that fingerprint, this function returns null.
     * 
     * @param fingerprint
     *            Fingerprint to check.
     * @param preferredParent
     *            Preferred parent if several matches are found.
     * @return MegaNode object with the provided fingerprint.
     */
    public MegaNode getNodeByFingerprint(String fingerprint, MegaNode preferredParent) {
        return megaApi.getNodeByFingerprint(fingerprint, preferredParent);
    }
    
    /**
     * Returns all nodes that have a fingerprint
     *
     * If there isn't any node in the account with that fingerprint, this function returns an empty MegaNodeList.
     *
     * @param fingerprint Fingerprint to check
     * @return List of nodes with the same fingerprint
     */
    public ArrayList<MegaNode> getNodesByFingerprint(String fingerprint) {
    	return nodeListToArray(megaApi.getNodesByFingerprint(fingerprint));
    }

    /**
     * Returns a node with the provided fingerprint that can be exported
     *
     * If there isn't any node in the account with that fingerprint, this function returns null.
     * If a file name is passed in the second parameter, it's also checked if nodes with a matching
     * fingerprint has that name. If there isn't any matching node, this function returns null.
     * This function ignores nodes that are inside the Rubbish Bin because public links to those nodes
     * can't be downloaded.
     *
     * @param fingerprint Fingerprint to check
     * @param name Name that the node should have
     * @return Exportable node that meet the requirements
     */
    public MegaNode getExportableNodeByFingerprint(String fingerprint, String name) {
    	return megaApi.getExportableNodeByFingerprint(fingerprint, name);
    }
    
    /**
     * Returns a node with the provided fingerprint that can be exported
     *
     * If there isn't any node in the account with that fingerprint, this function returns null.
     * This function ignores nodes that are inside the Rubbish Bin because public links to those nodes
     * can't be downloaded.
     *
     * @param fingerprint Fingerprint to check
     * @return Exportable node that meet the requirements
     */
    public MegaNode getExportableNodeByFingerprint(String fingerprint) {
    	return megaApi.getExportableNodeByFingerprint(fingerprint);
    }
    
    
    /**
     * Check if the account already has a node with the provided fingerprint.
     * <p>
     * A fingerprint for a local file can be generated using MegaApiJava.getFingerprint().
     * 
     * @param fingerprint
     *            Fingerprint to check.
     * @return true if the account contains a node with the same fingerprint.
     */
    public boolean hasFingerprint(String fingerprint) {
        return megaApi.hasFingerprint(fingerprint);
    }
    
    /**
     * getCRC Get the CRC of a file
     *
     * The CRC of a file is a hash of its contents.
     * If you need a more realiable method to check files, use fingerprint functions
     * (MegaApi::getFingerprint, MegaApi::getNodeByFingerprint) that also takes into
     * account the size and the modification time of the file to create the fingerprint.
     *
     * @param filePath Local file path
     * @return Base64-encoded CRC of the file
     */
    public String getCRC(String filePath) {
    	return megaApi.getCRC(filePath);
    }
    
    /**
     * Get the CRC from a fingerprint
     *
     * @param fingerprint fingerprint from which we want to get the CRC
     * @return Base64-encoded CRC from the fingerprint
     */
    public String getCRCFromFingerprint(String fingerprint) {
    	return megaApi.getCRCFromFingerprint(fingerprint);
    }
    
    /**
     * getCRC Get the CRC of a node
     *
     * The CRC of a node is a hash of its contents.
     * If you need a more realiable method to check files, use fingerprint functions
     * (MegaApi::getFingerprint, MegaApi::getNodeByFingerprint) that also takes into
     * account the size and the modification time of the node to create the fingerprint.
     *
     * @param node Node for which we want to get the CRC
     * @return Base64-encoded CRC of the node
     */
    public String getCRC(MegaNode node) {
    	return megaApi.getCRC(node);
    }
    
    /**
     * getNodeByCRC Returns a node with the provided CRC
     *
     * If there isn't any node in the selected folder with that CRC, this function returns NULL.
     * If there are several nodes with the same CRC, anyone can be returned.
     *
     * @param crc CRC to check
     * @param parent Parent node to scan. It must be a folder.
     * @return  Node with the selected CRC in the selected folder, or NULL
     * if it's not found.
     */
    public MegaNode getNodeByCRC(String crc, MegaNode parent) {
    	return megaApi.getNodeByCRC(crc, parent);
    }

    /**
     * Check if a node has an access level.
     * 
     * @param node
     *            Node to check.
     * @param level
     *            Access level to check.
     *            Valid values for this parameter are: <br>
     *            - MegaShare.ACCESS_OWNER. <br>
     *            - MegaShare.ACCESS_FULL. <br>
     *            - MegaShare.ACCESS_READWRITE. <br>
     *            - MegaShare.ACCESS_READ.
     * @return MegaError object with the result.
     *         Valid values for the error code are: <br>
     *         - MegaError.API_OK - The node has the required access level. <br>
     *         - MegaError.API_EACCESS - The node does not have the required access level. <br>
     *         - MegaError.API_ENOENT - The node does not exist in the account. <br>
     *         - MegaError.API_EARGS - Invalid parameters.
     */
    public MegaError checkAccess(MegaNode node, int level) {
        return megaApi.checkAccess(node, level);
    }

    /**
     * Check if a node can be moved to a target node.
     * 
     * @param node
     *            Node to check.
     * @param target
     *            Target for the move operation.
     * @return MegaError object with the result.
     *         Valid values for the error code are: <br>
     *         - MegaError.API_OK - The node can be moved to the target. <br>
     *         - MegaError.API_EACCESS - The node can't be moved because of permissions problems. <br>
     *         - MegaError.API_ECIRCULAR - The node can't be moved because that would create a circular linkage. <br>
     *         - MegaError.API_ENOENT - The node or the target does not exist in the account. <br>
     *         - MegaError.API_EARGS - Invalid parameters.
     */
    public MegaError checkMove(MegaNode node, MegaNode target) {
        return megaApi.checkMove(node, target);
    }
    
    /**
     * Check if the MEGA filesystem is available in the local computer
     *
     * This function returns true after a successful call to MegaApi::fetchNodes,
     * otherwise it returns false
     *
     * @return True if the MEGA filesystem is available
     */
    public boolean isFilesystemAvailable() {
    	return megaApi.isFilesystemAvailable();
    }    

    /**
     * Returns the root node of the account.
     * <p>
     * If you haven't successfully called MegaApiJava.fetchNodes() before,
     * this function returns null.
     * 
     * @return Root node of the account.
     */
    public MegaNode getRootNode() {
        return megaApi.getRootNode();
    }

    /**
     * Check if a node is in the Cloud Drive tree
     *
     * @param node Node to check
     * @return True if the node is in the cloud drive
     */
    public boolean isInCloud(MegaNode node){
        return megaApi.isInCloud(node);
    }

    /**
     * Check if a node is in the Rubbish bin tree
     *
     * @param node Node to check
     * @return True if the node is in the Rubbish bin
     */
    public boolean isInRubbish(MegaNode node){
        return megaApi.isInRubbish(node);
    }

    /**
     * Check if a node is in the Inbox tree
     *
     * @param node Node to check
     * @return True if the node is in the Inbox
     */
    public boolean isInInbox(MegaNode node){
        return megaApi.isInInbox(node);
    }

    /**
     * Returns the inbox node of the account.
     * <p>
     * If you haven't successfully called MegaApiJava.fetchNodes() before,
     * this function returns null.
     * 
     * @return Inbox node of the account.
     */
    public MegaNode getInboxNode() {
        return megaApi.getInboxNode();
    }

    /**
     * Returns the rubbish node of the account.
     * <p>
     * If you haven't successfully called MegaApiJava.fetchNodes() before,
     * this function returns null.
     * 
     * @return Rubbish node of the account.
     */
    public MegaNode getRubbishNode() {
        return megaApi.getRubbishNode();
    }
    
    /**
     * Get the time (in seconds) during which transfers will be stopped due to a bandwidth overquota
     * @return Time (in seconds) during which transfers will be stopped, otherwise 0
     */
    public long getBandwidthOverquotaDelay() {
    	return megaApi.getBandwidthOverquotaDelay();
    }

    /**
     * Search nodes containing a search string in their name.
     * <p>
     * The search is case-insensitive.
     *
     * @param parent
     *            The parent node of the tree to explore.
     * @param searchString
     *            Search string. The search is case-insensitive.
     * @param recursive
     *            true if you want to search recursively in the node tree.
     *            false if you want to search in the children of the node only.
     *
     * @param order Order for the returned list
     * Valid values for this parameter are:
     * - MegaApi::ORDER_NONE = 0
     *  Undefined order
     *
     *  - MegaApi::ORDER_DEFAULT_ASC = 1
     *  Folders first in alphabetical order, then files in the same order
     *
     *  - MegaApi::ORDER_DEFAULT_DESC = 2
     *  Files first in reverse alphabetical order, then folders in the same order
     *
     *  - MegaApi::ORDER_SIZE_ASC = 3
     *  Sort by size, ascending
     *
     *  - MegaApi::ORDER_SIZE_DESC = 4
     *  Sort by size, descending
     *
     *  - MegaApi::ORDER_CREATION_ASC = 5
     *  Sort by creation time in MEGA, ascending
     *
     *  - MegaApi::ORDER_CREATION_DESC = 6
     *  Sort by creation time in MEGA, descending
     *
     *  - MegaApi::ORDER_MODIFICATION_ASC = 7
     *  Sort by modification time of the original file, ascending
     *
     *  - MegaApi::ORDER_MODIFICATION_DESC = 8
     *  Sort by modification time of the original file, descending
     *
     *  - MegaApi::ORDER_ALPHABETICAL_ASC = 9
     *  Sort in alphabetical order, ascending
     *
     *  - MegaApi::ORDER_ALPHABETICAL_DESC = 10
     *  Sort in alphabetical order, descending
     *
     * @return List of nodes that contain the desired string in their name.
     */
    public ArrayList<MegaNode> search(MegaNode parent, String searchString, boolean recursive, int order) {
        return nodeListToArray(megaApi.search(parent, searchString, recursive, order));
    }

    /**
     * Search nodes containing a search string in their name
     *
     * The search is case-insensitive.
     *
     * You take the ownership of the returned value.
     *
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * @param node The parent node of the tree to explore
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken MegaCancelToken to be able to cancel the processing at any time.
     * @param recursive True if you want to seach recursively in the node tree.
     * False if you want to seach in the children of the node only
     * @param order Order for the returned list
     * Valid values for this parameter are:
     * - MegaApi::ORDER_NONE = 0
     * Undefined order
     *
     * - MegaApi::ORDER_DEFAULT_ASC = 1
     * Folders first in alphabetical order, then files in the same order
     *
     * - MegaApi::ORDER_DEFAULT_DESC = 2
     * Files first in reverse alphabetical order, then folders in the same order
     *
     * - MegaApi::ORDER_SIZE_ASC = 3
     * Sort by size, ascending
     *
     * - MegaApi::ORDER_SIZE_DESC = 4
     * Sort by size, descending
     *
     * - MegaApi::ORDER_CREATION_ASC = 5
     * Sort by creation time in MEGA, ascending
     *
     * - MegaApi::ORDER_CREATION_DESC = 6
     * Sort by creation time in MEGA, descending
     *
     * - MegaApi::ORDER_MODIFICATION_ASC = 7
     * Sort by modification time of the original file, ascending
     *
     * - MegaApi::ORDER_MODIFICATION_DESC = 8
     * Sort by modification time of the original file, descending
     *
     * - MegaApi::ORDER_ALPHABETICAL_ASC = 9
     * Sort in alphabetical order, ascending
     *
     * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
     * Sort in alphabetical order, descending
     *
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> search(MegaNode node, String searchString, MegaCancelToken cancelToken, boolean recursive, int order) {
        return nodeListToArray(megaApi.search(node, searchString, cancelToken, recursive, order));
    }

    /**
     * Search nodes containing a search string in their name.
     * <p>
     * The search is case-insensitive.
     * 
     * @param parent
     *            The parent node of the tree to explore.
     * @param searchString
     *            Search string. The search is case-insensitive.
     * @param recursive
     *            true if you want to search recursively in the node tree.
     *            false if you want to search in the children of the node only.
     * 
     * @return List of nodes that contain the desired string in their name.
     */
    public ArrayList<MegaNode> search(MegaNode parent, String searchString, boolean recursive) {
        return nodeListToArray(megaApi.search(parent, searchString, recursive));
    }

    /**
     * Search nodes containing a search string in their name.
     * <p>
     * The search is case-insensitive.
     * 
     * @param parent
     *            The parent node of the tree to explore.
     * @param searchString
     *            Search string. The search is case-insensitive.
     * 
     * @return List of nodes that contain the desired string in their name.
     */
    public ArrayList<MegaNode> search(MegaNode parent, String searchString) {
        return nodeListToArray(megaApi.search(parent, searchString));
    }

    /**
     * Search nodes containing a search string in their name.
     * <p>
     * The search is case-insensitive.
     *
     * @param searchString
     *            Search string. The search is case-insensitive.
     *
     * @param order Order for the returned list
     * Valid values for this parameter are:
     * - MegaApi::ORDER_NONE = 0
     *  Undefined order
     *
     *  - MegaApi::ORDER_DEFAULT_ASC = 1
     *  Folders first in alphabetical order, then files in the same order
     *
     *  - MegaApi::ORDER_DEFAULT_DESC = 2
     *  Files first in reverse alphabetical order, then folders in the same order
     *
     *  - MegaApi::ORDER_SIZE_ASC = 3
     *  Sort by size, ascending
     *
     *  - MegaApi::ORDER_SIZE_DESC = 4
     *  Sort by size, descending
     *
     *  - MegaApi::ORDER_CREATION_ASC = 5
     *  Sort by creation time in MEGA, ascending
     *
     *  - MegaApi::ORDER_CREATION_DESC = 6
     *  Sort by creation time in MEGA, descending
     *
     *  - MegaApi::ORDER_MODIFICATION_ASC = 7
     *  Sort by modification time of the original file, ascending
     *
     *  - MegaApi::ORDER_MODIFICATION_DESC = 8
     *  Sort by modification time of the original file, descending
     *
     *  - MegaApi::ORDER_ALPHABETICAL_ASC = 9
     *  Sort in alphabetical order, ascending
     *
     *  - MegaApi::ORDER_ALPHABETICAL_DESC = 10
     *  Sort in alphabetical order, descending
     *
     * @return List of nodes that contain the desired string in their name.
     */
    public ArrayList<MegaNode> search(String searchString, int order) {
        return nodeListToArray(megaApi.search(searchString, order));
    }

    /**
     * Search nodes containing a search string in their name
     *
     * The search is case-insensitive.
     *
     * The search will consider every accessible node for the account:
     *  - Cloud drive
     *  - Inbox
     *  - Rubbish bin
     *  - Incoming shares from other users
     *
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * You take the ownership of the returned value.
     *
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken MegaCancelToken to be able to cancel the processing at any time.
     * @param order Order for the returned list
     * Valid values for this parameter are:
     * - MegaApi::ORDER_NONE = 0
     * Undefined order
     *
     * - MegaApi::ORDER_DEFAULT_ASC = 1
     * Folders first in alphabetical order, then files in the same order
     *
     * - MegaApi::ORDER_DEFAULT_DESC = 2
     * Files first in reverse alphabetical order, then folders in the same order
     *
     * - MegaApi::ORDER_SIZE_ASC = 3
     * Sort by size, ascending
     *
     * - MegaApi::ORDER_SIZE_DESC = 4
     * Sort by size, descending
     *
     * - MegaApi::ORDER_CREATION_ASC = 5
     * Sort by creation time in MEGA, ascending
     *
     * - MegaApi::ORDER_CREATION_DESC = 6
     * Sort by creation time in MEGA, descending
     *
     * - MegaApi::ORDER_MODIFICATION_ASC = 7
     * Sort by modification time of the original file, ascending
     *
     * - MegaApi::ORDER_MODIFICATION_DESC = 8
     * Sort by modification time of the original file, descending
     *
     * - MegaApi::ORDER_ALPHABETICAL_ASC = 9
     * Sort in alphabetical order, ascending
     *
     * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
     * Sort in alphabetical order, descending
     *
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> search(String searchString, MegaCancelToken cancelToken, int order) {
        return nodeListToArray(megaApi.search(searchString, cancelToken, order));
    }


    /**
     * Search nodes containing a search string in their name
     *
     * The search is case-insensitive.
     *
     * The search will consider every accessible node for the account:
     *  - Cloud drive
     *  - Inbox
     *  - Rubbish bin
     *  - Incoming shares from other users
     *
     * You take the ownership of the returned value.
     *
     * @param searchString Search string. The search is case-insensitive
     *
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> search(String searchString) {
        return nodeListToArray(megaApi.search(searchString));
    }


    /**
     * Return a list of buckets, each bucket containing a list of recently added/modified nodes
     *
     * Each bucket contains files that were added/modified in a set, by a single user.
     *
     * @param days Age of actions since added/modified nodes will be considered (in days)
     * @param maxnodes Maximum amount of nodes to be considered
     *
     * @return List of buckets containing nodes that were added/modifed as a set
     */
    public ArrayList<MegaRecentActionBucket> getRecentActions (long days, long maxnodes) {
        return recentActionsToArray(megaApi.getRecentActions(days, maxnodes));
    }

    /**
     * Return a list of buckets, each bucket containing a list of recently added/modified nodes
     *
     * Each bucket contains files that were added/modified in a set, by a single user.
     *
     * This function uses the default parameters for the MEGA apps, which consider (currently)
     * interactions during the last 90 days and max 10.000 nodes.
     *
     * @return List of buckets containing nodes that were added/modifed as a set
     */

    public ArrayList<MegaRecentActionBucket> getRecentActions () {
        return recentActionsToArray(megaApi.getRecentActions());
    }

    /**
     * Process a node tree using a MegaTreeProcessor implementation.
     * 
     * @param parent
     *            The parent node of the tree to explore.
     * @param processor
     *            MegaTreeProcessor that will receive callbacks for every node in the tree.
     * @param recursive
     *            true if you want to recursively process the whole node tree.
     *            false if you want to process the children of the node only.
     * 
     * @return true if all nodes were processed. false otherwise (the operation can be
     *         cancelled by MegaTreeProcessor.processMegaNode()).
     */
    public boolean processMegaTree(MegaNode parent, MegaTreeProcessorInterface processor, boolean recursive) {
        DelegateMegaTreeProcessor delegateListener = new DelegateMegaTreeProcessor(this, processor);
        activeMegaTreeProcessors.add(delegateListener);
        boolean result = megaApi.processMegaTree(parent, delegateListener, recursive);
        activeMegaTreeProcessors.remove(delegateListener);
        return result;
    }

    /**
     * Process a node tree using a MegaTreeProcessor implementation.
     * 
     * @param parent
     *            The parent node of the tree to explore.
     * @param processor
     *            MegaTreeProcessor that will receive callbacks for every node in the tree.
     * 
     * @return true if all nodes were processed. false otherwise (the operation can be
     *         cancelled by MegaTreeProcessor.processMegaNode()).
     */
    public boolean processMegaTree(MegaNode parent, MegaTreeProcessorInterface processor) {
        DelegateMegaTreeProcessor delegateListener = new DelegateMegaTreeProcessor(this, processor);
        activeMegaTreeProcessors.add(delegateListener);
        boolean result = megaApi.processMegaTree(parent, delegateListener);
        activeMegaTreeProcessors.remove(delegateListener);
        return result;
    }

    /**
     * Returns a MegaNode that can be downloaded with any instance of MegaApi
     *
     * This function only allows to authorize file nodes.
     *
     * You can use MegaApi::startDownload with the resulting node with any instance
     * of MegaApi, even if it's logged into another account, a public folder, or not
     * logged in.
     *
     * If the first parameter is a public node or an already authorized node, this
     * function returns a copy of the node, because it can be already downloaded
     * with any MegaApi instance.
     *
     * If the node in the first parameter belongs to the account or public folder
     * in which the current MegaApi object is logged in, this funtion returns an
     * authorized node.
     *
     * If the first parameter is NULL or a node that is not a public node, is not
     * already authorized and doesn't belong to the current MegaApi, this function
     * returns NULL.
     *
     * You take the ownership of the returned value.
     *
     * @param node MegaNode to authorize
     * @return Authorized node, or NULL if the node can't be authorized or is not a file
     */
    public MegaNode authorizeNode(MegaNode node){
        return megaApi.authorizeNode(node);
    }

    /**
     *
     * Returns a MegaNode that can be downloaded/copied with a chat-authorization
     *
     * During preview of chat-links, you need to call this method to authorize the MegaNode
     * from a node-attachment message, so the API allows to access to it. The parameter to
     * authorize the access can be retrieved from MegaChatRoom::getAuthorizationToken when
     * the chatroom in in preview mode.
     *
     * You can use MegaApi::startDownload and/or MegaApi::copyNode with the resulting
     * node with any instance of MegaApi, even if it's logged into another account,
     * a public folder, or not logged in.
     *
     * You take the ownership of the returned value.
     *
     * @param node MegaNode to authorize
     * @param cauth Authorization token (public handle of the chatroom in B64url encoding)
     * @return Authorized node, or NULL if the node can't be authorized
     */
    public MegaNode authorizeChatNode(MegaNode node, String cauth){
        return megaApi.authorizeChatNode(node, cauth);
    }

    /**
     * Get the SDK version.
     * 
     * @return SDK version.
     */
    public String getVersion() {
        return megaApi.getVersion();
    }

    /**
     * Get the User-Agent header used by the SDK.
     * 
     * @return User-Agent used by the SDK.
     */
    public String getUserAgent() {
        return megaApi.getUserAgent();
    }

    /**
     * Changes the API URL.
     *
     * @param apiURL The API URL to change.
     * @param disablepkp boolean. Disable public key pinning if true. Do not disable public key pinning if false.
     */
    public void changeApiUrl(String apiURL, boolean disablepkp) {
        megaApi.changeApiUrl(apiURL, disablepkp);
    }

    /**
     * Changes the API URL.
     * <p>
     * Please note, this method does not disable public key pinning.
     *
     * @param apiURL The API URL to change.
     */
    public void changeApiUrl(String apiURL) {
        megaApi.changeApiUrl(apiURL);
    }

    /**
     * Set the language code used by the app
     * @param languageCode code used by the app
     *
     * @return True if the language code is known for the SDK, otherwise false
     */
    public boolean setLanguage(String languageCode){
        return megaApi.setLanguage(languageCode);
    }

    /**
     * Set the preferred language of the user
     *
     * Valid data in the MegaRequest object received in onRequestFinish:
     * - MegaRequest::getText - Return the language code
     *
     * If the language code is unknown for the SDK, the error code will be MegaError::API_ENOENT
     *
     * This attribute is automatically created by the server. Apps only need
     * to set the new value when the user changes the language.
     *
     * @param Language code to be set
     * @param listener MegaRequestListener to track this request
     */
    public void setLanguagePreference(String languageCode, MegaRequestListenerInterface listener){
        megaApi.setLanguagePreference(languageCode, createDelegateRequestListener(listener));
    }

    /**
     * Get the preferred language of the user
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Return the language code
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getLanguagePreference(MegaRequestListenerInterface listener){
        megaApi.getLanguagePreference(createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable file versioning
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     *
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_DISABLE_VERSIONS
     *
     * Valid data in the MegaRequest object received in onRequestFinish:
     * - MegaRequest::getText - "1" for disable, "0" for enable
     *
     * @param disable True to disable file versioning. False to enable it
     * @param listener MegaRequestListener to track this request
     */
    public void setFileVersionsOption(boolean disable, MegaRequestListenerInterface listener){
        megaApi.setFileVersionsOption(disable, createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable the automatic approval of incoming contact requests using a contact link
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     *
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION
     *
     * Valid data in the MegaRequest object received in onRequestFinish:
     * - MegaRequest::getText - "0" for disable, "1" for enable
     *
     * @param disable True to disable the automatic approval of incoming contact requests using a contact link
     * @param listener MegaRequestListener to track this request
     */
    public void setContactLinksOption(boolean disable, MegaRequestListenerInterface listener){
        megaApi.setContactLinksOption(disable, createDelegateRequestListener(listener));
    }

    /**
     * Check if file versioning is enabled or disabled
     *
     * If the option has never been set, the error code will be MegaError::API_ENOENT.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     *
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_DISABLE_VERSIONS
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - "1" for disable, "0" for enable
     * - MegaRequest::getFlag - True if disabled, false if enabled
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getFileVersionsOption(MegaRequestListenerInterface listener){
        megaApi.getFileVersionsOption(createDelegateRequestListener(listener));
    }

    /**
     * Check if the automatic approval of incoming contact requests using contact links is enabled or disabled
     *
     * If the option has never been set, the error code will be MegaError::API_ENOENT.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     *
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - "0" for disable, "1" for enable
     * - MegaRequest::getFlag - false if disabled, true if enabled
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getContactLinksOption(MegaRequestListenerInterface listener){
        megaApi.getContactLinksOption(createDelegateRequestListener(listener));
    }
    
    /**
     * Keep retrying when public key pinning fails
     *
     * By default, when the check of the MEGA public key fails, it causes an automatic
     * logout. Pass false to this function to disable that automatic logout and
     * keep the SDK retrying the request.
     *
     * Even if the automatic logout is disabled, a request of the type MegaRequest::TYPE_LOGOUT
     * will be automatically created and callbacks (onRequestStart, onRequestFinish) will
     * be sent. However, logout won't be really executed and in onRequestFinish the error code
     * for the request will be MegaError::API_EINCOMPLETE
     *
     * @param enable true to keep retrying failed requests due to a fail checking the MEGA public key
     * or false to perform an automatic logout in that case
     */
    public void retrySSLerrors(boolean enable) {
    	megaApi.retrySSLerrors(enable);
    }
    
    /**
     * Enable / disable the public key pinning
     *
     * Public key pinning is enabled by default for all sensible communications.
     * It is strongly discouraged to disable this feature.
     *
     * @param enable true to keep public key pinning enabled, false to disable it
     */
    public void setPublicKeyPinning(boolean enable) {
    	megaApi.setPublicKeyPinning(enable);
    }

    /**
     * Make a name suitable for a file name in the local filesystem.
     * <p>
     * This function escapes (%xx) forbidden characters in the local filesystem if needed.
     * You can revert this operation using MegaApiJava.unescapeFsIncompatible().
     * 
     * @param name
     *            Name to convert.
     * @return Converted name.
     */
    public String escapeFsIncompatible(String name) {
        return megaApi.escapeFsIncompatible(name);
    }

    /**
     * Unescape a file name escaped with MegaApiJava.escapeFsIncompatible().
     * 
     * @param localName
     *            Escaped name to convert.
     * @return Converted name.
     */
    public String unescapeFsIncompatible(String localName) {
        return megaApi.unescapeFsIncompatible(localName);
    }

    /**
     * Create a thumbnail for an image.
     *
     * @param imagePath Image path.
     * @param dstPath Destination path for the thumbnail (including the file name).
     * @return true if the thumbnail was successfully created, otherwise false.
     */
    public boolean createThumbnail(String imagePath, String dstPath) {
        return megaApi.createThumbnail(imagePath, dstPath);
    }

    /**
     * Create a preview for an image.
     *
     * @param imagePath Image path.
     * @param dstPath Destination path for the preview (including the file name).
     * @return true if the preview was successfully created, otherwise false.
     */
    public boolean createPreview(String imagePath, String dstPath) {
        return megaApi.createPreview(imagePath, dstPath);
    }

    /**
     * Convert a Base64 string to Base32.
     * <p>
     * If the input pointer is null, this function will return null.
     * If the input character array is not a valid base64 string
     * the effect is undefined.
     * 
     * @param base64
     *            null-terminated Base64 character array.
     * @return null-terminated Base32 character array.
     */
    public static String base64ToBase32(String base64) {
        return MegaApi.base64ToBase32(base64);
    }

    /**
     * Convert a Base32 string to Base64.
     * 
     * If the input pointer is null, this function will return null.
     * If the input character array is not a valid base32 string
     * the effect is undefined.
     * 
     * @param base32
     *            null-terminated Base32 character array.
     * @return null-terminated Base64 character array.
     */
    public static String base32ToBase64(String base32) {
        return MegaApi.base32ToBase64(base32);
    }

    /**
    * Recursively remove all local files/folders inside a local path
    * @param path Local path of a folder to start the recursive deletion
    * The folder itself is not deleted
    */
    public static void removeRecursively(String localPath) {
        MegaApi.removeRecursively(localPath);
    }
    
    /**
     * Check if the connection with MEGA servers is OK
     *
     * It can briefly return false even if the connection is good enough when
     * some storage servers are temporarily not available or the load of API
     * servers is high.
     *
     * @return true if the connection is perfectly OK, otherwise false
     */
    public boolean isOnline() {
    	return megaApi.isOnline();
    }

    /**
     * Start an HTTP proxy server in specified port
     *
     * If this function returns true, that means that the server is
     * ready to accept connections. The initialization is synchronous.
     *
     * The server will serve files using this URL format:
     * http://127.0.0.1/<NodeHandle>/<NodeName>
     *
     * The node name must be URL encoded and must match with the node handle.
     * You can generate a correct link for a MegaNode using MegaApi::httpServerGetLocalLink
     *
     * If the node handle belongs to a folder node, a web with the list of files
     * inside the folder is returned.
     *
     * It's important to know that the HTTP proxy server has several configuration options
     * that can restrict the nodes that will be served and the connections that will be accepted.
     *
     * These are the default options:
     * - The restricted mode of the server is set to MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * (see MegaApi::httpServerSetRestrictedMode)
     *
     * - Folder nodes are NOT allowed to be served (see MegaApi::httpServerEnableFolderServer)
     * - File nodes are allowed to be served (see MegaApi::httpServerEnableFileServer)
     * - Subtitles support is disabled (see MegaApi::httpServerEnableSubtitlesSupport)
     *
     * The HTTP server will only stream a node if it's allowed by all configuration options.
     *
     * @param localOnly true to listen on 127.0.0.1 only, false to listen on all network interfaces
     * @param port Port in which the server must accept connections
     * @return True is the server is ready, false if the initialization failed
     */
    public boolean httpServerStart(boolean localOnly, int port) {
        return megaApi.httpServerStart(localOnly, port);
    }

    /**
     * Start an HTTP proxy server in specified port
     *
     * If this function returns true, that means that the server is
     * ready to accept connections. The initialization is synchronous.
     *
     * The server will serve files using this URL format:
     * http://127.0.0.1/<NodeHandle>/<NodeName>
     *
     * The node name must be URL encoded and must match with the node handle.
     * You can generate a correct link for a MegaNode using MegaApi::httpServerGetLocalLink
     *
     * If the node handle belongs to a folder node, a web with the list of files
     * inside the folder is returned.
     *
     * It's important to know that the HTTP proxy server has several configuration options
     * that can restrict the nodes that will be served and the connections that will be accepted.
     *
     * These are the default options:
     * - The restricted mode of the server is set to MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * (see MegaApi::httpServerSetRestrictedMode)
     *
     * - Folder nodes are NOT allowed to be served (see MegaApi::httpServerEnableFolderServer)
     * - File nodes are allowed to be served (see MegaApi::httpServerEnableFileServer)
     * - Subtitles support is disabled (see MegaApi::httpServerEnableSubtitlesSupport)
     *
     * The HTTP server will only stream a node if it's allowed by all configuration options.
     *
     * @param localOnly true to listen on 127.0.0.1 only, false to listen on all network interfaces
     * @return True is the server is ready, false if the initialization failed
     */
    public boolean httpServerStart(boolean localOnly) {
        return megaApi.httpServerStart(localOnly);
    }

    /**
     * Start an HTTP proxy server in specified port
     *
     * If this function returns true, that means that the server is
     * ready to accept connections. The initialization is synchronous.
     *
     * The server will serve files using this URL format:
     * http://127.0.0.1/<NodeHandle>/<NodeName>
     *
     * The node name must be URL encoded and must match with the node handle.
     * You can generate a correct link for a MegaNode using MegaApi::httpServerGetLocalLink
     *
     * If the node handle belongs to a folder node, a web with the list of files
     * inside the folder is returned.
     *
     * It's important to know that the HTTP proxy server has several configuration options
     * that can restrict the nodes that will be served and the connections that will be accepted.
     *
     * These are the default options:
     * - The restricted mode of the server is set to MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * (see MegaApi::httpServerSetRestrictedMode)
     *
     * - Folder nodes are NOT allowed to be served (see MegaApi::httpServerEnableFolderServer)
     * - File nodes are allowed to be served (see MegaApi::httpServerEnableFileServer)
     * - Subtitles support is disabled (see MegaApi::httpServerEnableSubtitlesSupport)
     *
     * The HTTP server will only stream a node if it's allowed by all configuration options.
     *
     * @return True is the server is ready, false if the initialization failed
     */
    public boolean httpServerStart() {
        return megaApi.httpServerStart();
    }

    /**
     * Stop the HTTP proxy server
     *
     * When this function returns, the server is already shutdown.
     * If the HTTP proxy server isn't running, this functions does nothing
     */
    public void httpServerStop(){
        megaApi.httpServerStop();
    }

    /**
     * Check if the HTTP proxy server is running
     * @return 0 if the server is not running. Otherwise the port in which it's listening to
     */
    public int httpServerIsRunning(){
        return megaApi.httpServerIsRunning();
    }

    /**
     * Check if the HTTP proxy server is listening on all network interfaces
     * @return true if the HTTP proxy server is listening on 127.0.0.1 only, or it's not started.
     * If it's started and listening on all network interfaces, this function returns false
     */
    public boolean httpServerIsLocalOnly() {
        return megaApi.httpServerIsLocalOnly();
    }

    /**
     * Allow/forbid to serve files
     *
     * By default, files are served (when the server is running)
     *
     * Even if files are allowed to be served by this function, restrictions related to
     * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
     *
     * @param enable true to allow to server files, false to forbid it
     */
    public void httpServerEnableFileServer(boolean enable) {
        megaApi.httpServerEnableFileServer(enable);
    }

    /**
     * Check if it's allowed to serve files
     *
     * This function can return true even if the HTTP proxy server is not running
     *
     * Even if files are allowed to be served by this function, restrictions related to
     * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
     *
     * @return true if it's allowed to serve files, otherwise false
     */
    public boolean httpServerIsFileServerEnabled() {
        return megaApi.httpServerIsFileServerEnabled();
    }

    /**
     * Allow/forbid to serve folders
     *
     * By default, folders are NOT served
     *
     * Even if folders are allowed to be served by this function, restrictions related to
     * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
     *
     * @param enable true to allow to server folders, false to forbid it
     */
    public void httpServerEnableFolderServer(boolean enable) {
        megaApi.httpServerEnableFolderServer(enable);
    }

    /**
     * Check if it's allowed to serve folders
     *
     * This function can return true even if the HTTP proxy server is not running
     *
     * Even if folders are allowed to be served by this function, restrictions related to
     * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
     *
     * @return true if it's allowed to serve folders, otherwise false
     */
    public boolean httpServerIsFolderServerEnabled() {
        return megaApi.httpServerIsFolderServerEnabled();
    }

    /**
     * Enable/disable the restricted mode of the HTTP server
     *
     * This function allows to restrict the nodes that are allowed to be served.
     * For not allowed links, the server will return "407 Forbidden".
     *
     * Possible values are:
     * - HTTP_SERVER_DENY_ALL = -1
     * All nodes are forbidden
     *
     * - HTTP_SERVER_ALLOW_ALL = 0
     * All nodes are allowed to be served
     *
     * - HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1 (default)
     * Only links created with MegaApi::httpServerGetLocalLink are allowed to be served
     *
     * - HTTP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
     * Only the last link created with MegaApi::httpServerGetLocalLink is allowed to be served
     *
     * If a different value from the list above is passed to this function, it won't have any effect and the previous
     * state of this option will be preserved.
     *
     * The default value of this property is MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     *
     * The state of this option is preserved even if the HTTP server is restarted, but the
     * the HTTP proxy server only remembers the generated links since the last call to
     * MegaApi::httpServerStart
     *
     * Even if nodes are allowed to be served by this function, restrictions related to
     * other configuration options (MegaApi::httpServerEnableFileServer,
     * MegaApi::httpServerEnableFolderServer) are still applied.
     *
     * @param mode Required state for the restricted mode of the HTTP proxy server
     */
    public void httpServerSetRestrictedMode(int mode) {
        megaApi.httpServerSetRestrictedMode(mode);
    }

    /**
     * Check if the HTTP proxy server is working in restricted mode
     *
     * Possible return values are:
     * - HTTP_SERVER_DENY_ALL = -1
     * All nodes are forbidden
     *
     * - HTTP_SERVER_ALLOW_ALL = 0
     * All nodes are allowed to be served
     *
     * - HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1
     * Only links created with MegaApi::httpServerGetLocalLink are allowed to be served
     *
     * - HTTP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
     * Only the last link created with MegaApi::httpServerGetLocalLink is allowed to be served
     *
     * The default value of this property is MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     *
     * See MegaApi::httpServerEnableRestrictedMode and MegaApi::httpServerStart
     *
     * Even if nodes are allowed to be served by this function, restrictions related to
     * other configuration options (MegaApi::httpServerEnableFileServer,
     * MegaApi::httpServerEnableFolderServer) are still applied.
     *
     * @return State of the restricted mode of the HTTP proxy server
     */
    public int httpServerGetRestrictedMode() {
        return megaApi.httpServerGetRestrictedMode();
    }

    /**
     * Enable/disable the support for subtitles
     *
     * Subtitles support allows to stream some special links that otherwise wouldn't be valid.
     * For example, let's suppose that the server is streaming this video:
     * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.avi
     *
     * Some media players scan HTTP servers looking for subtitle files and request links like these ones:
     * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.txt
     * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.srt
     *
     * Even if a file with that name is in the same folder of the MEGA account, the node wouldn't be served because
     * the node handle wouldn't match.
     *
     * When this feature is enabled, the HTTP proxy server will check if there are files with that name
     * in the same folder as the node corresponding to the handle in the link.
     *
     * If a matching file is found, the name is exactly the same as the the node with the specified handle
     * (except the extension), the node with that handle is allowed to be streamed and this feature is enabled
     * the HTTP proxy server will serve that file.
     *
     * This feature is disabled by default.
     *
     * @param enable True to enable subtitles support, false to disable it
     */
    public void httpServerEnableSubtitlesSupport(boolean enable) {
        megaApi.httpServerEnableSubtitlesSupport(enable);
    }

    /**
     * Check if the support for subtitles is enabled
     *
     * See MegaApi::httpServerEnableSubtitlesSupport.
     *
     * This feature is disabled by default.
     *
     * @return true of the support for subtibles is enables, otherwise false
     */
    public boolean httpServerIsSubtitlesSupportEnabled() {
        return megaApi.httpServerIsSubtitlesSupportEnabled();
    }

    /**
     * Add a listener to receive information about the HTTP proxy server
     *
     * This is the valid data that will be provided on callbacks:
     * - MegaTransfer::getType - It will be MegaTransfer::TYPE_LOCAL_HTTP_DOWNLOAD
     * - MegaTransfer::getPath - URL requested to the HTTP proxy server
     * - MegaTransfer::getFileName - Name of the requested file (if any, otherwise NULL)
     * - MegaTransfer::getNodeHandle - Handle of the requested file (if any, otherwise NULL)
     * - MegaTransfer::getTotalBytes - Total bytes of the response (response headers + file, if required)
     * - MegaTransfer::getStartPos - Start position (for range requests only, otherwise -1)
     * - MegaTransfer::getEndPos - End position (for range requests only, otherwise -1)
     *
     * On the onTransferFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EINCOMPLETE - If the whole response wasn't sent
     * (it's normal to get this error code sometimes because media players close connections when they have
     * the data that they need)
     *
     * - MegaError::API_EREAD - If the connection with MEGA storage servers failed
     * - MegaError::API_EAGAIN - If the download speed is too slow for streaming
     * - A number > 0 means an HTTP error code returned to the client
     *
     * @param listener Listener to receive information about the HTTP proxy server
     */
    public void httpServerAddListener(MegaTransferListenerInterface listener) {
        megaApi.httpServerAddListener(createDelegateHttpServerListener(listener, false));
    }

    /**
     * Stop the reception of callbacks related to the HTTP proxy server on this listener
     * @param listener Listener that won't continue receiving information
     */
    public void httpServerRemoveListener(MegaTransferListenerInterface listener) {
        ArrayList<DelegateMegaTransferListener> listenersToRemove = new ArrayList<DelegateMegaTransferListener>();

        synchronized (activeHttpServerListeners) {
            Iterator<DelegateMegaTransferListener> it = activeHttpServerListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaTransferListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                    listenersToRemove.add(delegate);
                    it.remove();
                }
            }
        }

        for (int i=0;i<listenersToRemove.size();i++){
            megaApi.httpServerRemoveListener(listenersToRemove.get(i));
        }
    }

    /**
     * Returns a URL to a node in the local HTTP proxy server
     *
     * The HTTP proxy server must be running before using this function, otherwise
     * it will return NULL.
     *
     * You take the ownership of the returned value
     *
     * @param node Node to generate the local HTTP link
     * @return URL to the node in the local HTTP proxy server, otherwise NULL
     */
    public String httpServerGetLocalLink(MegaNode node) {
        return megaApi.httpServerGetLocalLink(node);
    }

    /**
     * Set the maximum buffer size for the internal buffer
     *
     * The HTTP proxy server has an internal buffer to store the data received from MEGA
     * while it's being sent to clients. When the buffer is full, the connection with
     * the MEGA storage server is closed, when the buffer has few data, the connection
     * with the MEGA storage server is started again.
     *
     * Even with very fast connections, due to the possible latency starting new connections,
     * if this buffer is small the streaming can have problems due to the overhead caused by
     * the excessive number of POST requests.
     *
     * It's recommended to set this buffer at least to 1MB
     *
     * For connections that request less data than the buffer size, the HTTP proxy server
     * will only allocate the required memory to complete the request to minimize the
     * memory usage.
     *
     * The new value will be taken into account since the next request received by
     * the HTTP proxy server, not for ongoing requests. It's possible and effective
     * to call this function even before the server has been started, and the value
     * will be still active even if the server is stopped and started again.
     *
     * @param bufferSize Maximum buffer size (in bytes) or a number <= 0 to use the
     * internal default value
     */
    public void httpServerSetMaxBufferSize(int bufferSize) {
        megaApi.httpServerSetMaxBufferSize(bufferSize);
    }

    /**
     * Get the maximum size of the internal buffer size
     *
     * See MegaApi::httpServerSetMaxBufferSize
     *
     * @return Maximum size of the internal buffer size (in bytes)
     */
    public int httpServerGetMaxBufferSize() {
        return megaApi.httpServerGetMaxBufferSize();
    }

    /**
     * Set the maximum size of packets sent to clients
     *
     * For each connection, the HTTP proxy server only sends one write to the underlying
     * socket at once. This parameter allows to set the size of that write.
     *
     * A small value could cause a lot of writes and would lower the performance.
     *
     * A big value could send too much data to the output buffer of the socket. That could
     * keep the internal buffer full of data that hasn't been sent to the client yet,
     * preventing the retrieval of additional data from the MEGA storage server. In that
     * circumstances, the client could read a lot of data at once and the HTTP server
     * could not have enough time to get more data fast enough.
     *
     * It's recommended to set this value to at least 8192 and no more than the 25% of
     * the maximum buffer size (MegaApi::httpServerSetMaxBufferSize).
     *
     * The new value will be takein into account since the next request received by
     * the HTTP proxy server, not for ongoing requests. It's possible and effective
     * to call this function even before the server has been started, and the value
     * will be still active even if the server is stopped and started again.
     *
     * @param outputSize Maximun size of data packets sent to clients (in bytes) or
     * a number <= 0 to use the internal default value
     */
    public void httpServerSetMaxOutputSize(int outputSize) {
        megaApi.httpServerSetMaxOutputSize(outputSize);
    }

    /**
     * Get the maximum size of the packets sent to clients
     *
     * See MegaApi::httpServerSetMaxOutputSize
     *
     * @return Maximum size of the packets sent to clients (in bytes)
     */
    public int httpServerGetMaxOutputSize() {
        return megaApi.httpServerGetMaxOutputSize();
    }

    /**
     * Get the MIME type associated with the extension
     *
     * You take the ownership of the returned value
     *
     * @param extension File extension (with or without a leading dot)
     * @return MIME type associated with the extension
     */
    public static String getMimeType(String extension) {
        return MegaApi.getMimeType(extension);
    }

    /**
     * Register a token for push notifications
     *
     * This function attach a token to the current session, which is intended to get push notifications
     * on mobile platforms like Android and iOS.
     *
     * The associated request type with this request is MegaRequest::TYPE_REGISTER_PUSH_NOTIFICATION
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getText - Returns the token provided.
     * - MegaRequest::getNumber - Returns the device type provided.
     *
     * @param deviceType Integer id for the provider. 1 for Android, 2 for iOS
     * @param token Character array representing the token to be registered.
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void registerPushNotifications(int deviceType, String token, MegaRequestListenerInterface listener) {
        megaApi.registerPushNotifications(deviceType, token, createDelegateRequestListener(listener));
    }

    /**
     * Register a token for push notifications
     *
     * This function attach a token to the current session, which is intended to get push notifications
     * on mobile platforms like Android and iOS.
     *
     * @param deviceType Integer id for the provider. 1 for Android, 2 for iOS
     * @param token Character array representing the token to be registered.
     */
    public void registerPushNotifications(int deviceType, String token) {
        megaApi.registerPushNotifications(deviceType, token);
    }

    /**
     * Get the MEGA Achievements of the account logged in
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always false
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the MEGA Achievements of this account
     *
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void getAccountAchievements(MegaRequestListenerInterface listener) {
        megaApi.getAccountAchievements(createDelegateRequestListener(listener));
    }

    /**
     * Get the MEGA Achievements of the account logged in
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always false
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the MEGA Achievements of this account
     */
    public void getAccountAchievements(){
        megaApi.getAccountAchievements();
    }

    /**
     * Get the list of existing MEGA Achievements
     *
     * Similar to MegaApi::getAccountAchievements, this method returns only the base storage and
     * the details for the different achievement classes, but not awards or rewards related to the
     * account that is logged in.
     * This function can be used to give an indication of what is available for advertising
     * for unregistered users, despite it can be used with a logged in account with no difference.
     *
     * @note: if the IP address is not achievement enabled (it belongs to a country where MEGA
     * Achievements are not enabled), the request will fail with MegaError::API_EACCESS.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the list of existing MEGA Achievements
     *
     * @param listener MegaRequestListenerInterface to track this request
     */
    public void getMegaAchievements(MegaRequestListenerInterface listener) {
        megaApi.getMegaAchievements(createDelegateRequestListener(listener));
    }

    /**
     * Get the list of existing MEGA Achievements
     *
     * Similar to MegaApi::getAccountAchievements, this method returns only the base storage and
     * the details for the different achievement classes, but not awards or rewards related to the
     * account that is logged in.
     * This function can be used to give an indication of what is available for advertising
     * for unregistered users, despite it can be used with a logged in account with no difference.
     *
     * @note: if the IP address is not achievement enabled (it belongs to a country where MEGA
     * Achievements are not enabled), the request will fail with MegaError::API_EACCESS.
     *
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always true
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the list of existing MEGA Achievements
     */
    public void getMegaAchievements() {
        megaApi.getMegaAchievements();
    }
    
    /**
     * Set original fingerprint for MegaNode
     *
     * @param node
     * @param fingerprint
     * @param listener
     */
    
    public void setOriginalFingerprint(MegaNode node, String fingerprint, MegaRequestListenerInterface listener){
        megaApi.setOriginalFingerprint(node,fingerprint,createDelegateRequestListener(listener));
    }
    
    /**
     * Get MegaNode list by original fingerprint
     *
     * @param originalfingerprint
     * @param parent
     */
    
    public MegaNodeList getNodesByOriginalFingerprint(String originalfingerprint, MegaNode parent){
        return megaApi.getNodesByOriginalFingerprint(originalfingerprint, parent);
    }
    
    /**
     * @brief Retrieve basic information about a folder link
     *
     * This function retrieves basic information from a folder link, like the number of files / folders
     * and the name of the folder. For folder links containing a lot of files/folders,
     * this function is more efficient than a fetchnodes.
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink() - Returns the public link to the folder
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaFolderInfo() - Returns information about the contents of the folder
     * - MegaRequest::getNodeHandle() - Returns the public handle of the folder
     * - MegaRequest::getParentHandle() - Returns the handle of the owner of the folder
     * - MegaRequest::getText() - Returns the name of the folder.
     * If there's no name, it returns the special status string "CRYPTO_ERROR".
     * If the length of the name is zero, it returns the special status string "BLANK".
     *
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS  - If the link is not a valid folder link
     * - MegaError::API_EKEY - If the public link does not contain the key or it is invalid
     *
     * @param megaFolderLink Public link to a folder in MEGA
     * @param listener MegaRequestListener to track this request
     */
    public void getPublicLinkInformation(String megaFolderLink, MegaRequestListenerInterface listener) {
        megaApi.getPublicLinkInformation(megaFolderLink, createDelegateRequestListener(listener));
    }

    /**
     * @brief Retrieve basic information about a folder link
     *
     * This function retrieves basic information from a folder link, like the number of files / folders
     * and the name of the folder. For folder links containing a lot of files/folders,
     * this function is more efficient than a fetchnodes.
     *
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink() - Returns the public link to the folder
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaFolderInfo() - Returns information about the contents of the folder
     * - MegaRequest::getNodeHandle() - Returns the public handle of the folder
     * - MegaRequest::getParentHandle() - Returns the handle of the owner of the folder
     * - MegaRequest::getText() - Returns the name of the folder.
     * If there's no name, it returns the special status string "CRYPTO_ERROR".
     * If the length of the name is zero, it returns the special status string "BLANK".
     *
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS  - If the link is not a valid folder link
     * - MegaError::API_EKEY - If the public link does not contain the key or it is invalid
     *
     * @param megaFolderLink Public link to a folder in MEGA
     */
    public void getPublicLinkInformation(String megaFolderLink) {
        megaApi.getPublicLinkInformation(megaFolderLink);
    }
    
    /****************************************************************************************************/
    // INTERNAL METHODS
    /****************************************************************************************************/
    private MegaRequestListener createDelegateRequestListener(MegaRequestListenerInterface listener) {
        DelegateMegaRequestListener delegateListener = new DelegateMegaRequestListener(this, listener, true);
        activeRequestListeners.add(delegateListener);
        return delegateListener;
    }

    private MegaRequestListener createDelegateRequestListener(MegaRequestListenerInterface listener, boolean singleListener) {
        DelegateMegaRequestListener delegateListener = new DelegateMegaRequestListener(this, listener, singleListener);
        activeRequestListeners.add(delegateListener);
        return delegateListener;
    }

    private MegaTransferListener createDelegateTransferListener(MegaTransferListenerInterface listener) {
        DelegateMegaTransferListener delegateListener = new DelegateMegaTransferListener(this, listener, true);
        activeTransferListeners.add(delegateListener);
        return delegateListener;
    }

    private MegaTransferListener createDelegateTransferListener(MegaTransferListenerInterface listener, boolean singleListener) {
        DelegateMegaTransferListener delegateListener = new DelegateMegaTransferListener(this, listener, singleListener);
        activeTransferListeners.add(delegateListener);
        return delegateListener;
    }

    private MegaGlobalListener createDelegateGlobalListener(MegaGlobalListenerInterface listener) {
        DelegateMegaGlobalListener delegateListener = new DelegateMegaGlobalListener(this, listener);
        activeGlobalListeners.add(delegateListener);
        return delegateListener;
    }

    private MegaListener createDelegateMegaListener(MegaListenerInterface listener) {
        DelegateMegaListener delegateListener = new DelegateMegaListener(this, listener);
        activeMegaListeners.add(delegateListener);
        return delegateListener;
    }

    private static MegaLogger createDelegateMegaLogger(MegaLoggerInterface listener){
        DelegateMegaLogger delegateLogger = new DelegateMegaLogger(listener);
        activeMegaLoggers.add(delegateLogger);
        return delegateLogger;
    }

    private MegaTransferListener createDelegateHttpServerListener(MegaTransferListenerInterface listener) {
        DelegateMegaTransferListener delegateListener = new DelegateMegaTransferListener(this, listener, true);
        activeHttpServerListeners.add(delegateListener);
        return delegateListener;
    }

    private MegaTransferListener createDelegateHttpServerListener(MegaTransferListenerInterface listener, boolean singleListener) {
        DelegateMegaTransferListener delegateListener = new DelegateMegaTransferListener(this, listener, singleListener);
        activeHttpServerListeners.add(delegateListener);
        return delegateListener;
    }

    void privateFreeRequestListener(DelegateMegaRequestListener listener) {
        activeRequestListeners.remove(listener);
    }

    void privateFreeTransferListener(DelegateMegaTransferListener listener) {
        activeTransferListeners.remove(listener);
    }

    static public ArrayList<MegaNode> nodeListToArray(MegaNodeList nodeList) {
        if (nodeList == null) {
            return null;
        }

        ArrayList<MegaNode> result = new ArrayList<MegaNode>(nodeList.size());
        for (int i = 0; i < nodeList.size(); i++) {
            result.add(nodeList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaShare> shareListToArray(MegaShareList shareList) {
        if (shareList == null) {
            return null;
        }

        ArrayList<MegaShare> result = new ArrayList<MegaShare>(shareList.size());
        for (int i = 0; i < shareList.size(); i++) {
            result.add(shareList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaContactRequest> contactRequestListToArray(MegaContactRequestList contactRequestList) {
        if (contactRequestList == null) {
            return null;
        }

        ArrayList<MegaContactRequest> result = new ArrayList<MegaContactRequest>(contactRequestList.size());
        for(int i=0; i<contactRequestList.size(); i++) {
            result.add(contactRequestList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaTransfer> transferListToArray(MegaTransferList transferList) {
        if (transferList == null) {
            return null;
        }

        ArrayList<MegaTransfer> result = new ArrayList<MegaTransfer>(transferList.size());
        for (int i = 0; i < transferList.size(); i++) {
            result.add(transferList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaUser> userListToArray(MegaUserList userList) {

        if (userList == null) {
            return null;
        }

        ArrayList<MegaUser> result = new ArrayList<MegaUser>(userList.size());
        for (int i = 0; i < userList.size(); i++) {
            result.add(userList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaUserAlert> userAlertListToArray(MegaUserAlertList userAlertList){

        if (userAlertList == null){
            return null;
        }

        ArrayList<MegaUserAlert> result = new ArrayList<MegaUserAlert>(userAlertList.size());
        for (int i = 0; i < userAlertList.size(); i++){
            result.add(userAlertList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaRecentActionBucket> recentActionsToArray(MegaRecentActionBucketList recentActionList) {
        if (recentActionList == null) {
            return null;
        }

        ArrayList<MegaRecentActionBucket> result = new ArrayList<>(recentActionList.size());
        for (int i = 0; i< recentActionList.size(); i++) {
            result.add(recentActionList.get(i).copy());
        }

        return result;
    }

    /**
     * Provide a phone number to get verification code.
     *
     * @param phoneNumber the phone number to receive the txt with verification code.
     * @param listener callback of this request.
     */
    public void sendSMSVerificationCode(String phoneNumber,nz.mega.sdk.MegaRequestListenerInterface listener) {
        megaApi.sendSMSVerificationCode(phoneNumber,createDelegateRequestListener(listener));
    }

    public void sendSMSVerificationCode(String phoneNumber,nz.mega.sdk.MegaRequestListenerInterface listener,boolean reverifying_whitelisted) {
        megaApi.sendSMSVerificationCode(phoneNumber,createDelegateRequestListener(listener),reverifying_whitelisted);
    }

    /**
     * Send the verification code to verifiy phone number.
     *
     * @param verificationCode received 6 digits verification code.
     * @param listener callback of this request.
     */
    public void checkSMSVerificationCode(String verificationCode,nz.mega.sdk.MegaRequestListenerInterface listener) {
        megaApi.checkSMSVerificationCode(verificationCode,createDelegateRequestListener(listener));
    }

    /**
     * Get the verified phone number of the mega account.
     *
     * @return verified phone number.
     */
    public String smsVerifiedPhoneNumber() {
        return megaApi.smsVerifiedPhoneNumber();
    }
    
    /**
     * Requests the contacts that are registered at MEGA (currently verified through SMS)
     *
     * @param contacts The map of contacts to get registered contacts from
     * @param listener MegaRequestListener to track this request
     */
    public void getRegisteredContacts(MegaStringMap contacts, nz.mega.sdk.MegaRequestListenerInterface listener) {
        megaApi.getRegisteredContacts(contacts, createDelegateRequestListener(listener));
    }
    
    /**
     * Requests the currently available country calling codes
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getCountryCallingCodes(nz.mega.sdk.MegaRequestListenerInterface listener) {
        megaApi.getCountryCallingCodes(createDelegateRequestListener(listener));
    }

    /**
     * Get the state to see whether blocked account could do SMS verification
     * 
     * @return the state 
     */
    public int smsAllowedState() {
        return megaApi.smsAllowedState();
    }

    /**
     * @brief Returns the email of the user who made the changes
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaRecentActionBucket object is deleted.
     *
     * @return The associated user's email
     */
    public void getUserEmail(long handle, nz.mega.sdk.MegaRequestListenerInterface listener) {
        megaApi.getUserEmail(handle, createDelegateRequestListener(listener));
    }

    /**
     * @brief Cancel a registration process
     *
     * If a signup link has been generated during registration process, call this function
     * to invalidate it. The ephemeral session will not be invalidated, only the signup link.
     *
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value 2
     *
     * @param listener MegaRequestListener to track this request
     */
    public void cancelCreateAccount(MegaRequestListenerInterface listener){
        megaApi.cancelCreateAccount(createDelegateRequestListener(listener));
    }

    /**
     * Gets the translated string of an error received in a request.
     *
     * @param error MegaError received in the request
     * @return The translated string
     */
    public static String getTranslatedErrorString(MegaError error) {
        MegaApplication app = MegaApplication.getInstance();
        if (app == null) {
            return error.getErrorString();
        }

        if (error.getErrorCode() > 0) {
            return app.getString(R.string.api_error_http);
        }

        switch (error.getErrorCode()) {
            case API_OK:
                return app.getString(R.string.api_ok);
            case API_EINTERNAL:
                return app.getString(R.string.api_einternal);
            case API_EARGS:
                return app.getString(R.string.api_eargs);
            case API_EAGAIN:
                return app.getString(R.string.api_eagain);
            case API_ERATELIMIT:
                return app.getString(R.string.api_eratelimit);
            case API_EFAILED:
                return app.getString(R.string.api_efailed);
            case API_ETOOMANY:
                if (error.getErrorString().equals("Terms of Service breached")) {
                    return app.getString(R.string.api_etoomany_ec_download);
                } else if (error.getErrorString().equals("Too many concurrent connections or transfers")){
                    return app.getString(R.string.api_etoomay);
                } else {
                    return error.getErrorString();
                }
            case API_ERANGE:
                return app.getString(R.string.api_erange);
            case API_EEXPIRED:
                return app.getString(R.string.api_eexpired);
            case API_ENOENT:
                return app.getString(R.string.api_enoent);
            case API_ECIRCULAR:
                if (error.getErrorString().equals("Upload produces recursivity")) {
                    return app.getString(R.string.api_ecircular_ec_upload);
                } else if (error.getErrorString().equals("Circular linkage detected")){
                    return app.getString(R.string.api_ecircular);
                } else {
                    return error.getErrorString();
                }
            case API_EACCESS:
                return app.getString(R.string.api_eaccess);
            case API_EEXIST:
                return app.getString(R.string.api_eexist);
            case API_EINCOMPLETE:
                return app.getString(R.string.api_eincomplete);
            case API_EKEY:
                return app.getString(R.string.api_ekey);
            case API_ESID:
                return app.getString(R.string.api_esid);
            case API_EBLOCKED:
                if (error.getErrorString().equals("Not accessible due to ToS/AUP violation")) {
                    return app.getString(R.string.api_eblocked_ec_import_ec_download);
                } else if (error.getErrorString().equals("Blocked")) {
                    return app.getString(R.string.api_eblocked);
                } else {
                    return error.getErrorString();
                }
            case API_EOVERQUOTA:
                return app.getString(R.string.api_eoverquota);
            case API_ETEMPUNAVAIL:
                return app.getString(R.string.api_etempunavail);
            case API_ETOOMANYCONNECTIONS:
                return app.getString(R.string.api_etoomanyconnections);
            case API_EWRITE:
                return app.getString(R.string.api_ewrite);
            case API_EREAD:
                return app.getString(R.string.api_eread);
            case API_EAPPKEY:
                return app.getString(R.string.api_eappkey);
            case API_ESSL:
                return app.getString(R.string.api_essl);
            case API_EGOINGOVERQUOTA:
                return app.getString(R.string.api_egoingoverquota);
            case API_EMFAREQUIRED:
                return app.getString(R.string.api_emfarequired);
            case API_EMASTERONLY:
                return app.getString(R.string.api_emasteronly);
            case API_EBUSINESSPASTDUE:
                return app.getString(R.string.api_ebusinesspastdue);
            case PAYMENT_ECARD:
                return app.getString(R.string.payment_ecard);
            case PAYMENT_EBILLING:
                return app.getString(R.string.payment_ebilling);
            case PAYMENT_EFRAUD:
                return app.getString(R.string.payment_efraud);
            case PAYMENT_ETOOMANY:
                return app.getString(R.string.payment_etoomay);
            case PAYMENT_EBALANCE:
                return app.getString(R.string.payment_ebalance);
            case PAYMENT_EGENERIC:
            default:
                return app.getString(R.string.payment_egeneric_api_error_unknown);
        }
    }
}
