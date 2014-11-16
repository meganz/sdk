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

    /**
     * @brief INVALID_HANDLE Invalid value for a handle
     *
     * This value is used to represent an invalid handle. Several MEGA objects can have
     * a handle but it will never be mega::INVALID_HANDLE
     *
     */
    const MegaHandle INVALID_HANDLE = ~(MegaHandle)0;

class MegaListener;
class MegaRequestListener;
class MegaTransferListener;
class MegaGlobalListener;
class MegaTreeProcessor;
class MegaAccountDetails;
class MegaPricing;
class MegaNode;
class MegaUser;
class MegaShare;
class MegaError;
class MegaRequest;
class MegaTransfer;
class MegaNodeList;
class MegaUserList;
class MegaShareList;
class MegaTransferList;
class MegaApi;

/**
 * @brief Interface to provide an external GFX processor
 *
 * You can implement this interface to provide a graphics processor to the SDK
 * in the MegaApi::MegaApi constructor. That way, SDK will use your implementation to generate
 * thumbnails/previews when needed.
 *
 * Images will be sequentially processed. At first, the SDK will call MegaGfxProcessor::readBitmap
 * with the path of the file. Then, it will call MegaGfxProcessor::getWidth and MegaGfxProcessor::getHeight
 * to get the dimensions of the file (in pixels). After that, the SDK will call
 * MegaGfxProcessor::getBitmapDataSize and MegaGfxProcessor::getBitmapData in a loop
 * to get thumbnails/previews of different sizes. Finally, the SDK will call
 * MegaGfxProcessor::freeBitmap to let you free the resources required to process
 * the current file.
 *
 * If the image has EXIF data, it should be rotated/mirrored before doing any
 * other processing. MegaGfxProcessor::getWidth, MegaGfxProcessor::getHeight and all
 * other coordinates in this interface are expressed over the image after the required
 * transformation based on the EXIF data.
 *
 * Generated images must be in JPG format.
 *
 */
class MegaGfxProcessor
{
public:
    /**
     * @brief Read the image file and check if it can be processed
     *
     * This is the first function that will be called to process an image. No other
     * functions of this interface will be called before this one.
     *
     * The recommended implementation is to read the file, check if it's an image and
     * get its dimensions. If everything is OK, the function should return true. If the
     * file isn't an image or can't be processed, this function should return false.
     *
     * The SDK will call this function with all files so it's probably a good idea to
     * check the extension before trying to open them.
     *
     * @param path Path of the file that is going to be processed
     * @return True if the implementation is able to manage the file, false otherwise.
     */
    virtual bool readBitmap(const char* path) = 0;

    /**
     * @brief Returns the width of the image
     *
     * This function must return the width of the image at the path provided in MegaGfxProcessor::readBitmap
     * If a number <= 0 is returned, the image won't be processed.
     *
     * @return The width of the image
     */
    virtual int getWidth() = 0;

    /**
     * @brief Returns the height of the image
     *
     * This function must return de width of the image at the path provided in MegaGfxProcessor::readBitmap
     * If a number <= 0 is returned, the image won't be processed.
     *
     * @return The height of the image
     */
    virtual int getHeight() = 0;

    /**
     * @brief Generates a thumbnail/preview image.
     *
     * This function provides the parameters of the thumbnail/preview that the SDK wants to generate.
     * If the implementation can create it, it has to provide the size of the buffer (in bytes) that
     * it needs to store the generated JPG image. Otherwise, it should return a number <= 0.
     *
     * The implementation of this function has to scale the image to the size (width, height) and then
     * extract the rectangle starting at the point (px, py) with size (rw, rh). (px, py, rw and rh) are
     * expressed in pixels over the scaled image, being the point (0, 0) the upper-left corner of the
     * scaled image, with the X-axis growing to the right and the Y-axis growing to the bottom.
     *
     * @param width Width of the scaled image from which the thumbnail/preview image will be extracted
     * @param height Height of the scaled image from which the thumbnail/preview image will be extracted
     * @param px X coordinate of the starting point of the desired image (in pixels over the scaled image)
     * @param py Y coordinate of the starting point of the desired image (in pixels over the scaled image)
     * @param rw Width of the desired image (in pixels over the scaled image)
     * @param rh Height of the desired image (in pixels over the scaled image)
     *
     * @return Size of the buffer required to store the image (in bytes) or a number <= 0 if it's not
     * possible to generate it.
     *
     */
    virtual int getBitmapDataSize(int width, int height, int px, int py, int rw, int rh) = 0;

    /**
     * @brief Copy the thumbnail/preview data to a buffer provided by the SDK
     *
     * The SDK uses this function immediately after MegaGfxProcessor::getBitmapDataSize when that
     * fuction succeed. The implementation of this function must copy the data of the image in the
     * buffer provided by the SDK. The size of this buffer will be the same as the value returned
     * in the previous call to MegaGfxProcessor::getBitmapDataSize. That size is provided in the
     * second parameter for compatibility with SWIG and to help the implementation to prevent
     * buffer overflow errors.
     *
     * @param bitmapData Preallocated buffer in which the implementation must write the generated image
     * @param size Size of the buffer. It will be the same that the previous return value of
     * MegaGfxProcessor::getBitmapDataSize
     *
     * @return True in case of success, false otherwise.
     */
    virtual bool getBitmapData(char *bitmapData, size_t size) = 0;

    /**
     * @brief Free resources associated with the processing of the current image
     *
     * With a call of this function, the processing of the image started with a call to
     * MegaGfxProcessor::readBitmap ends. No other functions will be called to continue processing
     * the current image, so you can free all related resources.
     *
     */
    virtual void freeBitmap() = 0;

    virtual ~MegaGfxProcessor() = 0;
};

/**
 * @brief Contains the information related to a proxy server.
 *
 * Pass an object of this class to MegaApi::setProxySettings to
 * start using a proxy server.
 *
 * Currently, only HTTP proxies are allowed. The proxy server
 * should support HTTP request and tunneling for HTTPS.
 *
 */
class MegaProxy
{
public:
    enum {PROXY_NONE = 0, PROXY_AUTO = 1, PROXY_CUSTOM = 2};

    /**
     * @brief Creates a MegaProxy object with the default settings (PROXY_AUTO)
     */
    MegaProxy();
    virtual ~MegaProxy();

    /**
     * @brief Sets the type of the proxy
     *
     * The allowed values in the current version are:
     * - PROXY_NONE means no proxy
     * - PROXY_AUTO means automatic detection (default)
     * - PROXY_CUSTOM means a proxy using user-provided data
     *
     * PROXY_AUTO is currently supported on Windows only, for other platforms
     * PROXY_NONE will be used as the automatic detected value.
     *
     * @param proxyType Sets the type of the proxy
     */
    void setProxyType(int proxyType);

    /**
     * @brief Sets the URL of the proxy
     *
     * That URL must follow this format: <scheme>://<hostname|ip>:<port>
     *
     * This is a valid example: http://127.0.0.1:8080
     *
     * @param proxyURL URL of the proxy: <scheme>://<hostname|ip>:<port>
     */
    void setProxyURL(const char *proxyURL);

    /**
     * @brief Set the credentials needed to use the proxy
     *
     * If you don't need to use any credentials, do not use this function
     * or pass NULL in the first parameter.
     *
     * @param username Username to access the proxy, or NULL if credentials aren't needed
     * @param password Password to access the proxy
     */
    void setCredentials(const char *username, const char *password);

    /**
     * @brief Returns the current proxy type of the object
     *
     * The allowed values in the current version are:
     * - PROXY_NONE means no proxy
     * - PROXY_AUTO means automatic detection (default)
     * - PROXY_CUSTOM means a proxy using user-provided data
     *
    * @return Current proxy type (PROXY_NONE, PROXY_AUTO or PROXY_CUSTOM)
     */
    int getProxyType();

