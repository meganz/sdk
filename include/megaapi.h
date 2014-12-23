/**
 * @file megaapi.h
 * @brief Public header file of the intermediate layer for the MEGA C++ SDK.
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
class MegaSync;
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
     * That URL must follow this format: "<scheme>://<hostname|ip>:<port>"
     *
     * This is a valid example: http://127.0.0.1:8080
     *
     * @param proxyURL URL of the proxy: "<scheme>://<hostname|ip>:<port>"
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

private:
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
     * For logs generated inside the SDK, this will contain the source file and the line of code.
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

        enum
        {
            CHANGE_TYPE_REMOVED         = 0x01,
            CHANGE_TYPE_ATTRIBUTES      = 0x02,
            CHANGE_TYPE_OWNER           = 0x04,
            CHANGE_TYPE_TIMESTAMP       = 0x08,
            CHANGE_TYPE_FILE_ATTRIBUTES = 0x10,
            CHANGE_TYPE_INSHARE         = 0x20,
            CHANGE_TYPE_OUTSHARE        = 0x40,
            CHANGE_TYPE_PARENT          = 0x80
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
         * The MegaNode object represents root of the MEGA Rubbish Bin
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
         * Every request and every transfer has a tag that identifies it.
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
         * @return true if this node has been removed from the MEGA account
         */
        virtual bool isRemoved() = 0;

        /**
         * @brief Returns true if this node has an specific change
         *
         * This value is only useful for nodes notified by MegaListener::onNodesUpdate or
         * MegaGlobalListener::onNodesUpdate that can notify about node modifications.
         *
         * In other cases, the return value of this function will be always false.
         *
         * @param changeType The type of change to check. It can be one of the following values:
         *
         * - MegaNode::CHANGE_TYPE_REMOVED         = 0x01
         * Check if the node is being removed
         *
         * - MegaNode::CHANGE_TYPE_ATTRIBUTES      = 0x02
         * Check if an attribute of the node has changed, usually the namespace name
         *
         * - MegaNode::CHANGE_TYPE_OWNER           = 0x04
         * Check if the owner of the node has changed
         *
         * - MegaNode::CHANGE_TYPE_TIMESTAMP       = 0x08
         * Check if the modification time of the node has changed
         *
         * - MegaNode::CHANGE_TYPE_FILE_ATTRIBUTES = 0x10
         * Check if file attributes have changed, usually the thumbnail or the preview for images
         *
         * - MegaNode::CHANGE_TYPE_INSHARE         = 0x20
         * Check if the node is a new or modified inshare
         *
         * - MegaNode:: CHANGE_TYPE_OUTSHARE       = 0x40
         * Check if the node is a new or modified outshare
         *
         * - MegaNode::CHANGE_TYPE_PARENT          = 0x80
         * Check if the parent of the node has changed
         *
         * @return true if this node has an specific change
         */
        virtual bool hasChanged(int changeType) = 0;

        /**
         * @brief Returns a bit field with the changes of the node
         *
         * This value is only useful for nodes notified by MegaListener::onNodesUpdate or
         * MegaGlobalListener::onNodesUpdate that can notify about node modifications.
         *
         * @return The returned value is an OR combination of these flags:
         *
         *- MegaNode::CHANGE_TYPE_REMOVED         = 0x01
         * The node is being removed
         *
         * - MegaNode::CHANGE_TYPE_ATTRIBUTES      = 0x02
         * An attribute of the node has changed, usually the namespace name
         *
         * - MegaNode::CHANGE_TYPE_OWNER           = 0x04
         * The owner of the node has changed
         *
         * - MegaNode::CHANGE_TYPE_TIMESTAMP       = 0x08
         * The modification time of the node has changed
         *
         * - MegaNode::CHANGE_TYPE_FILE_ATTRIBUTES = 0x10
         * File attributes have changed, usually the thumbnail or the preview for images
         *
         * - MegaNode::CHANGE_TYPE_INSHARE         = 0x20
         * The node is a new or modified inshare
         *
         * - MegaNode:: CHANGE_TYPE_OUTSHARE       = 0x40
         * The node is a new or modified outshare
         *
         * - MegaNode::CHANGE_TYPE_PARENT          = 0x80
         * The parent of the node has changed
         */
        virtual int getChanges() = 0;

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
         * the MegaUser object is deleted.
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

		virtual MegaNodeList *copy() = 0;

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
 * A MegaUserList has the ownership of the MegaUser objects that it contains, so they will be
 * only valid until the MegaUserList is deleted. If you want to retain a MegaUser returned by
 * a MegaUserList, use MegaUser::copy.
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

		virtual MegaUserList *copy() = 0;

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
		enum {
			TYPE_LOGIN, TYPE_CREATE_FOLDER, TYPE_MOVE, TYPE_COPY,
			TYPE_RENAME, TYPE_REMOVE, TYPE_SHARE,
			TYPE_IMPORT_LINK, TYPE_EXPORT, TYPE_FETCH_NODES, TYPE_ACCOUNT_DETAILS,
			TYPE_CHANGE_PW, TYPE_UPLOAD, TYPE_LOGOUT,
			TYPE_GET_PUBLIC_NODE, TYPE_GET_ATTR_FILE,
			TYPE_SET_ATTR_FILE, TYPE_GET_ATTR_USER,
			TYPE_SET_ATTR_USER, TYPE_RETRY_PENDING_CONNECTIONS,
			TYPE_ADD_CONTACT, TYPE_REMOVE_CONTACT, TYPE_CREATE_ACCOUNT,
			TYPE_CONFIRM_ACCOUNT,
			TYPE_QUERY_SIGNUP_LINK, TYPE_ADD_SYNC, TYPE_REMOVE_SYNC,
			TYPE_REMOVE_SYNCS, TYPE_PAUSE_TRANSFERS,
			TYPE_CANCEL_TRANSFER, TYPE_CANCEL_TRANSFERS,
			TYPE_DELETE, TYPE_REPORT_EVENT, TYPE_CANCEL_ATTR_FILE,
            TYPE_GET_PRICING, TYPE_GET_PAYMENT_URL, TYPE_GET_USER_DATA
		};

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
         *
         * This function returns a pointer to a statically allocated buffer.
         * You don't have to free the returned pointer
         *
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
         *
         * This value is valid for these requests:
         * - MegaApi::moveNode - Returns the handle of the node to move
         * - MegaApi::copyNode - Returns the handle of the node to copy
         * - MegaApi::renameNode - Returns the handle of the node to rename
         * - MegaApi::remove - Returns the handle of the node to remove
         * - MegaApi::sendFileToUser - Returns the handle of the node to send
         * - MegaApi::share - Returns the handle of the folder to share
         * - MegaApi::getThumbnail - Returns the handle of the node to get the thumbnail
         * - MegaApi::getPreview - Return the handle of the node to get the preview
         * - MegaApi::cancelGetThumbnail - Return the handle of the node
         * - MegaApi::cancelGetPreview - Returns the handle of the node
         * - MegaApi::setThumbnail - Returns the handle of the node
         * - MegaApi::setPreview - Returns the handle of the node
         * - MegaApi::exportNode - Returns the handle of the node
         * - MegaApi::disableExport - Returns the handle of the node
         * - MegaApi::getPaymentUrl - Returns the handle of the product
         * - MegaApi::syncFolder - Returns the handle of the folder in MEGA
         * - MegaApi::resumeSync - Returns the handle of the folder in MEGA
         * - MegaApi::removeSync - Returns the handle of the folder in MEGA
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::createFolder - Returns the handle of the new folder
         * - MegaApi::copyNode - Returns the handle of the new node
         * - MegaApi::importFileLink - Returns the handle of the new node
         *
         * @return Handle of a node related to the request
         */
        virtual MegaHandle getNodeHandle() const = 0;

        /**
         * @brief Returns a link related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::querySignupLink - Returns the confirmation link
         * - MegaApi::confirmAccount - Returns the confirmation link
         * - MegaApi::fastConfirmAccount - Returns the confirmation link
         * - MegaApi::loginToFolder - Returns the link to the folder
         * - MegaApi::importFileLink - Returns the link to the file to import
         * - MegaApi::getPublicNode - Returns the link to the file
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::exportNode - Returns the public link
         * - MegaApi::getPaymentUrl - Returns the payment link
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return Link related to the request
         */
		virtual const char* getLink() const = 0;

        /**
         * @brief Returns the handle of a parent node related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::createFolder - Returns the handle of the parent folder
         * - MegaApi::moveNode - Returns the handle of the new parent for the node
         * - MegaApi::copyNode - Returns the handle of the parent for the new node
         * - MegaApi::importFileLink - Returns the handle of the node that receives the imported file
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::syncFolder - Returns a fingerprint of the local folder, to resume the sync with (MegaApi::resumeSync)
         *
         * @return Handle of a parent node related to the request
         */
        virtual MegaHandle getParentHandle() const = 0;

        /**
         * @brief Returns a session key related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::fastLogin - Returns session key used to access the account
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return Session key related to the request
         */
        virtual const char* getSessionKey() const = 0;

        /**
         * @brief Returns a name related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::createAccount - Returns the name of the user
         * - MegaApi::fastCreateAccount - Returns the name of the user
         * - MegaApi::createFolder - Returns the name of the new folder
         * - MegaApi::renameNode - Returns the new name for the node
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::querySignupLink - Returns the name of the user
         * - MegaApi::confirmAccount - Returns the name of the user
         * - MegaApi::fastConfirmAccount - Returns the name of the user
         * - MegaApi::getUserData - Returns the name of the user
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return Name related to the request
         */
		virtual const char* getName() const = 0;

        /**
         * @brief Returns an email related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::login - Returns the email of the account
         * - MegaApi::fastLogin - Returns the email of the account
         * - MegaApi::loginToFolder - Returns the string "FOLDER"
         * - MegaApi::createAccount - Returns the email for the account
         * - MegaApi::fastCreateAccount - Returns the email for the account
         * - MegaApi::sendFileToUser - Returns the email of the user that receives the node
         * - MegaApi::share - Returns the email that receives the shared folder
         * - MegaApi::getUserAvatar - Returns the email of the user to get the avatar
         * - MegaApi::addContact - Returns the email of the contact
         * - MegaApi::removeContact - Returns the email of the contact
         * - MegaApi::getUserData - Returns the email of the contact
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::querySignupLink - Returns the email of the account
         * - MegaApi::confirmAccount - Returns the email of the account
         * - MegaApi::fastConfirmAccount - Returns the email of the account
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return Email related to the request
         */
		virtual const char* getEmail() const = 0;

        /**
         * @brief Returns a password related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::login - Returns the password of the account
         * - MegaApi::fastLogin - Returns the hash of the email
         * - MegaApi::createAccount - Returns the password for the account
         * - MegaApi::confirmAccount - Returns the password for the account
         * - MegaApi::changePassword - Returns the old password of the account (first parameter)
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getUserData - Returns the public RSA key of the contact, Base64-encoded
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return Password related to the request
         */
		virtual const char* getPassword() const = 0;

        /**
         * @brief Returns a new password related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::changePassword - Returns the new password for the account
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return New password related to the request
         */
		virtual const char* getNewPassword() const = 0;

        /**
         * @brief Returns a private key related to the request
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::fastLogin - Returns the base64pwKey parameter
         * - MegaApi::fastCreateAccount - Returns the base64pwKey parameter
         * - MegaApi::fastConfirmAccount - Returns the base64pwKey parameter
         *
         * @return Private key related to the request
         */
		virtual const char* getPrivateKey() const = 0;

        /**
         * @brief Returns an access level related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::share - Returns the access level for the shared folder
         * - MegaApi::exportNode - Returns true
         * - MegaApi::disableExport - Returns false
         *
         * @return Access level related to the request
         */
		virtual int getAccess() const = 0;

        /**
         * @brief Returns the path of a file related to the request
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::getThumbnail - Returns the destination path for the thumbnail
         * - MegaApi::getPreview - Returns the destination path for the preview
         * - MegaApi::getUserAvatar - Returns the destination path for the avatar
         * - MegaApi::setThumbnail - Returns the source path for the thumbnail
         * - MegaApi::setPreview - Returns the source path for the preview
         * - MegaApi::setAvatar - Returns the source path for the avatar
         * - MegaApi::syncFolder - Returns the path of the local folder
         * - MegaApi::resumeSync - Returns the path of the local folder
         *
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
         * This value is valid for these requests:
         * - MegaApi::copyNode - Returns the node to copy (if it is a public node)
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getPublicNode - Returns the public node
         *
         * @return Public node related to the request
         */
        virtual MegaNode *getPublicMegaNode() const = 0;

        /**
         * @brief Returns the type of parameter related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::getThumbnail - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         * - MegaApi::getPreview - Returns MegaApi::ATTR_TYPE_PREVIEW
         * - MegaApi::cancelGetThumbnail - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         * - MegaApi::cancelGetPreview - Returns MegaApi::ATTR_TYPE_PREVIEW
         * - MegaApi::setThumbnail - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         * - MegaApi::setPreview - Returns MegaApi::ATTR_TYPE_PREVIEW
         * - MegaApi::submitFeedback - Returns MegaApi::EVENT_FEEDBACK
         * - MegaApi::reportDebugEvent - Returns MegaApi::EVENT_DEBUG
         * - MegaApi::cancelTransfers - Returns MegaTransfer::TYPE_DOWNLOAD if downloads are cancelled or
         * MegaTransfer::TYPE_UPLOAD if uploads are cancelled
         *
         * @return Type of parameter related to the request
         */
        virtual int getParamType() const = 0;

        /**
         * @brief Returns a text relative to this request
         *
         * This value is valid for these requests:
         * - MegaApi::submitFeedback - Returns the comment about the app
         * - MegaApi::reportDebugEvent - Returns the debug message
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getUserData - Returns the XMPP ID of the contact
         *
         * @return Text relative to this request
         */
        virtual const char *getText() const = 0;

        /**
         * @brief Returns a number related to this request
         *
         * This value is valid for these requests:
         * - MegaApi::retryPendingConnections - Returns if transfers are retried
         * - MegaApi::submitFeedback - Returns the rating for the app
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::resumeSync - Returns the fingerprint of the local file
         *
         * @return Number related to this request
         */
        virtual long long getNumber() const = 0;

        /**
         * @brief Returns a flag related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::retryPendingConnections - Returns if request are disconnected
         * - MegaApi::pauseTransfers - Returns true if transfers were paused, false if they were resumed
         *
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
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getAccountDetails - Details of the MEGA account
         *
         * @return Details related to the MEGA account
         */
		virtual MegaAccountDetails *getMegaAccountDetails() const = 0;

        /**
         * @brief Returns available pricing plans to upgrade a MEGA account
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getPricing - Returns the available pricing plans
         *
         * @return Available pricing plans to upgrade a MEGA account
         */
        virtual MegaPricing *getPricing() const = 0;


        /**
         * @brief Returns the tag of a transfer related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::cancelTransfer - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
         *
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
        enum {TYPE_DOWNLOAD = 0,
              TYPE_UPLOAD};
        
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
         * @return Starting time of the transfer (in deciseconds)
         */
        virtual int64_t getStartTime() const = 0;

        /**
         * @brief Returns the number of transferred bytes during this request
         * @return Transferred bytes during this transfer
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
         * @return Local path related to this transfer
         */
		virtual const char* getPath() const = 0;

        /**
         * @brief Returns the parent path related to this request
         *
         * For uploads, this function returns the path to the folder containing the source file.
         * For downloads, it returns that path to the folder containing the destination file.
         *
         * @return Parent path related to this transfer
         */
		virtual const char* getParentPath() const = 0;

        /**
         * @brief Returns the handle related to this transfer
         *
         * For downloads, this function returns the handle of the source node.
         *
         * For uploads, it returns the handle of the new node in MegaTransferListener::onTransferFinish
         * and MegaListener::onTransferFinish when the error code is API_OK. Otherwise, it returns
         * mega::INVALID_HANDLE.
         *
         * @return The handle related to the transfer.
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
         * The return value is only valid for downloads of public nodes
         * You take the ownership of the returned value.
         *
         * @return Public node related to the transfer
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

#ifdef ENABLE_SYNC

/**
 * @brief Provides information about a synchronization
 *
 * Developers can use listeners (MegaListener, MegaSyncListener)
 * to track the progress of each synchronization. MegaSync objects are provided in callbacks sent
 * to these listeners and allow developers to know the state of the synchronizations and their parameters
 * and
 *
 * Objects of this class aren't live, they are snapshots of the state of the synchronization
 * when the object is created, they are immutable.
 *
 **/
