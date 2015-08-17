package nz.mega.sdk;

import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.Set;

/**
 * Java Application Programming Interface (API) to access MEGA SDK services on a MEGA account or shared public folder.
 * <p>
 * An appKey must be specified to use the MEGA SDK. Generate an appKey for free here:
 * - https://mega.co.nz/#sdk
 * <p>
 * Save on data usage and start up time by enabling local node caching. This can be enabled by passing a local path
 * in the constructor. Local node caching prevents the need to download the entire file system each time the MegaApiJava
 * object is logged in.
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
 * After using MegaApiJava.logout you can reuse the same MegaApi object to log in to another MEGA account or a public
 * folder.
 */
public class MegaApiJava {
    MegaApi megaApi;
    MegaGfxProcessor gfxProcessor;
    static DelegateMegaLogger logger;

    void runCallback(Runnable runnable) {
        runnable.run();
    }

    static Set<DelegateMegaRequestListener> activeRequestListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaRequestListener>());
    static Set<DelegateMegaTransferListener> activeTransferListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTransferListener>());
    static Set<DelegateMegaGlobalListener> activeGlobalListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaGlobalListener>());
    static Set<DelegateMegaListener> activeMegaListeners = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaListener>());
    static Set<DelegateMegaTreeProcessor> activeMegaTreeProcessors = Collections.synchronizedSet(new LinkedHashSet<DelegateMegaTreeProcessor>());

    // Order options for getChildren
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

    public final static int LOG_LEVEL_FATAL = 0; // Very severe error event that will presumably lead the application to abort.
    public final static int LOG_LEVEL_ERROR = LOG_LEVEL_FATAL + 1; // Error information but application will continue run.
    public final static int LOG_LEVEL_WARNING = LOG_LEVEL_ERROR + 1; // Information representing errors in application but application will keep
                                                                     // running
    public final static int LOG_LEVEL_INFO = LOG_LEVEL_WARNING + 1; // Mainly useful to represent current progress of application.
    public final static int LOG_LEVEL_DEBUG = LOG_LEVEL_INFO + 1; // Informational logs, that are useful for developers. Only applicable if DEBUG is
                                                                  // defined.
    public final static int LOG_LEVEL_MAX = LOG_LEVEL_DEBUG + 1;

    public final static int EVENT_FEEDBACK = 0;
    public final static int EVENT_DEBUG = EVENT_FEEDBACK + 1;
    public final static int EVENT_INVALID = EVENT_DEBUG + 1;

    /**
     * Constructor suitable for most applications.
     * 
     * @param appKey
     *            AppKey of your application.
     *            Generate an AppKey for free here: https://mega.co.nz/#sdk
     * 
     * @param basePath
<<<<<<< HEAD
     *            Base path to store the local cache.
     *            If you pass NULL to this parameter, the SDK won't use any local cache.
=======
     *            Base path to store the local cache
     *            If you pass `null` to this parameter, the SDK won't use any local cache.
>>>>>>> master
     * 
     */
    public MegaApiJava(String appKey, String basePath) {
        megaApi = new MegaApi(appKey, basePath);
    }

    /**
     * MegaApi Constructor that allows to use a custom GFX processor.
     * The SDK attach thumbnails and previews to all uploaded images. To generate them, it needs a graphics processor.
     * You can build the SDK with one of the provided built-in graphics processors. If none are available
     * in your app, you can implement the MegaGfxProcessor interface to provide a custom processor. Please
     * read the documentation of MegaGfxProcessor carefully to ensure that your implementation is valid.
     * 
     * @param appKey
     *            AppKey of your application.
     *            Generate an AppKey for free here: https://mega.co.nz/#sdk
     * 
     * @param userAgent
<<<<<<< HEAD
     *            User agent to use in network requests.
     *            If you pass NULL to this parameter, a default user agent will be used.
     * 
     * @param basePath
     *            Base path to store the local cache.
     *            If you pass NULL to this parameter, the SDK won't use any local cache.
     * 
     * @param gfxProcessor
     *            Image processor. The SDK will use it to generate previews and thumbnails.
     *            If you pass NULL to this parameter, the SDK will try to use the built-in image processors.
=======
     *            User agent to use in network requests
     *            If you pass `null` to this parameter, a default user agent will be used
     * 
     * @param basePath
     *            Base path to store the local cache
     *            If you pass `null` to this parameter, the SDK won't use any local cache.
     * 
     * @param gfxProcessor
     *            Image processor. The SDK will use it to generate previews and thumbnails
     *            If you pass `null` to this parameter, the SDK will try to use the built-in image processors.
>>>>>>> master
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
     * 
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
     *            Listener that will receive all events (requests, transfers, global, synchronization)
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
     *            Listener that will receive all events about requests
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
     *            Listener that will receive all events about transfers
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
     *            Listener that will receive global events
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
     *            Object that is unregistered
     */
    public void removeListener(MegaListenerInterface listener) {
        synchronized (activeMegaListeners) {
            Iterator<DelegateMegaListener> it = activeMegaListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                    megaApi.removeListener(delegate);
                    it.remove();
                }
            }
        }
    }

    /**
     * Unregister a MegaRequestListener.
     * <p>
     * Stop receiving events from the specified listener.
     * 
     * @param listener
     *            Object that is unregistered
     */
    public void removeRequestListener(MegaRequestListenerInterface listener) {
        synchronized (activeRequestListeners) {
            Iterator<DelegateMegaRequestListener> it = activeRequestListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaRequestListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                    megaApi.removeRequestListener(delegate);
                    it.remove();
                }
            }
        }
    }

    /**
     * Unregister a MegaTransferListener.
     * <p>
     * Stop receiving events from the specified listener.
     * 
     * @param listener
     *            Object that is unregistered
     */
    public void removeTransferListener(MegaTransferListenerInterface listener) {
        synchronized (activeTransferListeners) {
            Iterator<DelegateMegaTransferListener> it = activeTransferListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaTransferListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                    megaApi.removeTransferListener(delegate);
                    it.remove();
                }
            }
        }
    }

    /**
     * Unregister a MegaGlobalListener.
     * <p>
     * Stop receiving events from the specified listener.
     * 
     * @param listener
     *            Object that is unregistered
     */
    public void removeGlobalListener(MegaGlobalListenerInterface listener) {
        synchronized (activeGlobalListeners) {
            Iterator<DelegateMegaGlobalListener> it = activeGlobalListeners.iterator();
            while (it.hasNext()) {
                DelegateMegaGlobalListener delegate = it.next();
                if (delegate.getUserListener() == listener) {
                    megaApi.removeGlobalListener(delegate);
                    it.remove();
                }
            }
        }
    }

    /****************************************************************************************************/
    // UTILS
    /****************************************************************************************************/

    /**
     * Generates a private key based on the access password.
     * <p>
     * This is a time consuming operation (particularly for low-end mobile devices). As the resulting key is
     * required to log in, this function allows to do this step in a separate function. You should run this function
     * in a background thread, to prevent UI hangs. The resulting key can be used in MegaApiJava.fastLogin().
     * 
     * @param password
     *            Access password
     * @return Base64-encoded private key
     * @deprecated Legacy function soon to be removed.
     */
    @Deprecated public String getBase64PwKey(String password) {
        return megaApi.getBase64PwKey(password);
    }

    /**
     * Generates a hash based in the provided private key and email.
     * <p>
     * This is a time consuming operation (especially for low-end mobile devices). Since the resulting key is
     * required to log in, this function allows to do this step in a separate function. You should run this function
     * in a background thread, to prevent UI hangs. The resulting key can be used in MegaApiJava.fastLogin().
     * 
     * @param base64pwkey
     *            Private key returned by MegaApiJava.getBase64PwKey()
     * @return Base64-encoded hash
     * @deprecated Legacy function soon to be removed.
     */
    @Deprecated public String getStringHash(String base64pwkey, String inBuf) {
        return megaApi.getStringHash(base64pwkey, inBuf);
    }

    /**
     * Converts a Base32-encoded user handle (JID) to a MegaHandle.
     * <p>
     * @param base32Handle
     *            Base32-encoded handle (JID)
     * @return User handle
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
     *            Base64-encoded node handle
     * @return Node handle
     */
    public static long base64ToHandle(String base64Handle) {
        return MegaApi.base64ToHandle(base64Handle);
    }

    /**
     * Converts a MegaHandle to a Base64-encoded string.
     * <p>
     * You can revert this operation using MegaApiJava.base64ToHandle().
     * 
     * @param handle
     *            to be converted
     * @return Base64-encoded node handle
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
     * @param User
     *            handle to be converted
     * @return Base64-encoded user handle
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
     *            Byte array with random data
     * @param size
     *            Size of the byte array (in bytes)
     */
    public static void addEntropy(String data, long size) {
        MegaApi.addEntropy(data, size);
    }

    /**
     * Reconnect and retry all transfers.
     * <p>
     * @param listener
     *            MegaRequestListener to track this request
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

    /****************************************************************************************************/
    // REQUESTS
    /****************************************************************************************************/

    /**
     * Log in to a MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail - Returns the first parameter. <br>
     * - MegaRequest.getPassword - Returns the second parameter.
     * <p>
     * If the email/password are not valid the error code provided in onRequestFinish is
     * MegaError.API_ENOENT.
     * 
     * @param email
     *            Email of the user
     * @param password
     *            Password
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void login(String email, String password, MegaRequestListenerInterface listener) {
        megaApi.login(email, password, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account.
     * <p>
     * @param email
     *            Email of the user
     * @param password
     *            Password
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
     * - MegaRequest.getEmail - Retuns the string "FOLDER" <br>
     * - MegaRequest.getLink - Returns the public link to the folder
     * 
     * @param Public
     *            link to a folder in MEGA
     * @param listener
     *            MegaRequestListener to track this request
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
     * @param Public
     *            link to a folder in MEGA
     */
    public void loginToFolder(String megaFolderLink) {
        megaApi.loginToFolder(megaFolderLink);
    }

    /**
     * Log in to a MEGA account using precomputed keys.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail - Returns the first parameter <br>
     * - MegaRequest.getPassword - Returns the second parameter <br>
     * - MegaRequest.getPrivateKey - Returns the third parameter
     * <p>
     * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
     * MegaError.API_ENOENT.
     * 
     * @param email
     *            Email of the user
     * @param stringHash
     *            Hash of the email returned by MegaApiJava.getStringHash()
     * @param base64pwkey
     *            Private key calculated using MegaApiJava.getBase64PwKey()
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void fastLogin(String email, String stringHash, String base64pwkey, MegaRequestListenerInterface listener) {
        megaApi.fastLogin(email, stringHash, base64pwkey, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account using precomputed keys.
     * 
     * @param email
     *            Email of the user
     * @param stringHash
     *            Hash of the email returned by MegaApiJava.getStringHash()
     * @param base64pwkey
     *            Private key calculated using MegaApiJava.getBase64PwKey()
     */
    public void fastLogin(String email, String stringHash, String base64pwkey) {
        megaApi.fastLogin(email, stringHash, base64pwkey);
    }

    /**
     * Log in to a MEGA account using a session key.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_LOGIN.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getSessionKey - Returns the session key
     * 
     * @param session
     *            Session key previously dumped with MegaApiJava.dumpSession()
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void fastLogin(String session, MegaRequestListenerInterface listener) {
        megaApi.fastLogin(session, createDelegateRequestListener(listener));
    }

    /**
     * Log in to a MEGA account using a session key.
     * 
     * @param session
     *            Session key previously dumped with MegaApiJava.dumpSession()
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
     * @param Handle
     *            of the session. Use mega.INVALID_HANDLE to cancel all sessions except the current one
     * @param listener
     *            MegaRequestListenerInterface to track this request
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
     * @param Handle
     *            of the session. Use mega.INVALID_HANDLE to cancel all sessions except the current one
     */
    public void killSession(long sessionHandle) {
        megaApi.killSession(sessionHandle);
    }

    /**
     * Get data about the logged account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_USER_DATA.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getName - Returns the name of the logged user <br>
     * - MegaRequest.getPassword - Returns the the public RSA key of the account, Base64-encoded <br>
     * - MegaRequest.getPrivateKey - Returns the private RSA key of the account, Base64-encoded
     * 
     * @param listener
     *            MegaRequestListenerInterface to track this request
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
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getText - Returns the XMPP ID of the contact <br>
     * - MegaRequest.getPassword - Returns the public RSA key of the contact, Base64-encoded
     * 
     * @param user
     *            Contact to get the data
     * @param listener
     *            MegaRequestListenerInterface to track this request
     */
    public void getUserData(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.getUserData(user, createDelegateRequestListener(listener));
    }

    /**
     * Get data about a contact.
     * 
     * @param user
     *            Contact to get the data
     */
    public void getUserData(MegaUser user) {
        megaApi.getUserData(user);
    }

    /**
     * Get data about a contact.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_USER_DATA.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail - Returns the email or the Base64 handle of the contact
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getText - Returns the XMPP ID of the contact <br>
     * - MegaRequest.getPassword - Returns the public RSA key of the contact, Base64-encoded
     * 
     * @param user
     *            Email or Base64 handle of the contact
     * @param listener
     *            MegaRequestListenerInterface to track this request
     */
    public void getUserData(String user, MegaRequestListenerInterface listener) {
        megaApi.getUserData(user, createDelegateRequestListener(listener));
    }

    /**
     * Get data about a contact.
     * 
     * @param user
     *            Email or Base64 handle of the contact
     */
    public void getUserData(String user) {
        megaApi.getUserData(user);
    }

    /**
     * Returns the current session key.
     * <p>
     * You have to be logged in to get a valid session key. Otherwise,
     * this function returns `null`.
     * 
     * @return Current session key
     */
    public String dumpSession() {
        return megaApi.dumpSession();
    }

    /**
     * Returns the current XMPP session key.
     * <p>
     * You have to be logged in to get a valid session key. Otherwise,
     * this function returns `null`.
     * 
     * @return Current XMPP session key
     */
    public String dumpXMPPSession() {
        return megaApi.dumpXMPPSession();
    }

    /**
     * Initialize the creation of a new MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail - Returns the email for the account <br>
     * - MegaRequest.getPassword - Returns the password for the account <br>
     * - MegaRequest.getName - Returns the name of the user <br>
     * <p>
     * If this request succeed, a confirmation email will be sent to the users.
     * If an account with the same email already exists, you will get the error code
     * MegaError.API_EEXIST in onRequestFinish.
     * 
     * @param email
     *            Email for the account
     * @param password
     *            Password for the account
     * @param name
     *            Name of the user
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void createAccount(String email, String password, String name, MegaRequestListenerInterface listener) {
        megaApi.createAccount(email, password, name, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the creation of a new MEGA account.
     * 
     * @param email
     *            Email for the account
     * @param password
     *            Password for the account
     * @param name
     *            Name of the user
     */
    public void createAccount(String email, String password, String name) {
        megaApi.createAccount(email, password, name);
    }

    /**
     * Initialize the creation of a new MEGA account with precomputed keys.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CREATE_ACCOUNT.
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail - Returns the email for the account <br>
     * - MegaRequest.getPrivateKey - Returns the private key calculated with MegaApiJava.getBase64PwKey() <br>
     * - MegaRequest.getName - Returns the name of the user
     * <p>
     * If this request succeed, a confirmation email will be sent to the users.
     * If an account with the same email already exists, you will get the error code
     * MegaError.API_EEXIST in onRequestFinish.
     * 
     * @param email
     *            Email for the account
     * @param base64pwkey
     *            Private key calculated with MegMegaApiJavaaApi.getBase64PwKey()
     * @param name
     *            Name of the user
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void fastCreateAccount(String email, String base64pwkey, String name, MegaRequestListenerInterface listener) {
        megaApi.fastCreateAccount(email, base64pwkey, name, createDelegateRequestListener(listener));
    }

    /**
     * Initialize the creation of a new MEGA account with precomputed keys.
     *
     * @param email
     *            Email for the account
     * @param base64pwkey
     *            Private key calculated with MegaApiJava.getBase64PwKey()
     * @param name
     *            Name of the user
     */
    public void fastCreateAccount(String email, String base64pwkey, String name) {
        megaApi.fastCreateAccount(email, base64pwkey, name);
    }

    /**
     * Get information about a confirmation link.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_QUERY_SIGNUP_LINK.
     * Valid data in the MegaRequest object received on all callbacks: <br>
     * - MegaRequest.getLink - Returns the confirmation link
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getEmail - Return the email associated with the confirmation link <br>
     * - MegaRequest.getName - Returns the name associated with the confirmation link
     * 
     * @param link
     *            Confirmation link
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void querySignupLink(String link, MegaRequestListenerInterface listener) {
        megaApi.querySignupLink(link, createDelegateRequestListener(listener));
    }

    /**
     * Get information about a confirmation link.
     * 
     * @param link
     *            Confirmation link
     */
    public void querySignupLink(String link) {
        megaApi.querySignupLink(link);
    }

    /**
     * Confirm a MEGA account using a confirmation link and the user password.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CONFIRM_ACCOUNT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getLink - Returns the confirmation link <br>
     * - MegaRequest.getPassword - Returns the password
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getEmail - Email of the account <br>
     * - MegaRequest.getName - Name of the user
     * 
     * @param link
     *            Confirmation link
     * @param password
     *            Password for the account
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void confirmAccount(String link, String password, MegaRequestListenerInterface listener) {
        megaApi.confirmAccount(link, password, createDelegateRequestListener(listener));
    }

    /**
     * Confirm a MEGA account using a confirmation link and the user password.
     * 
     * @param link
     *            Confirmation link
     * @param password
     *            Password for the account
     */
    public void confirmAccount(String link, String password) {
        megaApi.confirmAccount(link, password);
    }

    /**
     * Confirm a MEGA account using a confirmation link and a precomputed key.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CONFIRM_ACCOUNT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getLink - Returns the confirmation link <br>
     * - MegaRequest.getPrivateKey - Returns the base64pwkey parameter
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getEmail - Email of the account <br>
     * - MegaRequest.getName - Name of the user
     * 
     * @param link
     *            Confirmation link
     * @param base64pwkey
     *            Private key precomputed with MegaApiJava.getBase64PwKey()
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void fastConfirmAccount(String link, String base64pwkey, MegaRequestListenerInterface listener) {
        megaApi.fastConfirmAccount(link, base64pwkey, createDelegateRequestListener(listener));
    }

    /**
     * Confirm a MEGA account using a confirmation link and a precomputed key.
     * 
     * @param link
     *            Confirmation link
     * @param base64pwkey
     *            Private key precomputed with MegaApiJava.getBase64PwKey()
     */
    public void fastConfirmAccount(String link, String base64pwkey) {
        megaApi.fastConfirmAccount(link, base64pwkey);
    }

    /**
     * Set proxy settings.
     * <p>
     * The SDK will start using the provided proxy settings as soon as this function returns.
     * 
     * @param Proxy
     *            settings
     * @see MegaProxy
     */
    public void setProxySettings(MegaProxy proxySettings) {
        megaApi.setProxySettings(proxySettings);
    }

    /**
     * Try to detect the system's proxy settings.
     * 
     * Automatic proxy detection is currently supported on Windows only.
     * On other platforms, this fuction will return a MegaProxy object
     * of type MegaProxy.PROXY_NONE.
     * 
     * @return MegaProxy object with the detected proxy settings
     */
    public MegaProxy getAutoProxySettings() {
        return megaApi.getAutoProxySettings();
    }

    /**
     * Check if the MegaApi object is logged in.
     * 
     * @return 0 if not logged in, Otherwise, a number >= 0
     */
    public int isLoggedIn() {
        return megaApi.isLoggedIn();
    }

    /**
     * Retuns the email of the currently open account.
     * 
     * If the MegaApi object isn't logged in or the email isn't available,
<<<<<<< HEAD
     * this function returns NULL.
=======
     * this function returns `null`
>>>>>>> master
     * 
     * @return Email of the account
     */
    public String getMyEmail() {
        return megaApi.getMyEmail();
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
     *            - MegaApiJava.LOG_LEVEL_FATAL = 0 <br>
     *            - MegaApiJava.LOG_LEVEL_ERROR = 1 <br>
     *            - MegaApiJava.LOG_LEVEL_WARNING = 2 <br>
     *            - MegaApiJava.LOG_LEVEL_INFO = 3 <br>
     *            - MegaApiJava.LOG_LEVEL_DEBUG = 4 <br>
     *            - MegaApiJava.LOG_LEVEL_MAX = 5
     */
    public static void setLogLevel(int logLevel) {
        MegaApi.setLogLevel(logLevel);
    }

    /**
     * Set a MegaLogger implementation to receive SDK logs.
     * <p>
     * Logs received by this objects depends on the active log level.
     * By default, it is MegaApiJava.LOG_LEVEL_INFO. You can change it
     * using MegaApiJava.setLogLevel.
     * 
     * @param megaLogger
     *            MegaLogger implementation
     */
    public static void setLoggerObject(MegaLoggerInterface megaLogger) {
        DelegateMegaLogger newLogger = new DelegateMegaLogger(megaLogger);
        MegaApi.setLoggerObject(newLogger);
        logger = newLogger;
    }

    /**
     * Send a log to the logging system.
     * <p>
     * This log will be received by the active logger object (MegaApiJava.setLoggerObject()) if
     * the log level is the same or lower than the active log level (MegaApiJava.setLogLevel()).
     * 
     * @param logLevel
     *            Log level for this message
     * @param message
     *            Message for the logging system
     * @param filename
     *            Origin of the log message
     * @param line
     *            Line of code where this message was generated
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
     *            Log level for this message
     * @param message
     *            Message for the logging system
     * @param filename
     *            Origin of the log message
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
     *            Log level for this message
     * @param message
     *            Message for the logging system
     */
    public static void log(int logLevel, String message) {
        MegaApi.log(logLevel, message);
    }

    /**
     * Create a folder in the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CREATE_FOLDER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParentHandle - Returns the handle of the parent folder <br>
     * - MegaRequest.getName - Returns the name of the new folder
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getNodeHandle - Handle of the new folder
     * 
     * @param name
     *            Name of the new folder
     * @param parent
     *            Parent folder
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void createFolder(String name, MegaNode parent, MegaRequestListenerInterface listener) {
        megaApi.createFolder(name, parent, createDelegateRequestListener(listener));
    }

    /**
     * Create a folder in the MEGA account.
     * 
     * @param name
     *            Name of the new folder
     * @param parent
     *            Parent folder
     */
    public void createFolder(String name, MegaNode parent) {
        megaApi.createFolder(name, parent);
    }

    /**
     * Move a node in the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_MOVE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node to move <br>
     * - MegaRequest.getParentHandle - Returns the handle of the new parent for the node
     * 
     * @param node
     *            Node to move
     * @param newParent
     *            New parent for the node
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void moveNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener) {
        megaApi.moveNode(node, newParent, createDelegateRequestListener(listener));
    }

    /**
     * Move a node in the MEGA account.
     * 
     * @param node
     *            Node to move
     * @param newParent
     *            New parent for the node
     */
    public void moveNode(MegaNode node, MegaNode newParent) {
        megaApi.moveNode(node, newParent);
    }

    /**
     * Copy a node in the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node to copy <br>
     * - MegaRequest.getParentHandle - Returns the handle of the new parent for the new node <br>
     * - MegaRequest.getPublicMegaNode - Returns the node to copy (if it is a public node)
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getNodeHandle - Handle of the new node
     * 
     * @param node
     *            Node to copy
     * @param newParent
     *            Parent for the new node
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void copyNode(MegaNode node, MegaNode newParent, MegaRequestListenerInterface listener) {
        megaApi.copyNode(node, newParent, createDelegateRequestListener(listener));
    }

    /**
     * Copy a node in the MEGA account.
     * <p>
     * @param node
     *            Node to copy
     * @param newParent
     *            Parent for the new node
     */
    public void copyNode(MegaNode node, MegaNode newParent) {
        megaApi.copyNode(node, newParent);
    }

    /**
     * Copy a node in the MEGA account changing the file name.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node to copy <br>
     * - MegaRequest.getParentHandle - Returns the handle of the new parent for the new node <br>
     * - MegaRequest.getPublicMegaNode - Returns the node to copy <br>
     * - MegaRequest.getName - Returns the name for the new node
     * 
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getNodeHandle - Handle of the new node
     * 
     * @param node
     *            Node to copy
     * @param newParent
     *            Parent for the new node
     * @param newName
     *            Name for the new node <br>
     * 
     *            This parameter is only used if the original node is a file and it isn't a public node,
     *            otherwise, it's ignored.
     * 
     * @param listener
     *            MegaRequestListenerInterface to track this request
     */
    public void copyNode(MegaNode node, MegaNode newParent, String newName, MegaRequestListenerInterface listener) {
        megaApi.copyNode(node, newParent, newName, createDelegateRequestListener(listener));
    }

    /**
     * Copy a node in the MEGA account changing the file name.
     * 
     * @param node
     *            Node to copy
     * @param newParent
     *            Parent for the new node
     * @param newName
     *            Name for the new node <br>
     * 
     *            This parameter is only used if the original node is a file and it isn't a public node,
     *            otherwise, it's ignored.
     * 
     */
    public void copyNode(MegaNode node, MegaNode newParent, String newName) {
        megaApi.copyNode(node, newParent, newName);
    }

    /**
     * Rename a node in the MEGA account
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_RENAME
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node to rename <br>
     * - MegaRequest.getName - Returns the new name for the node
     * 
     * @param node
     *            Node to modify
     * @param newName
     *            New name for the node
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void renameNode(MegaNode node, String newName, MegaRequestListenerInterface listener) {
        megaApi.renameNode(node, newName, createDelegateRequestListener(listener));
    }

    /**
     * Rename a node in the MEGA account.
     * 
     * @param node
     *            Node to modify
     * @param newName
     *            New name for the node
     */
    public void renameNode(MegaNode node, String newName) {
        megaApi.renameNode(node, newName);
    }

    /**
     * Remove a node from the MEGA account.
     * <p>
     * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
     * the node to the Rubbish Bin use MegaApiJava.moveNode()
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REMOVE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node to remove
     * 
     * @param node
     *            Node to remove
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void remove(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.remove(node, createDelegateRequestListener(listener));
    }

    /**
     * Remove a node from the MEGA account.
     * 
     * @param node
     *            Node to remove
     */
    public void remove(MegaNode node) {
        megaApi.remove(node);
    }

    /**
     * Send a node to the Inbox of another MEGA user using a MegaUser.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node to send <br>
     * - MegaRequest.getEmail - Returns the email of the user that receives the node
     * 
     * @param node
     *            Node to send
     * @param user
     *            User that receives the node
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void sendFileToUser(MegaNode node, MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.sendFileToUser(node, user, createDelegateRequestListener(listener));
    }

    /**
     * Send a node to the Inbox of another MEGA user using a MegaUser.
     * 
     * @param node
     *            Node to send
     * @param user
     *            User that receives the node
     */
    public void sendFileToUser(MegaNode node, MegaUser user) {
        megaApi.sendFileToUser(node, user);
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using a MegaUser.
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare.ACCESS_UNKNOWN.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the folder to share <br>
     * - MegaRequest.getEmail - Returns the email of the user that receives the shared folder <br>
     * - MegaRequest.getAccess - Returns the access that is granted to the user
     * 
     * @param node
     *            The folder to share. It must be a non-root folder
     * @param user
     *            User that receives the shared folder
     * @param level
     *            Permissions that are granted to the user <br>
     *            Valid values for this parameter: <br>
     *            - MegaShare.ACCESS_UNKNOWN = -1
     *            Stop sharing a folder with this user <br>
     * 
     *            - MegaShare.ACCESS_READ = 0 <br>
     *            - MegaShare.ACCESS_READWRITE = 1 <br>
     *            - MegaShare.ACCESS_FULL = 2 <br>
     *            - MegaShare.ACCESS_OWNER = 3
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void share(MegaNode node, MegaUser user, int level, MegaRequestListenerInterface listener) {
        megaApi.share(node, user, level, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using a MegaUser.
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare.ACCESS_UNKNOWN.
     * 
     * @param node
     *            The folder to share. It must be a non-root folder
     * @param user
     *            User that receives the shared folder
     * @param level
     *            Permissions that are granted to the user <br>
     *            Valid values for this parameter: <br>
     *            - MegaShare.ACCESS_UNKNOWN = -1
     *            Stop sharing a folder with this user
     * 
     *            - MegaShare.ACCESS_READ = 0 <br>
     *            - MegaShare.ACCESS_READWRITE = 1 <br>
     *            - MegaShare.ACCESS_FULL = 2 <br>
     *            - MegaShare.ACCESS_OWNER = 3
     * 
     */
    public void share(MegaNode node, MegaUser user, int level) {
        megaApi.share(node, user, level);
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using his email.
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare.ACCESS_UNKNOWN
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_COPY
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the folder to share <br>
     * - MegaRequest.getEmail - Returns the email of the user that receives the shared folder <br>
     * - MegaRequest.getAccess - Returns the access that is granted to the user
     * 
     * @param node
     *            The folder to share. It must be a non-root folder
     * @param email
     *            Email of the user that receives the shared folder. If it doesn't have a MEGA account,
     *            the folder will be shared anyway and the user will be invited to register an account.
     * @param level
     *            Permissions that are granted to the user <br>
     *            Valid values for this parameter: <br>
     *            - MegaShare.ACCESS_UNKNOWN = -1
     *            Stop sharing a folder with this user <br>
     * 
     *            - MegaShare.ACCESS_READ = 0 <br>
     *            - MegaShare.ACCESS_READWRITE = 1 <br>
     *            - MegaShare.ACCESS_FULL = 2 <br>
     *            - MegaShare.ACCESS_OWNER = 3
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void share(MegaNode node, String email, int level, MegaRequestListenerInterface listener) {
        megaApi.share(node, email, level, createDelegateRequestListener(listener));
    }

    /**
     * Share or stop sharing a folder in MEGA with another user using his email.
     * <p>
     * To share a folder with an user, set the desired access level in the level parameter. If you
     * want to stop sharing a folder use the access level MegaShare.ACCESS_UNKNOWN.
     * 
     * @param node
     *            The folder to share. It must be a non-root folder
     * @param email
     *            Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
     *            and the user will be invited to register an account.
     * @param level
     *            Permissions that are granted to the user <br>
     *            Valid values for this parameter: <br>
     *            - MegaShare.ACCESS_UNKNOWN = -1 <br>
     *            Stop sharing a folder with this user
     * 
     *            - MegaShare.ACCESS_READ = 0 <br>
     *            - MegaShare.ACCESS_READWRITE = 1 <br>
     *            - MegaShare.ACCESS_FULL = 2 <br>
     *            - MegaShare.ACCESS_OWNER = 3
     * 
     */
    public void share(MegaNode node, String email, int level) {
        megaApi.share(node, email, level);
    }

    /**
     * Import a public link to the account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_IMPORT_LINK
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getLink - Returns the public link to the file <br>
     * - MegaRequest.getParentHandle - Returns the folder that receives the imported file
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getNodeHandle - Handle of the new node in the account
     * 
     * @param megaFileLink
     *            Public link to a file in MEGA
     * @param parent
     *            Parent folder for the imported file
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void importFileLink(String megaFileLink, MegaNode parent, MegaRequestListenerInterface listener) {
        megaApi.importFileLink(megaFileLink, parent, createDelegateRequestListener(listener));
    }

    /**
     * Import a public link to the account.
     * 
     * @param megaFileLink
     *            Public link to a file in MEGA
     * @param parent
     *            Parent folder for the imported file
     */
    public void importFileLink(String megaFileLink, MegaNode parent) {
        megaApi.importFileLink(megaFileLink, parent);
    }

    /**
     * Get a MegaNode from a public link to a file.
     * <p>
     * A public node can be imported using MegaApiJava.copy() or downloaded using MegaApiJava.startDownload().
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_PUBLIC_NODE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getLink - Returns the public link to the file
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getPublicMegaNode - Public MegaNode corresponding to the public link
     * 
     * @param megaFileLink
     *            Public link to a file in MEGA
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void getPublicNode(String megaFileLink, MegaRequestListenerInterface listener) {
        megaApi.getPublicNode(megaFileLink, createDelegateRequestListener(listener));
    }

    /**
     * Get a MegaNode from a public link to a file.
     * <p>
     * A public node can be imported using MegaApiJava.copy() or downloaded using MegaApiJava.startDownload().
     * 
     * @param megaFileLink
     *            Public link to a file in MEGA
     */
    public void getPublicNode(String megaFileLink) {
        megaApi.getPublicNode(megaFileLink);
    }

    /**
     * Get the thumbnail of a node.
     * <p>
     * If the node doesn't have a thumbnail the request fails with the MegaError.API_ENOENT
     * error code.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getFile - Returns the destination path <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.ATTR_TYPE_THUMBNAIL
     * 
     * @param node
     *            Node to get the thumbnail
     * @param dstFilePath
     *            Destination path for the thumbnail.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
     *            will be used as the file name inside that folder. If the path doesn't finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void getThumbnail(MegaNode node, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getThumbnail(node, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the thumbnail of a node.
     * <p>
     * If the node doesn't have a thumbnail the request fails with the MegaError.API_ENOENT
     * error code.
     * 
     * @param node
     *            Node to get the thumbnail
     * @param dstFilePath
     *            Destination path for the thumbnail.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
     *            will be used as the file name inside that folder. If the path doesn't finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     */
    public void getThumbnail(MegaNode node, String dstFilePath) {
        megaApi.getThumbnail(node, dstFilePath);
    }

    /**
     * Get the preview of a node.
     * <p>
     * If the node doesn't have a preview the request fails with the MegaError.API_ENOENT
     * error code.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getFile - Returns the destination path <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.ATTR_TYPE_PREVIEW
     * 
     * @param node
     *            Node to get the preview
     * @param dstFilePath
     *            Destination path for the preview.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg")
     *            will be used as the file name inside that folder. If the path doesn't finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void getPreview(MegaNode node, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getPreview(node, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the preview of a node.
     * <p>
     * If the node doesn't have a preview the request fails with the MegaError.API_ENOENT
     * error code.
     * 
     * @param node
     *            Node to get the preview
     * @param dstFilePath
     *            Destination path for the preview.
     *            If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg")
     *            will be used as the file name inside that folder. If the path doesn't finish with
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
     * - MegaRequest.getFile - Returns the destination path <br>
     * - MegaRequest.getEmail - Returns the email of the user
     * 
     * @param user
     *            MegaUser to get the avatar
     * @param dstFilePath
     *            Destination path for the avatar. It has to be a path to a file, not to a folder.
     *            If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *            will be used as the file name inside that folder. If the path doesn't finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void getUserAvatar(MegaUser user, String dstFilePath, MegaRequestListenerInterface listener) {
        megaApi.getUserAvatar(user, dstFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Get the avatar of a MegaUser.
     * 
     * @param user
     *            MegaUser to get the avatar
     * @param dstFilePath
     *            Destination path for the avatar. It has to be a path to a file, not to a folder.
     *            If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
     *            will be used as the file name inside that folder. If the path doesn't finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     */
    public void getUserAvatar(MegaUser user, String dstFilePath) {
        megaApi.getUserAvatar(user, dstFilePath);
    }

    /**
     * Cancel the retrieval of a thumbnail.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.ATTR_TYPE_THUMBNAIL
     * 
     * @param node
     *            Node to cancel the retrieval of the thumbnail
     * @param listener
     *            MegaRequestListener to track this request
     * @see MegaApiJava.getThumbnail()
     */
    public void cancelGetThumbnail(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.cancelGetThumbnail(node, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the retrieval of a thumbnail.
     * 
     * @param node
     *            Node to cancel the retrieval of the thumbnail
     * @see MegaApiJava.getThumbnail()
     */
    public void cancelGetThumbnail(MegaNode node) {
        megaApi.cancelGetThumbnail(node);
    }

    /**
     * Cancel the retrieval of a preview.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.ATTR_TYPE_PREVIEW
     * 
     * @param node
     *            Node to cancel the retrieval of the preview
     * @param listener
     *            MegaRequestListener to track this request
     * @see MegaApiJava.getPreview()
     */
    public void cancelGetPreview(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.cancelGetPreview(node, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the retrieval of a preview.
     * 
     * @param node
     *            Node to cancel the retrieval of the preview
     * @see MegaApiJava.getPreview()
     */
    public void cancelGetPreview(MegaNode node) {
        megaApi.cancelGetPreview(node);
    }

    /**
     * Set the thumbnail of a MegaNode.
     * 
     * The associated request type with this request is MegaRequest.TYPE_SET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getFile - Returns the source path <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.ATTR_TYPE_THUMBNAIL
     * 
     * @param node
     *            MegaNode to set the thumbnail
     * @param srcFilePath
     *            Source path of the file that will be set as thumbnail
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void setThumbnail(MegaNode node, String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setThumbnail(node, srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the thumbnail of a MegaNode.
     * 
     * @param node
     *            MegaNode to set the thumbnail
     * @param srcFilePath
     *            Source path of the file that will be set as thumbnail
     */
    public void setThumbnail(MegaNode node, String srcFilePath) {
        megaApi.setThumbnail(node, srcFilePath);
    }

    /**
     * Set the preview of a MegaNode.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_SET_ATTR_FILE
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getFile - Returns the source path <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.ATTR_TYPE_PREVIEW
     * 
     * @param node
     *            MegaNode to set the preview
     * @param srcFilePath
     *            Source path of the file that will be set as preview
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void setPreview(MegaNode node, String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setPreview(node, srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the preview of a MegaNode.
     * 
     * @param node
     *            MegaNode to set the preview
     * @param srcFilePath
     *            Source path of the file that will be set as preview
     */
    public void setPreview(MegaNode node, String srcFilePath) {
        megaApi.setPreview(node, srcFilePath);
    }

    /**
     * Set the avatar of the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getFile - Returns the source path
     * 
     * @param srcFilePath
     *            Source path of the file that will be set as avatar
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void setAvatar(String srcFilePath, MegaRequestListenerInterface listener) {
        megaApi.setAvatar(srcFilePath, createDelegateRequestListener(listener));
    }

    /**
     * Set the avatar of the MEGA account.
     * 
     * @param srcFilePath
     *            Source path of the file that will be set as avatar
     */
    public void setAvatar(String srcFilePath) {
        megaApi.setAvatar(srcFilePath);
    }

    /**
     * Set an attribute of the current user.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_SET_ATTR_USER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParamType - Returns the attribute type <br>
     * - MegaRequest.getFile - Returns the new value for the attribute
     * 
     * @param type
     *            Attribute type. Valid values are: <br>
     * 
     *            USER_ATTR_FIRSTNAME = 1
     *            Change the firstname of the user <br>
     *            USER_ATTR_LASTNAME = 2
     *            Change the lastname of the user
     * @param value
     *            New attribute value
     * @param listener
     *            MegaRequestListenerInterface to track this request
     */
    public void setUserAttribute(int type, String value, MegaRequestListenerInterface listener) {
        megaApi.setUserAttribute(type, value, createDelegateRequestListener(listener));
    }

    /**
     * Set an attribute of the current user.
     * 
     * @param type
     *            Attribute type. Valid values are: <br>
     * 
     *            USER_ATTR_FIRSTNAME = 1
     *            Change the firstname of the user <br>
     *            USER_ATTR_LASTNAME = 2
     *            Change the lastname of the user <br>
     * @param value
     *            New attribute value
     */
    public void setUserAttribute(int type, String value) {
        megaApi.setUserAttribute(type, value);
    }

    /**
     * Generate a public link of a file/folder in MEGA.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getAccess - Returns true
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getLink - Public link
     * 
     * @param node
     *            MegaNode to get the public link
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void exportNode(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.exportNode(node, createDelegateRequestListener(listener));
    }

    /**
     * Generate a public link of a file/folder in MEGA.
     * 
     * @param node
     *            MegaNode to get the public link
     */
    public void exportNode(MegaNode node) {
        megaApi.exportNode(node);
    }

    /**
     * Stop sharing a file/folder.
     * 
     * The associated request type with this request is MegaRequest.TYPE_EXPORT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the node <br>
     * - MegaRequest.getAccess - Returns false
     * 
     * @param node
     *            MegaNode to stop sharing
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void disableExport(MegaNode node, MegaRequestListenerInterface listener) {
        megaApi.disableExport(node, createDelegateRequestListener(listener));
    }

    /**
     * Stop sharing a file/folder.
     * 
     * @param node
     *            MegaNode to stop sharing
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
     *            MegaRequestListener to track this request
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
     * Get details about the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_ACCOUNT_DETAILS.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getMegaAccountDetails - Details of the MEGA account
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void getAccountDetails(MegaRequestListenerInterface listener) {
        megaApi.getAccountDetails(createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account.
     */
    public void getAccountDetails() {
        megaApi.getAccountDetails();
    }

    /**
     * Get details about the MEGA account.
     * <p>
     * This function allows to optionally get data about sessions, transactions and purchases related to the account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_ACCOUNT_DETAILS.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getMegaAccountDetails - Details of the MEGA account
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions, MegaRequestListenerInterface listener) {
        megaApi.getExtendedAccountDetails(sessions, purchases, transactions, createDelegateRequestListener(listener));
    }

    /**
     * Get details about the MEGA account.
     * 
     * This function allows to optionally get data about sessions, transactions and purchases related to the account.
     * 
     */
    public void getExtendedAccountDetails(boolean sessions, boolean purchases, boolean transactions) {
        megaApi.getExtendedAccountDetails(sessions, purchases, transactions);
    }

    /**
     * Get details about the MEGA account.
     * 
     * This function allows to optionally get data about sessions and purchases related to the account.
     * 
     */
    public void getExtendedAccountDetails(boolean sessions, boolean purchases) {
        megaApi.getExtendedAccountDetails(sessions, purchases);
    }

    /**
     * Get details about the MEGA account.
     * 
     * This function allows to optionally get data about sessions related to the account.
     * 
     */
    public void getExtendedAccountDetails(boolean sessions) {
        megaApi.getExtendedAccountDetails(sessions);
    }

    /**
     * Get details about the MEGA account.
     * 
     */
    public void getExtendedAccountDetails() {
        megaApi.getExtendedAccountDetails();
    }

    /**
     * Get the available pricing plans to upgrade a MEGA account.
     * <p>
     * You can get a payment URL for any of the pricing plans provided by this function
     * using MegaApiJava.getPaymentUrl().
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_PRICING.
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getPricing - MegaPricing object with all pricing plans
     * 
     * @param listener
     *            MegaRequestListener to track this request
     * 
     * @see MegaApiJava.getPaymentUrl()
     */
    public void getPricing(MegaRequestListenerInterface listener) {
        megaApi.getPricing(createDelegateRequestListener(listener));
    }

    /**
     * Get the available pricing plans to upgrade a MEGA account.
     * <p>
     * You can get a payment URL for any of the pricing plans provided by this function
     * using MegaApiJava.getPaymentUrl().
     * 
     * @see MegaApiJava.getPaymentUrl()
     */
    public void getPricing() {
        megaApi.getPricing();
    }

    /**
     * Get the payment id for an upgrade.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_GET_PAYMENT_ID
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getNodeHandle - Returns the handle of the product
     * <p>
     * Valid data in the MegaRequest object received in onRequestFinish when the error code
     * is MegaError.API_OK: <br>
     * - MegaRequest.getLink - Payment link
     * 
     * @param productHandle
     *            Handle of the product (see MegaApiJava.getPricing())
     * @param listener
     *            MegaRequestListener to track this request
     * @see MegaApiJava.getPricing()
     */
    public void getPaymentId(long productHandle, MegaRequestListenerInterface listener) {
        megaApi.getPaymentId(productHandle, createDelegateRequestListener(listener));
    }

    /**
     * Get the payment URL for an upgrade.
     * 
     * @param productHandle
     *            Handle of the product (see MegaApiJava.getPricing())
     * 
     * @see MegaApiJava.getPricing()
     */
    public void getPaymentId(long productHandle) {
        megaApi.getPaymentId(productHandle);
    }

    /**
     * Send the Google Play receipt after a correct purchase of a subscription.
     * 
     * @param receipt
     *            String The complete receipt from Google Play
     * @param listener
     *            MegaRequestListener to track this request
     * 
     */
    public void submitPurchaseReceipt(String receipt, MegaRequestListenerInterface listener) {
        megaApi.submitPurchaseReceipt(receipt, createDelegateRequestListener(listener));
    }

    /**
     * Send the Google Play receipt after a correct purchase of a subscription.
     * 
     * @param receipt
     *            String The complete receipt from Google Play
     * 
     */
    public void submitPurchaseReceipt(String receipt) {
        megaApi.submitPurchaseReceipt(receipt);
    }

    /**
     * Export the master key of the account.
     * <p>
     * The returned value is a Base64-encoded string
     * <p>
     * With the master key, it's possible to start the recovery of an account when the
     * password is lost: <br>
     * - https://mega.co.nz/#recovery
     * 
     * @return Base64-encoded master key
     */
    public String exportMasterKey() {
        return megaApi.exportMasterKey();
    }

    /**
     * Change the password of the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CHANGE_PW
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getPassword - Returns the old password <br>
     * - MegaRequest.getNewPassword - Returns the new password
     * 
     * @param oldPassword
     *            Old password
     * @param newPassword
     *            New password
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void changePassword(String oldPassword, String newPassword, MegaRequestListenerInterface listener) {
        megaApi.changePassword(oldPassword, newPassword, createDelegateRequestListener(listener));
    }

    /**
     * Change the password of the MEGA account.
     * 
     * @param oldPassword
     *            Old password
     * @param newPassword
     *            New password
     */
    public void changePassword(String oldPassword, String newPassword) {
        megaApi.changePassword(oldPassword, newPassword);
    }

    /**
     * Add a new contact to the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_ADD_CONTACT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail - Returns the email of the contact
     * 
     * @param email
     *            Email of the new contact
     * @param listener
     *            MegaRequestListener to track this request
     * @deprecated This method of adding contacts will be removed in future updates.
     * Please use MegaApiJava.inviteContact().
     */
    @Deprecated public void addContact(String email, MegaRequestListenerInterface listener) {
        megaApi.addContact(email, createDelegateRequestListener(listener));
    }

    /**
     * Add a new contact to the MEGA account.
     * 
     * @param email
     *            Email of the new contact
     * @deprecated This method of adding contacts will be removed in future updates.
     * Please use MegaApiJava.inviteContact().
     */
    @Deprecated public void addContact(String email) {
        megaApi.addContact(email);
    }

    /**
     * Remove a contact to the MEGA account.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REMOVE_CONTACT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getEmail - Returns the email of the contact
     * 
     * @param email
     *            Email of the contact
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void removeContact(MegaUser user, MegaRequestListenerInterface listener) {
        megaApi.removeContact(user, createDelegateRequestListener(listener));
    }

    /**
     * Remove a contact to the MEGA account.
     * 
     * @param email
     *            Email of the contact
     */
    public void removeContact(MegaUser user) {
        megaApi.removeContact(user);
    }

    /**
     * Logout of the MEGA account
     * 
     * The associated request type with this request is MegaRequest.TYPE_LOGOUT
     * 
     * @param listener
     *            MegaRequestListener to track this request
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
     * @brief Logout of the MEGA account without invalidating the session <br>
     * 
     *        The associated request type with this request is MegaRequest.TYPE_LOGOUT
     * 
     * @param listener
     *            MegaRequestListener to track this request
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
     * Submit feedback about the app.
     * <p>
     * The User-Agent is used to identify the app. It can be set in MegaApiJava.MegaApi().
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_REPORT_EVENT
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParamType - Returns MegaApiJava.EVENT_FEEDBACK <br>
     * - MegaRequest.getText - Retuns the comment about the app <br>
     * - MegaRequest.getNumber - Returns the rating for the app
     * 
     * @param rating
     *            Integer to rate the app. Valid values: from 1 to 5.
     * @param comment
     *            Comment about the app
     * @param listener
     *            MegaRequestListener to track this request
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
     * The User-Agent is used to identify the app. It can be set in MegaApiJava.MegaApi()
     * 
     * @param rating
     *            Integer to rate the app. Valid values: from 1 to 5.
     * @param comment
     *            Comment about the app
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
     * - MegaRequest.getParamType - Returns MegaApiJava.EVENT_DEBUG <br>
     * - MegaRequest.getText - Retuns the debug message
     * 
     * @param text
     *            Debug message
     * @param listener
     *            MegaRequestListener to track this request
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
     * - MegaRequest.getParamType - Returns MegaApiJava.EVENT_DEBUG <br>
     * - MegaRequest.getText - Retuns the debug message
     * 
     * @param text
     *            Debug message
     * @deprecated This function is for internal usage of MEGA apps. This feedback
     *             is sent to MEGA servers.
     */
    @Deprecated public void reportDebugEvent(String text) {
        megaApi.reportDebugEvent(text);
    }

    /****************************************************************************************************/
    // TRANSFERS
    /****************************************************************************************************/

    /**
     * Upload a file.
     * 
     * @param Local
     *            path of the file
     * @param Parent
     *            node for the file in the MEGA account
     * @param listener
     *            MegaTransferListener to track this transfer
     */
    public void startUpload(String localPath, MegaNode parent, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file.
     * 
     * @param Local
     *            path of the file
     * @param Parent
     *            node for the file in the MEGA account
     */
    public void startUpload(String localPath, MegaNode parent) {
        megaApi.startUpload(localPath, parent);
    }

    /**
     * Upload a file with a custom modification time.
     * 
     * @param localPath
     *            Local path of the file
     * @param parent
     *            Parent node for the file in the MEGA account
     * @param mtime
     *            Custom modification time for the file in MEGA (in seconds since the epoch)
     * @param listener
     *            MegaTransferListener to track this transfer
     */
    public void startUpload(String localPath, MegaNode parent, long mtime, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, mtime, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file with a custom modification time.
     * 
     * @param localPath
     *            Local path of the file
     * @param parent
     *            Parent node for the file in the MEGA account
     * @param mtime
     *            Custom modification time for the file in MEGA (in seconds since the epoch)
     */
    public void startUpload(String localPath, MegaNode parent, long mtime) {
        megaApi.startUpload(localPath, parent, mtime);
    }

    /**
     * Upload a file with a custom name.
     * 
     * @param localPath
     *            Local path of the file
     * @param parent
     *            Parent node for the file in the MEGA account
     * @param fileName
     *            Custom file name for the file in MEGA
     * @param listener
     *            MegaTransferListener to track this transfer
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, fileName, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file with a custom name.
     * 
     * @param localPath
     *            Local path of the file
     * @param parent
     *            Parent node for the file in the MEGA account
     * @param fileName
     *            Custom file name for the file in MEGA
     */
    public void startUpload(String localPath, MegaNode parent, String fileName) {
        megaApi.startUpload(localPath, parent, fileName);
    }

    /**
     * Upload a file with a custom name and a custom modification time.
     * 
     * @param localPath
     *            Local path of the file
     * @param parent
     *            Parent node for the file in the MEGA account
     * @param fileName
     *            Custom file name for the file in MEGA
     * @param mtime
     *            Custom modification time for the file in MEGA (in seconds since the epoch)
     * @param listener
     *            MegaTransferListener to track this transfer
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, long mtime, MegaTransferListenerInterface listener) {
        megaApi.startUpload(localPath, parent, fileName, mtime, createDelegateTransferListener(listener));
    }

    /**
     * Upload a file with a custom name and a custom modification time.
     * 
     * @param localPath
     *            Local path of the file
     * @param parent
     *            Parent node for the file in the MEGA account
     * @param fileName
     *            Custom file name for the file in MEGA
     * @param mtime
     *            Custom modification time for the file in MEGA (in seconds since the epoch)
     */
    public void startUpload(String localPath, MegaNode parent, String fileName, long mtime) {
        megaApi.startUpload(localPath, parent, fileName, mtime);
    }

    /**
     * Download a file from MEGA.
     * 
     * @param node
     *            MegaNode that identifies the file
     * @param localPath
     *            Destination path for the file.
     *            If this path is a local folder, it must end with a '\' or '/' character and the file name
     *            in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     * 
     * @param listener
     *            MegaTransferListener to track this transfer
     */
    public void startDownload(MegaNode node, String localPath, MegaTransferListenerInterface listener) {
        megaApi.startDownload(node, localPath, createDelegateTransferListener(listener));
    }

    /**
     * Download a file from MEGA.
     * 
     * @param node
     *            MegaNode that identifies the file
     * @param localPath
     *            Destination path for the file.
     *            If this path is a local folder, it must end with a '\' or '/' character and the file name
     *            in MEGA will be used to store a file inside that folder. If the path doesn't finish with
     *            one of these characters, the file will be downloaded to a file in that path.
     */
    public void startDownload(MegaNode node, String localPath) {
        megaApi.startDownload(node, localPath);
    }

    /**
     * Start a streaming download.
     * <p>
     * Streaming downloads do not save the downloaded data into a local file. It is provided
     * in MegaTransferListener.onTransferUpdate in a byte buffer. The pointer is returned by
     * MegaTransfer.getLastBytes and the size of the buffer by MegaTransfer.getDeltaSize
     * <p>
     * The same byte array is also provided in the callback MegaTransferListener.onTransferData for
     * compatibility with other programming languages. Only the MegaTransferListener passed to this function
     * will receive MegaTransferListener.onTransferData callbacks. MegaTransferListener objects registered
     * with MegaApiJava.addTransferListener() will not receive them for performance reasons.
     * 
     * @param node
     *            MegaNode that identifies the file (public nodes aren't supported yet)
     * @param startPos
     *            First byte to download from the file
     * @param size
     *            Size of the data to download
     * @param listener
     *            MegaTransferListener to track this transfer
     */
    public void startStreaming(MegaNode node, long startPos, long size, MegaTransferListenerInterface listener) {
        megaApi.startStreaming(node, startPos, size, createDelegateTransferListener(listener));
    }

    /**
     * Cancel a transfer.
     * <p>
     * When a transfer is cancelled, it will finish and will provide the error code
     * MegaError.API_EINCOMPLETE in MegaTransferListener.onTransferFinish and
     * MegaListener.onTransferFinish.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_TRANSFER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer.getTag)
     * 
     * @param transfer
     *            MegaTransfer object that identifies the transfer.
     *            You can get this object in any MegaTransferListener callback or any MegaListener callback
     *            related to transfers.
     * 
     * @param listener
     *            MegaRequestListener to track this request
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
     * MegaError.API_EINCOMPLETE in MegaTransferListener.onTransferFinish and
     * MegaListener.onTransferFinish
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_TRANSFER
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer.getTag)
     * 
     * @param transferTag
     *            tag that identifies the transfer.
     *            You can get this tag using MegaTransfer.getTag
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void cancelTransferByTag(int transferTag, MegaRequestListenerInterface listener) {
        megaApi.cancelTransferByTag(transferTag, createDelegateRequestListener(listener));
    }

    /**
     * Cancel the transfer with a specific tag.
     * 
     * @param transferTag
     *            tag that identifies the transfer.
     *            You can get this tag using MegaTransfer.getTag
     */
    public void cancelTransferByTag(int transferTag) {
        megaApi.cancelTransferByTag(transferTag);
    }

    /**
     * Cancel all transfers of the same type.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_CANCEL_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getParamType - Returns the first parameter
     * 
     * @param type
     *            Type of transfers to cancel.
     *            Valid values are: <br>
     *            - MegaTransfer.TYPE_DOWNLOAD = 0 <br>
     *            - MegaTransfer.TYPE_UPLOAD = 1
     * 
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void cancelTransfers(int direction, MegaRequestListenerInterface listener) {
        megaApi.cancelTransfers(direction, createDelegateRequestListener(listener));
    }

    /**
     * Cancel all transfers of the same type.
     * 
     * @param type
     *            Type of transfers to cancel.
     *            Valid values are: <br>
     *            - MegaTransfer.TYPE_DOWNLOAD = 0 <br>
     *            - MegaTransfer.TYPE_UPLOAD = 1
     */
    public void cancelTransfers(int direction) {
        megaApi.cancelTransfers(direction);
    }

    /**
     * Pause/resume all transfers.
     * <p>
     * The associated request type with this request is MegaRequest.TYPE_PAUSE_TRANSFERS
     * Valid data in the MegaRequest object received on callbacks: <br>
     * - MegaRequest.getFlag - Returns the first parameter
     * 
     * @param pause
     *            true to pause all transfers / false to resume all transfers
     * @param listener
     *            MegaRequestListener to track this request
     */
    public void pauseTransfers(boolean pause, MegaRequestListenerInterface listener) {
        megaApi.pauseTransfers(pause, createDelegateRequestListener(listener));
    }

    /**
     * Pause/resume all transfers.
     * 
     * @param pause
     *            true to pause all transfers / false to resume all transfers
     */
    public void pauseTransfers(boolean pause) {
        megaApi.pauseTransfers(pause);
    }

    /**
     * Set the upload speed limit.
     * <p>
     * The limit will be applied on the server side when starting a transfer. Thus the limit won't be
     * applied for already started uploads and it's applied per storage server.
     * 
     * @param bpslimit
     *            -1 to automatically select the limit, 0 for no limit, otherwise the speed limit
     *            in bytes per second
     */
    public void setUploadLimit(int bpslimit) {
        megaApi.setUploadLimit(bpslimit);
    }

    /**
     * Get all active transfers.
     * 
     * @return List with all active transfers
     */
    public ArrayList<MegaTransfer> getTransfers() {
        return transferListToArray(megaApi.getTransfers());
    }

    /**
     * Get the transfer with a transfer tag.
     * <p>
     * MegaTransfer.getTag can be used to get the transfer tag.
     * 
     * @param Transfer
     *            tag to check
     * @return MegaTransfer object with that tag, or `null` if there isn't any
     *         active transfer with it
     * 
     */
    public MegaTransfer getTransferByTag(int transferTag) {
        return megaApi.getTransferByTag(transferTag);
    }

    /**
     * Get all active transfers based on the type.
     * 
     * @param type
     *            MegaTransfer.TYPE_DOWNLOAD || MegaTransfer.TYPE_UPLOAD
     * 
     * @return List with all active download or upload transfers
     */
    public ArrayList<MegaTransfer> getTransfers(int type) {
        return transferListToArray(megaApi.getTransfers(type));
    }

    /**
     * Force a loop of the SDK thread.
     * 
     * @deprecated This function is only here for debugging purposes. It will probably
     *             be removed in future updates
     */
    @Deprecated public void update() {
        megaApi.update();
    }

    /**
     * Check if the SDK is waiting for the server.
     * 
     * @return true if the SDK is waiting for the server to complete a request
     */
    public boolean isWaiting() {
        return megaApi.isWaiting();
    }

    /**
     * Get the number of pending uploads.
     * 
     * @return Pending uploads
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated public int getNumPendingUploads() {
        return megaApi.getNumPendingUploads();
    }

    /**
     * Get the number of pending downloads.
     * 
     * @return Pending downloads
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated public int getNumPendingDownloads() {
        return megaApi.getNumPendingDownloads();
    }

    /**
     * Get the number of queued uploads since the last call to MegaApiJava.resetTotalUploads().
     * 
     * @return Number of queued uploads since the last call to MegaApiJava.resetTotalUploads()
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated public int getTotalUploads() {
        return megaApi.getTotalUploads();
    }

    /**
     * Get the number of queued uploads since the last call to MegaApiJava.resetTotalDownloads().
     * 
     * @return Number of queued uploads since the last call to MegaApiJava.resetTotalDownloads()
     * @deprecated Function related to statistics will be reviewed in future updates. They
     *             could change or be removed in the current form.
     */
    @Deprecated public int getTotalDownloads() {
        return megaApi.getTotalDownloads();
    }

    /**
     * Reset the number of total downloads.
     * <p>
     * This function resets the number returned by MegaApiJava.getTotalDownloads().
     * 
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     * 
     */
    @Deprecated public void resetTotalDownloads() {
        megaApi.resetTotalDownloads();
    }

    /**
     * Reset the number of total uploads.
     * <p>
     * This function resets the number returned by MegaApiJava.getTotalUploads().
     * 
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated public void resetTotalUploads() {
        megaApi.resetTotalUploads();
    }

    /**
     * Get the total downloaded bytes since the creation of the MegaApi object.
     * 
     * @return Total downloaded bytes since the creation of the MegaApi object
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     */
    @Deprecated public long getTotalDownloadedBytes() {
        return megaApi.getTotalDownloadedBytes();
    }

    /**
     * Get the total uploaded bytes since the creation of the MegaApi object.
     * 
     * @return Total uploaded bytes since the creation of the MegaApi object
     * @deprecated Function related to statistics will be reviewed in future updates to
     *             provide more data and avoid race conditions. They could change or be removed in the current form.
     * 
     */
    @Deprecated public long getTotalUploadedBytes() {
        return megaApi.getTotalUploadedBytes();
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
    @Deprecated public void updateStats() {
        megaApi.updateStats();
    }

    public void startUnbufferedDownload(MegaNode node, long startOffset, long size, OutputStream outputStream, MegaTransferListenerInterface listener) {
        DelegateMegaTransferListener delegateListener = new DelegateOutputMegaTransferListener(this, outputStream, listener, true);
        activeTransferListeners.add(delegateListener);
        megaApi.startStreaming(node, startOffset, size, delegateListener);
    }

    public void startUnbufferedDownload(MegaNode node, OutputStream outputStream, MegaTransferListenerInterface listener) {
        startUnbufferedDownload(node, 0, node.getSize(), outputStream, listener);
    }

    /****************************************************************************************************/
    // FILESYSTEM METHODS
    /****************************************************************************************************/

    /**
     * Get the number of child nodes.
     * <p>
     * If the node doesn't exist in MEGA or isn't a folder,
     * this function returns 0.
     * <p>
     * This function doesn't search recursively, only returns the direct child nodes.
     * 
     * @param parent
     *            Parent node
     * @return Number of child nodes
     */
    public int getNumChildren(MegaNode parent) {
        return megaApi.getNumChildren(parent);
    }

    /**
     * Get the number of child files of a node.
     * <p>
     * If the node doesn't exist in MEGA or isn't a folder,
     * this function returns 0.
     * <p>
     * This function doesn't search recursively, only returns the direct child files.
     * 
     * @param parent
     *            Parent node
     * @return Number of child files
     */
    public int getNumChildFiles(MegaNode parent) {
        return megaApi.getNumChildFiles(parent);
    }

    /**
     * Get the number of child folders of a node.
     * <p>
     * If the node doesn't exist in MEGA or isn't a folder,
     * this function returns 0.
     * <p>
     * This function doesn't search recursively, only returns the direct child folders.
     * 
     * @param parent
     *            Parent node
     * @return Number of child folders
     */
    public int getNumChildFolders(MegaNode parent) {
        return megaApi.getNumChildFolders(parent);
    }

    /**
     * Get all children of a MegaNode.
     * <p>
     * If the parent node doesn't exist or it isn't a folder, this function
<<<<<<< HEAD
     * returns NULL.
=======
     * returns `null`
>>>>>>> master
     * 
     * @param parent
     *            Parent node
     * @param order
     *            Order for the returned list.
     *            Valid values for this parameter are: <br>
     *            - MegaApiJava.ORDER_NONE = 0
     *            Undefined order <br>
     * 
     *            - MegaApiJava.ORDER_DEFAULT_ASC = 1
     *            Folders first in alphabetical order, then files in the same order <br>
     * 
     *            - MegaApiJava.ORDER_DEFAULT_DESC = 2
     *            Files first in reverse alphabetical order, then folders in the same order <br>
     * 
     *            - MegaApiJava.ORDER_SIZE_ASC = 3
     *            Sort by size, ascending <br>
     * 
     *            - MegaApiJava.ORDER_SIZE_DESC = 4
     *            Sort by size, descending <br>
     * 
     *            - MegaApiJava.ORDER_CREATION_ASC = 5
     *            Sort by creation time in MEGA, ascending <br>
     * 
     *            - MegaApiJava.ORDER_CREATION_DESC = 6
     *            Sort by creation time in MEGA, descending <br>
     * 
     *            - MegaApiJava.ORDER_MODIFICATION_ASC = 7
     *            Sort by modification time of the original file, ascending <br>
     * 
     *            - MegaApiJava.ORDER_MODIFICATION_DESC = 8
     *            Sort by modification time of the original file, descending <br>
     * 
     *            - MegaApiJava.ORDER_ALPHABETICAL_ASC = 9
     *            Sort in alphabetical order, ascending <br>
     * 
     *            - MegaApiJava.ORDER_ALPHABETICAL_DESC = 10
     *            Sort in alphabetical order, descending
     * @return List with all child MegaNode objects
     */
    public ArrayList<MegaNode> getChildren(MegaNode parent, int order) {
        return nodeListToArray(megaApi.getChildren(parent, order));
    }

    /**
     * Get all children of a MegaNode.
     * <p>
     * If the parent node doesn't exist or it isn't a folder, this function
<<<<<<< HEAD
     * returns NULL.
=======
     * returns `null`
>>>>>>> master
     * 
     * @param parent
     *            Parent node
     * 
     * @return List with all child MegaNode objects
     */
    public ArrayList<MegaNode> getChildren(MegaNode parent) {
        return nodeListToArray(megaApi.getChildren(parent));
    }

    /**
     * Get the current index of the node in the parent folder for a specific sorting order.
     * <p>
     * If the node doesn't exist or it doesn't have a parent node (because it's a root node)
     * this function returns -1.
     * 
     * @param node
     *            Node to check
     * @param order
     *            Sorting order to use
     * @return Index of the node in its parent folder
     */
    public int getIndex(MegaNode node, int order) {
        return megaApi.getIndex(node, order);
    }

    /**
     * Get the current index of the node in the parent folder.
     * <p>
     * If the node doesn't exist or it doesn't have a parent node (because it's a root node)
     * this function returns -1.
     * 
     * @param node
     *            Node to check
     * 
     * @return Index of the node in its parent folder
     */
    public int getIndex(MegaNode node) {
        return megaApi.getIndex(node);
    }

    /**
     * Get the child node with the provided name.
     * 
<<<<<<< HEAD
     * If the node doesn't exist, this function returns NULL.
=======
     * If the node doesn't exist, this function returns `null`
>>>>>>> master
     * 
     * @param Parent
     *            node
     * @param Name
     *            of the node
     * @return The MegaNode that has the selected parent and name
     */
    public MegaNode getChildNode(MegaNode parent, String name) {
        return megaApi.getChildNode(parent, name);
    }

    /**
     * Get the parent node of a MegaNode.
     * 
     * If the node doesn't exist in the account or
<<<<<<< HEAD
     * it is a root node, this function returns NULL.
=======
     * it is a root node, this function returns `null`
>>>>>>> master
     * 
     * @param node
     *            MegaNode to get the parent
     * @return The parent of the provided node
     */
    public MegaNode getParentNode(MegaNode node) {
        return megaApi.getParentNode(node);
    }

    /**
<<<<<<< HEAD
     * Get the path of a MegaNode.
     * <p>
     * If the node doesn't exist, this function returns NULL.
     * You can recoved the node later unsing MegaApiJava.getNodeByPath()
=======
     * Get the path of a MegaNode
     * 
     * If the node doesn't exist, this function returns `null`.
     * You can recoved the node later unsing MegaApi::getNodeByPath
>>>>>>> master
     * except if the path contains names with '/', '\' or ':' characters.
     * 
     * @param node
     *            MegaNode for which the path will be returned
     * @return The path of the node
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
     *            Path to check
     * @param n
     *            Base node if the path is relative
     * @return The MegaNode object in the path, otherwise `null`
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
     * Paths with names containing '/', '\' or ':' aren't compatible
     * with this function.
     * 
     * @param path
     *            Path to check
     * 
     * @return The MegaNode object in the path, otherwise `null`
     */
    public MegaNode getNodeByPath(String path) {
        return megaApi.getNodeByPath(path);
    }

    /**
     * Get the MegaNode that has a specific handle.
     * <p>
     * You can get the handle of a MegaNode using MegaNode.getHandle. The same handle
     * can be got in a Base64-encoded string using MegaNode.getBase64Handle. Conversions
     * between these formats can be done using MegaApiJava.base64ToHandle() and MegaApiJava.handleToBase64().
     * 
     * @param MegaHandler
     *            Node handle to check
     * @return MegaNode object with the handle, otherwise `null`
     */
    public MegaNode getNodeByHandle(long handle) {
        return megaApi.getNodeByHandle(handle);
    }

    /**
     * Get all contacts of this MEGA account.
     * 
     * @return List of MegaUser object with all contacts of this account
     */
    public ArrayList<MegaUser> getContacts() {
        return userListToArray(megaApi.getContacts());
    }

    /**
     * Get the MegaUser that has a specific email address.
     * <p>
     * You can get the email of a MegaUser using MegaUser.getEmail.
     * 
     * @param email
     *            Email address to check
     * @return MegaUser that has the email address, otherwise `null`
     */
    public MegaUser getContact(String email) {
        return megaApi.getContact(email);
    }

    /**
     * Get a list with all inbound sharings from one MegaUser.
     * 
     * @param user
     *            MegaUser sharing folders with this account
     * @return List of MegaNode objects that this user is sharing with this account
     */
    public ArrayList<MegaNode> getInShares(MegaUser user) {
        return nodeListToArray(megaApi.getInShares(user));
    }

    /**
     * Get a list with all inboud sharings.
     * 
     * @return List of MegaNode objects that other users are sharing with this account
     */
    public ArrayList<MegaNode> getInShares() {
        return nodeListToArray(megaApi.getInShares());
    }

    /**
     * Check if a MegaNode is being shared.
     * <p>
     * For nodes that are being shared, you can get a a list of MegaShare
     * objects using MegaApiJava.getOutShares().
     * 
     * @param node
     *            Node to check
     * @return true is the MegaNode is being shared, otherwise false
     */
    public boolean isShared(MegaNode node) {
        return megaApi.isShared(node);
    }

    /**
     * Get a list with all active outbound sharings.
     * 
     * @return List of MegaShare objects
     */
    public ArrayList<MegaShare> getOutShares() {
        return shareListToArray(megaApi.getOutShares());
    }

    /**
     * Get a list with the active outbound sharings for a MegaNode.
     * <p>
     * If the node doesn't exist in the account, this function returns an empty list.
     * 
     * @param node
     *            MegaNode to check
     * @return List of MegaShare objects
     */
    public ArrayList<MegaShare> getOutShares(MegaNode node) {
        return shareListToArray(megaApi.getOutShares(node));
    }

    /**
     * Get the access level of a MegaNode.
     * 
     * @param node
     *            MegaNode to check
     * @return Access level of the node.
     *         Valid values are: <br>
     *         - MegaShare.ACCESS_OWNER <br>
     *         - MegaShare.ACCESS_FULL <br>
     *         - MegaShare.ACCESS_READWRITE <br>
     *         - MegaShare.ACCESS_READ <br>
     *         - MegaShare.ACCESS_UNKNOWN
     */
    public int getAccess(MegaNode node) {
        return megaApi.getAccess(node);
    }

    /**
     * Get the size of a node tree.
     * <p>
     * If the MegaNode is a file, this function returns the size of the file.
     * If it's a folder, this fuction returns the sum of the sizes of all nodes
     * in the node tree.
     * 
     * @param node
     *            Parent node
     * @return Size of the node tree
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
     *            Local file path
     * @return Base64-encoded fingerprint for the file
     */
    public String getFingerprint(String filePath) {
        return megaApi.getFingerprint(filePath);
    }

    /**
     * Get a Base64-encoded fingerprint for a node.
     * <p>
     * If the node doesn't exist or doesn't have a fingerprint, this function returns null.
     * 
     * @param node
     *            Node for which we want to get the fingerprint
     * @return Base64-encoded fingerprint for the file
     */
    public String getFingerprint(MegaNode node) {
        return megaApi.getFingerprint(node);
    }

    /**
     * Returns a node with the provided fingerprint.
     * <p>
     * If there isn't any node in the account with that fingerprint, this function returns null.
     * 
     * @param fingerprint
     *            Fingerprint to check
     * @return MegaNode object with the provided fingerprint
     */
    public MegaNode getNodeByFingerprint(String fingerprint) {
        return megaApi.getNodeByFingerprint(fingerprint);
    }

    public MegaNode getNodeByFingerprint(String fingerprint, MegaNode preferredParent) {
        return megaApi.getNodeByFingerprint(fingerprint, preferredParent);
    }

    /**
     * Check if the account already has a node with the provided fingerprint.
     * <p>
     * A fingerprint for a local file can be generated using MegaApiJava.getFingerprint().
     * 
     * @param fingerprint
     *            Fingerprint to check
     * @return true if the account contains a node with the same fingerprint
     */
    public boolean hasFingerprint(String fingerprint) {
        return megaApi.hasFingerprint(fingerprint);
    }

    /**
     * Check if a node has an access level.
     * 
     * @param node
     *            Node to check
     * @param level
     *            Access level to check.
     *            Valid values for this parameter are: <br>
     *            - MegaShare.ACCESS_OWNER <br>
     *            - MegaShare.ACCESS_FULL <br>
     *            - MegaShare.ACCESS_READWRITE <br>
     *            - MegaShare.ACCESS_READ
     * @return MegaError object with the result.
     *         Valid values for the error code are: <br>
     *         - MegaError.API_OK - The node has the required access level <br>
     *         - MegaError.API_EACCESS - The node doesn't have the required access level <br>
     *         - MegaError.API_ENOENT - The node doesn't exist in the account <br>
     *         - MegaError.API_EARGS - Invalid parameters
     */
    public MegaError checkAccess(MegaNode node, int level) {
        return megaApi.checkAccess(node, level);
    }

    /**
     * Check if a node can be moved to a target node.
     * 
     * @param node
     *            Node to check
     * @param target
     *            Target for the move operation
     * @return MegaError object with the result.
     *         Valid values for the error code are: <br>
     *         - MegaError.API_OK - The node can be moved to the target <br>
     *         - MegaError.API_EACCESS - The node can't be moved because of permissions problems <br>
     *         - MegaError.API_ECIRCULAR - The node can't be moved because that would create a circular linkage <br>
     *         - MegaError.API_ENOENT - The node or the target doesn't exist in the account <br>
     *         - MegaError.API_EARGS - Invalid parameters
     */
    public MegaError checkMove(MegaNode node, MegaNode target) {
        return megaApi.checkMove(node, target);
    }

    /**
     * Returns the root node of the account.
     * <p>
     * If you haven't successfully called MegaApiJava.fetchNodes() before,
     * this function returns null.
     * 
     * @return Root node of the account
     */
    public MegaNode getRootNode() {
        return megaApi.getRootNode();
    }

    /**
     * Returns the inbox node of the account.
     * <p>
     * If you haven't successfully called MegaApiJava.fetchNodes() before,
     * this function returns null.
     * 
     * @return Inbox node of the account
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
     * @return Rubbish node of the account
     */
    public MegaNode getRubbishNode() {
        return megaApi.getRubbishNode();
    }

    /**
     * Search nodes containing a search string in their name.
     * <p>
     * The search is case-insensitive.
     * 
     * @param node
     *            The parent node of the tree to explore
     * @param searchString
     *            Search string. The search is case-insensitive
     * @param recursive
     *            True if you want to seach recursively in the node tree.
     *            False if you want to seach in the children of the node only
     * 
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> search(MegaNode parent, String searchString, boolean recursive) {
        return nodeListToArray(megaApi.search(parent, searchString, recursive));
    }

    /**
     * Search nodes containing a search string in their name.
     * <p>
     * The search is case-insensitive.
     * 
     * @param node
     *            The parent node of the tree to explore
     * @param searchString
     *            Search string. The search is case-insensitive
     * 
     * @return List of nodes that contain the desired string in their name
     */
    public ArrayList<MegaNode> search(MegaNode parent, String searchString) {
        return nodeListToArray(megaApi.search(parent, searchString));
    }

    /**
     * Process a node tree using a MegaTreeProcessor implementation.
     * 
     * @param node
     *            The parent node of the tree to explore
     * @param processor
     *            MegaTreeProcessor that will receive callbacks for every node in the tree
     * @param recursive
     *            True if you want to recursively process the whole node tree.
     *            False if you want to process the children of the node only
     * 
     * @return True if all nodes were processed. False otherwise (the operation can be
     *         cancelled by MegaTreeProcessor.processMegaNode())
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
     * @param node
     *            The parent node of the tree to explore
     * @param processor
     *            MegaTreeProcessor that will receive callbacks for every node in the tree
     * 
     * @return True if all nodes were processed. False otherwise (the operation can be
     *         cancelled by MegaTreeProcessor.processMegaNode())
     */
    public boolean processMegaTree(MegaNode parent, MegaTreeProcessorInterface processor) {
        DelegateMegaTreeProcessor delegateListener = new DelegateMegaTreeProcessor(this, processor);
        activeMegaTreeProcessors.add(delegateListener);
        boolean result = megaApi.processMegaTree(parent, delegateListener);
        activeMegaTreeProcessors.remove(delegateListener);
        return result;
    }

    /**
     * Get the SDK version.
     * 
     * @return SDK version
     */
    public String getVersion() {
        return megaApi.getVersion();
    }

    /**
     * Get the User-Agent header used by the SDK.
     * 
     * @return User-Agent used by the SDK
     */
    public String getUserAgent() {
        return megaApi.getUserAgent();
    }

    public void changeApiUrl(String apiURL, boolean disablepkp) {
        megaApi.changeApiUrl(apiURL, disablepkp);
    }

    public void changeApiUrl(String apiURL) {
        megaApi.changeApiUrl(apiURL);
    }

    /**
<<<<<<< HEAD
     * Make a name suitable for a file name in the local filesystem.
     * 
     * This function escapes (%xx) forbidden characters in the local filesystem if needed.
     * You can revert this operation using MegaApiJava.localToName().
     * 
     * @param name
     *            Name to convert
     * @return Converted name
     */
    public String nameToLocal(String name) {
        return megaApi.nameToLocal(name);
    }

    /**
     * Unescape a file name escaped with MegaApiJava.nameToLocal().
     * 
     * @param name
     *            Escaped name to convert
     * @return Converted name
     */
    public String localToName(String localName) {
        return megaApi.localToName(localName);
    }

    /**
     * Convert a Base64 string to Base32.
     * <p>
     * If the input pointer is NULL, this function will return NULL.
=======
     * Convert a Base64 string to Base32
     * 
     * If the input pointer is `null`, this function will return `null`.
>>>>>>> master
     * If the input character array isn't a valid base64 string
     * the effect is undefined.
     * 
     * @param base64
     *            `null`-terminated Base64 character array
     * @return `null`-terminated Base32 character array
     */
    public static String base64ToBase32(String base64) {
        return MegaApi.base64ToBase32(base64);
    }

    /**
     * Convert a Base32 string to Base64.
     * 
     * If the input pointer is `null`, this function will return `null`.
     * If the input character array isn't a valid base32 string
     * the effect is undefined.
     * 
     * @param base32
     *            `null`-terminated Base32 character array
     * @return `null`-terminated Base64 character array
     */
    public static String base32ToBase64(String base32) {
        return MegaApi.base32ToBase64(base32);
    }

    public static void removeRecursively(String localPath) {
        MegaApi.removeRecursively(localPath);
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

    void privateFreeRequestListener(DelegateMegaRequestListener listener) {
        activeRequestListeners.remove(listener);
    }

    void privateFreeTransferListener(DelegateMegaTransferListener listener) {
        activeTransferListeners.remove(listener);
    }

    static ArrayList<MegaNode> nodeListToArray(MegaNodeList nodeList) {
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
}