    /**
     * @brief Returns the URL of the proxy, previously set with MegaProxy::setProxyURL.
     *
     * The MegaProxy object retains the ownership of the returned value.
     * It will be valid until the MegaProxy::setProxyURL is called (that will delete the previous value)
     * or until the MegaProxy object is deleted.
     *
     * @return URL of the proxy
     */
    const char *getProxyURL();

    /**
     * @brief Returns true if credentials are needed to access the proxy, false otherwise.
     *
     * The default value of this function is false. It will return true after calling
     * MegaProxy::setCredentials with a non NULL username.
     *
     * @return True if credentials are needed to access the proxy, false otherwise.
     */
    bool credentialsNeeded();

    /**
     * @brief Return the username required to access the proxy
     *
     * The MegaProxy object retains the ownership of the returned value.
     * It will be valid until the MegaProxy::setCredentials is called (that will delete the previous value)
     * or until the MegaProxy object is deleted.
     *
     * @return Username required to access the proxy
     */
    const char *getUsername();

    /**
     * @brief Return the username required to access the proxy
     *
     * The MegaProxy object retains the ownership of the returned value.
     * It will be valid until the MegaProxy::setCredentials is called (that will delete the previous value)
     * or until the MegaProxy object is deleted.
     *
     * @return Password required to access the proxy
     */
    const char *getPassword();

protected:
    int proxyType;
    const char *proxyURL;
    const char *username;
    const char *password;
};

/**
 * @brief Interface to receive SDK logs
 *
 * You can implement this class and pass an object of your subclass to MegaApi::setLoggerClass
 * to receive SDK logs. You will have to use also MegaApi::setLogLevel to select the level of
 * the logs that you want to receive.
 *
 */
class MegaLogger
{
public:
    /**
     * @brief This function will be called with all logs with level <= your selected
     * level of logging (by default it is MegaApi::LOG_LEVEL_INFO)
     *
     * @param time Readable string representing the current time.
     *
     * The SDK retains the ownership of this string, it won't be valid after this funtion returns.
     *
     * @param loglevel Log level of this message
     *
     * Valid values are:
     * - MegaApi::LOG_LEVEL_FATAL = 0
     * - MegaApi::LOG_LEVEL_ERROR = 1
     * - MegaApi::LOG_LEVEL_WARNING = 2
     * - MegaApi::LOG_LEVEL_INFO = 3
     * - MegaApi::LOG_LEVEL_DEBUG = 4
     * - MegaApi::LOG_LEVEL_MAX = 5
     *
     * @param source Location where this log was generated
     *
     * For logs generated inside the SDK, this will contain <source file>:<line of code>
     * The SDK retains the ownership of this string, it won't be valid after this funtion returns.
     *
     * @param message Log message
     *
     * The SDK retains the ownership of this string, it won't be valid after this funtion returns.
     *
     */
    virtual void log(const char *time, int loglevel, const char *source, const char *message);
    virtual ~MegaLogger(){}
};

/**
 * @brief Represents a node (file/folder) in the MEGA account
 *
 * It allows to get all data related to a file/folder in MEGA. It can be also used
 * to start SDK requests (MegaApi::renameNode, MegaApi::moveNode, etc.)
 *
 * Objects of this class aren't live, they are snapshots of the state of a node
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can inspect the MEGA filesystem and get these objects using
 * MegaApi::getChildren, MegaApi::getChildNode and other MegaApi functions.
 *
 */
class MegaNode
{
    public:
		enum {
			TYPE_UNKNOWN = -1,
			TYPE_FILE = 0,
			TYPE_FOLDER,
			TYPE_ROOT,
			TYPE_INCOMING,
            TYPE_RUBBISH
		};

        virtual ~MegaNode() = 0;

        /**
         * @brief Creates a copy of this MegaNode object.
         *
         * The resulting object is fully independent of the source MegaNode,
         * it contains a copy of all internal attributes, so it will be valid after
         * the original object is deleted.
         *
         * You are the owner of the returned object
         *
         * @return Copy of the MegaNode object
         */
        virtual MegaNode *copy() = 0;

        /**
         * @brief Returns the type of the node
         *
         * Valid values are:
         * - TYPE_UNKNOWN = -1,
         * Unknown node type
         *
         * - TYPE_FILE = 0,
         * The MegaNode object represents a file in MEGA
         *
         * - TYPE_FOLDER = 1
         * The MegaNode object represents a folder in MEGA
         *
         * - TYPE_ROOT = 2
         * The MegaNode object represents root of the MEGA Cloud Drive
         *
         * - TYPE_INCOMING = 3
         * The MegaNode object represents root of the MEGA Inbox
         *
         * - TYPE_RUBBISH = 4
         * The MegaNode object represents root of the MEGA Ribbish Bin
         *
         * @return Type of the node
         */
        virtual int getType() = 0;

        /**
         * @brief Returns the name of the node
         *
         * The name is only valid for nodes of type TYPE_FILE or TYPE_FOLDER.
         * For other MegaNode types, the name is undefined.
         *
         * The MegaNode object retains the ownership of the returned string. It will
         * be valid until the MegaNode object is deleted.
         *
         * @return Name of the node
         */
        virtual const char* getName() = 0;

        /**
         * @brief Returns the handle of this MegaNode in a Base64-encoded string
         *
         * You take the ownership of the returned string.
         *
         * @return Base64-encoded handle of the node
         */
        virtual const char* getBase64Handle() = 0;

        /**
         * @brief Returns the size of the node
         *
         * The returned value is only valid for nodes of type TYPE_FILE.
         *
         * @return Size of the node
         */
        virtual int64_t getSize() = 0;

        /**
         * @brief Returns the creation time of the node in MEGA (in seconds since the epoch)
         *
         * The returned value is only valid for nodes of type TYPE_FILE or TYPE_FOLDER.
         *
         * @return Creation time of the node (in seconds since the epoch)
         */
        virtual int64_t getCreationTime() = 0;

        /**
         * @brief Returns the modification time of the file that was uploaded to MEGA (in seconds since the epoch)
         *
         * The returned value is only valid for nodes of type TYPE_FILE.
         *
         * @return Modification time of the file that was uploaded to MEGA (in seconds since the epoch)
         */
        virtual int64_t getModificationTime() = 0;

        /**
         * @brief Returns a handle to identify this MegaNode
         *
         * You can use MegaApi::getNodeByHandle to recover the node later.
         *
         * @return Handle that identifies this MegaNode
         */
        virtual MegaHandle getHandle() = 0;

        /**
         * @brief Returns the key of the node in a Base64-encoded string
         *
         * The return value is only valid for nodes of type TYPE_FILE
         *
         * You take the ownership of the returned string.
         *
         * @return Returns the key of the node.
         */
        virtual const char* getBase64Key() = 0;

        /**
         * @brief Returns the tag of the operation that created/modified this node in MEGA
         *
         * Every request and every synchronization has a tag that identifies it.
         * When a request creates or modifies a node, the tag is associated with the node
         * at runtime, this association is lost after a reload of the filesystem or when
         * the SDK is closed.
         *
         * This tag is specially useful to know if a node reported in MegaListener::onNodesUpdate or
         * MegaGlobalListener::onNodesUpdate was modified by a local operation (tag != 0) or by an
         * external operation, made by another MEGA client (tag == 0).
         *
         * If the node hasn't been created/modified during the current execution, this function returns 0
         *
         * @return The tag associated with the node.
         */
        virtual int getTag() = 0;

        /**
         * @brief Returns true if this node represents a file (type == TYPE_FILE)
         * @return true if this node represents a file, otherwise false
         */
        virtual bool isFile() = 0;