class MegaSyncListener
{
public:
    /**
     * @brief This function is called when the state of a synced file changes
     *
     * Possible values for the state are:
     * - MegaApi::STATE_SYNCED = 1
     * The file is synced with the MEGA account
     *
     * - MegaApi::STATE_PENDING = 2
     * The file isn't synced with the MEGA account. It's waiting to be synced.
     *
     * - MegaApi::STATE_SYNCING = 3
     * The file is being synced with the MEGA account
     *
     * @param api MegaApi object that is synchronizing files
     * @param filePath Local path of the file
     * @param newState New state of the file
     */
    virtual void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, const char *filePath, int newState);

    /**
     * @brief This function is called when the state of the synchronization changes
     *
     * The SDK calls this function when the state of the synchronization changes, for example
     * from 'scanning' to 'syncing' or 'failed'.
     *
     * You can use MegaSync::getState to get the new state.
     *
     * @param api MegaApi object that is synchronizing files
     */
    virtual void onSyncStateChanged(MegaApi *api,  MegaSync *sync);
};

/**
 * @brief Provides information about a synchronization
 */
class MegaSync
{
public:
    enum
    {
        SYNC_FAILED = -2,
        SYNC_CANCELED = -1,
        SYNC_INITIALSCAN = 0,
        SYNC_ACTIVE
    };

    virtual ~MegaSync() = 0;

    /**
     * @brief Creates a copy of this MegaSync object
     *
     * The resulting object is fully independent of the source MegaSync,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaError object
     */
    virtual MegaSync *copy() = 0;

    /**
     * @brief Get the handle of the folder that is being synced
     * @return Handle of the folder that is being synced in MEGA
     */
    virtual MegaHandle getMegaHandle() const = 0;

    /**
     * @brief Get the path of the local folder that is being synced
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaRequest object is deleted.
     *
     * @return Local folder that is being synced
     */
    virtual const char* getLocalFolder() const = 0;

    /**
     * @brief Gets an unique identifier of the local folder that is being synced
     * @return Unique identifier of the local folder that is being synced
     */
    virtual long long getLocalFingerprint() const = 0;

    /**
     * @brief Returns the identifier of this synchronization
     *
     * Identifiers of synchronizations are always negative numbers.
     *
     * @return Identifier of the synchronization
     */
    virtual int getTag() const = 0;

