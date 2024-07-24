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

import static nz.mega.sdk.MegaSync.SyncRunningState.RUNSTATE_PAUSED;
import static nz.mega.sdk.MegaSync.SyncRunningState.RUNSTATE_RUNNING;
import static nz.mega.sdk.MegaSync.SyncRunningState.RUNSTATE_SUSPENDED;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.OutputStream;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.Set;

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

    private static final Set<DelegateMegaRequestListener> activeRequestListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaRequestListener>());
    private static final Set<DelegateMegaTransferListener> activeTransferListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTransferListener>());
    private static final Set<DelegateMegaGlobalListener> activeGlobalListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaGlobalListener>());
    private static final Set<DelegateMegaListener> activeMegaListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaListener>());
    private static final Set<DelegateMegaLogger> activeMegaLoggers = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaLogger>());
    private static final Set<DelegateMegaTreeProcessor> activeMegaTreeProcessors = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTreeProcessor>());
    private static final Set<DelegateMegaTransferListener> activeHttpServerListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTransferListener>());

    /**
     * INVALID_HANDLE Invalid value for a handle
     * <p>
     * This value is used to represent an invalid handle. Several MEGA objects can have
     * a handle but it will never be INVALID_HANDLE.
     */
    public final static long INVALID_HANDLE = ~(long) 0;

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
    public final static int USER_ATTR_DEVICE_NAMES = MegaApi.USER_ATTR_DEVICE_NAMES;
    public final static int USER_ATTR_MY_BACKUPS_FOLDER = MegaApi.USER_ATTR_MY_BACKUPS_FOLDER;
    public final static int USER_ATTR_APPS_PREFS = MegaApi.USER_ATTR_APPS_PREFS;
    public final static int USER_ATTR_CC_PREFS = MegaApi.USER_ATTR_CC_PREFS;

    // deprecated: public final static int USER_ATTR_BACKUP_NAMES = MegaApi.USER_ATTR_BACKUP_NAMES;
    public final static int USER_ATTR_COOKIE_SETTINGS = MegaApi.USER_ATTR_COOKIE_SETTINGS;

    public final static int NODE_ATTR_DURATION = MegaApi.NODE_ATTR_DURATION;
    public final static int NODE_ATTR_COORDINATES = MegaApi.NODE_ATTR_COORDINATES;
    public final static int NODE_ATTR_LABEL = MegaApi.NODE_ATTR_LABEL;
    public final static int NODE_ATTR_FAV = MegaApi.NODE_ATTR_FAV;

    public final static int PAYMENT_METHOD_BALANCE = MegaApi.PAYMENT_METHOD_BALANCE;
    public final static int PAYMENT_METHOD_PAYPAL = MegaApi.PAYMENT_METHOD_PAYPAL;
    public final static int PAYMENT_METHOD_ITUNES = MegaApi.PAYMENT_METHOD_ITUNES;
    public final static int PAYMENT_METHOD_GOOGLE_WALLET = MegaApi.PAYMENT_METHOD_GOOGLE_WALLET;
    public final static int PAYMENT_METHOD_BITCOIN = MegaApi.PAYMENT_METHOD_BITCOIN;
    public final static int PAYMENT_METHOD_UNIONPAY = MegaApi.PAYMENT_METHOD_UNIONPAY;
    public final static int PAYMENT_METHOD_FORTUMO = MegaApi.PAYMENT_METHOD_FORTUMO;
    public final static int PAYMENT_METHOD_STRIPE = MegaApi.PAYMENT_METHOD_STRIPE;
    public final static int PAYMENT_METHOD_CREDIT_CARD = MegaApi.PAYMENT_METHOD_CREDIT_CARD;
    public final static int PAYMENT_METHOD_CENTILI = MegaApi.PAYMENT_METHOD_CENTILI;
    public final static int PAYMENT_METHOD_PAYSAFE_CARD = MegaApi.PAYMENT_METHOD_PAYSAFE_CARD;
    public final static int PAYMENT_METHOD_ASTROPAY = MegaApi.PAYMENT_METHOD_ASTROPAY;
    public final static int PAYMENT_METHOD_RESERVED = MegaApi.PAYMENT_METHOD_RESERVED;
    public final static int PAYMENT_METHOD_WINDOWS_STORE = MegaApi.PAYMENT_METHOD_WINDOWS_STORE;
    public final static int PAYMENT_METHOD_TPAY = MegaApi.PAYMENT_METHOD_TPAY;
    public final static int PAYMENT_METHOD_DIRECT_RESELLER = MegaApi.PAYMENT_METHOD_DIRECT_RESELLER;
    public final static int PAYMENT_METHOD_ECP = MegaApi.PAYMENT_METHOD_ECP;
    public final static int PAYMENT_METHOD_SABADELL = MegaApi.PAYMENT_METHOD_SABADELL;
    public final static int PAYMENT_METHOD_HUAWEI_WALLET = MegaApi.PAYMENT_METHOD_HUAWEI_WALLET;
    public final static int PAYMENT_METHOD_STRIPE2 = MegaApi.PAYMENT_METHOD_STRIPE2;
    public final static int PAYMENT_METHOD_WIRE_TRANSFER = MegaApi.PAYMENT_METHOD_WIRE_TRANSFER;

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
    public final static int RETRY_UNKNOWN = MegaApi.RETRY_UNKNOWN;

    public final static int KEEP_ALIVE_CAMERA_UPLOADS = MegaApi.KEEP_ALIVE_CAMERA_UPLOADS;

    public final static int STORAGE_STATE_UNKNOWN = MegaApi.STORAGE_STATE_UNKNOWN;
    public final static int STORAGE_STATE_GREEN = MegaApi.STORAGE_STATE_GREEN;
    public final static int STORAGE_STATE_ORANGE = MegaApi.STORAGE_STATE_ORANGE;
    public final static int STORAGE_STATE_RED = MegaApi.STORAGE_STATE_RED;
    public final static int STORAGE_STATE_CHANGE = MegaApi.STORAGE_STATE_CHANGE;
    public final static int STORAGE_STATE_PAYWALL = MegaApi.STORAGE_STATE_PAYWALL;

    public final static int BUSINESS_STATUS_EXPIRED = MegaApi.BUSINESS_STATUS_EXPIRED;
    public final static int BUSINESS_STATUS_INACTIVE = MegaApi.BUSINESS_STATUS_INACTIVE;
    public final static int BUSINESS_STATUS_ACTIVE = MegaApi.BUSINESS_STATUS_ACTIVE;
    public final static int BUSINESS_STATUS_GRACE_PERIOD = MegaApi.BUSINESS_STATUS_GRACE_PERIOD;

    public final static int AFFILIATE_TYPE_INVALID = MegaApi.AFFILIATE_TYPE_INVALID;
    public final static int AFFILIATE_TYPE_ID = MegaApi.AFFILIATE_TYPE_ID;
    public final static int AFFILIATE_TYPE_FILE_FOLDER = MegaApi.AFFILIATE_TYPE_FILE_FOLDER;
    public final static int AFFILIATE_TYPE_CHAT = MegaApi.AFFILIATE_TYPE_CHAT;
    public final static int AFFILIATE_TYPE_CONTACT = MegaApi.AFFILIATE_TYPE_CONTACT;

    public final static int CREATE_ACCOUNT = MegaApi.CREATE_ACCOUNT;
    public final static int RESUME_ACCOUNT = MegaApi.RESUME_ACCOUNT;
    public final static int CANCEL_ACCOUNT = MegaApi.CANCEL_ACCOUNT;
    public final static int CREATE_EPLUSPLUS_ACCOUNT = MegaApi.CREATE_EPLUSPLUS_ACCOUNT;
    public final static int RESUME_EPLUSPLUS_ACCOUNT = MegaApi.RESUME_EPLUSPLUS_ACCOUNT;

    public final static int ORDER_NONE = MegaApi.ORDER_NONE;
    public final static int ORDER_DEFAULT_ASC = MegaApi.ORDER_DEFAULT_ASC;
    public final static int ORDER_DEFAULT_DESC = MegaApi.ORDER_DEFAULT_DESC;
    public final static int ORDER_SIZE_ASC = MegaApi.ORDER_SIZE_ASC;
    public final static int ORDER_SIZE_DESC = MegaApi.ORDER_SIZE_DESC;
    public final static int ORDER_CREATION_ASC = MegaApi.ORDER_CREATION_ASC;
    public final static int ORDER_CREATION_DESC = MegaApi.ORDER_CREATION_DESC;
    public final static int ORDER_MODIFICATION_ASC = MegaApi.ORDER_MODIFICATION_ASC;
    public final static int ORDER_MODIFICATION_DESC = MegaApi.ORDER_MODIFICATION_DESC;
    public final static int ORDER_LINK_CREATION_ASC = MegaApi.ORDER_LINK_CREATION_ASC;
    public final static int ORDER_LINK_CREATION_DESC = MegaApi.ORDER_LINK_CREATION_DESC;
    public final static int ORDER_LABEL_ASC = MegaApi.ORDER_LABEL_ASC;
    public final static int ORDER_LABEL_DESC = MegaApi.ORDER_LABEL_DESC;
    public final static int ORDER_FAV_ASC = MegaApi.ORDER_FAV_ASC;
    public final static int ORDER_FAV_DESC = MegaApi.ORDER_FAV_DESC;

    public final static int TCP_SERVER_DENY_ALL = MegaApi.TCP_SERVER_DENY_ALL;
    public final static int TCP_SERVER_ALLOW_ALL = MegaApi.TCP_SERVER_ALLOW_ALL;
    public final static int TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS = MegaApi.TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    public final static int TCP_SERVER_ALLOW_LAST_LOCAL_LINK = MegaApi.TCP_SERVER_ALLOW_LAST_LOCAL_LINK;

    public final static int HTTP_SERVER_DENY_ALL = MegaApi.HTTP_SERVER_DENY_ALL;
    public final static int HTTP_SERVER_ALLOW_ALL = MegaApi.HTTP_SERVER_ALLOW_ALL;
    public final static int HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS = MegaApi.HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    public final static int HTTP_SERVER_ALLOW_LAST_LOCAL_LINK = MegaApi.HTTP_SERVER_ALLOW_LAST_LOCAL_LINK;

    public final static int FILE_TYPE_DEFAULT = MegaApi.FILE_TYPE_DEFAULT;
    public final static int FILE_TYPE_PHOTO = MegaApi.FILE_TYPE_PHOTO;
    public final static int FILE_TYPE_AUDIO = MegaApi.FILE_TYPE_AUDIO;
    public final static int FILE_TYPE_VIDEO = MegaApi.FILE_TYPE_VIDEO;
    public final static int FILE_TYPE_DOCUMENT = MegaApi.FILE_TYPE_DOCUMENT;
    public final static int FILE_TYPE_PDF = MegaApi.FILE_TYPE_PDF;
    public final static int FILE_TYPE_PRESENTATION = MegaApi.FILE_TYPE_PRESENTATION;
    public final static int FILE_TYPE_ARCHIVE = MegaApi.FILE_TYPE_ARCHIVE;
    public final static int FILE_TYPE_PROGRAM = MegaApi.FILE_TYPE_PROGRAM;
    public final static int FILE_TYPE_MISC = MegaApi.FILE_TYPE_MISC;
    public final static int FILE_TYPE_SPREADSHEET = MegaApi.FILE_TYPE_SPREADSHEET;
    public final static int FILE_TYPE_ALL_DOCS = MegaApi.FILE_TYPE_ALL_DOCS;
    public final static int FILE_TYPE_OTHERS = MegaApi.FILE_TYPE_OTHERS;

    public final static int SEARCH_TARGET_INSHARE = MegaApi.SEARCH_TARGET_INSHARE;
    public final static int SEARCH_TARGET_OUTSHARE = MegaApi.SEARCH_TARGET_OUTSHARE;
    public final static int SEARCH_TARGET_PUBLICLINK = MegaApi.SEARCH_TARGET_PUBLICLINK;
    public final static int SEARCH_TARGET_ROOTNODE = MegaApi.SEARCH_TARGET_ROOTNODE;
    public final static int SEARCH_TARGET_ALL = MegaApi.SEARCH_TARGET_ALL;

    public final static int ACCOUNT_NOT_BLOCKED = MegaApi.ACCOUNT_NOT_BLOCKED;
    public final static int ACCOUNT_BLOCKED_TOS_COPYRIGHT = MegaApi.ACCOUNT_BLOCKED_TOS_COPYRIGHT;
    public final static int ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT = MegaApi.ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT;
    public final static int ACCOUNT_BLOCKED_SUBUSER_DISABLED = MegaApi.ACCOUNT_BLOCKED_SUBUSER_DISABLED;
    public final static int ACCOUNT_BLOCKED_SUBUSER_REMOVED = MegaApi.ACCOUNT_BLOCKED_SUBUSER_REMOVED;
    public final static int ACCOUNT_BLOCKED_VERIFICATION_SMS = MegaApi.ACCOUNT_BLOCKED_VERIFICATION_SMS;
    public final static int ACCOUNT_BLOCKED_VERIFICATION_EMAIL = MegaApi.ACCOUNT_BLOCKED_VERIFICATION_EMAIL;

    public final static int BACKUP_TYPE_INVALID = MegaApi.BACKUP_TYPE_INVALID;
    public final static int BACKUP_TYPE_TWO_WAY_SYNC = MegaApi.BACKUP_TYPE_TWO_WAY_SYNC;
    public final static int BACKUP_TYPE_UP_SYNC = MegaApi.BACKUP_TYPE_UP_SYNC;
    public final static int BACKUP_TYPE_DOWN_SYNC = MegaApi.BACKUP_TYPE_DOWN_SYNC;
    public final static int BACKUP_TYPE_CAMERA_UPLOADS = MegaApi.BACKUP_TYPE_CAMERA_UPLOADS;
    public final static int BACKUP_TYPE_MEDIA_UPLOADS = MegaApi.BACKUP_TYPE_MEDIA_UPLOADS;
    public final static int BACKUP_TYPE_BACKUP_UPLOAD = MegaApi.BACKUP_TYPE_BACKUP_UPLOAD;

    public final static int ADS_DEFAULT = MegaApi.ADS_DEFAULT;
    public final static int ADS_FORCE_ADS = MegaApi.ADS_FORCE_ADS;
    public final static int ADS_IGNORE_MEGA = MegaApi.ADS_IGNORE_MEGA;
    public final static int ADS_IGNORE_COUNTRY = MegaApi.ADS_IGNORE_COUNTRY;
    public final static int ADS_IGNORE_IP = MegaApi.ADS_IGNORE_IP;
    public final static int ADS_IGNORE_PRO = MegaApi.ADS_IGNORE_PRO;
    public final static int ADS_FLAG_IGNORE_ROLLOUT = MegaApi.ADS_FLAG_IGNORE_ROLLOUT;

    public final static int CLIENT_TYPE_DEFAULT = MegaApi.CLIENT_TYPE_DEFAULT;
    public final static int CLIENT_TYPE_VPN = MegaApi.CLIENT_TYPE_VPN;
    public final static int CLIENT_TYPE_PASSWORD_MANAGER = MegaApi.CLIENT_TYPE_PASSWORD_MANAGER;

    MegaApi getMegaApi() {
        return megaApi;
    }

    /**
     * Constructor suitable for most applications.
     *
     * @param appKey   AppKey of your application.
     *                 Generate an AppKey for free here: https://mega.co.nz/#sdk
     * @param basePath Base path to store the local cache.
     *                 If you pass null to this parameter, the SDK won't use any local cache.
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
     * @param appKey       AppKey of your application.
     *                     Generate an AppKey for free here: https://mega.co.nz/#sdk
     * @param userAgent    User agent to use in network requests.
     *                     If you pass null to this parameter, a default user agent will be used.
     * @param basePath     Base path to store the local cache.
     *                     If you pass null to this parameter, the SDK won't use any local cache.
     * @param gfxProcessor Image processor. The SDK will use it to generate previews and thumbnails.
     *                     If you pass null to this parameter, the SDK will try to use the built-in image processors.
     */
    public MegaApiJava(String appKey, String userAgent, String basePath, MegaGfxProcessor gfxProcessor) {
        this.gfxProcessor = gfxProcessor;
        megaApi = new MegaApi(appKey, gfxProcessor, basePath, userAgent);
    }

    /**
     * MegaApi Constructor that allows use of a custom GFX processor & specify client type.
     * <p>
     * The SDK attaches thumbnails and previews to all uploaded images. To generate them, it needs a graphics processor.
     * You can build the SDK with one of the provided built-in graphics processors. If none are available
     * in your app, you can implement the MegaGfxProcessor interface to provide a custom processor. Please
     * read the documentation of MegaGfxProcessor carefully to ensure that your implementation is valid.
     *
     * @param appKey       AppKey of your application.
     *                     Generate an AppKey for free here: https://mega.co.nz/#sdk
     * @param userAgent    User agent to use in network requests.
     *                     If you pass null to this parameter, a default user agent will be used.
     * @param basePath     Base path to store the local cache.
     *                     If you pass null to this parameter, the SDK won't use any local cache.
     * @param gfxProcessor Image processor. The SDK will use it to generate previews and thumbnails.
     *                     If you pass null to this parameter, the SDK will try to use the built-in image processors.
     * @param clientType   Client type (default, VPN or Password Manager) enables SDK to function differently
     *                     Possible values:
     *                     MegaApi::CLIENT_TYPE_DEFAULT = 0
     *                     MegaApi::CLIENT_TYPE_VPN = 1
     *                     MegaApi::CLIENT_TYPE_PASSWORD_MANAGER = 2
     */
    public MegaApiJava(String appKey, String userAgent, String basePath, MegaGfxProcessor gfxProcessor, int clientType) {
        this.gfxProcessor = gfxProcessor;
        megaApi = new MegaApi(appKey, gfxProcessor, basePath, userAgent, 1, clientType);
    }

    /**
     * Constructor suitable for most applications.
     *
     * @param appKey AppKey of your application.
     *               Generate an AppKey for free here: https://mega.co.nz/#sdk
     */
    public MegaApiJava(String appKey) {
        megaApi = new MegaApi(appKey);
    }

    //****************************************************************************************************/
    // LISTENER MANAGEMENT
    //****************************************************************************************************/

    /**
     * Register a listener to receive all events (requests, transfers, global, synchronization).
     * <p>
     * You can use MegaApiJava.removeListener() to stop receiving events.
     *
     * @param listener Listener that will receive all events (requests, transfers, global, synchronization).
     */
    public void addListener(MegaListenerInterface listener) {
        megaApi.addListener(createDelegateMegaListener(listener));
    }

    /**
     * Register a listener to receive all events about requests.
     * <p>
     * You can use MegaApiJava.removeRequestListener() to stop receiving events.
     *
     * @param listener Listener that will receive all events about requests.
     */
    public void addRequestListener(MegaRequestListenerInterface listener) {
        megaApi.addRequestListener(createDelegateRequestListener(listener, false));
    }

    /**
     * Register a listener to receive all events about transfers.
     * <p>
     * You can use MegaApiJava.removeTransferListener() to stop receiving events.
     *
     * @param listener Listener that will receive all events about transfers.
     */
    public void addTransferListener(MegaTransferListenerInterface listener) {
        megaApi.addTransferListener(createDelegateTransferListener(listener, false));
    }

    /**
     * Register a listener to receive global events.
     * <p>
     * You can use MegaApiJava.removeGlobalListener() to stop receiving events.
     *
     * @param listener Listener that will receive global events.
     */
    public void addGlobalListener(MegaGlobalListenerInterface listener) {
        megaApi.addGlobalListener(createDelegateGlobalListener(listener));
    }

    /**
     * Unregister a listener.
     * <p>
     * This listener won't receive more events.
     *
     * @param listener Object that is unregistered.
     */
    public void removeListener(MegaListenerInterface listener) {
        ArrayList<DelegateMegaListener> listenersToRemove = new ArrayList<>();

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

        for (int i = 0; i < listenersToRemove.size(); i++) {
            megaApi.removeListener(listenersToRemove.get(i));
        }
    }

    /**
     * Unregister a MegaRequestListener.
     * <p>
     * This listener won't receive more events.
     *
     * @param listener Object that is unregistered.
     */
    public void removeRequestListener(MegaRequestListenerInterface listener) {
        ArrayList<DelegateMegaRequestListener> listenersToRemove = new ArrayList<>();
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

        for (int i = 0; i < listenersToRemove.size(); i++) {
            megaApi.removeRequestListener(listenersToRemove.get(i));
        }
    }

    /**
     * Unregister a MegaTransferListener.
     * <p>
     * This listener won't receive more events.
     *
     * @param listener Object that is unregistered.
     */
    public void removeTransferListener(MegaTransferListenerInterface listener) {
        ArrayList<DelegateMegaTransferListener> listenersToRemove = new ArrayList<>();

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

        for (int i = 0; i < listenersToRemove.size(); i++) {
            megaApi.removeTransferListener(listenersToRemove.get(i));
        }
    }

    /**
     * Unregister a MegaGlobalListener.
     * <p>
     * This listener won't receive more events.
     *
     * @param listener Object that is unregistered.
     */
    public void removeGlobalListener(MegaGlobalListenerInterface listener) {
        ArrayList<DelegateMegaGlobalListener> listenersToRemove = new ArrayList<>();

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

        for (int i = 0; i < listenersToRemove.size(); i++) {
            megaApi.removeGlobalListener(listenersToRemove.get(i));
        }
    }

    //****************************************************************************************************/
    // UTILS
    //****************************************************************************************************/

    /**
     * Get an URL to transfer the current session to the webclient
     * <p>
     * This function creates a new session for the link so logging out in the web client won't log out
     * the current session.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_SESSION_TRANSFER_URL
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - URL to open the desired page with the same account
     * <p>
     * If the client is logged in, but the account is not fully confirmed (ie. signup not completed yet),
     * this method will return API_EACCESS.
     * <p>
     * If the client is not logged in, there won't be any session to transfer, but this method will still
     * return the https://mega.nz/#<path>.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param path     Path inside https://mega.nz/# that we want to open with the current session
     *                 For example, if you want to open https://mega.nz/#pro, the parameter of this function should be "pro".
     * @param listener MegaRequestListener to track this request
     */
    public void getSessionTransferURL(String path, MegaRequestListenerInterface listener) {
        megaApi.getSessionTransferURL(path, createDelegateRequestListener(listener));
    }

    /**
     * Get an URL to transfer the current session to the webclient
     * <p>
     * This function creates a new session for the link so logging out in the web client won't log out
     * the current session.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_SESSION_TRANSFER_URL
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - URL to open the desired page with the same account
     * <p>
     * If the client is logged in, but the account is not fully confirmed (ie. signup not completed yet),
     * this method will return API_EACCESS.
     * <p>
     * If the client is not logged in, there won't be any session to transfer, but this method will still
     * return the https://mega.nz/#<path>.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param path Path inside https://mega.nz/# that we want to open with the current session
     *             For example, if you want to open https://mega.nz/#pro, the parameter of this function should be "pro".
     */
    public void getSessionTransferURL(String path) {
        megaApi.getSessionTransferURL(path);
    }

    /**
     * Converts a Base32-encoded user handle (JID) to a MegaHandle.
     * <p>
     *
     * @param base32Handle Base32-encoded handle (JID).
     * @return User handle.
     */
    public static long base32ToHandle(String base32Handle) {
        return MegaApi.base32ToHandle(base32Handle);
    }

    /**
     * Converts a Base64-encoded node handle to a MegaHandle.
     * <p>
     * The returned value can be used to recover a MegaNode using MegaApi::getNodeByHandle
     * You can revert this operation using MegaApi::handleToBase64
     *
     * @param base64Handle Base64-encoded node handle
     * @return Node handle
     */
    public static long base64ToHandle(String base64Handle) {
        return MegaApi.base64ToHandle(base64Handle);
    }

    /**
     * Converts a Base64-encoded user handle to a MegaHandle
     * <p>
     * You can revert this operation using MegaApi::userHandleToBase64
     *
     * @param base64Handle Base64-encoded node handle
     * @return User handle
     */
    public static long base64ToUserHandle(String base64Handle) {
        return MegaApi.base64ToUserHandle(base64Handle);
    }

    /**
     * Converts the handle of a node to a Base64-encoded string
     * <p>
     * You take the ownership of the returned value
     * You can revert this operation using MegaApi::base64ToHandle
     *
     * @param handle Node handle to be converted
     * @return Base64-encoded node handle
     */
    public static String handleToBase64(long handle) {
        return MegaApi.handleToBase64(handle);
    }

    /**
     * Converts a MegaHandle to a Base64-encoded string
     * <p>
     * You take the ownership of the returned value
     * You can revert this operation using MegaApi::base64ToUserHandle
     *
     * @param handle User handle to be converted
     * @return Base64-encoded user handle
     */
    public static String userHandleToBase64(long handle) {
        return MegaApi.userHandleToBase64(handle);
    }

    /**
     * Add entropy to internal random number generators
     * <p>
     * It's recommended to call this function with random data specially to
     * enhance security,
     *
     * @param data Byte array with random data
     * @param size Size of the byte array (in bytes)
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
     * <p>
     * This function will NOT return a valid value until the callback onEvent with
     * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
     * a fetchNodes to check this value, but only when it follows a login with user and password,
     * not when an existing session is resumed.
     *
     * @return True if this feature is enabled. Otherwise false.
     */
    public boolean serverSideRubbishBinAutopurgeEnabled() {
        return megaApi.serverSideRubbishBinAutopurgeEnabled();
    }

    /**
     * Check if the new format for public links is enabled
     * <p>
     * This function will NOT return a valid value until the callback onEvent with
     * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
     * a fetchNodes to check this value, but only when it follows a login with user and password,
     * not when an existing session is resumed.
     * <p>
     * For not logged-in mode, you need to call MegaApi::getMiscFlags first.
     *
     * @return True if this feature is enabled. Otherwise, false.
     */
    public boolean newLinkFormatEnabled() {
        return megaApi.newLinkFormatEnabled();
    }

    /**
     * Check if the logged in account is considered new
     *
     * This function will NOT return a valid value until the callback onEvent with
     * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
     * a fetchnodes to check this value.
     *
     * @return True if account is considered new. Otherwise, false.
     */
    public Boolean accountIsNew() {
        return megaApi.accountIsNew();
    }

    /**
     * Get the value of an A/B Test flag
     * <p>
     * Any value greater than 0 means he flag is active.
     *
     * @param flag Name or key of the value to be retrieved, flag should not have ab_ prefix.
     *
     * @return A long with the value of the flag.
     */
    public long getABTestValue(String flag) {
        return megaApi.getABTestValue(flag);
    }

    /**
     * Check if multi-factor authentication can be enabled for the current account.
     * <p>
     * This function will NOT return a valid value until the callback onEvent with
     * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
     * a fetchNodes to check this value, but only when it follows a login with user and password,
     * not when an existing session is resumed.
     * <p>
     * For not logged-in mode, you need to call MegaApi::getMiscFlags first.
     *
     * @return True if multi-factor authentication can be enabled for the current account, otherwise false.
     */
    public boolean multiFactorAuthAvailable() {
        return megaApi.multiFactorAuthAvailable();
    }

    /**
     * Reset the verified phone number for the account logged in.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_RESET_SMS_VERIFIED_NUMBER
     * If there's no verified phone number associated for the account logged in, the error code
     * provided in onRequestFinish is MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void resetSmsVerifiedPhoneNumber(MegaRequestListenerInterface listener) {
        megaApi.resetSmsVerifiedPhoneNumber(createDelegateRequestListener(listener));
    }

    /**
     * Reset the verified phone number for the account logged in.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_RESET_SMS_VERIFIED_NUMBER
     * If there's no verified phone number associated for the account logged in, the error code
     * provided in onRequestFinish is MegaError::API_ENOENT.
     */
    public void resetSmsVerifiedPhoneNumber() {
        megaApi.resetSmsVerifiedPhoneNumber();
    }

    /**
     * Check if multi-factor authentication is enabled for an account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_CHECK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email sent in the first parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if multi-factor authentication is enabled or false if it's disabled.
     *
     * @param email    Email to check
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthCheck(String email, MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthCheck(email, createDelegateRequestListener(listener));
    }

    /**
     * Check if multi-factor authentication is enabled for an account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_CHECK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email sent in the first parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if multi-factor authentication is enabled or false if it's disabled.
     *
     * @param email Email to check
     */
    public void multiFactorAuthCheck(String email) {
        megaApi.multiFactorAuthCheck(email);
    }

    /**
     * Get the secret code of the account to enable multi-factor authentication
     * The MegaApi object must be logged into an account to successfully use this function.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the Base32 secret code needed to configure multi-factor authentication.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthGetCode(MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthGetCode(createDelegateRequestListener(listener));
    }

    /**
     * Get the secret code of the account to enable multi-factor authentication
     * The MegaApi object must be logged into an account to successfully use this function.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the Base32 secret code needed to configure multi-factor authentication.
     */
    public void multiFactorAuthGetCode() {
        megaApi.multiFactorAuthGetCode();
    }

    /**
     * Enable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns true
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin      Valid pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthEnable(String pin, MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthEnable(pin, createDelegateRequestListener(listener));
    }

    /**
     * Enable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns true
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin Valid pin code for multi-factor authentication
     */
    public void multiFactorAuthEnable(String pin) {
        megaApi.multiFactorAuthEnable(pin);
    }

    /**
     * Disable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns false
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin      Valid pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthDisable(String pin, MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthDisable(pin, createDelegateRequestListener(listener));
    }

    /**
     * Disable multi-factor authentication for the account
     * The MegaApi object must be logged into an account to successfully use this function.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns false
     * - MegaRequest::getPassword - Returns the pin sent in the first parameter
     *
     * @param pin Valid pin code for multi-factor authentication
     */
    public void multiFactorAuthDisable(String pin) {
        megaApi.multiFactorAuthDisable(pin);
    }

    /**
     * Log in to a MEGA account with multi-factor authentication enabled
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the first parameter
     * - MegaRequest::getPassword - Returns the second parameter
     * - MegaRequest::getText - Returns the third parameter
     * <p>
     * If the email/password aren't valid the error code provided in onRequestFinish is
     * MegaError::API_ENOENT.
     *
     * @param email    Email of the user
     * @param password Password
     * @param pin      Pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthLogin(String email, String password, String pin, MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthLogin(email, password, pin, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account with multi-factor authentication enabled
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the first parameter
     * - MegaRequest::getPassword - Returns the second parameter
     * - MegaRequest::getText - Returns the third parameter
     * <p>
     * If the email/password aren't valid the error code provided in onRequestFinish is
     * MegaError::API_ENOENT.
     *
     * @param email    Email of the user
     * @param password Password
     * @param pin      Pin code for multi-factor authentication
     */
    public void multiFactorAuthLogin(String email, String password, String pin) {
        megaApi.multiFactorAuthLogin(email, password, pin);
    }

    /**
     * Change the password of a MEGA account with multi-factor authentication enabled
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getPassword - Returns the old password (if it was passed as parameter)
     * - MegaRequest::getNewPassword - Returns the new password
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * @param oldPassword Old password (optional, it can be NULL to not check the old password)
     * @param newPassword New password
     * @param pin         Pin code for multi-factor authentication
     * @param listener    MegaRequestListener to track this request
     */
    public void multiFactorAuthChangePassword(String oldPassword, String newPassword, String pin, MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthChangePassword(oldPassword, newPassword, pin, createDelegateRequestListener(listener));
    }

    /**
     * Change the password of a MEGA account with multi-factor authentication enabled
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getPassword - Returns the old password (if it was passed as parameter)
     * - MegaRequest::getNewPassword - Returns the new password
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     *
     * @param oldPassword Old password (optional, it can be NULL to not check the old password)
     * @param newPassword New password
     * @param pin         Pin code for multi-factor authentication
     */
    public void multiFactorAuthChangePassword(String oldPassword, String newPassword, String pin) {
        megaApi.multiFactorAuthChangePassword(oldPassword, newPassword, pin);
    }

    /**
     * Initialize the change of the email address associated to an account with multi-factor authentication enabled.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     * <p>
     * If this request succeeds, a change-email link will be sent to the specified email address.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param email    The new email to be associated to the account.
     * @param pin      Pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     */
    public void multiFactorAuthChangeEmail(String email, String pin, MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthChangeEmail(email, pin, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the change of the email address associated to an account with multi-factor authentication enabled.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     * <p>
     * If this request succeeds, a change-email link will be sent to the specified email address.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param email The new email to be associated to the account.
     * @param pin   Pin code for multi-factor authentication
     */
    public void multiFactorAuthChangeEmail(String email, String pin) {
        megaApi.multiFactorAuthChangeEmail(email, pin);
    }


    /**
     * Initialize the cancellation of an account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
     * <p>
     * If this request succeeds, a cancellation link will be sent to the email address of the user.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param pin      Pin code for multi-factor authentication
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::confirmCancelAccount
     */
    public void multiFactorAuthCancelAccount(String pin, MegaRequestListenerInterface listener) {
        megaApi.multiFactorAuthCancelAccount(pin, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the cancellation of an account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
     * <p>
     * If this request succeeds, a cancellation link will be sent to the email address of the user.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getText - Returns the pin code for multi-factor authentication
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param pin Pin code for multi-factor authentication
     * @see MegaApi::confirmCancelAccount
     */
    public void multiFactorAuthCancelAccount(String pin) {
        megaApi.multiFactorAuthCancelAccount(pin);
    }

    /**
     * Fetch details related to time zones and the current default
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_FETCH_TIMEZONE.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaTimeZoneDetails - Returns details about timezones and the current default
     *
     * @param listener MegaRequestListener to track this request
     */
    void fetchTimeZone(MegaRequestListenerInterface listener) {
        megaApi.fetchTimeZone(createDelegateRequestListener(listener));
    }

    /**
     * Fetch details related to time zones and the current default
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_FETCH_TIMEZONE.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaTimeZoneDetails - Returns details about timezones and the current default
     */
    void fetchTimeZone() {
        megaApi.fetchTimeZone();
    }

    /**
     * Log in to a MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the first parameter
     * - MegaRequest::getPassword - Returns the second parameter
     * <p>
     * If the email/password aren't valid the error code provided in onRequestFinish is
     * MegaError::API_ENOENT.
     *
     * @param email    Email of the user
     * @param password Password
     * @param listener MegaRequestListener to track this request
     */
    public void login(String email, String password, MegaRequestListenerInterface listener) {
        megaApi.login(email, password, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the first parameter
     * - MegaRequest::getPassword - Returns the second parameter
     * <p>
     * If the email/password aren't valid the error code provided in onRequestFinish is
     * MegaError::API_ENOENT.
     *
     * @param email    Email of the user
     * @param password Password
     */
    public void login(String email, String password) {
        megaApi.login(email, password);
    }

    /**
     * Log in to a public folder using a folder link
     * <p>
     * After a successful login, you should call MegaApi::fetchNodes to get filesystem and
     * start working with the folder.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the string "FOLDER"
     * - MegaRequest::getLink - Returns the public link to the folder
     *
     * @param megaFolderLink Public link to a folder in MEGA
     * @param listener       MegaRequestListener to track this request
     */
    public void loginToFolder(String megaFolderLink, MegaRequestListenerInterface listener) {
        megaApi.loginToFolder(megaFolderLink, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a public folder using a folder link
     * <p>
     * After a successful login, you should call MegaApi::fetchNodes to get filesystem and
     * start working with the folder.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the string "FOLDER"
     * - MegaRequest::getLink - Returns the public link to the folder
     *
     * @param megaFolderLink Public link to a folder in MEGA
     */
    public void loginToFolder(String megaFolderLink) {
        megaApi.loginToFolder(megaFolderLink);
    }

    /**
     * Log in to a MEGA account using a session key
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getSessionKey - Returns the session key
     *
     * @param session  Session key previously dumped with MegaApi::dumpSession
     * @param listener MegaRequestListener to track this request
     */
    public void fastLogin(String session, MegaRequestListenerInterface listener) {
        megaApi.fastLogin(session, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account using a session key
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getSessionKey - Returns the session key
     *
     * @param session Session key previously dumped with MegaApi::dumpSession
     */
    public void fastLogin(String session) {
        megaApi.fastLogin(session);
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
     * @param sessionHandle of the session. Use mega.INVALID_HANDLE to cancel all sessions except the current one.
     * @param listener      MegaRequestListenerInterface to track this request.
     */
    public void killSession(long sessionHandle, MegaRequestListenerInterface listener) {
        megaApi.killSession(sessionHandle, createDelegateRequestListener(listener));
    }

    /**
     * Close a MEGA session
     * <p>
     * All clients using this session will be automatically logged out.
     * <p>
     * You can get session information using MegaApi::getExtendedAccountDetails.
     * Then use MegaAccountDetails::getNumSessions and MegaAccountDetails::getSession
     * to get session info.
     * MegaAccountSession::getHandle provides the handle that this function needs.
     * <p>
     * If you use mega::INVALID_HANDLE, all sessions except the current one will be closed
     *
     * @param sessionHandle Handle of the session. Use mega::INVALID_HANDLE to cancel all sessions except the current one
     */
    public void killSession(long sessionHandle) {
        megaApi.killSession(sessionHandle);
    }

    /**
     * Get data about the logged account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns the name of the logged user
     * - MegaRequest::getPassword - Returns the the public RSA key of the account, Base64-encoded
     * - MegaRequest::getPrivateKey - Returns the private RSA key of the account, Base64-encoded
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getUserData(MegaRequestListenerInterface listener) {
        megaApi.getUserData(createDelegateRequestListener(listener));
    }

    /**
     * Get data about the logged account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns the name of the logged user
     * - MegaRequest::getPassword - Returns the the public RSA key of the account, Base64-encoded
     * - MegaRequest::getPrivateKey - Returns the private RSA key of the account, Base64-encoded
     */
    public void getUserData() {
        megaApi.getUserData();
    }

    /**
     * Get data about a contact
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPassword - Returns the public RSA key of the contact, Base64-encoded
     *
     * @param user     Contact to get the data
     * @param listener MegaRequestListener to track this request
     */
    public void getUserData(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.getUserData(user, createDelegateRequestListener(listener));
    }

    /**
     * Get data about a contact
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPassword - Returns the public RSA key of the contact, Base64-encoded
     *
     * @param user Contact to get the data
     */
    public void getUserData(MegaUser user) {
        megaApi.getUserData(user);
    }

    /**
     * Get information about a MEGA user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email or the Base64 handle of the user,
     * depending on the value provided as user parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPassword - Returns the public RSA key of the user, Base64-encoded
     *
     * @param user     Email or Base64 handle of the user
     * @param listener MegaRequestListener to track this request
     */
    public void getUserData(String user, MegaRequestListenerInterface listener) {
        megaApi.getUserData(user, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a MEGA user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email or the Base64 handle of the user,
     * depending on the value provided as user parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPassword - Returns the public RSA key of the user, Base64-encoded
     *
     * @param user Email or Base64 handle of the user
     */
    public void getUserData(String user) {
        megaApi.getUserData(user);
    }

    /**
     * Fetch miscellaneous flags when not logged in
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_MISC_FLAGS.
     * <p>
     * When onRequestFinish is called with MegaError::API_OK, the miscellaneous flags are available.
     * If you are logged in into an account, the error code provided in onRequestFinish is
     * MegaError::API_EACCESS.
     *
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::multiFactorAuthAvailable
     * @see MegaApi::newLinkFormatEnabled
     * @see MegaApi::smsAllowedState
     */
    public void getMiscFlags(MegaRequestListenerInterface listener) {
        megaApi.getMiscFlags(createDelegateRequestListener(listener));
    }

    /**
     * Fetch miscellaneous flags when not logged in
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_MISC_FLAGS.
     * <p>
     * When onRequestFinish is called with MegaError::API_OK, the miscellaneous flags are available.
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
     * Returns the current session key
     * <p>
     * You have to be logged in to get a valid session key. Otherwise,
     * this function returns NULL.
     * <p>
     * You take the ownership of the returned value.
     *
     * @return Current session key
     */
    @Nullable
    public String dumpSession() {
        return megaApi.dumpSession();
    }

    /**
     * Get an authentication token that can be used to identify the user account
     * <p>
     * If this MegaApi object is not logged into an account, this function will return NULL
     * <p>
     * The value returned by this function can be used in other instances of MegaApi
     * thanks to the function MegaApi::setAccountAuth.
     * <p>
     * You take the ownership of the returned value
     *
     * @return Authentication token
     */
    @Nullable
    public String getAccountAuth() {
        return megaApi.getAccountAuth();
    }

    /**
     * Use an authentication token to identify an account while accessing public folders
     * <p>
     * This function is useful to preserve the PRO status when a public folder is being
     * used. The identifier will be sent in all API requests made after the call to this function.
     * <p>
     * To stop using the current authentication token, it's needed to explicitly call
     * this function with NULL as parameter. Otherwise, the value set would continue
     * being used despite this MegaApi object is logged in or logged out.
     * <p>
     * It's recommended to call this function before the usage of MegaApi::loginToFolder
     *
     * @param auth Authentication token used to identify the account of the user.
     *             You can get it using MegaApi::getAccountAuth with an instance of MegaApi logged into
     *             an account.
     */
    public void setAccountAuth(String auth) {
        megaApi.setAccountAuth(auth);
    }

    /**
     * Initialize the creation of a new MEGA account, with firstname and lastname
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     * - MegaRequest::getParamType - Returns the value MegaApi::CREATE_ACCOUNT
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * <p>
     * If this request succeeds, a new ephemeral account will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     * <p>
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email     Email for the account
     * @param password  Password for the account
     * @param firstname Firstname of the user
     * @param lastname  Lastname of the user
     * @param listener  MegaRequestListener to track this request
     */
    public void createAccount(String email, String password, String firstname, String lastname, MegaRequestListenerInterface listener) {
        megaApi.createAccount(email, password, firstname, lastname, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the creation of a new MEGA account, with firstname and lastname
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     * - MegaRequest::getParamType - Returns the value MegaApi::CREATE_ACCOUNT
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * <p>
     * If this request succeeds, a new ephemeral account will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     * <p>
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email     Email for the account
     * @param password  Password for the account
     * @param firstname Firstname of the user
     * @param lastname  Lastname of the user
     */
    public void createAccount(String email, String password, String firstname, String lastname) {
        megaApi.createAccount(email, password, firstname, lastname);
    }

    /**
     * Create Ephemeral++ account
     * <p>
     * This kind of account allows to join chat links and to keep the session in the device
     * where it was created.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     * - MegaRequest::getParamType - Returns the value MegaApi:CREATE_EPLUSPLUS_ACCOUNT
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * <p>
     * If this request succeeds, a new ephemeral++ account will be created for the new user.
     * The app may resume the create-account process by using MegaApi::resumeCreateAccountEphemeralPlusPlus.
     *
     * @param firstname Firstname of the user
     * @param lastname  Lastname of the user
     * @param listener  MegaRequestListener to track this request
     * @implSpec This account should be confirmed in same device it was created
     */
    public void createEphemeralAccountPlusPlus(String firstname, String lastname, MegaRequestListenerInterface listener) {
        megaApi.createEphemeralAccountPlusPlus(firstname, lastname, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the creation of a new MEGA account, with firstname and lastname
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     * - MegaRequest::getAccess - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     * - MegaRequest::getParamType - Returns the value MegaApi::CREATE_ACCOUNT
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * <p>
     * If this request succeeds, a new ephemeral session will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     * <p>
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email                Email for the account
     * @param password             Password for the account
     * @param firstname            Firstname of the user
     * @param lastname             Lastname of the user
     * @param lastPublicHandle     Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *                             - MegaApi::AFFILIATE_TYPE_ID = 1
     *                             - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *                             - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *                             - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     * @param lastAccessTimestamp  Timestamp of the last access
     * @param listener             MegaRequestListener to track this request
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
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getPassword - Returns the password for the account
     * - MegaRequest::getName - Returns the firstname of the user
     * - MegaRequest::getText - Returns the lastname of the user
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     * - MegaRequest::getAccess - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     * - MegaRequest::getParamType - Returns the value MegaApi::CREATE_ACCOUNT
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * <p>
     * If this request succeeds, a new ephemeral session will be created for the new user
     * and a confirmation email will be sent to the specified email address. The app may
     * resume the create-account process by using MegaApi::resumeCreateAccount.
     * <p>
     * If an account with the same email already exists, you will get the error code
     * MegaError::API_EEXIST in onRequestFinish
     *
     * @param email                Email for the account
     * @param password             Password for the account
     * @param firstname            Firstname of the user
     * @param lastname             Lastname of the user
     * @param lastPublicHandle     Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *                             - MegaApi::AFFILIATE_TYPE_ID = 1
     *                             - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *                             - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *                             - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     * @param lastAccessTimestamp  Timestamp of the last access
     */
    public void createAccount(String email, String password, String firstname, String lastname,
                              long lastPublicHandle, int lastPublicHandleType, long lastAccessTimestamp) {
        megaApi.createAccount(email, password, firstname, lastname, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp);
    }

    /**
     * Resume a registration process
     * <p>
     * When a user begins the account registration process by calling MegaApi::createAccount,
     * an ephemeral account is created.
     * <p>
     * Until the user successfully confirms the signup link sent to the provided email address,
     * you can resume the ephemeral session in order to change the email address, resend the
     * signup link (@see MegaApi::sendSignupLink) and also to receive notifications in case the
     * user confirms the account using another client (MegaGlobalListener::onAccountUpdate or
     * MegaListener::onAccountUpdate). It is also possible to cancel the registration process by
     * MegaApi::cancelCreateAccount, which invalidates the signup link associated to the ephemeral
     * session (the session will be still valid).
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * - MegaRequest::getParamType - Returns the value MegaApi::RESUME_ACCOUNT
     * <p>
     * In case the account is already confirmed, the associated request will fail with
     * error MegaError::API_EARGS.
     *
     * @param sid      Session id valid for the ephemeral account (@see MegaApi::createAccount)
     * @param listener MegaRequestListener to track this request
     */
    public void resumeCreateAccount(String sid, MegaRequestListenerInterface listener) {
        megaApi.resumeCreateAccount(sid, createDelegateRequestListener(listener));
    }

    /**
     * Resume a registration process
     * <p>
     * When a user begins the account registration process by calling MegaApi::createAccount,
     * an ephemeral account is created.
     * <p>
     * Until the user successfully confirms the signup link sent to the provided email address,
     * you can resume the ephemeral session in order to change the email address, resend the
     * signup link (@see MegaApi::sendSignupLink) and also to receive notifications in case the
     * user confirms the account using another client (MegaGlobalListener::onAccountUpdate or
     * MegaListener::onAccountUpdate). It is also possible to cancel the registration process by
     * MegaApi::cancelCreateAccount, which invalidates the signup link associated to the ephemeral
     * session (the session will be still valid).
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * - MegaRequest::getParamType - Returns the value MegaApi::RESUME_ACCOUNT
     * <p>
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
     * <p>
     * This function is useful to send the confirmation link again or to send it to a different
     * email address, in case the user mistyped the email at the registration form. It can only
     * be used after a successful call to MegaApi::createAccount or MegaApi::resumeCreateAccount.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SEND_SIGNUP_LINK.
     *
     * @param email    Email for the account
     * @param name     Full name of the user (firstname + lastname)
     * @param listener MegaRequestListener to track this request
     */
    public void resendSignupLink(String email, String name, MegaRequestListenerInterface listener) {
        megaApi.resendSignupLink(email, name, createDelegateRequestListener(listener));
    }

    /**
     * Sends the confirmation email for a new account
     * <p>
     * This function is useful to send the confirmation link again or to send it to a different
     * email address, in case the user mistyped the email at the registration form. It can only
     * be used after a successful call to MegaApi::createAccount or MegaApi::resumeCreateAccount.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SEND_SIGNUP_LINK.
     *
     * @param email    Email for the account
     * @param name     Full name of the user (firstname + lastname)
     * @param password Password for the account
     */
    public void sendSignupLink(String email, String name, String password) {
        megaApi.sendSignupLink(email, name, password);
    }

    /**
     * Get information about a confirmation link or a new signup link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_QUERY_SIGNUP_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the confirmation link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * - MegaRequest::getName - Returns the name associated with the link (available only for confirmation links)
     * - MegaRequest::getFlag - Returns true if the account was automatically confirmed, otherwise false
     * <p>
     * If MegaRequest::getFlag returns true, the account was automatically confirmed and it's not needed
     * to call MegaApi::confirmAccount. If it returns false, it's needed to call MegaApi::confirmAccount
     * as usual. New accounts (V2, starting from April 2018) do not require a confirmation with the password,
     * but old confirmation links (V1) require it, so it's needed to check that parameter in onRequestFinish
     * to know how to proceed.
     * <p>
     * If already logged-in into a different account, you will get the error code MegaError::API_EACCESS
     * in onRequestFinish.
     * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
     * will get the error code MegaError::API_EEXPIRED in onRequestFinish.
     * In both cases, the MegaRequest::getEmail will return the email of the account that was attempted
     * to confirm, and the MegaRequest::getName will return the name.
     *
     * @param link     Confirmation link (confirm) or new signup link (newsignup)
     * @param listener MegaRequestListener to track this request
     */
    public void querySignupLink(String link, MegaRequestListenerInterface listener) {
        megaApi.querySignupLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a confirmation link or a new signup link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_QUERY_SIGNUP_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the confirmation link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * - MegaRequest::getName - Returns the name associated with the link (available only for confirmation links)
     * - MegaRequest::getFlag - Returns true if the account was automatically confirmed, otherwise false
     * <p>
     * If MegaRequest::getFlag returns true, the account was automatically confirmed and it's not needed
     * to call MegaApi::confirmAccount. If it returns false, it's needed to call MegaApi::confirmAccount
     * as usual. New accounts (V2, starting from April 2018) do not require a confirmation with the password,
     * but old confirmation links (V1) require it, so it's needed to check that parameter in onRequestFinish
     * to know how to proceed.
     * <p>
     * If already logged-in into a different account, you will get the error code MegaError::API_EACCESS
     * in onRequestFinish.
     * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
     * will get the error code MegaError::API_EEXPIRED in onRequestFinish.
     * In both cases, the MegaRequest::getEmail will return the email of the account that was attempted
     * to confirm, and the MegaRequest::getName will return the name.
     *
     * @param link Confirmation link (confirm) or new signup link (newsignup)
     */
    public void querySignupLink(String link) {
        megaApi.querySignupLink(link);
    }

    /**
     * Confirm a MEGA account using a confirmation link and the user password
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the confirmation link
     * - MegaRequest::getPassword - Returns the password
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Email of the account
     * - MegaRequest::getName - Name of the user
     * <p>
     * As a result of a successful confirmation, the app will receive the callback
     * MegaListener::onEvent and MegaGlobalListener::onEvent with an event of type
     * MegaEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
     * the account by checking MegaEvent::getText. @see MegaListener::onEvent.
     * <p>
     * If already logged-in into a different account, you will get the error code MegaError::API_EACCESS
     * in onRequestFinish.
     * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
     * will get the error code MegaError::API_EEXPIRED in onRequestFinish.
     * In both cases, the MegaRequest::getEmail will return the email of the account that was attempted
     * to confirm, and the MegaRequest::getName will return the name.
     *
     * @param link     Confirmation link
     * @param password Password of the account
     * @param listener MegaRequestListener to track this request
     */
    public void confirmAccount(String link, String password, MegaRequestListenerInterface listener) {
        megaApi.confirmAccount(link, password, createDelegateRequestListener(listener));
    }

    /**
     * Confirm a MEGA account using a confirmation link and the user password
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the confirmation link
     * - MegaRequest::getPassword - Returns the password
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Email of the account
     * - MegaRequest::getName - Name of the user
     * <p>
     * As a result of a successful confirmation, the app will receive the callback
     * MegaListener::onEvent and MegaGlobalListener::onEvent with an event of type
     * MegaEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
     * the account by checking MegaEvent::getText. @see MegaListener::onEvent.
     * <p>
     * If already logged-in into a different account, you will get the error code MegaError::API_EACCESS
     * in onRequestFinish.
     * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
     * will get the error code MegaError::API_EEXPIRED in onRequestFinish.
     * In both cases, the MegaRequest::getEmail will return the email of the account that was attempted
     * to confirm, and the MegaRequest::getName will return the name.
     *
     * @param link     Confirmation link
     * @param password Password of the account
     */
    public void confirmAccount(String link, String password) {
        megaApi.confirmAccount(link, password);
    }

    /**
     * Initialize the reset of the existing password, with and without the Master Key.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_RECOVERY_LINK.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * - MegaRequest::getFlag - Returns whether the user has a backup of the master key or not.
     * <p>
     * If this request succeeds, a recovery link will be sent to the user.
     * If no account is registered under the provided email, you will get the error code
     * MegaError::API_ENOENT in onRequestFinish
     *
     * @param email        Email used to register the account whose password wants to be reset.
     * @param hasMasterKey True if the user has a backup of the master key. Otherwise, false.
     * @param listener     MegaRequestListener to track this request
     */
    public void resetPassword(String email, boolean hasMasterKey, MegaRequestListenerInterface listener) {
        megaApi.resetPassword(email, hasMasterKey, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a recovery link created by MegaApi::resetPassword.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_QUERY_RECOVERY_LINK
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the recovery link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * - MegaRequest::getFlag - Return whether the link requires masterkey to reset password.
     *
     * @param link     Recovery link (recover)
     * @param listener MegaRequestListener to track this request
     */
    public void queryResetPasswordLink(String link, MegaRequestListenerInterface listener) {
        megaApi.queryResetPasswordLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Set a new password for the account pointed by the recovery link.
     * <p>
     * Recovery links are created by calling MegaApi::resetPassword and may or may not
     * require to provide the Master Key.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_RECOVERY_LINK
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the recovery link
     * - MegaRequest::getPassword - Returns the new password
     * - MegaRequest::getPrivateKey - Returns the Master Key, when provided
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * - MegaRequest::getFlag - Return whether the link requires masterkey to reset password.
     * <p>
     * If the account is logged-in into a different account than the account for which the link
     * was generated, onRequestFinish will be called with the error code MegaError::API_EACCESS.
     *
     * @param link      The recovery link sent to the user's email address.
     * @param newPwd    The new password to be set.
     * @param masterKey Base64-encoded string containing the master key (optional).
     * @param listener  MegaRequestListener to track this request
     * @see MegaApi::queryResetPasswordLink and the flag of the MegaRequest::TYPE_QUERY_RECOVERY_LINK in there.
     */
    public void confirmResetPassword(String link, String newPwd, String masterKey, MegaRequestListenerInterface listener) {
        megaApi.confirmResetPassword(link, newPwd, masterKey, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the cancellation of an account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
     * <p>
     * If this request succeeds, a cancellation link will be sent to the email address of the user.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::confirmCancelAccount
     */
    public void cancelAccount(MegaRequestListenerInterface listener) {
        megaApi.cancelAccount(createDelegateRequestListener(listener));
    }

    /**
     * Get information about a cancel link created by MegaApi::cancelAccount.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_QUERY_RECOVERY_LINK
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the cancel link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     *
     * @param link     Cancel link (cancel)
     * @param listener MegaRequestListener to track this request
     */
    public void queryCancelLink(String link, MegaRequestListenerInterface listener) {
        megaApi.queryCancelLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Effectively parks the user's account without creating a new fresh account.
     * <p>
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * The contents of the account will then be purged after 60 days. Once the account is
     * parked, the user needs to contact MEGA support to restore the account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_CANCEL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the recovery link
     * - MegaRequest::getPassword - Returns the new password
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     *
     * @param link     Cancellation link sent to the user's email address;
     * @param pwd      Password for the account.
     * @param listener MegaRequestListener to track this request
     */
    public void confirmCancelAccount(String link, String pwd, MegaRequestListenerInterface listener) {
        megaApi.confirmCancelAccount(link, pwd, createDelegateRequestListener(listener));
    }

    /**
     * Allow to resend the verification email for Weak Account Protection
     * <p>
     * The verification email will be resent to the same address as it was previously sent to.
     * <p>
     * This function can be called if the the reason for being blocked is:
     * 700: the account is suspended for Weak Account Protection.
     * <p>
     * If the logged in account is not suspended or is suspended for some other reason,
     * onRequestFinish will be called with the error code MegaError::API_EACCESS.
     * <p>
     * If the logged in account has not been sent the unlock email before,
     * onRequestFinish will be called with the error code MegaError::API_EARGS.
     * <p>
     * If the logged in account has already sent the unlock email and until it's available again,
     * onRequestFinish will be called with the error code MegaError::API_ETEMPUNAVAIL.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void resendVerificationEmail(MegaRequestListenerInterface listener) {
        megaApi.resendVerificationEmail(createDelegateRequestListener(listener));
    }

    /**
     * Allow to resend the verification email for Weak Account Protection
     * <p>
     * The verification email will be resent to the same address as it was previously sent to.
     * <p>
     * This function can be called if the the reason for being blocked is:
     * 700: the account is suspended for Weak Account Protection.
     * <p>
     * If the logged in account is not suspended or is suspended for some other reason,
     * onRequestFinish will be called with the error code MegaError::API_EACCESS.
     * <p>
     * If the logged in account has not been sent the unlock email before,
     * onRequestFinish will be called with the error code MegaError::API_EARGS.
     * <p>
     * If the logged in account has already sent the unlock email and until it's available again,
     * onRequestFinish will be called with the error code MegaError::API_ETEMPUNAVAIL.
     */
    public void resendVerificationEmail() {
        megaApi.resendVerificationEmail();
    }

    /**
     * Initialize the change of the email address associated to the account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getEmail - Returns the email for the account
     * <p>
     * If this request succeeds, a change-email link will be sent to the specified email address.
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param email    The new email to be associated to the account.
     * @param listener MegaRequestListener to track this request
     */
    public void changeEmail(String email, MegaRequestListenerInterface listener) {
        megaApi.changeEmail(email, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a change-email link created by MegaApi::changeEmail.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_QUERY_RECOVERY_LINK
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the change-email link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * <p>
     * If the account is logged-in into a different account than the account for which the link
     * was generated, onRequestFinish will be called with the error code MegaError::API_EACCESS.
     *
     * @param link     Change-email link (verify)
     * @param listener MegaRequestListener to track this request
     */
    public void queryChangeEmailLink(String link, MegaRequestListenerInterface listener) {
        megaApi.queryChangeEmailLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Effectively changes the email address associated to the account.
     * <p>
     * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK.
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink - Returns the change-email link
     * - MegaRequest::getPassword - Returns the password
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Return the email associated with the link
     * <p>
     * If the account is logged-in into a different account than the account for which the link
     * was generated, onRequestFinish will be called with the error code MegaError::API_EACCESS.
     *
     * @param link     Change-email link sent to the user's email address.
     * @param pwd      Password for the account.
     * @param listener MegaRequestListener to track this request
     */
    public void confirmChangeEmail(String link, String pwd, MegaRequestListenerInterface listener) {
        megaApi.confirmChangeEmail(link, pwd, createDelegateRequestListener(listener));
    }

    /**
     * Set proxy settings
     * <p>
     * The SDK will start using the provided proxy settings as soon as this function returns.
     *
     * @param proxySettings Proxy settings
     * @see MegaProxy
     */
    public void setProxySettings(MegaProxy proxySettings) {
        megaApi.setProxySettings(proxySettings);
    }

    /**
     * Try to detect the system's proxy settings
     * <p>
     * Automatic proxy detection is currently supported on Windows only.
     * On other platforms, this function will return a MegaProxy object
     * of type MegaProxy::PROXY_NONE
     * <p>
     * You take the ownership of the returned value.
     *
     * @return MegaProxy object with the detected proxy settings
     */
    @Nullable
    public MegaProxy getAutoProxySettings() {
        return megaApi.getAutoProxySettings();
    }

    /**
     * Check if the MegaApi object is logged in
     *
     * @return 0 if not logged in, Otherwise, a number > 0
     */
    public int isLoggedIn() {
        return megaApi.isLoggedIn();
    }

    /**
     * Check if we are logged in into an Ephemeral account ++
     *
     * @return true if logged into an Ephemeral account ++, Otherwise return false
     */
    public boolean isEphemeralPlusPlus() {
        return megaApi.isEphemeralPlusPlus();
    }

    /**
     * Check the reason of being blocked.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_WHY_AM_I_BLOCKED.
     * <p>
     * This request can be sent internally at anytime (whenever an account gets blocked), so
     * a MegaGlobalListener should process the result, show the reason and logout.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the reason string (in English)
     * - MegaRequest::getNumber - Returns the reason code. Possible values:
     * - MegaApi::ACCOUNT_NOT_BLOCKED = 0
     * Account is not blocked in any way.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_TOS_COPYRIGHT = 200
     * Suspension only for multiple copyright violations.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT = 300
     * Suspension message for any type of suspension, but copyright suspension.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_SUBUSER_DISABLED = 400
     * Subuser of the business account has been disabled.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_SUBUSER_REMOVED = 401
     * Subuser of business account has been removed.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_SMS = 500
     * The account is temporary blocked and needs to be verified by an SMS code.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_EMAIL = 700
     * The account is temporary blocked and needs to be verified by email (Weak Account Protection).
     * <p>
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
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_WHY_AM_I_BLOCKED.
     * <p>
     * This request can be sent internally at anytime (whenever an account gets blocked), so
     * a MegaGlobalListener should process the result, show the reason and logout.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the reason string (in English)
     * - MegaRequest::getNumber - Returns the reason code. Possible values:
     * - MegaApi::ACCOUNT_NOT_BLOCKED = 0
     * Account is not blocked in any way.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_TOS_COPYRIGHT = 200
     * Suspension only for multiple copyright violations.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT = 300
     * Suspension message for any type of suspension, but copyright suspension.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_SUBUSER_DISABLED = 400
     * Subuser of the business account has been disabled.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_SUBUSER_REMOVED = 401
     * Subuser of business account has been removed.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_SMS = 500
     * The account is temporary blocked and needs to be verified by an SMS code.
     * <p>
     * - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_EMAIL = 700
     * The account is temporary blocked and needs to be verified by email (Weak Account Protection).
     * <p>
     * If the error code in the MegaRequest object received in onRequestFinish
     * is MegaError::API_OK, the user is not blocked.
     */
    public void whyAmIBlocked() {
        megaApi.whyAmIBlocked();
    }

    /**
     * Create a contact link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_CREATE.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getFlag - Returns the value of \c renew parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Return the handle of the new contact link
     *
     * @param renew    True to invalidate the previous contact link (if any).
     * @param listener MegaRequestListener to track this request
     */
    public void contactLinkCreate(boolean renew, MegaRequestListenerInterface listener) {
        megaApi.contactLinkCreate(renew, createDelegateRequestListener(listener));
    }

    /**
     * Create a contact link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_CREATE.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getFlag - Returns the value of \c renew parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Return the handle of the new contact link
     */
    public void contactLinkCreate() {
        megaApi.contactLinkCreate();
    }

    /**
     * Get information about a contact link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_QUERY.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getParentHandle - Returns the userhandle of the contact
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getName - Returns the first name of the contact
     * - MegaRequest::getText - Returns the last name of the contact
     * - MegaRequest::getFile - Returns the avatar of the contact (JPG with Base64 encoding)
     *
     * @param handle   Handle of the contact link to check
     * @param listener MegaRequestListener to track this request
     */
    public void contactLinkQuery(long handle, MegaRequestListenerInterface listener) {
        megaApi.contactLinkQuery(handle, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a contact link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_QUERY.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getParentHandle - Returns the userhandle of the contact
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getName - Returns the first name of the contact
     * - MegaRequest::getText - Returns the last name of the contact
     * - MegaRequest::getFile - Returns the avatar of the contact (JPG with Base64 encoding)
     *
     * @param handle Handle of the contact link to check
     */
    public void contactLinkQuery(long handle) {
        megaApi.contactLinkQuery(handle);
    }

    /**
     * Delete a contact link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_DELETE.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     *
     * @param handle   Handle of the contact link to delete
     *                 If the parameter is INVALID_HANDLE, the active contact link is deleted
     * @param listener MegaRequestListener to track this request
     */
    public void contactLinkDelete(long handle, MegaRequestListenerInterface listener) {
        megaApi.contactLinkDelete(handle, createDelegateRequestListener(listener));
    }

    /**
     * Delete a contact link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_DELETE.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the contact link
     *
     * @param handle Handle of the contact link to delete
     *               If the parameter is INVALID_HANDLE, the active contact link is deleted
     */
    public void contactLinkDelete(long handle) {
        megaApi.contactLinkDelete(handle);
    }

    /**
     * Get the next PSA (Public Service Announcement) that should be shown to the user
     * <p>
     * After the PSA has been accepted or dismissed by the user, app should
     * use MegaApi::setPSA to notify API servers about this event and
     * do not get the same PSA again in the next call to this function.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PSA.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Returns the id of the PSA (useful to call MegaApi::setPSA later)
     * Depending on the format of the PSA, the request may additionally return, for the new format:
     * - MegaRequest::getEmail - Returns the URL (or an empty string))
     * - MegaRequest::getName - Returns the title of the PSA
     * - MegaRequest::getText - Returns the text of the PSA
     * - MegaRequest::getFile - Returns the URL of the image of the PSA
     * - MegaRequest::getPassword - Returns the text for the positive button (or an empty string)
     * - MegaRequest::getLink - Returns the link for the positive button (or an empty string)
     * <p>
     * If there isn't any new PSA to show, onRequestFinish will be called with the error
     * code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::setPSA
     */
    public void getPSAWithUrl(MegaRequestListenerInterface listener) {
        megaApi.getPSAWithUrl(createDelegateRequestListener(listener));
    }

    /**
     * Notify API servers that a PSA (Public Service Announcement) has been already seen
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER.
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_LAST_PSA
     * - MegaRequest::getText - Returns the id passed in the first parameter (as a string)
     *
     * @param id       Identifier of the PSA
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::getPSA
     */
    public void setPSA(int id, MegaRequestListenerInterface listener) {
        megaApi.setPSA(id, createDelegateRequestListener(listener));
    }

    /**
     * Notify API servers that a PSA (Public Service Announcement) has been already seen
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER.
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_LAST_PSA
     * - MegaRequest::getText - Returns the id passed in the first parameter (as a string)
     *
     * @param id Identifier of the PSA¡
     * @see MegaApi::getPSA
     */
    public void setPSA(int id) {
        megaApi.setPSA(id);
    }

    /**
     * Command to acknowledge user alerts.
     * <p>
     * Other clients will be notified that alerts to this point have been seen.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_USERALERT_ACKNOWLEDGE.
     *
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::getUserAlerts
     */
    public void acknowledgeUserAlerts(MegaRequestListenerInterface listener) {
        megaApi.acknowledgeUserAlerts(createDelegateRequestListener(listener));
    }

    /**
     * Command to acknowledge user alerts.
     * <p>
     * Other clients will be notified that alerts to this point have been seen.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_USERALERT_ACKNOWLEDGE.
     *
     * @see MegaApi::getUserAlerts
     */
    public void acknowledgeUserAlerts() {
        megaApi.acknowledgeUserAlerts();
    }

    /**
     * Returns the email of the currently open account
     * <p>
     * If the MegaApi object isn't logged in or the email isn't available,
     * this function returns NULL
     * <p>
     * You take the ownership of the returned value
     *
     * @return Email of the account
     */
    @Nullable
    public String getMyEmail() {
        return megaApi.getMyEmail();
    }

    /**
     * Returns the user handle of the currently open account
     * <p>
     * If the MegaApi object isn't logged in,
     * this function returns NULL
     * <p>
     * You take the ownership of the returned value
     *
     * @return User handle of the account
     */
    @Nullable
    public String getMyUserHandle() {
        return megaApi.getMyUserHandle();
    }

    /**
     * Returns the user handle of the currently open account
     * <p>
     * If the MegaApi object isn't logged in,
     * this function returns INVALID_HANDLE
     *
     * @return User handle of the account
     */
    public long getMyUserHandleBinary() {
        return megaApi.getMyUserHandleBinary();
    }

    /**
     * Get the MegaUser of the currently open account
     * <p>
     * If the MegaApi object isn't logged in, this function returns NULL.
     * <p>
     * You take the ownership of the returned value
     *
     * @return MegaUser of the currently open account, otherwise NULL
     * @implSpec The visibility of your own user is undefined and shouldn't be used.
     */
    @Nullable
    public MegaUser getMyUser() {
        return megaApi.getMyUser();
    }

    /**
     * Returns whether MEGA Achievements are enabled for the open account
     *
     * @return True if enabled, false otherwise.
     */
    public boolean isAchievementsEnabled() {
        return megaApi.isAchievementsEnabled();
    }

    /**
     * Check if the account is a business account.
     *
     * @return returns true if it's a business account, otherwise false
     */
    public boolean isBusinessAccount() {
        return megaApi.isBusinessAccount();
    }

    /**
     * Check if the account is a master account.
     * <p>
     * When a business account is a sub-user, not the master, some user actions will be blocked.
     * In result, the API will return the error code MegaError::API_EMASTERONLY. Some examples of
     * requests that may fail with this error are:
     * - MegaApi::cancelAccount
     * - MegaApi::changeEmail
     * - MegaApi::remove
     * - MegaApi::removeVersion
     *
     * @return returns true if it's a master account, false if it's a sub-user account
     */
    public boolean isMasterBusinessAccount() {
        return megaApi.isMasterBusinessAccount();
    }

    /**
     * Check if the business account is active or not.
     * <p>
     * When a business account is not active, some user actions will be blocked. In result, the API
     * will return the error code MegaError::API_EBUSINESSPASTDUE. Some examples of requests
     * that may fail with this error are:
     * - MegaApi::startDownload
     * - MegaApi::startUpload
     * - MegaApi::copyNode
     * - MegaApi::share
     * - MegaApi::cleanRubbishBin
     *
     * @return returns true if the account is active, otherwise false
     */
    public boolean isBusinessAccountActive() {
        return megaApi.isBusinessAccountActive();
    }

    /**
     * Get the status of a business account.
     *
     * @return Returns the business account status, possible values:
     * MegaApi::BUSINESS_STATUS_EXPIRED = -1
     * MegaApi::BUSINESS_STATUS_INACTIVE = 0
     * MegaApi::BUSINESS_STATUS_ACTIVE = 1
     * MegaApi::BUSINESS_STATUS_GRACE_PERIOD = 2
     */
    public int getBusinessStatus() {
        return megaApi.getBusinessStatus();
    }

    /**
     * Returns the deadline to remedy the storage overquota situation
     * <p>
     * This value is valid only when MegaApi::getUserData has been called after
     * receiving a callback MegaListener/MegaGlobalListener::onEvent of type
     * MegaEvent::EVENT_STORAGE, reporting STORAGE_STATE_PAYWALL.
     * The value will become invalid once the state of storage changes.
     *
     * @return Timestamp representing the deadline to remedy the overquota
     */
    public long getOverquotaDeadlineTs() {
        return megaApi.getOverquotaDeadlineTs();
    }

    /**
     * Returns when the user was warned about overquota state
     * <p>
     * This value is valid only when MegaApi::getUserData has been called after
     * receiving a callback MegaListener/MegaGlobalListener::onEvent of type
     * MegaEvent::EVENT_STORAGE, reporting STORAGE_STATE_PAYWALL.
     * The value will become invalid once the state of storage changes.
     * <p>
     * You take the ownership of the returned value.
     *
     * @return MegaIntegerList with the timestamp corresponding to each warning
     */
    public MegaIntegerList getOverquotaWarningsTs() {
        return megaApi.getOverquotaWarningsTs();
    }

    /**
     * Check if the password is correct for the current account
     *
     * @param password Password to check
     * @return True if the password is correct for the current account, otherwise false.
     */
    public boolean checkPassword(String password) {
        return megaApi.checkPassword(password);
    }

    /**
     * Returns the credentials of the currently open account
     * <p>
     * If the MegaApi object isn't logged in or there's no signing key available,
     * this function returns NULL
     * <p>
     * You take the ownership of the returned value.
     * Use delete [] to free it.
     *
     * @return Fingerprint of the signing key of the current account
     */
    public String getMyCredentials() {
        return megaApi.getMyCredentials();
    }

    /**
     * Returns the credentials of a given user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns MegaApi::USER_ATTR_ED25519_PUBLIC_KEY
     * - MegaRequest::getFlag - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPassword - Returns the credentials in hexadecimal format
     *
     * @param user     MegaUser of the contact (see MegaApi::getContact) to get the fingerprint
     * @param listener MegaRequestListener to track this request
     */
    public void getUserCredentials(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.getUserCredentials(user, createDelegateRequestListener(listener));
    }

    /**
     * Checks if credentials are verified for the given user
     *
     * @param user MegaUser of the contact whose credentials want to be checked
     * @return true if verified, false otherwise
     */
    public boolean areCredentialsVerified(MegaUser user) {
        return megaApi.areCredentialsVerified(user);
    }

    /**
     * Verify credentials of a given user
     * <p>
     * This function allow to tag credentials of a user as verified. It should be called when the
     * logged in user compares the fingerprint of the user (provided by an independent and secure
     * method) with the fingerprint shown by the app (@see MegaApi::getUserCredentials).
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_VERIFY_CREDENTIALS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns userhandle
     *
     * @param user     MegaUser of the contact whose credentials want to be verified
     * @param listener MegaRequestListener to track this request
     */
    public void verifyCredentials(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.verifyCredentials(user, createDelegateRequestListener(listener));
    }

    /**
     * Reset credentials of a given user
     * <p>
     * Call this function to forget the existing authentication of keys and signatures for a given
     * user. A full reload of the account will start the authentication process again.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_VERIFY_CREDENTIALS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns userhandle
     * - MegaRequest::getFlag - Returns true
     *
     * @param user     MegaUser of the contact whose credentials want to be reset
     * @param listener MegaRequestListener to track this request
     */
    public void resetCredentials(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.resetCredentials(user, createDelegateRequestListener(listener));
    }

    /**
     * Set the active log level
     * <p>
     * This function sets the log level of the logging system. Any log listener registered by
     * MegaApi::addLoggerObject will receive logs with the same or a lower level than
     * the one passed to this function.
     *
     * @param logLevel Active log level
     *                 These are the valid values for this parameter:
     *                 - MegaApi::LOG_LEVEL_FATAL = 0
     *                 - MegaApi::LOG_LEVEL_ERROR = 1
     *                 - MegaApi::LOG_LEVEL_WARNING = 2
     *                 - MegaApi::LOG_LEVEL_INFO = 3
     *                 - MegaApi::LOG_LEVEL_DEBUG = 4
     *                 - MegaApi::LOG_LEVEL_MAX = 5
     */
    public static void setLogLevel(int logLevel) {
        MegaApi.setLogLevel(logLevel);
    }

    /**
     * Add a MegaLogger implementation to receive SDK logs
     * <p>
     * Logs received by this objects depends on the active log level.
     * By default, it is MegaApi::LOG_LEVEL_INFO. You can change it
     * using MegaApi::setLogLevel.
     * <p>
     * You can remove the existing logger by using MegaApi::removeLoggerObject.
     * <p>
     * In performance mode, it is assumed that this is only called on startup and
     * not while actively logging.
     *
     * @param megaLogger MegaLogger implementation
     */
    public static void addLoggerObject(MegaLoggerInterface megaLogger) {
        MegaApi.addLoggerObject(createDelegateMegaLogger(megaLogger));
    }

    /**
     * Remove a MegaLogger implementation to stop receiving SDK logs
     * <p>
     * If the logger was registered in the past, it will stop receiving log
     * messages after the call to this function.
     * <p>
     * In performance mode, it is assumed that this is only called on shutdown and
     * not while actively logging.
     *
     * @param megaLogger Previously registered MegaLogger implementation
     */
    public static void removeLoggerObject(MegaLoggerInterface megaLogger) {
        ArrayList<DelegateMegaLogger> listenersToRemove = new ArrayList<>();

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

        for (int i = 0; i < listenersToRemove.size(); i++) {
            MegaApi.removeLoggerObject(listenersToRemove.get(i));
        }
    }

    /**
     * Send a log to the logging system
     * <p>
     * This log will be received by the active logger object (MegaApi::setLoggerObject) if
     * the log level is the same or lower than the active log level (MegaApi::setLogLevel)
     * <p>
     * The third and the fourth parameter are optional. You may want to use __FILE__ and __LINE__
     * to complete them.
     * <p>
     * In performance mode, only logging to console is serialized through a mutex.
     * Logging to `MegaLogger`s is not serialized and has to be done by the subclasses if needed.
     *
     * @param logLevel Log level for this message
     * @param message  Message for the logging system
     * @param filename Origin of the log message
     * @param line     Line of code where this message was generated
     */
    public static void log(int logLevel, String message, String filename, int line) {
        MegaApi.log(logLevel, message, filename, line);
    }

    /**
     * Send a log to the logging system
     * <p>
     * This log will be received by the active logger object (MegaApi::setLoggerObject) if
     * the log level is the same or lower than the active log level (MegaApi::setLogLevel)
     * <p>
     * The third is optional. You may want to use __FILE__ to complete it.
     * <p>
     * In performance mode, only logging to console is serialized through a mutex.
     * Logging to `MegaLogger`s is not serialized and has to be done by the subclasses if needed.
     *
     * @param logLevel Log level for this message
     * @param message  Message for the logging system
     * @param filename Origin of the log message
     */
    public static void log(int logLevel, String message, String filename) {
        MegaApi.log(logLevel, message, filename);
    }

    /**
     * Send a log to the logging system
     * <p>
     * This log will be received by the active logger object (MegaApi::setLoggerObject) if
     * the log level is the same or lower than the active log level (MegaApi::setLogLevel)
     * <p>
     * In performance mode, only logging to console is serialized through a mutex.
     * Logging to `MegaLogger`s is not serialized and has to be done by the subclasses if needed.
     *
     * @param logLevel Log level for this message
     * @param message  Message for the logging system
     */
    public static void log(int logLevel, String message) {
        MegaApi.log(logLevel, message);
    }

    /**
     * Create a folder in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_FOLDER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns the handle of the parent folder
     * - MegaRequest::getName - Returns the name of the new folder
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new folder
     * - MegaRequest::getFlag - True if target folder (\c parent) was overridden
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param name     Name of the new folder
     * @param parent   Parent folder
     * @param listener MegaRequestListener to track this request
     */
    public void createFolder(String name, MegaNode parent, MegaRequestListenerInterface listener) {
        megaApi.createFolder(name, parent, createDelegateRequestListener(listener));
    }

    /**
     * Create a folder in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_FOLDER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns the handle of the parent folder
     * - MegaRequest::getName - Returns the name of the new folder
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new folder
     * - MegaRequest::getFlag - True if target folder (\c parent) was overridden
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param name   Name of the new folder
     * @param parent Parent folder
     */
    public void createFolder(String name, MegaNode parent) {
        megaApi.createFolder(name, parent);
    }

    /**
     * Get Password Manager Base folder node from the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_PASSWORD_MANAGER_BASE
     * Valid data in the MegaRequest object received on callbacks:
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the folder
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getPasswordManagerBase(MegaRequestListenerInterface listener) {
        megaApi.getPasswordManagerBase(createDelegateRequestListener(listener));
    }

    /**
     * Returns true if provided MegaHandle is of a Password Node Folder
     *
     * A folder is considered a Password Node Folder if Password Manager Base is its
     * ancestor.
     *
     * @param node MegaHandle of the node to check if it is a Password Node Folder
     * @return true if this node is a Password Node Folder
     */
    public boolean isPasswordNodeFolder(long node) {
        return megaApi.isPasswordNodeFolder(node);
    }

    /**
     * Create a new Password Node in your Password Manager tree
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_PASSWORD_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Handle of the parent provided as an argument
     * - MegaRequest::getName - name for the new Password Node provided as an argument
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new Password Node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param name Name for the new Password Node
     * @param data Password Node data for the Password Node
     * @param parent Parent folder for the new Password Node
     * @param listener MegaRequestListener to track this request
     */
    public void createPasswordNode(String name, MegaNode.PasswordNodeData data, long parent,
                                   MegaRequestListenerInterface listener) {
        megaApi.createPasswordNode(name, data, parent, createDelegateRequestListener(listener));
    }

    /**
     * Update a Password Node in the MEGA account according to the parameters
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_UPDATE_PASSWORD_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - handle provided of the Password Node to update
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node to modify
     * @param newData New data for the Password Node to update
     * @param listener MegaRequestListener to track this request
     */
    public void updatePasswordNode(long node, MegaNode.PasswordNodeData newData,
                                   MegaRequestListenerInterface listener) {
        megaApi.updatePasswordNode(node, newData, createDelegateRequestListener(listener));
    }

    /**
     * Move a node in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to move
     * @param newParent New parent for the node
     * @param listener  MegaRequestListener to track this request
     */
    public void moveNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener) {
        megaApi.moveNode(node, newParent, createDelegateRequestListener(listener));
    }

    /**
     * Move a node in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to move
     * @param newParent New parent for the node
     */
    public void moveNode(MegaNode node, MegaNode newParent) {
        megaApi.moveNode(node, newParent);
    }

    /**
     * Move a node in the MEGA account changing the file name
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     * - MegaRequest::getName - Returns the name for the new node
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - True if target folder (\c newParent) was overridden
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to move
     * @param newParent New parent for the node
     * @param newName   Name for the new node
     * @param listener  MegaRequestListener to track this request
     */
    public void moveNode(MegaNode node, MegaNode newParent, String newName, MegaRequestListenerInterface listener) {
        megaApi.moveNode(node, newParent, newName, createDelegateRequestListener(listener));
    }

    /**
     * Move a node in the MEGA account changing the file name
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to move
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
     * - MegaRequest::getName - Returns the name for the new node
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - True if target folder (\c newParent) was overridden
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to move
     * @param newParent New parent for the node
     * @param newName   Name for the new node
     */
    public void moveNode(MegaNode node, MegaNode newParent, String newName) {
        megaApi.moveNode(node, newParent, newName);
    }

    /**
     * Copy a node in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy (if it is a public node)
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     * - MegaRequest::getFlag - True if target folder (\c newParent) was overridden
     * <p>
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to copy
     * @param newParent Parent for the new node
     * @param listener  MegaRequestListener to track this request
     * @implSpec In case the target folder was overridden, the MegaRequest::getParentHandle still keeps
     * the handle of the original target folder. You can check the final parent by checking the
     * value returned by MegaNode::getParentHandle
     */
    public void copyNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener) {
        megaApi.copyNode(node, newParent, createDelegateRequestListener(listener));
    }

    /**
     * Copy a node in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy (if it is a public node)
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     * - MegaRequest::getFlag - True if target folder (\c newParent) was overridden
     * <p>
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to copy
     * @param newParent Parent for the new node
     * @implSpec In case the target folder was overridden, the MegaRequest::getParentHandle still keeps
     * the handle of the original target folder. You can check the final parent by checking the
     * value returned by MegaNode::getParentHandle
     */
    public void copyNode(MegaNode node, MegaNode newParent) {
        megaApi.copyNode(node, newParent);
    }

    /**
     * Copy a node in the MEGA account changing the file name
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy
     * - MegaRequest::getName - Returns the name for the new node
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     * <p>
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to copy
     * @param newParent Parent for the new node
     * @param newName   Name for the new node
     * @param listener  MegaRequestListener to track this request
     */
    public void copyNode(MegaNode node, MegaNode newParent, String newName, MegaRequestListenerInterface listener) {
        megaApi.copyNode(node, newParent, newName, createDelegateRequestListener(listener));
    }

    /**
     * Copy a node in the MEGA account changing the file name
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to copy
     * - MegaRequest::getParentHandle - Returns the handle of the new parent for the new node
     * - MegaRequest::getPublicMegaNode - Returns the node to copy
     * - MegaRequest::getName - Returns the name for the new node
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node
     * <p>
     * If the status of the business account is expired, onRequestFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node to copy
     * @param newParent Parent for the new node
     * @param newName   Name for the new node
     */
    public void copyNode(MegaNode node, MegaNode newParent, String newName) {
        megaApi.copyNode(node, newParent, newName);
    }

    /**
     * Rename a node in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_RENAME
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to rename
     * - MegaRequest::getName - Returns the new name for the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     Node to modify
     * @param newName  New name for the node
     * @param listener MegaRequestListener to track this request
     */
    public void renameNode(MegaNode node, String newName, MegaRequestListenerInterface listener) {
        megaApi.renameNode(node, newName, createDelegateRequestListener(listener));
    }

    /**
     * Rename a node in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_RENAME
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to rename
     * - MegaRequest::getName - Returns the new name for the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node    Node to modify
     * @param newName New name for the node
     */
    public void renameNode(MegaNode node, String newName) {
        megaApi.renameNode(node, newName);
    }

    /**
     * Remove a node from the MEGA account
     * <p>
     * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
     * the node to the Rubbish Bin use MegaApi::moveNode
     * <p>
     * If the node has previous versions, they will be deleted too
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
     * - MegaRequest::getFlag - Returns false because previous versions won't be preserved
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param node     Node to remove
     * @param listener MegaRequestListener to track this request
     */
    public void remove(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.remove(node, createDelegateRequestListener(listener));
    }

    /**
     * Remove a node from the MEGA account
     * <p>
     * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
     * the node to the Rubbish Bin use MegaApi::moveNode
     * <p>
     * If the node has previous versions, they will be deleted too
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
     * - MegaRequest::getFlag - Returns false because previous versions won't be preserved
     * <p>
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
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_VERSIONS
     * <p>
     * When the request finishes, file versions might not be deleted yet.
     * Deletions are notified using onNodesUpdate callbacks.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void removeVersions(MegaRequestListenerInterface listener) {
        megaApi.removeVersions(createDelegateRequestListener(listener));
    }

    /**
     * Remove a version of a file from the MEGA account
     * <p>
     * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
     * the node to the Rubbish Bin use MegaApi::moveNode.
     * <p>
     * If the node has previous versions, they won't be deleted.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
     * - MegaRequest::getFlag - Returns true because previous versions will be preserved
     * <p>
     * If the MEGA account is a sub-user business account, onRequestFinish will
     * be called with the error code MegaError::API_EMASTERONLY.
     *
     * @param node     Node to remove
     * @param listener MegaRequestListener to track this request
     */
    public void removeVersion(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.removeVersion(node, createDelegateRequestListener(listener));
    }

    /**
     * Restore a previous version of a file
     * <p>
     * Only versions of a file can be restored, not the current version (because it's already current).
     * The node will be copied and set as current. All the version history will be preserved without changes,
     * being the old current node the previous version of the new current node, and keeping the restored
     * node also in its previous place in the version history.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_RESTORE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to restore
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param version  Node with the version to restore
     * @param listener MegaRequestListener to track this request
     */
    public void restoreVersion(MegaNode version, MegaRequestListenerInterface listener) {
        megaApi.restoreVersion(version, createDelegateRequestListener(listener));
    }

    /**
     * Clean the Rubbish Bin in the MEGA account
     * <p>
     * This function effectively removes every node contained in the Rubbish Bin. In order to
     * avoid accidental deletions, you might want to warn the user about the action.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CLEAN_RUBBISH_BIN. This
     * request returns MegaError::API_ENOENT if the Rubbish bin is already empty.
     * <p>
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
     * <p>
     * This function effectively removes every node contained in the Rubbish Bin. In order to
     * avoid accidental deletions, you might want to warn the user about the action.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CLEAN_RUBBISH_BIN. This
     * request returns MegaError::API_ENOENT if the Rubbish bin is already empty.
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     */
    public void cleanRubbishBin() {
        megaApi.cleanRubbishBin();
    }

    /**
     * Send a node to the Inbox of another MEGA user using a MegaUser
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to send
     * - MegaRequest::getEmail - Returns the email of the user that receives the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     Node to send
     * @param user     User that receives the node
     * @param listener MegaRequestListener to track this request
     */
    public void sendFileToUser(MegaNode node, MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.sendFileToUser(node, user, createDelegateRequestListener(listener));
    }

    /**
     * Send a node to the Inbox of another MEGA user using a MegaUser
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to send
     * - MegaRequest::getEmail - Returns the email of the user that receives the node
     * <p>
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
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node to send
     * - MegaRequest::getEmail - Returns the email of the user that receives the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     Node to send
     * @param email    Email of the user that receives the node
     * @param listener MegaRequestListener to track this request
     */
    public void sendFileToUser(MegaNode node, String email, MegaRequestListenerInterface listener) {
        megaApi.sendFileToUser(node, email, createDelegateRequestListener(listener));
    }

    /**
     * Upgrade cryptographic security
     * <p>
     * This should be called only after MegaEvent::EVENT_UPGRADE_SECURITY event is received to effectively
     * proceed with the cryptographic upgrade process.
     * This should happen only once per account.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void upgradeSecurity(MegaRequestListenerInterface listener) {
        megaApi.upgradeSecurity(createDelegateRequestListener(listener));
    }

    /**
     * Get the contact verification warning flag status
     *
     * It returns if showing the warnings to verify contacts is enabled.
     *
     * @return True if showing the warnings are enabled, false otherwise.
     */
    public boolean contactVerificationWarningEnabled() {
        return megaApi.contactVerificationWarningEnabled();
    }

    /**
     * Creates a new share key for the node if there is no share key already created.
     * <p>
     * Call it before starting any new share.
     *
     * @param node     The folder to share. It must be a non-root folder
     * @param listener MegaRequestListener to track this request
     */
    public void openShareDialog(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.openShareDialog(node, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using a MegaUser
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     The folder to share. It must be a non-root folder
     * @param user     User that receives the shared folder
     * @param level    Permissions that are granted to the user
     *                 Valid values for this parameter:
     *                 - MegaShare::ACCESS_UNKNOWN = -1
     *                 Stop sharing a folder with this user
     *                 - MegaShare::ACCESS_READ = 0
     *                 - MegaShare::ACCESS_READWRITE = 1
     *                 - MegaShare::ACCESS_FULL = 2
     *                 - MegaShare::ACCESS_OWNER = 3
     * @param listener MegaRequestListener to track this request
     */
    public void share(MegaNode node, MegaUser user, int level, MegaRequestListenerInterface listener) {
        megaApi.share(node, user, level, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using a MegaUser
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node  The folder to share. It must be a non-root folder
     * @param user  User that receives the shared folder
     * @param level Permissions that are granted to the user
     *              Valid values for this parameter:
     *              - MegaShare::ACCESS_UNKNOWN = -1
     *              Stop sharing a folder with this user
     *              - MegaShare::ACCESS_READ = 0
     *              - MegaShare::ACCESS_READWRITE = 1
     *              - MegaShare::ACCESS_FULL = 2
     *              - MegaShare::ACCESS_OWNER = 3
     */
    public void share(MegaNode node, MegaUser user, int level) {
        megaApi.share(node, user, level);
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using his email
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     The folder to share. It must be a non-root folder
     * @param email    Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
     *                 and the user will be invited to register an account.
     * @param level    Permissions that are granted to the user
     *                 Valid values for this parameter:
     *                 - MegaShare::ACCESS_UNKNOWN = -1
     *                 Stop sharing a folder with this user
     *                 <p>
     *                 - MegaShare::ACCESS_READ = 0
     *                 - MegaShare::ACCESS_READWRITE = 1
     *                 - MegaShare::ACCESS_FULL = 2
     *                 - MegaShare::ACCESS_OWNER = 3
     * @param listener MegaRequestListener to track this request
     */
    public void share(MegaNode node, String email, int level, MegaRequestListenerInterface listener) {
        megaApi.share(node, email, level, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using his email
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SHARE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
     * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
     * - MegaRequest::getAccess - Returns the access that is granted to the user
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node  The folder to share. It must be a non-root folder
     * @param email Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
     *              and the user will be invited to register an account.
     * @param level Permissions that are granted to the user
     *              Valid values for this parameter:
     *              - MegaShare::ACCESS_UNKNOWN = -1
     *              Stop sharing a folder with this user
     *              <p>
     *              - MegaShare::ACCESS_READ = 0
     *              - MegaShare::ACCESS_READWRITE = 1
     *              - MegaShare::ACCESS_FULL = 2
     *              - MegaShare::ACCESS_OWNER = 3
     */
    public void share(MegaNode node, String email, int level) {
        megaApi.share(node, email, level);
    }

    /**
     * Import a public link to the account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_IMPORT_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     * - MegaRequest::getParentHandle - Returns the folder that receives the imported file
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node in the account
     * - MegaRequest::getFlag - True if target folder (\c parent) was overridden
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     * @param parent       Parent folder for the imported file
     * @param listener     MegaRequestListener to track this request
     */
    public void importFileLink(String megaFileLink, MegaNode parent, MegaRequestListenerInterface listener) {
        megaApi.importFileLink(megaFileLink, parent, createDelegateRequestListener(listener));
    }

    /**
     * Import a public link to the account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_IMPORT_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     * - MegaRequest::getParentHandle - Returns the folder that receives the imported file
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodeHandle - Handle of the new node in the account
     * - MegaRequest::getFlag - True if target folder (\c parent) was overridden
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     * @param parent       Parent folder for the imported file
     */
    public void importFileLink(String megaFileLink, MegaNode parent) {
        megaApi.importFileLink(megaFileLink, parent);
    }

    /**
     * Decrypt password-protected public link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the encrypted public link to the file/folder
     * - MegaRequest::getPassword - Returns the password to decrypt the link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Decrypted public link
     *
     * @param link     Password/protected public link to a file/folder in MEGA
     * @param password Password to decrypt the link
     * @param listener MegaRequestListener to track this request
     */
    public void decryptPasswordProtectedLink(String link, String password, MegaRequestListenerInterface listener) {
        megaApi.decryptPasswordProtectedLink(link, password, createDelegateRequestListener(listener));
    }

    /**
     * Decrypt password-protected public link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the encrypted public link to the file/folder
     * - MegaRequest::getPassword - Returns the password to decrypt the link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Decrypted public link
     *
     * @param link     Password/protected public link to a file/folder in MEGA
     * @param password Password to decrypt the link
     */
    public void decryptPasswordProtectedLink(String link, String password) {
        megaApi.decryptPasswordProtectedLink(link, password);
    }

    /**
     * Encrypt public link with password
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to be encrypted
     * - MegaRequest::getPassword - Returns the password to encrypt the link
     * - MegaRequest::getFlag - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Encrypted public link
     *
     * @param link     Public link to be encrypted, including encryption key for the link
     * @param password Password to encrypt the link
     * @param listener MegaRequestListener to track this request
     */
    public void encryptLinkWithPassword(String link, String password, MegaRequestListenerInterface listener) {
        megaApi.encryptLinkWithPassword(link, password, createDelegateRequestListener(listener));
    }

    /**
     * Encrypt public link with password
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PASSWORD_LINK
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to be encrypted
     * - MegaRequest::getPassword - Returns the password to encrypt the link
     * - MegaRequest::getFlag - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Encrypted public link
     *
     * @param link     Public link to be encrypted, including encryption key for the link
     * @param password Password to encrypt the link
     */
    public void encryptLinkWithPassword(String link, String password) {
        megaApi.encryptLinkWithPassword(link, password);
    }

    /**
     * Get a MegaNode from a public link to a file
     * <p>
     * A public node can be imported using MegaApi::copyNode or downloaded using MegaApi::startDownload
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PUBLIC_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPublicMegaNode - Public MegaNode corresponding to the public link
     * - MegaRequest::getFlag - Return true if the provided key along the link is invalid.
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     * @param listener     MegaRequestListener to track this request
     */
    public void getPublicNode(String megaFileLink, MegaRequestListenerInterface listener) {
        megaApi.getPublicNode(megaFileLink, createDelegateRequestListener(listener));
    }

    /**
     * Get a MegaNode from a public link to a file
     * <p>
     * A public node can be imported using MegaApi::copyNode or downloaded using MegaApi::startDownload
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PUBLIC_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the public link to the file
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPublicMegaNode - Public MegaNode corresponding to the public link
     * - MegaRequest::getFlag - Return true if the provided key along the link is invalid.
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param megaFileLink Public link to a file in MEGA
     */
    public void getPublicNode(String megaFileLink) {
        megaApi.getPublicNode(megaFileLink);
    }

    /**
     * Get the thumbnail of a node
     * <p>
     * If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT
     * error code
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getText - Returns the file attribute string if \c node is an attached node from chats. NULL otherwise
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
     * - MegaRequest::getBase64Key - Returns the nodekey in Base64 (only when node has file attributes)
     * - MegaRequest::getPrivateKey - Returns the file-attribute string (only when node has file attributes)
     *
     * @param node        Node to get the thumbnail
     * @param dstFilePath Destination path for the thumbnail.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     * @param listener    MegaRequestListener to track this request
     */
    public void getThumbnail(MegaNode node, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getThumbnail(node, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the thumbnail of a node
     * <p>
     * If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT
     * error code
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getText - Returns the file attribute string if \c node is an attached node from chats. NULL otherwise
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
     * - MegaRequest::getBase64Key - Returns the nodekey in Base64 (only when node has file attributes)
     * - MegaRequest::getPrivateKey - Returns the file-attribute string (only when node has file attributes)
     *
     * @param node        Node to get the thumbnail
     * @param dstFilePath Destination path for the thumbnail.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     */
    public void getThumbnail(MegaNode node, String dstFilePath) {
        megaApi.getThumbnail(node, dstFilePath);
    }

    /**
     * Get the preview of a node
     * <p>
     * If the node doesn't have a preview the request fails with the MegaError::API_ENOENT
     * error code
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getText - Returns the file attribute string if \c node is an attached node from chats. NULL otherwise
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
     * - MegaRequest::getBase64Key - Returns the nodekey in Base64 (only when node has file attributes)
     * - MegaRequest::getPrivateKey - Returns the file-attribute string (only when node has file attributes)
     *
     * @param node        Node to get the preview
     * @param dstFilePath Destination path for the preview.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     * @param listener    MegaRequestListener to track this request
     */
    public void getPreview(MegaNode node, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getPreview(node, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the preview of a node
     * <p>
     * If the node doesn't have a preview the request fails with the MegaError::API_ENOENT
     * error code
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getText - Returns the file attribute string if \c node is an attached node from chats. NULL otherwise
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
     * - MegaRequest::getBase64Key - Returns the nodekey in Base64 (only when node has file attributes)
     * - MegaRequest::getPrivateKey - Returns the file-attribute string (only when node has file attributes)
     *
     * @param node        Node to get the preview
     * @param dstFilePath Destination path for the preview.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     */
    public void getPreview(MegaNode node, String dstFilePath) {
        megaApi.getPreview(node, dstFilePath);
    }

    /**
     * Get the avatar of a MegaUser
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email of the user
     *
     * @param user        MegaUser to get the avatar. If this parameter is set to NULL, the avatar is obtained
     *                    for the active account
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     * @param listener    MegaRequestListener to track this request
     */
    public void getUserAvatar(MegaUser user, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getUserAvatar(user, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the avatar of a MegaUser
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email of the user
     *
     * @param user        MegaUser to get the avatar. If this parameter is set to NULL, the avatar is obtained
     *                    for the active account
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     */
    public void getUserAvatar(MegaUser user, String dstFilePath) {
        megaApi.getUserAvatar(user, dstFilePath);
    }

    /**
     * Get the avatar of any user in MEGA
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
     *
     * @param email_or_handle Email or user handle (Base64 encoded) to get the avatar. If this parameter is
     *                        set to NULL, the avatar is obtained for the active account
     * @param dstFilePath     Destination path for the avatar. It has to be a path to a file, not to a folder.
     *                        If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *                        will be used as the file name inside that folder. If the path doesn't finish with
     *                        one of these characters, the file will be downloaded to a file in that path.
     * @param listener        MegaRequestListener to track this request
     */
    public void getUserAvatar(String email_or_handle, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getUserAvatar(email_or_handle, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the avatar of any user in MEGA
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
     *
     * @param email_or_handle Email or user handle (Base64 encoded) to get the avatar. If this parameter is
     *                        set to NULL, the avatar is obtained for the active account
     * @param dstFilePath     Destination path for the avatar. It has to be a path to a file, not to a folder.
     *                        If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *                        will be used as the file name inside that folder. If the path doesn't finish with
     *                        one of these characters, the file will be downloaded to a file in that path.
     */
    public void getUserAvatar(String email_or_handle, String dstFilePath) {
        megaApi.getUserAvatar(email_or_handle, dstFilePath);
    }

    /**
     * Get the avatar of the active account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email of the user
     *
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     * @param listener    MegaRequestListener to track this request
     */
    public void getUserAvatar(String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getUserAvatar(dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the avatar of the active account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the destination path
     * - MegaRequest::getEmail - Returns the email of the user
     *
     * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
     *                    If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *                    will be used as the file name inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     */
    public void getUserAvatar(String dstFilePath) {
        megaApi.getUserAvatar(dstFilePath);
    }

    /**
     * Get the default color for the avatar.
     * <p>
     * This color should be used only when the user doesn't have an avatar.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param user MegaUser to get the color of the avatar.
     * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
     */
    @Nullable
    public String getUserAvatarColor(@Nullable MegaUser user) {
        return MegaApi.getUserAvatarColor(user);
    }

    /**
     * Get the default color for the avatar.
     * <p>
     * This color should be used only when the user doesn't have an avatar.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param userhandle User handle (Base64 encoded) to get the avatar.
     * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
     */
    @Nullable
    public String getUserAvatarColor(@Nullable String userhandle) {
        return MegaApi.getUserAvatarColor(userhandle);
    }

    /**
     * Get the secondary color for the avatar.
     * <p>
     * This color should be used only when the user doesn't have an avatar, making a
     * gradient in combination with the color returned from getUserAvatarColor.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param user MegaUser to get the color of the avatar.
     * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
     */
    @Nullable
    public String getUserAvatarSecondaryColor(@Nullable MegaUser user) {
        return MegaApi.getUserAvatarSecondaryColor(user);
    }

    /**
     * Get the secondary color for the avatar.
     * <p>
     * This color should be used only when the user doesn't have an avatar, making a
     * gradient in combination with the color returned from getUserAvatarColor.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param userhandle User handle (Base64 encoded) to get the avatar.
     * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
     */
    @Nullable
    public String getUserAvatarSecondaryColor(@Nullable String userhandle) {
        return MegaApi.getUserAvatarSecondaryColor(userhandle);
    }

    /**
     * Get an attribute of a MegaUser.
     * <p>
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param user     MegaUser to get the attribute. If this parameter is set to NULL, the attribute
     *                 is obtained for the active account
     * @param type     Attribute type
     *                 Valid values are:
     *                 MegaApi::USER_ATTR_FIRSTNAME = 1
     *                 Get the firstname of the user (public)
     *                 MegaApi::USER_ATTR_LASTNAME = 2
     *                 Get the lastname of the user (public)
     *                 MegaApi::USER_ATTR_AUTHRING = 3
     *                 Get the authentication ring of the user (private)
     *                 MegaApi::USER_ATTR_LAST_INTERACTION = 4
     *                 Get the last interaction of the contacts of the user (private)
     *                 MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *                 Get the public key Ed25519 of the user (public)
     *                 MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *                 Get the public key Cu25519 of the user (public)
     *                 MegaApi::USER_ATTR_KEYRING = 7
     *                 Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     *                 MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     *                 Get the signature of RSA public key of the user (public)
     *                 MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     *                 Get the signature of Cu25519 public key of the user (public)
     *                 MegaApi::USER_ATTR_LANGUAGE = 14
     *                 Get the preferred language of the user (private, non-encrypted)
     *                 MegaApi::USER_ATTR_PWD_REMINDER = 15
     *                 Get the password-reminder-dialog information (private, non-encrypted)
     *                 MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     *                 Get whether user has versions disabled or enabled (private, non-encrypted)
     *                 MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     *                 Get whether user generates rich-link messages or not (private)
     *                 MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *                 Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *                 MegaApi::USER_ATTR_STORAGE_STATE = 21
     *                 Get the state of the storage (private non-encrypted)
     *                 MegaApi::ATTR_GEOLOCATION = 22
     *                 Get the user geolocation (private)
     *                 MegaApi::ATTR_CAMERA_UPLOADS_FOLDER = 23
     *                 Get the target folder for Camera Uploads (private)
     *                 MegaApi::ATTR_MY_CHAT_FILES_FOLDER = 24
     *                 Get the target folder for My chat files (private)
     *                 MegaApi::ATTR_ALIAS = 27
     *                 Get the list of the users' aliases (private)
     *                 MegaApi::ATTR_DEVICE_NAMES = 30
     *                 Get the list of device names (private)
     *                 MegaApi::ATTR_MY_BACKUPS_FOLDER = 31
     *                 Get the target folder for My Backups (private)
     * @param listener MegaRequestListener to track this request
     */
    public void getUserAttribute(MegaUser user, int type, MegaRequestListenerInterface listener) {
        megaApi.getUserAttribute(user, type, createDelegateRequestListener(listener));
    }

    /**
     * Get an attribute of a MegaUser.
     * <p>
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param user MegaUser to get the attribute. If this parameter is set to NULL, the attribute
     *             is obtained for the active account
     * @param type Attribute type
     *             Valid values are:
     *             MegaApi::USER_ATTR_FIRSTNAME = 1
     *             Get the firstname of the user (public)
     *             MegaApi::USER_ATTR_LASTNAME = 2
     *             Get the lastname of the user (public)
     *             MegaApi::USER_ATTR_AUTHRING = 3
     *             Get the authentication ring of the user (private)
     *             MegaApi::USER_ATTR_LAST_INTERACTION = 4
     *             Get the last interaction of the contacts of the user (private)
     *             MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *             Get the public key Ed25519 of the user (public)
     *             MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *             Get the public key Cu25519 of the user (public)
     *             MegaApi::USER_ATTR_KEYRING = 7
     *             Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     *             MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     *             Get the signature of RSA public key of the user (public)
     *             MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     *             Get the signature of Cu25519 public key of the user (public)
     *             MegaApi::USER_ATTR_LANGUAGE = 14
     *             Get the preferred language of the user (private, non-encrypted)
     *             MegaApi::USER_ATTR_PWD_REMINDER = 15
     *             Get the password-reminder-dialog information (private, non-encrypted)
     *             MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     *             Get whether user has versions disabled or enabled (private, non-encrypted)
     *             MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     *             Get whether user generates rich-link messages or not (private)
     *             MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *             Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *             MegaApi::USER_ATTR_STORAGE_STATE = 21
     *             Get the state of the storage (private non-encrypted)
     *             MegaApi::ATTR_GEOLOCATION = 22
     *             Get the user geolocation (private)
     *             MegaApi::ATTR_CAMERA_UPLOADS_FOLDER = 23
     *             Get the target folder for Camera Uploads (private)
     *             MegaApi::ATTR_MY_CHAT_FILES_FOLDER = 24
     *             Get the target folder for My chat files (private)
     *             MegaApi::ATTR_ALIAS = 27
     *             Get the list of the users' aliases (private)
     *             MegaApi::ATTR_DEVICE_NAMES = 30
     *             Get the list of device names (private)
     *             MegaApi::ATTR_MY_BACKUPS_FOLDER = 31
     *             Get the target folder for My Backups (private)
     */
    public void getUserAttribute(MegaUser user, int type) {
        megaApi.getUserAttribute(user, type);
    }

    /**
     * Get an attribute of any user in MEGA.
     * <p>
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param email_or_handle Email or user handle (Base64 encoded) to get the attribute.
     *                        If this parameter is set to NULL, the attribute is obtained for the active account.
     * @param type            Attribute type
     *                        Valid values are:
     *                        MegaApi::USER_ATTR_FIRSTNAME = 1
     *                        Get the firstname of the user (public)
     *                        MegaApi::USER_ATTR_LASTNAME = 2
     *                        Get the lastname of the user (public)
     *                        MegaApi::USER_ATTR_AUTHRING = 3
     *                        Get the authentication ring of the user (private)
     *                        MegaApi::USER_ATTR_LAST_INTERACTION = 4
     *                        Get the last interaction of the contacts of the user (private)
     *                        MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *                        Get the public key Ed25519 of the user (public)
     *                        MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *                        Get the public key Cu25519 of the user (public)
     *                        MegaApi::USER_ATTR_KEYRING = 7
     *                        Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     *                        MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     *                        Get the signature of RSA public key of the user (public)
     *                        MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     *                        Get the signature of Cu25519 public key of the user (public)
     *                        MegaApi::USER_ATTR_LANGUAGE = 14
     *                        Get the preferred language of the user (private, non-encrypted)
     *                        MegaApi::USER_ATTR_PWD_REMINDER = 15
     *                        Get the password-reminder-dialog information (private, non-encrypted)
     *                        MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     *                        Get whether user has versions disabled or enabled (private, non-encrypted)
     *                        MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *                        Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *                        MegaApi::USER_ATTR_STORAGE_STATE = 21
     *                        Get the state of the storage (private non-encrypted)
     * @param listener        MegaRequestListener to track this request
     */
    public void getUserAttribute(String email_or_handle, int type, MegaRequestListenerInterface listener) {
        megaApi.getUserAttribute(email_or_handle, type, createDelegateRequestListener(listener));
    }

    /**
     * Get an attribute of any user in MEGA.
     * <p>
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param email_or_handle Email or user handle (Base64 encoded) to get the attribute.
     *                        If this parameter is set to NULL, the attribute is obtained for the active account.
     * @param type            Attribute type
     *                        Valid values are:
     *                        MegaApi::USER_ATTR_FIRSTNAME = 1
     *                        Get the firstname of the user (public)
     *                        MegaApi::USER_ATTR_LASTNAME = 2
     *                        Get the lastname of the user (public)
     *                        MegaApi::USER_ATTR_AUTHRING = 3
     *                        Get the authentication ring of the user (private)
     *                        MegaApi::USER_ATTR_LAST_INTERACTION = 4
     *                        Get the last interaction of the contacts of the user (private)
     *                        MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *                        Get the public key Ed25519 of the user (public)
     *                        MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *                        Get the public key Cu25519 of the user (public)
     *                        MegaApi::USER_ATTR_KEYRING = 7
     *                        Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     *                        MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     *                        Get the signature of RSA public key of the user (public)
     *                        MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     *                        Get the signature of Cu25519 public key of the user (public)
     *                        MegaApi::USER_ATTR_LANGUAGE = 14
     *                        Get the preferred language of the user (private, non-encrypted)
     *                        MegaApi::USER_ATTR_PWD_REMINDER = 15
     *                        Get the password-reminder-dialog information (private, non-encrypted)
     *                        MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     *                        Get whether user has versions disabled or enabled (private, non-encrypted)
     *                        MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *                        Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *                        MegaApi::USER_ATTR_STORAGE_STATE = 21
     *                        Get the state of the storage (private non-encrypted)
     */
    public void getUserAttribute(String email_or_handle, int type) {
        megaApi.getUserAttribute(email_or_handle, type);
    }

    /**
     * Get an attribute of the current account.
     * <p>
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param type     Attribute type
     *                 Valid values are:
     *                 MegaApi::USER_ATTR_FIRSTNAME = 1
     *                 Get the firstname of the user (public)
     *                 MegaApi::USER_ATTR_LASTNAME = 2
     *                 Get the lastname of the user (public)
     *                 MegaApi::USER_ATTR_AUTHRING = 3
     *                 Get the authentication ring of the user (private)
     *                 MegaApi::USER_ATTR_LAST_INTERACTION = 4
     *                 Get the last interaction of the contacts of the user (private)
     *                 MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *                 Get the public key Ed25519 of the user (public)
     *                 MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *                 Get the public key Cu25519 of the user (public)
     *                 MegaApi::USER_ATTR_KEYRING = 7
     *                 Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     *                 MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     *                 Get the signature of RSA public key of the user (public)
     *                 MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     *                 Get the signature of Cu25519 public key of the user (public)
     *                 MegaApi::USER_ATTR_LANGUAGE = 14
     *                 Get the preferred language of the user (private, non-encrypted)
     *                 MegaApi::USER_ATTR_PWD_REMINDER = 15
     *                 Get the password-reminder-dialog information (private, non-encrypted)
     *                 MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     *                 Get whether user has versions disabled or enabled (private, non-encrypted)
     *                 MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     *                 Get whether user generates rich-link messages or not (private)
     *                 MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *                 Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *                 MegaApi::USER_ATTR_STORAGE_STATE = 21
     *                 Get the state of the storage (private non-encrypted)
     *                 MegaApi::USER_ATTR_GEOLOCATION = 22
     *                 Get whether the user has enabled send geolocation messages (private)
     *                 MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     *                 Get the settings for push notifications (private non-encrypted)
     * @param listener MegaRequestListener to track this request
     */
    public void getUserAttribute(int type, MegaRequestListenerInterface listener) {
        megaApi.getUserAttribute(type, createDelegateRequestListener(listener));
    }

    /**
     * Get an attribute of the current account.
     * <p>
     * User attributes can be private or public. Private attributes are accessible only by
     * your own user, while public ones are retrievable by any of your contacts.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Returns the value for public attributes
     * - MegaRequest::getMegaStringMap - Returns the value for private attributes
     *
     * @param type Attribute type
     *             Valid values are:
     *             MegaApi::USER_ATTR_FIRSTNAME = 1
     *             Get the firstname of the user (public)
     *             MegaApi::USER_ATTR_LASTNAME = 2
     *             Get the lastname of the user (public)
     *             MegaApi::USER_ATTR_AUTHRING = 3
     *             Get the authentication ring of the user (private)
     *             MegaApi::USER_ATTR_LAST_INTERACTION = 4
     *             Get the last interaction of the contacts of the user (private)
     *             MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *             Get the public key Ed25519 of the user (public)
     *             MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *             Get the public key Cu25519 of the user (public)
     *             MegaApi::USER_ATTR_KEYRING = 7
     *             Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     *             MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY = 8
     *             Get the signature of RSA public key of the user (public)
     *             MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY = 9
     *             Get the signature of Cu25519 public key of the user (public)
     *             MegaApi::USER_ATTR_LANGUAGE = 14
     *             Get the preferred language of the user (private, non-encrypted)
     *             MegaApi::USER_ATTR_PWD_REMINDER = 15
     *             Get the password-reminder-dialog information (private, non-encrypted)
     *             MegaApi::USER_ATTR_DISABLE_VERSIONS = 16
     *             Get whether user has versions disabled or enabled (private, non-encrypted)
     *             MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     *             Get whether user generates rich-link messages or not (private)
     *             MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *             Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *             MegaApi::USER_ATTR_STORAGE_STATE = 21
     *             Get the state of the storage (private non-encrypted)
     *             MegaApi::USER_ATTR_GEOLOCATION = 22
     *             Get whether the user has enabled send geolocation messages (private)
     *             MegaApi::USER_ATTR_PUSH_SETTINGS = 23
     *             Get the settings for push notifications (private non-encrypted)
     */
    public void getUserAttribute(int type) {
        megaApi.getUserAttribute(type);
    }

    /**
     * Cancel the retrieval of a thumbnail
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
     *
     * @param node     Node to cancel the retrieval of the thumbnail
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::getThumbnail
     */
    public void cancelGetThumbnail(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.cancelGetThumbnail(node, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the retrieval of a thumbnail
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
     *
     * @param node Node to cancel the retrieval of the thumbnail
     * @see MegaApi::getThumbnail
     */
    public void cancelGetThumbnail(MegaNode node) {
        megaApi.cancelGetThumbnail(node);
    }

    /**
     * Cancel the retrieval of a preview
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
     *
     * @param node     Node to cancel the retrieval of the preview
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::getPreview
     */
    public void cancelGetPreview(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.cancelGetPreview(node, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the retrieval of a preview
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
     *
     * @param node Node to cancel the retrieval of the preview
     * @see MegaApi::getPreview
     */
    public void cancelGetPreview(MegaNode node) {
        megaApi.cancelGetPreview(node);
    }

    /**
     * Set the thumbnail of a MegaNode
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getFile - Returns the source path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
     *
     * @param node        MegaNode to set the thumbnail
     * @param srcFilePath Source path of the file that will be set as thumbnail
     * @param listener    MegaRequestListener to track this request
     */
    public void setThumbnail(MegaNode node, String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setThumbnail(node, srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the thumbnail of a MegaNode
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getFile - Returns the source path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
     *
     * @param node        MegaNode to set the thumbnail
     * @param srcFilePath Source path of the file that will be set as thumbnail
     */
    public void setThumbnail(MegaNode node, String srcFilePath) {
        megaApi.setThumbnail(node, srcFilePath);
    }

    /**
     * Set the preview of a MegaNode
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getFile - Returns the source path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
     *
     * @param node        MegaNode to set the preview
     * @param srcFilePath Source path of the file that will be set as preview
     * @param listener    MegaRequestListener to track this request
     */
    public void setPreview(MegaNode node, String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setPreview(node, srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the preview of a MegaNode
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getFile - Returns the source path
     * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
     *
     * @param node        MegaNode to set the preview
     * @param srcFilePath Source path of the file that will be set as preview
     */
    public void setPreview(MegaNode node, String srcFilePath) {
        megaApi.setPreview(node, srcFilePath);
    }

    /**
     * Set/Remove the avatar of the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the source path (optional)
     *
     * @param srcFilePath Source path of the file that will be set as avatar.
     *                    If NULL, the existing avatar will be removed (if any).
     *                    In case the avatar never existed before, removing the avatar returns MegaError::API_ENOENT
     * @param listener    MegaRequestListener to track this request
     */
    public void setAvatar(String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setAvatar(srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set/Remove the avatar of the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFile - Returns the source path (optional)
     *
     * @param srcFilePath Source path of the file that will be set as avatar.
     *                    If NULL, the existing avatar will be removed (if any).
     *                    In case the avatar never existed before, removing the avatar returns MegaError::API_ENOENT
     */
    public void setAvatar(String srcFilePath) {
        megaApi.setAvatar(srcFilePath);
    }

    /**
     * Set a public attribute of the current user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param type     Attribute type
     *                 Valid values are:
     *                 MegaApi::USER_ATTR_FIRSTNAME = 1
     *                 Set the firstname of the user (public)
     *                 MegaApi::USER_ATTR_LASTNAME = 2
     *                 Set the lastname of the user (public)
     *                 MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *                 Set the public key Ed25519 of the user (public)
     *                 MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *                 Set the public key Cu25519 of the user (public)
     *                 MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *                 Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *                 <p>
     *                 If the MEGA account is a sub-user business account, and the value of the parameter
     *                 type is equal to MegaApi::USER_ATTR_FIRSTNAME or MegaApi::USER_ATTR_LASTNAME
     *                 onRequestFinish will be called with the error code MegaError::API_EMASTERONLY.
     * @param value    New attribute value
     * @param listener MegaRequestListener to track this request
     */
    public void setUserAttribute(int type, String value, MegaRequestListenerInterface listener) {
        megaApi.setUserAttribute(type, value, createDelegateRequestListener(listener));
    }

    /**
     * Set a public attribute of the current user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param type  Attribute type
     *              Valid values are:
     *              MegaApi::USER_ATTR_FIRSTNAME = 1
     *              Set the firstname of the user (public)
     *              MegaApi::USER_ATTR_LASTNAME = 2
     *              Set the lastname of the user (public)
     *              MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
     *              Set the public key Ed25519 of the user (public)
     *              MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
     *              Set the public key Cu25519 of the user (public)
     *              MegaApi::USER_ATTR_RUBBISH_TIME = 19
     *              Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     *              <p>
     *              If the MEGA account is a sub-user business account, and the value of the parameter
     *              type is equal to MegaApi::USER_ATTR_FIRSTNAME or MegaApi::USER_ATTR_LASTNAME
     *              onRequestFinish will be called with the error code MegaError::API_EMASTERONLY.
     * @param value New attribute value
     */
    public void setUserAttribute(int type, String value) {
        megaApi.setUserAttribute(type, value);
    }

    /**
     * Set a private attribute of the current user
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getMegaStringMap - Returns the new value for the attribute
     *
     * You can remove existing records/keypairs from the following attributes:
     *  - MegaApi::ATTR_ALIAS
     *  - MegaApi::ATTR_DEVICE_NAMES
     *  - MegaApi::USER_ATTR_APPS_PREFS
     *  - MegaApi::USER_ATTR_CC_PREFS
     * by adding a keypair into MegaStringMap whit the key to remove and an empty C-string null terminated as value.
     *
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     * Get whether user generates rich-link messages or not (private)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Set whether the user can send geolocation messages (private)
     * MegaApi::ATTR_ALIAS = 27
     * Set the list of users's aliases (private)
     * MegaApi::ATTR_DEVICE_NAMES = 30
     * Set the list of device names (private)
     * MegaApi::ATTR_APPS_PREFS = 38
     * Set the apps prefs (private)
     *
     * @param value New attribute value
     * @param listener MegaRequestListener to track this request
     */
    public void setUserAttribute(int type, MegaStringMap value, MegaRequestListenerInterface listener) {
        megaApi.setUserAttribute(type, value, createDelegateRequestListener(listener));
    }

    /**
     * Set a private attribute of the current user
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type
     * - MegaRequest::getMegaStringMap - Returns the new value for the attribute
     *
     * You can remove existing records/keypairs from the following attributes:
     *  - MegaApi::ATTR_ALIAS
     *  - MegaApi::ATTR_DEVICE_NAMES
     *  - MegaApi::USER_ATTR_APPS_PREFS
     *  - MegaApi::USER_ATTR_CC_PREFS
     * by adding a keypair into MegaStringMap whit the key to remove and an empty C-string null terminated as value.
     *
     * @param type Attribute type
     *
     * Valid values are:
     *
     * MegaApi::USER_ATTR_AUTHRING = 3
     * Get the authentication ring of the user (private)
     * MegaApi::USER_ATTR_LAST_INTERACTION = 4
     * Get the last interaction of the contacts of the user (private)
     * MegaApi::USER_ATTR_KEYRING = 7
     * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
     * MegaApi::USER_ATTR_RICH_PREVIEWS = 18
     * Get whether user generates rich-link messages or not (private)
     * MegaApi::USER_ATTR_RUBBISH_TIME = 19
     * Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
     * MegaApi::USER_ATTR_GEOLOCATION = 22
     * Set whether the user can send geolocation messages (private)
     * MegaApi::ATTR_ALIAS = 27
     * Set the list of users's aliases (private)
     * MegaApi::ATTR_DEVICE_NAMES = 30
     * Set the list of device names (private)
     * MegaApi::ATTR_APPS_PREFS = 38
     * Set the apps prefs (private)
     *
     * @param value New attribute value
     */
    public void setUserAttribute(int type, MegaStringMap value) {
        megaApi.setUserAttribute(type, value);
    }

    /**
     * Set a custom attribute for the node
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getName - Returns the name of the custom attribute
     * - MegaRequest::getText - Returns the text for the attribute
     * - MegaRequest::getFlag - Returns false (not official attribute)
     * <p>
     * The attribute name must be an UTF8 string with between 1 and 7 bytes
     * If the attribute already has a value, it will be replaced
     * If value is NULL, the attribute will be removed from the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     Node that will receive the attribute
     * @param attrName Name of the custom attribute.
     *                 The length of this parameter must be between 1 and 7 UTF8 bytes
     * @param value    Value for the attribute
     * @param listener MegaRequestListener to track this request
     */
    public void setCustomNodeAttribute(MegaNode node, String attrName, String value, MegaRequestListenerInterface listener) {
        megaApi.setCustomNodeAttribute(node, attrName, value, createDelegateRequestListener(listener));
    }

    /**
     * Set a custom attribute for the node
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getName - Returns the name of the custom attribute
     * - MegaRequest::getText - Returns the text for the attribute
     * - MegaRequest::getFlag - Returns false (not official attribute)
     * <p>
     * The attribute name must be an UTF8 string with between 1 and 7 bytes
     * If the attribute already has a value, it will be replaced
     * If value is NULL, the attribute will be removed from the node
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     Node that will receive the attribute
     * @param attrName Name of the custom attribute.
     *                 The length of this parameter must be between 1 and 7 UTF8 bytes
     * @param value    Value for the attribute
     */
    public void setCustomNodeAttribute(MegaNode node, String attrName, String value) {
        megaApi.setCustomNodeAttribute(node, attrName, value);
    }

    /**
     * Set node label as a node attribute.
     * Valid values for label attribute are:
     * - MegaNode::NODE_LBL_RED = 1
     * - MegaNode::NODE_LBL_ORANGE = 2
     * - MegaNode::NODE_LBL_YELLOW = 3
     * - MegaNode::NODE_LBL_GREEN = 4
     * - MegaNode::NODE_LBL_BLUE = 5
     * - MegaNode::NODE_LBL_PURPLE = 6
     * - MegaNode::NODE_LBL_GREY = 7
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getNumDetails - Returns the label for the node
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_LABEL
     *
     * @param node     Node that will receive the information.
     * @param label    Label of the node
     * @param listener MegaRequestListener to track this request
     */
    public void setNodeLabel(MegaNode node, int label, MegaRequestListenerInterface listener) {
        megaApi.setNodeLabel(node, label, createDelegateRequestListener(listener));
    }

    /**
     * Set node label as a node attribute.
     * Valid values for label attribute are:
     * - MegaNode::NODE_LBL_RED = 1
     * - MegaNode::NODE_LBL_ORANGE = 2
     * - MegaNode::NODE_LBL_YELLOW = 3
     * - MegaNode::NODE_LBL_GREEN = 4
     * - MegaNode::NODE_LBL_BLUE = 5
     * - MegaNode::NODE_LBL_PURPLE = 6
     * - MegaNode::NODE_LBL_GREY = 7
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getNumDetails - Returns the label for the node
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_LABEL
     *
     * @param node  Node that will receive the information.
     * @param label Label of the node
     */
    public void setNodeLabel(MegaNode node, int label) {
        megaApi.setNodeLabel(node, label);
    }

    /**
     * Remove node label
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_LABEL
     *
     * @param node     Node that will receive the information.
     * @param listener MegaRequestListener to track this request
     */
    public void resetNodeLabel(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.resetNodeLabel(node, createDelegateRequestListener(listener));
    }

    /**
     * Remove node label
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_LABEL
     *
     * @param node Node that will receive the information.
     */
    public void resetNodeLabel(MegaNode node) {
        megaApi.resetNodeLabel(node);
    }

    /**
     * Set node favourite as a node attribute.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getNumDetails - Returns 1 if node is set as favourite, otherwise return 0
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_FAV
     *
     * @param node      Node that will receive the information.
     * @param favourite if true set node as favourite, otherwise remove the attribute
     * @param listener  MegaRequestListener to track this request
     */
    public void setNodeFavourite(MegaNode node, boolean favourite, MegaRequestListenerInterface listener) {
        megaApi.setNodeFavourite(node, favourite, createDelegateRequestListener(listener));
    }

    /**
     * Set node favourite as a node attribute.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getNumDetails - Returns 1 if node is set as favourite, otherwise return 0
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_FAV
     *
     * @param node      Node that will receive the information.
     * @param favourite if true set node as favourite, otherwise remove the attribute
     */
    public void setNodeFavourite(MegaNode node, boolean favourite) {
        megaApi.setNodeFavourite(node, favourite);
    }

    /**
     * Mark a node as sensitive
     * <p>
     * Descendants will inherit the sensitive property.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getNumDetails - Returns 1 if node is set as sensitive, otherwise return 0
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_SENSITIVE
     *
     * @param node      Node that will receive the information.
     * @param sensitive if true set node as sensitive, otherwise remove the attribute
     * @param listener  MegaRequestListener to track this request
     */
    public void setNodeSensitive(MegaNode node, boolean sensitive, MegaRequestListenerInterface listener) {
        megaApi.setNodeSensitive(node, sensitive, createDelegateRequestListener(listener));
    }

    /**
     * Mark a node as sensitive
     * <p>
     * Descendants will inherit the sensitive property.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getNumDetails - Returns 1 if node is set as sensitive, otherwise return 0
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_SENSITIVE
     *
     * @param node      Node that will receive the information.
     * @param sensitive if true set node as sensitive, otherwise remove the attribute
     */
    public void setNodeSensitive(MegaNode node, boolean sensitive) {
        megaApi.setNodeSensitive(node, sensitive);
    }

    /**
     * Ascertain if the node is marked as sensitive or a descendent of such
     * <p>
     * see MegaNode::isMarkedSensitive to see if the node is sensitive
     *
     * @param node node to inspect
     */
    public boolean isSensitiveInherited(MegaNode node) {
        return megaApi.isSensitiveInherited(node);
    }

    /**
     * Get a list of favourite nodes.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node provided
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_FAV
     * - MegaRequest::getNumDetails - Returns the count requested
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaHandleList - List of handles of favourite nodes
     *
     * @param node     Node and its children that will be searched for favourites. Search all nodes if null
     * @param count    if count is zero return all favourite nodes, otherwise return only 'count' favourite nodes
     * @param listener MegaRequestListener to track this request
     */
    public void getFavourites(MegaNode node, int count, MegaRequestListenerInterface listener) {
        megaApi.getFavourites(node, count, createDelegateRequestListener(listener));
    }

    /**
     * Set the GPS coordinates of image files as a node attribute.
     * <p>
     * To remove the existing coordinates, set both the latitude and longitude to
     * the value MegaNode::INVALID_COORDINATE.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_COORDINATES
     * - MegaRequest::getNumDetails - Returns the longitude, scaled to integer in the range of [0, 2^24]
     * - MegaRequest::getTransferTag() - Returns the latitude, scaled to integer in the range of [0, 2^24)
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node      Node that will receive the information.
     * @param latitude  Latitude in signed decimal degrees notation
     * @param longitude Longitude in signed decimal degrees notation
     * @param listener  MegaRequestListener to track this request
     */
    public void setNodeCoordinates(MegaNode node, double latitude, double longitude, MegaRequestListenerInterface listener) {
        megaApi.setNodeCoordinates(node, latitude, longitude, createDelegateRequestListener(listener));
    }

    /**
     * Set node description as a node attribute
     *
     * To remove node description, set description to NULL
     *
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that received the attribute
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_DESCRIPTION
     * - MegaRequest::getText - Returns node description
     *
     * If the size of the description is greater than MAX_NODE_DESCRIPTION_SIZE, onRequestFinish will be
     * called with the error code MegaError::API_EARGS.
     *
     * If the MEGA account is a business account and its status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node that will receive the information.
     * @param description Node description
     * @param listener MegaRequestListener to track this request
     */
    public void setNodeDescription(MegaNode node, String description, MegaRequestListenerInterface listener){
        megaApi.setNodeDescription(node, description, createDelegateRequestListener(listener));
    }

    /**
     * Add new tag stored as node attribute
     *
     * The associated request type with this request is MegaRequest::TYPE_TAG_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that received the tag
     * - MegaRequest::getParamType - Returns operation type (0 - Add tag, 1 - Remove tag, 2 - Update tag)
     * - MegaRequest::getText - Returns tag
     *
     * ',' is an invalid character to be used in a tag. If it is contained in the tag,
     * onRequestFinish will be called with the error code MegaError::API_EARGS.
     *
     * If the length of all tags is higher than 3000 onRequestFinish will be called with
     * the error code MegaError::API_EARGS
     *
     * If tag already exists, onRequestFinish will be called with the error code MegaError::API_EEXISTS
     *
     * If number of tags exceed the maximum number of tags (10),
     * onRequestFinish will be called with the error code MegaError::API_ETOOMANY
     *
     * If the MEGA account is a business account and its status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node that will receive the information.
     * @param tag New tag
     * @param listener MegaRequestListener to track this request
     */
    public void addNodeTag(MegaNode node, String tag, MegaRequestListenerInterface listener){
        megaApi.addNodeTag(node, tag, createDelegateRequestListener(listener));
    }

    /**
     * Remove a tag stored as a node attribute
     *
     * The associated request type with this request is MegaRequest::TYPE_TAG_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that received the tag
     * - MegaRequest::getParamType - Returns operation type (0 - Add tag, 1 - Temove tag, 2 - Update tag)
     * - MegaRequest::getText - Returns tag
     *
     * If tag doesn't exist, onRequestFinish will be called with the error code MegaError::API_ENOENT
     *
     * If the MEGA account is a business account and its status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node that will receive the information.
     * @param tag Tag to be removed
     * @param listener MegaRequestListener to track this request
     */
    public void removeNodeTag(MegaNode node, String tag, MegaRequestListenerInterface listener){
        megaApi.removeNodeTag(node, tag, createDelegateRequestListener(listener));
    }

    /**
     * Update a tag stored as a node attribute
     *
     * The associated request type with this request is MegaRequest::TYPE_TAG_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node that received the tag
     * - MegaRequest::getParamType - Returns operation type (0 - Add tag, 1 - Temove tag, 2 - Update tag)
     * - MegaRequest::getText - Returns new tag
     * - MegaRequest::getName - Returns old tag
     *
     * ',' is an invalid character to be used in a tag. If it is contained in the tag,
     * onRequestFinish will be called with the error code MegaError::API_EARGS.
     *
     * If the length of all tags is higher than 3000 characters onRequestFinish will be called with
     * the error code MegaError::API_EARGS
     *
     * If newTag already exists, onRequestFinish will be called with the error code MegaError::API_EEXISTS
     * If oldTag doesn't exist, onRequestFinish will be called with the error code MegaError::API_ENOENT
     *
     * If the MEGA account is a business account and its status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node Node that will receive the information.
     * @param newTag New tag value
     * @param oldTag Old tag value
     * @param listener MegaRequestListener to track this request
     */
    public void updateNodeTag(MegaNode node, String newTag, String oldTag, MegaRequestListenerInterface listener){
        megaApi.updateNodeTag(node, newTag, oldTag, createDelegateRequestListener(listener));
    }

    /**
     * Retrieve all unique node tags present across all nodes in the account
     *
     * If the searchString contains invalid characters, such as ',', an empty list will be
     * returned.
     *
     * This function allows to cancel the processing at any time by passing a
     * MegaCancelToken and calling to MegaCancelToken::setCancelFlag(true).
     *
     * You take ownership of the returned value.
     *
     * @param searchString Optional parameter to filter the tags based on a specific search
     * string. If set to nullptr, all node tags will be retrieved.
     * @param cancelToken MegaCancelToken to be able to cancel the processing at any time.
     *
     * @return All the unique node tags that match the search criteria.
     */
    public MegaStringList getAllNodeTags(String searchString, MegaCancelToken cancelToken){
        megaApi.getAllNodeTags(searchString, cancelToken);
    }

    /**
     * Generate a public link of a file/folder in MEGA
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     MegaNode to get the public link
     * @param listener MegaRequestListener to track this request
     */
    public void exportNode(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.exportNode(node, createDelegateRequestListener(listener));
    }

    /**
     * Generate a public link of a file/folder in MEGA
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     * <p>
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
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node       MegaNode to get the public link
     * @param expireTime Unix timestamp until the public link will be valid
     * @param listener   MegaRequestListener to track this request
     * @implNote A Unix timestamp represents the number of seconds since 00:00 hours, Jan 1, 1970 UTC
     */
    public void exportNode(MegaNode node, int expireTime, MegaRequestListenerInterface listener) {
        megaApi.exportNode(node, expireTime, createDelegateRequestListener(listener));
    }

    /**
     * Generate a temporary public link of a file/folder in MEGA
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Public link
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node       MegaNode to get the public link
     * @param expireTime Unix timestamp until the public link will be valid
     * @implNote A Unix timestamp represents the number of seconds since 00:00 hours, Jan 1, 1970 UTC
     */
    public void exportNode(MegaNode node, int expireTime) {
        megaApi.exportNode(node, expireTime);
    }

    /**
     * Stop sharing a file/folder
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns false
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node     MegaNode to stop sharing
     * @param listener MegaRequestListener to track this request
     */
    public void disableExport(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.disableExport(node, createDelegateRequestListener(listener));
    }

    /**
     * Stop sharing a file/folder
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getAccess - Returns false
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param node MegaNode to stop sharing
     */
    public void disableExport(MegaNode node) {
        megaApi.disableExport(node);
    }

    /**
     * Fetch the filesystem in MEGA and resumes syncs following a successful fetch
     * <p>
     * The MegaApi object must be logged in in an account or a public folder
     * to successfully complete this request.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_FETCH_NODES
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if logged in into a folder and the provided key is invalid. Otherwise, false.
     * - MegaRequest::getNodeHandle - Returns the public handle if logged into a public folder. Otherwise, INVALID_HANDLE
     *
     * @param listener MegaRequestListener to track this request
     */
    public void fetchNodes(MegaRequestListenerInterface listener) {
        megaApi.fetchNodes(createDelegateRequestListener(listener));
    }

    /**
     * Fetch the filesystem in MEGA and resumes syncs following a successful fetch
     * <p>
     * The MegaApi object must be logged in in an account or a public folder
     * to successfully complete this request.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_FETCH_NODES
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if logged in into a folder and the provided key is invalid. Otherwise, false.
     * - MegaRequest::getNodeHandle - Returns the public handle if logged into a public folder. Otherwise, INVALID_HANDLE
     */
    public void fetchNodes() {
        megaApi.fetchNodes();
    }

    /**
     * Get details about the MEGA account
     * <p>
     * Only basic data will be available. If you can get more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     * <p>
     * The available flags are:
     * - storage quota: (numDetails & 0x01)
     * - transfer quota: (numDetails & 0x02)
     * - pro level: (numDetails & 0x04)
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getAccountDetails(MegaRequestListenerInterface listener) {
        megaApi.getAccountDetails(createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account
     * <p>
     * Only basic data will be available. If you can get more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     * <p>
     * The available flags are:
     * - storage quota: (numDetails & 0x01)
     * - transfer quota: (numDetails & 0x02)
     * - pro level: (numDetails & 0x04)
     */
    public void getAccountDetails() {
        megaApi.getAccountDetails();
    }

    /**
     * Get details about the MEGA account
     * <p>
     * Only basic data will be available. If you need more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     * <p>
     * Use this version of the function to get just the details you need, to minimise server load
     * and keep the system highly available for all. At least one flag must be set.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     * <p>
     * The available flags are:
     * - storage quota: (numDetails & 0x01)
     * - transfer quota: (numDetails & 0x02)
     * - pro level: (numDetails & 0x04)
     * <p>
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param storage  If true, account storage details are requested
     * @param transfer If true, account transfer details are requested
     * @param pro      If true, pro level of account is requested
     * @param listener MegaRequestListener to track this request
     */
    public void getSpecificAccountDetails(boolean storage, boolean transfer, boolean pro, MegaRequestListenerInterface listener) {
        megaApi.getSpecificAccountDetails(storage, transfer, pro, -1, createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account
     * <p>
     * Only basic data will be available. If you need more data (sessions, transactions, purchases),
     * use MegaApi::getExtendedAccountDetails.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     * <p>
     * Use this version of the function to get just the details you need, to minimise server load
     * and keep the system highly available for all. At least one flag must be set.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     * <p>
     * The available flags are:
     * - storage quota: (numDetails & 0x01)
     * - transfer quota: (numDetails & 0x02)
     * - pro level: (numDetails & 0x04)
     * <p>
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param storage  If true, account storage details are requested
     * @param transfer If true, account transfer details are requested
     * @param pro      If true, pro level of account is requested
     */
    public void getSpecificAccountDetails(boolean storage, boolean transfer, boolean pro) {
        megaApi.getSpecificAccountDetails(storage, transfer, pro, -1);
    }

    /**
     * Get details about the MEGA account
     * <p>
     * This function allows to optionally get data about sessions, transactions and purchases related to the account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     * <p>
     * The available flags are:
     * - transactions: (numDetails & 0x08)
     * - purchases: (numDetails & 0x10)
     * - sessions: (numDetails & 0x020)
     * <p>
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param sessions     If true, sessions are requested
     * @param purchases    If true, purchases are requested
     * @param transactions If true, transactions are requested
     * @param listener     MegaRequestListener to track this request
     */
    public void getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions, MegaRequestListenerInterface listener) {
        megaApi.getExtendedAccountDetails(sessions, purchases, transactions, createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account
     * <p>
     * This function allows to optionally get data about sessions, transactions and purchases related to the account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
     * - MegaRequest::getNumDetails - Requested flags
     * <p>
     * The available flags are:
     * - transactions: (numDetails & 0x08)
     * - purchases: (numDetails & 0x10)
     * - sessions: (numDetails & 0x020)
     * <p>
     * In case none of the flags are set, the associated request will fail with error MegaError::API_EARGS.
     *
     * @param sessions     If true, sessions are requested
     * @param purchases    If true, purchases are requested
     * @param transactions If true, transactions are requested
     */
    public void getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions) {
        megaApi.getExtendedAccountDetails(sessions, purchases, transactions);
    }

    /**
     * Get the available pricing plans to upgrade a MEGA account
     * <p>
     * You can get a payment ID for any of the pricing plans provided by this function
     * using MegaApi::getPaymentId
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PRICING
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPricing - MegaPricing object with all pricing plans
     * - MegaRequest::getCurrency - MegaCurrency object with currency data related to prices
     *
     * @param listener MegaRequestListener to track this request
     * @see MegaApi::getPaymentId
     */
    public void getPricing(MegaRequestListenerInterface listener) {
        megaApi.getPricing(createDelegateRequestListener(listener));
    }

    /**
     * Get the available pricing plans to upgrade a MEGA account
     * <p>
     * You can get a payment ID for any of the pricing plans provided by this function
     * using MegaApi::getPaymentId
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PRICING
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPricing - MegaPricing object with all pricing plans
     * - MegaRequest::getCurrency - MegaCurrency object with currency data related to prices
     *
     * @see MegaApi::getPaymentId
     */
    public void getPricing() {
        megaApi.getPricing();
    }

    /**
     * Get the payment URL for an upgrade
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     * @param listener      MegaRequestListener to track this request
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, MegaRequestListenerInterface listener) {
        megaApi.getPaymentId(productHandle, createDelegateRequestListener(listener));
    }

    /**
     * Get the payment URL for an upgrade
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle Handle of the product (see MegaApi::getPricing)
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle) {
        megaApi.getPaymentId(productHandle);
    }

    /**
     * Get the payment URL for an upgrade
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle    Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param listener         MegaRequestListener to track this request
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle, MegaRequestListenerInterface listener) {
        megaApi.getPaymentId(productHandle, lastPublicHandle, createDelegateRequestListener(listener));
    }

    /**
     * Get the payment URL for an upgrade
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     *
     * @param productHandle    Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle) {
        megaApi.getPaymentId(productHandle, lastPublicHandle);
    }

    /**
     * Get the payment URL for an upgrade
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param productHandle        Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle     Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *                             - MegaApi::AFFILIATE_TYPE_ID = 1
     *                             - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *                             - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *                             - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     * @param lastAccessTimestamp  Timestamp of the last access
     * @param listener             MegaRequestListener to track this request
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle, int lastPublicHandleType,
                             long lastAccessTimestamp, MegaRequestListenerInterface listener) {
        megaApi.getPaymentId(productHandle, lastPublicHandle, lastPublicHandleType,
                lastAccessTimestamp, createDelegateRequestListener(listener));
    }

    /**
     * Get the payment URL for an upgrade
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the product
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getLink - Payment ID
     * - MegaRequest::getParentHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param productHandle        Handle of the product (see MegaApi::getPricing)
     * @param lastPublicHandle     Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *                             - MegaApi::AFFILIATE_TYPE_ID = 1
     *                             - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *                             - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *                             - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     * @param lastAccessTimestamp  Timestamp of the last access
     * @see MegaApi::getPricing
     */
    public void getPaymentId(long productHandle, long lastPublicHandle, int lastPublicHandleType, long lastAccessTimestamp) {
        megaApi.getPaymentId(productHandle, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp);
    }

    /**
     * Upgrade an account
     *
     * @param productHandle Product handle to purchase
     *                      <p>
     *                      It's possible to get all pricing plans with their product handles using
     *                      MegaApi::getPricing
     *                      <p>
     *                      The associated request type with this request is MegaRequest::TYPE_UPGRADE_ACCOUNT
     *                      Valid data in the MegaRequest object received on callbacks:
     *                      - MegaRequest::getNodeHandle - Returns the handle of the product
     *                      - MegaRequest::getNumber - Returns the payment method
     * @param paymentMethod Payment method
     *                      Valid values are:
     *                      - MegaApi::PAYMENT_METHOD_BALANCE = 0
     *                      Use the account balance for the payment
     *                      <p>
     *                      - MegaApi::PAYMENT_METHOD_CREDIT_CARD = 8
     *                      Complete the payment with your credit card. Use MegaApi::creditCardStore to add
     *                      a credit card to your account
     * @param listener      MegaRequestListener to track this request
     */
    public void upgradeAccount(long productHandle, int paymentMethod, MegaRequestListenerInterface listener) {
        megaApi.upgradeAccount(productHandle, paymentMethod, createDelegateRequestListener(listener));
    }

    /**
     * Upgrade an account
     *
     * @param productHandle Product handle to purchase
     *                      <p>
     *                      It's possible to get all pricing plans with their product handles using
     *                      MegaApi::getPricing
     *                      <p>
     *                      The associated request type with this request is MegaRequest::TYPE_UPGRADE_ACCOUNT
     *                      Valid data in the MegaRequest object received on callbacks:
     *                      - MegaRequest::getNodeHandle - Returns the handle of the product
     *                      - MegaRequest::getNumber - Returns the payment method
     * @param paymentMethod Payment method
     *                      Valid values are:
     *                      - MegaApi::PAYMENT_METHOD_BALANCE = 0
     *                      Use the account balance for the payment
     *                      <p>
     *                      - MegaApi::PAYMENT_METHOD_CREDIT_CARD = 8
     *                      Complete the payment with your credit card. Use MegaApi::creditCardStore to add
     *                      a credit card to your account
     */
    public void upgradeAccount(long productHandle, int paymentMethod) {
        megaApi.upgradeAccount(productHandle, paymentMethod);
    }

    /**
     * Submit a purchase receipt for verification
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     *
     * @param gateway  Payment gateway
     *                 Currently supported payment gateways are:
     *                 - MegaApi::PAYMENT_METHOD_ITUNES = 2
     *                 - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     *                 - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     * @param receipt  Purchase receipt
     * @param listener MegaRequestListener to track this request
     */
    public void submitPurchaseReceipt(int gateway, String receipt, MegaRequestListenerInterface listener) {
        megaApi.submitPurchaseReceipt(gateway, receipt, createDelegateRequestListener(listener));
    }

    /**
     * Submit a purchase receipt for verification
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     *
     * @param gateway Payment gateway
     *                Currently supported payment gateways are:
     *                - MegaApi::PAYMENT_METHOD_ITUNES = 2
     *                - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     *                - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     * @param receipt Purchase receipt
     */
    public void submitPurchaseReceipt(int gateway, String receipt) {
        megaApi.submitPurchaseReceipt(gateway, receipt);
    }

    /**
     * Submit a purchase receipt for verification
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     *
     * @param gateway          Payment gateway
     *                         Currently supported payment gateways are:
     *                         - MegaApi::PAYMENT_METHOD_ITUNES = 2
     *                         - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     *                         - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     * @param receipt          Purchase receipt
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     * @param listener         MegaRequestListener to track this request
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle, MegaRequestListenerInterface listener) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle, createDelegateRequestListener(listener));
    }

    /**
     * Submit a purchase receipt for verification
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     *
     * @param gateway          Payment gateway
     *                         Currently supported payment gateways are:
     *                         - MegaApi::PAYMENT_METHOD_ITUNES = 2
     *                         - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     *                         - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     * @param receipt          Purchase receipt
     * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle);
    }

    /**
     * Submit a purchase receipt for verification
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param gateway              Payment gateway
     *                             Currently supported payment gateways are:
     *                             - MegaApi::PAYMENT_METHOD_ITUNES = 2
     *                             - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     *                             - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     * @param receipt              Purchase receipt
     * @param lastPublicHandle     Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *                             - MegaApi::AFFILIATE_TYPE_ID = 1
     *                             - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *                             - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *                             - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     * @param lastAccessTimestamp  Timestamp of the last access
     * @param listener             MegaRequestListener to track this request
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle, int lastPublicHandleType,
                                      long lastAccessTimestamp, MegaRequestListenerInterface listener) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle, lastPublicHandleType,
                lastAccessTimestamp, createDelegateRequestListener(listener));
    }

    /**
     * Submit a purchase receipt for verification
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the payment gateway
     * - MegaRequest::getText - Returns the purchase receipt
     * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
     * - MegaRequest::getParamType - Returns the type of lastPublicHandle
     * - MegaRequest::getTransferredBytes - Returns the timestamp of the last access
     *
     * @param gateway              Payment gateway
     *                             Currently supported payment gateways are:
     *                             - MegaApi::PAYMENT_METHOD_ITUNES = 2
     *                             - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3
     *                             - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     * @param receipt              Purchase receipt
     * @param lastPublicHandle     Last public node handle accessed by the user in the last 24h
     * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
     *                             - MegaApi::AFFILIATE_TYPE_ID = 1
     *                             - MegaApi::AFFILIATE_TYPE_FILE_FOLDER = 2
     *                             - MegaApi::AFFILIATE_TYPE_CHAT = 3
     *                             - MegaApi::AFFILIATE_TYPE_CONTACT = 4
     * @param lastAccessTimestamp  Timestamp of the last access
     */
    public void submitPurchaseReceipt(int gateway, String receipt, long lastPublicHandle, int lastPublicHandleType, long lastAccessTimestamp) {
        megaApi.submitPurchaseReceipt(gateway, receipt, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp);
    }

    /**
     * Store a credit card
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_STORE
     *
     * @param address1     Billing address
     * @param address2     Second line of the billing address (optional)
     * @param city         City of the billing address
     * @param province     Province of the billing address
     * @param country      Country of the billing address
     * @param postalcode   Postal code of the billing address
     * @param firstname    Firstname of the owner of the credit card
     * @param lastname     Lastname of the owner of the credit card
     * @param creditcard   Credit card number. Only digits, no spaces nor dashes
     * @param expire_month Expire month of the credit card. Must have two digits ("03" for example)
     * @param expire_year  Expire year of the credit card. Must have four digits ("2010" for example)
     * @param cv2          Security code of the credit card (3 digits)
     * @param listener     MegaRequestListener to track this request
     */
    public void creditCardStore(String address1, String address2, String city, String province, String country, String postalcode, String firstname, String lastname, String creditcard, String expire_month, String expire_year, String cv2, MegaRequestListenerInterface listener) {
        megaApi.creditCardStore(address1, address2, city, province, country, postalcode, firstname, lastname, creditcard, expire_month, expire_year, cv2, createDelegateRequestListener(listener));
    }

    /**
     * Store a credit card
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_STORE
     *
     * @param address1     Billing address
     * @param address2     Second line of the billing address (optional)
     * @param city         City of the billing address
     * @param province     Province of the billing address
     * @param country      Country of the billing address
     * @param postalcode   Postal code of the billing address
     * @param firstname    Firstname of the owner of the credit card
     * @param lastname     Lastname of the owner of the credit card
     * @param creditcard   Credit card number. Only digits, no spaces nor dashes
     * @param expire_month Expire month of the credit card. Must have two digits ("03" for example)
     * @param expire_year  Expire year of the credit card. Must have four digits ("2010" for example)
     * @param cv2          Security code of the credit card (3 digits)
     */
    public void creditCardStore(String address1, String address2, String city, String province, String country, String postalcode, String firstname, String lastname, String creditcard, String expire_month, String expire_year, String cv2) {
        megaApi.creditCardStore(address1, address2, city, province, country, postalcode, firstname, lastname, creditcard, expire_month, expire_year, cv2);
    }

    /**
     * Get the credit card subscriptions of the account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Number of credit card subscriptions
     *
     * @param listener MegaRequestListener to track this request
     */
    public void creditCardQuerySubscriptions(MegaRequestListenerInterface listener) {
        megaApi.creditCardQuerySubscriptions(createDelegateRequestListener(listener));
    }

    /**
     * Get the credit card subscriptions of the account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Number of credit card subscriptions
     */
    public void creditCardQuerySubscriptions() {
        megaApi.creditCardQuerySubscriptions();
    }

    /**
     * Cancel credit card subscriptions of the account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS
     *
     * @param reason   Reason for the cancellation. It can be NULL.
     * @param listener MegaRequestListener to track this request
     */
    public void creditCardCancelSubscriptions(String reason, MegaRequestListenerInterface listener) {
        megaApi.creditCardCancelSubscriptions(reason, createDelegateRequestListener(listener));
    }

    /**
     * Cancel credit card subscriptions of the account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS
     *
     * @param reason Reason for the cancellation. It can be NULL.
     */
    public void creditCardCancelSubscriptions(String reason) {
        megaApi.creditCardCancelSubscriptions(reason);
    }

    /**
     * Get the available payment methods
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_METHODS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Bitfield with available payment methods
     * <p>
     * To know if a payment method is available, you can do a check like this one:
     * request->getNumber() & (1 << MegaApi::PAYMENT_METHOD_CREDIT_CARD)
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getPaymentMethods(MegaRequestListenerInterface listener) {
        megaApi.getPaymentMethods(createDelegateRequestListener(listener));
    }

    /**
     * Get the available payment methods
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_METHODS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Bitfield with available payment methods
     * <p>
     * To know if a payment method is available, you can do a check like this one:
     * request->getNumber() & (1 << MegaApi::PAYMENT_METHOD_CREDIT_CARD)
     */
    public void getPaymentMethods() {
        megaApi.getPaymentMethods();
    }

    /**
     * Export the master key of the account
     * <p>
     * The returned value is a Base64-encoded string
     * <p>
     * With the master key, it's possible to start the recovery of an account when the
     * password is lost:
     * - https://mega.nz/#recovery
     * - MegaApi::resetPassword()
     * <p>
     * You take the ownership of the returned value.
     *
     * @return Base64-encoded master key
     */
    @Nullable
    public String exportMasterKey() {
        return megaApi.exportMasterKey();
    }

    /**
     * Notify the user has exported the master key
     * <p>
     * This function should be called when the user exports the master key by
     * clicking on "Copy" or "Save file" options.
     * <p>
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember the user has a backup of his/her master key. In consequence,
     * MEGA will not ask the user to remind the password for the account.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void masterKeyExported(MegaRequestListenerInterface listener) {
        megaApi.masterKeyExported(createDelegateRequestListener(listener));
    }

    /**
     * Notify the user has successfully checked his password
     * <p>
     * This function should be called when the user demonstrates that he remembers
     * the password to access the account
     * <p>
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember this event. In consequence, MEGA will not continue asking the user
     * to remind the password for the account in a short time.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void passwordReminderDialogSucceeded(MegaRequestListenerInterface listener) {
        megaApi.passwordReminderDialogSucceeded(createDelegateRequestListener(listener));
    }

    /**
     * Notify the user has successfully skipped the password check
     * <p>
     * This function should be called when the user skips the verification of
     * the password to access the account
     * <p>
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember this event. In consequence, MEGA will not continue asking the user
     * to remind the password for the account in a short time.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void passwordReminderDialogSkipped(MegaRequestListenerInterface listener) {
        megaApi.passwordReminderDialogSkipped(createDelegateRequestListener(listener));
    }

    /**
     * Notify the user wants to totally disable the password check
     * <p>
     * This function should be called when the user rejects to verify that he remembers
     * the password to access the account and doesn't want to see the reminder again.
     * <p>
     * As result, the user attribute MegaApi::USER_ATTR_PWD_REMINDER will be updated
     * to remember this event. In consequence, MEGA will not ask the user
     * to remind the password for the account again.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * - MegaRequest::getText - Returns the new value for the attribute
     *
     * @param listener MegaRequestListener to track this request
     */
    public void passwordReminderDialogBlocked(MegaRequestListenerInterface listener) {
        megaApi.passwordReminderDialogBlocked(createDelegateRequestListener(listener));
    }

    /**
     * Check if the app should show the password reminder dialog to the user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if the password reminder dialog should be shown
     * <p>
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT but the value of MegaRequest::getFlag will still
     * be valid.
     *
     * @param atLogout True if the check is being done just before a logout
     * @param listener MegaRequestListener to track this request
     */
    public void shouldShowPasswordReminderDialog(boolean atLogout, MegaRequestListenerInterface listener) {
        megaApi.shouldShowPasswordReminderDialog(atLogout, createDelegateRequestListener(listener));
    }

    /**
     * Check if the master key has been exported
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getAccess - Returns true if the master key has been exported
     * <p>
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void isMasterKeyExported(MegaRequestListenerInterface listener) {
        megaApi.isMasterKeyExported(createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable the generation of rich previews
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param enable   True to enable the generation of rich previews
     * @param listener MegaRequestListener to track this request
     */
    public void enableRichPreviews(boolean enable, MegaRequestListenerInterface listener) {
        megaApi.enableRichPreviews(enable, createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable the generation of rich previews
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param enable True to enable the generation of rich previews
     */
    public void enableRichPreviews(boolean enable) {
        megaApi.enableRichPreviews(enable);
    }

    /**
     * Check if rich previews are automatically generated
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns zero
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if generation of rich previews is enabled
     * - MegaRequest::getMegaStringMap - Returns the raw content of the attribute: [<key><value>]*
     * <p>
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (false).
     *
     * @param listener MegaRequestListener to track this request
     */
    public void isRichPreviewsEnabled(MegaRequestListenerInterface listener) {
        megaApi.isRichPreviewsEnabled(createDelegateRequestListener(listener));
    }

    /**
     * Check if rich previews are automatically generated
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns zero
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if generation of rich previews is enabled
     * - MegaRequest::getMegaStringMap - Returns the raw content of the attribute: [<key><value>]*
     * <p>
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (false).
     */
    public void isRichPreviewsEnabled() {
        megaApi.isRichPreviewsEnabled();
    }

    /**
     * Check if the app should show the rich link warning dialog to the user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns one
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if it is necessary to show the rich link warning
     * - MegaRequest::getNumber - Returns the number of times that user has indicated that doesn't want
     * modify the message with a rich link. If number is bigger than three, the extra option "Never"
     * must be added to the warning dialog.
     * - MegaRequest::getMegaStringMap - Returns the raw content of the attribute: [<key><value>]*
     * <p>
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (true).
     *
     * @param listener MegaRequestListener to track this request
     */
    public void shouldShowRichLinkWarning(MegaRequestListenerInterface listener) {
        megaApi.shouldShowRichLinkWarning(createDelegateRequestListener(listener));
    }

    /**
     * Check if the app should show the rich link warning dialog to the user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     * - MegaRequest::getNumDetails - Returns one
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getFlag - Returns true if it is necessary to show the rich link warning
     * - MegaRequest::getNumber - Returns the number of times that user has indicated that doesn't want
     * modify the message with a rich link. If number is bigger than three, the extra option "Never"
     * must be added to the warning dialog.
     * - MegaRequest::getMegaStringMap - Returns the raw content of the attribute: [<key><value>]*
     * <p>
     * If the corresponding user attribute is not set yet, the request will fail with the
     * error code MegaError::API_ENOENT, but the value of MegaRequest::getFlag will still be valid (true).
     */
    public void shouldShowRichLinkWarning() {
        megaApi.shouldShowRichLinkWarning();
    }

    /**
     * Set the number of times "Not now" option has been selected in the rich link warning dialog
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param value    Number of times "Not now" option has been selected
     * @param listener MegaRequestListener to track this request
     */
    public void setRichLinkWarningCounterValue(int value, MegaRequestListenerInterface listener) {
        megaApi.setRichLinkWarningCounterValue(value, createDelegateRequestListener(listener));
    }

    /**
     * Set the number of times "Not now" option has been selected in the rich link warning dialog
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
     *
     * @param value Number of times "Not now" option has been selected
     */
    public void setRichLinkWarningCounterValue(int value) {
        megaApi.setRichLinkWarningCounterValue(value);
    }

    /**
     * Enable the sending of geolocation messages
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_GEOLOCATION
     *
     * @param listener MegaRequestListener to track this request
     */
    public void enableGeolocation(MegaRequestListenerInterface listener) {
        megaApi.enableGeolocation(createDelegateRequestListener(listener));
    }

    /**
     * Check if the sending of geolocation messages is enabled
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_GEOLOCATION
     * <p>
     * Sending a Geolocation message is enabled if the MegaRequest object, received in onRequestFinish,
     * has error code MegaError::API_OK. In other cases, send geolocation messages is not enabled and
     * the application has to answer before send a message of this type.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void isGeolocationEnabled(MegaRequestListenerInterface listener) {
        megaApi.isGeolocationEnabled(createDelegateRequestListener(listener));
    }

    /**
     * Set My Chat Files target folder.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER
     * - MegaRequest::getMegaStringMap - Returns a MegaStringMap.
     * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
     *
     * @param nodehandle MegaHandle of the node to be used as target folder
     * @param listener   MegaRequestListener to track this request
     */
    public void setMyChatFilesFolder(long nodehandle, MegaRequestListenerInterface listener) {
        megaApi.setMyChatFilesFolder(nodehandle, createDelegateRequestListener(listener));
    }

    /**
     * Gets My chat files target folder.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodehandle - Returns the handle of the node where My Chat Files are stored
     * <p>
     * If the folder is not set, the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getMyChatFilesFolder(MegaRequestListenerInterface listener) {
        megaApi.getMyChatFilesFolder(createDelegateRequestListener(listener));
    }

    /**
     * Set Camera Uploads primary target folder.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getFlag - Returns false
     * - MegaRequest::getNodehandle - Returns the provided node handle
     * - MegaRequest::getMegaStringMap - Returns a MegaStringMap.
     * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
     *
     * @param nodehandle MegaHandle of the node to be used as primary target folder
     * @param listener   MegaRequestListener to track this request
     */
    public void setCameraUploadsFolder(long nodehandle, MegaRequestListenerInterface listener) {
        megaApi.setCameraUploadsFolder(nodehandle, createDelegateRequestListener(listener));
    }

    /**
     * Set Camera Uploads for both primary and secondary target folder.
     * <p>
     * If only one of the target folders wants to be set, simply pass a INVALID_HANDLE to
     * as the other target folder and it will remain untouched.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getNodehandle - Returns the provided node handle for primary folder
     * - MegaRequest::getParentHandle - Returns the provided node handle for secondary folder
     *
     * @param primaryFolder   MegaHandle of the node to be used as primary target folder
     * @param secondaryFolder MegaHandle of the node to be used as secondary target folder
     * @param listener        MegaRequestListener to track this request
     */
    public void setCameraUploadsFolders(long primaryFolder, long secondaryFolder, MegaRequestListenerInterface listener) {
        megaApi.setCameraUploadsFolders(primaryFolder, secondaryFolder, createDelegateRequestListener(listener));
    }

    /**
     * Gets Camera Uploads primary target folder.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getFlag - Returns false
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodehandle - Returns the handle of the primary node where Camera Uploads files are stored
     * <p>
     * If the folder is not set, the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getCameraUploadsFolder(MegaRequestListenerInterface listener) {
        megaApi.getCameraUploadsFolder(createDelegateRequestListener(listener));
    }

    /**
     * Gets Camera Uploads secondary target folder.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
     * - MegaRequest::getFlag - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodehandle - Returns the handle of the secondary node where Camera Uploads files are stored
     * <p>
     * If the secondary folder is not set, the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getCameraUploadsFolderSecondary(MegaRequestListenerInterface listener) {
        megaApi.getCameraUploadsFolderSecondary(createDelegateRequestListener(listener));
    }

    /**
     * Gets the alias for an user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_ALIAS
     * - MegaRequest::getNodeHandle - user handle in binary
     * - MegaRequest::getText - user handle encoded in B64
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns the user alias
     * <p>
     * If the user alias doesn't exists the request will fail with the error code MegaError::API_ENOENT.
     *
     * @param uh       handle of the user in binary
     * @param listener MegaRequestListener to track this request
     */
    public void getUserAlias(long uh, MegaRequestListenerInterface listener) {
        megaApi.getUserAlias(uh, createDelegateRequestListener(listener));
    }

    /**
     * Set or reset an alias for a user
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_ALIAS
     * - MegaRequest::getNodeHandle - Returns the user handle in binary
     * - MegaRequest::getText - Returns the user alias
     *
     * @param uh       handle of the user in binary
     * @param alias    the user alias, or null to reset the existing
     * @param listener MegaRequestListener to track this request
     */
    public void setUserAlias(long uh, String alias, MegaRequestListenerInterface listener) {
        megaApi.setUserAlias(uh, alias, createDelegateRequestListener(listener));
    }

    /**
     * Get push notification settings
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PUSH_SETTINGS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaPushNotificationSettings - Returns settings for push notifications
     *
     * @param listener MegaRequestListener to track this request
     * @see MegaPushNotificationSettings class for more details.
     */
    public void getPushNotificationSettings(MegaRequestListenerInterface listener) {
        megaApi.getPushNotificationSettings(createDelegateRequestListener(listener));
    }

    /**
     * Set push notification settings
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PUSH_SETTINGS
     * - MegaRequest::getMegaPushNotificationSettings - Returns settings for push notifications
     *
     * @param settings MegaPushNotificationSettings with the new settings
     * @param listener MegaRequestListener to track this request
     * @see MegaPushNotificationSettings class for more details. You can prepare a new object by
     * calling MegaPushNotificationSettings::createInstance.
     */
    public void setPushNotificationSettings(MegaPushNotificationSettings settings, MegaRequestListenerInterface listener) {
        megaApi.setPushNotificationSettings(settings, createDelegateRequestListener(listener));
    }

    /**
     * Get the number of days for rubbish-bin cleaning scheduler
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RUBBISH_TIME
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler.
     * Zero means that the rubbish-bin cleaning scheduler is disabled (only if the account is PRO)
     * Any negative value means that the configured value is invalid.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getRubbishBinAutopurgePeriod(MegaRequestListenerInterface listener) {
        megaApi.getRubbishBinAutopurgePeriod(createDelegateRequestListener(listener));
    }

    /**
     * Set the number of days for rubbish-bin cleaning scheduler
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RUBBISH_TIME
     * - MegaRequest::getNumber - Returns the days for rubbish-bin cleaning scheduler passed as parameter
     *
     * @param days     Number of days for rubbish-bin cleaning scheduler. It must be >= 0.
     *                 The value zero disables the rubbish-bin cleaning scheduler (only for PRO accounts).
     * @param listener MegaRequestListener to track this request
     */
    public void setRubbishBinAutopurgePeriod(int days, MegaRequestListenerInterface listener) {
        megaApi.setRubbishBinAutopurgePeriod(days, createDelegateRequestListener(listener));
    }

    /**
     * Returns the id of this device
     * <p>
     * You take the ownership of the returned value.
     *
     * @return The id of this device
     */
    @Nullable
    public String getDeviceId() {
        return megaApi.getDeviceId();
    }

    /**
     * Returns the name set for this device
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DEVICE_NAMES
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns device name.
     *
     * @param listener MegaRequestListener to track this request
     * @deprecated This version of the function is deprecated. Please use the non-deprecated one below.
     */
    @Deprecated
    public void getDeviceName(MegaRequestListenerInterface listener) {
        megaApi.getDeviceName(createDelegateRequestListener(listener));
    }

    /**
     * Returns the name previously set for a device
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DEVICE_NAMES
     * - MegaRequest::getText - Returns passed device id (or the value returned by getDeviceId()
     * if deviceId was initially passed as null).
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns device name.
     *
     * @param deviceId The id of the device to get the name for. If null, the value returned
     * by getDeviceId() will be used instead.
     * @param listener MegaRequestListener to track this request
     */
    public void getDeviceName(String deviceId, MegaRequestListenerInterface listener) {
        megaApi.getDeviceName(deviceId, createDelegateRequestListener(listener));
    }

    /**
     * Sets device name
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DEVICE_NAMES
     * - MegaRequest::getName - Returns device name.
     *
     * @param deviceName String with device name
     * @param listener   MegaRequestListener to track this request
     * @deprecated This version of the function is deprecated. Please use the non-deprecated one below.
     */
    @Deprecated
    public void setDeviceName(String deviceName, MegaRequestListenerInterface listener) {
        megaApi.setDeviceName(deviceName, createDelegateRequestListener(listener));
    }

    /**
     * Sets name for specified device
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DEVICE_NAMES
     * - MegaRequest::getName - Returns device name.
     * - MegaRequest::getText - Returns passed device id (or the value returned by getDeviceId()
     * if deviceId was initially passed as null).
     *
     * @param deviceId The id of the device to set the name for. If null, the value returned
     * by getDeviceId() will be used instead.
     * @param deviceName String with device name
     * @param listener MegaRequestListener to track this request
     */
    public void setDeviceName(String deviceId, String deviceName, MegaRequestListenerInterface listener) {
        megaApi.setDeviceName(deviceId, deviceName, createDelegateRequestListener(listener));
    }

    /**
     * Returns the name set for this drive
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DRIVE_NAMES
     * - MegaRequest::getFile - Returns the path to the drive
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns drive name.
     *
     * @param pathToDrive Path to the root of the external drive
     * @param listener    MegaRequestListener to track this request
     */
    public void getDriveName(String pathToDrive, MegaRequestListenerInterface listener) {
        megaApi.getDriveName(pathToDrive, createDelegateRequestListener(listener));
    }

    /**
     * Returns the name set for this drive
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DRIVE_NAMES
     * - MegaRequest::getFile - Returns the path to the drive
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getName - Returns drive name.
     *
     * @param pathToDrive Path to the root of the external drive
     */
    public void getDriveName(String pathToDrive) {
        megaApi.getDriveName(pathToDrive);
    }

    /**
     * Sets drive name
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DRIVE_NAMES
     * - MegaRequest::getName - Returns drive name.
     * - MegaRequest::getFile - Returns the path to the drive
     *
     * @param pathToDrive Path to the root of the external drive
     * @param driveName   String with drive name
     * @param listener    MegaRequestListener to track this request
     */
    public void setDriveName(String pathToDrive, String driveName, MegaRequestListenerInterface listener) {
        megaApi.setDriveName(pathToDrive, driveName, createDelegateRequestListener(listener));
    }

    /**
     * Sets drive name
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DRIVE_NAMES
     * - MegaRequest::getName - Returns drive name.
     * - MegaRequest::getFile - Returns the path to the drive
     *
     * @param pathToDrive Path to the root of the external drive
     * @param driveName   String with drive name
     */
    public void setDriveName(String pathToDrive, String driveName) {
        megaApi.setDriveName(pathToDrive, driveName);
    }

    /**
     * Change the password of the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getPassword - Returns the old password (if it was passed as parameter)
     * - MegaRequest::getNewPassword - Returns the new password
     *
     * @param oldPassword Old password (optional, it can be NULL to not check the old password)
     * @param newPassword New password
     * @param listener    MegaRequestListener to track this request
     */
    public void changePassword(String oldPassword, String newPassword, MegaRequestListenerInterface listener) {
        megaApi.changePassword(oldPassword, newPassword, createDelegateRequestListener(listener));
    }

    /**
     * Change the password of the MEGA account
     * <p>
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
     * Invite another person to be your MEGA contact
     * <p>
     * The user doesn't need to be registered on MEGA. If the email isn't associated with
     * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_INVITE_CONTACT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getText - Returns the text of the invitation
     * - MegaRequest::getNumber - Returns the action
     * <p>
     * Sending a reminder within a two week period since you started or your last reminder will
     * fail the API returning the error code MegaError::API_EACCESS.
     *
     * @param email    Email of the new contact
     * @param message  Message for the user (can be NULL)
     * @param action   Action for this contact request. Valid values are:
     *                 - MegaContactRequest::INVITE_ACTION_ADD = 0
     *                 - MegaContactRequest::INVITE_ACTION_DELETE = 1
     *                 - MegaContactRequest::INVITE_ACTION_REMIND = 2
     * @param listener MegaRequestListener to track this request
     */
    public void inviteContact(String email, String message, int action, MegaRequestListenerInterface listener) {
        megaApi.inviteContact(email, message, action, createDelegateRequestListener(listener));
    }

    /**
     * Invite another person to be your MEGA contact
     * <p>
     * The user doesn't need to be registered on MEGA. If the email isn't associated with
     * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_INVITE_CONTACT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getText - Returns the text of the invitation
     * - MegaRequest::getNumber - Returns the action
     * <p>
     * Sending a reminder within a two week period since you started or your last reminder will
     * fail the API returning the error code MegaError::API_EACCESS.
     *
     * @param email   Email of the new contact
     * @param message Message for the user (can be NULL)
     * @param action  Action for this contact request. Valid values are:
     *                - MegaContactRequest::INVITE_ACTION_ADD = 0
     *                - MegaContactRequest::INVITE_ACTION_DELETE = 1
     *                - MegaContactRequest::INVITE_ACTION_REMIND = 2
     */
    public void inviteContact(String email, String message, int action) {
        megaApi.inviteContact(email, message, action);
    }

    /**
     * Invite another person to be your MEGA contact using a contact link handle
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_INVITE_CONTACT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     * - MegaRequest::getText - Returns the text of the invitation
     * - MegaRequest::getNumber - Returns the action
     * - MegaRequest::getNodeHandle - Returns the contact link handle
     * <p>
     * Sending a reminder within a two week period since you started or your last reminder will
     * fail the API returning the error code MegaError::API_EACCESS.
     *
     * @param email       Email of the new contact
     * @param message     Message for the user (can be NULL)
     * @param action      Action for this contact request. Valid values are:
     *                    - MegaContactRequest::INVITE_ACTION_ADD = 0
     *                    - MegaContactRequest::INVITE_ACTION_DELETE = 1
     *                    - MegaContactRequest::INVITE_ACTION_REMIND = 2
     * @param contactLink Contact link handle of the other account. This parameter is considered only if the
     *                    \c action is MegaContactRequest::INVITE_ACTION_ADD. Otherwise, it's ignored and it has no effect.
     * @param listener    MegaRequestListener to track this request
     */
    public void inviteContact(String email, String message, int action, long contactLink, MegaRequestListenerInterface listener) {
        megaApi.inviteContact(email, message, action, contactLink, createDelegateRequestListener(listener));
    }

    /**
     * Reply to a contact request
     *
     * @param request  Contact request. You can get your pending contact requests using MegaApi::getIncomingContactRequests
     * @param action   Action for this contact request. Valid values are:
     *                 - MegaContactRequest::REPLY_ACTION_ACCEPT = 0
     *                 - MegaContactRequest::REPLY_ACTION_DENY = 1
     *                 - MegaContactRequest::REPLY_ACTION_IGNORE = 2
     *                 <p>
     *                 The associated request type with this request is MegaRequest::TYPE_REPLY_CONTACT_REQUEST
     *                 Valid data in the MegaRequest object received on callbacks:
     *                 - MegaRequest::getNodeHandle - Returns the handle of the contact request
     *                 - MegaRequest::getNumber - Returns the action
     * @param listener MegaRequestListener to track this request
     */
    public void replyContactRequest(MegaContactRequest request, int action, MegaRequestListenerInterface listener) {
        megaApi.replyContactRequest(request, action, createDelegateRequestListener(listener));
    }

    /**
     * Reply to a contact request
     *
     * @param request Contact request. You can get your pending contact requests using MegaApi::getIncomingContactRequests
     * @param action  Action for this contact request. Valid values are:
     *                - MegaContactRequest::REPLY_ACTION_ACCEPT = 0
     *                - MegaContactRequest::REPLY_ACTION_DENY = 1
     *                - MegaContactRequest::REPLY_ACTION_IGNORE = 2
     *                <p>
     *                The associated request type with this request is MegaRequest::TYPE_REPLY_CONTACT_REQUEST
     *                Valid data in the MegaRequest object received on callbacks:
     *                - MegaRequest::getNodeHandle - Returns the handle of the contact request
     *                - MegaRequest::getNumber - Returns the action
     */
    public void replyContactRequest(MegaContactRequest request, int action) {
        megaApi.replyContactRequest(request, action);
    }

    /**
     * Remove a contact to the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_CONTACT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     *
     * @param user     MegaUser of the contact (see MegaApi::getContact)
     * @param listener MegaRequestListener to track this request
     */
    public void removeContact(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.removeContact(user, createDelegateRequestListener(listener));
    }

    /**
     * Remove a contact to the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_CONTACT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getEmail - Returns the email of the contact
     *
     * @param user MegaUser of the contact (see MegaApi::getContact)
     */
    public void removeContact(MegaUser user) {
        megaApi.removeContact(user);
    }

    /**
     * Logout of the MEGA account invalidating the session
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGOUT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the keepSyncConfigsFile
     * - MegaRequest::getFlag - Returns true
     * <p>
     * Under certain circumstances, this request might return the error code
     * MegaError::API_ESID. It should not be taken as an error, since the reason
     * is that the logout action has been notified before the reception of the
     * logout response itself.
     * <p>
     * In case of an automatic logout (ie. when the account become blocked by
     * ToS infringement), the MegaRequest::getParamType indicates the error that
     * triggered the automatic logout (MegaError::API_EBLOCKED for the example).
     *
     * @param listener MegaRequestListener to track this request
     */
    public void logout(MegaRequestListenerInterface listener) {
        megaApi.logout(false, createDelegateRequestListener(listener));
    }

    /**
     * Logout of the MEGA account invalidating the session
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGOUT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the keepSyncConfigsFile
     * - MegaRequest::getFlag - Returns true
     * <p>
     * Under certain circumstances, this request might return the error code
     * MegaError::API_ESID. It should not be taken as an error, since the reason
     * is that the logout action has been notified before the reception of the
     * logout response itself.
     * <p>
     * In case of an automatic logout (ie. when the account become blocked by
     * ToS infringement), the MegaRequest::getParamType indicates the error that
     * triggered the automatic logout (MegaError::API_EBLOCKED for the example).
     */
    public void logout() {
        megaApi.logout(false, null);
    }

    /**
     * Logout of the MEGA account without invalidating the session
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGOUT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns false
     *
     * @param listener MegaRequestListener to track this request
     */
    public void localLogout(MegaRequestListenerInterface listener) {
        megaApi.localLogout(createDelegateRequestListener(listener));
    }

    /**
     * Logout of the MEGA account without invalidating the session
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_LOGOUT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns false
     */
    public void localLogout() {
        megaApi.localLogout();
    }

    /**
     * Invalidate the existing cache and create a fresh one
     */
    public void invalidateCache() {
        megaApi.invalidateCache();
    }

    /**
     * Estimate the strength of a password
     * <p>
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
    public int getPasswordStrength(String password) {
        return megaApi.getPasswordStrength(password);
    }

    /**
     * Generate a new pseudo-randomly characters-based password
     *
     * You take ownership of the returned value.
     * Use delete[] to free it.
     *
     * @param useUpper  boolean indicating if at least 1 upper case letter shall be included
     * @param useDigit  boolean indicating if at least 1 digit shall be included
     * @param useSymbol boolean indicating if at least 1 symbol from !@#$%^&*() shall be included
     * @param length    int with the number of characters that will be included.
     *                  Minimum valid length is 8 and maximum valid is 64.
     * @return Null-terminated char string containing the newly generated password.
     */
    public static String generateRandomCharsPassword(boolean useUpper, boolean useDigit, boolean useSymbol, int length) {
        return MegaApi.generateRandomCharsPassword(useUpper, useDigit, useSymbol, length);
    }

    /**
     * Send events to the stats server
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SEND_EVENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the event type
     * - MegaRequest::getText - Returns the event message
     *
     * @param eventType Event type
     *                  Event types are restricted to the following ranges:
     *                  - MEGAcmd:   [98900, 99000)
     *                  - MEGAchat:  [99000, 99199)
     *                  - Android:   [99200, 99300)
     *                  - iOS:       [99300, 99400)
     *                  - MEGA SDK:  [99400, 99500)
     *                  - MEGAsync:  [99500, 99600)
     *                  - Webclient: [99600, 99800]
     * @param message   Event message
     * @deprecated This function is for internal usage of MEGA apps for debug purposes. This info
     * is sent to MEGA servers.
     * This version of the function is deprecated. Please use the non-deprecated one below.
     */
    @Deprecated
    public void sendEvent(int eventType, String message) {
        megaApi.sendEvent(eventType, message);
    }

    /**
     * Send events to the stats server
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SEND_EVENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the event type
     * - MegaRequest::getText - Returns the event message
     * - MegaRequest::getFlag - Returns the addJourneyId flag
     * - MegaRequest::getSessionKey - Returns the ViewID
     *
     * @param eventType    Event type
     *                     Event types are restricted to the following ranges:
     *                     - MEGAcmd:   [98900, 99000)
     *                     - MEGAchat:  [99000, 99199)
     *                     - Android:   [99200, 99300)
     *                     - iOS:       [99300, 99400)
     *                     - MEGA SDK:  [99400, 99500)
     *                     - MEGAsync:  [99500, 99600)
     *                     - Webclient: [99600, 99800]
     * @param message      Event message
     * @param addJourneyId True if JourneyID should be included. Otherwise, false.
     * @param viewId       ViewID value (C-string null-terminated) to be sent with the event.
     *                     This value should have been generated with MegaApi::generateViewId method.
     * @deprecated This function is for internal usage of MEGA apps for debug purposes. This info
     * is sent to MEGA servers.
     */
    @Deprecated
    public void sendEvent(int eventType, String message, boolean addJourneyId, @Nullable String viewId) {
        megaApi.sendEvent(eventType, message, addJourneyId, viewId);
    }

    /**
     * Create a new ticket for support with attached description
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SUPPORT_TICKET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the type of the ticket
     * - MegaRequest::getText - Returns the description of the issue
     *
     * @param message  Description of the issue for support
     * @param type     Ticket type. These are the available types:
     *                 0  for General Enquiry
     *                 1  for Technical Issue
     *                 2  for Payment Issue
     *                 3  for Forgotten Password
     *                 4  for Transfer Issue
     *                 5  for Contact/Sharing Issue
     *                 6  for MEGAsync Issue
     *                 7  for Missing/Invisible Data
     *                 8  for help-centre clarifications
     *                 9  for iOS issue
     *                 10 for Android issue
     * @param listener MegaRequestListener to track this request
     */
    public void createSupportTicket(String message, int type, MegaRequestListenerInterface listener) {
        megaApi.createSupportTicket(message, type, createDelegateRequestListener(listener));
    }

    /**
     * Use HTTPS communications only
     * <p>
     * The default behavior is to use HTTP for transfers and the persistent connection
     * to wait for external events. Those communications don't require HTTPS because
     * all transfer data is already end-to-end encrypted and no data is transmitted
     * over the connection to wait for events (it's just closed when there are new events).
     * <p>
     * This feature should only be enabled if there are problems to contact MEGA servers
     * through HTTP because otherwise it doesn't have any benefit and will cause a
     * higher CPU usage.
     * <p>
     * See MegaApi::usingHttpsOnly
     *
     * @param httpsOnly True to use HTTPS communications only
     */
    public void useHttpsOnly(boolean httpsOnly) {
        megaApi.useHttpsOnly(httpsOnly);
    }

    /**
     * Check if the SDK is using HTTPS communications only
     * <p>
     * The default behavior is to use HTTP for transfers and the persistent connection
     * to wait for external events. Those communications don't require HTTPS because
     * all transfer data is already end-to-end encrypted and no data is transmitted
     * over the connection to wait for events (it's just closed when there are new events).
     * <p>
     * See MegaApi::useHttpsOnly
     *
     * @return True if the SDK is using HTTPS communications only. Otherwise false.
     */
    public boolean usingHttpsOnly() {
        return megaApi.usingHttpsOnly();
    }

    //****************************************************************************************************/
    // TRANSFERS
    //****************************************************************************************************/

    /**
     * Upload a file to support
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     * <p>
     * For folders, onTransferFinish will be called with error MegaError:API_EARGS;
     *
     * @param localPath         Local path of the file
     * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
     *                          This parameter is intended to automatically delete temporary files that are only created to be uploaded.
     *                          Use this parameter with caution. Set it to true only if you are sure about what are you doing.
     * @param listener          MegaTransferListener to track this transfer
     */
    public void startUploadForSupport(String localPath, boolean isSourceTemporary, MegaTransferListenerInterface listener) {
        megaApi.startUploadForSupport(localPath, isSourceTemporary, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or a folder
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     * <p>
     * When user wants to upload/download a batch of items that at least contains one folder, SDK mutex will be partially
     * locked until:
     * - we have received onTransferStart for every file in the batch
     * - we have received onTransferUpdate with MegaTransfer::getStage == MegaTransfer::STAGE_TRANSFERRING_FILES
     * for every folder in the batch
     * <p>
     * During this period, the only safe method (to avoid deadlocks) to cancel transfers is by calling CancelToken::cancel(true).
     * This method will cancel all transfers(not finished yet).
     * <p>
     * Important considerations:
     * - A cancel token instance can be shared by multiple transfers, and calling CancelToken::cancel(true) will affect all
     * of those transfers.
     * <p>
     * - It's app responsibility, to keep cancel token instance alive until receive MegaTransferListener::onTransferFinish for all MegaTransfers
     * that shares the same cancel token instance.
     * <p>
     * In case any other folder is being uploaded/downloaded, and MegaTransfer::getStage for that transfer returns
     * a value between the following stages: MegaTransfer::STAGE_SCAN and MegaTransfer::STAGE_PROCESS_TRANSFER_QUEUE
     * both included, don't use MegaApi::cancelTransfer to cancel this transfer (it could generate a deadlock),
     * instead of that, use MegaCancelToken::cancel(true) calling through MegaCancelToken instance associated to this transfer.
     * <p>
     * For more information about MegaTransfer stages please refer to onTransferUpdate documentation.
     *
     * @param localPath         Local path of the file or folder
     * @param parent            Parent node for the file or folder in the MEGA account
     * @param fileName          Custom file name for the file or folder in MEGA
     *                          + If you don't need this param provide NULL as value
     * @param mtime             Custom modification time for the file in MEGA (in seconds since the epoch)
     *                          + If you don't need this param provide MegaApi::INVALID_CUSTOM_MOD_TIME as value
     * @param appData           Custom app data to save in the MegaTransfer object
     *                          The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     *                          related to the transfer. If a transfer is started with exactly the same data
     *                          (local path and target parent) as another one in the transfer queue, the new transfer
     *                          fails with the error API_EEXISTS and the appData of the new transfer is appended to
     *                          the appData of the old transfer, using a '!' separator if the old transfer had already
     *                          appData.
     *                          + If you don't need this param provide NULL as value
     * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
     *                          This parameter is intended to automatically delete temporary files that are only created to be uploaded.
     *                          Use this parameter with caution. Set it to true only if you are sure about what are you doing.
     *                          + If you don't need this param provide false as value
     * @param startFirst        puts the transfer on top of the upload queue
     *                          + If you don't need this param provide false as value
     * @param cancelToken       MegaCancelToken to be able to cancel a folder/file upload process.
     *                          This param is required to be able to cancel the transfer safely.
     *                          App retains the ownership of this param.
     * @param listener          MegaTransferListener to track this transfer
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, long mtime,
                            String appData, boolean isSourceTemporary, boolean startFirst,
                            MegaCancelToken cancelToken, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, fileName, mtime, appData, isSourceTemporary,
                startFirst, cancelToken, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file or a folder
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     * <p>
     * When user wants to upload/download a batch of items that at least contains one folder, SDK mutex will be partially
     * locked until:
     * - we have received onTransferStart for every file in the batch
     * - we have received onTransferUpdate with MegaTransfer::getStage == MegaTransfer::STAGE_TRANSFERRING_FILES
     * for every folder in the batch
     * <p>
     * During this period, the only safe method (to avoid deadlocks) to cancel transfers is by calling CancelToken::cancel(true).
     * This method will cancel all transfers(not finished yet).
     * <p>
     * Important considerations:
     * - A cancel token instance can be shared by multiple transfers, and calling CancelToken::cancel(true) will affect all
     * of those transfers.
     * <p>
     * - It's app responsibility, to keep cancel token instance alive until receive MegaTransferListener::onTransferFinish for all MegaTransfers
     * that shares the same cancel token instance.
     * <p>
     * In case any other folder is being uploaded/downloaded, and MegaTransfer::getStage for that transfer returns
     * a value between the following stages: MegaTransfer::STAGE_SCAN and MegaTransfer::STAGE_PROCESS_TRANSFER_QUEUE
     * both included, don't use MegaApi::cancelTransfer to cancel this transfer (it could generate a deadlock),
     * instead of that, use MegaCancelToken::cancel(true) calling through MegaCancelToken instance associated to this transfer.
     * <p>
     * For more information about MegaTransfer stages please refer to onTransferUpdate documentation.
     *
     * @param localPath         Local path of the file or folder
     * @param parent            Parent node for the file or folder in the MEGA account
     * @param fileName          Custom file name for the file or folder in MEGA
     *                          + If you don't need this param provide NULL as value
     * @param mtime             Custom modification time for the file in MEGA (in seconds since the epoch)
     *                          + If you don't need this param provide MegaApi::INVALID_CUSTOM_MOD_TIME as value
     * @param appData           Custom app data to save in the MegaTransfer object
     *                          The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     *                          related to the transfer. If a transfer is started with exactly the same data
     *                          (local path and target parent) as another one in the transfer queue, the new transfer
     *                          fails with the error API_EEXISTS and the appData of the new transfer is appended to
     *                          the appData of the old transfer, using a '!' separator if the old transfer had already
     *                          appData.
     *                          + If you don't need this param provide NULL as value
     * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
     *                          This parameter is intended to automatically delete temporary files that are only created to be uploaded.
     *                          Use this parameter with caution. Set it to true only if you are sure about what are you doing.
     *                          + If you don't need this param provide false as value
     * @param startFirst        puts the transfer on top of the upload queue
     *                          + If you don't need this param provide false as value
     * @param cancelToken       MegaCancelToken to be able to cancel a folder/file upload process.
     *                          This param is required to be able to cancel the transfer safely.
     *                          App retains the ownership of this param.
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, long mtime,
                            String appData, boolean isSourceTemporary, boolean startFirst,
                            MegaCancelToken cancelToken) {
        megaApi.startUpload(localPath, parent, fileName, mtime, appData, isSourceTemporary,
                startFirst, cancelToken);
    }

    /**
     * Upload a file or a folder
     * <p>
     * This method should be used ONLY to share by chat a local file. In case the file
     * is already uploaded, but the corresponding node is missing the thumbnail and/or preview,
     * this method will force a new upload from the scratch (ensuring the file attributes are set),
     * instead of doing a remote copy.
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath         Local path of the file or folder
     * @param parent            Parent node for the file or folder in the MEGA account
     * @param appData           Custom app data to save in the MegaTransfer object
     *                          The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     *                          related to the transfer. If a transfer is started with exactly the same data
     *                          (local path and target parent) as another one in the transfer queue, the new transfer
     *                          fails with the error API_EEXISTS and the appData of the new transfer is appended to
     *                          the appData of the old transfer, using a '!' separator if the old transfer had already
     *                          appData.
     * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
     *                          This parameter is intended to automatically delete temporary files that are only created to be uploaded.
     *                          Use this parameter with caution. Set it to true only if you are sure about what are you doing.
     * @param fileName          Custom file name for the file or folder in MEGA
     */
    public void startUploadForChat(String localPath, MegaNode parent, String appData, boolean isSourceTemporary, String fileName) {
        megaApi.startUploadForChat(localPath, parent, appData, isSourceTemporary, fileName);
    }

    /**
     * Upload a file or a folder
     * <p>
     * This method should be used ONLY to share by chat a local file. In case the file
     * is already uploaded, but the corresponding node is missing the thumbnail and/or preview,
     * this method will force a new upload from the scratch (ensuring the file attributes are set),
     * instead of doing a remote copy.
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param localPath         Local path of the file or folder
     * @param parent            Parent node for the file or folder in the MEGA account
     * @param appData           Custom app data to save in the MegaTransfer object
     *                          The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     *                          related to the transfer. If a transfer is started with exactly the same data
     *                          (local path and target parent) as another one in the transfer queue, the new transfer
     *                          fails with the error API_EEXISTS and the appData of the new transfer is appended to
     *                          the appData of the old transfer, using a '!' separator if the old transfer had already
     *                          appData.
     * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
     *                          This parameter is intended to automatically delete temporary files that are only created to be uploaded.
     *                          Use this parameter with caution. Set it to true only if you are sure about what are you doing.
     * @param fileName          Custom file name for the file or folder in MEGA
     * @param listener          MegaTransferListener to track this transfer
     */
    public void startUploadForChat(String localPath, MegaNode parent, String appData, boolean isSourceTemporary, String fileName, MegaTransferListenerInterface listener) {
        megaApi.startUploadForChat(localPath, parent, appData, isSourceTemporary, fileName, createDelegateTransferListener(listener));
    }

    /**
     * Download a file or a folder from MEGA, saving custom app data during the transfer
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     * <p>
     * In case any other folder is being uploaded/downloaded, and MegaTransfer::getStage for that transfer returns
     * a value between the following stages: MegaTransfer::STAGE_SCAN and MegaTransfer::STAGE_PROCESS_TRANSFER_QUEUE
     * both included, don't use MegaApi::cancelTransfer to cancel this transfer (it could generate a deadlock),
     * instead of that, use MegaCancelToken::cancel(true) calling through MegaCancelToken instance associated to this transfer.
     * <p>
     * For more information about MegaTransfer stages please refer to onTransferUpdate documentation.
     *
     * @param node        MegaNode that identifies the file or folder
     * @param localPath   Destination path for the file or folder
     *                    If this path is a local folder, it must end with a '\' or '/' character and the file name
     *                    in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     * @param fileName    Custom file name for the file or folder in local destination
     *                    + If you don't need this param provide NULL as value
     * @param appData     Custom app data to save in the MegaTransfer object
     *                    The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     *                    related to the transfer.
     *                    + If you don't need this param provide NULL as value
     * @param startFirst  puts the transfer on top of the download queue
     *                    + If you don't need this param provide false as value
     * @param cancelToken MegaCancelToken to be able to cancel a folder/file download process.
     *                    This param is required to be able to cancel the transfer safely by calling MegaCancelToken::cancel(true)
     *                    You preserve the ownership of this param.
     * @param collisionCheck Indicates collision check on same files, valid values are:
     *      - MegaTransfer::COLLISION_CHECK_ASSUMESAME          = 1,
     *      - MegaTransfer::COLLISION_CHECK_ALWAYSERROR         = 2,
     *      - MegaTransfer::COLLISION_CHECK_FINGERPRINT         = 3,
     *      - MegaTransfer::COLLISION_CHECK_METAMAC             = 4,
     *      - MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT     = 5,
     *
     * @param collisionResolution Indicates how to save same files, valid values are:
     *      - MegaTransfer::COLLISION_RESOLUTION_OVERWRITE                      = 1,
     *      - MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N                     = 2,
     *      - MegaTransfer::COLLISION_RESOLUTION_EXISTING_TO_OLDN               = 3,
     * @param listener    MegaTransferListener to track this transfer
     */
    public void startDownload(MegaNode node, String localPath, String fileName, String appData,
                              boolean startFirst, MegaCancelToken cancelToken, int collisionCheck, int collisionResolution,
                              MegaTransferListenerInterface listener) {
        megaApi.startDownload(node, localPath, fileName, appData, startFirst, cancelToken, collisionCheck, collisionResolution,
                false, createDelegateTransferListener(listener));
    }

    /**
     * Download a file or a folder from MEGA, saving custom app data during the transfer
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     * <p>
     * In case any other folder is being uploaded/downloaded, and MegaTransfer::getStage for that transfer returns
     * a value between the following stages: MegaTransfer::STAGE_SCAN and MegaTransfer::STAGE_PROCESS_TRANSFER_QUEUE
     * both included, don't use MegaApi::cancelTransfer to cancel this transfer (it could generate a deadlock),
     * instead of that, use MegaCancelToken::cancel(true) calling through MegaCancelToken instance associated to this transfer.
     * <p>
     * For more information about MegaTransfer stages please refer to onTransferUpdate documentation.
     *
     * @param node        MegaNode that identifies the file or folder
     * @param localPath   Destination path for the file or folder
     *                    If this path is a local folder, it must end with a '\' or '/' character and the file name
     *                    in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     *                    one of these characters, the file will be downloaded to a file in that path.
     * @param fileName    Custom file name for the file or folder in local destination
     *                    + If you don't need this param provide NULL as value
     * @param appData     Custom app data to save in the MegaTransfer object
     *                    The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
     *                    related to the transfer.
     *                    + If you don't need this param provide NULL as value
     * @param startFirst  puts the transfer on top of the download queue
     *                    + If you don't need this param provide false as value
     * @param cancelToken MegaCancelToken to be able to cancel a folder/file download process.
     *                    This param is required to be able to cancel the transfer safely by calling MegaCancelToken::cancel(true)
     *                    You preserve the ownership of this param.
     * @param collisionCheck Indicates collision check on same files, valid values are:
     *      - MegaTransfer::COLLISION_CHECK_ASSUMESAME          = 1,
     *      - MegaTransfer::COLLISION_CHECK_ALWAYSERROR         = 2,
     *      - MegaTransfer::COLLISION_CHECK_FINGERPRINT         = 3,
     *      - MegaTransfer::COLLISION_CHECK_METAMAC             = 4,
     *      - MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT     = 5,
     *
     * @param collisionResolution Indicates how to save same files, valid values are:
     *      - MegaTransfer::COLLISION_RESOLUTION_OVERWRITE                      = 1,
     *      - MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N                     = 2,
     *      - MegaTransfer::COLLISION_RESOLUTION_EXISTING_TO_OLDN               = 3,
     */
    public void startDownload(MegaNode node, String localPath, String fileName, String appData,
                              boolean startFirst, MegaCancelToken cancelToken, int collisionCheck, int collisionResolution) {
        megaApi.startDownload(node, localPath, fileName, appData, startFirst, cancelToken, collisionCheck, collisionResolution,
                false);
    }

    /**
     * Start an streaming download for a file in MEGA
     * <p>
     * Streaming downloads don't save the downloaded data into a local file. It is provided
     * in MegaTransferListener::onTransferUpdate in a byte buffer. The pointer is returned by
     * MegaTransfer::getLastBytes and the size of the buffer in MegaTransfer::getDeltaSize
     * <p>
     * The same byte array is also provided in the callback MegaTransferListener::onTransferData for
     * compatibility with other programming languages. Only the MegaTransferListener passed to this function
     * will receive MegaTransferListener::onTransferData callbacks. MegaTransferListener objects registered
     * with MegaApi::addTransferListener won't receive them for performance reasons
     * <p>
     * If the status of the business account is expired, onTransferFinish will be called with the error
     * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
     * "Your business account is overdue, please contact your administrator."
     *
     * @param node     MegaNode that identifies the file
     * @param startPos First byte to download from the file
     * @param size     Size of the data to download
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
     * @param transfer MegaTransfer object that identifies the transfer.
     *                 You can get this object in any MegaTransferListener callback or any MegaListener callback
     *                 related to transfers.
     * @param listener MegaRequestListener to track this request.
     */
    public void cancelTransfer(MegaTransfer transfer, MegaRequestListenerInterface listener) {
        megaApi.cancelTransfer(transfer, createDelegateRequestListener(listener));
    }

    /**
     * Cancel a transfer.
     *
     * @param transfer MegaTransfer object that identifies the transfer.
     *                 You can get this object in any MegaTransferListener callback or any MegaListener callback
     *                 related to transfers.
     */
    public void cancelTransfer(MegaTransfer transfer) {
        megaApi.cancelTransfer(transfer);
    }

    /**
     * Move a transfer one position up in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_UP
     *
     * @param transfer Transfer to move
     * @param listener MegaRequestListener to track this request
     */
    public void moveTransferUp(MegaTransfer transfer, MegaRequestListenerInterface listener) {
        megaApi.moveTransferUp(transfer, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer one position up in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_UP
     *
     * @param transfer Transfer to move
     */
    public void moveTransferUp(MegaTransfer transfer) {
        megaApi.moveTransferUp(transfer);
    }

    /**
     * Move a transfer one position up in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_UP
     *
     * @param transferTag Tag of the transfer to move
     * @param listener    MegaRequestListener to track this request
     */
    public void moveTransferUpByTag(int transferTag, MegaRequestListenerInterface listener) {
        megaApi.moveTransferUpByTag(transferTag, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer one position up in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_UP
     *
     * @param transferTag Tag of the transfer to move
     */
    public void moveTransferUpByTag(int transferTag) {
        megaApi.moveTransferUpByTag(transferTag);
    }

    /**
     * Move a transfer one position down in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_DOWN
     *
     * @param transfer Transfer to move
     * @param listener MegaRequestListener to track this request
     */
    public void moveTransferDown(MegaTransfer transfer, MegaRequestListenerInterface listener) {
        megaApi.moveTransferDown(transfer, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer one position down in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_DOWN
     *
     * @param transfer Transfer to move
     */
    public void moveTransferDown(MegaTransfer transfer) {
        megaApi.moveTransferDown(transfer);
    }

    /**
     * Move a transfer one position down in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_DOWN
     *
     * @param transferTag Tag of the transfer to move
     * @param listener    MegaRequestListener to track this request
     */
    public void moveTransferDownByTag(int transferTag, MegaRequestListenerInterface listener) {
        megaApi.moveTransferDownByTag(transferTag, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer one position down in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_DOWN
     *
     * @param transferTag Tag of the transfer to move
     */
    public void moveTransferDownByTag(int transferTag) {
        megaApi.moveTransferDownByTag(transferTag);
    }

    /**
     * Move a transfer to the top of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_TOP
     *
     * @param transfer Transfer to move
     * @param listener MegaRequestListener to track this request
     */
    public void moveTransferToFirst(MegaTransfer transfer, MegaRequestListenerInterface listener) {
        megaApi.moveTransferToFirst(transfer, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer to the top of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_TOP
     *
     * @param transfer Transfer to move
     */
    public void moveTransferToFirst(MegaTransfer transfer) {
        megaApi.moveTransferToFirst(transfer);
    }

    /**
     * Move a transfer to the top of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_TOP
     *
     * @param transferTag Tag of the transfer to move
     * @param listener    MegaRequestListener to track this request
     */
    public void moveTransferToFirstByTag(int transferTag, MegaRequestListenerInterface listener) {
        megaApi.moveTransferToFirstByTag(transferTag, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer to the top of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_TOP
     *
     * @param transferTag Tag of the transfer to move
     */
    public void moveTransferToFirstByTag(int transferTag) {
        megaApi.moveTransferToFirstByTag(transferTag);
    }

    /**
     * Move a transfer to the bottom of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_BOTTOM
     *
     * @param transfer Transfer to move
     * @param listener MegaRequestListener to track this request
     */
    public void moveTransferToLast(MegaTransfer transfer, MegaRequestListenerInterface listener) {
        megaApi.moveTransferToLast(transfer, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer to the bottom of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_BOTTOM
     *
     * @param transfer Transfer to move
     */
    public void moveTransferToLast(MegaTransfer transfer) {
        megaApi.moveTransferToLast(transfer);
    }

    /**
     * Move a transfer to the bottom of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_BOTTOM
     *
     * @param transferTag Tag of the transfer to move
     * @param listener    MegaRequestListener to track this request
     */
    public void moveTransferToLastByTag(int transferTag, MegaRequestListenerInterface listener) {
        megaApi.moveTransferToLastByTag(transferTag, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer to the bottom of the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
     * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_BOTTOM
     *
     * @param transferTag Tag of the transfer to move
     */
    public void moveTransferToLastByTag(int transferTag) {
        megaApi.moveTransferToLastByTag(transferTag);
    }

    /**
     * Move a transfer before another one in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns false (it means that it's a manual move)
     * - MegaRequest::getNumber - Returns the tag of the transfer with the target position
     *
     * @param transfer     Transfer to move
     * @param prevTransfer Transfer with the target position
     * @param listener     MegaRequestListener to track this request
     */
    public void moveTransferBefore(MegaTransfer transfer, MegaTransfer prevTransfer, MegaRequestListenerInterface listener) {
        megaApi.moveTransferBefore(transfer, prevTransfer, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer before another one in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns false (it means that it's a manual move)
     * - MegaRequest::getNumber - Returns the tag of the transfer with the target position
     *
     * @param transfer     Transfer to move
     * @param prevTransfer Transfer with the target position
     */
    public void moveTransferBefore(MegaTransfer transfer, MegaTransfer prevTransfer) {
        megaApi.moveTransferBefore(transfer, prevTransfer);
    }

    /**
     * Move a transfer before another one in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns false (it means that it's a manual move)
     * - MegaRequest::getNumber - Returns the tag of the transfer with the target position
     *
     * @param transferTag     Tag of the transfer to move
     * @param prevTransferTag Tag of the transfer with the target position
     * @param listener        MegaRequestListener to track this request
     */
    public void moveTransferBeforeByTag(int transferTag, int prevTransferTag, MegaRequestListenerInterface listener) {
        megaApi.moveTransferBeforeByTag(transferTag, prevTransferTag, createDelegateRequestListener(listener));
    }

    /**
     * Move a transfer before another one in the transfer queue
     * <p>
     * If the transfer is successfully moved, onTransferUpdate will be called
     * for the corresponding listeners of the moved transfer and the new priority
     * of the transfer will be available using MegaTransfer::getPriority
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
     * - MegaRequest::getFlag - Returns false (it means that it's a manual move)
     * - MegaRequest::getNumber - Returns the tag of the transfer with the target position
     *
     * @param transferTag     Tag of the transfer to move
     * @param prevTransferTag Tag of the transfer with the target position
     */
    public void moveTransferBeforeByTag(int transferTag, int prevTransferTag) {
        megaApi.moveTransferBeforeByTag(transferTag, prevTransferTag);
    }

    /**
     * Cancel the transfer with a specific tag
     * <p>
     * When a transfer is cancelled, it will finish and will provide the error code
     * MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and
     * MegaListener::onTransferFinish
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
     *
     * @param transferTag tag that identifies the transfer
     *                    You can get this tag using MegaTransfer::getTag
     * @param listener    MegaRequestListener to track this request
     */
    public void cancelTransferByTag(int transferTag, MegaRequestListenerInterface listener) {
        megaApi.cancelTransferByTag(transferTag, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the transfer with a specific tag.
     *
     * @param transferTag tag that identifies the transfer.
     *                    You can get this tag using MegaTransfer.getTag().
     */
    public void cancelTransferByTag(int transferTag) {
        megaApi.cancelTransferByTag(transferTag);
    }

    /**
     * Cancel all transfers of the same type
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the first parameter
     *
     * @param direction Type of transfers to cancel.
     *                  Valid values are:
     *                  - MegaTransfer::TYPE_DOWNLOAD = 0
     *                  - MegaTransfer::TYPE_UPLOAD = 1
     * @param listener  MegaRequestListener to track this request
     */
    public void cancelTransfers(int direction, MegaRequestListenerInterface listener) {
        megaApi.cancelTransfers(direction, createDelegateRequestListener(listener));
    }

    /**
     * Cancel all transfers of the same type
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the first parameter
     *
     * @param direction Type of transfers to cancel.
     *                  Valid values are:
     *                  - MegaTransfer::TYPE_DOWNLOAD = 0
     *                  - MegaTransfer::TYPE_UPLOAD = 1
     */
    public void cancelTransfers(int direction) {
        megaApi.cancelTransfers(direction);
    }

    /**
     * Pause/resume all transfers
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns the first parameter
     *
     * @param pause    true to pause all transfers / false to resume all transfers
     * @param listener MegaRequestListener to track this request
     */
    public void pauseTransfers(boolean pause, MegaRequestListenerInterface listener) {
        megaApi.pauseTransfers(pause, createDelegateRequestListener(listener));
    }

    /**
     * Pause/resume all transfers
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns the first parameter
     *
     * @param pause true to pause all transfers / false to resume all transfers
     */
    public void pauseTransfers(boolean pause) {
        megaApi.pauseTransfers(pause);
    }

    /**
     * Pause/resume all transfers in one direction (uploads or downloads)
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns the first parameter
     * - MegaRequest::getNumber - Returns the direction of the transfers to pause/resume
     *
     * @param pause     true to pause transfers / false to resume transfers
     * @param direction Direction of transfers to pause/resume
     *                  Valid values for this parameter are:
     *                  - MegaTransfer::TYPE_DOWNLOAD = 0
     *                  - MegaTransfer::TYPE_UPLOAD = 1
     * @param listener  MegaRequestListener to track this request
     */
    public void pauseTransfers(boolean pause, int direction, MegaRequestListenerInterface listener) {
        megaApi.pauseTransfers(pause, direction, createDelegateRequestListener(listener));
    }

    /**
     * Pause/resume all transfers in one direction (uploads or downloads)
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Returns the first parameter
     * - MegaRequest::getNumber - Returns the direction of the transfers to pause/resume
     *
     * @param pause     true to pause transfers / false to resume transfers
     * @param direction Direction of transfers to pause/resume
     *                  Valid values for this parameter are:
     *                  - MegaTransfer::TYPE_DOWNLOAD = 0
     *                  - MegaTransfer::TYPE_UPLOAD = 1
     */
    public void pauseTransfers(boolean pause, int direction) {
        megaApi.pauseTransfers(pause, direction);
    }

    /**
     * Pause/resume a transfer
     * <p>
     * The request finishes with MegaError::API_OK if the state of the transfer is the
     * desired one at that moment. That means that the request succeed when the transfer
     * is successfully paused or resumed, but also if the transfer was already in the
     * desired state and it wasn't needed to change anything.
     * <p>
     * Resumed transfers don't necessarily continue just after the resumption. They
     * are tagged as queued and are processed according to its position on the request queue.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to pause or resume
     * - MegaRequest::getFlag - Returns true if the transfer has to be pause or false if it has to be resumed
     *
     * @param transfer Transfer to pause or resume
     * @param pause    True to pause the transfer or false to resume it
     * @param listener MegaRequestListener to track this request
     */
    public void pauseTransfer(MegaTransfer transfer, boolean pause, MegaRequestListenerInterface listener) {
        megaApi.pauseTransfer(transfer, pause, createDelegateRequestListener(listener));
    }

    /**
     * Pause/resume a transfer
     * <p>
     * The request finishes with MegaError::API_OK if the state of the transfer is the
     * desired one at that moment. That means that the request succeed when the transfer
     * is successfully paused or resumed, but also if the transfer was already in the
     * desired state and it wasn't needed to change anything.
     * <p>
     * Resumed transfers don't necessarily continue just after the resumption. They
     * are tagged as queued and are processed according to its position on the request queue.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTransferTag - Returns the tag of the transfer to pause or resume
     * - MegaRequest::getFlag - Returns true if the transfer has to be pause or false if it has to be resumed
     *
     * @param transferTag Tag of the transfer to pause or resume
     * @param pause       True to pause the transfer or false to resume it
     * @param listener    MegaRequestListener to track this request
     */
    public void pauseTransferByTag(int transferTag, boolean pause, MegaRequestListenerInterface listener) {
        megaApi.pauseTransferByTag(transferTag, pause, createDelegateRequestListener(listener));
    }

    /**
     * Returns the state (paused/unpaused) of transfers
     *
     * @param direction Direction of transfers to check
     *                  Valid values for this parameter are:
     *                  - MegaTransfer::TYPE_DOWNLOAD = 0
     *                  - MegaTransfer::TYPE_UPLOAD = 1
     * @return true if transfers on that direction are paused, false otherwise
     */
    public boolean areTransfersPaused(int direction) {
        return megaApi.areTransfersPaused(direction);
    }

    /**
     * Set the upload speed limit
     * <p>
     * The limit will be applied on the server side when starting a transfer. Thus the limit won't be
     * applied for already started uploads and it's applied per storage server.
     *
     * @param bpslimit -1 to automatically select the limit, 0 for no limit, otherwise the speed limit
     *                 in bytes per second
     */
    public void setUploadLimit(int bpslimit) {
        megaApi.setUploadLimit(bpslimit);
    }

    /**
     * Set the transfer method for downloads
     * <p>
     * Valid methods are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     * <p>
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     * <p>
     * - TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
     * The SDK selects the transfer method automatically starting with alternative port 8080.
     *
     * @param method Selected transfer method for downloads
     */
    public void setDownloadMethod(int method) {
        megaApi.setDownloadMethod(method);
    }

    /**
     * Set the transfer method for uploads
     * <p>
     * Valid methods are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     * <p>
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     * <p>
     * - TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
     * The SDK selects the transfer method automatically starting with alternative port 8080.
     *
     * @param method Selected transfer method for uploads
     */
    public void setUploadMethod(int method) {
        megaApi.setUploadMethod(method);
    }


    /**
     * Get the maximum download speed in bytes per second
     * <p>
     * The value 0 means unlimited speed
     *
     * @return Download speed in bytes per second
     */
    public int getMaxDownloadSpeed() {
        return megaApi.getMaxDownloadSpeed();
    }

    /**
     * Get the maximum upload speed in bytes per second
     * <p>
     * The value 0 means unlimited speed
     *
     * @return Upload speed in bytes per second
     */
    public int getMaxUploadSpeed() {
        return megaApi.getMaxUploadSpeed();
    }

    /**
     * Return the current download speed
     *
     * @return Download speed in bytes per second
     */
    public int getCurrentDownloadSpeed() {
        return megaApi.getCurrentDownloadSpeed();
    }

    /**
     * Return the current download speed
     *
     * @return Download speed in bytes per second
     */
    public int getCurrentUploadSpeed() {
        return megaApi.getCurrentUploadSpeed();
    }

    /**
     * Return the current transfer speed
     *
     * @param type Type of transfer to get the speed.
     *             Valid values are MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD
     * @return Transfer speed for the transfer type, or 0 if the parameter is invalid
     */
    public int getCurrentSpeed(int type) {
        return megaApi.getCurrentSpeed(type);
    }

    /**
     * Get the active transfer method for downloads
     * <p>
     * Valid values for the return parameter are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     * <p>
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     * <p>
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
     * <p>
     * Valid values for the return parameter are:
     * - TRANSFER_METHOD_NORMAL = 0
     * HTTP transfers using port 80. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_ALTERNATIVE_PORT = 1
     * HTTP transfers using port 8080. Data is already encrypted.
     * <p>
     * - TRANSFER_METHOD_AUTO = 2
     * The SDK selects the transfer method automatically
     * <p>
     * - TRANSFER_METHOD_AUTO_NORMAL = 3
     * The SDK selects the transfer method automatically starting with port 80.
     * <p>
     * - TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
     * The SDK selects the transfer method automatically starting with alternative port 8080.
     *
     * @return Active transfer method for uploads
     */
    public int getUploadMethod() {
        return megaApi.getUploadMethod();
    }

    /**
     * Get information about transfer queues
     *
     * @param listener MegaTransferListener to start receiving information about transfers
     * @return Information about transfer queues
     */
    @Nullable
    public MegaTransferData getTransferData(MegaTransferListenerInterface listener) {
        return megaApi.getTransferData(createDelegateTransferListener(listener, false));
    }

    /**
     * Get the first transfer in a transfer queue
     * <p>
     * You take the ownership of the returned value.
     *
     * @param type Transfer queue to get the first transfer (MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD)
     * @return MegaTransfer object related to the first transfer in the queue or NULL if there isn't any transfer
     */
    public MegaTransfer getFirstTransfer(int type) {
        return megaApi.getFirstTransfer(type);
    }

    /**
     * Force an onTransferUpdate callback for the specified transfer
     * <p>
     * The callback will be received by transfer listeners registered to receive all
     * callbacks related to callbacks and additionally by the listener in the last
     * parameter of this function, if it's not NULL.
     *
     * @param transfer Transfer that will be provided in the onTransferUpdate callback
     * @param listener Listener that will receive the callback
     */
    public void notifyTransfer(MegaTransfer transfer, MegaTransferListenerInterface listener) {
        megaApi.notifyTransfer(transfer, createDelegateTransferListener(listener));
    }

    /**
     * Force an onTransferUpdate callback for the specified transfer
     * <p>
     * The callback will be received by transfer listeners registered to receive all
     * callbacks related to callbacks and additionally by the listener in the last
     * parameter of this function, if it's not NULL.
     *
     * @param transferTag Tag of the transfer that will be provided in the onTransferUpdate callback
     * @param listener    Listener that will receive the callback
     */
    public void notifyTransferByTag(int transferTag, MegaTransferListenerInterface listener) {
        megaApi.notifyTransferByTag(transferTag, createDelegateTransferListener(listener));
    }

    /**
     * Get all active transfers
     * <p>
     * You take the ownership of the returned value
     *
     * @return List with all active transfers
     * @see MegaApi::startUpload, MegaApi::startDownload
     */
    @Nullable
    public ArrayList<MegaTransfer> getTransfers() {
        return transferListToArray(megaApi.getTransfers());
    }

    /**
     * Get the transfer with a transfer tag
     * <p>
     * That tag can be got using MegaTransfer::getTag
     * <p>
     * You take the ownership of the returned value
     *
     * @param transferTag tag to check
     * @return MegaTransfer object with that tag, or NULL if there isn't any
     * active transfer with it
     */
    @Nullable
    public MegaTransfer getTransferByTag(int transferTag) {
        return megaApi.getTransferByTag(transferTag);
    }

    /**
     * Get all transfers of a specific type (downloads or uploads)
     * <p>
     * If the parameter isn't MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD
     * this function returns an empty list.
     * <p>
     * You take the ownership of the returned value
     *
     * @param type MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD
     * @return List with transfers of the desired type
     */
    @Nullable
    public ArrayList<MegaTransfer> getTransfers(int type) {
        return transferListToArray(megaApi.getTransfers(type));
    }

    /**
     * Get a list of transfers that belong to a folder transfer
     * <p>
     * This function provides the list of transfers started in the context
     * of a folder transfer.
     * <p>
     * If the tag in the parameter doesn't belong to a folder transfer,
     * this function returns an empty list.
     * <p>
     * The transfers provided by this function are the ones that are added to the
     * transfer queue when this function is called. Finished transfers, or transfers
     * not added to the transfer queue yet (for example, uploads that are waiting for
     * the creation of the parent folder in MEGA) are not returned by this function.
     * <p>
     * You take the ownership of the returned value
     *
     * @param transferTag Tag of the folder transfer to check
     * @return List of transfers in the context of the selected folder transfer
     * @see MegaTransfer::isFolderTransfer, MegaTransfer::getFolderTransferTag
     */
    @Nullable
    public ArrayList<MegaTransfer> getChildTransfers(int transferTag) {
        return transferListToArray(megaApi.getChildTransfers(transferTag));
    }

    /**
     * Check if the SDK is waiting to complete a request and get the reason
     *
     * @return State of SDK.
     * <p>
     * Valid values are:
     * - MegaApi::RETRY_NONE = 0
     * SDK is not waiting for the server to complete a request
     * <p>
     * - MegaApi::RETRY_CONNECTIVITY = 1
     * SDK is waiting for the server to complete a request due to connectivity issues
     * <p>
     * - MegaApi::RETRY_SERVERS_BUSY = 2
     * SDK is waiting for the server to complete a request due to a HTTP error 500
     * <p>
     * - MegaApi::RETRY_API_LOCK = 3
     * SDK is waiting for the server to complete a request due to an API lock (API error -3)
     * <p>
     * - MegaApi::RETRY_RATE_LIMIT = 4,
     * SDK is waiting for the server to complete a request due to a rate limit (API error -4)
     * <p>
     * - MegaApi::RETRY_LOCAL_LOCK = 5
     * SDK is waiting for a local locked file
     * <p>
     * - MegaApi::RETRY_UNKNOWN = 6
     * SDK is waiting for the server to complete a request with unknown reason
     */
    public int isWaiting() {
        return megaApi.isWaiting();
    }

    /**
     * Get the number of pending uploads
     *
     * @return Pending uploads
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public int getNumPendingUploads() {
        return megaApi.getNumPendingUploads();
    }

    /**
     * Get the number of pending downloads
     *
     * @return Pending downloads
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public int getNumPendingDownloads() {
        return megaApi.getNumPendingDownloads();
    }

    /**
     * Get the number of queued uploads since the last call to MegaApi::resetTotalUploads
     *
     * @return Number of queued uploads since the last call to MegaApi::resetTotalUploads
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public int getTotalUploads() {
        return megaApi.getTotalUploads();
    }

    /**
     * Get the number of queued uploads since the last call to MegaApi::resetTotalDownloads
     *
     * @return Number of queued uploads since the last call to MegaApi::resetTotalDownloads
     * @deprecated Function related to statistics will be reviewed in future updates. They
     * could change or be removed in the current form.
     */
    @Deprecated
    public int getTotalDownloads() {
        return megaApi.getTotalDownloads();
    }

    /**
     * Reset the number of total downloads
     * This function resets the number returned by MegaApi::getTotalDownloads
     *
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public void resetTotalDownloads() {
        megaApi.resetTotalDownloads();
    }

    /**
     * Reset the number of total uploads
     * This function resets the number returned by MegaApi::getTotalUploads
     *
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public void resetTotalUploads() {
        megaApi.resetTotalUploads();
    }

    /**
     * Get the total downloaded bytes
     * <p>
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalDownloads
     * or just before a log in or a log out.
     * <p>
     * Only regular downloads are taken into account, not streaming nor folder transfers.
     *
     * @return Total downloaded bytes
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public long getTotalDownloadedBytes() {
        return megaApi.getTotalDownloadedBytes();
    }

    /**
     * Get the total uploaded bytes
     * <p>
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalUploads
     * or just before a log in or a log out.
     * <p>
     * Only regular uploads are taken into account, not folder transfers.
     *
     * @return Total uploaded bytes
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public long getTotalUploadedBytes() {
        return megaApi.getTotalUploadedBytes();
    }

    /**
     * Get the total bytes of started downloads
     * <p>
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalDownloads
     * or just before a log in or a log out.
     * <p>
     * Only regular downloads are taken into account, not streaming nor folder transfers.
     *
     * @return Total bytes of started downloads
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public long getTotalDownloadBytes() {
        return megaApi.getTotalDownloadBytes();
    }

    /**
     * Get the total bytes of started uploads
     * <p>
     * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalUploads
     * or just before a log in or a log out.
     * <p>
     * Only regular uploads are taken into account, not folder transfers.
     *
     * @return Total bytes of started uploads
     * @deprecated Function related to statistics will be reviewed in future updates to
     * provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated
    public long getTotalUploadBytes() {
        return megaApi.getTotalUploadBytes();
    }

    /**
     * Get the total number of nodes in the account
     *
     * @return Total number of nodes in the account
     */
    public BigInteger getNumNodes() {
        return megaApi.getNumNodes();
    }

    /**
     * Starts an unbuffered download of a node (file) from the user's MEGA account.
     *
     * @param node         The MEGA node to download.
     * @param startOffset  long. The byte to start from.
     * @param size         long. Size of the download.
     * @param outputStream The output stream object to use for this download.
     * @param listener     MegaRequestListener to track this request.
     */
    public void startUnbufferedDownload(MegaNode node, long startOffset, long size, OutputStream outputStream, MegaTransferListenerInterface listener) {
        DelegateMegaTransferListener delegateListener = new DelegateOutputMegaTransferListener(this, outputStream, listener, true);
        activeTransferListeners.add(delegateListener);
        megaApi.startStreaming(node, startOffset, size, delegateListener);
    }

    /**
     * Starts an unbuffered download of a node (file) from the user's MEGA account.
     *
     * @param node         The MEGA node to download.
     * @param outputStream The output stream object to use for this download.
     * @param listener     MegaRequestListener to track this request.
     */
    public void startUnbufferedDownload(MegaNode node, OutputStream outputStream, MegaTransferListenerInterface listener) {
        startUnbufferedDownload(node, 0, node.getSize(), outputStream, listener);
    }

    //****************************************************************************************************/
    // FILESYSTEM METHODS
    //****************************************************************************************************/

    /**
     * Get the number of child nodes
     * <p>
     * If the node doesn't exist in MEGA or isn't a folder,
     * this function returns 0
     * <p>
     * This function doesn't search recursively, only returns the direct child nodes.
     *
     * @param parent Parent node
     * @return Number of child nodes
     */
    public int getNumChildren(MegaNode parent) {
        return megaApi.getNumChildren(parent);
    }

    /**
     * Get the number of child files of a node
     * <p>
     * If the node doesn't exist in MEGA or isn't a folder,
     * this function returns 0
     * <p>
     * This function doesn't search recursively, only returns the direct child files.
     *
     * @param parent Parent node
     * @return Number of child files
     */
    public int getNumChildFiles(MegaNode parent) {
        return megaApi.getNumChildFiles(parent);
    }

    /**
     * Get the number of child folders of a node
     * <p>
     * If the node doesn't exist in MEGA or isn't a folder,
     * this function returns 0
     * <p>
     * This function doesn't search recursively, only returns the direct child folders.
     *
     * @param parent Parent node
     * @return Number of child folders
     */
    public int getNumChildFolders(MegaNode parent) {
        return megaApi.getNumChildFolders(parent);
    }

    /**
     * Get children of a particular parent or a predefined location, and allow filtering
     * the results. @see MegaSearchFilter
     * The look-up is case-insensitive.
     * For invalid filtering options, this function returns an empty list.
     *
     * You take the ownership of the returned value
     *
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true).
     *
     * @param filter Container for filtering options. In order to be considered valid it must
     * - be not null
     * - have valid ancestor handle (different than INVALID_HANDLE) set by calling byLocationHandle(),
     *   and in consequence it must have default value for location (SEARCH_TARGET_ALL)
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
     * - MegaApi::ORDER_LABEL_ASC = 17
     * Sort by color label, ascending. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_LABEL_DESC = 18
     * Sort by color label, descending. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_FAV_ASC = 19
     * Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_FAV_DESC = 20
     * Sort nodes with favourite attr last. With this order, folders are returned first, then files
     *
     * @param cancelToken MegaCancelToken to be able to cancel the processing at any time.
     * @param searchPage Container for pagination options; if null, all results will be returned
     *
     * @return List with found children as MegaNode objects
     */
    public ArrayList<MegaNode> getChildren(MegaSearchFilter filter, int order, MegaCancelToken cancelToken, MegaSearchPage searchPage) {
        return nodeListToArray(megaApi.getChildren(filter, order, cancelToken, searchPage));
    }

    /**
     * Get all children of a MegaNode
     * <p>
     * If the parent node doesn't exist or it isn't a folder, this function
     * returns an empty list
     * <p>
     * You take the ownership of the returned value
     *
     * @param parent Parent node
     * @param order  Order for the returned list
     *               Valid values for this parameter are:
     *               - MegaApi::ORDER_NONE = 0
     *               Undefined order
     *               <p>
     *               - MegaApi::ORDER_DEFAULT_ASC = 1
     *               Folders first in alphabetical order, then files in the same order
     *               <p>
     *               - MegaApi::ORDER_DEFAULT_DESC = 2
     *               Files first in reverse alphabetical order, then folders in the same order
     *               <p>
     *               - MegaApi::ORDER_SIZE_ASC = 3
     *               Sort by size, ascending
     *               <p>
     *               - MegaApi::ORDER_SIZE_DESC = 4
     *               Sort by size, descending
     *               <p>
     *               - MegaApi::ORDER_CREATION_ASC = 5
     *               Sort by creation time in MEGA, ascending
     *               <p>
     *               - MegaApi::ORDER_CREATION_DESC = 6
     *               Sort by creation time in MEGA, descending
     *               <p>
     *               - MegaApi::ORDER_MODIFICATION_ASC = 7
     *               Sort by modification time of the original file, ascending
     *               <p>
     *               - MegaApi::ORDER_MODIFICATION_DESC = 8
     *               Sort by modification time of the original file, descending
     *               <p>
     *               - MegaApi::ORDER_LABEL_ASC = 17
     *               Sort by color label, ascending. With this order, folders are returned first, then files
     *               <p>
     *               - MegaApi::ORDER_LABEL_DESC = 18
     *               Sort by color label, descending. With this order, folders are returned first, then files
     *               <p>
     *               - MegaApi::ORDER_FAV_ASC = 19
     *               Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *               <p>
     *               - MegaApi::ORDER_FAV_DESC = 20
     *               Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List with all child MegaNode objects
     */
    public ArrayList<MegaNode> getChildren(MegaNode parent, int order) {
        return nodeListToArray(megaApi.getChildren(parent, order));
    }


    /**
     * Get all children of a list of MegaNodes
     * <p>
     * If any parent node doesn't exist or it isn't a folder, that parent
     * will be skipped.
     * <p>
     * You take the ownership of the returned value
     *
     * @param parentNodes List of parent nodes
     * @param order       Order for the returned list
     *                    Valid values for this parameter are:
     *                    - MegaApi::ORDER_NONE = 0
     *                    Undefined order
     *                    <p>
     *                    - MegaApi::ORDER_DEFAULT_ASC = 1
     *                    Folders first in alphabetical order, then files in the same order
     *                    <p>
     *                    - MegaApi::ORDER_DEFAULT_DESC = 2
     *                    Files first in reverse alphabetical order, then folders in the same order
     *                    <p>
     *                    - MegaApi::ORDER_SIZE_ASC = 3
     *                    Sort by size, ascending
     *                    <p>
     *                    - MegaApi::ORDER_SIZE_DESC = 4
     *                    Sort by size, descending
     *                    <p>
     *                    - MegaApi::ORDER_CREATION_ASC = 5
     *                    Sort by creation time in MEGA, ascending
     *                    <p>
     *                    - MegaApi::ORDER_CREATION_DESC = 6
     *                    Sort by creation time in MEGA, descending
     *                    <p>
     *                    - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                    Sort by modification time of the original file, ascending
     *                    <p>
     *                    - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                    Sort by modification time of the original file, descending
     *                    <p>
     *                    - MegaApi::ORDER_LABEL_ASC = 17
     *                    Sort by color label, ascending. With this order, folders are returned first, then files
     *                    <p>
     *                    - MegaApi::ORDER_LABEL_DESC = 18
     *                    Sort by color label, descending. With this order, folders are returned first, then files
     *                    <p>
     *                    - MegaApi::ORDER_FAV_ASC = 19
     *                    Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                    <p>
     *                    - MegaApi::ORDER_FAV_DESC = 20
     *                    Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List with all child MegaNode objects
     */
    public ArrayList<MegaNode> getChildren(MegaNodeList parentNodes, int order) {
        return nodeListToArray(megaApi.getChildren(parentNodes, order));
    }

    /**
     * Get all children of a MegaNode
     * <p>
     * If the parent node doesn't exist or it isn't a folder, this function
     * returns an empty list
     * <p>
     * You take the ownership of the returned value
     *
     * @param parent Parent node
     * @return List with all child MegaNode objects
     */
    public ArrayList<MegaNode> getChildren(MegaNode parent) {
        return nodeListToArray(megaApi.getChildren(parent));
    }

    /**
     * Get all versions of a file
     *
     * @param node Node to check
     * @return List with all versions of the node, including the current version
     */
    public ArrayList<MegaNode> getVersions(MegaNode node) {
        return nodeListToArray(megaApi.getVersions(node));
    }

    /**
     * Get the number of versions of a file
     *
     * @param node Node to check
     * @return Number of versions of the node, including the current version
     */
    public int getNumVersions(MegaNode node) {
        return megaApi.getNumVersions(node);
    }

    /**
     * Check if a file has previous versions
     *
     * @param node Node to check
     * @return true if the node has any previous version
     */
    public boolean hasVersions(MegaNode node) {
        return megaApi.hasVersions(node);
    }

    /**
     * Get information about the contents of a folder
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_FOLDER_INFO
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaFolderInfo - MegaFolderInfo object with the information related to the folder
     *
     * @param node     Folder node to inspect
     * @param listener MegaRequestListener to track this request
     */
    public void getFolderInfo(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.getFolderInfo(node, createDelegateRequestListener(listener));
    }

    /**
     * Returns true if the node has children
     *
     * @return true if the node has children
     */
    public boolean hasChildren(MegaNode parent) {
        return megaApi.hasChildren(parent);
    }

    /**
     * Get the child node with the provided name
     * <p>
     * If the node doesn't exist, this function returns NULL
     * <p>
     * You take the ownership of the returned value
     *
     * @param parent Parent node
     * @param name   Name of the node
     * @return The MegaNode that has the selected parent and name
     */
    @Nullable
    public MegaNode getChildNode(MegaNode parent, String name) {
        return megaApi.getChildNode(parent, name);
    }

    /**
     * Get the parent node of a MegaNode
     * <p>
     * If the node doesn't exist in the account or
     * it is a root node, this function returns NULL
     * <p>
     * You take the ownership of the returned value.
     *
     * @param node MegaNode to get the parent
     * @return The parent of the provided node
     */
    @Nullable
    public MegaNode getParentNode(MegaNode node) {
        return megaApi.getParentNode(node);
    }

    /**
     * Get the path of a MegaNode
     * <p>
     * If the node doesn't exist, this function returns NULL.
     * You can recover the node later using MegaApi::getNodeByPath
     * except if the path contains names with '/', '\' or ':' characters.
     * <p>
     * You take the ownership of the returned value
     *
     * @param node MegaNode for which the path will be returned
     * @return The path of the node
     */
    @Nullable
    public String getNodePath(MegaNode node) {
        return megaApi.getNodePath(node);
    }

    /**
     * Get the MegaNode in a specific path in the MEGA account
     * <p>
     * The path separator character is '/'
     * The Root node is /
     * The Inbox root node is //in/
     * The Rubbish root node is //bin/
     * <p>
     * Paths with names containing '/', '\' or ':' aren't compatible
     * with this function.
     * <p>
     * It is needed to be logged in and to have successfully completed a fetchNodes
     * request before calling this function. Otherwise, it will return NULL.
     * <p>
     * You take the ownership of the returned value
     *
     * @param path Path to check
     * @param n    Base node if the path is relative
     * @return The MegaNode object in the path, otherwise NULL
     */
    @Nullable
    public MegaNode getNodeByPath(String path, MegaNode n) {
        return megaApi.getNodeByPath(path, n);
    }

    /**
     * Get the MegaNode in a specific path in the MEGA account
     * <p>
     * The path separator character is '/'
     * The Root node is /
     * The Inbox root node is //in/
     * The Rubbish root node is //bin/
     * <p>
     * Paths with names containing '/', '\' or ':' aren't compatible
     * with this function.
     * <p>
     * It is needed to be logged in and to have successfully completed a fetchNodes
     * request before calling this function. Otherwise, it will return NULL.
     * <p>
     * You take the ownership of the returned value
     *
     * @param path Path to check
     * @return The MegaNode object in the path, otherwise NULL
     */
    @Nullable
    public MegaNode getNodeByPath(String path) {
        return megaApi.getNodeByPath(path);
    }

    /**
     * Get the MegaNode that has a specific handle
     * <p>
     * You can get the handle of a MegaNode using MegaNode::getHandle. The same handle
     * can be got in a Base64-encoded string using MegaNode::getBase64Handle. Conversions
     * between these formats can be done using MegaApi::base64ToHandle and MegaApi::handleToBase64
     * <p>
     * It is needed to be logged in and to have successfully completed a fetchNodes
     * request before calling this function. Otherwise, it will return NULL.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param h Node handle to check
     * @return MegaNode object with the handle, otherwise NULL
     */
    @Nullable
    public MegaNode getNodeByHandle(long h) {
        return megaApi.getNodeByHandle(h);
    }

    /**
     * Get the MegaContactRequest that has a specific handle
     * <p>
     * You can get the handle of a MegaContactRequest using MegaContactRequest::getHandle.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param handle Contact request handle to check
     * @return MegaContactRequest object with the handle, otherwise NULL
     */
    @Nullable
    public MegaContactRequest getContactRequestByHandle(long handle) {
        return megaApi.getContactRequestByHandle(handle);
    }

    /**
     * Get all contacts of this MEGA account
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaUser object with all contacts of this account
     */
    public ArrayList<MegaUser> getContacts() {
        return userListToArray(megaApi.getContacts());
    }

    /**
     * Get the MegaUser that has a specific email address
     * <p>
     * You can get the email of a MegaUser using MegaUser::getEmail
     * <p>
     * You take the ownership of the returned value
     *
     * @param user Email or Base64 handle of the user
     * @return MegaUser that has the email address, otherwise NULL
     */
    @Nullable
    public MegaUser getContact(String user) {
        return megaApi.getContact(user);
    }

    /**
     * Get all MegaUserAlerts for the logged in user
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaUserAlert objects
     */
    public ArrayList<MegaUserAlert> getUserAlerts() {
        return userAlertListToArray(megaApi.getUserAlerts());
    }

    /**
     * Get the number of unread user alerts for the logged in user
     *
     * @return Number of unread user alerts
     */
    public int getNumUnreadUserAlerts() {
        return megaApi.getNumUnreadUserAlerts();
    }

    /**
     * Get a list with all inbound sharings from one MegaUser
     * <p>
     * You take the ownership of the returned value
     *
     * @param user MegaUser sharing folders with this account
     * @return List of MegaNode objects that this user is sharing with this account
     */
    public ArrayList<MegaNode> getInShares(MegaUser user) {
        return nodeListToArray(megaApi.getInShares(user));
    }

    /**
     * Get a list with all inbound sharings from one MegaUser
     * <p>
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     * <p>
     * You take the ownership of the returned value
     *
     * @param user  MegaUser sharing folders with this account
     * @param order Sorting order to use
     * @return List of MegaNode objects that this user is sharing with this account
     */
    public ArrayList<MegaNode> getInShares(MegaUser user, int order) {
        return nodeListToArray(megaApi.getInShares(user, order));
    }

    /**
     * Get a list with all inbound sharings
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaNode objects that other users are sharing with this account
     */
    public ArrayList<MegaNode> getInShares() {
        return nodeListToArray(megaApi.getInShares());
    }

    /**
     * Get a list with all inbound sharings
     * <p>
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     * <p>
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaNode objects that other users are sharing with this account
     */
    public ArrayList<MegaNode> getInShares(int order) {
        return nodeListToArray(megaApi.getInShares(order));
    }

    /**
     * Get a list with all active inbound sharings
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaShare objects that other users are sharing with this account
     */
    public ArrayList<MegaShare> getInSharesList() {
        return shareListToArray(megaApi.getInSharesList());
    }

    /**
     * Get a list with all active inbound sharings
     * <p>
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     * <p>
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaShare objects that other users are sharing with this account
     */
    public ArrayList<MegaShare> getInSharesList(int order) {
        return shareListToArray(megaApi.getInSharesList(order));
    }

    /**
     * Get a list with all unverified inbound sharings
     * <p>
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaShare objects that other users are sharing with this account
     */
    public ArrayList<MegaShare> getUnverifiedIncomingShares(int order) {
        return shareListToArray(megaApi.getUnverifiedInShares(order));
    }

    /**
     * Get the user relative to an incoming share
     * <p>
     * This function will return NULL if the node is not found
     * <p>
     * When recurse is true and the root of the specified node is not an incoming share,
     * this function will return NULL.
     * When recurse is false and the specified node doesn't represent the root of an
     * incoming share, this function will return NULL.
     * <p>
     * You take the ownership of the returned value
     *
     * @param node Node to look for inshare user.
     * @return MegaUser relative to the incoming share
     */
    @Nullable
    public MegaUser getUserFromInShare(MegaNode node) {
        return megaApi.getUserFromInShare(node);
    }

    /**
     * Get the user relative to an incoming share
     * <p>
     * This function will return NULL if the node is not found
     * <p>
     * When recurse is true and the root of the specified node is not an incoming share,
     * this function will return NULL.
     * When recurse is false and the specified node doesn't represent the root of an
     * incoming share, this function will return NULL.
     * <p>
     * You take the ownership of the returned value
     *
     * @param node    Node to look for inshare user.
     * @param recurse use root node corresponding to the node passed
     * @return MegaUser relative to the incoming share
     */
    @Nullable
    public MegaUser getUserFromInShare(MegaNode node, boolean recurse) {
        return megaApi.getUserFromInShare(node, recurse);
    }

    /**
     * Check if a MegaNode is pending to be shared with another User. This situation
     * happens when a node is to be shared with a User which is not a contact yet.
     * <p>
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
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaShare objects
     */
    public ArrayList<MegaShare> getOutShares() {
        return shareListToArray(megaApi.getOutShares());
    }

    /**
     * Get a list with all active and pending outbound sharings
     * <p>
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC
     * <p>
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaShare objects
     */
    public ArrayList<MegaShare> getOutShares(int order) {
        return shareListToArray(megaApi.getOutShares(order));
    }

    /**
     * Get a list with the active and pending outbound sharings for a MegaNode
     * <p>
     * If the node doesn't exist in the account, this function returns an empty list.
     * <p>
     * You take the ownership of the returned value
     *
     * @param node MegaNode to check
     * @return List of MegaShare objects
     */
    public ArrayList<MegaShare> getOutShares(MegaNode node) {
        return shareListToArray(megaApi.getOutShares(node));
    }

    /**
     * Get a list with all unverified sharings
     * <p>
     * You take the ownership of the returned value
     *
     * @param order Sorting order to use
     * @return List of MegaShare objects
     */
    public ArrayList<MegaShare> getUnverifiedOutgoingShares(int order) {
        return shareListToArray(megaApi.getUnverifiedOutShares(order));
    }

    /**
     * Check if a node belongs to your own cloud
     *
     * @param handle Node to check
     * @return True if it belongs to your own cloud
     */
    public boolean isPrivateNode(long handle) {
        return megaApi.isPrivateNode(handle);
    }

    /**
     * Check if a node does NOT belong to your own cloud
     * <p>
     * In example, nodes from incoming shared folders do not belong to your cloud.
     *
     * @param handle Node to check
     * @return True if it does NOT belong to your own cloud
     */
    public boolean isForeignNode(long handle) {
        return megaApi.isForeignNode(handle);
    }

    /**
     * Get a list with all public links
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaNode objects that are shared with everyone via public link
     */
    public ArrayList<MegaNode> getPublicLinks() {
        return nodeListToArray(megaApi.getPublicLinks());
    }

    /**
     * Get a list with all public links
     * <p>
     * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
     * MegaApi::ORDER_DEFAULT_DESC, MegaApi::ORDER_LINK_CREATION_ASC,
     * MegaApi::ORDER_LINK_CREATION_DESC
     * <p>
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
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaContactRequest objects.
     */
    public ArrayList<MegaContactRequest> getIncomingContactRequests() {
        return contactRequestListToArray(megaApi.getIncomingContactRequests());
    }

    /**
     * Get a list with all outgoing contact requests.
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of MegaContactRequest objects.
     */
    public ArrayList<MegaContactRequest> getOutgoingContactRequests() {
        return contactRequestListToArray(megaApi.getOutgoingContactRequests());
    }

    /**
     * Get the access level of a MegaNode
     *
     * @param node MegaNode to check
     * @return Access level of the node
     * Valid values are:
     * - MegaShare::ACCESS_OWNER
     * - MegaShare::ACCESS_FULL
     * - MegaShare::ACCESS_READWRITE
     * - MegaShare::ACCESS_READ
     * - MegaShare::ACCESS_UNKNOWN
     */
    public int getAccess(MegaNode node) {
        return megaApi.getAccess(node);
    }

    /**
     * Get the size of a node tree
     * <p>
     * If the MegaNode is a file, this function returns the size of the file.
     * If it's a folder, this function returns the sum of the sizes of all nodes
     * in the node tree.
     *
     * @param node Parent node
     * @return Size of the node tree
     */
    public long getSize(MegaNode node) {
        return megaApi.getSize(node);
    }

    /**
     * Get a Base64-encoded fingerprint for a local file
     * <p>
     * The fingerprint is created taking into account the modification time of the file
     * and file contents. This fingerprint can be used to get a corresponding node in MEGA
     * using MegaApi::getNodeByFingerprint
     * <p>
     * If the file can't be found or can't be opened, this function returns NULL
     * <p>
     * You take the ownership of the returned value
     *
     * @param filePath Local file path
     * @return Base64-encoded fingerprint for the file
     */
    @Nullable
    public String getFingerprint(String filePath) {
        return megaApi.getFingerprint(filePath);
    }

    /**
     * Returns a node with the provided fingerprint
     * <p>
     * If there isn't any node in the account with that fingerprint, this function returns NULL.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param fingerprint Fingerprint to check
     * @return MegaNode object with the provided fingerprint
     */
    @Nullable
    public MegaNode getNodeByFingerprint(String fingerprint) {
        return megaApi.getNodeByFingerprint(fingerprint);
    }

    /**
     * Returns a node with the provided fingerprint
     * <p>
     * If there isn't any node in the account with that fingerprint, this function returns NULL.
     * If there are several nodes with the same fingerprint, nodes in the preferred
     * parent folder take precedence.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param fingerprint Fingerprint to check
     * @param parent      Preferred parent node
     * @return MegaNode object with the provided fingerprint
     */
    @Nullable
    public MegaNode getNodeByFingerprint(String fingerprint, MegaNode parent) {
        return megaApi.getNodeByFingerprint(fingerprint, parent);
    }

    /**
     * Returns all nodes that have a fingerprint
     * <p>
     * If there isn't any node in the account with that fingerprint, this function returns an empty MegaNodeList.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param fingerprint Fingerprint to check
     * @return List of nodes with the same fingerprint
     */
    public ArrayList<MegaNode> getNodesByFingerprint(String fingerprint) {
        return nodeListToArray(megaApi.getNodesByFingerprint(fingerprint));
    }

    /**
     * Returns a node with the provided fingerprint that can be exported
     * <p>
     * If there isn't any node in the account with that fingerprint, this function returns NULL.
     * If a file name is passed in the second parameter, it's also checked if nodes with a matching
     * fingerprint has that name. If there isn't any matching node, this function returns NULL.
     * This function ignores nodes that are inside the Rubbish Bin because public links to those nodes
     * can't be downloaded.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param fingerprint Fingerprint to check
     * @param name        Name that the node should have (optional)
     * @return Exportable node that meet the requirements
     */
    @Nullable
    public MegaNode getExportableNodeByFingerprint(String fingerprint, String name) {
        return megaApi.getExportableNodeByFingerprint(fingerprint, name);
    }

    /**
     * Returns a node with the provided fingerprint that can be exported
     * <p>
     * If there isn't any node in the account with that fingerprint, this function returns NULL.
     * If there isn't any matching node, this function returns NULL.
     * This function ignores nodes that are inside the Rubbish Bin because public links to those nodes
     * can't be downloaded.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param fingerprint Fingerprint to check
     * @return Exportable node that meet the requirements
     */
    @Nullable
    public MegaNode getExportableNodeByFingerprint(String fingerprint) {
        return megaApi.getExportableNodeByFingerprint(fingerprint);
    }

    /**
     * Check if the account already has a node with the provided fingerprint
     * <p>
     * A fingerprint for a local file can be generated using MegaApi::getFingerprint
     *
     * @param fingerprint Fingerprint to check
     * @return true if the account contains a node with the same fingerprint
     */
    public boolean hasFingerprint(String fingerprint) {
        return megaApi.hasFingerprint(fingerprint);
    }

    /**
     * getCRC Get the CRC of a file
     * <p>
     * The CRC of a file is a hash of its contents.
     * If you need a more reliable method to check files, use fingerprint functions
     * (MegaApi::getFingerprint, MegaApi::getNodeByFingerprint) that also takes into
     * account the size and the modification time of the file to create the fingerprint.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param filePath Local file path
     * @return Base64-encoded CRC of the file
     */
    @Nullable
    public String getCRC(String filePath) {
        return megaApi.getCRC(filePath);
    }

    /**
     * Get the CRC from a fingerprint
     * <p>
     * You take the ownership of the returned value.
     *
     * @param fingerprint fingerprint from which we want to get the CRC
     * @return Base64-encoded CRC from the fingerprint
     */
    @Nullable
    public String getCRCFromFingerprint(String fingerprint) {
        return megaApi.getCRCFromFingerprint(fingerprint);
    }

    /**
     * getCRC Get the CRC of a node
     * <p>
     * The CRC of a node is a hash of its contents.
     * If you need a more reliable method to check files, use fingerprint functions
     * (MegaApi::getFingerprint, MegaApi::getNodeByFingerprint) that also takes into
     * account the size and the modification time of the node to create the fingerprint.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param node Node for which we want to get the CRC
     * @return Base64-encoded CRC of the node
     */
    @Nullable
    public String getCRC(MegaNode node) {
        return megaApi.getCRC(node);
    }

    /**
     * getNodeByCRC Returns a node with the provided CRC
     * <p>
     * If there isn't any node in the selected folder with that CRC, this function returns NULL.
     * If there are several nodes with the same CRC, anyone can be returned.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param crc    CRC to check
     * @param parent Parent node to scan. It must be a folder.
     * @return Node with the selected CRC in the selected folder, or NULL
     * if it's not found.
     */
    @Nullable
    public MegaNode getNodeByCRC(String crc, MegaNode parent) {
        return megaApi.getNodeByCRC(crc, parent);
    }

    /**
     * Check if a node has an access level
     * <p>
     * You take the ownership of the returned value
     *
     * @param node  Node to check
     * @param level Access level to check
     *              Valid values for this parameter are:
     *              - MegaShare::ACCESS_OWNER
     *              - MegaShare::ACCESS_FULL
     *              - MegaShare::ACCESS_READWRITE
     *              - MegaShare::ACCESS_READ
     * @return Pointer to MegaError with the result.
     * Valid values for the error code are:
     * - MegaError::API_OK - The node has the required access level
     * - MegaError::API_EACCESS - The node doesn't have the required access level
     * - MegaError::API_ENOENT - The node doesn't exist in the account
     * - MegaError::API_EARGS - Invalid parameters
     */
    public MegaError checkAccessErrorExtended(MegaNode node, int level) {
        return megaApi.checkAccessErrorExtended(node, level);
    }

    /**
     * Check if a node can be moved to a target node
     * <p>
     * You take the ownership of the returned value
     *
     * @param node   Node to check
     * @param target Target for the move operation
     * @return MegaError object with the result:
     * Valid values for the error code are:
     * - MegaError::API_OK - The node can be moved to the target
     * - MegaError::API_EACCESS - The node can't be moved because of permissions problems
     * - MegaError::API_ECIRCULAR - The node can't be moved because that would create a circular linkage
     * - MegaError::API_ENOENT - The node or the target doesn't exist in the account
     * - MegaError::API_EARGS - Invalid parameters
     */
    public MegaError checkMoveErrorExtended(MegaNode node, MegaNode target) {
        return megaApi.checkMoveErrorExtended(node, target);
    }

    /**
     * Check if the MEGA filesystem is available in the local computer
     * <p>
     * This function returns true after a successful call to MegaApi::fetchNodes,
     * otherwise it returns false
     *
     * @return True if the MEGA filesystem is available
     */
    public boolean isFilesystemAvailable() {
        return megaApi.isFilesystemAvailable();
    }

    /**
     * Returns the root node of the account
     * <p>
     * You take the ownership of the returned value
     * <p>
     * If you haven't successfully called MegaApi::fetchNodes before,
     * this function returns NULL
     *
     * @return Root node of the account
     */
    @Nullable
    public MegaNode getRootNode() {
        return megaApi.getRootNode();
    }

    /**
     * Returns the inbox node of the account.
     * <p>
     * If you haven't successfully called MegaApiJava.fetchNodes() before,
     * this function returns null.
     *
     * @return Inbox node of the account.
     */
    @Nullable
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
    @Nullable
    public MegaNode getRubbishNode() {
        return megaApi.getRubbishNode();
    }

    /**
     * Check if a node is in the Cloud Drive tree
     *
     * @param node Node to check
     * @return True if the node is in the cloud drive
     */
    public boolean isInCloud(MegaNode node) {
        return megaApi.isInCloud(node);
    }

    /**
     * Check if a node is in the Rubbish bin tree
     *
     * @param node Node to check
     * @return True if the node is in the Rubbish bin
     */
    public boolean isInRubbish(MegaNode node) {
        return megaApi.isInRubbish(node);
    }

    /**
     * Check if a node is in the Inbox tree
     *
     * @param node Node to check
     * @return True if the node is in the Inbox
     */
    public boolean isInInbox(MegaNode node) {
        return megaApi.isInInbox(node);
    }

    /**
     * Get the time (in seconds) during which transfers will be stopped due to a bandwidth overquota
     *
     * @return Time (in seconds) during which transfers will be stopped, otherwise 0
     */
    public long getBandwidthOverquotaDelay() {
        return megaApi.getBandwidthOverquotaDelay();
    }

    /**
     * Search nodes and allow filtering the results.
     * The search is case-insensitive.
     *
     * You take the ownership of the returned value.
     *
     * @param filter Container for filtering options, cannot be null
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
     * - MegaApi::ORDER_LABEL_ASC = 17
     * Sort by color label, ascending. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_LABEL_DESC = 18
     * Sort by color label, descending. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_FAV_ASC = 19
     * Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_FAV_DESC = 20
     * Sort nodes with favourite attr last. With this order, folders are returned first, then files
     *
     * @param cancelToken MegaCancelToken to be able to cancel the search at any time.
     *
     * @return List with found nodes as MegaNode objects
     */
    public ArrayList<MegaNode> search(MegaSearchFilter filter, int order, MegaCancelToken cancelToken) {
        return nodeListToArray(megaApi.search(filter, order, cancelToken));
    }

    /**
     * Search nodes and allow filtering the results.
     * The search is case-insensitive.
     *
     * You take the ownership of the returned value.
     *
     * @param filter Container for filtering options, cannot be null
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
     * - MegaApi::ORDER_LABEL_ASC = 17
     * Sort by color label, ascending. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_LABEL_DESC = 18
     * Sort by color label, descending. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_FAV_ASC = 19
     * Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *
     * - MegaApi::ORDER_FAV_DESC = 20
     * Sort nodes with favourite attr last. With this order, folders are returned first, then files
     *
     * @param cancelToken MegaCancelToken to be able to cancel the search at any time.
     * @param searchPage Container for pagination options; if null, all results will be returned
     *
     * @return List with found nodes as MegaNode objects
     */
    public ArrayList<MegaNode> search(MegaSearchFilter filter, int order, MegaCancelToken cancelToken, MegaSearchPage searchPage) {
        return nodeListToArray(megaApi.search(filter, order, cancelToken, searchPage));
    }

    /**
     * Search nodes containing a search string in their name
     * <p>
     * The search is case-insensitive.
     * <p>
     * You take the ownership of the returned value.
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * @param node         The parent node of the tree to explore
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param recursive    True if you want to search recursively in the node tree.
     *                     False if you want to search in the children of the node only
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> search(MegaNode node, String searchString, @NotNull MegaCancelToken cancelToken, boolean recursive, int order) {
        return nodeListToArray(megaApi.search(node, searchString, cancelToken, recursive, order));
    }

    /**
     * Search nodes containing a search string in their name
     * <p>
     * The search is case-insensitive.
     * <p>
     * The search will consider every accessible node for the account:
     * - Cloud drive
     * - Inbox
     * - Rubbish bin
     * - Incoming shares from other users
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> search(String searchString, @NotNull MegaCancelToken cancelToken, int order) {
        return nodeListToArray(megaApi.search(searchString, cancelToken, order));
    }

    /**
     * Search nodes on incoming shares containing a search string in their name
     * <p>
     * The search is case-insensitive.
     * <p>
     * The method will search exclusively on incoming shares
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> searchOnInShares(String searchString, @NotNull MegaCancelToken cancelToken, int order) {
        return nodeListToArray(megaApi.searchOnInShares(searchString, cancelToken, order));
    }

    /**
     * Search nodes on outbound shares containing a search string in their name
     * <p>
     * The search is case-insensitive.
     * <p>
     * The method will search exclusively on outbound shares
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> searchOnOutShares(String searchString, @NotNull MegaCancelToken cancelToken, int order) {
        return nodeListToArray(megaApi.searchOnOutShares(searchString, cancelToken, order));
    }

    /**
     * Search nodes on public links containing a search string in their name
     * <p>
     * The search is case-insensitive.
     * <p>
     * The method will search exclusively on public links
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> searchOnPublicLinks(String searchString, @NotNull MegaCancelToken cancelToken, int order) {
        return nodeListToArray(megaApi.searchOnPublicLinks(searchString, cancelToken, order));
    }

    /**
     * Allow to search nodes with the following options:
     * - Search given a parent node of the tree to explore, or on the contrary search in a
     * specific target (root nodes, inshares, outshares, public links)
     * - Search recursively
     * - Containing a search string in their name
     * - Filter by the type of the node
     * - Order the returned list
     * <p>
     * If node is provided, it will be the parent node of the tree to explore,
     * search string and/or nodeType can be added to search parameters
     * <p>
     * If node and searchString are not provided, and node type is not valid, this method will
     * return an empty list.
     * <p>
     * The search is case-insensitive. If the search string is not provided but type has any value
     * defined at nodefiletype_t (except FILE_TYPE_DEFAULT),
     * this method will return a list that contains nodes of the same type as provided.
     * <p>
     * You take the ownership of the returned value.
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * @param node         The parent node of the tree to explore
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param recursive    True if you want to search recursively in the node tree.
     *                     False if you want to search in the children of the node only
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @param type         Type of nodes requested in the search
     *                     Valid values for this parameter are:
     *                     - MegaApi::FILE_TYPE_DEFAULT = 0  --> all types
     *                     - MegaApi::FILE_TYPE_PHOTO = 1
     *                     - MegaApi::FILE_TYPE_AUDIO = 2
     *                     - MegaApi::FILE_TYPE_VIDEO = 3
     *                     - MegaApi::FILE_TYPE_ALL_DOCS = 11
     * @param target       Target type where this method will search
     *                     Valid values for this parameter are
     *                     - SEARCH_TARGET_INSHARE = 0
     *                     - SEARCH_TARGET_OUTSHARE = 1
     *                     - SEARCH_TARGET_PUBLICLINK = 2
     *                     - SEARCH_TARGET_ROOTNODE = 3
     *                     - SEARCH_TARGET_ALL = 4
     * @return List of nodes that match with the search parameters
     */
    public ArrayList<MegaNode> searchByType(MegaNode node, String searchString,
                                            @NotNull MegaCancelToken cancelToken, boolean recursive, int order, int type, int target) {
        return nodeListToArray(megaApi.searchByType(node, searchString, cancelToken, recursive,
                order, type, target));
    }

    /**
     * Allow to search nodes with the following options:
     * - Search in a specific target (root nodes, inshares, outshares, public links)
     * - Filter by the type of the node
     * - Order the returned list
     * <p>
     * If node type is not valid, this method will return an empty list.
     * <p>
     * The search is case-insensitive. If the type has any value defined at nodefiletype_t
     * (except FILE_TYPE_DEFAULT), this method will return a list
     * that contains nodes of the same type as provided.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param order  Order for the returned list
     *               Valid values for this parameter are:
     *               - MegaApi::ORDER_NONE = 0
     *               Undefined order
     *               <p>
     *               - MegaApi::ORDER_DEFAULT_ASC = 1
     *               Folders first in alphabetical order, then files in the same order
     *               <p>
     *               - MegaApi::ORDER_DEFAULT_DESC = 2
     *               Files first in reverse alphabetical order, then folders in the same order
     *               <p>
     *               - MegaApi::ORDER_SIZE_ASC = 3
     *               Sort by size, ascending
     *               <p>
     *               - MegaApi::ORDER_SIZE_DESC = 4
     *               Sort by size, descending
     *               <p>
     *               - MegaApi::ORDER_CREATION_ASC = 5
     *               Sort by creation time in MEGA, ascending
     *               <p>
     *               - MegaApi::ORDER_CREATION_DESC = 6
     *               Sort by creation time in MEGA, descending
     *               <p>
     *               - MegaApi::ORDER_MODIFICATION_ASC = 7
     *               Sort by modification time of the original file, ascending
     *               <p>
     *               - MegaApi::ORDER_MODIFICATION_DESC = 8
     *               Sort by modification time of the original file, descending
     *               <p>
     *               - MegaApi::ORDER_LABEL_ASC = 17
     *               Sort by color label, ascending. With this order, folders are returned first, then files
     *               <p>
     *               - MegaApi::ORDER_LABEL_DESC = 18
     *               Sort by color label, descending. With this order, folders are returned first, then files
     *               <p>
     *               - MegaApi::ORDER_FAV_ASC = 19
     *               Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *               <p>
     *               - MegaApi::ORDER_FAV_DESC = 20
     *               Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @param type   Type of nodes requested in the search
     *               Valid values for this parameter are:
     *               - MegaApi::FILE_TYPE_DEFAULT = 0  --> all types
     *               - MegaApi::FILE_TYPE_PHOTO = 1
     *               - MegaApi::FILE_TYPE_AUDIO = 2
     *               - MegaApi::FILE_TYPE_VIDEO = 3
     *               - MegaApi::FILE_TYPE_ALL_DOCS = 11
     * @param target Target type where this method will search
     *               Valid values for this parameter are
     *               - SEARCH_TARGET_INSHARE = 0
     *               - SEARCH_TARGET_OUTSHARE = 1
     *               - SEARCH_TARGET_PUBLICLINK = 2
     *               - SEARCH_TARGET_ROOTNODE = 3
     *               - SEARCH_TARGET_ALL = 4
     * @return List of nodes that match with the search parameters
     */
    public ArrayList<MegaNode> searchByType(@NotNull MegaCancelToken cancelToken, int order, int type, int target) {
        return nodeListToArray(megaApi.searchByType(null, null, cancelToken, true,
                order, type, target));
    }

    /**
     * Allow to search nodes with the following options:
     * - Search given a parent node of the tree to explore
     * - Search recursively
     * - Containing a search string in their name
     * - Filter by the type of the node
     * - Order the returned list
     * <p>
     * If node is provided, it will be the parent node of the tree to explore,
     * search string and/or nodeType can be added to search parameters
     * <p>
     * If node and searchString are not provided, and node type is not valid, this method will
     * return an empty list.
     * <p>
     * The search is case-insensitive. If the search string is not provided but type has any value
     * defined at nodefiletype_t (except FILE_TYPE_DEFAULT),
     * this method will return a list that contains nodes of the same type as provided.
     * <p>
     * You take the ownership of the returned value.
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * @param node         The parent node of the tree to explore
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param recursive    True if you want to search recursively in the node tree.
     *                     False if you want to search in the children of the node only
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @param type         Type of nodes requested in the search
     *                     Valid values for this parameter are:
     *                     - MegaApi::FILE_TYPE_DEFAULT = 0  --> all types
     *                     - MegaApi::FILE_TYPE_PHOTO = 1
     *                     - MegaApi::FILE_TYPE_AUDIO = 2
     *                     - MegaApi::FILE_TYPE_VIDEO = 3
     *                     - MegaApi::FILE_TYPE_ALL_DOCS = 11
     * @return List of nodes that match with the search parameters
     */
    public ArrayList<MegaNode> searchByType(MegaNode node, String searchString,
                                            @NotNull MegaCancelToken cancelToken, boolean recursive, int order, int type) {
        return nodeListToArray(megaApi.searchByType(node, searchString, cancelToken, recursive,
                order, type));
    }

    /**
     * Allow to search nodes with the following options:
     * - Search given a parent node of the tree to explore
     * - Search recursively
     * - Containing a search string in their name
     * - Order the returned list
     * <p>
     * If node is provided, it will be the parent node of the tree to explore,
     * search string can be added to search parameters
     * <p>
     * If node and searchString are not provided, this method will
     * return an empty list.
     * <p>
     * You take the ownership of the returned value.
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * @param node         The parent node of the tree to explore
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param recursive    True if you want to search recursively in the node tree.
     *                     False if you want to search in the children of the node only
     * @param order        Order for the returned list
     *                     Valid values for this parameter are:
     *                     - MegaApi::ORDER_NONE = 0
     *                     Undefined order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_ASC = 1
     *                     Folders first in alphabetical order, then files in the same order
     *                     <p>
     *                     - MegaApi::ORDER_DEFAULT_DESC = 2
     *                     Files first in reverse alphabetical order, then folders in the same order
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_ASC = 3
     *                     Sort by size, ascending
     *                     <p>
     *                     - MegaApi::ORDER_SIZE_DESC = 4
     *                     Sort by size, descending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_ASC = 5
     *                     Sort by creation time in MEGA, ascending
     *                     <p>
     *                     - MegaApi::ORDER_CREATION_DESC = 6
     *                     Sort by creation time in MEGA, descending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_ASC = 7
     *                     Sort by modification time of the original file, ascending
     *                     <p>
     *                     - MegaApi::ORDER_MODIFICATION_DESC = 8
     *                     Sort by modification time of the original file, descending
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_ASC = 17
     *                     Sort by color label, ascending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_LABEL_DESC = 18
     *                     Sort by color label, descending. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_ASC = 19
     *                     Sort nodes with favourite attr first. With this order, folders are returned first, then files
     *                     <p>
     *                     - MegaApi::ORDER_FAV_DESC = 20
     *                     Sort nodes with favourite attr last. With this order, folders are returned first, then files
     * @return List of nodes that match with the search parameters
     */
    public ArrayList<MegaNode> searchByType(MegaNode node, String searchString,
                                            @NotNull MegaCancelToken cancelToken, boolean recursive, int order) {
        return nodeListToArray(megaApi.searchByType(node, searchString, cancelToken, recursive,
                order));
    }

    /**
     * Allow to search nodes with the following options:
     * - Search given a parent node of the tree to explore
     * - Search recursively
     * - Containing a search string in their name
     * <p>
     * If node is provided, it will be the parent node of the tree to explore,
     * search string can be added to search parameters
     * <p>
     * If node and searchString are not provided, this method will
     * return an empty list.
     * <p>
     * You take the ownership of the returned value.
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * @param node         The parent node of the tree to explore
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @param recursive    True if you want to search recursively in the node tree.
     *                     False if you want to search in the children of the node only
     * @return List of nodes that match with the search parameters
     */
    public ArrayList<MegaNode> searchByType(MegaNode node, String searchString,
                                            @NotNull MegaCancelToken cancelToken, boolean recursive) {
        return nodeListToArray(megaApi.searchByType(node, searchString, cancelToken, recursive));
    }

    /**
     * Allow to search nodes with the following options:
     * - Search given a parent node of the tree to explore
     * - Containing a search string in their name
     * <p>
     * If node is provided, it will be the parent node of the tree to explore,
     * search string can be added to search parameters
     * <p>
     * If node and searchString are not provided, this method will
     * return an empty list.
     * <p>
     * You take the ownership of the returned value.
     * <p>
     * This function allows to cancel the processing at any time by passing a MegaCancelToken and calling
     * to MegaCancelToken::setCancelFlag(true). If a valid object is passed, it must be kept alive until
     * this method returns.
     *
     * @param node         The parent node of the tree to explore
     * @param searchString Search string. The search is case-insensitive
     * @param cancelToken  MegaCancelToken to be able to cancel the processing at any time.
     * @return List of nodes that match with the search parameters
     */
    public ArrayList<MegaNode> searchByType(MegaNode node, String searchString,
                                            @NotNull MegaCancelToken cancelToken) {
        return nodeListToArray(megaApi.searchByType(node, searchString, cancelToken));
    }

    /**
     * Get a list of buckets, each bucket containing a list of recently added/modified nodes
     * <p>
     * Each bucket contains files that were added/modified in a set, by a single user.
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the number of days since nodes will be considerated
     * - MegaRequest::getParamType - Returns the maximun number of nodes
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_RECENT_ACTIONS
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getRecentsBucket - Returns buckets with a list of recently added/modified nodes
     * <p>
     * The recommended values for the following parameters are to consider
     * interactions during the last 30 days and maximum 500 nodes.
     *
     * @param days     Age of actions since added/modified nodes will be considered (in days)
     * @param maxnodes Maximum amount of nodes to be considered
     * @param listener MegaRequestListener to track this request
     */
    public void getRecentActionsAsync(long days, long maxnodes, MegaRequestListenerInterface listener) {
        megaApi.getRecentActionsAsync(days, maxnodes, createDelegateRequestListener(listener));
    }

    /**
     * Get a list of buckets, each bucket containing a list of recently added/modified nodes
     * <p>
     * Each bucket contains files that were added/modified in a set, by a single user.
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNumber - Returns the number of days since nodes will be considerated
     * - MegaRequest::getParamType - Returns the maximun number of nodes
     * <p>
     * The recommended values for the following parameters are to consider
     * interactions during the last 30 days and maximum 500 nodes.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_RECENT_ACTIONS
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getRecentsBucket - Returns buckets with a list of recently added/modified nodes
     *
     * @param days     Age of actions since added/modified nodes will be considered (in days)
     * @param maxnodes Maximum amount of nodes to be considered
     */
    public void getRecentActionsAsync(long days, long maxnodes) {
        megaApi.getRecentActionsAsync(days, maxnodes);
    }

    /**
     * Process a node tree using a MegaTreeProcessor implementation
     *
     * @param node      The parent node of the tree to explore
     * @param processor MegaTreeProcessor that will receive callbacks for every node in the tree
     * @param recursive True if you want to recursively process the whole node tree.
     *                  False if you want to process the children of the node only
     * @return True if all nodes were processed. False otherwise (the operation can be
     * cancelled by MegaTreeProcessor::processMegaNode())
     */
    public boolean processMegaTree(MegaNode node, MegaTreeProcessorInterface processor, boolean recursive) {
        DelegateMegaTreeProcessor delegateListener = new DelegateMegaTreeProcessor(this, processor);
        activeMegaTreeProcessors.add(delegateListener);
        boolean result = megaApi.processMegaTree(node, delegateListener, recursive);
        activeMegaTreeProcessors.remove(delegateListener);
        return result;
    }

    /**
     * Process a node tree using a MegaTreeProcessor implementation
     *
     * @param node      The parent node of the tree to explore
     * @param processor MegaTreeProcessor that will receive callbacks for every node in the tree
     * @return True if all nodes were processed. False otherwise (the operation can be
     * cancelled by MegaTreeProcessor::processMegaNode())
     */
    public boolean processMegaTree(MegaNode node, MegaTreeProcessorInterface processor) {
        DelegateMegaTreeProcessor delegateListener = new DelegateMegaTreeProcessor(this, processor);
        activeMegaTreeProcessors.add(delegateListener);
        boolean result = megaApi.processMegaTree(node, delegateListener);
        activeMegaTreeProcessors.remove(delegateListener);
        return result;
    }

    /**
     * Returns a MegaNode that can be downloaded with any instance of MegaApi
     * <p>
     * You can use MegaApi::startDownload with the resulting node with any instance
     * of MegaApi, even if it's logged into another account, a public folder, or not
     * logged in.
     * <p>
     * If the first parameter is a public node or an already authorized node, this
     * function returns a copy of the node, because it can be already downloaded
     * with any MegaApi instance.
     * <p>
     * If the node in the first parameter belongs to the account or public folder
     * in which the current MegaApi object is logged in, this function returns an
     * authorized node.
     * <p>
     * If the first parameter is NULL or a node that is not a public node, is not
     * already authorized and doesn't belong to the current MegaApi, this function
     * returns NULL.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param node MegaNode to authorize
     * @return Authorized node, or NULL if the node can't be authorized
     */
    @Nullable
    public MegaNode authorizeNode(MegaNode node) {
        return megaApi.authorizeNode(node);
    }

    /**
     * Returns a MegaNode that can be downloaded/copied with a chat-authorization
     * <p>
     * During preview of chat-links, you need to call this method to authorize the MegaNode
     * from a node-attachment message, so the API allows to access to it. The parameter to
     * authorize the access can be retrieved from MegaChatRoom::getAuthorizationToken when
     * the chatroom in in preview mode.
     * <p>
     * You can use MegaApi::startDownload and/or MegaApi::copyNode with the resulting
     * node with any instance of MegaApi, even if it's logged into another account,
     * a public folder, or not logged in.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param node  MegaNode to authorize
     * @param cauth Authorization token (public handle of the chatroom in B64url encoding)
     * @return Authorized node, or NULL if the node can't be authorized
     */
    @Nullable
    public MegaNode authorizeChatNode(MegaNode node, String cauth) {
        return megaApi.authorizeChatNode(node, cauth);
    }

    /**
     * Get the SDK version
     * <p>
     * The returned string is an statically allocated array.
     * Do not delete it.
     *
     * @return SDK version
     */
    @Nullable
    public String getVersion() {
        return megaApi.getVersion();
    }

    /**
     * Get the User-Agent header used by the SDK
     * <p>
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaApi object is deleted.
     *
     * @return User-Agent used by the SDK
     */
    @Nullable
    public String getUserAgent() {
        return megaApi.getUserAgent();
    }

    /**
     * Change the API URL
     * <p>
     * This function allows to change the API URL.
     * It's only useful for testing or debugging purposes.
     *
     * @param apiURL     New API URL
     * @param disablepkp true to disable public key pinning for this URL
     */
    public void changeApiUrl(String apiURL, boolean disablepkp) {
        megaApi.changeApiUrl(apiURL, disablepkp);
    }

    /**
     * Change the API URL
     * <p>
     * This function allows to change the API URL.
     * It's only useful for testing or debugging purposes.
     *
     * @param apiURL New API URL
     */
    public void changeApiUrl(String apiURL) {
        megaApi.changeApiUrl(apiURL);
    }

    /**
     * Set the language code used by the app
     *
     * @param languageCode Language code used by the app
     * @return True if the language code is known for the SDK, otherwise false
     */
    public boolean setLanguage(String languageCode) {
        return megaApi.setLanguage(languageCode);
    }

    /**
     * Generate an unique ViewID
     * <p>
     * The caller gets the ownership of the object.
     * <p>
     * A ViewID consists of a random generated id, encoded in hexadecimal as 16 characters of a null-terminated string.
     */
    public String generateViewId() {
        return megaApi.generateViewId();
    }

    /**
     * Set the preferred language of the user
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish:
     * - MegaRequest::getText - Return the language code
     * <p>
     * If the language code is unknown for the SDK, the error code will be MegaError::API_ENOENT
     * <p>
     * This attribute is automatically created by the server. Apps only need
     * to set the new value when the user changes the language.
     *
     * @param languageCode Language code to be set
     * @param listener     MegaRequestListener to track this request
     */
    public void setLanguagePreference(String languageCode, MegaRequestListenerInterface listener) {
        megaApi.setLanguagePreference(languageCode, createDelegateRequestListener(listener));
    }

    /**
     * Get the preferred language of the user
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - Return the language code
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getLanguagePreference(MegaRequestListenerInterface listener) {
        megaApi.getLanguagePreference(createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable file versioning
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_DISABLE_VERSIONS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish:
     * - MegaRequest::getText - "1" for disable, "0" for enable
     *
     * @param disable  True to disable file versioning. False to enable it
     * @param listener MegaRequestListener to track this request
     */
    public void setFileVersionsOption(boolean disable, MegaRequestListenerInterface listener) {
        megaApi.setFileVersionsOption(disable, createDelegateRequestListener(listener));
    }

    /**
     * Enable or disable the automatic approval of incoming contact requests using a contact link
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish:
     * - MegaRequest::getText - "0" for disable, "1" for enable
     *
     * @param disable  True to disable the automatic approval of incoming contact requests using a contact link
     * @param listener MegaRequestListener to track this request
     */
    public void setContactLinksOption(boolean disable, MegaRequestListenerInterface listener) {
        megaApi.setContactLinksOption(disable, createDelegateRequestListener(listener));
    }

    /**
     * Check if file versioning is enabled or disabled
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_DISABLE_VERSIONS
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - "1" for disable, "0" for enable
     * - MegaRequest::getFlag - True if disabled, false if enabled
     * <p>
     * If the option has never been set, the error code will be MegaError::API_ENOENT.
     * In that case, file versioning is enabled by default and MegaRequest::getFlag returns false.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getFileVersionsOption(MegaRequestListenerInterface listener) {
        megaApi.getFileVersionsOption(createDelegateRequestListener(listener));
    }

    /**
     * Check if the automatic approval of incoming contact requests using contact links is enabled or disabled
     * <p>
     * If the option has never been set, the error code will be MegaError::API_ENOENT.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getText - "0" for disable, "1" for enable
     * - MegaRequest::getFlag - false if disabled, true if enabled
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getContactLinksOption(MegaRequestListenerInterface listener) {
        megaApi.getContactLinksOption(createDelegateRequestListener(listener));
    }

    /**
     * Keep retrying when public key pinning fails
     * <p>
     * By default, when the check of the MEGA public key fails, it causes an automatic
     * logout. Pass false to this function to disable that automatic logout and
     * keep the SDK retrying the request.
     * <p>
     * Even if the automatic logout is disabled, a request of the type MegaRequest::TYPE_LOGOUT
     * will be automatically created and callbacks (onRequestStart, onRequestFinish) will
     * be sent. However, logout won't be really executed and in onRequestFinish the error code
     * for the request will be MegaError::API_EINCOMPLETE
     *
     * @param enable true to keep retrying failed requests due to a fail checking the MEGA public key
     *               or false to perform an automatic logout in that case
     */
    public void retrySSLerrors(boolean enable) {
        megaApi.retrySSLerrors(enable);
    }

    /**
     * Enable / disable the public key pinning
     * <p>
     * Public key pinning is enabled by default for all sensible communications.
     * It is strongly discouraged to disable this feature.
     *
     * @param enable true to keep public key pinning enabled, false to disable it
     */
    public void setPublicKeyPinning(boolean enable) {
        megaApi.setPublicKeyPinning(enable);
    }

    /**
     * Make a name suitable for a file name in the local filesystem
     * <p>
     * This function escapes (%xx) forbidden characters in the local filesystem if needed.
     * You can revert this operation using MegaApi::unescapeFsIncompatible
     * <p>
     * If no dstPath is provided or filesystem type it's not supported this method will
     * escape characters contained in the following list: \/:?\"<>|*
     * Otherwise it will check forbidden characters for local filesystem type
     * <p>
     * The input string must be UTF8 encoded. The returned value will be UTF8 too.
     * <p>
     * You take the ownership of the returned value
     *
     * @param filename Name to convert (UTF8)
     * @param dstPath  Destination path
     * @return Converted name (UTF8)
     */
    @Nullable
    public String escapeFsIncompatible(String filename, String dstPath) {
        return megaApi.escapeFsIncompatible(filename, dstPath);
    }

    /**
     * Unescape a file name escaped with MegaApi::escapeFsIncompatible
     * <p>
     * If no localPath is provided or filesystem type it's not supported, this method will
     * unescape those sequences that once has been unescaped results in any character
     * of the following list: \/:?\"<>|*
     * Otherwise it will unescape those characters forbidden in local filesystem type
     * <p>
     * The input string must be UTF8 encoded. The returned value will be UTF8 too.
     * You take the ownership of the returned value
     *
     * @param name      Escaped name to convert (UTF8)
     * @param localPath Local path
     * @return Converted name (UTF8)
     */
    @Nullable
    String unescapeFsIncompatible(String name, String localPath) {
        return megaApi.unescapeFsIncompatible(name, localPath);
    }

    /**
     * Create a thumbnail for an image
     *
     * @param imagePath Image path
     * @param dstPath   Destination path for the thumbnail (including the file name)
     * @return True if the thumbnail was successfully created, otherwise false.
     */
    public boolean createThumbnail(String imagePath, String dstPath) {
        return megaApi.createThumbnail(imagePath, dstPath);
    }

    /**
     * Create a preview for an image
     *
     * @param imagePath Image path
     * @param dstPath   Destination path for the preview (including the file name)
     * @return True if the preview was successfully created, otherwise false.
     */
    public boolean createPreview(String imagePath, String dstPath) {
        return megaApi.createPreview(imagePath, dstPath);
    }

    /**
     * Convert a Base64 string to Base32
     * <p>
     * If the input pointer is NULL, this function will return NULL.
     * If the input character array isn't a valid base64 string
     * the effect is undefined
     * <p>
     * You take the ownership of the returned value
     *
     * @param base64 NULL-terminated Base64 character array
     * @return NULL-terminated Base32 character array
     */
    @Nullable
    public static String base64ToBase32(String base64) {
        return MegaApi.base64ToBase32(base64);
    }

    /**
     * Convert a Base32 string to Base64
     * <p>
     * If the input pointer is NULL, this function will return NULL.
     * If the input character array isn't a valid base32 string
     * the effect is undefined
     * <p>
     * You take the ownership of the returned value
     *
     * @param base32 NULL-terminated Base32 character array
     * @return NULL-terminated Base64 character array
     */
    @Nullable
    public static String base32ToBase64(String base32) {
        return MegaApi.base32ToBase64(base32);
    }

    /**
     * Recursively remove all local files/folders inside a local path
     *
     * @param path Local path of a folder to start the recursive deletion
     *             The folder itself is not deleted
     */
    public static void removeRecursively(String path) {
        MegaApi.removeRecursively(path);
    }

    /**
     * Check if the connection with MEGA servers is OK
     * <p>
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
     * <p>
     * If this function returns true, that means that the server is
     * ready to accept connections. The initialization is synchronous.
     * <p>
     * The server will serve files using this URL format:
     * http://127.0.0.1/<NodeHandle>/<NodeName>
     * <p>
     * The node name must be URL encoded and must match with the node handle.
     * You can generate a correct link for a MegaNode using MegaApi::httpServerGetLocalLink
     * <p>
     * If the node handle belongs to a folder node, a web with the list of files
     * inside the folder is returned.
     * <p>
     * It's important to know that the HTTP proxy server has several configuration options
     * that can restrict the nodes that will be served and the connections that will be accepted.
     * <p>
     * These are the default options:
     * - The restricted mode of the server is set to MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * (see MegaApi::httpServerSetRestrictedMode)
     * <p>
     * - Folder nodes are NOT allowed to be served (see MegaApi::httpServerEnableFolderServer)
     * - File nodes are allowed to be served (see MegaApi::httpServerEnableFileServer)
     * - Subtitles support is disabled (see MegaApi::httpServerEnableSubtitlesSupport)
     * <p>
     * The HTTP server will only stream a node if it's allowed by all configuration options.
     *
     * @param localOnly true to listen on 127.0.0.1 only, false to listen on all network interfaces
     * @param port      Port in which the server must accept connections
     * @return True if the server is ready, false if the initialization failed
     */
    public boolean httpServerStart(boolean localOnly, int port) {
        return megaApi.httpServerStart(localOnly, port);
    }

    /**
     * Start an HTTP proxy server in specified port
     * <p>
     * If this function returns true, that means that the server is
     * ready to accept connections. The initialization is synchronous.
     * <p>
     * The server will serve files using this URL format:
     * http://127.0.0.1/<NodeHandle>/<NodeName>
     * <p>
     * The node name must be URL encoded and must match with the node handle.
     * You can generate a correct link for a MegaNode using MegaApi::httpServerGetLocalLink
     * <p>
     * If the node handle belongs to a folder node, a web with the list of files
     * inside the folder is returned.
     * <p>
     * It's important to know that the HTTP proxy server has several configuration options
     * that can restrict the nodes that will be served and the connections that will be accepted.
     * <p>
     * These are the default options:
     * - The restricted mode of the server is set to MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * (see MegaApi::httpServerSetRestrictedMode)
     * <p>
     * - Folder nodes are NOT allowed to be served (see MegaApi::httpServerEnableFolderServer)
     * - File nodes are allowed to be served (see MegaApi::httpServerEnableFileServer)
     * - Subtitles support is disabled (see MegaApi::httpServerEnableSubtitlesSupport)
     * <p>
     * The HTTP server will only stream a node if it's allowed by all configuration options.
     *
     * @param localOnly true to listen on 127.0.0.1 only, false to listen on all network interfaces
     * @return True if the server is ready, false if the initialization failed
     */
    public boolean httpServerStart(boolean localOnly) {
        return megaApi.httpServerStart(localOnly);
    }

    /**
     * Start an HTTP proxy server in specified port
     * <p>
     * If this function returns true, that means that the server is
     * ready to accept connections. The initialization is synchronous.
     * <p>
     * The server will serve files using this URL format:
     * http://127.0.0.1/<NodeHandle>/<NodeName>
     * <p>
     * The node name must be URL encoded and must match with the node handle.
     * You can generate a correct link for a MegaNode using MegaApi::httpServerGetLocalLink
     * <p>
     * If the node handle belongs to a folder node, a web with the list of files
     * inside the folder is returned.
     * <p>
     * It's important to know that the HTTP proxy server has several configuration options
     * that can restrict the nodes that will be served and the connections that will be accepted.
     * <p>
     * These are the default options:
     * - The restricted mode of the server is set to MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * (see MegaApi::httpServerSetRestrictedMode)
     * <p>
     * - Folder nodes are NOT allowed to be served (see MegaApi::httpServerEnableFolderServer)
     * - File nodes are allowed to be served (see MegaApi::httpServerEnableFileServer)
     * - Subtitles support is disabled (see MegaApi::httpServerEnableSubtitlesSupport)
     * <p>
     * The HTTP server will only stream a node if it's allowed by all configuration options.
     *
     * @return True if the server is ready, false if the initialization failed
     */
    public boolean httpServerStart() {
        return megaApi.httpServerStart();
    }

    /**
     * Stop the HTTP proxy server
     * <p>
     * When this function returns, the server is already shutdown.
     * If the HTTP proxy server isn't running, this functions does nothing
     */
    public void httpServerStop() {
        megaApi.httpServerStop();
    }

    /**
     * Check if the HTTP proxy server is running
     *
     * @return 0 if the server is not running. Otherwise the port in which it's listening to
     */
    public int httpServerIsRunning() {
        return megaApi.httpServerIsRunning();
    }

    /**
     * Check if the HTTP proxy server is listening on all network interfaces
     *
     * @return true if the HTTP proxy server is listening on 127.0.0.1 only, or it's not started.
     * If it's started and listening on all network interfaces, this function returns false
     */
    public boolean httpServerIsLocalOnly() {
        return megaApi.httpServerIsLocalOnly();
    }

    /**
     * Allow/forbid to serve files
     * <p>
     * By default, files are served (when the server is running)
     * <p>
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
     * <p>
     * This function can return true even if the HTTP proxy server is not running
     * <p>
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
     * <p>
     * By default, folders are NOT served
     * <p>
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
     * <p>
     * This function can return true even if the HTTP proxy server is not running
     * <p>
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
     * <p>
     * This function allows to restrict the nodes that are allowed to be served.
     * For not allowed links, the server will return "407 Forbidden".
     * <p>
     * Possible values are:
     * - HTTP_SERVER_DENY_ALL = -1
     * All nodes are forbidden
     * <p>
     * - HTTP_SERVER_ALLOW_ALL = 0
     * All nodes are allowed to be served
     * <p>
     * - HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1 (default)
     * Only links created with MegaApi::httpServerGetLocalLink are allowed to be served
     * <p>
     * - HTTP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
     * Only the last link created with MegaApi::httpServerGetLocalLink is allowed to be served
     * <p>
     * If a different value from the list above is passed to this function, it won't have any effect and the previous
     * state of this option will be preserved.
     * <p>
     * The default value of this property is MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * <p>
     * The state of this option is preserved even if the HTTP server is restarted, but
     * the HTTP proxy server only remembers the generated links since the last call to
     * MegaApi::httpServerStart
     * <p>
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
     * <p>
     * Possible return values are:
     * - HTTP_SERVER_DENY_ALL = -1
     * All nodes are forbidden
     * <p>
     * - HTTP_SERVER_ALLOW_ALL = 0
     * All nodes are allowed to be served
     * <p>
     * - HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1
     * Only links created with MegaApi::httpServerGetLocalLink are allowed to be served
     * <p>
     * - HTTP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
     * Only the last link created with MegaApi::httpServerGetLocalLink is allowed to be served
     * <p>
     * The default value of this property is MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
     * <p>
     * See MegaApi::httpServerEnableRestrictedMode and MegaApi::httpServerStart
     * <p>
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
     * <p>
     * Subtitles support allows to stream some special links that otherwise wouldn't be valid.
     * For example, let's suppose that the server is streaming this video:
     * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.avi
     * <p>
     * Some media players scan HTTP servers looking for subtitle files and request links like these ones:
     * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.txt
     * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.srt
     * <p>
     * Even if a file with that name is in the same folder of the MEGA account, the node wouldn't be served because
     * the node handle wouldn't match.
     * <p>
     * When this feature is enabled, the HTTP proxy server will check if there are files with that name
     * in the same folder as the node corresponding to the handle in the link.
     * <p>
     * If a matching file is found, the name is exactly the same as the the node with the specified handle
     * (except the extension), the node with that handle is allowed to be streamed and this feature is enabled
     * the HTTP proxy server will serve that file.
     * <p>
     * This feature is disabled by default.
     *
     * @param enable True to enable subtitles support, false to disable it
     */
    public void httpServerEnableSubtitlesSupport(boolean enable) {
        megaApi.httpServerEnableSubtitlesSupport(enable);
    }

    /**
     * Check if the support for subtitles is enabled
     * <p>
     * See MegaApi::httpServerEnableSubtitlesSupport.
     * <p>
     * This feature is disabled by default.
     *
     * @return true of the support for subtitles is enabled, otherwise false
     */
    public boolean httpServerIsSubtitlesSupportEnabled() {
        return megaApi.httpServerIsSubtitlesSupportEnabled();
    }

    /**
     * Add a listener to receive information about the HTTP proxy server
     * <p>
     * This is the valid data that will be provided on callbacks:
     * - MegaTransfer::getType - It will be MegaTransfer::TYPE_LOCAL_TCP_DOWNLOAD
     * - MegaTransfer::getPath - URL requested to the HTTP proxy server
     * - MegaTransfer::getFileName - Name of the requested file (if any, otherwise NULL)
     * - MegaTransfer::getNodeHandle - Handle of the requested file (if any, otherwise NULL)
     * - MegaTransfer::getTotalBytes - Total bytes of the response (response headers + file, if required)
     * - MegaTransfer::getStartPos - Start position (for range requests only, otherwise -1)
     * - MegaTransfer::getEndPos - End position (for range requests only, otherwise -1)
     * <p>
     * On the onTransferFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EINCOMPLETE - If the whole response wasn't sent
     * (it's normal to get this error code sometimes because media players close connections when they have
     * the data that they need)
     * <p>
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
     *
     * @param listener Listener that won't continue receiving information
     */
    public void httpServerRemoveListener(MegaTransferListenerInterface listener) {
        ArrayList<DelegateMegaTransferListener> listenersToRemove = new ArrayList<>();

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

        for (int i = 0; i < listenersToRemove.size(); i++) {
            megaApi.httpServerRemoveListener(listenersToRemove.get(i));
        }
    }

    /**
     * Returns a URL to a node in the local HTTP proxy server
     * <p>
     * The HTTP proxy server must be running before using this function, otherwise
     * it will return NULL.
     * <p>
     * You take the ownership of the returned value
     *
     * @param node Node to generate the local HTTP link
     * @return URL to the node in the local HTTP proxy server, otherwise NULL
     */
    @Nullable
    public String httpServerGetLocalLink(MegaNode node) {
        return megaApi.httpServerGetLocalLink(node);
    }

    /**
     * Set the maximum buffer size for the internal buffer
     * <p>
     * The HTTP proxy server has an internal buffer to store the data received from MEGA
     * while it's being sent to clients. When the buffer is full, the connection with
     * the MEGA storage server is closed, when the buffer has few data, the connection
     * with the MEGA storage server is started again.
     * <p>
     * Even with very fast connections, due to the possible latency starting new connections,
     * if this buffer is small the streaming can have problems due to the overhead caused by
     * the excessive number of POST requests.
     * <p>
     * It's recommended to set this buffer at least to 1MB
     * <p>
     * For connections that request less data than the buffer size, the HTTP proxy server
     * will only allocate the required memory to complete the request to minimize the
     * memory usage.
     * <p>
     * The new value will be taken into account since the next request received by
     * the HTTP proxy server, not for ongoing requests. It's possible and effective
     * to call this function even before the server has been started, and the value
     * will be still active even if the server is stopped and started again.
     *
     * @param bufferSize Maximum buffer size (in bytes) or a number <= 0 to use the
     *                   internal default value
     */
    public void httpServerSetMaxBufferSize(int bufferSize) {
        megaApi.httpServerSetMaxBufferSize(bufferSize);
    }

    /**
     * Get the maximum size of the internal buffer size
     * <p>
     * See MegaApi::httpServerSetMaxBufferSize
     *
     * @return Maximum size of the internal buffer size (in bytes)
     */
    public int httpServerGetMaxBufferSize() {
        return megaApi.httpServerGetMaxBufferSize();
    }

    /**
     * Set the maximum size of packets sent to clients
     * <p>
     * For each connection, the HTTP proxy server only sends one write to the underlying
     * socket at once. This parameter allows to set the size of that write.
     * <p>
     * A small value could cause a lot of writes and would lower the performance.
     * <p>
     * A big value could send too much data to the output buffer of the socket. That could
     * keep the internal buffer full of data that hasn't been sent to the client yet,
     * preventing the retrieval of additional data from the MEGA storage server. In that
     * circumstances, the client could read a lot of data at once and the HTTP server
     * could not have enough time to get more data fast enough.
     * <p>
     * It's recommended to set this value to at least 8192 and no more than the 25% of
     * the maximum buffer size (MegaApi::httpServerSetMaxBufferSize).
     * <p>
     * The new value will be taken into account since the next request received by
     * the HTTP proxy server, not for ongoing requests. It's possible and effective
     * to call this function even before the server has been started, and the value
     * will be still active even if the server is stopped and started again.
     *
     * @param outputSize Maximum size of data packets sent to clients (in bytes) or
     *                   a number <= 0 to use the internal default value
     */
    public void httpServerSetMaxOutputSize(int outputSize) {
        megaApi.httpServerSetMaxOutputSize(outputSize);
    }

    /**
     * Get the maximum size of the packets sent to clients
     * <p>
     * See MegaApi::httpServerSetMaxOutputSize
     *
     * @return Maximum size of the packets sent to clients (in bytes)
     */
    public int httpServerGetMaxOutputSize() {
        return megaApi.httpServerGetMaxOutputSize();
    }

    /**
     * Get the MIME type associated with the extension
     * <p>
     * You take the ownership of the returned value
     *
     * @param extension File extension (with or without a leading dot)
     * @return MIME type associated with the extension
     */
    @Nullable
    public static String getMimeType(String extension) {
        return MegaApi.getMimeType(extension);
    }

    /**
     * Register a token for push notifications
     * <p>
     * This function attach a token to the current session, which is intended to get push notifications
     * on mobile platforms like Android and iOS.
     * <p>
     * The push notification mechanism is platform-dependent. Hence, the app should indicate the
     * type of push notification to be registered. Currently, the different types are:
     * - MegaApi::PUSH_NOTIFICATION_ANDROID    = 1
     * - MegaApi::PUSH_NOTIFICATION_IOS_VOIP   = 2
     * - MegaApi::PUSH_NOTIFICATION_IOS_STD    = 3
     * - MegaApi::PUSH_NOTIFICATION_ANDROID_HUAWEI = 4
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REGISTER_PUSH_NOTIFICATION
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getText - Returns the token provided.
     * - MegaRequest::getNumber - Returns the device type provided.
     *
     * @param deviceType Type of notification to be registered.
     * @param token      Character array representing the token to be registered.
     * @param listener   MegaRequestListener to track this request
     */
    public void registerPushNotifications(int deviceType, String token, MegaRequestListenerInterface listener) {
        megaApi.registerPushNotifications(deviceType, token, createDelegateRequestListener(listener));
    }

    /**
     * Register a token for push notifications
     * <p>
     * This function attach a token to the current session, which is intended to get push notifications
     * on mobile platforms like Android and iOS.
     * <p>
     * The push notification mechanism is platform-dependent. Hence, the app should indicate the
     * type of push notification to be registered. Currently, the different types are:
     * - MegaApi::PUSH_NOTIFICATION_ANDROID    = 1
     * - MegaApi::PUSH_NOTIFICATION_IOS_VOIP   = 2
     * - MegaApi::PUSH_NOTIFICATION_IOS_STD    = 3
     * - MegaApi::PUSH_NOTIFICATION_ANDROID_HUAWEI = 4
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REGISTER_PUSH_NOTIFICATION
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getText - Returns the token provided.
     * - MegaRequest::getNumber - Returns the device type provided.
     *
     * @param deviceType Type of notification to be registered.
     * @param token      Character array representing the token to be registered.
     */
    public void registerPushNotifications(int deviceType, String token) {
        megaApi.registerPushNotifications(deviceType, token);
    }

    /**
     * Get the MEGA Achievements of the account logged in
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always false
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the MEGA Achievements of this account
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getAccountAchievements(MegaRequestListenerInterface listener) {
        megaApi.getAccountAchievements(createDelegateRequestListener(listener));
    }

    /**
     * Get the MEGA Achievements of the account logged in
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always false
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the MEGA Achievements of this account
     */
    public void getAccountAchievements() {
        megaApi.getAccountAchievements();
    }

    /**
     * Get the list of existing MEGA Achievements
     * <p>
     * Similar to MegaApi::getAccountAchievements, this method returns only the base storage and
     * the details for the different achievement classes, but not awards or rewards related to the
     * account that is logged in.
     * This function can be used to give an indication of what is available for advertising
     * for unregistered users, despite it can be used with a logged in account with no difference.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the list of existing MEGA Achievements
     *
     * @param listener MegaRequestListener to track this request
     * @implNote if the IP address is not achievement enabled (it belongs to a country where MEGA
     * Achievements are not enabled), the request will fail with MegaError::API_EACCESS.
     */
    public void getMegaAchievements(MegaRequestListenerInterface listener) {
        megaApi.getMegaAchievements(createDelegateRequestListener(listener));
    }

    /**
     * Get the list of existing MEGA Achievements
     * <p>
     * Similar to MegaApi::getAccountAchievements, this method returns only the base storage and
     * the details for the different achievement classes, but not awards or rewards related to the
     * account that is logged in.
     * This function can be used to give an indication of what is available for advertising
     * for unregistered users, despite it can be used with a logged in account with no difference.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getFlag - Always true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaAchievementsDetails - Details of the list of existing MEGA Achievements
     *
     * @implNote if the IP address is not achievement enabled (it belongs to a country where MEGA
     * Achievements are not enabled), the request will fail with MegaError::API_EACCESS.
     */
    public void getMegaAchievements() {
        megaApi.getMegaAchievements();
    }

    /**
     * Set the OriginalFingerprint of a node.
     * <p>
     * Use this call to attach an originalFingerprint to a node. The fingerprint must
     * be generated from the file prior to modification, where this node is the modified file.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the node
     * - MegaRequest::getText - Returns the specified fingerprint
     * - MegaRequest::getFlag - Returns true (official attribute)
     * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_ORIGINALFINGERPRINT
     *
     * @param node                The node to attach the originalFingerprint to.
     * @param originalFingerprint The fingerprint of the file before modification
     * @param listener            MegaRequestListener to track this request
     */
    public void setOriginalFingerprint(MegaNode node, String originalFingerprint, MegaRequestListenerInterface listener) {
        megaApi.setOriginalFingerprint(node, originalFingerprint, createDelegateRequestListener(listener));
    }

    /**
     * Returns nodes that have an originalFingerprint equal to the supplied value
     * <p>
     * Search the node tree and return a list of nodes that have an originalFingerprint, which
     * matches the supplied originalFingerprint.
     * <p>
     * If the parent node supplied is not NULL, it only searches nodes below that parent folder,
     * otherwise all nodes are searched. If no nodes are found with that original fingerprint,
     * this function returns an empty MegaNodeList.
     * <p>
     * You take the ownership of the returned value.
     *
     * @param originalFingerprint Original Fingerprint to check
     * @param parent              Only return nodes below this specified parent folder. Pass NULL to consider all nodes.
     * @return List of nodes with the same original fingerprint
     */
    public MegaNodeList getNodesByOriginalFingerprint(String originalFingerprint, MegaNode parent) {
        return megaApi.getNodesByOriginalFingerprint(originalFingerprint, parent);
    }

    /**
     * Retrieve basic information about a folder link
     * <p>
     * This function retrieves basic information from a folder link, like the number of files / folders
     * and the name of the folder. For folder links containing a lot of files/folders,
     * this function is more efficient than a fetchNodes.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink() - Returns the public link to the folder
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaFolderInfo() - Returns information about the contents of the folder
     * - MegaRequest::getNodeHandle() - Returns the public handle of the folder
     * - MegaRequest::getParentHandle() - Returns the handle of the owner of the folder
     * - MegaRequest::getText() - Returns the name of the folder.
     * If there's no name, it returns the special status string "CRYPTO_ERROR".
     * If the length of the name is zero, it returns the special status string "BLANK".
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS - If the link is not a valid folder link
     * - MegaError::API_EKEY - If the public link does not contain the key or it is invalid
     *
     * @param megaFolderLink Public link to a folder in MEGA
     * @param listener       MegaRequestListener to track this request
     */
    public void getPublicLinkInformation(String megaFolderLink, MegaRequestListenerInterface listener) {
        megaApi.getPublicLinkInformation(megaFolderLink, createDelegateRequestListener(listener));
    }

    /**
     * Retrieve basic information about a folder link
     * <p>
     * This function retrieves basic information from a folder link, like the number of files / folders
     * and the name of the folder. For folder links containing a lot of files/folders,
     * this function is more efficient than a fetchNodes.
     * <p>
     * Valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getLink() - Returns the public link to the folder
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaFolderInfo() - Returns information about the contents of the folder
     * - MegaRequest::getNodeHandle() - Returns the public handle of the folder
     * - MegaRequest::getParentHandle() - Returns the handle of the owner of the folder
     * - MegaRequest::getText() - Returns the name of the folder.
     * If there's no name, it returns the special status string "CRYPTO_ERROR".
     * If the length of the name is zero, it returns the special status string "BLANK".
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS - If the link is not a valid folder link
     * - MegaError::API_EKEY - If the public link does not contain the key or it is invalid
     *
     * @param megaFolderLink Public link to a folder in MEGA
     */
    public void getPublicLinkInformation(String megaFolderLink) {
        megaApi.getPublicLinkInformation(megaFolderLink);
    }

    /**
     * Call the low level function setrlimit() for NOFILE, needed for some platforms.
     * <p>
     * Particularly on phones, the system default limit for the number of open files (and sockets)
     * is quite low.   When the SDK can be working on many files and many sockets at once,
     * we need a higher limit.   Those limits need to take into account the needs of the whole
     * app and not just the SDK, of course.   This function is provided in order that the app
     * can make that call and set appropriate limits.
     *
     * @param newNumFileLimit The new limit of file and socket handles for the whole app.
     * @return True when there were no errors setting the new limit (even when clipped to the maximum
     * allowed value). It returns false when setting a new limit failed.
     */
    public boolean platformSetRLimitNumFile(int newNumFileLimit) {
        return megaApi.platformSetRLimitNumFile(newNumFileLimit);
    }

    /**
     * Call the low level function getrlimit() for NOFILE, needed for some platforms.
     *
     * @return The current limit for the number of open files (and sockets) for the app, or -1 if error.
     */
    public int platformGetRLimitNumFile() {
        return megaApi.platformGetRLimitNumFile();
    }

    /**
     * Requests a list of all Smart Banners available for current user.
     * <p>
     * The response value is stored as a MegaBannerList.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_BANNERS
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaBannerList: the list of banners
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EACCESS - If called with no user being logged in.
     * - MegaError::API_EINTERNAL - If the internally used user attribute exists but can't be decoded.
     * - MegaError::API_ENOENT if there are no banners to return to the user.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getBanners(MegaRequestListenerInterface listener) {
        megaApi.getBanners(createDelegateRequestListener(listener));
    }

    /**
     * Requests a list of all Smart Banners available for current user.
     * <p>
     * The response value is stored as a MegaBannerList.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_BANNERS
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaBannerList: the list of banners
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EACCESS - If called with no user being logged in.
     * - MegaError::API_EINTERNAL - If the internally used user attribute exists but can't be decoded.
     * - MegaError::API_ENOENT if there are no banners to return to the user.
     */
    public void getBanners() {
        megaApi.getBanners();
    }

    /**
     * No longer show the Smart Banner with the specified id to the current user.
     *
     * @param id       The identifier of the Smart Banner
     * @param listener MegaRequestListener to track this request
     */
    public void dismissBanner(int id, MegaRequestListenerInterface listener) {
        megaApi.dismissBanner(id, createDelegateRequestListener(listener));
    }

    /**
     * No longer show the Smart Banner with the specified id to the current user.
     *
     * @param id The identifier of the Smart Banner
     */
    public void dismissBanner(int id) {
        megaApi.dismissBanner(id);
    }

    //****************************************************************************************************/
    // INTERNAL METHODS
    //****************************************************************************************************/

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

    private static MegaLogger createDelegateMegaLogger(MegaLoggerInterface listener) {
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
        megaApi.removeRequestListener(listener);
    }

    void privateFreeTransferListener(DelegateMegaTransferListener listener) {
        activeTransferListeners.remove(listener);
    }

    static public ArrayList<MegaNode> nodeListToArray(MegaNodeList nodeList) {
        if (nodeList == null) {
            return null;
        }

        ArrayList<MegaNode> result = new ArrayList<>(nodeList.size());
        for (int i = 0; i < nodeList.size(); i++) {
            result.add(nodeList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaShare> shareListToArray(MegaShareList shareList) {
        if (shareList == null) {
            return null;
        }

        ArrayList<MegaShare> result = new ArrayList<>(shareList.size());
        for (int i = 0; i < shareList.size(); i++) {
            result.add(shareList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaContactRequest> contactRequestListToArray(MegaContactRequestList contactRequestList) {
        if (contactRequestList == null) {
            return null;
        }

        ArrayList<MegaContactRequest> result = new ArrayList<>(contactRequestList.size());
        for (int i = 0; i < contactRequestList.size(); i++) {
            result.add(contactRequestList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaTransfer> transferListToArray(MegaTransferList transferList) {
        if (transferList == null) {
            return null;
        }

        ArrayList<MegaTransfer> result = new ArrayList<>(transferList.size());
        for (int i = 0; i < transferList.size(); i++) {
            result.add(transferList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaUser> userListToArray(MegaUserList userList) {

        if (userList == null) {
            return null;
        }

        ArrayList<MegaUser> result = new ArrayList<>(userList.size());
        for (int i = 0; i < userList.size(); i++) {
            result.add(userList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaUserAlert> userAlertListToArray(MegaUserAlertList userAlertList) {

        if (userAlertList == null) {
            return null;
        }

        ArrayList<MegaUserAlert> result = new ArrayList<>(userAlertList.size());
        for (int i = 0; i < userAlertList.size(); i++) {
            result.add(userAlertList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaSet> megaSetListToArray(MegaSetList megaSetList) {
        if (megaSetList == null) {
            return null;
        }

        ArrayList<MegaSet> result = new ArrayList<>((int) megaSetList.size());
        for (int i = 0; i < megaSetList.size(); i++) {
            result.add(megaSetList.get(i).copy());
        }

        return result;
    }

    static ArrayList<MegaSetElement> megaSetElementListToArray(MegaSetElementList megaSetElementList) {
        if (megaSetElementList == null) {
            return null;
        }

        ArrayList<MegaSetElement> result = new ArrayList<>((int) megaSetElementList.size());
        for (int i = 0; i < megaSetElementList.size(); i++) {
            result.add(megaSetElementList.get(i).copy());
        }

        return result;
    }

    /**
     * Creates a copy of MegaRecentActionBucket required for its usage in the app.
     *
     * @param bucket The MegaRecentActionBucket received.
     * @return A copy of MegaRecentActionBucket.
     */
    public MegaRecentActionBucket copyBucket(MegaRecentActionBucket bucket) {
        return bucket.copy();
    }

    /**
     * Returns whether notifications about a chat have to be generated.
     *
     * @param chatid MegaHandle that identifies the chat room.
     * @return true if notification has to be created.
     */
    public boolean isChatNotifiable(long chatid) {
        return megaApi.isChatNotifiable(chatid);
    }

    /**
     * Provide a phone number to get verification code.
     *
     * @param phoneNumber the phone number to receive the txt with verification code.
     * @param listener    callback of this request.
     */
    public void sendSMSVerificationCode(String phoneNumber, nz.mega.sdk.MegaRequestListenerInterface listener) {
        megaApi.sendSMSVerificationCode(phoneNumber, createDelegateRequestListener(listener));
    }

    public void sendSMSVerificationCode(String phoneNumber, nz.mega.sdk.MegaRequestListenerInterface listener, boolean reverifying_whitelisted) {
        megaApi.sendSMSVerificationCode(phoneNumber, createDelegateRequestListener(listener), reverifying_whitelisted);
    }

    /**
     * Send the verification code to verify phone number.
     *
     * @param verificationCode received 6 digits verification code.
     * @param listener         callback of this request.
     */
    public void checkSMSVerificationCode(String verificationCode, nz.mega.sdk.MegaRequestListenerInterface listener) {
        megaApi.checkSMSVerificationCode(verificationCode, createDelegateRequestListener(listener));
    }

    /**
     * Get the verified phone number of the mega account.
     *
     * @return verified phone number.
     */
    @Nullable
    public String smsVerifiedPhoneNumber() {
        return megaApi.smsVerifiedPhoneNumber();
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
     * Get the email address of any user in MEGA.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_USER_EMAIL
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the user (the provided one as parameter)
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getEmail - Returns the email address
     *
     * @param handle   Handle of the user to get the attribute.
     * @param listener MegaRequestListener to track this request
     */
    public void getUserEmail(long handle, MegaRequestListenerInterface listener) {
        megaApi.getUserEmail(handle, createDelegateRequestListener(listener));
    }

    /**
     * Resume a registration process for an Ephemeral++ account
     * <p>
     * When a user begins the account registration process by calling
     * MegaApi::createEphemeralAccountPlusPlus an ephemeral++ account is created.
     * <p>
     * Until the user successfully confirms the signup link sent to the provided email address,
     * you can resume the ephemeral session in order to change the email address, resend the
     * signup link (@see MegaApi::sendSignupLink) and also to receive notifications in case the
     * user confirms the account using another client (MegaGlobalListener::onAccountUpdate or
     * MegaListener::onAccountUpdate). It is also possible to cancel the registration process by
     * MegaApi::cancelCreateAccount, which invalidates the signup link associated to the ephemeral
     * session (the session will be still valid).
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getSessionKey - Returns the session id to resume the process
     * - MegaRequest::getParamType - Returns the value MegaApi::RESUME_EPLUSPLUS_ACCOUNT
     * <p>
     * In case the account is already confirmed, the associated request will fail with
     * error MegaError::API_EARGS.
     *
     * @param sid      Session id valid for the ephemeral++ account (@see MegaApi::createEphemeralAccountPlusPlus)
     * @param listener MegaRequestListener to track this request
     */
    public void resumeCreateAccountEphemeralPlusPlus(String sid, MegaRequestListenerInterface listener) {
        megaApi.resumeCreateAccountEphemeralPlusPlus(sid, createDelegateRequestListener(listener));
    }

    /**
     * Cancel a registration process
     * <p>
     * If a signup link has been generated during registration process, call this function
     * to invalidate it. The ephemeral session will not be invalidated, only the signup link.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value MegaApi::CANCEL_ACCOUNT
     *
     * @param listener MegaRequestListener to track this request
     */
    public void cancelCreateAccount(MegaRequestListenerInterface listener) {
        megaApi.cancelCreateAccount(createDelegateRequestListener(listener));
    }

    /**
     * @param backupType  back up type requested for the service
     * @param targetNode  MEGA folder to hold the backups
     * @param localFolder Local path of the folder
     * @param backupName  Name of the backup
     * @param state       state
     * @param subState    subState
     * @param listener    MegaRequestListener to track this request
     *                    Registers a backup to display in Backup Centre
     *                    <p>
     *                    Apps should register backups, like CameraUploads, in order to be listed in the
     *                    BackupCentre. The client should send heartbeats to indicate the progress of the
     *                    backup (see \c MegaApi::sendBackupHeartbeats).
     *                    <p>
     *                    Possible types of backups:
     *                    BACKUP_TYPE_CAMERA_UPLOADS = 3,
     *                    BACKUP_TYPE_MEDIA_UPLOADS = 4,   // Android has a secondary CU
     *                    <p>
     *                    Note that the backup name is not registered in the API as part of the data of this
     *                    backup. It will be stored in a user's attribute after this request finished. For
     *                    more information, see \c MegaApi::setBackupName and MegaApi::getBackupName.
     *                    <p>
     *                    The associated request type with this request is MegaRequest::TYPE_BACKUP_PUT
     *                    Valid data in the MegaRequest object received on callbacks:
     *                    - MegaRequest::getParentHandle - Returns the backupId
     *                    - MegaRequest::getNodeHandle - Returns the target node of the backup
     *                    - MegaRequest::getName - Returns the backup name of the remote location
     *                    - MegaRequest::getAccess - Returns the backup state
     *                    - MegaRequest::getFile - Returns the path of the local folder
     *                    - MegaRequest::getTotalBytes - Returns the backup type
     *                    - MegaRequest::getNumDetails - Returns the backup substate
     *                    - MegaRequest::getFlag - Returns true
     *                    - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     */
    public void setBackup(int backupType, long targetNode, String localFolder, String backupName,
                          int state, int subState, MegaRequestListenerInterface listener) {
        megaApi.setBackup(backupType, targetNode, localFolder, backupName, state, subState,
                createDelegateRequestListener(listener));
    }

    /**
     * @param backupId    backup id identifying the backup to be updated
     * @param backupType  back up type requested for the service
     * @param targetNode  MEGA folder to hold the backups
     * @param localFolder Local path of the folder
     * @param state       backup state
     * @param subState    backup subState
     * @param listener    MegaRequestListener to track this request
     *                    Update the information about a registered backup for Backup Centre
     *                    <p>
     *                    Possible types of backups:
     *                    BACKUP_TYPE_INVALID = -1,
     *                    BACKUP_TYPE_CAMERA_UPLOADS = 3,
     *                    BACKUP_TYPE_MEDIA_UPLOADS = 4,   // Android has a secondary CU
     *                    <p>
     *                    Params that keep the same value are passed with invalid value to avoid to send to the server
     *                    Invalid values:
     *                    - type: BACKUP_TYPE_INVALID
     *                    - nodeHandle: UNDEF
     *                    - localFolder: nullptr
     *                    - deviceId: nullptr
     *                    - state: -1
     *                    - subState: -1
     *                    - extraData: nullptr
     *                    <p>
     *                    If you want to update the backup name, use \c MegaApi::setBackupName.
     *                    <p>
     *                    The associated request type with this request is MegaRequest::TYPE_BACKUP_PUT
     *                    Valid data in the MegaRequest object received on callbacks:
     *                    - MegaRequest::getParentHandle - Returns the backupId
     *                    - MegaRequest::getTotalBytes - Returns the backup type
     *                    - MegaRequest::getNodeHandle - Returns the target node of the backup
     *                    - MegaRequest::getFile - Returns the path of the local folder
     *                    - MegaRequest::getAccess - Returns the backup state
     *                    - MegaRequest::getNumDetails - Returns the backup substate
     *                    - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     */
    public void updateBackup(long backupId, int backupType, long targetNode, String localFolder,
                             String backupName, int state, int subState,
                             MegaRequestListenerInterface listener) {
        megaApi.updateBackup(backupId, backupType, targetNode, localFolder, backupName, state,
                subState, createDelegateRequestListener(listener));
    }

    /**
     * @param backupId backup id identifying the backup to be removed
     * @param listener MegaRequestListener to track this request
     *                 Unregister a backup already registered for the Backup Centre
     *                 <p>
     *                 This method allows to remove a backup from the list of backups displayed in the
     *                 Backup Centre. @see \c MegaApi::setBackup.
     *                 <p>
     *                 The associated request type with this request is MegaRequest::TYPE_BACKUP_REMOVE
     *                 Valid data in the MegaRequest object received on callbacks:
     *                 - MegaRequest::getParentHandle - Returns the backupId
     *                 - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     */
    public void removeBackup(long backupId, MegaRequestListenerInterface listener) {
        megaApi.removeBackup(backupId, createDelegateRequestListener(listener));
    }

    /**
     * Fetch information about all registered backups for Backup Centre
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_BACKUP_INFO
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaBackupInfoList - Returns information about all registered backups
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getBackupInfo(MegaRequestListenerInterface listener) {
        megaApi.getBackupInfo(createDelegateRequestListener(listener));
    }

    /**
     * @param backupId backup id identifying the backup
     * @param status   backup state
     * @param progress backup progress
     * @param ups      Number of pending upload transfers
     * @param downs    Number of pending download transfers
     * @param ts       Last action timestamp
     * @param lastNode Last node handle to be synced
     * @param listener MegaRequestListener to track this request
     *                 Send heartbeat associated with an existing backup
     *                 <p>
     *                 The client should call this method regularly for every registered backup, in order to
     *                 inform about the status of the backup.
     *                 <p>
     *                 Progress, last timestamp and last node are not always meaningful (ie. when the Camera
     *                 Uploads starts a new batch, there isn't a last node, or when the CU up to date and
     *                 inactive for long time, the progress doesn't make sense). In consequence, these parameters
     *                 are optional. They will not be sent to API if they take the following values:
     *                 - lastNode = INVALID_HANDLE
     *                 - lastTs = -1
     *                 - progress = -1
     *                 <p>
     *                 The associated request type with this request is MegaRequest::TYPE_BACKUP_PUT_HEART_BEAT
     *                 Valid data in the MegaRequest object received on callbacks:
     *                 - MegaRequest::getParentHandle - Returns the backupId
     *                 - MegaRequest::getAccess - Returns the backup state
     *                 - MegaRequest::getNumDetails - Returns the backup substate
     *                 - MegaRequest::getParamType - Returns the number of pending upload transfers
     *                 - MegaRequest::getTransferTag - Returns the number of pending download transfers
     *                 - MegaRequest::getNumber - Returns the last action timestamp
     *                 - MegaRequest::getNodeHandle - Returns the last node handle to be synced
     */
    public void sendBackupHeartbeat(long backupId, int status, int progress, int ups, int downs,
                                    long ts, long lastNode, MegaRequestListenerInterface listener) {
        megaApi.sendBackupHeartbeat(backupId, status, progress, ups, downs, ts, lastNode,
                createDelegateRequestListener(listener));
    }

    /**
     * Fetch ads
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_FETCH_ADS
     * Valid data in the MegaRequest object received on callbacks:
     *  - MegaRequest::getNumber A bitmap flag used to communicate with the API
     *  - MegaRequest::getMegaStringList List of the adslot ids to fetch
     *  - MegaRequest::getNodeHandle  Public handle that the user is visiting
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaStringMap: map with relationship between ids and ius
     *
     * @param adFlags A bitmap flag used to communicate with the API
     * Valid values are:
     *      - ADS_DEFAULT = 0x0
     *      - ADS_FORCE_ADS = 0x200
     *      - ADS_IGNORE_MEGA = 0x400
     *      - ADS_IGNORE_COUNTRY = 0x800
     *      - ADS_IGNORE_IP = 0x1000
     *      - ADS_IGNORE_PRO = 0x2000
     *      - ADS_FLAG_IGNORE_ROLLOUT = 0x4000
     * @param adUnits MegaStringList, a list of the adslot ids to fetch; it cannot be null nor empty
     * @param publicHandle MegaHandle, provide the public handle that the user is visiting
     * @param listener MegaRequestListener to track this request
     */
    public void fetchAds(int adFlags, MegaStringList adUnits, long publicHandle,
                         MegaRequestListenerInterface listener) {
        megaApi.fetchAds(adFlags, adUnits, publicHandle, createDelegateRequestListener(listener));
    };

    /**
     * Check if ads should show or not
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_QUERY_ADS
     * Valid data in the MegaRequest object received on callbacks:
     *  - MegaRequest::getNumber A bitmap flag used to communicate with the API
     *  - MegaRequest::getNodeHandle  Public handle that the user is visiting
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumDetails Return if ads should be show or not
     *
     * @param adFlags A bitmap flag used to communicate with the API
     * Valid values are:
     *      - ADS_DEFAULT = 0x0
     *      - ADS_FORCE_ADS = 0x200
     *      - ADS_IGNORE_MEGA = 0x400
     *      - ADS_IGNORE_COUNTRY = 0x800
     *      - ADS_IGNORE_IP = 0x1000
     *      - ADS_IGNORE_PRO = 0x2000
     *      - ADS_FLAG_IGNORE_ROLLOUT = 0x4000
     * @param publicHandle MegaHandle, provide the public handle that the user is visiting
     * @param listener MegaRequestListener to track this request
     */
    public void queryAds(int adFlags, long publicHandle, MegaRequestListenerInterface listener) {
        megaApi.queryAds(adFlags, publicHandle, createDelegateRequestListener(listener));
    }

    /**
     * Set a bitmap to indicate whether some cookies are enabled or not
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_COOKIE_SETTINGS
     * - MegaRequest::getNumDetails - Return a bitmap with cookie settings
     * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     *
     * @param settings A bitmap with cookie settings
     *                 Valid bits are:
     *                 - Bit 0: essential
     *                 - Bit 1: preference
     *                 - Bit 2: analytics
     *                 - Bit 3: ads
     *                 - Bit 4: thirdparty
     * @param listener MegaRequestListener to track this request
     */
    public void setCookieSettings(int settings, MegaRequestListenerInterface listener) {
        megaApi.setCookieSettings(settings, createDelegateRequestListener(listener));
    }

    /**
     * Set a bitmap to indicate whether some cookies are enabled or not
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_COOKIE_SETTINGS
     * - MegaRequest::getNumDetails - Return a bitmap with cookie settings
     * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     *
     * @param settings A bitmap with cookie settings
     *                 Valid bits are:
     *                 - Bit 0: essential
     *                 - Bit 1: preference
     *                 - Bit 2: analytics
     *                 - Bit 3: ads
     *                 - Bit 4: thirdparty
     */
    public void setCookieSettings(int settings) {
        megaApi.setCookieSettings(settings);
    }

    /**
     * Get a bitmap to indicate whether some cookies are enabled or not
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value USER_ATTR_COOKIE_SETTINGS
     * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumDetails Return the bitmap with cookie settings
     * Valid bits are:
     * - Bit 0: essential
     * - Bit 1: preference
     * - Bit 2: analytics
     * - Bit 3: ads
     * - Bit 4: thirdparty
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EINTERNAL - If the value for cookie settings bitmap was invalid
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getCookieSettings(MegaRequestListenerInterface listener) {
        megaApi.getCookieSettings(createDelegateRequestListener(listener));
    }

    /**
     * Get a bitmap to indicate whether some cookies are enabled or not
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the value USER_ATTR_COOKIE_SETTINGS
     * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNumDetails Return the bitmap with cookie settings
     * Valid bits are:
     * - Bit 0: essential
     * - Bit 1: preference
     * - Bit 2: analytics
     * - Bit 3: ads
     * - Bit 4: thirdparty
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EINTERNAL - If the value for cookie settings bitmap was invalid
     */
    public void getCookieSettings() {
        megaApi.getCookieSettings();
    }

    /**
     * Check if the app can start showing the cookie banner
     * <p>
     * This function will NOT return a valid value until the callback onEvent with
     * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
     * a fetchnodes to check this value, but only when it follows a login with user and password,
     * not when an existing session is resumed.
     * <p>
     * For not logged-in mode, you need to call MegaApi::getMiscFlags first.
     *
     * @return True if this feature is enabled. Otherwise, false.
     */
    public boolean isCookieBannerEnabled() {
        return megaApi.cookieBannerEnabled();
    }

    /**
     * Creates the special folder for backups ("My backups")
     * <p>
     * It creates a new folder inside the Vault rootnode and later stores the node's
     * handle in a user's attribute, MegaApi::USER_ATTR_MY_BACKUPS_FOLDER.
     * <p>
     * Apps should first check if this folder exists already, by calling
     * MegaApi::getUserAttribute for the corresponding attribute.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_SET_MY_BACKUPS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getText - Returns the name provided as parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getNodehandle - Returns the node handle of the folder created
     * <p>
     * If the folder for backups already existed, the request will fail with the error API_EACCESS.
     *
     * @param localizedName Localized name for "My backups" folder
     * @param listener      MegaRequestListener to track this request
     */
    public void setMyBackupsFolder(String localizedName, MegaRequestListenerInterface listener) {
        megaApi.setMyBackupsFolder(localizedName, createDelegateRequestListener(listener));
    }

    /**
     * Request creation of a new Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns INVALID_HANDLE
     * - MegaRequest::getText - Returns name of the Set
     * - MegaRequest::getParamType - Returns CREATE_SET, possibly combined with OPTION_SET_NAME
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSet - Returns either the new Set, or null if it was not created.
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param name     the name that should be given to the new Set
     * @param type     the type of the Set (see MegaSet for possible types)
     * @param listener MegaRequestListener to track this request
     */
    public void createSet(String name, int type, MegaRequestListenerInterface listener) {
        megaApi.createSet(name, type, createDelegateRequestListener(listener));
    }

    /**
     * Request creation of a new Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns INVALID_HANDLE
     * - MegaRequest::getText - Returns name of the Set
     * - MegaRequest::getParamType - Returns CREATE_SET, possibly combined with OPTION_SET_NAME
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSet - Returns either the new Set, or null if it was not created.
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param name the name that should be given to the new Set
     */
    public void createSet(String name) {
        megaApi.createSet(name);
    }

    /**
     * Request to update the name of a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Set to be updated
     * - MegaRequest::getText - Returns new name of the Set
     * - MegaRequest::getParamType - Returns OPTION_SET_NAME
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set with the given id could not be found (before or after the request).
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set to be updated
     * @param name     the new name that should be given to the Set
     * @param listener MegaRequestListener to track this request
     */
    public void updateSetName(long sid, String name, MegaRequestListenerInterface listener) {
        megaApi.updateSetName(sid, name, createDelegateRequestListener(listener));
    }

    /**
     * Request to update the name of a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Set to be updated
     * - MegaRequest::getText - Returns new name of the Set
     * - MegaRequest::getParamType - Returns OPTION_SET_NAME
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set with the given id could not be found (before or after the request).
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid  the id of the Set to be updated
     * @param name the new name that should be given to the Set
     */
    public void updateSetName(long sid, String name) {
        megaApi.updateSetName(sid, name);
    }

    /**
     * Request to update the cover of a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Set to be updated
     * - MegaRequest::getNodeHandle - Returns Element id to be set as the new cover
     * - MegaRequest::getParamType - Returns OPTION_SET_COVER
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS - Given Element id was not part of the current Set; Malformed (from API).
     * - MegaError::API_ENOENT - Set with the given id could not be found (before or after the request).
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set to be updated
     * @param eid      the id of the Element to be set as cover
     * @param listener MegaRequestListener to track this request
     */
    public void putSetCover(long sid, long eid, MegaRequestListenerInterface listener) {
        megaApi.putSetCover(sid, eid, createDelegateRequestListener(listener));
    }

    /**
     * Request to update the cover of a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Set to be updated
     * - MegaRequest::getNodeHandle - Returns Element id to be set as the new cover
     * - MegaRequest::getParamType - Returns OPTION_SET_COVER
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS - Given Element id was not part of the current Set; Malformed (from API).
     * - MegaError::API_ENOENT - Set with the given id could not be found (before or after the request).
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid the id of the Set to be updated
     * @param eid the id of the Element to be set as cover
     */
    public void putSetCover(long sid, long eid) {
        megaApi.putSetCover(sid, eid);
    }

    /**
     * Request to remove a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Set to be removed
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set to be removed
     * @param listener MegaRequestListener to track this request
     */
    public void removeSet(long sid, MegaRequestListenerInterface listener) {
        megaApi.removeSet(sid, createDelegateRequestListener(listener));
    }

    /**
     * Request to remove a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Set to be removed
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid the id of the Set to be removed
     */
    public void removeSet(long sid) {
        megaApi.removeSet(sid);
    }

    /**
     * Request creation of multiple Elements for a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getMegaHandleList - Returns a list containing the file handles corresponding to the new Elements
     * - MegaRequest::getMegaStringList - Returns a list containing the names corresponding to the new Elements
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSetElementList - Returns a list containing only the new Elements
     * - MegaRequest::getMegaIntegerList - Returns a list containing error codes for all requested Elements
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not besetExcludedNames found.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set that will own the new Elements
     * @param nodes    the handles of the file-nodes that will be represented by the new Elements
     * @param names    the names that should be given to the new Elements (param names must be either null or have
     *                 the same size() as param nodes)
     * @param listener MegaRequestListener to track this request
     */
    public void createSetElements(long sid, MegaHandleList nodes, MegaStringList names, MegaRequestListenerInterface listener) {
        megaApi.createSetElements(sid, nodes, names, createDelegateRequestListener(listener));
    }

    /**
     * Request creation of a new Element for a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns INVALID_HANDLE
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getParamType - Returns CREATE_ELEMENT, possibly combined with OPTION_ELEMENT_NAME
     * - MegaRequest::getText - Returns name of the Element
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSetElementList - Returns a list containing only the new Element
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not be found, or node could not be found.
     * - MegaError::API_EKEY - File-node had no key.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set that will own the new Element
     * @param node     the handle of the file-node that will be represented by the new Element
     * @param name     the name that should be given to the new Element
     * @param listener MegaRequestListener to track this request
     */
    public void createSetElement(long sid, long node, String name, MegaRequestListenerInterface listener) {
        megaApi.createSetElement(sid, node, name, createDelegateRequestListener(listener));
    }

    /**
     * Request creation of a new Element for a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns INVALID_HANDLE
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getParamType - Returns CREATE_ELEMENT, possibly combined with OPTION_ELEMENT_NAME
     * - MegaRequest::getText - Returns name of the Element
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSetElementList - Returns a list containing only the new Element
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not be found, or node could not be found.
     * - MegaError::API_EKEY - File-node had no key.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid  the id of the Set that will own the new Element
     * @param node the handle of the file-node that will be represented by the new Element
     * @param name the name that should be given to the new Element
     */
    public void createSetElement(long sid, long node, String name) {
        megaApi.createSetElement(sid, node, name);
    }

    /**
     * Request creation of a new Element for a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns INVALID_HANDLE
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getParamType - Returns CREATE_ELEMENT, possibly combined with OPTION_ELEMENT_NAME
     * - MegaRequest::getText - Returns name of the Element
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSetElementList - Returns a list containing only the new Element
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not be found, or node could not be found.
     * - MegaError::API_EKEY - File-node had no key.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid  the id of the Set that will own the new Element
     * @param node the handle of the file-node that will be represented by the new Element
     */
    public void createSetElement(long sid, long node) {
        megaApi.createSetElement(sid, node);
    }

    /**
     * Request to update the name of an Element
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Element to be updated
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getParamType - Returns OPTION_ELEMENT_NAME
     * - MegaRequest::getText - Returns name of the Element
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Element could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set that owns the Element
     * @param eid      the id of the Element that will be updated
     * @param name     the new name that should be given to the Element
     * @param listener MegaRequestListener to track this request
     */
    public void updateSetElementName(long sid, long eid, String name, MegaRequestListenerInterface listener) {
        megaApi.updateSetElementName(sid, eid, name, createDelegateRequestListener(listener));
    }

    /**
     * Request to update the name of an Element
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Element to be updated
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getParamType - Returns OPTION_ELEMENT_NAME
     * - MegaRequest::getText - Returns name of the Element
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Element could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid  the id of the Set that owns the Element
     * @param eid  the id of the Element that will be updated
     * @param name the new name that should be given to the Element
     */
    public void updateSetElementName(long sid, long eid, String name) {
        megaApi.updateSetElementName(sid, eid, name);
    }

    /**
     * Request to update the order of an Element
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Element to be updated
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getParamType - Returns OPTION_ELEMENT_ORDER
     * - MegaRequest::getNumber - Returns order of the Element
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Element could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set that owns the Element
     * @param eid      the id of the Element that will be updated
     * @param order    the new order of the Element
     * @param listener MegaRequestListener to track this request
     */
    public void updateSetElementOrder(long sid, long eid, long order, MegaRequestListenerInterface listener) {
        megaApi.updateSetElementOrder(sid, eid, order, createDelegateRequestListener(listener));
    }

    /**
     * Request to update the order of an Element
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Element to be updated
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getParamType - Returns OPTION_ELEMENT_ORDER
     * - MegaRequest::getNumber - Returns order of the Element
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Element could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid   the id of the Set that owns the Element
     * @param eid   the id of the Element that will be updated
     * @param order the new order of the Element
     */
    public void updateSetElementOrder(long sid, long eid, long order) {
        megaApi.updateSetElementOrder(sid, eid, order);
    }

    /**
     * Request removal of multiple Elements from a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_SET_ELEMENTS
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * - MegaRequest::getMegaHandleList - Returns a list containing the handles of Elements to be removed
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaIntegerList - Returns a list containing error codes for all Elements intended for removal
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set that will own the new Elements
     * @param eids     the ids of Elements to be removed
     * @param listener MegaRequestListener to track this request
     */
    public void removeSetElements(long sid, MegaHandleList eids, MegaRequestListenerInterface listener) {
        megaApi.removeSetElements(sid, eids, createDelegateRequestListener(listener));
    }

    /**
     * Request to remove an Element
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Element to be removed
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - No Set or no Element with given ids could be found (before or after the request).
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid      the id of the Set that owns the Element
     * @param eid      the id of the Element to be removed
     * @param listener MegaRequestListener to track this request
     */
    public void removeSetElement(long sid, long eid, MegaRequestListenerInterface listener) {
        megaApi.removeSetElement(sid, eid, createDelegateRequestListener(listener));
    }

    /**
     * Request to remove an Element
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_SET_ELEMENT
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns id of the Element to be removed
     * - MegaRequest::getTotalBytes - Returns the id of the Set
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - No Set or no Element with given ids could be found (before or after the request).
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     *
     * @param sid the id of the Set that owns the Element
     * @param eid the id of the Element to be removed
     */
    public void removeSetElement(long sid, long eid) {
        megaApi.removeSetElement(sid, eid);
    }

    /**
     * Get a list of all Sets available for current user.
     * <p>
     * The response value is stored as a MegaSetList.
     * <p>
     * You take the ownership of the returned value
     *
     * @return list of Sets
     */
    public MegaSetList getSets() {
        return megaApi.getSets();
    }

    /**
     * Get the Set with the given id, for current user.
     * <p>
     * The response value is stored as a MegaSet.
     * <p>
     * You take the ownership of the returned value
     *
     * @param sid the id of the Set to be retrieved
     * @return the requested Set, or null if not found
     */
    public MegaSet getSet(long sid) {
        return megaApi.getSet(sid);
    }

    /**
     * Get the cover (Element id) of the Set with the given id, for current user.
     *
     * @param sid the id of the Set to retrieve the cover for
     * @return Element id of the cover, or INVALIDHANDLE if not set or invalid id
     */
    public long getSetCover(long sid) {
        return megaApi.getSetCover(sid);
    }

    /**
     * Get Element count of the Set with the given id, for current user.
     *
     * @param sid the id of the Set to get Element count for
     * @return Element count of requested Set, or 0 if not found
     */
    public long getSetElementCount(long sid) {
        return megaApi.getSetElementCount(sid);
    }

    /**
     * Get Element count of the Set with the given id, for current user.
     *
     * @param sid                         the id of the Set to get Element count for
     * @param includeElementsInRubbishBin consider or filter out Elements in Rubbish Bin
     * @return Element count of requested Set, or 0 if not found
     */
    public long getSetElementCount(long sid, boolean includeElementsInRubbishBin) {
        return megaApi.getSetElementCount(sid, includeElementsInRubbishBin);
    }

    /**
     * Get all Elements in the Set with given id, for current user.
     * <p>
     * The response value is stored as a MegaSetElementList.
     * <p>
     * You take the ownership of the returned value
     *
     * @param sid the id of the Set owning the Elements
     * @return all Elements in that Set, or null if not found or none added
     */
    public MegaSetElementList getSetElements(long sid) {
        return megaApi.getSetElements(sid);
    }

    /**
     * Get all Elements in the Set with given id, for current user.
     * <p>
     * The response value is stored as a MegaSetElementList.
     * <p>
     * You take the ownership of the returned value
     *
     * @param sid                         the id of the Set owning the Elements
     * @param includeElementsInRubbishBin includeElementsInRubbishBin consider or filter out Elements in Rubbish Bin
     * @return all Elements in that Set, or null if not found or none added
     */
    public MegaSetElementList getSetElements(long sid, boolean includeElementsInRubbishBin) {
        return megaApi.getSetElements(sid, includeElementsInRubbishBin);
    }

    /**
     * Get a particular Element in a particular Set, for current user.
     * <p>
     * The response value is stored as a MegaSetElement.
     * <p>
     * You take the ownership of the returned value
     *
     * @param sid the id of the Set owning the Element
     * @param eid the id of the Element to be retrieved
     * @return requested Element, or null if not found
     */
    public MegaSetElement getSetElement(long sid, long eid) {
        return megaApi.getSetElement(sid, eid);
    }

    /**
     * Start a Sync or Backup between a local folder and a folder in MEGA
     *
     * This function should be used to add a new synchronization/backup task for the MegaApi.
     * To resume a previously configured task folder, use MegaApi::enableSync.
     *
     * Both TYPE_TWOWAY and TYPE_BACKUP are supported for the first parameter.
     *
     * The sync/backup's name is optional. If not provided, it will take the name of the leaf folder of
     * the local path. In example, for "/home/user/Documents", it will become "Documents".
     *
     * The remote sync root folder should be INVALID_HANDLE for syncs of TYPE_BACKUP. The handle of the
     * remote node, which is created as part of this request, will be set to the MegaRequest::getNodeHandle.
     *
     * The associated request type with this request is MegaRequest::TYPE_ADD_SYNC
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
     * - MegaRequest::getFile - Returns the path of the local folder
     * - MegaRequest::getName - Returns the name of the sync
     * - MegaRequest::getParamType - Returns the type of the sync
     * - MegaRequest::getLink - Returns the drive root if external backup
     * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
     * - MegaRequest::getNumDetails - If different than NO_SYNC_ERROR, it returns additional info for
     * the  specific sync error (MegaSync::Error). It could happen both when the request has succeeded (API_OK) and
     * also in some cases of failure, when the request error is not accurate enough.
     *
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is other than MegaError::API_OK:
     * - MegaRequest::getNumber - Fingerprint of the local folder. Note, fingerprint will only be valid
     * if the sync was added with no errors
     * - MegaRequest::getParentHandle - Returns the sync backupId
     *
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EARGS - If the local folder was not set or is not a folder.
     * - MegaError::API_EACCESS - If the user was invalid, or did not have an attribute for "My Backups" folder,
     * or the attribute was invalid, or "My Backups"/`DEVICE_NAME` existed but was not a folder, or it had the
     * wrong 'dev-id'/'drv-id' tag.
     * - MegaError::API_EINTERNAL - If the user attribute for "My Backups" folder did not have a record containing
     * the handle.
     * - MegaError::API_ENOENT - If the handle of "My Backups" folder contained in the user attribute was invalid
     * - or the node could not be found.
     * - MegaError::API_EINCOMPLETE - If device id was not set, or if current user did not have an attribute for
     * device name, or the attribute was invalid, or the attribute did not contain a record for the device name,
     * or device name was empty.
     * - MegaError::API_EEXIST - If this is a new device, but a folder with the same device-name already exists.
     *
     * @param syncType Type of sync. Currently supported: TYPE_TWOWAY and TYPE_BACKUP.
     * @param localSyncRootFolder Path of the Local folder to sync/backup.
     * @param name Name given to the sync. You can pass NULL, and the folder name will be used instead.
     * @param remoteSyncRootFolder Handle of MEGA folder. If you have a MegaNode for that folder, use its getHandle()
     * @param driveRootIfExternal Only relevant for backups, and only if the backup is on an external disk. Otherwise use NULL.
     * @param listener MegaRequestListener to track this request
     */
    public void syncFolder(
            MegaSync.SyncType syncType,
            String localSyncRootFolder,
            String name,
            long remoteSyncRootFolder,
            String driveRootIfExternal,
            MegaRequestListenerInterface listener
    ) {
        megaApi.syncFolder(
                syncType,
                localSyncRootFolder,
                name,
                remoteSyncRootFolder,
                driveRootIfExternal,
                createDelegateRequestListener(listener, false)
        );
    }

    /**
     * Get all configured syncs
     *
     * You take the ownership of the returned value
     *
     * @return List of MegaSync objects with all syncs
     */
    public MegaSyncList getSyncs() {
        return megaApi.getSyncs();
    }

    /**
     * De-configure the sync/backup of a folder
     *
     * The folder will stop being synced. No files in the local nor in the remote folder
     * will be deleted due to the usage of this function.
     *
     * The synchronization will stop and the local sync database will be deleted
     * The backupId of this sync will be invalid going forward.
     *
     * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNC
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParentHandle - Returns sync backupId
     * - MegaRequest::getFlag - Returns true
     * - MegaRequest::getFile - Returns the path of the local folder (for active syncs only)
     *
     * @param backupId Identifier of the Sync (unique per user, provided by API)
     * @param listener MegaRequestListener to track this request
     */
    public void removeSync(long backupId) {
        megaApi.removeSync(backupId);
    }

    /**
     * Resumes all sync folder pairs
     */
    public void resumeAllSyncs() {
        MegaSyncList syncs = megaApi.getSyncs();
        int syncsSize = syncs.size();

        for (int i = 0; i < syncsSize; i++) {
            MegaSync sync = syncs.get(i);
            megaApi.setSyncRunState(sync.getBackupId(), RUNSTATE_RUNNING);
        }
    }

    /**
     * Pauses all sync folder pairs
     */
    public void pauseAllSyncs() {
        MegaSyncList syncs = megaApi.getSyncs();
        int syncsSize = syncs.size();

        for (int i = 0; i < syncsSize; i++) {
            MegaSync sync = syncs.get(i);
            megaApi.setSyncRunState(sync.getBackupId(), RUNSTATE_PAUSED);
        }
    }

    /**
     * Returns true if the Set has been exported (has a public link)
     * <p>
     * Public links are created by calling MegaApi::exportSet
     *
     * @param sid the id of the Set to check
     * @return true if param sid is an exported Set
     */
    public boolean isExportedSet(long sid) {
        return megaApi.isExportedSet(sid);
    }

    /**
     * Generate a public link of a Set in MEGA
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns id of the Set used as parameter
     * - MegaRequest::getFlag - Returns a boolean set to true representing the call was
     * meant to enable/create the export
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSet - MegaSet including the public id
     * - MegaRequest::getLink - Public link
     * <p>
     * MegaError::API_OK results in onSetsUpdate being triggered as well
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param sid      MegaHandle to get the public link
     * @param listener MegaRequestListener to track this request
     */
    public void exportSet(long sid, MegaRequestListenerInterface listener) {
        megaApi.exportSet(sid, createDelegateRequestListener(listener));
    }

    /**
     * Stop sharing a Set
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_EXPORT_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getNodeHandle - Returns id of the Set used as parameter
     * - MegaRequest::getFlag - Returns a boolean set to false representing the call was
     * meant to disable the export
     * <p>
     * MegaError::API_OK results in onSetsUpdate being triggered as well
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param sid      Set MegaHandle to stop sharing
     * @param listener MegaRequestListener to track this request
     */
    public void disableExportSet(long sid, MegaRequestListenerInterface listener) {
        megaApi.disableExportSet(sid, createDelegateRequestListener(listener));
    }

    /**
     * Request to fetch a public/exported Set and its Elements.
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_FETCH_SET
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getLink - Returns the link used for the public Set fetch request
     * <p>
     * In addition to fetching the Set (including Elements), SDK's instance is set
     * to preview mode for the public Set. This mode allows downloading of foreign
     * SetElements included in the public Set.
     * <p>
     * To disable the preview mode and release resources used by the preview Set,
     * use MegaApi::stopPublicSetPreview
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getMegaSet - Returns the Set
     * - MegaRequest::getMegaSetElementList - Returns the list of Elements
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - Set could not be found.
     * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
     * - MegaError::API_EARGS - Malformed (from API).
     * - MegaError::API_EACCESS - Permissions Error (from API).
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     * <p>
     *
     * @param publicSetLink Public link to a Set in MEGA
     * @param listener      MegaRequestListener to track this request
     */
    public void fetchPublicSet(String publicSetLink, MegaRequestListenerInterface listener) {
        megaApi.fetchPublicSet(publicSetLink, createDelegateRequestListener(listener));
    }

    /**
     * Stops public Set preview mode for current SDK instance
     * <p>
     * MegaApi instance is no longer useful until a new login
     */
    public void stopPublicSetPreview() {
        megaApi.stopPublicSetPreview();
    }

    /**
     * Returns if this MegaApi instance is in a public/exported Set preview mode
     *
     * @return True if public Set preview mode is enabled
     */
    public boolean inPublicSetPreview() {
        return megaApi.inPublicSetPreview();
    }


    /**
     * Get current public/exported Set in Preview mode
     * <p>
     * The response value is stored as a MegaSet.
     * <p>
     * You take the ownership of the returned value
     *
     * @return Current public/exported Set in preview mode or nullptr if there is none
     */
    @Nullable
    public MegaSet getPublicSetInPreview() {
        return megaApi.getPublicSetInPreview();
    }


    /**
     * Get current public/exported SetElements in Preview mode
     * <p>
     * The response value is stored as a MegaSetElementList.
     * <p>
     * You take the ownership of the returned value
     *
     * @return Current public/exported SetElements in preview mode or nullptr if there is none
     */
    public MegaSetElementList getPublicSetElementsInPreview() {
        return megaApi.getPublicSetElementsInPreview();
    }

    /**
     * Gets a MegaNode for the foreign MegaSetElement that can be used to download the Element
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_EXPORTED_SET_ELEMENT
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError::API_OK:
     * - MegaRequest::getPublicMegaNode - Returns the MegaNode (ownership transferred)
     * <p>
     * On the onRequestFinish error, the error code associated to the MegaError can be:
     * - MegaError::API_EACCESS - Public Set preview mode is not enabled
     * - MegaError::API_EARGS - MegaHandle for SetElement provided as param doesn't match any Element
     * in previewed Set
     * <p>
     * If the MEGA account is a business account and it's status is expired, onRequestFinish will
     * be called with the error code MegaError::API_EBUSINESSPASTDUE.
     *
     * @param eid      MegaHandle of target SetElement from Set in preview mode
     * @param listener MegaRequestListener to track this request
     */
    public void getPreviewElementNode(long eid, MegaRequestListenerInterface listener) {
        megaApi.getPreviewElementNode(eid, createDelegateRequestListener(listener));
    }


    /**
     * Gets a MegaNode for the foreign MegaSetElement that can be used to download the Element
     *
     * @param sid MegaHandle of target Set to get its public link/URL
     * @return const char* with the public URL if success, nullptr otherwise
     * In any case, one of the followings error codes with the result can be found in the log:
     * - API_OK on success
     * - API_ENOENT if sid doesn't match any owned Set or the Set is not exported
     * - API_EARGS if there was an internal error composing the URL
     */
    @Nullable
    public String getPublicLinkForExportedSet(long sid) {
        return megaApi.getPublicLinkForExportedSet(sid);
    }

    /**
     * Get the list of IDs for enabled notifications
     * <p>
     * You take the ownership of the returned value
     *
     * @return List of IDs for enabled notifications
     */
    public MegaIntegerList getEnabledNotifications() {
        return megaApi.getEnabledNotifications();
    }

    /**
     * Get list of available notifications for Notification Center
     * <p>
     * The associated request type with this request is MegaRequest::TYPE_GET_NOTIFICATIONS.
     * <p>
     * When onRequestFinish received MegaError::API_OK, valid data in the MegaRequest object is:
     * - MegaRequest::getMegaNotifications - Returns the list of notifications
     * <p>
     * When onRequestFinish errored, the error code associated to the MegaError can be:
     * - MegaError::API_ENOENT - No such notifications exist, and MegaRequest::getMegaNotifications
     * will return a non-null, empty list.
     * - MegaError::API_EACCESS - No user was logged in.
     * - MegaError::API_EINTERNAL - Received answer could not be read.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getNotifications(MegaRequestListenerInterface listener) {
        megaApi.getNotifications(createDelegateRequestListener(listener));
    }

    /**
     * Set last read notification for Notification Center
     * <p>
     * The type associated with this request is MegaRequest::TYPE_SET_ATTR_USER
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_LAST_READ_NOTIFICATION
     * - MegaRequest::getNumber - Returns the ID to be set as last read
     * <p>
     * Note that any notifications with ID equal to or less than the given one will be marked as seen
     * in Notification Center.
     *
     * @param notificationId ID of the notification to be set as last read. Value `0` is an invalid ID.
     *                       Passing `0` will clear a previously set last read value.
     * @param listener       MegaRequestListener to track this request
     */
    public void setLastReadNotification(long notificationId, MegaRequestListenerInterface listener) {
        megaApi.setLastReadNotification(notificationId, createDelegateRequestListener(listener));
    }

    /**
     * Get last read notification for Notification Center
     * <p>
     * The type associated with this request is MegaRequest::TYPE_GET_ATTR_USER
     * <p>
     * Valid data in the MegaRequest object received on callbacks:
     * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_LAST_READ_NOTIFICATION
     * <p>
     * When onRequestFinish received MegaError::API_OK, valid data in the MegaRequest object is:
     * - MegaRequest::getNumber - Returns the ID of the last read Notification
     * Note that when the ID returned here was `0` it means that no ID was set as last read.
     * Note that the value returned here should be treated like a 32bit unsigned int.
     *
     * @param listener MegaRequestListener to track this request
     */
    public void getLastReadNotification(MegaRequestListenerInterface listener) {
        megaApi.getLastReadNotification(createDelegateRequestListener(listener));
    }

    /**
     * Get the type and value for the flag with the given name,
     * if present among either A/B Test or Feature flags.
     *
     * If found among A/B Test flags and commit was true, also inform the API
     * that a user has become relevant for that A/B Test flag, in which case
     * the associated request type with this request is MegaRequest::TYPE_AB_TEST_ACTIVE
     * and valid data in the MegaRequest object received on all callbacks:
     * - MegaRequest::getText - Returns the flag passed as parameter
     *
     * @param flagName Name or key of the value to be retrieved (and possibly be sent to API as active).
     * @param commit Determine whether an A/B Test flag will be sent to API as active.
     * @param listener MegaRequestListener to track this request, ignored if commit was false
     *
     * @return A MegaFlag instance with the type and value of the flag.
     */
    public MegaFlag getFlag(String flagName, Boolean commit, MegaRequestListenerInterface listener) {
       return megaApi.getFlag(flagName, commit, createDelegateRequestListener(listener));
    }

    /**
     * Initiate an asynchronous request to receive stalled issues.
     *
     * Use MegaRequestListenerInterface to subscribe for result.
     * Result is of MegaRequest.TYPE_GET_SYNC_STALL_LIST type.
     */
    public void requestMegaSyncStallList(MegaRequestListenerInterface listener) {
        megaApi.getMegaSyncStallList(createDelegateRequestListener(listener));
    }

    /**
     * Find out if the syncs need User intervention for some files/folders
     *
     * use getMegaSyncStallList() to find out what needs attention.
     *
     * @return true if the User is needs to intervene.
     *
     */
    public boolean isSyncStalled() {
        return megaApi.isSyncStalled();
    }

    /**
     * Resume a previously suspended sync
     */
    public void resumeSync(long backupId) {
        megaApi.setSyncRunState(backupId, RUNSTATE_RUNNING);
    }

    /**
     * Suspend a sync
     * <p>
     * Use this method to pause a running Sync. The sync can be resumed later by calling MegaApi::resumeSync.
     */
    public void pauseSync(long backupId) {
        megaApi.setSyncRunState(backupId, RUNSTATE_SUSPENDED);
    }

    /**
     * Check if it's possible to start synchronizing a folder node. Return SyncError errors.
     *
     * Possible return values for this function are:
     * - MegaError::API_OK if the folder is syncable
     * - MegaError::API_ENOENT if the node doesn't exist in the account
     * - MegaError::API_EARGS if the node is NULL or is not a folder
     *
     * - MegaError::API_EACCESS:
     *              SyncError: SHARE_NON_FULL_ACCESS An ancestor node does not have full access
     *              SyncError: REMOTE_NODE_INSIDE_RUBBISH
     * - MegaError::API_EEXIST if there is a conflicting synchronization (nodes can't be synced twice)
     *              SyncError: ACTIVE_SYNC_BELOW_PATH - There's a synced node below the path to be synced
     *              SyncError: ACTIVE_SYNC_ABOVE_PATH - There's a synced node above the path to be synced
     *              SyncError: ACTIVE_SYNC_SAME_PATH - There's a synced node at the path to be synced
     * - MegaError::API_EINCOMPLETE if the SDK hasn't been built with support for synchronization
     *
     *  @return API_OK if syncable. Error otherwise sets syncError in the returned MegaError
     *          caller must free
     */
    public MegaError isNodeSyncableWithError(MegaNode megaNode) {
        return megaApi.isNodeSyncableWithError(megaNode);
    }

}