        /**
         * @brief Returns true this node represents a folder or a root node
         *
         * @return true this node represents a folder or a root node
         */
        virtual bool isFolder() = 0;

        /**
         * @brief Returns true if this node has been removed from the MEGA account
         *
         * This value is only useful for nodes notified by MegaListener::onNodesUpdate or
         * MegaGlobalListener::onNodesUpdate that can notify about deleted nodes.
         *
         * In other cases, the return value of this function will be always false.
         *
         * @return True if this node has been removed from the MEGA account
         */
        virtual bool isRemoved() = 0;

        /**
         * @brief Returns true if the node has an associated thumbnail
         * @return true if the node has an associated thumbnail
         */
        virtual bool hasThumbnail() = 0;

        /**
         * @brief Returns true if the node has an associated preview
         * @return true if the node has an associated preview
         */
        virtual bool hasPreview() = 0;

        /**
         * @brief Returns true if this is a public node
         *
         * Only MegaNode objects generated with MegaApi::getPublicMegaNode
         * will return true.
         *
         * @return true if this is a public node
         */
        virtual bool isPublic() = 0;

        /**
         * @brief Returns a string that contains the decryption key of the file (in binary format)
         *
         * The MegaNode object retains the ownership of the returned pointer. It will be valid until the deletion
         * of the MegaNode object.
         *
         * @return Decryption key of the file (in binary format)
         * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
         * Use MegaNode::getBase64Key instead
         */
        virtual std::string* getNodeKey() = 0;

        /**
         * @brief Returns a string that contains the encrypted attributes of the file (in binary format)
         *
         * The return value is only valid for public nodes or undecrypted nodes. In all other cases this function
         * will return an empty string.
         *
         * The MegaNode object retains the ownership of the returned pointer. It will be valid until the deletion
         * of the MegaNode object.
         *
         * @return Encrypted attributes of the file (in binary format)
         * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
         * Use MegaNode::getName and MegaNode::getModificationTime and MegaApi::getFingerprint. They provide the same information,
         * decrypted and in a manageable format.
         */
        virtual std::string* getAttrString() = 0;

#ifdef ENABLE_SYNC
        /**
         * @brief Returns true if this node was deleted from the MEGA account by the
         * synchronization engine
         *
         * This value is only useful for nodes notified by MegaListener::onNodesUpdate or
         * MegaGlobalListener::onNodesUpdate that can notify about deleted nodes.
         *
         * In other cases, the return value of this function will be always false.
         *
         * @return True if this node was deleted from the MEGA account by the synchronization engine
         */
        virtual bool isSyncDeleted() = 0;

        /**
         * @brief Returns the local path associated with this node
         *
         * Only synchronized nodes has an associated local path, for all other nodes
         * the return value will be an empty string.
         *
         * @return The local path associated with this node or an empty string if the node isn't synced-
         */
        virtual std::string getLocalPath() = 0;
#endif

};

/**
 * @brief Represents an user in MEGA
 *
 * It allows to get all data related to an user in MEGA. It can be also used
 * to start SDK requests (MegaApi::share MegaApi::removeContact, etc.)
 *
 * Objects of this class aren't live, they are snapshots of the state of an user
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can get the contacts of an account using
 * MegaApi::getContacts and MegaApi::getContact.
 *
 */
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

        /**
         * @brief Creates a copy of this MegaUser object.
         *
         * The resulting object is fully independent of the source MegaUser,
         * it contains a copy of all internal attributes, so it will be valid after
         * the original object is deleted.
         *
         * You are the owner of the returned object
         *
         * @return Copy of the MegaUser object
         */
		virtual MegaUser *copy() = 0;

        /**
         * @brief Returns the email associated with the contact.
         *
         * The email can be used to recover the MegaUser object later using MegaApi::getContact
         *
         * The MegaUser object retains the ownership of the returned string, it will be valid until
         * the MegaUer object is deleted.
         *
         * @return The email associated with the contact.
         */
		virtual const char* getEmail() = 0;

        /**
         * @brief Get the current visibility of the contact
         *
         * The returned value will be one of these:
         *
         * - VISIBILITY_UNKNOWN = -1
         * The visibility of the contact isn't know
         *
         * - VISIBILITY_HIDDEN = 0
         * The contact is currently hidden
         *
         * - VISIBILITY_VISIBLE = 1
         * The contact is currently visible
         *
         * - VISIBILITY_ME = 2
         * The contact is the owner of the account being used by the SDK
         *
         * @return Current visibility of the contact
         */
		virtual int getVisibility() = 0;

        /**
         * @brief Returns the timestamp when the contact was added to the contact list (in seconds since the epoch)
         * @return Timestamp when the contact was added to the contact list (in seconds since the epoch)
         */
		virtual time_t getTimestamp() = 0;
};

/**
 * @brief Represents the outbound sharing of a folder with an user in MEGA
 *
 * It allows to get all data related to the sharing. You can start sharing a folder with
 * a contact or cancel an existing sharing using MegaApi::share. A public link of a folder
 * is also considered a sharing and can be cancelled.
 *
 * Objects of this class aren't live, they are snapshots of the state of the sharing
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can get current active sharings using MegaApi::getOutShares
 *
 */
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

        /**
         * @brief Creates a copy of this MegaShare object
         *
         * The resulting object is fully independent of the source MegaShare,
         * it contains a copy of all internal attributes, so it will be valid after
         * the original object is deleted.
         *
         * You are the owner of the returned object
         *
         * @return Copy of the MegaShare object
         */
		virtual MegaShare *copy() = 0;

        /**
         * @brief Returns the email of the user with whom we are sharing the folder
         *
         * For public shared folders, this function return NULL
         *
         * @return The email of the user with whom we share the folder, or NULL if it's a public folder
         */
		virtual const char *getUser() = 0;

        /**
         * @brief Returns the handle of the folder that is being shared
         * @return The handle of the folder that is being shared
         */
        virtual MegaHandle getNodeHandle() = 0;

        /**
         * @brief Returns the access level of the sharing
         *
         * Possible return values are:
         * - ACCESS_UNKNOWN = -1
         * It means that the access level is unknown
         *
         * - ACCESS_READ = 0
         * The user can read the folder only
         *
         * - ACCESS_READWRITE = 1
         * The user can read and write the folder
         *
         * - ACCESS_FULL = 2
         * The user has full permissions over the folder
         *
         * - ACCESS_OWNER = 3
         * The user is the owner of the folder
         *
         * @return The access level of the sharing
         */
		virtual int getAccess() = 0;

        /**
         * @brief Returns the timestamp when the sharing was created (in seconds since the epoch)
         * @return The timestamp when the sharing was created (in seconds since the epoch)
         */
        virtual int64_t getTimestamp() = 0;
};

/**
 * @brief List of MegaNode objects
 *
 * A MegaNodeList has the ownership of the MegaNode objects that it contains, so they will be
 * only valid until the NodeList is deleted. If you want to retain a MegaMode returned by
 * a MegaNodeList, use MegaNode::copy.
 *
 * Objects of this class are immutable.
 *
 * @see MegaApi::getChildren, MegaApi::search, MegaApi::getInShares
 */
class MegaNodeList
{
	public:
        virtual ~MegaNodeList() = 0;

        /**
         * @brief Returns the MegaNode at the position i in the MegaNodeList
         *
         * The MegaNodeList retains the ownership of the returned MegaNode. It will be only valid until
         * the MegaNodeList is deleted.
         *
         * If the index is >= the size of the list, this function returns NULL.
         *
         * @param i Position of the MegaNode that we want to get for the list
         * @return MegaNode at the position i in the list
         */
		virtual MegaNode* get(int i) = 0;