    /**
     * @brief Get the state of the synchronization
     *
     * Possible values are:
     * - SYNC_FAILED = -2
     * The synchronization has failed and has been disabled
     *
     * - SYNC_CANCELED = -1,
     * The synchronization has failed and has been disabled
     *
     * - SYNC_INITIALSCAN = 0,
     * The synchronization is doing the initial scan
     *
     * - SYNC_ACTIVE
     * The synchronization is active
     *
     * @return State of the synchronization
     */
    virtual int getState() const = 0;
};

#endif

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
         * @param errorCode Error code for this error
		 */
		MegaError(int errorCode);

		/**
		 * @brief Creates a new MegaError object copying another one
         * @param megaError MegaError object to be copied
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

    private:
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
         * @param node Node to be processed
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
         * @param api MegaApi object that started the transfer
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
         * @param transfer Information about the transfer
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
        /**
         * @brief This function is called when there are new or updated contacts in the account
         *
         * The SDK retains the ownership of the MegaUserList in the second parameter. The list and all the
         * MegaUser objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaUserList::copy. If you want to save only some of the MegaUser objects, use MegaUser::copy
         * for those objects.
         *
         * @param api MegaApi object connected to the account
         * @param users List that contains the new or updated contacts
         */
        virtual void onUsersUpdate(MegaApi* api, MegaUserList *users);

        /**
         * @brief This function is called when there are new or updated nodes in the account
         *
         * When the full account is reloaded or a large number of server notifications arrives at once, the
         * second parameter will be NULL.
         *
         * The SDK retains the ownership of the MegaNodeList in the second parameter. The list and all the
         * MegaNode objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaNodeList::copy. If you want to save only some of the MegaNode objects, use MegaNode::copy
         * for those nodes.
         *
         * @param api MegaApi object connected to the account
         * @param nodes List that contains the new or updated nodes
         */
        virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);

        /**
         * @brief This function is called when an inconsistency is detected in the local cache
         *
         * You should call MegaApi::fetchNodes when this callback is received
         *
         * @param api MegaApi object connected to the account
         */
        virtual void onReloadNeeded(MegaApi* api);

#ifdef ENABLE_SYNC
        /**
         * @brief This function is called with the state of the synchronization engine has changed
         *
         * You can call MegaApi::isScanning and MegaApi::isWaiting to know the global state
         * of the synchronization engine.
         *
         * @param api MegaApi object related to the event
         */
        virtual void onGlobalSyncStateChanged(MegaApi* api);
#endif

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
         * @param transfer Information about the transfer
         * @param error Error information
         */
        virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error);

        /**
         * @brief This function is called when there are new or updated contacts in the account
         *
         * The SDK retains the ownership of the MegaUserList in the second parameter. The list and all the
         * MegaUser objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaUserList::copy. If you want to save only some of the MegaUser objects, use MegaUser::copy
         * for those objects.
         *
         * @param api MegaApi object connected to the account
         * @param users List that contains the new or updated contacts
         */
        virtual void onUsersUpdate(MegaApi* api, MegaUserList *users);

        /**
         * @brief This function is called when there are new or updated nodes in the account
         *
         * When the full account is reloaded or a large number of server notifications arrives at once, the
         * second parameter will be NULL.
         *
         * The SDK retains the ownership of the MegaNodeList in the second parameter. The list and all the
         * MegaNode objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaNodeList::copy. If you want to save only some of the MegaNode objects, use MegaNode::copy
         * for those nodes.
         *
         * @param api MegaApi object connected to the account
         * @param nodes List that contains the new or updated nodes
         */
        virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);

        /**
         * @brief This function is called when an inconsistency is detected in the local cache
         *
         * You should call MegaApi::fetchNodes when this callback is received
         *
         * @param api MegaApi object connected to the account
         */
        virtual void onReloadNeeded(MegaApi* api);

#ifdef ENABLE_SYNC
    /**
     * @brief This function is called when the state of a synced file changes
     *
     * Possible values for the state are:
     * - MegaApi::STATE_SYNCED = 1
     * The file is synced with the MEGA account
     *
     * - MegaApi::STATE_PENDING = 2
     * The file isn't synced with the MEGA account. It's waiting to be synced.
     *
     * - MegaApi::STATE_SYNCING = 3
     * The file is being synced with the MEGA account
     *
     * @param api MegaApi object that is synchronizing files
     * @param filePath Local path of the file
     * @param newState New state of the file
     */
    virtual void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, const char *filePath, int newState);

    /**
     * @brief This function is called when the state of the synchronization changes
     *
     * The SDK calls this function when the state of the synchronization changes. you can use
     * MegaSync::getState to get the new state of the synchronization
     *
     * @param api MegaApi object that is synchronizing files
     */
    virtual void onSyncStateChanged(MegaApi *api,  MegaSync *sync);

    /**
     * @brief This function is called with the state of the synchronization engine has changed
     *
     * You can call MegaApi::isScanning and MegaApi::isWaiting to know the global state
     * of the synchronization engine.
     *
     * @param api MegaApi object related to the event
     */
    virtual void onGlobalSyncStateChanged(MegaApi* api);