        /**
         * @brief Returns the number of MegaNode objects in the list
         * @return Number of MegaNode objects in the list
         */
		virtual int size() = 0;
};

/**
 * @brief List of MegaUser objects
 *
 * An MegaUserList has the ownership of the MegaUser objects that it contains, so they will be
 * only valid until the MegaUserList is deleted. If you want to retain a MegaUser returned by
 * a MegaUserList, use UserList::copy.
 *
 * Objects of this class are immutable.
 *
 * @see MegaApi::getContacts
 *
 */
class MegaUserList
{
	public:
        virtual ~MegaUserList() = 0;

        /**
         * @brief Returns the MegaUser at the position i in the MegaUserList
         *
         * The MegaUserList retains the ownership of the returned MegaUser. It will be only valid until
         * the MegaUserList is deleted.
         *
         * If the index is >= the size of the list, this function returns NULL.
         *
         * @param i Position of the MegaUser that we want to get for the list
         * @return MegaUser at the position i in the list
         */
		virtual MegaUser* get(int i) = 0;

        /**
         * @brief Returns the number of MegaUser objects in the list
         * @return Number of MegaUser objects in the list
         */
        virtual int size() = 0;
};

/**
 * @brief List of MegaShare objects
 *
 * A MegaShareList has the ownership of the MegaShare objects that it contains, so they will be
 * only valid until the MegaShareList is deleted. If you want to retain a MegaShare returned by
 * a MegaShareList, use MegaShare::copy.
 *
 * Objects of this class are immutable.
 *
 * @see MegaApi::getOutShares
 */
class MegaShareList
{
	public:
        virtual ~MegaShareList() = 0;

        /**
         * @brief Returns the MegaShare at the position i in the MegaShareList
         *
         * The MegaShareList retains the ownership of the returned MegaShare. It will be only valid until
         * the MegaShareList is deleted.
         *
         * If the index is >= the size of the list, this function returns NULL.
         *
         * @param i Position of the MegaShare that we want to get for the list
         * @return MegaShare at the position i in the list
         */
		virtual MegaShare* get(int i) = 0;

        /**
         * @brief Returns the number of MegaShare objects in the list
         * @return Number of MegaShare objects in the list
         */
		virtual int size() = 0;
};

/**
 * @brief List of MegaTransfer objects
 *
 * A MegaTransferList has the ownership of the MegaTransfer objects that it contains, so they will be
 * only valid until the MegaTransferList is deleted. If you want to retain a MegaTransfer returned by
 * a MegaTransferList, use MegaTransfer::copy.
 *
 * Objects of this class are immutable.
 *
 * @see MegaApi::getTransfers
 */
class MegaTransferList
{
	public:
        virtual ~MegaTransferList() = 0;

        /**
         * @brief Returns the MegaTransfer at the position i in the MegaTransferList
         *
         * The MegaTransferList retains the ownership of the returned MegaTransfer. It will be only valid until
         * the MegaTransferList is deleted.
         *
         * If the index is >= the size of the list, this function returns NULL.
         *
         * @param i Position of the MegaTransfer that we want to get for the list
         * @return MegaTransfer at the position i in the list
         */
        virtual MegaTransfer* get(int i) = 0;

        /**
         * @brief Returns the number of MegaTransfer objects in the list
         * @return Number of MegaTransfer objects in the list
         */
		virtual int size() = 0;
};

/**
 * @brief Provides information about an asynchronous request
 *
 * Most functions in this API are asynchonous, except the ones that never require to
 * contact MEGA servers. Developers can use listeners (MegaListener, MegaRequestListener)
 * to track the progress of each request. MegaRequest objects are provided in callbacks sent
 * to these listeners and allow developers to know the state of the request, their parameters
 * and their results.
 *
 * Objects of this class aren't live, they are snapshots of the state of the request
 * when the object is created, they are immutable.
 *
 * These objects have a high number of 'getters', but only some of them return valid values
 * for each type of request. Documentation of each request specify which fields are valid.
 *
 */
class MegaRequest
{
	public:
        enum {  TYPE_LOGIN, TYPE_CREATE_FOLDER, TYPE_MOVE, TYPE_COPY,
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

        /**
         * @brief Creates a copy of this MegaRequest object
         *
         * The resulting object is fully independent of the source MegaRequest,
         * it contains a copy of all internal attributes, so it will be valid after
         * the original object is deleted.
         *
         * You are the owner of the returned object
         *
         * @return Copy of the MegaRequest object
         */
		virtual MegaRequest *copy() = 0;

        /**
         * @brief Returns the type of request associated with the object
         * @return Type of request associated with the object
         */
		virtual int getType() const = 0;

        /**
         * @brief Returns a readable string that shows the type of request
         * @return Readable string showing the type of request
         */
		virtual const char *getRequestString() const = 0;

        /**
         * @brief Returns a readable string that shows the type of request
         *
         * This function provides exactly the same result as MegaRequest::getRequestString.
         * It's provided for a better Java compatibility
         *
         * @return Readable string showing the type of request
         */
		virtual const char* toString() const = 0;

        /**
         * @brief Returns a readable string that shows the type of request
         *
         * This function provides exactly the same result as MegaRequest::getRequestString.
         * It's provided for a better Python compatibility
         *
         * @return Readable string showing the type of request
         */
        virtual const char* __str__() const = 0;

        /**
         * @brief Returns the handle of a node related to the request
         * @return Handle of a node related to the request
         */
        virtual MegaHandle getNodeHandle() const = 0;

        /**
         * @brief Returns a link related to the request
         * @return Link related to the request
         */
		virtual const char* getLink() const = 0;

        /**
         * @brief Returns the handle of a parent node related to the request
         * @return Handle of a parent node related to the request
         */
        virtual MegaHandle getParentHandle() const = 0;

        /**
         * @brief Returns a session key related to the request
         * @return Session key related to the request
         */
        virtual const char* getSessionKey() const = 0;

        /**
         * @brief Returns a name related to the request
         * @return Name related to the request
         */
		virtual const char* getName() const = 0;

        /**
         * @brief Returns an email related to the request
         * @return Email related to the request
         */
		virtual const char* getEmail() const = 0;

        /**
         * @brief Returns a password related to the request
         * @return Password related to the request
         */
		virtual const char* getPassword() const = 0;

        /**
         * @brief Returns a new password related to the request
         * @return New password related to the request
         */
		virtual const char* getNewPassword() const = 0;

        /**
         * @brief Returns a private key related to the request
         * @return Private key related to the request
         */
		virtual const char* getPrivateKey() const = 0;

        /**
         * @brief Returns an access level related to the request
         * @return Access level related to the request
         */
		virtual int getAccess() const = 0;

        /**
         * @brief Returns the path of a file related to the request
         * @return Path of a file related to the request
         */
        virtual const char* getFile() const = 0;

        /**
         * @brief Return the number of times that a request has temporarily failed
         * @return Number of times that a request has temporarily failed
         */
		virtual int getNumRetry() const = 0;

        /**
         * @brief Returns a public node related to the request
         *
         * The MegaRequest object retains the ownership of the returned value. It will be valid
         * until the MegaRequest object is deleted.
         *
         * If you want to use the returned node beyond the deletion of the MegaRequest object,
         * you must call MegaNode::copy or use MegaRequest::getPublicMegaNode instead
         *
         * @return Public node related to the request
         *
         * @deprecated This function will be removed in future updates. You should use
         * MegaRequest::getPublicMegaNode instead.
         *
         */
        virtual MegaNode *getPublicNode() const = 0;

        /**
         * @brief Returns a public node related to the request
         *
         * You take the ownership of the returned value.
         *
         * @return Public node related to the request
         */
        virtual MegaNode *getPublicMegaNode() const = 0;

        /**
         * @brief Returns the type of parameter related to the request
         * @return Type of parameter related to the request
         */
        virtual int getParamType() const = 0;

        /**
         * @brief Returns a flag related to the request
         * @return Flag related to the request
         */
        virtual bool getFlag() const = 0;

        /**
         * @brief Returns the number of transferred bytes during the request
         * @return Number of transferred bytes during the request
         */
        virtual long long getTransferredBytes() const = 0;

        /**
         * @brief Returns the number of bytes that the SDK will have to transfer to finish the request
         * @return Number of bytes that the SDK will have to transfer to finish the request
         */
        virtual long long getTotalBytes() const = 0;

        /**
         * @brief Return the MegaRequestListener associated with this request
         *
         * This function will return NULL if there isn't an associated request listener.
         *
         * @return MegaRequestListener associated with this request
         */
		virtual MegaRequestListener *getListener() const = 0;

        /**
         * @brief Returns details related to the MEGA account
         * @return Details related to the MEGA account
         */
		virtual MegaAccountDetails *getMegaAccountDetails() const = 0;

        /**
         * @brief Returns available pricing plans to upgrade a MEGA account
         * @return Available pricing plans to upgrade a MEGA account
         */
        virtual MegaPricing *getPricing() const = 0;


        /**
         * @brief Returns the tag of a transfer related to the request
         * @return Tag of a transfer related to the request
         */
        virtual int getTransferTag() const = 0;

        /**
         * @brief Returns the number of details related to this request
         * @return Number of details related to this request
         */
        virtual int getNumDetails() const = 0;
};

/**
 * @brief Provides information about a transfer
 *
 * Developers can use listeners (MegaListener, MegaTransferListener)
 * to track the progress of each transfer. MegaTransfer objects are provided in callbacks sent
 * to these listeners and allow developers to know the state of the transfers, their parameters
 * and their results.
 *
 * Objects of this class aren't live, they are snapshots of the state of the transfer
 * when the object is created, they are immutable.
 *
 */
class MegaTransfer
{
	public:
        enum {TYPE_DOWNLOAD, TYPE_UPLOAD};
        
		virtual ~MegaTransfer() = 0;

        /**
         * @brief Creates a copy of this MegaTransfer object
         *
         * The resulting object is fully independent of the source MegaTransfer,
         * it contains a copy of all internal attributes, so it will be valid after
         * the original object is deleted.
         *
         * You are the owner of the returned object
         *
         * @return Copy of the MegaTransfer object
         */
        virtual MegaTransfer *copy() = 0;

        /**
         * @brief Returns the type of the transfer (TYPE_DOWNLOAD, TYPE_UPLOAD)
         * @return The type of the transfer (TYPE_DOWNLOAD, TYPE_UPLOAD)
         */
		virtual int getType() const = 0;

        /**
         * @brief Returns a readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         * @return Readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         */
		virtual const char * getTransferString() const = 0;

        /**
         * @brief Returns a readable string that shows the type of the transfer
         *
         * This function provides exactly the same result as MegaTransfer::getTransferString (UPLOAD, DOWNLOAD)
         * It's provided for a better Java compatibility
         *
         * @return Readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         */
        virtual const char* toString() const = 0;

        /**
         * @brief Returns a readable string that shows the type of the transfer
         *
         * This function provides exactly the same result as MegaTransfer::getTransferString (UPLOAD, DOWNLOAD)
         * It's provided for a better Python compatibility
         *
         * @return Readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         */
		virtual const char* __str__() const = 0;

        /**
         * @brief Returns the starting time of the request (in deciseconds)
         *
         * The returned value is a monotonic time since some unspecified starting point expressed in
         * deciseconds.
         *
         * @return Starting time of the request (in deciseconds)
         */
        virtual int64_t getStartTime() const = 0;

        /**
         * @brief Returns the number of transferred bytes during this request
         * @return Transferred bytes during this request
         */
		virtual long long getTransferredBytes() const = 0;

        /**
         * @brief Returns the total bytes to be transferred to complete the transfer
         * @return Total bytes to be transferred to complete the transfer
         */
		virtual long long getTotalBytes() const = 0;

        /**
         * @brief Returns the local path related to this request
         *
         * For uploads, this function returns the path to the source file. For downloads, it
         * returns the path of the destination file.
         *
         * @return Local path related to this request
         */
		virtual const char* getPath() const = 0;

        /**
         * @brief Returns the parent path related to this request
         *
         * For uploads, this function returns the path to the folder containing the source file.
         * For downloads, it returns that path to the folder containing the destination file.
         *
         * @return Parent path related to this request
         */
		virtual const char* getParentPath() const = 0;

        /**
         * @brief Returns the handle related to this transfer
         *
         * For downloads, this function returns the handle of the source node. For uploads,
         * it always returns mega::INVALID_HANDLE.
         *
         * @return The handle of the downloaded node, or mega::INVALID_HANDLE for uploads.
         */
        virtual MegaHandle getNodeHandle() const = 0;

        /**
         * @brief Returns the handle of the parent node related to this transfer
         *
         * For downloads, this function returns always mega::INVALID_HANDLE. For uploads,
         * it returns the handle of the destination node (folder) for the uploaded file.
         *
         * @return The handle of the destination folder for uploads, or mega::INVALID_HANDLE for downloads.
         */
        virtual MegaHandle getParentHandle() const = 0;

        /**
         * @brief Returns the starting position of the transfer for streaming downloads
         *
         * The return value of this fuction will be 0 if the transfer isn't a streaming
         * download (MegaApi::startStreaming)
         *
         * @return Starting position of the transfer for streaming downloads, otherwise 0
         */
		virtual long long getStartPos() const = 0;

        /**
         * @brief Returns the end position of the transfer for streaming downloads
         *
         * The return value of this fuction will be 0 if the transfer isn't a streaming
         * download (MegaApi::startStreaming)
         *
         * @return End position of the transfer for streaming downloads, otherwise 0
         */
		virtual long long getEndPos() const = 0;

		/**
		 * @brief Returns the name of the file that is being transferred
		 *
		 * It's possible to upload a file with a different name (MegaApi::startUpload). In that case,
		 * this function returns the destination name.
		 *
		 * @return Name of the file that is being transferred
		 */
		virtual const char* getFileName() const = 0;

		/**
		 * @brief Returns the MegaTransferListener object associated with this transfer
		 *
		 * MegaTransferListener objects can be associated with transfers at startup, if a listener
		 * isn't associated, this function will return NULL
		 *
		 * @return Listener associated with this transfer
		 */
		virtual MegaTransferListener* getListener() const = 0;

		/**
		 * @brief Return the number of times that a transfer has temporarily failed
		 * @return Number of times that a transfer has temporarily failed
		 */
		virtual int getNumRetry() const = 0;

		/**
		 * @brief Returns the maximum number of times that the transfer will be retried
		 * @return Mmximum number of times that the transfer will be retried
		 */
		virtual int getMaxRetries() const = 0;

		/**
		 * @brief Returns an integer that identifies this transfer
		 * @return Integer that identifies this transfer
		 */
		virtual int getTag() const = 0;

		/**
		 * @brief Returns the average speed of this transfer
		 * @return Average speed of this transfer
		 */
		virtual long long getSpeed() const = 0;

		/**
		 * @brief Returns the number of bytes transferred since the previous callback
		 * @return Number of bytes transferred since the previous callback
		 * @see MegaListener::onTransferUpdate, MegaTransferListener::onTransferUpdate
		 */
		virtual long long getDeltaSize() const = 0;