#endif
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
 * to a MEGA account or a public folder). If the login request succeed, you must call MegaApi::fetchNodes to get the filesystem in MEGA.
 * After successfully completing that request, you can use all other functions, manage the files and start transfers.
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

        enum {
            ATTR_TYPE_THUMBNAIL = 0,
            ATTR_TYPE_PREVIEW = 1
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

#ifdef ENABLE_SYNC
        /**
         * @brief Add a listener for all events related to synchronizations
         * @param listener Listener that will receive synchronization events
         */
        void addSyncListener(MegaSyncListener *listener);

        /**
         * @brief Unregister a synchronization listener
         * @param listener Objet that will be unregistered
         */
        void removeSyncListener(MegaSyncListener *listener);
#endif

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

        /**
         * @brief Generates a private key based on the access password
         *
         * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
         * required to log in, this function allows to do this step in a separate function. You should run this function
         * in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin
         *
         * You take the ownership of the returned value.
         *
         * @param password Access password
         * @return Base64-encoded private key
         */
        const char* getBase64PwKey(const char *password);

        /**
         * @brief Generates a hash based in the provided private key and email
         *
         * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
         * required to log in, this function allows to do this step in a separate function. You should run this function
         * in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin
         *
         * You take the ownership of the returned value.
         *
         * @param base64pwkey Private key returned by MegaApi::getBase64PwKey
         * @param email Email to create the hash
         * @return Base64-encoded hash
         */
        const char* getStringHash(const char* base64pwkey, const char* email);

        /**
         * @brief Converts a Base64-encoded node handle to a MegaHandle
         *
         * The returned value can be used to recover a MegaNode using MegaApi::getNodeByHandle
         * You can revert this operation using MegaApi::handleToBase64
         *
         * @param base64Handle Base64-encoded node handle
         * @return Node handle
         */
        static MegaHandle base64ToHandle(const char* base64Handle);

        /**
         * @brief Converts a MegaHandle to a Base64-encoded string
         *
         * You take the ownership of the returned value
         * You can revert this operation using MegaApi::base64ToHandle
         *
         * @param handle to be converted
         * @return Base64-encoded node handle
         */
        static const char* handleToBase64(MegaHandle handle);

        /**
         * @brief Add entropy to internal random number generators
         *
         * It's recommended to call this function with random data specially to
         * enhance security,
         *
         * @param data Byte array with random data
         * @param size Size of the byte array (in bytes)
         */
        static void addEntropy(char* data, unsigned int size);


        /**
         * @brief Retry all pending requests
         *
         * When requests fails they wait some time before being retried. That delay grows exponentially if the request
         * fails again. For this reason, and since this request is very lightweight, it's recommended to call it with
         * the default parameters on every user interaction with the application. This will prevent very big delays
         * completing requests.
         *
         * The associated request type with this request is MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFlag - Returns the first parameter
         * - MegaRequest::getNumber - Returns the second parameter
         *
         * @param disconnect true if you want to disconnect already connected requests
         * It's not recommended to set this flag to true if you are not fully sure about what are you doing. If you
         * send a request that needs some time to complete and you disconnect it in a loop without giving it enough time,
         * it could be retrying forever.
         *
         * @param includexfers true to retry also transfers
         * It's not recommended to set this flag. Transfer has a retry counter and are aborted after a number of retries
         * MegaTransfer::getMaxRetries. Setting this flag to true, you will force more immediate retries and your transfers
         * could fail faster.
         *
         * @param listener MegaRequestListener to track this request
         */
        void retryPendingConnections(bool disconnect = false, bool includexfers = false, MegaRequestListener* listener = NULL);

        /**
         * @brief Log in to a MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGIN.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the first parameter
         * - MegaRequest::getPassword - Returns the second parameter
         *
         * If the email/password aren't valid the error code provided in onRequestFinish is
         * MegaError::API_ENOENT.
         *
         * @param email Email of the user
         * @param password Password
         * @param listener MegaRequestListener to track this request
         */
        void login(const char* email, const char* password, MegaRequestListener *listener = NULL);

        /**
         * @brief Log in to a public folder using a folder link
         *
         * After a successful login, you should call MegaApi::fetchNodes to get filesystem and
         * start working with the folder.
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGIN.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Retuns the string "FOLDER"
         * - MegaRequest::getLink - Returns the public link to the folder
         *
         * @param megaFolderLink Public link to a folder in MEGA
         * @param listener MegaRequestListener to track this request
         */
        void loginToFolder(const char* megaFolderLink, MegaRequestListener *listener = NULL);

        /**
         * @brief Log in to a MEGA account using precomputed keys
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGIN.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the first parameter
         * - MegaRequest::getPassword - Returns the second parameter
         * - MegaRequest::getPrivateKey - Returns the third parameter
         *
         * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
         * MegaError::API_ENOENT.
         *
         * @param email Email of the user
         * @param stringHash Hash of the email returned by MegaApi::getStringHash
         * @param base64pwkey Private key calculated using MegaApi::getBase64PwKey
         * @param listener MegaRequestListener to track this request
         */
        void fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener = NULL);

        /**
         * @brief Log in to a MEGA account using a session key
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGIN.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getSessionKey - Returns the session key
         *
         * @param session Session key previously dumped with MegaApi::dumpSession
         * @param listener MegaRequestListener to track this request
         */
        void fastLogin(const char* session, MegaRequestListener *listener = NULL);

        /**
         * @brief Get data about the logged account
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the name of the logged user
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserData(MegaRequestListener *listener = NULL);

        /**
         * @brief Get data about a contact
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email of the contact
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Returns the XMPP ID of the contact
         * - MegaRequest::getPassword - Returns the public RSA key of the contact, Base64-encoded
         *
         * @param user Contact to get the data
         * @param listener MegaRequestListener to track this request
         */
        void getUserData(MegaUser *user, MegaRequestListener *listener = NULL);

        /**
         * @brief Returns the current session key
         *
         * You have to be logged in to get a valid session key. Otherwise,
         * this function returns NULL.
         *
         * You take the ownership of the returned value.
         *
         * @return Current session key
         */
        const char *dumpSession();

        /**
         * @brief Initialize the creation of a new MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email for the account
         * - MegaRequest::getPassword - Returns the password for the account
         * - MegaRequest::getName - Returns the name of the user
         *
         * If this request succeed, a confirmation email will be sent to the users.
         * If an account with the same email already exists, you will get the error code
         * MegaError::API_EEXIST in onRequestFinish
         *
         * @param email Email for the account
         * @param password Password for the account
         * @param name Name of the user
         * @param listener MegaRequestListener to track this request
         */
        void createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the creation of a new MEGA account with precomputed keys
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email for the account
         * - MegaRequest::getPrivateKey - Returns the private key calculated with MegaApi::getBase64PwKey
         * - MegaRequest::getName - Returns the name of the user
         *
         * If this request succeed, a confirmation email will be sent to the users.
         * If an account with the same email already exists, you will get the error code
         * MegaError::API_EEXIST in onRequestFinish
         *
         * @param email Email for the account
         * @param base64pwkey Private key calculated with MegaApi::getBase64PwKey
         * @param name Name of the user
         * @param listener MegaRequestListener to track this request
         */
        void fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener = NULL);

        /**
         * @brief Get information about a confirmation link
         *
         * The associated request type with this request is MegaRequest::TYPE_QUERY_SIGNUP_LINK.
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getLink - Returns the confirmation link
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getEmail - Return the email associated with the confirmation link
         * - MegaRequest::getName - Returns the name associated with the confirmation link
         *
         * @param link Confirmation link
         * @param listener MegaRequestListener to track this request
         */
        void querySignupLink(const char* link, MegaRequestListener *listener = NULL);

        /**
         * @brief Confirm a MEGA account using a confirmation link and the user password
         *
         * The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getLink - Returns the confirmation link
         * - MegaRequest::getPassword - Returns the password
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getEmail - Email of the account
         * - MegaRequest::getName - Name of the user
         *
         * @param link Confirmation link
         * @param password Password of the account
         * @param listener MegaRequestListener to track this request
         */
        void confirmAccount(const char* link, const char *password, MegaRequestListener *listener = NULL);

        /**
         * @brief Confirm a MEGA account using a confirmation link and a precomputed key
         *
         * The associated request type with this request is MegaRequest::TYPE_CONFIRM_ACCOUNT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getLink - Returns the confirmation link
         * - MegaRequest::getPrivateKey - Returns the base64pwkey parameter
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getEmail - Email of the account
         * - MegaRequest::getName - Name of the user
         *
         * @param link Confirmation link
         * @param base64pwkey Private key precomputed with MegaApi::getBase64PwKey
         * @param listener MegaRequestListener to track this request
         */
        void fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener = NULL);

        /**
         * @brief Set proxy settings
         *
         * The SDK will start using the provided proxy settings as soon as this function returns.
         *
         * @param proxySettings Proxy settings
         * @see MegaProxy
         */
        void setProxySettings(MegaProxy *proxySettings);

        /**
         * @brief Try to detect the system's proxy settings
         *
         * Automatic proxy detection is currently supported on Windows only.
         * On other platforms, this fuction will return a MegaProxy object
         * of type MegaProxy::PROXY_NONE
         *
         * You take the ownership of the returned value.
         *
         * @return MegaProxy object with the detected proxy settings
         */
        MegaProxy *getAutoProxySettings();

        /**
         * @brief Check if the MegaApi object is logged in
         * @return 0 if not logged in, Otherwise, a number >= 0
         */
        int isLoggedIn();

        /**
         * @brief Retuns the email of the currently open account
         *
         * If the MegaApi object isn't logged in or the email isn't available,
         * this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @return Email of the account
         */
        const char* getMyEmail();

        /**
         * @brief Retuns the XMPP ID of the currently open account
         *
         * If the MegaApi object isn't logged in or the email isn't available,
         * this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @return XMPP ID of the account
         */
        const char* getXMPPUserId();

        /**
         * @brief Set the active log level
         *
         * This function sets the log level of the logging system. If you set a log listener using
         * MegaApi::setLoggerObject, you will receive logs with the same or a lower level than
         * the one passed to this function.
         *
         * @param logLevel Active log level
         *
         * These are the valid values for this parameter:
         * - MegaApi::LOG_LEVEL_FATAL = 0
         * - MegaApi::LOG_LEVEL_ERROR = 1
         * - MegaApi::LOG_LEVEL_WARNING = 2
         * - MegaApi::LOG_LEVEL_INFO = 3
         * - MegaApi::LOG_LEVEL_DEBUG = 4
         * - MegaApi::LOG_LEVEL_MAX = 5
         */
        static void setLogLevel(int logLevel);

        /**
         * @brief Set a MegaLogger implementation to receive SDK logs
         *
         * Logs received by this objects depends on the active log level.
         * By default, it is MegaApi::LOG_LEVEL_INFO. You can change it
         * using MegaApi::setLogLevel.
         *
         * @param megaLogger MegaLogger implementation
         */
        static void setLoggerObject(MegaLogger *megaLogger);

        /**
         * @brief Send a log to the logging system
         *
         * This log will be received by the active logger object (MegaApi::setLoggerObject) if
         * the log level is the same or lower than the active log level (MegaApi::setLogLevel)
         *
         * The third and the fouth parameget are optional. You may want to use  __FILE__ and __LINE__
         * to complete them.
         *
         * @param logLevel Log level for this message
         * @param message Message for the logging system
         * @param filename Origin of the log message
         * @param line Line of code where this message was generated
         */
        static void log(int logLevel, const char* message, const char *filename = "", int line = -1);

        /**
         * @brief Create a folder in the MEGA account
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
         * @param name Name of the new folder
         * @param parent Parent folder
         * @param listener MegaRequestListener to track this request
         */
        void createFolder(const char* name, MegaNode *parent, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a node in the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node to move
         * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
         *
         * @param node Node to move
         * @param newParent New parent for the node
         * @param listener MegaRequestListener to track this request
         */
        void moveNode(MegaNode* node, MegaNode* newParent, MegaRequestListener *listener = NULL);

        /**
         * @brief Copy a node in the MEGA account
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
         * @param node Node to copy
         * @param newParent Parent for the new node
         * @param listener MegaRequestListener to track this request
         */
        void copyNode(MegaNode* node, MegaNode *newParent, MegaRequestListener *listener = NULL);

        /**
         * @brief Rename a node in the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_RENAME
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node to rename
         * - MegaRequest::getName - Returns the new name for the node
         *
         * @param node Node to modify
         * @param newName New name for the node
         * @param listener MegaRequestListener to track this request
         */
        void renameNode(MegaNode* node, const char* newName, MegaRequestListener *listener = NULL);

        /**
         * @brief Remove a node from the MEGA account
         *
         * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
         * the node to the Rubbish Bin use MegaApi::moveNode
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
         *
         * @param node Node to remove
         * @param listener MegaRequestListener to track this request
         */
        void remove(MegaNode* node, MegaRequestListener *listener = NULL);

        /**
         * @brief Send a node to the Inbox of another MEGA user using a MegaUser
         *
         * The associated request type with this request is MegaRequest::TYPE_COPY
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node to send
         * - MegaRequest::getEmail - Returns the email of the user that receives the node
         *
         * @param node Node to send
         * @param user User that receives the node
         * @param listener MegaRequestListener to track this request
         */
        void sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener = NULL);

        /**
         * @brief Share or stop sharing a folder in MEGA with another user using a MegaUser
         *
         * To share a folder with an user, set the desired access level in the level parameter. If you
         * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
         *
         * The associated request type with this request is MegaRequest::TYPE_COPY
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
         * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
         * - MegaRequest::getAccess - Returns the access that is granted to the user
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
        void share(MegaNode *node, MegaUser* user, int level, MegaRequestListener *listener = NULL);

        /**
         * @brief Share or stop sharing a folder in MEGA with another user using his email
         *
         * To share a folder with an user, set the desired access level in the level parameter. If you
         * want to stop sharing a folder use the access level MegaShare::ACCESS_UNKNOWN
         *
         * The associated request type with this request is MegaRequest::TYPE_COPY
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder to share
         * - MegaRequest::getEmail - Returns the email of the user that receives the shared folder
         * - MegaRequest::getAccess - Returns the access that is granted to the user
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
        void share(MegaNode *node, const char* email, int level, MegaRequestListener *listener = NULL);

        /**
         * @brief Import a public link to the account
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
         * @param megaFileLink Public link to a file in MEGA
         * @param parent Parent folder for the imported file
         * @param listener MegaRequestListener to track this request
         */
        void importFileLink(const char* megaFileLink, MegaNode* parent, MegaRequestListener *listener = NULL);

        /**
         * @brief Get a MegaNode from a public link to a file
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
         *
         * @param megaFileLink Public link to a file in MEGA
         * @param listener MegaRequestListener to track this request
         */
        void getPublicNode(const char* megaFileLink, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the thumbnail of a node
         *
         * If the node doesn't have a thumbnail the request fails with the MegaError::API_ENOENT
         * error code
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getFile - Returns the destination path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         *
         * @param node Node to get the thumbnail
         * @param dstFilePath Destination path for the thumbnail.
         * If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
         * will be used as the file name inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the preview of a node
         *
         * If the node doesn't have a preview the request fails with the MegaError::API_ENOENT
         * error code
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getFile - Returns the destination path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
         *
         * @param node Node to get the preview
         * @param dstFilePath Destination path for the preview.
         * If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "1.jpg")
         * will be used as the file name inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the avatar of a MegaUser
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFile - Returns the destination path
         * - MegaRequest::getEmail - Returns the email of the user
         *
         * @param user MegaUser to get the avatar
         * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
         * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
         * will be used as the file name inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener = NULL);


        /**
         * @brief Cancel the retrieval of a thumbnail
         *
         * The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         *
         * @param node Node to cancel the retrieval of the thumbnail
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::getThumbnail
         */
		void cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener = NULL);

        /**
         * @brief Cancel the retrieval of a preview
         *
         * The associated request type with this request is MegaRequest::TYPE_CANCEL_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
         *
         * @param node Node to cancel the retrieval of the preview
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::getPreview
         */
        void cancelGetPreview(MegaNode* node, MegaRequestListener *listener = NULL);

        /**
         * @brief Set the thumbnail of a MegaNode
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getFile - Returns the source path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         *
         * @param node MegaNode to set the thumbnail
         * @param srcFilePath Source path of the file that will be set as thumbnail
         * @param listener MegaRequestListener to track this request
         */
        void setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Set the preview of a MegaNode
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getFile - Returns the source path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
         *
         * @param node MegaNode to set the preview
         * @param srcFilePath Source path of the file that will be set as preview
         * @param listener MegaRequestListener to track this request
         */
        void setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Set the avatar of the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFile - Returns the source path
         *
         * @param srcFilePath Source path of the file that will be set as avatar
         * @param listener MegaRequestListener to track this request
         */
        void setAvatar(const char *srcFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Generate a public link of a file/folder in MEGA
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
         * @param node MegaNode to get the public link
         * @param listener MegaRequestListener to track this request
         */
        void exportNode(MegaNode *node, MegaRequestListener *listener = NULL);

        /**
         * @brief Stop sharing a file/folder
         *
         * The associated request type with this request is MegaRequest::TYPE_EXPORT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getAccess - Returns false
         *
         * @param node MegaNode to stop sharing
         * @param listener MegaRequestListener to track this request
         */
        void disableExport(MegaNode *node, MegaRequestListener *listener = NULL);

        /**
         * @brief Fetch the filesystem in MEGA
         *
         * The MegaApi object must be logged in in an account or a public folder
         * to successfully complete this request.
         *
         * The associated request type with this request is MegaRequest::TYPE_FETCH_NODES
         *
         * @param listener MegaRequestListener to track this request
         */
        void fetchNodes(MegaRequestListener *listener = NULL);

        /**
         * @brief Get details about the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
         *
         * @param listener MegaRequestListener to track this request
         */
        void getAccountDetails(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the available pricing plans to upgrade a MEGA account
         *
         * You can get a payment URL for any of the pricing plans provided by this function
         * using MegaApi::getPaymentUrl
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_PRICING
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getPricing - MegaPricing object with all pricing plans
         *
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::getPaymentUrl
         */
        void getPricing(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the payment URL for an upgrade
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_URL
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the product
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getLink - Payment link
         *
         * @param productHandle Handle of the product (see MegaApi::getPricing)
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::getPricing
         */
        void getPaymentUrl(MegaHandle productHandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Export the master key of the account
         *
         * The returned value is a Base64-encoded string
         *
         * With the master key, it's possible to start the recovery of an account when the
         * password is lost:
         * - https://mega.co.nz/#recovery
         *
         * You take the ownership of the returned value.
         *
         * @return Base64-encoded master key
         */
        const char *exportMasterKey();

        /**
         * @brief Change the password of the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_CHANGE_PW
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getPassword - Returns the old password
         * - MegaRequest::getNewPassword - Returns the new password
         *
         * @param oldPassword Old password
         * @param newPassword New password
         * @param listener MegaRequestListener to track this request
         */
        void changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener = NULL);

        /**
         * @brief Add a new contact to the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_ADD_CONTACT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email of the contact
         *
         * @param email Email of the new contact
         * @param listener MegaRequestListener to track this request
         */
        void addContact(const char* email, MegaRequestListener* listener = NULL);

        /**
         * @brief Remove a contact to the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_CONTACT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email of the contact
         *
         * @param user MegaUser of the contact (see MegaApi::getContact)
         * @param listener MegaRequestListener to track this request
         */
        void removeContact(MegaUser *user, MegaRequestListener* listener = NULL);

        /**
         * @brief Logout of the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGOUT
         *
         * @param listener MegaRequestListener to track this request
         */
        void logout(MegaRequestListener *listener = NULL);

        /**
         * @brief Submit feedback about the app
         *
         * The User-Agent is used to identify the app. It can be set in MegaApi::MegaApi
         *
         * The associated request type with this request is MegaRequest::TYPE_REPORT_EVENT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns MegaApi::EVENT_FEEDBACK
         * - MegaRequest::getText - Retuns the comment about the app
         * - MegaRequest::getNumber - Returns the rating for the app
         *
         * @param rating Integer to rate the app. Valid values: from 1 to 5.
         * @param comment Comment about the app
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This function is for internal usage of MEGA apps. This feedback
         * is sent to MEGA servers.
         *
         */
        void submitFeedback(int rating, const char *comment, MegaRequestListener *listener = NULL);

        /**
         * @brief Send a debug report
         *
         * The User-Agent is used to identify the app. It can be set in MegaApi::MegaApi
         *
         * The associated request type with this request is MegaRequest::TYPE_REPORT_EVENT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns MegaApi::EVENT_DEBUG
         * - MegaRequest::getText - Retuns the debug message
         *
         * @param text Debug message
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This function is for internal usage of MEGA apps. This feedback
         * is sent to MEGA servers.
         */
        void reportDebugEvent(const char *text, MegaRequestListener *listener = NULL);



        ///////////////////   TRANSFERS ///////////////////

        /**
         * @brief Upload a file
         * @param localPath Local path of the file
         * @param parent Parent node for the file in the MEGA account
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char* localPath, MegaNode *parent, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file with a custom modification time
         * @param localPath Local path of the file
         * @param parent Parent node for the file in the MEGA account
         * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char* localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file with a custom name
         * @param localPath Local path of the file
         * @param parent Parent node for the file in the MEGA account
         * @param fileName Custom file name for the file in MEGA
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener = NULL);

        /**
         * @brief Upload a file with a custom name and a custom modification time
         * @param localPath Local path of the file
         * @param parent Parent node for the file in the MEGA account
         * @param fileName Custom file name for the file in MEGA
         * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, int64_t mtime, MegaTransferListener *listener = NULL);

        /**
         * @brief Download a file from MEGA
         * @param node MegaNode that identifies the file
         * @param localPath Destination path for the file
         * If this path is a local folder, it must end with a '\' or '/' character and the file name
         * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         *
         * @param listener MegaTransferListener to track this transfer
         */
        void startDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);

        /**
         * @brief Start an streaming download
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
         * @param node MegaNode that identifies the file (public nodes aren't supported yet)
         * @param startPos First byte to download from the file
         * @param size Size of the data to download
         * @param listener MegaTransferListener to track this transfer
         */
        void startStreaming(MegaNode* node, int64_t startPos, int64_t size, MegaTransferListener *listener);

        /**
         * @brief Cancel a transfer
         *
         * When a transfer is cancelled, it will finish and will provide the error code
         * MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and
         * MegaListener::onTransferFinish
         *
         * The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
         *
         * @param transfer MegaTransfer object that identifies the transfer
         * You can get this object in any MegaTransferListener callback or any MegaListener callback
         * related to transfers.
         *
         * @param listener MegaRequestListener to track this request
         */
        void cancelTransfer(MegaTransfer *transfer, MegaRequestListener *listener = NULL);

        /**
         * @brief Cancel all transfers of the same type
         *
         * The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFERS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the first parameter
         *
         * @param type Type of transfers to cancel.
         * Valid values are:
         * - MegaTransfer::TYPE_DOWNLOAD = 0
         * - MegaTransfer::TYPE_UPLOAD = 1
         *
         * @param listener MegaRequestListener to track this request
         */
        void cancelTransfers(int type, MegaRequestListener *listener = NULL);

        /**
         * @brief Pause/resume all transfers
         *
         * The associated request type with this request is MegaRequest::TYPE_PAUSE_TRANSFERS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFlag - Returns the first parameter
         *
         * @param pause true to pause all transfers / false to resume all transfers
         * @param listener MegaRequestListener to track this request
         */
        void pauseTransfers(bool pause, MegaRequestListener* listener = NULL);

        /**
         * @brief Set the upload speed limit
         *
         * The limit will be applied on the server side when starting a transfer. Thus the limit won't be
         * applied for already started uploads and it's applied per storage server.
         *
         * @param bpslimit -1 to automatically select the limit, 0 for no limit, otherwise the speed limit
         * in bytes per second
         */
        void setUploadLimit(int bpslimit);

        /**
         * @brief Get all active transfers
         *
         * You take the ownership of the returned value
         *
         * @return List with all active transfers
         */
        MegaTransferList *getTransfers();

        /**
         * @brief Get all transfers of a specific type (downloads or uploads)
         *
         * If the parameter isn't MegaTransfer.TYPE_DOWNLOAD or MegaTransfer.TYPE_UPLOAD
         * this function returns an empty list.
         *
         * You take the ownership of the returned value
         *
         * @param type MegaTransfer.TYPE_DOWNLOAD or MegaTransfer.TYPE_UPLOAD
         * @return List with transfers of the desired type
         */
        MegaTransferList *getTransfers(int type);

#ifdef ENABLE_SYNC

        ///////////////////   SYNCHRONIZATION   ///////////////////

        /**
         * @brief Get the synchronization state of a local file
         * @param Path of the local file
         * @return Synchronization state of the local file.
         * Valid values are:
         * - STATE_NONE = 0
         * The file isn't inside a synced folder
         *
         * - MegaApi::STATE_SYNCED = 1
         * The file is in sync with the MEGA account
         *
         * - MegaApi::STATE_PENDING = 2
         * The file is pending to be synced with the MEGA account
         *
         * - MegaApi::STATE_SYNCING = 3
         * The file is being synced with the MEGA account
         *
         * - MegaApi::STATE_IGNORED = 4
         * The file is inside a synced folder, but it is ignored
         * by the selected exclusion filters
         *
         */
        int syncPathState(std::string *path);

        /**
         * @brief Get the MegaNode associated with a local synced file
         * @param path Local path of the file
         * @return The same file in MEGA or NULL if the file isn't synced
         */
        MegaNode *getSyncedNode(std::string *path);

        /**
         * @brief Synchronize a local folder and a folder in MEGA
         *
         * This function should be used to add a new synchronized folders. To resume a previously
         * added synchronized folder, use MegaApi::resumeSync
         *
         * The associated request type with this request is MegaRequest::TYPE_ADD_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFile - Returns the path of the local folder
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Fingerprint of the local folder to resume the sync (MegaApi::resumeSync)
         *
         * @param localFolder Local folder
         * @param megaFolder MEGA folder
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::resumeSync
         */
        void syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRequestListener* listener = NULL);

        /**
         * @brief Resume a previously synced folder
         *
         * This function should be called in the onRequestFinish callback for MegaApi::fetchNodes, before the callback
         * returns, to ensure that all changes made in the MEGA account while the synchronization was stopped
         * are correctly applied.
         *
         * The third parameter allows to pass a fingerprint of the local folder to check if it has changed since
         * the previous execution. That fingerprint can be obtained using MegaRequest::getParentHandle in the
         * onRequestFinish callback if the MegaApi::syncFolder request. If the provided fingerprint doesn't match
         * the current fingerprint of the local folder, this request will fail with the error code
         * MegaError::API_EFAILED
         *
         * The associated request type with this request is MegaRequest::TYPE_ADD_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFile - Returns the path of the local folder
         * - MegaRequest::getNumber - Returns the fingerprint of the local folder
         *
         * @param localFolder Local folder
         * @param megaFolder MEGA folder
         * @param localfp Fingerprint of the local file
         * @param listener MegaRequestListener to track this request
         */
        void resumeSync(const char *localFolder, MegaNode *megaFolder, long long localfp, MegaRequestListener* listener = NULL);

        /**
         * @brief Remove a synced folder
         *
         * The folder will stop being synced. Nothing in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         *
         * @param megaFolder MEGA folder
         * @param listener MegaRequestListener to track this request
         */
        void removeSync(MegaNode *megaFolder, MegaRequestListener *listener = NULL);

        /**
         * @brief Remove a synced folder
         *
         * The folder will stop being synced. Nothing in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         *
         * @param sync Synchronization to cancel
         * @param listener MegaRequestListener to track this request
         */
        void removeSync(MegaSync *sync, MegaRequestListener *listener = NULL);

        /**
         * @brief Remove all active synced folders
         *
         * All folders will stop being synced. Nothing in the local nor in the remote folders
         * will be deleted due to the usage of this function.
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNCS
         *
         * @param listener MegaRequestListener to track this request
         */
        void removeSyncs(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the number of active synced folders
         * @return The number of active synced folders
         *
         * @deprecated New functions to manage synchronizations are being implemented. This funtion will
         * be removed in future updates.
         */
        int getNumActiveSyncs();

        /**
         * @brief Check if the synchronization engine is scanning files
         * @return true if it is scanning, otherwise false
         */
        bool isScanning();

        /**
         * @brief Check if the MegaNode is synchronized with a local file
         * @param MegaNode to check
         * @return true if the node is synchronized, othewise false
         * @see MegaApi::getLocalPath
         */
        bool isSynced(MegaNode *n);

        /**
         * @brief Set a list of excluded file names
         *
         * Wildcards (* and ?) are allowed
         *
         * @param List of excluded file names
         * @deprecated A more powerful exclusion system based on regular expresions is being developed. This
         * function will be removed in future updates
         */
        void setExcludedNames(std::vector<std::string> *excludedNames);

        /**
         * @brief Move a local file to the local "Debris" folder
         *
         * The file have to be inside a local synced folder
         *
         * @param path Path of the local file
         * @return true on success, false on failure
         */
        bool moveToLocalDebris(const char *path);

        /**
         * @brief Check if a name is syncable based on the excluded names
         * @param name Name to check
         * @return true if the name is syncable, otherwise false
         * @deprecated A more powerful exclusion system based on regular expresions is being developed. This
         * function will be removed or modified in future updates
         */
        bool isSyncable(const char *name);

        /**
         * @brief Get the corresponding local path of a synced node
         * @param Node to check
         * @return Local path of the corresponding file in the local computer. If the node is't synced
         * this function returns an empty string.
         *
         * @deprecated New functions to manage synchronizations are being implemented. This funtion will
         * be removed in future updates.
         */
        std::string getLocalPath(MegaNode *node);
#endif

        /**
         * @brief Force a loop of the SDK thread
         * @deprecated This function is only here for debugging purposes. It will probably
         * be removed in future updates
         */
        void update();

        /**
         * @brief Check if the SDK is waiting for the server
         * @return true if the SDK is waiting for the server to complete a request
         */
        bool isWaiting();

        /**
         * @brief Get the number of pending uploads
         *
         * @return Pending uploads
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         */
        int getNumPendingUploads();

        /**
         * @brief Get the number of pending downloads
         * @return Pending downloads
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         */
        int getNumPendingDownloads();

        /**
         * @brief Get the number of queued uploads since the last call to MegaApi::resetTotalUploads
         * @return Number of queued uploads since the last call to MegaApi::resetTotalUploads
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         */
        int getTotalUploads();

        /**
         * @brief Get the number of queued uploads since the last call to MegaApi::resetTotalDownloads
         * @return Number of queued uploads since the last call to MegaApi::resetTotalDownloads
         *
         * @deprecated Function related to statistics will be reviewed in future updates. They
         * could change or be removed in the current form.
         */
        int getTotalDownloads();

        /**
         * @brief Reset the number of total downloads
         * This function resets the number returned by MegaApi::getTotalDownloads
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         *
         */
        void resetTotalDownloads();

        /**
         * @brief Reset the number of total uploads
         * This function resets the number returned by MegaApi::getTotalUploads
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         */
        void resetTotalUploads();
        /**
         * @brief Get the total downloaded bytes since the creation of the MegaApi object
         * @return Total downloaded bytes since the creation of the MegaApi object
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         */
        long long getTotalDownloadedBytes();

        /**
         * @brief Get the total uploaded bytes since the creation of the MegaApi object
         * @return Total uploaded bytes since the creation of the MegaApi object
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         *
         */
        long long getTotalUploadedBytes();

        /**
         * @brief Update the number of pending downloads/uploads
         *
         * This function forces a count of the pending downloads/uploads. It could
         * affect the return value of MegaApi::getNumPendingDownloads and
         * MegaApi::getNumPendingUploads.
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         *
         */
        void updateStats();

        enum {	ORDER_NONE = 0, ORDER_DEFAULT_ASC, ORDER_DEFAULT_DESC,
            ORDER_SIZE_ASC, ORDER_SIZE_DESC,
            ORDER_CREATION_ASC, ORDER_CREATION_DESC,
            ORDER_MODIFICATION_ASC, ORDER_MODIFICATION_DESC,
            ORDER_ALPHABETICAL_ASC, ORDER_ALPHABETICAL_DESC};


		/**
		 * @brief Get the number of child nodes
		 *
		 * If the node doesn't exist in MEGA or isn't a folder,
		 * this function returns 0
		 *
		 * This function doesn't search recursively, only returns the direct child nodes.
		 *
		 * @param parent Parent node
		 * @return Number of child nodes
		 */
		int getNumChildren(MegaNode* parent);

		/**
		 * @brief Get the number of child files of a node
		 *
		 * If the node doesn't exist in MEGA or isn't a folder,
		 * this function returns 0
		 *
		 * This function doesn't search recursively, only returns the direct child files.
		 *
		 * @param parent Parent node
		 * @return Number of child files
		 */
		int getNumChildFiles(MegaNode* parent);

		/**
		 * @brief Get the number of child folders of a node
		 *
		 * If the node doesn't exist in MEGA or isn't a folder,
		 * this function returns 0
		 *
		 * This function doesn't search recursively, only returns the direct child folders.
		 *
		 * @param parent Parent node
		 * @return Number of child folders
		 */
		int getNumChildFolders(MegaNode* parent);

		/**
		 * @brief Get all children of a MegaNode
		 *
		 * If the parent node doesn't exist or it isn't a folder, this function
		 * returns NULL
		 *
		 * You take the ownership of the returned value
		 *
		 * @param parent Parent node
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
		 * @return List with all child MegaNode objects
		 */
        MegaNodeList* getChildren(MegaNode *parent, int order = 1);

        /**
         * @brief Get the current index of the node in the parent folder for a specific sorting order
         *
         * If the node doesn't exist or it doesn't have a parent node (because it's a root node)
         * this function returns -1
         *
         * @param node Node to check
         * @param order Sorting order to use
         * @return Index of the node in its parent folder
         */
        int getIndex(MegaNode* node, int order = 1);

        /**
         * @brief Get the child node with the provided name
         *
         * If the node doesn't exist, this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @param parent Parent node
         * @param name Name of the node
         * @return The MegaNode that has the selected parent and name
         */
        MegaNode *getChildNode(MegaNode *parent, const char* name);

        /**
         * @brief Get the parent node of a MegaNode
         *
         * If the node doesn't exist in the account or
         * it is a root node, this function returns NULL
         *
         * You take the ownership of the returned value.
         *
         * @param node MegaNode to get the parent
         * @return The parent of the provided node
         */
        MegaNode *getParentNode(MegaNode *node);

        /**
         * @brief Get the path of a MegaNode
         *
         * If the node doesn't exist, this function returns NULL.
         * You can recoved the node later using MegaApi::getNodeByPath
         * except if the path contains names with  '/', '\' or ':' characters.
         *
         * You take the ownership of the returned value
         *
         * @param node MegaNode for which the path will be returned
         * @return The path of the node
         */
        const char* getNodePath(MegaNode *node);

        /**
         * @brief Get the MegaNode in a specific path in the MEGA account
         *
         * The path separator character is '/'
         * The Root node is /
         * The Inbox root node is //in/
         * The Rubbish root node is //bin/
         *
         * Paths with names containing '/', '\' or ':' aren't compatible
         * with this function.
         *
         * It is needed to be logged in and to have successfully completed a fetchNodes
         * request before calling this function. Otherwise, it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @param path Path to check
         * @param n Base node if the path is relative
         * @return The MegaNode object in the path, otherwise NULL
         */
        MegaNode *getNodeByPath(const char *path, MegaNode *n = NULL);

        /**
         * @brief Get the MegaNode that has a specific handle
         *
         * You can get the handle of a MegaNode using MegaNode::getHandle. The same handle
         * can be got in a Base64-encoded string using MegaNode::getBase64Handle. Conversions
         * between these formats can be done using MegaApi::base64ToHandle and MegaApi::handleToBase64
         *
         * It is needed to be logged in and to have successfully completed a fetchNodes
         * request before calling this function. Otherwise, it will return NULL.
         *
         * You take the ownership of the returned value.
         *
         * @param MegaHandler Node handle to check
         * @return MegaNode object with the handle, otherwise NULL
         */
        MegaNode *getNodeByHandle(MegaHandle MegaHandler);

        /**
         * @brief Get all contacts of this MEGA account
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaUser object with all contacts of this account
         */
        MegaUserList* getContacts();

        /**
         * @brief Get the MegaUser that has a specific email address
         *
         * You can get the email of a MegaUser using MegaUser::getEmail
         *
         * You take the ownership of the returned value
         *
         * @param email Email address to check
         * @return MegaUser that has the email address, otherwise NULL
         */
        MegaUser* getContact(const char* email);

        /**
         * @brief Get a list with all inbound sharings from one MegaUser
         *
         * You take the ownership of the returned value
         *
         * @param user MegaUser sharing folders with this account
         * @return List of MegaNode objects that this user is sharing with this account
         */
        MegaNodeList *getInShares(MegaUser* user);

        /**
         * @brief Get a list with all inboud sharings
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaNode objects that other users are sharing with this account
         */
        MegaNodeList *getInShares();

        /**
         * @brief Check if a MegaNode is being shared
         *
         * For nodes that are being shared, you can get a a list of MegaShare
         * objects using MegaApi::getOutShares
         *
         * @param node Node to check
         * @return true is the MegaNode is being shared, otherwise false
         */
        bool isShared(MegaNode *node);

        /**
         * @brief Get a list with all active outbound sharings
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaShare objects
         */
        MegaShareList *getOutShares();

        /**
         * @brief Get a list with the active outbound sharings for a MegaNode
         *
         * If the node doesn't exist in the account, this function returns an empty list.
         *
         * You take the ownership of the returned value
         *
         * @param node MegaNode to check
         * @return List of MegaShare objects
         */
        MegaShareList *getOutShares(MegaNode *node);

        /**
         * @brief Get the access level of a MegaNode
         * @param node MegaNode to check
         * @return Access level of the node
         * Valid values are:
         * - MegaShare::ACCESS_OWNER
         * - MegaShare::ACCESS_FULL
         * - MegaShare::ACCESS_READWRITE
         * - MegaShare::ACCESS_READ
         * - MegaShare::ACCESS_UNKNOWN
         */
        int getAccess(MegaNode* node);

        /**
         * @brief Get the size of a node tree
         *
         * If the MegaNode is a file, this function returns the size of the file.
         * If it's a folder, this fuction returns the sum of the sizes of all nodes
         * in the node tree.
         *
         * @param node Parent node
         * @return Size of the node tree
         */
        long long getSize(MegaNode *node);

        /**
         * @brief Get a Base64-encoded fingerprint for a local file
         *
         * The fingerprint is created taking into account the modification time of the file
         * and file contents. This fingerprint can be used to get a corresponding node in MEGA
         * using MegaApi::getNodeByFingerprint
         *
         * If the file can't be found or can't be opened, this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @param filePath Local file path
         * @return Base64-encoded fingerprint for the file
         */
        const char* getFingerprint(const char *filePath);

        /**
         * @brief Get a Base64-encoded fingerprint for a node
         *
         * If the node doesn't exist or doesn't have a fingerprint, this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @param node Node for which we want to get the fingerprint
         * @return Base64-encoded fingerprint for the file
         */
        const char *getFingerprint(MegaNode *node);

        /**
         * @brief Returns a node with the provided fingerprint
         *
         * If there isn't any node in the account with that fingerprint, this function returns NULL.
         *
         * You take the ownership of the returned value.
         *
         * @param fingerprint Fingerprint to check
         * @return MegaNode object with the provided fingerprint
         */
        MegaNode *getNodeByFingerprint(const char* fingerprint);

        /**
         * @brief Check if the account already has a node with the provided fingerprint
         *
         * A fingerprint for a local file can be generated using MegaApi::getFingerprint
         *
         * @param fingerprint Fingerprint to check
         * @return true if the account contains a node with the same fingerprint
         */
        bool hasFingerprint(const char* fingerprint);

        /**
         * @brief Check if a node has an access level
         *
         * @param node Node to check
         * @param level Access level to check
         * Valid values for this parameter are:
         * - MegaShare::ACCESS_OWNER
         * - MegaShare::ACCESS_FULL
         * - MegaShare::ACCESS_READWRITE
         * - MegaShare::ACCESS_READ
         *
         * @return MegaError object with the result.
         * Valid values for the error code are:
         * - MegaError::API_OK - The node has the required access level
         * - MegaError::API_EACCESS - The node doesn't have the required access level
         * - MegaError::API_ENOENT - The node doesn't exist in the account
         * - MegaError::API_EARGS - Invalid parameters
         */
        MegaError checkAccess(MegaNode* node, int level);

        /**
         * @brief Check if a node can be moved to a target node
         * @param node Node to check
         * @param target Target for the move operation
         * @return MegaError object with the result:
         * Valid values for the error code are:
         * - MegaError::API_OK - The node can be moved to the target
         * - MegaError::API_EACCESS - The node can't be moved because of permissions problems
         * - MegaError::API_ECIRCULAR - The node can't be moved because that would create a circular linkage
         * - MegaError::API_ENOENT - The node or the target doesn't exist in the account
         * - MegaError::API_EARGS - Invalid parameters
         */
        MegaError checkMove(MegaNode* node, MegaNode* target);

        /**
         * @brief Returns the root node of the account
         *
         * You take the ownership of the returned value
         *
         * If you haven't successfully called MegaApi::fetchNodes before,
         * this function returns NULL
         *
         * @return Root node of the account
         */
        MegaNode *getRootNode();

        /**
         * @brief Returns the inbox node of the account
         *
         * You take the ownership of the returned value
         *
         * If you haven't successfully called MegaApi::fetchNodes before,
         * this function returns NULL
         *
         * @return Inbox node of the account
         */
        MegaNode* getInboxNode();

        /**
         * @brief Returns the rubbish node of the account
         *
         * You take the ownership of the returned value
         *
         * If you haven't successfully called MegaApi::fetchNodes before,
         * this function returns NULL
         *
         * @return Rubbish node of the account
         */
        MegaNode *getRubbishNode();

        /**
         * @brief Search nodes containing a search string in their name
         *
         * The search is case-insensitive.
         *
         * @param node The parent node of the tree to explore
         * @param searchString Search string. The search is case-insensitive
         * @param recursive True if you want to seach recursively in the node tree.
         * False if you want to seach in the children of the node only
         *
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* search(MegaNode* node, const char* searchString, bool recursive = 1);

        /**
         * @brief Process a node tree using a MegaTreeProcessor implementation
         * @param node The parent node of the tree to explore
         * @param processor MegaTreeProcessor that will receive callbacks for every node in the tree
         * @param recursive True if you want to recursively process the whole node tree.
         * False if you want to process the children of the node only
         *
         * @return True if all nodes were processed. False otherwise (the operation can be
         * cancelled by MegaTreeProcessor::processMegaNode())
         */
        bool processMegaTree(MegaNode* node, MegaTreeProcessor* processor, bool recursive = 1);

	#ifdef _WIN32
		/**
		 * @brief Convert an UTF16 string to UTF8 (Windows only)
		 * @param utf16data UTF16 buffer
		 * @param utf16size Size of the UTF16 buffer (in characters)
		 * @param utf8string Pointer to a string that will be filled with UTF8 characters
		 * If the conversion fails, the size of the string will be 0
		 */
        static void utf16ToUtf8(const wchar_t* utf16data, int utf16size, std::string* utf8string);

        /**
         * @brief Convert an UTF8 string to UTF16 (Windows only)
         * @param utf8data NULL-terminated UTF8 character array
         * @param utf16string Pointer to a string that will be filled with UTF16 characters
         * If the conversion fails, the size of the string will be 0
         */
        static void utf8ToUtf16(const char* utf8data, std::string* utf16string);
    #endif

        /**
         * @brief Function to copy a buffer
         *
         * The new buffer is allocated by new[] so you should release
         * it with delete[].
         *
         * @param buffer Character buffer to copy
         * @return Copy of the character buffer
         */
        static char* strdup(const char* buffer);

        static void removeRecursively(const char *path);
private:
        MegaApiImpl *pImpl;
};


class MegaHashSignatureImpl;

/**
 * @brief Class to check a digital signatures
 *
 * The typical usage of this class:
 * - Construct the object using a public key
 * - Add data using MegaHashSignature::add (it can be called many times to add more data)
 * - Call MegaHashSignature::check to know if the data matches a signature
 * - Call MegaHashSignature::init and reuse the object if needed
 */
class MegaHashSignature
{
public:
    /**
     * @brief Initialize the object with a public key to check digital signatures
     * @param base64Key Base64-encode public key.
     *
     * This is the public key used to distribute MEGAsync updates:
     * "EACTzXPE8fdMhm6LizLe1FxV2DncybVh2cXpW3momTb8tpzRNT833r1RfySz5uHe8gdoXN1W0eM5Bk8X-LefygYYDS9RyXrRZ8qXrr9ITJ4r8ATnFIEThO5vqaCpGWTVi5pOPI5FUTJuhghVKTyAels2SpYT5CmfSQIkMKv7YVldaV7A-kY060GfrNg4--ETyIzhvaSZ_jyw-gmzYl_dwfT9kSzrrWy1vQG8JPNjKVPC4MCTZJx9SNvp1fVi77hhgT-Mc5PLcDIfjustlJkDBHtmGEjyaDnaWQf49rGq94q23mLc56MSjKpjOR1TtpsCY31d1Oy2fEXFgghM0R-1UkKswVuWhEEd8nO2PimJOl4u9ZJ2PWtJL1Ro0Hlw9OemJ12klIAxtGV-61Z60XoErbqThwWT5Uu3D2gjK9e6rL9dufSoqjC7UA2C0h7KNtfUcUHw0UWzahlR8XBNFXaLWx9Z8fRtA_a4seZcr0AhIA7JdQG5i8tOZo966KcFnkU77pfQTSprnJhCfEmYbWm9EZA122LJBWq2UrSQQN3pKc9goNaaNxy5PYU1yXyiAfMVsBDmDonhRWQh2XhdV-FWJ3rOGMe25zOwV4z1XkNBuW4T1JF2FgqGR6_q74B2ccFC8vrNGvlTEcs3MSxTI_EKLXQvBYy7hxG8EPUkrMVCaWzzTQAFEQ"
     */
    MegaHashSignature(const char *base64Key);
    ~MegaHashSignature();

    /**
     * @brief Reinitialize the object
     */
    void init();

    /**
     * @brief Add data to calculate the signature
     * @param data Byte buffer with the data
     * @param size Size of the buffer
     */
    void add(const char *data, unsigned size);

    /**
     * @brief Check if the introduced data matches a signature
     * @param base64Signature Base64-encoded digital signature
     * @return true if the signature is correct, otherwise false
     */
    bool checkSignature(const char *base64Signature);

private:
	MegaHashSignatureImpl *pImpl;    
};


/**
 * @brief Details about a MEGA account
 */
class MegaAccountDetails
{
public:
    enum
    {
        ACCOUNT_TYPE_FREE = 0,
        ACCOUNT_TYPE_PROI = 1,
        ACCOUNT_TYPE_PROII = 2,
        ACCOUNT_TYPE_PROIII = 3
    };

	virtual ~MegaAccountDetails() = 0;

    /**
     * @brief Get the PRO level of the MEGA account
     * @return PRO level of the MEGA account.
     * Valid values are:
     * - MegaAccountDetails::ACCOUNT_TYPE_FREE = 0
     * - MegaAccountDetails::ACCOUNT_TYPE_PROI = 1
     * - MegaAccountDetails::ACCOUNT_TYPE_PROII = 2
     * - MegaAccountDetails::ACCOUNT_TYPE_PROIII = 3
     */
    virtual int getProLevel() = 0;

    /**
     * @brief Get the maximum storage for the account (in bytes)
     * @return Maximum storage for the account (in bytes)
     */
    virtual long long getStorageMax() = 0;

    /**
     * @brief Get the used storage
     * @return Used storage (in bytes)
     */
    virtual long long getStorageUsed() = 0;

    /**
     * @brief Get the maximum available bandwidth for the account
     * @return Maximum available bandwidth (in bytes)
     */
    virtual long long getTransferMax() = 0;

    /**
     * @brief Get the used bandwidth
     * @return Used bandwidth (in bytes)
     */
    virtual long long getTransferOwnUsed() = 0;

    /**
     * @brief Get the used storage in for a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Used storage (in bytes)
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getStorageUsed(MegaHandle handle) = 0;

    /**
     * @brief Get the number of files in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of files in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getNumFiles(MegaHandle handle) = 0;

    /**
     * @brief Get the number of folders in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of folders in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getNumFolders(MegaHandle handle) = 0;

    /**
     * @brief Creates a copy of this MegaAccountDetails object.
     *
     * The resulting object is fully independent of the source MegaAccountDetails,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaAccountDetails object
     */
	virtual MegaAccountDetails* copy() = 0;
};

/**
 * @brief Details about pricing plans
 *
 * Use MegaApi::getPricing to get the pricing plans to upgrade MEGA accounts
 */
class MegaPricing
{
public:
    virtual ~MegaPricing() = 0;

    /**
     * @brief Get the number of available products to upgrade the account
     * @return Number of available products
     */
    virtual int getNumProducts() = 0;

    /**
     * @brief Get the handle of a product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Handle of the product
     * @see MegaApi::getPaymentUrl
     */
    virtual MegaHandle getHandle(int productIndex) = 0;

    /**
     * @brief Get the PRO level associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return PRO level associated with the product:
     * Valid values are:
     * - MegaAccountDetails::ACCOUNT_TYPE_FREE = 0
     * - MegaAccountDetails::ACCOUNT_TYPE_PROI = 1
     * - MegaAccountDetails::ACCOUNT_TYPE_PROII = 2
     * - MegaAccountDetails::ACCOUNT_TYPE_PROIII = 3
     */
    virtual int getProLevel(int productIndex) = 0;

    /**
     * @brief Get the number of GB of storage associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of storage
     */
    virtual int getGBStorage(int productIndex) = 0;

    /**
     * @brief Get the number of GB of bandwidth associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of bandwidth
     */
    virtual int getGBTransfer(int productIndex) = 0;

    /**
     * @brief Get the duration of the product (in months)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Duration of the product (in months)
     */
    virtual int getMonths(int productIndex) = 0;

    /**
     * @brief getAmount Get the price of the product (in cents)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Price of the product (in cents)
     */
    virtual int getAmount(int productIndex) = 0;

    /**
     * @brief Get the currency associated with MegaPricing::getAmount
     *
     * The SDK retains the ownership of the returned value.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Currency associated with MegaPricing::getAmount
     */
    virtual const char* getCurrency(int productIndex) = 0;

    /**
     * @brief Creates a copy of this MegaPricing object.
     *
     * The resulting object is fully independent of the source MegaPricing,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaPricing object
     */
    virtual MegaPricing *copy() = 0;
};

}

#endif //MEGAAPI_H