		/**
		 * @brief Returns the timestamp when the last data was received (in deciseconds)
		 *
		 * This timestamp doesn't have a defined starting point. Use the difference between
		 * the return value of this function and MegaTransfer::getStartTime to know how
		 * much time the transfer has been running.
		 *
		 * @return Timestamp when the last data was received (in deciseconds)
		 */
        virtual int64_t getUpdateTime() const = 0;

        /**
         * @brief Returns a public node related to the transfer
         *
         * The return value is only valid for downloads of public nodes (MegaApi::startPublicDownload)
         * You take the ownership of the returned value.
         *
         * @return Public node related to the transfer
         * @see MegaApi::startPublicDownload
         */
        virtual MegaNode *getPublicMegaNode() const = 0;


        /**
         * @brief Returns true if this transfer belongs to the synchronization engine
         *
         * A single transfer can upload/download several files with exactly the same contents. If
         * some of these files are being transferred by the synchonization engine, but there is at
         * least one file started by the application, this function returns false.
         *
         * This data is important to know if the transfer is cancellable. Regular transfers are cancellable
         * but synchronization transfers aren't.
         *
         * @return true if this transfer belongs to the synchronization engine, otherwise false
         */
        virtual bool isSyncTransfer() const = 0;

        /**
         * @brief Returns true is this is a streaming transfer
         * @return true if this is a streaming transfer, false otherwise
         * @see MegaApi::startStreaming
         */
        virtual bool isStreamingTransfer() const = 0;

        /**
         * @brief Returns the received bytes since the last callback
         *
         * The returned value is only valid for streaming transfers (MegaApi::startStreaming).
         *
         * @return Received bytes since the last callback
         */
        virtual char *getLastBytes() const = 0;
};

/**
 * @brief Provides information about an error
 */
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

		/**
		 * @brief Creates a new MegaError object
		 * @param Error code for this error
		 */
		MegaError(int errorCode);

		/**
		 * @brief Creates a new MegaError object copying another one
		 * @param MegaError object to be copied
		 */
		MegaError(const MegaError &megaError);
		virtual ~MegaError();

        /**
         * @brief Creates a copy of this MegaError object
         *
         * The resulting object is fully independent of the source MegaError,
         * it contains a copy of all internal attributes, so it will be valid after
         * the original object is deleted.
         *
         * You are the owner of the returned object
         *
         * @return Copy of the MegaError object
         */
        MegaError* copy();

		/**
		 * @brief Returns the error code associated with this MegaError
		 * @return Error code associated with this MegaError
		 */
		int getErrorCode() const;

		/**
		 * @brief Returns a readable description of the error
		 *
		 * This function returns a pointer to a statically allocated buffer.
		 * You don't have to free the returned pointer
		 *
		 * @return Readable description of the error
		 */
		const char* getErrorString() const;

		/**
		 * @brief Returns a readable description of the error
		 *
		 * This function returns a pointer to a statically allocated buffer.
		 * You don't have to free the returned pointer
		 *
		 * This function provides exactly the same result as MegaError::getErrorString.
		 * It's provided for a better Java compatibility
		 *
		 * @return Readable description of the error
		 */
        const char* toString() const;

		/**
		 * @brief Returns a readable description of the error
		 *
		 * This function returns a pointer to a statically allocated buffer.
		 * You don't have to free the returned pointer
		 *
		 * This function provides exactly the same result as MegaError::getErrorString.
		 * It's provided for a better Python compatibility
		 *
		 * @return Readable description of the error
		 */
		const char* __str__() const;

		/**
		 * @brief Provides the error description associated with an error code
		 *
		 * This function returns a pointer to a statically allocated buffer.
		 * You don't have to free the returned pointer
		 *
		 * @param errorCode Error code for which the description will be returned
		 * @return Description associated with the error code
		 */
        static const char *getErrorString(int errorCode);

	protected:
        //< 0 = API error code, > 0 = http error, 0 = No error
		int errorCode;
};

/**
 * @brief Interface to process node trees
 *
 * An implementation of this class can be used to process a node tree passing a pointer to
 * MegaApi::processMegaTree
 */
class MegaTreeProcessor
{
    public:
        /**
         * @brief Function that will be called for all nodes in a node tree
         * @param Node to be processed
         * @return true to continue processing nodes, false to stop
         */
        virtual bool processMegaNode(MegaNode* node);
        virtual ~MegaTreeProcessor();
};

/**
 * @brief Interface to receive information about requests
 *
 * All requests allows to pass a pointer to an implementation of this interface in the last parameter.
 * You can also get information about all requests using MegaApi::addRequestListener
 *
 * MegaListener objects can also receive information about requests
 *
 * This interface uses MegaRequest objects to provide information of requests. Take into account that not all
 * fields of MegaRequest objects are valid for all requests. See the documentation about each request to know
 * which fields contain useful information for each one.
 *
 */
class MegaRequestListener
{
    public:
        /**
         * @brief This function is called when a request is about to start being processed
         *
         * The SDK retains the ownership of the request parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         */
        virtual void onRequestStart(MegaApi* api, MegaRequest *request);

        /**
         * @brief This function is called when a request has finished
         *
         * There won't be more callbacks about this request.
         * The last parameter provides the result of the request. If the request finished without problems,
         * the error code will be API_OK
         *
         * The SDK retains the ownership of the request and error parameters.
         * Don't use them after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         * @param e Error information
         */
        virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);

        /**
         * @brief This function is called to inform about the progres of a request
         *
         * Currently, this callback is only used for fetchNodes (MegaRequest::TYPE_FETCH_NODES) requests
         *
         * The SDK retains the ownership of the request parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         * @see MegaRequest::getTotalBytes MegaRequest::getTransferredBytes
         */
        virtual void onRequestUpdate(MegaApi*api, MegaRequest *request);

        /**
         * @brief This function is called when there is a temporary error processing a request
         *
         * The request continues after this callback, so expect more MegaRequestListener::onRequestTemporaryError or
         * a MegaRequestListener::onRequestFinish callback
         *
         * The SDK retains the ownership of the request and error parameters.
         * Don't use them after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         * @param error Error information
         */
        virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error);
        virtual ~MegaRequestListener();
};

/**
 * @brief Interface to receive information about transfers
 *
 * All transfers allows to pass a pointer to an implementation of this interface in the last parameter.
 * You can also get information about all transfers using MegaApi::addTransferListener
 *
 * MegaListener objects can also receive information about transfers
 *
 */
class MegaTransferListener
{
    public:
        /**
         * @brief This function is called when a transfer is about to start being processed
         *
         * The SDK retains the ownership of the transfer parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param transfer Information about the transfer
         */
        virtual void onTransferStart(MegaApi *api, MegaTransfer *transfer);

        /**
         * @brief This function is called when a transfer has finished
         *
         * The SDK retains the ownership of the transfer and error parameters.
         * Don't use them after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * There won't be more callbacks about this transfer.
         * The last parameter provides the result of the transfer. If the transfer finished without problems,
         * the error code will be API_OK
         *
         * Take into account that when a file is uploaded, an additional request is required to attach the uploaded
         * file to the account. That is automatically made by the SDK, but this means that the file won't be still
         * attached to the account when this callback is received. You can know when the file is finally attached
         * thanks to the MegaGlobalListener::onNodesUpdate MegaListener::onNodesUpdate callbacks.
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         * @param error Error information
         */
        virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error);

        /**
         * @brief This function is called to inform about the progress of a transfer
         *
         * The SDK retains the ownership of the transfer parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         *
         * @see MegaTransfer::getTransferredBytes, MegaTransfer::getSpeed
         */
        virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);

        /**
         * @brief This function is called when there is a temporary error processing a transfer
         *
         * The transfer continues after this callback, so expect more MegaTransferListener::onTransferTemporaryError or
         * a MegaTransferListener::onTransferFinish callback
         *
         * The SDK retains the ownership of the transfer and error parameters.
         * Don't use them after this functions returns.
         *
         * @param api MegaApi object that started the transfer
         * @param request Information about the transfer
         * @param error Error information
         */
        virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error);

        virtual ~MegaTransferListener();

        /**
         * @brief This function is called to provide the last readed bytes of streaming downloads
         *
         * This function won't be called for non streaming downloads. You can get the same buffer
         * provided by this function in MegaTransferListener::onTransferUpdate, using
         * MegaTransfer::getLastBytes MegaTransfer::getDeltaSize.
         *
         * The SDK retains the ownership of the transfer and buffer parameters.
         * Don't use them after this functions returns.
         *
         * This callback is mainly provided for compatibility with other programming languages.
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         * @param buffer Buffer with the last readed bytes
         * @param size Size of the buffer
         * @return true to continue the transfer, false to cancel it
         *
         * @see MegaApi::startStreaming
         */
        virtual bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size);
};

/**
 * @brief Interface to get information about global events
 *
 * You can implement this interface and start receiving events calling MegaApi::addGlobalListener
 *
 * MegaListener objects can also receive global events
 */
class MegaGlobalListener
{
    public:
    #if defined(__ANDROID__) || defined(WINDOWS_PHONE) || defined(TARGET_OS_IPHONE)
        virtual void onUsersUpdate(MegaApi* api);
        virtual void onNodesUpdate(MegaApi* api);
    #else

        /**
         * @brief This function is called when there are new or updated contacts in the account
         * @param api MegaApi object connected to the account
         * @param users List that contains the new or updated contacts
         */
        virtual void onUsersUpdate(MegaApi* api, MegaUserList *users);

        /**
         * @brief This function is called when there are new or updated nodes in the account
         * @param api MegaApi object connected to the account
         * @param nodes List that contains the new or updated nodes
         */
        virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
    #endif

        /**
         * @brief This function is called when an inconsistency is detected in the local cache
         *
         * You should call MegaApi::fetchNodes when this callback is received
         *
         * @param api MegaApi object connected to the account
         */
        virtual void onReloadNeeded(MegaApi* api);
        virtual ~MegaGlobalListener();
};

/**
 * @brief Interface to get all information related to a MEGA account
 *
 * Implementations of this interface can receive all events (request, transfer, global) and two
 * additional events related to the synchronization engine. The SDK will provide a new interface
 * to get synchronization events separately in future updates-
 *
 * Multiple inheritance isn't used for compatibility with other programming languages
 *
 */
class MegaListener
{
    public:
        /**
         * @brief This function is called when a request is about to start being processed
         *
         * The SDK retains the ownership of the request parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         */
        virtual void onRequestStart(MegaApi* api, MegaRequest *request);

        /**
         * @brief This function is called when a request has finished
         *
         * There won't be more callbacks about this request.
         * The last parameter provides the result of the request. If the request finished without problems,
         * the error code will be API_OK
         *
         * The SDK retains the ownership of the request and error parameters.
         * Don't use them after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         * @param e Error information
         */
        virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);

        /**
         * @brief This function is called to inform about the progres of a request
         *
         * Currently, this callback is only used for fetchNodes (MegaRequest::TYPE_FETCH_NODES) requests
         *
         * The SDK retains the ownership of the request parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         * @see MegaRequest::getTotalBytes MegaRequest::getTransferredBytes
         */
        virtual void onRequestUpdate(MegaApi*api, MegaRequest *request);

        /**
         * @brief This function is called when there is a temporary error processing a request
         *
         * The request continues after this callback, so expect more MegaRequestListener::onRequestTemporaryError or
         * a MegaRequestListener::onRequestFinish callback
         *
         * The SDK retains the ownership of the request and error parameters.
         * Don't use them after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param request Information about the request
         * @param error Error information
         */
        virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error);

        /**
         * @brief This function is called when a transfer is about to start being processed
         *
         * The SDK retains the ownership of the transfer parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the request
         * @param transfer Information about the transfer
         */
        virtual void onTransferStart(MegaApi *api, MegaTransfer *transfer);

        /**
         * @brief This function is called when a transfer has finished
         *
         * The SDK retains the ownership of the transfer and error parameters.
         * Don't use them after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * There won't be more callbacks about this transfer.
         * The last parameter provides the result of the transfer. If the transfer finished without problems,
         * the error code will be API_OK
         *
         * Take into account that when a file is uploaded, an additional request is required to attach the uploaded
         * file to the account. That is automatically made by the SDK, but this means that the file won't be still
         * attached to the account when this callback is received. You can know when the file is finally attached
         * thanks to the MegaGlobalListener::onNodesUpdate MegaListener::onNodesUpdate callbacks.
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         * @param error Error information
         */
        virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error);

        /**
         * @brief This function is called to inform about the progress of a transfer
         *
         * The SDK retains the ownership of the transfer parameter.
         * Don't use it after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         *
         * @see MegaTransfer::getTransferredBytes, MegaTransfer::getSpeed
         */
        virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);

        /**
         * @brief This function is called when there is a temporary error processing a transfer
         *
         * The transfer continues after this callback, so expect more MegaTransferListener::onTransferTemporaryError or
         * a MegaTransferListener::onTransferFinish callback
         *
         * The SDK retains the ownership of the transfer and error parameters.
         * Don't use them after this functions returns.
         *
         * @param api MegaApi object that started the transfer
         * @param request Information about the transfer
         * @param error Error information
         */
        virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error);

    #if defined(__ANDROID__) || defined(WINDOWS_PHONE) || defined(TARGET_OS_IPHONE)
        virtual void onUsersUpdate(MegaApi* api);
        virtual void onNodesUpdate(MegaApi* api);
    #else
        /**
         * @brief This function is called when there are new or updated contacts in the account
         * @param api MegaApi object connected to the account
         * @param users List that contains the new or updated contacts
         */
        virtual void onUsersUpdate(MegaApi* api, MegaUserList *users);

        /**
         * @brief This function is called when there are new or updated nodes in the account
         * @param api MegaApi object connected to the account
         * @param nodes List that contains the new or updated nodes
         */
        virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
    #endif

        /**
         * @brief This function is called when an inconsistency is detected in the local cache
         *
         * You should call MegaApi::fetchNodes when this callback is received
         *
         * @param api MegaApi object connected to the account
         */
        virtual void onReloadNeeded(MegaApi* api);

        /**
         * @brief This function is called when the state of a synced file changes
         *
         * Possible values for the state are:
         * -STATE_SYNCED = 1
         * The file is synced with the MEGA account
         *
         * -STATE_PENDING = 2
         * The file isn't synced with the MEGA account. It's waiting to be synced.
         *
         * -STATE_SYNCING = 3
         * The file is being synced with the MEGA account
         *
         * Warning: More powerfull callbacks about synchronizations will be provided in future
         * updates. This function will probably be removed.
         *
         * @param api MegaApi object that is synchronizing files
         * @param filePath Local path of the file
         * @param newState New state of the file
         */
        virtual void onSyncFileStateChanged(MegaApi *api, const char *filePath, int newState);

        /**
         * @brief This function is called when the state of the synchronization changes
         *
         * The SDK calls this function when the state of the synchronization changes, for example
         * from 'scanning' to 'syncing' or 'failed'.
         *
         * Warning: More powerfull callbacks about synchronizations will be provided in future
         * updates. This function will probably be removed.
         *
         * @param api MegaApi object that is synchronizing files
         */
        virtual void onSyncStateChanged(MegaApi *api);

        virtual ~MegaListener();
};

class MegaApiImpl;

/**
 * @brief Allows to control a MEGA account or a shared folder
 *
 * You must provide an appKey to use this SDK. You can generate an appKey for your app for free here:
 * - https://mega.co.nz/#sdk
 *
 * You can enable local node caching by passing a local path in the constructor of this class. That saves many data usage
 * and many time starting your app because the entire filesystem won't have to be downloaded each time. The persistent
 * node cache will only be loaded by logging in with a session key. To take advantage of this feature, apart of passing the
 * local path to the constructor, your application have to save the session key after login (MegaApi::dumpSession) and use
 * it to log in the next time. This is highly recommended also to enhance the security, because in this was the access password
 * doesn't have to be stored by the application.
 *
 * To access MEGA using this SDK, you have to create an object of this class and use one of the MegaApi::login options (to log in
 * to a MEGA account or a public folder). If the login request succeed, call MegaApi::fetchNodes to get the filesystem in MEGA.
 * After that, you can use all other requests, manage the files and start transfers.
 *
 * After using MegaApi::logout you can reuse the same MegaApi object to log in to another MEGA account or a public folder.
 *
 */
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

        /**
         * @brief Constructor suitable for most applications
         * @param appKey AppKey of your application
         * You can generate your AppKey for free here:
         * - https://mega.co.nz/#sdk
         *
         * @param basePath Base path to store the local cache
         * If you pass NULL to this parameter, the SDK won't use any local cache.
         *
         * @param userAgent User agent to use in network requests
         * If you pass NULL to this parameter, a default user agent will be used
         *
         */
        MegaApi(const char *appKey, const char *basePath = NULL, const char *userAgent = NULL);

        /**
         * @brief MegaApi Constructor that allows to use a custom GFX processor
         *
         * The SDK attach thumbnails and previews to all uploaded images. To generate them, it needs a graphics processor.
         * You can build the SDK with one of the provided built-in graphics processors. If none of them is available
         * in your app, you can implement the MegaGfxProcessor interface to provide your custom processor. Please
         * read the documentation of MegaGfxProcessor carefully to ensure that your implementation is valid.
         *
         * @param appKey AppKey of your application
         * You can generate your AppKey for free here:
         * - https://mega.co.nz/#sdk
         *
         * @param processor Image processor. The SDK will use it to generate previews and thumbnails
         * If you pass NULL to this parameter, the SDK will try to use the built-in image processors.
         *
         * @param basePath Base path to store the local cache
         * If you pass NULL to this parameter, the SDK won't use any local cache.
         *
         * @param userAgent User agent to use in network requests
         * If you pass NULL to this parameter, a default user agent will be used
         *
         */
        MegaApi(const char *appKey, MegaGfxProcessor* processor, const char *basePath = NULL, const char *userAgent = NULL);

#ifdef ENABLE_SYNC
        /**
         * @brief Special constructor to allow non root synchronization on OSX
         *
         * The synchronization engine needs to read filesystem notifications from /dev/fsevents to work efficiently.
         * Only root can open this file, so if you want to use the synchronization engine on OSX you will have to
         * run the application as root, or to use this constructor to provide an open file descriptor to /dev/fsevents
         *
         * You could open /dev/fsevents in a minimal loader with root permissions and provide the file descriptor
         * to a new executable that uses this constructor. Here you have an example implementation of the loader:
         *
         * int main(int argc, char *argv[])
         * {
         *     char buf[16];
         *     int fd = open("/dev/fsevents", O_RDONLY);
         *     seteuid(getuid());
         *     snprintf(buf, sizeof(buf), "%d", fd);
         *     execl("executablePath", buf, NULL);
         *     return 0;
         * }
         *
         * If you use another constructor. The synchronization engine will still work on OSX, but it will scan all files
         * regularly so it will be much less efficient.
         *
         * @param appKey AppKey of your application
         * You can generate your AppKey for free here:
         * - https://mega.co.nz/#sdk
         *
         * @param basePath Base path to store the local cache
         * If you pass NULL to this parameter, the SDK won't use any local cache.
         *
         * @param userAgent User agent to use in network requests
         * If you pass NULL to this parameter, a default user agent will be used
         *
         * @param fseventsfd Open file descriptor of /dev/fsevents
         *
         */
        MegaApi(const char *appKey, const char *basePath, const char *userAgent, int fseventsfd);
#endif

        virtual ~MegaApi();


        /**
         * @brief Register a listener to receive all events (requests, transfers, global, synchronization)
         *
         * You can use MegaApi::removeListener to stop receiving events.
         *
         * @param listener Listener that will receive all events (requests, transfers, global, synchronization)
         */
        void addListener(MegaListener* listener);

        /**
         * @brief Register a listener to receive all events about requests
         *
         * You can use MegaApi::removeRequestListener to stop receiving events.
         *
         * @param listener Listener that will receive all events about requests
         */
        void addRequestListener(MegaRequestListener* listener);

        /**
         * @brief Register a listener to receive all events about transfers
         *
         * You can use MegaApi::removeTransferListener to stop receiving events.
         *
         * @param listener Listener that will receive all events about transfers
         */
        void addTransferListener(MegaTransferListener* listener);

        /**
         * @brief Register a listener to receive global events
         *
         * You can use MegaApi::removeGlobalListener to stop receiving events.
         *
         * @param listener Listener that will receive global events
         */
        void addGlobalListener(MegaGlobalListener* listener);

        /**
         * @brief Unregister a listener
         *
         * This listener won't receive more events.
         *
         * @param listener Object that is unregistered
         */
        void removeListener(MegaListener* listener);

        /**
         * @brief Unregister a MegaRequestListener
         *
         * This listener won't receive more events.
         *
         * @param listener Object that is unregistered
         */
        void removeRequestListener(MegaRequestListener* listener);

        /**
         * @brief Unregister a MegaTransferListener
         *
         * This listener won't receive more events.
         *
         * @param listener Object that is unregistered
         */
        void removeTransferListener(MegaTransferListener* listener);

        /**
         * @brief Unregister a MegaGlobalListener
         *
         * This listener won't receive more events.
         *
         * @param listener Object that is unregistered
         */
        void removeGlobalListener(MegaGlobalListener* listener);

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
        MegaTransferList *getTransfers();

#ifdef ENABLE_SYNC
        //Sync
        int syncPathState(std::string *path);
        MegaNode *getSyncedNode(std::string *path);
        void syncFolder(const char *localFolder, MegaNode *megaFolder);
        void resumeSync(const char *localFolder, long long localfp, MegaNode *megaFolder);
        void removeSync(MegaHandle nodeMegaHandle, MegaRequestListener *listener=NULL);
        int getNumActiveSyncs();
        void stopSyncs(MegaRequestListener *listener=NULL);
        bool isIndexing();
        bool isSynced(MegaNode *n);
        void setExcludedNames(std::vector<std::string> *excludedNames);
        bool moveToLocalDebris(const char *path);
        bool isSyncable(const char *name);
        std::string getLocalPath(MegaNode *node);
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
        MegaNodeList* getChildren(MegaNode *parent, int order=1);
        MegaNode *getChildNode(MegaNode *parent, const char* name);
        MegaNode *getParentNode(MegaNode *node);
        const char* getNodePath(MegaNode *node);
        MegaNode *getNodeByPath(const char *path, MegaNode *n = NULL);
        MegaNode *getNodeByHandle(MegaHandle MegaHandler);
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
        bool hasFingerprint(const char* fingerprint);

        //Permissions
        MegaError checkAccess(MegaNode* node, int level);
        MegaError checkMove(MegaNode* node, MegaNode* target);

        MegaNode *getRootNode();
        MegaNode* getInboxNode();
        MegaNode *getRubbishNode();

        MegaNodeList* search(MegaNode* node, const char* searchString, bool recursive = 1);
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
