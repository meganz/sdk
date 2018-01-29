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
class MegaAchievementsDetails;
class MegaPricing;
class MegaNode;
class MegaUser;
class MegaContactRequest;
class MegaShare;
class MegaError;
class MegaRequest;
class MegaEvent;
class MegaTransfer;
class MegaSync;
class MegaStringList;
class MegaNodeList;
class MegaUserList;
class MegaContactRequestList;
class MegaShareList;
class MegaTransferList;
class MegaApi;

class MegaSemaphore;

/**
 * @brief Interface to provide an external GFX processor
 *
 * You can implement this interface to provide a graphics processor to the SDK
 * in the MegaApi::MegaApi constructor. That way, SDK will use your implementation to generate
 * thumbnails/previews when needed.
 *
 * The implementation will receive callbacks from an internal worker thread.
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
    virtual bool readBitmap(const char* path);

    /**
     * @brief Returns the width of the image
     *
     * This function must return the width of the image at the path provided in MegaGfxProcessor::readBitmap
     * If a number <= 0 is returned, the image won't be processed.
     *
     * @return The width of the image
     */
    virtual int getWidth();

    /**
     * @brief Returns the height of the image
     *
     * This function must return de width of the image at the path provided in MegaGfxProcessor::readBitmap
     * If a number <= 0 is returned, the image won't be processed.
     *
     * @return The height of the image
     */
    virtual int getHeight();

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
    virtual int getBitmapDataSize(int width, int height, int px, int py, int rw, int rh);

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
    virtual bool getBitmapData(char *bitmapData, size_t size);

    /**
     * @brief Free resources associated with the processing of the current image
     *
     * With a call of this function, the processing of the image started with a call to
     * MegaGfxProcessor::readBitmap ends. No other functions will be called to continue processing
     * the current image, so you can free all related resources.
     *
     */
    virtual void freeBitmap();

    virtual ~MegaGfxProcessor();
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
            CHANGE_TYPE_PARENT          = 0x80,
            CHANGE_TYPE_PENDINGSHARE    = 0x100,
            CHANGE_TYPE_PUBLIC_LINK     = 0x200
        };

        static const int INVALID_DURATION = -1;
        static const double INVALID_COORDINATE;

        virtual ~MegaNode();

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
        virtual MegaNode *copy();

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
        virtual int getType();

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
        virtual const char* getName();

        /**
         * @brief Returns the fingerprint (Base64-encoded) of the node
         *
         * Only files have a fingerprint, and there could be files without it.
         * If the node doesn't have a fingerprint, this funtion returns NULL
         *
         * The MegaNode object retains the ownership of the returned string. It will
         * be valid until the MegaNode object is deleted.
         *
         * @return Base64-encoded fingerprint of the node, or NULL it the node doesn't have a fingerprint.
         */
        virtual const char* getFingerprint();

        /**
         * @brief Returns true if the node has custom attributes
         *
         * Custom attributes can be set using MegaApi::setCustomNodeAttribute
         *
         * @return True if the node has custom attributes, otherwise false
         * @see MegaApi::setCustomNodeAttribute
         */
        virtual bool hasCustomAttrs();

        /**
         * @brief Returns the list with the names of the custom attributes of the node
         *
         * Custom attributes can be set using MegaApi::setCustomNodeAttribute
         *
         * You take the ownership of the returned value
         *
         * @return Names of the custom attributes of the node
         * @see MegaApi::setCustomNodeAttribute
         */
        virtual MegaStringList *getCustomAttrNames();

        /**
         * @brief Get a custom attribute of the node
         *
         * Custom attributes can be set using MegaApi::setCustomNodeAttribute
         *
         * The MegaNode object retains the ownership of the returned string. It will
         * be valid until the MegaNode object is deleted.
         *
         * @param attrName Name of the custom attribute
         * @return Custom attribute of the node
         * @see MegaApi::setCustomNodeAttribute
         */
        virtual const char *getCustomAttr(const char* attrName);

        /**
         * @brief Get the attribute of the node representing its duration.
         *
         * The purpose of this attribute is to store the duration of audio/video files.
         *
         * @return The number of seconds, or -1 if this attribute is not set.
         */
        virtual int getDuration();

        /**
         * @brief Get the attribute of the node representing the latitude.
         *
         * The purpose of this attribute is to store the coordinate where a photo was taken.
         *
         * @return The latitude coordinate in its decimal degree notation, or INVALID_COORDINATE
         * if this attribute is not set.
         */
        virtual double getLatitude();

        /**
         * @brief Get the attribute of the node representing the longitude.
         *
         * The purpose of this attribute is to store the coordinate where a photo was taken.
         *
         * @return The longitude coordinate in its decimal degree notation, or INVALID_COORDINATE
         * if this attribute is not set.
         */
        virtual double getLongitude();

        /**
         * @brief Returns the handle of this MegaNode in a Base64-encoded string
         *
         * You take the ownership of the returned string.
         * Use delete [] to free it.
         *
         * @return Base64-encoded handle of the node
         */
        virtual char* getBase64Handle();

        /**
         * @brief Returns the size of the node
         *
         * The returned value is only valid for nodes of type TYPE_FILE.
         *
         * @return Size of the node
         */
        virtual int64_t getSize();

        /**
         * @brief Returns the creation time of the node in MEGA (in seconds since the epoch)
         *
         * The returned value is only valid for nodes of type TYPE_FILE or TYPE_FOLDER.
         *
         * @return Creation time of the node (in seconds since the epoch)
         */
        virtual int64_t getCreationTime();

        /**
         * @brief Returns the modification time of the file that was uploaded to MEGA (in seconds since the epoch)
         *
         * The returned value is only valid for nodes of type TYPE_FILE.
         *
         * @return Modification time of the file that was uploaded to MEGA (in seconds since the epoch)
         */
        virtual int64_t getModificationTime();

        /**
         * @brief Returns a handle to identify this MegaNode
         *
         * You can use MegaApi::getNodeByHandle to recover the node later.
         *
         * @return Handle that identifies this MegaNode
         */
        virtual MegaHandle getHandle();

        /**
         * @brief Returns the handle of the parent node
         *
         * You can use MegaApi::getNodeByHandle to recover the node later.
         *
         * @return Handle of the parent node (or INVALID_HANDLE for root nodes)
         */
        virtual MegaHandle getParentHandle();

        /**
         * @brief Returns the key of the node in a Base64-encoded string
         *
         * You take the ownership of the returned string.
         * Use delete [] to free it.
         *
         * @return Returns the key of the node.
         */
        virtual char* getBase64Key();

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
        virtual int getTag();

        /**
         * @brief Returns the expiration time of a public link, if any
         *
         * @return The expiration time as an Epoch timestamp. Returns 0 for non-expire
         * links, and -1 if the MegaNode is not exported.
         */
        virtual int64_t getExpirationTime();

        /**
         * @brief Returns the public handle of a node
         *
         * Only exported nodes have a public handle.
         *
         * @return The public handle of an exported node. If the MegaNode
         * has not been exported, it returns UNDEF.
         */
        virtual MegaHandle getPublicHandle();

         /**
         * @brief Returns a public node corresponding to the exported MegaNode
         *
         * @return Public node for the exported node. If the MegaNode has not been
         * exported or it has expired, then it returns NULL.
         */
        virtual MegaNode* getPublicNode();

        /**
         * @brief Returns the URL for the public link of the exported node.
         *
         * You take the ownership of the returned string.
         * Use delete [] to free it.
         *
         * @param includeKey False if you want the link without the key.
         * @return The URL for the public link of the exported node. If the MegaNode
         * has not been exported, it returns NULL.
         */
        virtual char * getPublicLink(bool includeKey = true);

        /**
         * @brief Returns true if this node represents a file (type == TYPE_FILE)
         * @return true if this node represents a file, otherwise false
         */
        virtual bool isFile();

        /**
         * @brief Returns true this node represents a folder or a root node
         *
         * @return true this node represents a folder or a root node
         */
        virtual bool isFolder();

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
        virtual bool isRemoved();

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
         * - MegaNode::CHANGE_TYPE_PENDINGSHARE    = 0x100
         * Check if the pending share of the node has changed
         *
         * - MegaNode::CHANGE_TYPE_PUBLIC_LINK     = 0x200
         * Check if the public link of the node has changed
         *
         * @return true if this node has an specific change
         */
        virtual bool hasChanged(int changeType);

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
         * - MegaNode::CHANGE_TYPE_OUTSHARE       = 0x40
         * The node is a new or modified outshare
         *
         * - MegaNode::CHANGE_TYPE_PARENT          = 0x80
         * The parent of the node has changed
         *
         * - MegaNode::CHANGE_TYPE_PENDINGSHARE    = 0x100
         * Check if the pending share of the node has changed
         *
         * - MegaNode::CHANGE_TYPE_PUBLIC_LINK     = 0x200
         * Check if the public link of the node has changed
         *
         */
        virtual int getChanges();

        /**
         * @brief Returns true if the node has an associated thumbnail
         * @return true if the node has an associated thumbnail
         */
        virtual bool hasThumbnail();

        /**
         * @brief Returns true if the node has an associated preview
         * @return true if the node has an associated preview
         */
        virtual bool hasPreview();

        /**
         * @brief Returns true if this is a public node
         *
         * Only MegaNode objects generated with MegaApi::getPublicMegaNode
         * will return true.
         *
         * @return true if this is a public node
         */
        virtual bool isPublic();

        /**
         * @brief Check if the MegaNode is being shared by/with your own user
         *
         * For nodes that are being shared, you can get a list of MegaShare
         * objects using MegaApi::getOutShares, or a list of MegaNode objects
         * using MegaApi::getInShares
         *
         * @return true is the MegaNode is being shared, otherwise false
         * @note Exported nodes (public link) are not considered to be shared nodes.
         */
        virtual bool isShared();

        /**
         * @brief Check if the MegaNode is being shared with other users
         *
         * For nodes that are being shared, you can get a list of MegaShare
         * objects using MegaApi::getOutShares
         *
         * @return true is the MegaNode is being shared, otherwise false
         */
        virtual bool isOutShare();

        /**
         * @brief Check if a MegaNode belong to another User, but it is shared with you
         *
         * For nodes that are being shared, you can get a list of MegaNode
         * objects using MegaApi::getInShares
         *
         * @return true is the MegaNode is being shared, otherwise false
         */
        virtual bool isInShare();

        /**
         * @brief Returns true if the node has been exported (has a public link)
         *
         * Public links are created by calling MegaApi::exportNode.
         *
         * @return true if this is an exported node
         */
        virtual bool isExported();

        /**
         * @brief Returns true if the node has been exported (has a temporal public link)
         * and the related public link has expired.
         *
         * Public links are created by calling MegaApi::exportNode.
         *
         * @return true if the public link has expired.
         */
        virtual bool isExpired();

        /**
         * @brief Returns true if this the node has been exported
         * and the related public link has been taken down.
         *
         * Public links are created by calling MegaApi::exportNode.
         *
         * @return true if the public link has been taken down.
         */
        virtual bool isTakenDown();

        /**
         * @brief Returns true if this MegaNode is a private node from a foreign account
         *
         * Only MegaNodes created with MegaApi::createPublicFileNode and MegaApi::createPublicFolderNode
         * returns true in this function.
         *
         * @return true if this node is a private node from a foreign account
         */
        virtual bool isForeign();

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
        virtual std::string* getNodeKey();

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
        virtual std::string* getAttrString();

        /**
         * @brief Returns the file attributes related to the node
         *
         * The return value is only valid for nodes attached in a chatroom. In all other cases this function
         * will return NULL.
         *
         * You take the ownership of the returned string.
         * Use delete [] to free it.
         *
         * @return File attributes related to the node
         */
        virtual char *getFileAttrString();

        /**
         * @brief Return the private auth token to access this node
         *
         * The MegaNode object retains the ownership of the returned pointer. It will be valid until the deletion
         * of the MegaNode object.
         *
         * @return Private auth token to access the node
         * @deprecated This function is intended for internal purposes and will be probably removed in future updates.
         */
        virtual std::string* getPrivateAuth();

        /**
         * @brief Set an auth token to access this node
         * @param privateAuth token to access the node
         * @deprecated This function is intended for internal purposes and will be probably removed in future updates.
         */
        virtual void setPrivateAuth(const char *privateAuth);

        /**
         * @brief Return the public auth token to access this node
         *
         * The MegaNode object retains the ownership of the returned pointer. It will be valid until the deletion
         * of the MegaNode object.
         *
         * @return Public auth token to access the node
         * @deprecated This function is intended for internal purposes and will be probably removed in future updates.
         */
        virtual std::string* getPublicAuth();

        /**
         * @brief Returns the child nodes of an authorized folder node
         *
         * This function always returns NULL, except for authorized folder nodes.
         * Authorized folder nodes are the ones returned by MegaApi::authorizeNode.
         *
         * The MegaNode object retains the ownership of the returned pointer. It will be valid until the deletion
         * of the MegaNode object.
         *
         * @return Child nodes of an authorized folder node, otherwise NULL
         */
        virtual MegaNodeList *getChildren();

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
        virtual bool isSyncDeleted();

        /**
         * @brief Returns the local path associated with this node
         *
         * Only synchronized nodes has an associated local path, for all other nodes
         * the return value will be an empty string.
         *
         * @return The local path associated with this node or an empty string if the node isn't synced-
         */
        virtual std::string getLocalPath();
#endif

        /**
         * @brief Provides a serialization of the MegaNode object
         *
         * @note This function is intended to use ONLY with MegaNode objects obtained from
         * attachment messages received in a chatroom (@see MegaChatMessage::getMegaNodeList()).
         * Using MegaNode objects returned by MegaNode::unserialize from a serialized
         * non-chat MegaNode object may cause undefined behavior.
         *
         * You take the ownership of the returned value.
         *
         * @return Serialization of the MegaNode object, in Base64, or NULL if error.
         */
        virtual char *serialize();

        /**
         * @brief Returns a new MegaNode object from its serialization
         *
         * @note This function is intended to use ONLY with MegaNode objects obtained from
         * attachment messages received in a chatroom (@see MegaChatMessage::getMegaNodeList()).
         * Using MegaNode objects obtained by MegaNode::unserialize from a serialized
         * non-chat MegaNode object may cause undefined behavior.
         *
         * You take the ownership of the returned value.
         *
         * @param Serialization of a MegaNode object obtained from a chat message (in Base64)
         * @return A new MegaNode object, or NULL if error.
         */
        static MegaNode* unserialize(const char *d);
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
            VISIBILITY_VISIBLE = 1,
            VISIBILITY_INACTIVE = 2,
            VISIBILITY_BLOCKED = 3
		};

		virtual ~MegaUser();

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
        virtual MegaUser *copy();

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
        virtual const char* getEmail();

        /**
         * @brief Returns the handle associated with the contact.
         *
         * @return The handle associated with the contact.
         */
        virtual MegaHandle getHandle();

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
         * - VISIBILITY_INACTIVE = 2
         * The contact is currently inactive
         *
         * - VISIBILITY_BLOCKED = 3
         * The contact is currently blocked
         *
         * @note The visibility of your own user is undefined and shouldn't be used.
         * @return Current visibility of the contact
         */
        virtual int getVisibility();

        /**
         * @brief Returns the timestamp when the contact was added to the contact list (in seconds since the epoch)
         * @return Timestamp when the contact was added to the contact list (in seconds since the epoch)
         */
        virtual int64_t getTimestamp();

        enum
        {
            CHANGE_TYPE_AUTHRING        = 0x01,
            CHANGE_TYPE_LSTINT          = 0x02,
            CHANGE_TYPE_AVATAR          = 0x04,
            CHANGE_TYPE_FIRSTNAME       = 0x08,
            CHANGE_TYPE_LASTNAME        = 0x10,
            CHANGE_TYPE_EMAIL           = 0x20,
            CHANGE_TYPE_KEYRING         = 0x40,
            CHANGE_TYPE_COUNTRY         = 0x80,
            CHANGE_TYPE_BIRTHDAY        = 0x100,
            CHANGE_TYPE_PUBKEY_CU255    = 0x200,
            CHANGE_TYPE_PUBKEY_ED255    = 0x400,
            CHANGE_TYPE_SIG_PUBKEY_RSA  = 0x800,
            CHANGE_TYPE_SIG_PUBKEY_CU255 = 0x1000,
            CHANGE_TYPE_LANGUAGE        = 0x2000,
            CHANGE_TYPE_PWD_REMINDER    = 0x4000,
            CHANGE_TYPE_DISABLE_VERSIONS = 0x8000
        };

        /**
         * @brief Returns true if this user has an specific change
         *
         * This value is only useful for users notified by MegaListener::onUsersUpdate or
         * MegaGlobalListener::onUsersUpdate that can notify about user modifications.
         *
         * In other cases, the return value of this function will be always false.
         *
         * @param changeType The type of change to check. It can be one of the following values:
         *
         * - MegaUser::CHANGE_TYPE_AUTH            = 0x01
         * Check if the user has new or modified authentication information
         *
         * - MegaUser::CHANGE_TYPE_LSTINT          = 0x02
         * Check if the last interaction timestamp is modified
         *
         * - MegaUser::CHANGE_TYPE_AVATAR          = 0x04
         * Check if the user has a new or modified avatar image, or if the avatar was removed
         *
         * - MegaUser::CHANGE_TYPE_FIRSTNAME       = 0x08
         * Check if the user has new or modified firstname
         *
         * - MegaUser::CHANGE_TYPE_LASTNAME        = 0x10
         * Check if the user has new or modified lastname
         *
         * - MegaUser::CHANGE_TYPE_EMAIL           = 0x20
         * Check if the user has modified email
         *
         * - MegaUser::CHANGE_TYPE_KEYRING        = 0x40
         * Check if the user has new or modified keyring
         *
         * - MegaUser::CHANGE_TYPE_COUNTRY        = 0x80
         * Check if the user has new or modified country
         *
         * - MegaUser::CHANGE_TYPE_BIRTHDAY        = 0x100
         * Check if the user has new or modified birthday, birthmonth or birthyear
         *
         * - MegaUser::CHANGE_TYPE_PUBKEY_CU255    = 0x200
         * Check if the user has new or modified public key for chat
         *
         * - MegaUser::CHANGE_TYPE_PUBKEY_ED255    = 0x400
         * Check if the user has new or modified public key for signing
         *
         * - MegaUser::CHANGE_TYPE_SIG_PUBKEY_RSA  = 0x800
         * Check if the user has new or modified signature for RSA public key
         *
         * - MegaUser::CHANGE_TYPE_SIG_PUBKEY_CU255 = 0x1000
         * Check if the user has new or modified signature for Cu25519 public key
         *
         * - MegaUser::CHANGE_TYPE_LANGUAGE         = 0x2000
         * Check if the user has modified the preferred language
         *
         * - MegaUser::CHANGE_TYPE_PWD_REMINDER     = 0x4000
         * Check if the data related to the password reminder dialog has changed
         *
         * - MegaUser::CHANGE_TYPE_DISABLE_VERSIONS     = 0x8000
         * Check if option for file versioning has changed
         *
         * @return true if this user has an specific change
         */
        virtual bool hasChanged(int changeType);

        /**
         * @brief Returns a bit field with the changes of the user
         *
         * This value is only useful for users notified by MegaListener::onUsersUpdate or
         * MegaGlobalListener::onUsersUpdate that can notify about user modifications.
         *
         * @return The returned value is an OR combination of these flags:
         *
         * - MegaUser::CHANGE_TYPE_AUTH            = 0x01
         * Check if the user has new or modified authentication information
         *
         * - MegaUser::CHANGE_TYPE_LSTINT          = 0x02
         * Check if the last interaction timestamp is modified
         *
         * - MegaUser::CHANGE_TYPE_AVATAR          = 0x04
         * Check if the user has a new or modified avatar image
         *
         * - MegaUser::CHANGE_TYPE_FIRSTNAME       = 0x08
         * Check if the user has new or modified firstname
         *
         * - MegaUser::CHANGE_TYPE_LASTNAME        = 0x10
         * Check if the user has new or modified lastname
         *
         * - MegaUser::CHANGE_TYPE_EMAIL           = 0x20
         * Check if the user has modified email
         *
         * - MegaUser::CHANGE_TYPE_KEYRING        = 0x40
         * Check if the user has new or modified keyring
         *
         * - MegaUser::CHANGE_TYPE_COUNTRY        = 0x80
         * Check if the user has new or modified country
         *
         * - MegaUser::CHANGE_TYPE_BIRTHDAY        = 0x100
         * Check if the user has new or modified birthday, birthmonth or birthyear
         *
         * - MegaUser::CHANGE_TYPE_PUBKEY_CU255    = 0x200
         * Check if the user has new or modified public key for chat
         *
         * - MegaUser::CHANGE_TYPE_PUBKEY_ED255    = 0x400
         * Check if the user has new or modified public key for signing
         *
         * - MegaUser::CHANGE_TYPE_SIG_PUBKEY_RSA  = 0x800
         * Check if the user has new or modified signature for RSA public key
         *
         * - MegaUser::CHANGE_TYPE_SIG_PUBKEY_CU255 = 0x1000
         * Check if the user has new or modified signature for Cu25519 public key
         *
         * - MegaUser::CHANGE_TYPE_LANGUAGE         = 0x2000
         * Check if the user has modified the preferred language
         *
         * - MegaUser::CHANGE_TYPE_PWD_REMINDER     = 0x4000
         * Check if the data related to the password reminder dialog has changed
         *
         * - MegaUser::CHANGE_TYPE_DISABLE_VERSIONS     = 0x8000
         * Check if option for file versioning has changed
         */
        virtual int getChanges();

        /**
         * @brief Indicates if the user is changed by yourself or by another client.
         *
         * This value is only useful for users notified by MegaListener::onUsersUpdate or
         * MegaGlobalListener::onUsersUpdate that can notify about user modifications.
         *
         * @return 0 if the change is external. >0 if the change is the result of an
         * explicit request, -1 if the change is the result of an implicit request
         * made by the SDK internally.
         */
        virtual int isOwnChange();
};

/**
 * @brief List of MegaHandle objects
 *
 */
class MegaHandleList
{
public:
    /**
     * @brief Creates a new instance of MegaHandleList
     * @return A pointer the new object
     */
    static MegaHandleList *createInstance();

    virtual ~MegaHandleList();

    /**
     * @brief Creates a copy of this MegaHandleList object
     *
     * The resulting object is fully independent of the source MegaHandleList,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaHandleList object
     */
    virtual MegaHandleList *copy() const;

    /**
     * @brief Returns the MegaHandle at the position i in the MegaHandleList
     *
     *
     * If the index is >= the size of the list, this function returns MEGACHAT_INVALID_HANDLE.
     *
     * @param i Position of the MegaHandle that we want to get for the list
     * @return MegaHandle at the position i in the list
     */
    virtual MegaHandle get(unsigned int i) const;

    /**
     * @brief Returns the number of MegaHandles in the list
     * @return Number of MegaHandles in the list
     */
    virtual unsigned int size() const;

    /**
     * @brief Add new MegaHandle to list
     * @param megaHandle to be added
     */
    virtual void addMegaHandle(MegaHandle megaHandle);
};

/**
 * @brief Represents the outbound sharing of a folder with a user in MEGA
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

		virtual ~MegaShare();

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
        virtual MegaShare *copy();

        /**
         * @brief Returns the email of the user with whom we are sharing the folder
         *
         * For public shared folders, this function returns NULL
         *
         * The MegaShare object retains the ownership of the returned string, it will be valid until
         * the MegaShare object is deleted.
         *
         * @return The email of the user with whom we share the folder, or NULL if it's a public folder
         */
        virtual const char *getUser();

        /**
         * @brief Returns the handle of the folder that is being shared
         * @return The handle of the folder that is being shared
         */
        virtual MegaHandle getNodeHandle();

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
        virtual int getAccess();

        /**
         * @brief Returns the timestamp when the sharing was created (in seconds since the epoch)
         * @return The timestamp when the sharing was created (in seconds since the epoch)
         */
        virtual int64_t getTimestamp();
};

#ifdef ENABLE_CHAT
class MegaTextChatPeerList
{
public:
    enum {
        PRIV_UNKNOWN = -2,
        PRIV_RM = -1,
        PRIV_RO = 0,
        PRIV_STANDARD = 2,
        PRIV_MODERATOR = 3
    };

    /**
     * @brief Creates a new instance of MegaTextChatPeerList
     * @return A pointer to the superclass of the private object
     */
    static MegaTextChatPeerList * createInstance();

    virtual ~MegaTextChatPeerList();

    /**
     * @brief Creates a copy of this MegaTextChatPeerList object
     *
     * The resulting object is fully independent of the source MegaTextChatPeerList,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaTextChatPeerList object
     */
    virtual MegaTextChatPeerList *copy() const;

    /**
     * @brief addPeer Adds a new chat peer to the list
     *
     * @param h MegaHandle of the user to be added
     * @param priv Privilege level of the user to be added
     * Valid values are:
     * - MegaTextChatPeerList::PRIV_UNKNOWN = -2
     * - MegaTextChatPeerList::PRIV_RM = -1
     * - MegaTextChatPeerList::PRIV_RO = 0
     * - MegaTextChatPeerList::PRIV_STANDARD = 2
     * - MegaTextChatPeerList::PRIV_MODERATOR = 3
     */
    virtual void addPeer(MegaHandle h, int priv);

    /**
     * @brief Returns the MegaHandle of the chat peer at the position i in the list
     *
     * If the index is >= the size of the list, this function returns INVALID_HANDLE.
     *
     * @param i Position of the chat peer that we want to get from the list
     * @return MegaHandle of the chat peer at the position i in the list
     */
    virtual MegaHandle getPeerHandle(int i) const;

    /**
     * @brief Returns the privilege of the chat peer at the position i in the list
     *
     * If the index is >= the size of the list, this function returns PRIV_UNKNOWN.
     *
     * @param i Position of the chat peer that we want to get from the list
     * @return Privilege level of the chat peer at the position i in the list.
     * Valid values are:
     * - MegaTextChatPeerList::PRIV_UNKNOWN = -2
     * - MegaTextChatPeerList::PRIV_RM = -1
     * - MegaTextChatPeerList::PRIV_RO = 0
     * - MegaTextChatPeerList::PRIV_STANDARD = 2
     * - MegaTextChatPeerList::PRIV_MODERATOR = 3
     */
    virtual int getPeerPrivilege(int i) const;

    /**
     * @brief Returns the number of chat peer in the list
     * @return Number of chat peers in the list
     */
    virtual int size() const;

protected:
    MegaTextChatPeerList();

};

class MegaTextChat
{
public:

    enum
    {
        CHANGE_TYPE_ATTACHMENT        = 0x01
    };

    virtual ~MegaTextChat();

    /**
     * @brief Creates a copy of this MegaTextChat object
     *
     * The resulting object is fully independent of the source MegaTextChat,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaTextChat object
     */
    virtual MegaTextChat *copy() const;

    /**
     * @brief getHandle Returns the MegaHandle of the chat.
     * @return MegaHandle of the chat.
     */
    virtual MegaHandle getHandle() const;

    /**
     * @brief getOwnPrivilege Returns your privilege level in this chat
     * @return
     */
    virtual int getOwnPrivilege() const;

    /**
     * @brief Returns the chat shard
     * @return The chat shard
     */
    virtual int getShard() const;

    /**
     * @brief getPeerList Returns the full user list and privileges (including yourself).
     *
     * The MegaTextChat retains the ownership of the returned MetaTextChatPeerList. It will
     * be only valid until the MegaTextChat is deleted.
     *
     * @return The list of peers in the chat.
     */
    virtual const MegaTextChatPeerList *getPeerList() const;

    /**
     * @brief Establish the list of peers participating on this chatroom
     *
     * If a peers list already exist, this function will delete it.
     *
     * The MegaTextChat does not take ownership of the list passed as parameter, it makes
     * a local copy.
     *
     * @param peers List of peers
     */
    virtual void setPeerList(const MegaTextChatPeerList *peers);

    /**
     * @brief isGroup Returns whether this chat is a group chat or not
     * @return True if this chat is a group chat. Only chats with more than 2 peers are groupal chats.
     */
    virtual bool isGroup() const;

    /**
     * @brief getOriginatingUser Returns the user that originated the chat notification
     *
     * @note This value is only relevant for new or updated chats notified by MegaGlobalListener::onChatsUpdate or
     * MegaListener::onChatsUpdate.
     *
     * @return The handle of the user who originated the chat notification.
     */
    virtual MegaHandle getOriginatingUser() const;

    /**
     * @brief Returns the title of the chat, if any.
     *
     * The MegaTextChat retains the ownership of the returned string. It will
     * be only valid until the MegaTextChat is deleted.
     *
     * @return The title of the chat as a byte array encoded in Base64URL, or NULL if not available.
     */
    virtual const char *getTitle() const;


    /**
     * @brief Returns true if this chat has an specific change
     *
     * This value is only useful for chats notified by MegaListener::onChatsUpdate or
     * MegaGlobalListener::onChatsUpdate that can notify about chat modifications.
     *
     * In other cases, the return value of this function will be always false.
     *
     * @param changeType The type of change to check. It can be one of the following values:
     *
     * - MegaUser::CHANGE_TYPE_ATTACHMENT      = 0x01
     * Check if the access to nodes have been granted/revoked
     *
     * @return true if this chat has an specific change
     */
    virtual bool hasChanged(int changeType) const;

    /**
     * @brief Returns a bit field with the changes of the chatroom
     *
     * This value is only useful for chats notified by MegaListener::onChatsUpdate or
     * MegaGlobalListener::onChatsUpdate that can notify about chat modifications.
     *
     * @return The returned value is an OR combination of these flags:
     *
     * - MegaUser::CHANGE_TYPE_ATTACHMENT      = 0x01
     * Check if the access to nodes have been granted/revoked
     */
    virtual int getChanges() const;

    /**
     * @brief Indicates if the chat is changed by yourself or by another client.
     *
     * This value is only useful for chats notified by MegaListener::onChatsUpdate or
     * MegaGlobalListener::onChatsUpdate that can notify about chat modifications.
     *
     * @return 0 if the change is external. >0 if the change is the result of an
     * explicit request, -1 if the change is the result of an implicit request
     * made by the SDK internally.
     */
    virtual int isOwnChange() const;

    /**
     * @brief Returns the creation timestamp of the chat
     *
     * In seconds since the Epoch
     *
     * @return Creation date of the chat
     */
    virtual int64_t getCreationTime() const;
};

/**
 * @brief List of MegaTextChat objects
 *
 * A MegaTextChatList has the ownership of the MegaTextChat objects that it contains, so they will be
 * only valid until the MegaTextChatList is deleted. If you want to retain a MegaTextChat returned by
 * a MegaTextChatList, use MegaTextChat::copy.
 *
 * Objects of this class are immutable.
 */
class MegaTextChatList
{
public:

    virtual ~MegaTextChatList();

    virtual MegaTextChatList *copy() const;

    /**
     * @brief Returns the MegaTextChat at the position i in the MegaTextChatList
     *
     * The MegaTextChatList retains the ownership of the returned MegaTextChat. It will be only valid until
     * the MegaTextChatList is deleted. If you want to retain a MegaTextChat returned by this function,
     * use MegaTextChat::copy.
     *
     * If the index is >= the size of the list, this function returns NULL.
     *
     * @param i Position of the MegaTextChat that we want to get for the list
     * @return MegaTextChat at the position i in the list
     */
    virtual const MegaTextChat *get(unsigned int i)  const;

    /**
     * @brief Returns the number of MegaTextChats in the list
     * @return Number of MegaTextChats in the list
     */
    virtual int size() const;
};

#endif

/**
 * @brief Map of string values with string keys (map<string,string>)
 *
 * A MegaStringMap has the ownership of the strings that it contains, so they will be
 * only valid until the MegaStringMap is deleted. If you want to retain a string returned by
 * a MegaStringMap, copy it.
 *
 * Objects of this class are immutable.
 */

class MegaStringMap
{
public:
    virtual ~MegaStringMap();

    virtual MegaStringMap *copy() const;

    /**
     * @brief Returns the string at the position key in the MegaStringMap
     *
     * The returned value is a null-terminated char array. If the value in the map is an array of
     * bytes, then it will be a Base64-encoded string.
     *
     * The MegaStringMap retains the ownership of the returned string. It will be only valid until
     * the MegaStringMap is deleted.
     *
     * If the key is not found in the map, this function returns NULL.
     *
     * @param key Key of the string that you want to get from the map
     * @return String at the position key in the map
     */
    virtual const char* get(const char* key) const;

    /**
     * @brief Returns the list of keys in the MegaStringMap
     *
     * You take the ownership of the returned value
     *
     * @return A MegaStringList containing the keys present in the MegaStringMap
     */
    virtual MegaStringList *getKeys() const;

    /**
     * @brief Sets a value in the MegaStringMap for the given key.
     *
     * If the key already exists in the MegaStringMap, the value will be overwritten by the
     * new value.
     *
     * The MegaStringMap does not take ownership of the strings passed as parameter, it makes
     * a local copy.
     *
     * @param key Key for the new value in the map. It must be a NULL-terminated string.
     * @param value The new value for the key in the map. It must be a NULL-terminated string.
     */
    virtual void set(const char* key, const char *value);

    /**
     * @brief Returns the number of strings in the map
     * @return Number of strings in the map
     */
    virtual int size() const;
};

/**
 * @brief List of strings
 *
 * A MegaStringList has the ownership of the strings that it contains, so they will be
 * only valid until the MegaStringList is deleted. If you want to retain a string returned by
 * a MegaStringList, copy it.
 *
 * Objects of this class are immutable.
 */
class MegaStringList
{
public:
    virtual ~MegaStringList();

    virtual MegaStringList *copy();

    /**
     * @brief Returns the string at the position i in the MegaStringList
     *
     * The MegaStringList retains the ownership of the returned string. It will be only valid until
     * the MegaStringList is deleted.
     *
     * If the index is >= the size of the list, this function returns NULL.
     *
     * @param i Position of the string that we want to get for the list
     * @return string at the position i in the list
     */
    virtual const char* get(int i);

    /**
     * @brief Returns the number of strings in the list
     * @return Number of strings in the list
     */
    virtual int size();
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
        /**
         * @brief Creates a new instance of MegaChatPeerList
         * @return A pointer to the superclass of the private object
         */
        static MegaNodeList * createInstance();

		virtual ~MegaNodeList();

        virtual MegaNodeList *copy();

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
        virtual MegaNode* get(int i);

        /**
         * @brief Returns the number of MegaNode objects in the list
         * @return Number of MegaNode objects in the list
         */
        virtual int size();

        /**
         * @brief Add new node to list
         * @param MegaNode to be added. The node inserted is a copy from 'node'
         */
        virtual void addNode(MegaNode* node);
};

/**
 * @brief Lists of file and folder children MegaNode objects
 *
 * A MegaChildrenLists object has the ownership of the MegaNodeList objects that it contains,
 * so they will be only valid until the MegaChildrenLists is deleted. If you want to retain
 * a MegaNodeList returned by a MegaChildrenLists, use MegaNodeList::copy.
 *
 * Objects of this class are immutable.
 */
class MegaChildrenLists
{
public:
    virtual ~MegaChildrenLists();
    virtual MegaChildrenLists *copy();

    /**
     * @brief Get the list of folder MegaNode objects
     * @return List of MegaNode folders
     */
    virtual MegaNodeList* getFolderList();

    /**
     * @brief Get the list of file MegaNode objects
     * @return List of MegaNode files
     */
    virtual MegaNodeList* getFileList();
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
		virtual ~MegaUserList();

		virtual MegaUserList *copy();

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
        virtual MegaUser* get(int i);

        /**
         * @brief Returns the number of MegaUser objects in the list
         * @return Number of MegaUser objects in the list
         */
        virtual int size();
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
		virtual ~MegaShareList();

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
        virtual MegaShare* get(int i);

        /**
         * @brief Returns the number of MegaShare objects in the list
         * @return Number of MegaShare objects in the list
         */
        virtual int size();
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
		virtual ~MegaTransferList();

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
        virtual MegaTransfer* get(int i);

        /**
         * @brief Returns the number of MegaTransfer objects in the list
         * @return Number of MegaTransfer objects in the list
         */
        virtual int size();
};

/**
 * @brief List of MegaContactRequest objects
 *
 * A MegaContactRequestList has the ownership of the MegaContactRequest objects that it contains, so they will be
 * only valid until the MegaContactRequestList is deleted. If you want to retain a MegaContactRequest returned by
 * a MegaContactRequestList, use MegaContactRequest::copy.
 *
 * Objects of this class are immutable.
 *
 * @see MegaApi::getContactRequests
 */
class MegaContactRequestList
{
    public:
        virtual ~MegaContactRequestList();

        virtual MegaContactRequestList *copy();


        /**
         * @brief Returns the MegaContactRequest at the position i in the MegaContactRequestList
         *
         * The MegaContactRequestList retains the ownership of the returned MegaContactRequest. It will be only valid until
         * the MegaContactRequestList is deleted.
         *
         * If the index is >= the size of the list, this function returns NULL.
         *
         * @param i Position of the MegaContactRequest that we want to get for the list
         * @return MegaContactRequest at the position i in the list
         */
        virtual MegaContactRequest* get(int i);

        /**
         * @brief Returns the number of MegaContactRequest objects in the list
         * @return Number of MegaContactRequest objects in the list
         */
        virtual int size();
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
            TYPE_REMOVE_CONTACT, TYPE_CREATE_ACCOUNT,
            TYPE_CONFIRM_ACCOUNT,
            TYPE_QUERY_SIGNUP_LINK, TYPE_ADD_SYNC, TYPE_REMOVE_SYNC,
            TYPE_REMOVE_SYNCS, TYPE_PAUSE_TRANSFERS,
            TYPE_CANCEL_TRANSFER, TYPE_CANCEL_TRANSFERS,
            TYPE_DELETE, TYPE_REPORT_EVENT, TYPE_CANCEL_ATTR_FILE,
            TYPE_GET_PRICING, TYPE_GET_PAYMENT_ID, TYPE_GET_USER_DATA,
            TYPE_LOAD_BALANCING, TYPE_KILL_SESSION, TYPE_SUBMIT_PURCHASE_RECEIPT,
            TYPE_CREDIT_CARD_STORE, TYPE_UPGRADE_ACCOUNT, TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS,
            TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS, TYPE_GET_SESSION_TRANSFER_URL,
            TYPE_GET_PAYMENT_METHODS, TYPE_INVITE_CONTACT, TYPE_REPLY_CONTACT_REQUEST,
            TYPE_SUBMIT_FEEDBACK, TYPE_SEND_EVENT, TYPE_CLEAN_RUBBISH_BIN,
            TYPE_SET_ATTR_NODE, TYPE_CHAT_CREATE, TYPE_CHAT_FETCH, TYPE_CHAT_INVITE,
            TYPE_CHAT_REMOVE, TYPE_CHAT_URL, TYPE_CHAT_GRANT_ACCESS, TYPE_CHAT_REMOVE_ACCESS,
            TYPE_USE_HTTPS_ONLY, TYPE_SET_PROXY,
            TYPE_GET_RECOVERY_LINK, TYPE_QUERY_RECOVERY_LINK, TYPE_CONFIRM_RECOVERY_LINK,
            TYPE_GET_CANCEL_LINK, TYPE_CONFIRM_CANCEL_LINK,
            TYPE_GET_CHANGE_EMAIL_LINK, TYPE_CONFIRM_CHANGE_EMAIL_LINK,
            TYPE_CHAT_UPDATE_PERMISSIONS, TYPE_CHAT_TRUNCATE, TYPE_CHAT_SET_TITLE, TYPE_SET_MAX_CONNECTIONS,
            TYPE_PAUSE_TRANSFER, TYPE_MOVE_TRANSFER, TYPE_CHAT_PRESENCE_URL, TYPE_REGISTER_PUSH_NOTIFICATION,
            TYPE_GET_USER_EMAIL, TYPE_APP_VERSION, TYPE_GET_LOCAL_SSL_CERT, TYPE_SEND_SIGNUP_LINK,
            TYPE_QUERY_DNS, TYPE_QUERY_GELB, TYPE_CHAT_STATS, TYPE_DOWNLOAD_FILE,
            TYPE_QUERY_TRANSFER_QUOTA, TYPE_PASSWORD_LINK, TYPE_GET_ACHIEVEMENTS,
            TYPE_RESTORE, TYPE_REMOVE_VERSIONS,
            TOTAL_OF_REQUEST_TYPES
        };

        virtual ~MegaRequest();

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
        virtual MegaRequest *copy();

        /**
         * @brief Returns the type of request associated with the object
         * @return Type of request associated with the object
         */
        virtual int getType() const;

        /**
         * @brief Returns a readable string that shows the type of request
         *
         * This function returns a pointer to a statically allocated buffer.
         * You don't have to free the returned pointer
         *
         * @return Readable string showing the type of request
         */
        virtual const char *getRequestString() const;

        /**
         * @brief Returns a readable string that shows the type of request
         *
         * This function provides exactly the same result as MegaRequest::getRequestString.
         * It's provided for a better Java compatibility
         *
         * @return Readable string showing the type of request
         */
        virtual const char* toString() const;

        /**
         * @brief Returns a readable string that shows the type of request
         *
         * This function provides exactly the same result as MegaRequest::getRequestString.
         * It's provided for a better Python compatibility
         *
         * @return Readable string showing the type of request
         */
        virtual const char* __str__() const;

        /**
         * @brief Returns a readable string that shows the type of request
         *
         * This function provides exactly the same result as MegaRequest::getRequestString.
         * It's provided for a better PHP compatibility
         *
         * @return Readable string showing the type of request
         */
        virtual const char* __toString() const;

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
         * - MegaApi::getPaymentId - Returns the handle of the product
         * - MegaApi::syncFolder - Returns the handle of the folder in MEGA
         * - MegaApi::resumeSync - Returns the handle of the folder in MEGA
         * - MegaApi::removeSync - Returns the handle of the folder in MEGA
         * - MegaApi::upgradeAccount - Returns that handle of the product
         * - MegaApi::replyContactRequest - Returns the handle of the contact request
         * - MegaApi::inviteToChat - Returns the handle of the chat
         * - MegaApi::removeFromChat - Returns the handle of the chat
         * - MegaApi::getUrlChat - Returns the handle of the chat
         * - MegaApi::grantAccessInChat - Returns the handle of the node
         * - MegaApi::removeAccessInChat - Returns the handle of the node
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::createFolder - Returns the handle of the new folder
         * - MegaApi::copyNode - Returns the handle of the new node
         * - MegaApi::importFileLink - Returns the handle of the new node
         *
         * @return Handle of a node related to the request
         */
        virtual MegaHandle getNodeHandle() const;

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
         * - MegaApi::getPaymentId - Returns the payment identifier
         * - MegaApi::getUrlChat - Returns the user-specific URL for the chat
         * - MegaApi::getChatPresenceURL - Returns the user-specific URL for the chat presence server
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return Link related to the request
         */
        virtual const char* getLink() const;

        /**
         * @brief Returns the handle of a parent node related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::createFolder - Returns the handle of the parent folder
         * - MegaApi::moveNode - Returns the handle of the new parent for the node
         * - MegaApi::copyNode - Returns the handle of the parent for the new node
         * - MegaApi::importFileLink - Returns the handle of the node that receives the imported file
         * - MegaApi::inviteToChat - Returns the handle of the user to be invited
         * - MegaApi::removeFromChat - Returns the handle of the user to be removed
         * - MegaApi::grantAccessInchat - Returns the chat identifier
         * - MegaApi::removeAccessInchat - Returns the chat identifier
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::syncFolder - Returns a fingerprint of the local folder, to resume the sync with (MegaApi::resumeSync)
         *
         * @return Handle of a parent node related to the request
         */
        virtual MegaHandle getParentHandle() const;

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
        virtual const char* getSessionKey() const;

        /**
         * @brief Returns a name related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::createAccount - Returns the name or the firstname of the user
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
        virtual const char* getName() const;

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
         * - MegaApi::removeContact - Returns the email of the contact
         * - MegaApi::getUserData - Returns the email of the contact
         * - MegaApi::inviteContact - Returns the email of the contact
         * - MegaApi::grantAccessInChat -Returns the MegaHandle of the user in Base64 enconding
         * - MegaApi::removeAccessInChat -Returns the MegaHandle of the user in Base64 enconding
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
        virtual const char* getEmail() const;

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
        virtual const char* getPassword() const;

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
        virtual const char* getNewPassword() const;

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
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getUserData - Returns the private RSA key of the account, Base64-encoded
         *
         * @return Private key related to the request
         */
        virtual const char* getPrivateKey() const;

        /**
         * @brief Returns an access level related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::share - Returns the access level for the shared folder
         * - MegaApi::exportNode - Returns true
         * - MegaApi::disableExport - Returns false
         * - MegaApi::inviteToChat - Returns the privilege level wanted for the user
         *
         * @return Access level related to the request
         */
        virtual int getAccess() const;

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
        virtual const char* getFile() const;

        /**
         * @brief Return the number of times that a request has temporarily failed
         * @return Number of times that a request has temporarily failed
         */
        virtual int getNumRetry() const;

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
        virtual MegaNode *getPublicNode() const;

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
         * You take the ownership of the returned value.
         *
         * @return Public node related to the request
         */
        virtual MegaNode *getPublicMegaNode() const;

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
         * - MegaApi::reportDebugEvent - Returns MegaApi::EVENT_DEBUG
         * - MegaApi::cancelTransfers - Returns MegaTransfer::TYPE_DOWNLOAD if downloads are cancelled or MegaTransfer::TYPE_UPLOAD if uploads are cancelled
         * - MegaApi::setUserAttribute - Returns the attribute type
         * - MegaApi::getUserAttribute - Returns the attribute type
         * - MegaApi::setMaxConnections - Returns the direction of transfers
         *
         * @return Type of parameter related to the request
         */
        virtual int getParamType() const;

        /**
         * @brief Returns a text relative to this request
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::submitFeedback - Returns the comment about the app
         * - MegaApi::reportDebugEvent - Returns the debug message
         * - MegaApi::setUserAttribute - Returns the new value for the attribute
         * - MegaApi::inviteContact - Returns the message appended to the contact invitation
         * - MegaApi::sendEvent - Returns the event message
         * - MegaApi::createAccount - Returns the lastname for the new account
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getUserData - Returns the XMPP JID of the user
         * - MegaApi::getUserAttribute - Returns the value of the attribute
         *
         * @return Text relative to this request
         */
        virtual const char *getText() const;

        /**
         * @brief Returns a number related to this request
         *
         * This value is valid for these requests:
         * - MegaApi::retryPendingConnections - Returns if transfers are retried
         * - MegaApi::submitFeedback - Returns the rating for the app
         * - MegaApi::pauseTransfers - Returns the direction of the transfers to pause/resume
         * - MegaApi::upgradeAccount - Returns the payment method
         * - MegaApi::replyContactRequest - Returns the action to do with the contact request
         * - MegaApi::inviteContact - Returns the action to do with the contact request
         * - MegaApi::sendEvent - Returns the event type
         * - MegaApi::moveTransferUp - Returns MegaTransfer::MOVE_TYPE_UP
         * - MegaApi::moveTransferUpByTag - Returns MegaTransfer::MOVE_TYPE_UP
         * - MegaApi::moveTransferDown - Returns MegaTransfer::MOVE_TYPE_DOWN
         * - MegaApi::moveTransferDownByTag - Returns MegaTransfer::MOVE_TYPE_DOWN
         * - MegaApi::moveTransferToFirst - Returns MegaTransfer::MOVE_TYPE_TOP
         * - MegaApi::moveTransferToFirstByTag - Returns MegaTransfer::MOVE_TYPE_TOP
         * - MegaApi::moveTransferToLast - Returns MegaTransfer::MOVE_TYPE_BOTTOM
         * - MegaApi::moveTransferToLastByTag - Returns MegaTransfer::MOVE_TYPE_BOTTOM
         * - MegaApi::moveTransferBefore - Returns the tag of the transfer with the target position
         * - MegaApi::moveTransferBeforeByTag - Returns the tag of the transfer with the target position
         * - MegaApi::setMaxConnections - Returns the number of connections
         * - MegaApi::queryTransferQuota - Returns the amount of bytes to be transferred
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::resumeSync - Returns the fingerprint of the local file
         * - MegaApi::creditCardQuerySubscriptions - Returns the number of credit card subscriptions
         * - MegaApi::getPaymentMethods - Returns a bitfield with the available payment methods
         *
         * @return Number related to this request
         */
        virtual long long getNumber() const;

        /**
         * @brief Returns a flag related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::retryPendingConnections - Returns if request are disconnected
         * - MegaApi::pauseTransfers - Returns true if transfers were paused, false if they were resumed
         * - MegaApi::createChat - Creates a chat for one or more participants
         * - MegaApi::fetchnodes - Return true if logged in into a folder and the provided key is invalid.
         * - MegaApi::getPublicNode - Return true if the provided key along the link is invalid.
         * - MegaApi::pauseTransfer - Returns true if the transfer has to be pause or false if it has to be resumed
         * - MegaApi::pauseTransferByTag - Returns true if the transfer has to be pause or false if it has to be resumed
         * - MegaApi::moveTransferUp - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferUpByTag - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferDown - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferDownByTag - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferToFirst - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferToFirstByTag - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferToLast - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferToLastByTag - Returns true (it means that it's an automatic move)
         * - MegaApi::moveTransferBefore - Returns false (it means that it's a manual move)
         * - MegaApi::moveTransferBeforeByTag - Returns false (it means that it's a manual move)
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::queryTransferQuota - True if it is expected to get an overquota error, otherwise false
         *
         * @return Flag related to the request
         */
        virtual bool getFlag() const;

        /**
         * @brief Returns the number of transferred bytes during the request
         * @return Number of transferred bytes during the request
         */
        virtual long long getTransferredBytes() const;

        /**
         * @brief Returns the number of bytes that the SDK will have to transfer to finish the request
         * @return Number of bytes that the SDK will have to transfer to finish the request
         */
        virtual long long getTotalBytes() const;

        /**
         * @brief Return the MegaRequestListener associated with this request
         *
         * This function will return NULL if there isn't an associated request listener.
         *
         * @return MegaRequestListener associated with this request
         */
        virtual MegaRequestListener *getListener() const;

        /**
         * @brief Returns details related to the MEGA account
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getAccountDetails - Details of the MEGA account
         *
         * You take the ownership of the returned value.
         *
         * @return Details related to the MEGA account
         */
        virtual MegaAccountDetails *getMegaAccountDetails() const;

        /**
         * @brief Returns available pricing plans to upgrade a MEGA account
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getPricing - Returns the available pricing plans
         *
         * You take the ownership of the returned value.
         *
         * @return Available pricing plans to upgrade a MEGA account
         */
        virtual MegaPricing *getPricing() const;

        /**
         * @brief Returns details related to the MEGA Achievements of this account
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getMegaAchievements - Details of the MEGA Achievements of this account
         *
         * You take the ownership of the returned value.
         *
         * @return Details related to the MEGA Achievements of this account
         */
        virtual MegaAchievementsDetails *getMegaAchievementsDetails() const;

        /**
         * @brief Returns the tag of a transfer related to the request
         *
         * This value is valid for these requests:
         * - MegaApi::cancelTransfer - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
         * - MegaApi::pauseTransfer - Returns the tag of the request to pause or resume
         * - MegaApi::pauseTransferByTag - Returns the tag of the request to pause or resume
         * - MegaApi::moveTransferUp - Returns the tag of the transfer to move
         * - MegaApi::moveTransferUpByTag - Returns the tag of the transfer to move
         * - MegaApi::moveTransferDown - Returns the tag of the transfer to move
         * - MegaApi::moveTransferDownByTag - Returns the tag of the transfer to move
         * - MegaApi::moveTransferToFirst - Returns the tag of the transfer to move
         * - MegaApi::moveTransferToFirstByTag - Returns the tag of the transfer to move
         * - MegaApi::moveTransferToLast - Returns the tag of the transfer to move
         * - MegaApi::moveTransferToLastByTag - Returns the tag of the transfer to move
         * - MegaApi::moveTransferBefore - Returns the tag of the transfer to move
         * - MegaApi::moveTransferBeforeByTag - Returns the tag of the transfer to move
         *
         * @return Tag of a transfer related to the request
         */
        virtual int getTransferTag() const;

        /**
         * @brief Returns the number of details related to this request
         * @return Number of details related to this request
         */
        virtual int getNumDetails() const;

        /**
         * @brief Returns the tag that identifies this request
         *
         * The tag is unique for the MegaApi object that has generated it only
         *
         * @return Unique tag that identifies this request
         */
        virtual int getTag() const;

#ifdef ENABLE_CHAT
        /**
         * @brief Returns the list of peers in a chat.
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::createChat - Returns the list of peers and their privilege level
         *
         * @return List of peers of a chat
         */
        virtual MegaTextChatPeerList *getMegaTextChatPeerList() const;

        /**
         * @brief Returns the list of chats.
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::createChat - Returns the new chat's information
         *
         * @return List of chats
         */
        virtual MegaTextChatList *getMegaTextChatList() const;
#endif

        /**
         * @brief Returns the string map
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getUserAttribute - Returns the attribute value
         *
         * @return String map including the key-value pairs of the attribute
         */
        virtual MegaStringMap* getMegaStringMap() const;
};

/**
 * @brief Provides information about an event
 *
 * Objects of this class aren't live, they are snapshots of the state of the event
 * when the object is created, they are immutable.
 */
class MegaEvent
{
public:

    enum {
        EVENT_COMMIT_DB = 0,
        EVENT_ACCOUNT_CONFIRMATION = 1,
        EVENT_CHANGE_TO_HTTPS = 2,
        EVENT_DISCONNECT = 3
    };

    virtual ~MegaEvent();

    /**
     * @brief Creates a copy of this MegaEvent object
     *
     * The resulting object is fully independent of the source MegaEvent,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaEvent object
     */
    virtual MegaEvent *copy();

    /**
     * @brief Returns the type of the event associated with the object
     * @return Type of the event associated with the object
     */
    virtual int getType() const;

    /**
     * @brief Returns a text relative to this event
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaEvent object is deleted.
     *
     * @return Text relative to this event
     */
    virtual const char *getText() const;
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
        enum {
            TYPE_DOWNLOAD = 0,
            TYPE_UPLOAD,
            TYPE_LOCAL_HTTP_DOWNLOAD
        };

        enum {
            STATE_NONE = 0,
            STATE_QUEUED,
            STATE_ACTIVE,
            STATE_PAUSED,
            STATE_RETRYING,
            STATE_COMPLETING,
            STATE_COMPLETED,
            STATE_CANCELLED,
            STATE_FAILED
        };

        enum {
            MOVE_TYPE_UP = 1,
            MOVE_TYPE_DOWN,
            MOVE_TYPE_TOP,
            MOVE_TYPE_BOTTOM
        };
        
        virtual ~MegaTransfer();

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
        virtual MegaTransfer *copy();

        /**
         * @brief Returns the type of the transfer (TYPE_DOWNLOAD, TYPE_UPLOAD)
         * @return The type of the transfer (TYPE_DOWNLOAD, TYPE_UPLOAD)
         */
        virtual int getType() const;

        /**
         * @brief Returns a readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         *
         * This function returns a pointer to a statically allocated buffer.
         * You don't have to free the returned pointer
         *
         * @return Readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         */
        virtual const char *getTransferString() const;

        /**
         * @brief Returns a readable string that shows the type of the transfer
         *
         * This function provides exactly the same result as MegaTransfer::getTransferString (UPLOAD, DOWNLOAD)
         * It's provided for a better Java compatibility
         *
         * @return Readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         */
        virtual const char* toString() const;

        /**
         * @brief Returns a readable string that shows the type of the transfer
         *
         * This function provides exactly the same result as MegaTransfer::getTransferString (UPLOAD, DOWNLOAD)
         * It's provided for a better Python compatibility
         *
         * @return Readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         */
        virtual const char* __str__() const;

        /**
         * @brief Returns a readable string that shows the type of the transfer
         *
         * This function provides exactly the same result as MegaTransfer::getTransferString (UPLOAD, DOWNLOAD)
         * It's provided for a better PHP compatibility
         *
         * @return Readable string showing the type of transfer (UPLOAD, DOWNLOAD)
         */
        virtual const char *__toString() const;

        /**
         * @brief Returns the starting time of the request (in deciseconds)
         *
         * The returned value is a monotonic time since some unspecified starting point expressed in
         * deciseconds.
         *
         * @return Starting time of the transfer (in deciseconds)
         */
        virtual int64_t getStartTime() const;

        /**
         * @brief Returns the number of transferred bytes during this request
         * @return Transferred bytes during this transfer
         */
        virtual long long getTransferredBytes() const;

        /**
         * @brief Returns the total bytes to be transferred to complete the transfer
         * @return Total bytes to be transferred to complete the transfer
         */
        virtual long long getTotalBytes() const;

        /**
         * @brief Returns the local path related to this request
         *
         * For uploads, this function returns the path to the source file. For downloads, it
         * returns the path of the destination file.
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaTransfer object is deleted.
         *
         * @return Local path related to this transfer
         */
        virtual const char* getPath() const;

        /**
         * @brief Returns the parent path related to this request
         *
         * For uploads, this function returns the path to the folder containing the source file.
         * For downloads, it returns that path to the folder containing the destination file.
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaTransfer object is deleted.
         *
         * @return Parent path related to this transfer
         */
        virtual const char* getParentPath() const;

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
        virtual MegaHandle getNodeHandle() const;

        /**
         * @brief Returns the handle of the parent node related to this transfer
         *
         * For downloads, this function returns always mega::INVALID_HANDLE. For uploads,
         * it returns the handle of the destination node (folder) for the uploaded file.
         *
         * @return The handle of the destination folder for uploads, or mega::INVALID_HANDLE for downloads.
         */
        virtual MegaHandle getParentHandle() const;

        /**
         * @brief Returns the starting position of the transfer for streaming downloads
         *
         * The return value of this fuction will be 0 if the transfer isn't a streaming
         * download (MegaApi::startStreaming)
         *
         * @return Starting position of the transfer for streaming downloads, otherwise 0
         */
        virtual long long getStartPos() const;

        /**
         * @brief Returns the end position of the transfer for streaming downloads
         *
         * The return value of this fuction will be 0 if the transfer isn't a streaming
         * download (MegaApi::startStreaming)
         *
         * @return End position of the transfer for streaming downloads, otherwise 0
         */
        virtual long long getEndPos() const;

		/**
		 * @brief Returns the name of the file that is being transferred
		 *
		 * It's possible to upload a file with a different name (MegaApi::startUpload). In that case,
		 * this function returns the destination name.
		 *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaTransfer object is deleted.
         *
		 * @return Name of the file that is being transferred
		 */
		virtual const char* getFileName() const;

		/**
		 * @brief Returns the MegaTransferListener object associated with this transfer
		 *
		 * MegaTransferListener objects can be associated with transfers at startup, if a listener
		 * isn't associated, this function will return NULL
		 *
		 * @return Listener associated with this transfer
		 */
		virtual MegaTransferListener* getListener() const;

		/**
		 * @brief Return the number of times that a transfer has temporarily failed
		 * @return Number of times that a transfer has temporarily failed
		 */
		virtual int getNumRetry() const;

		/**
		 * @brief Returns the maximum number of times that the transfer will be retried
		 * @return Mmximum number of times that the transfer will be retried
		 */
		virtual int getMaxRetries() const;

		/**
		 * @brief Returns an integer that identifies this transfer
		 * @return Integer that identifies this transfer
		 */
		virtual int getTag() const;

		/**
         * @brief Returns the current speed of this transfer
         * @return Current speed of this transfer
		 */
		virtual long long getSpeed() const;

        /**
         * @brief Returns the average speed of this transfer
         * @return Average speed of this transfer
         */
        virtual long long getMeanSpeed() const;

        /**
		 * @brief Returns the number of bytes transferred since the previous callback
		 * @return Number of bytes transferred since the previous callback
		 * @see MegaListener::onTransferUpdate, MegaTransferListener::onTransferUpdate
		 */
		virtual long long getDeltaSize() const;

		/**
		 * @brief Returns the timestamp when the last data was received (in deciseconds)
		 *
		 * This timestamp doesn't have a defined starting point. Use the difference between
		 * the return value of this function and MegaTransfer::getStartTime to know how
		 * much time the transfer has been running.
		 *
		 * @return Timestamp when the last data was received (in deciseconds)
		 */
		virtual int64_t getUpdateTime() const;

        /**
         * @brief Returns a public node related to the transfer
         *
         * The return value is only valid for downloads of public nodes.
         *
         * You take the ownership of the returned value.
         *
         * @return Public node related to the transfer
         */
        virtual MegaNode *getPublicMegaNode() const;

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
        virtual bool isSyncTransfer() const;

        /**
         * @brief Returns true is this is a streaming transfer
         * @return true if this is a streaming transfer, false otherwise
         * @see MegaApi::startStreaming
         */
        virtual bool isStreamingTransfer() const;

        /**
         * @brief Returns the received bytes since the last callback
         *
         * The returned value is only valid for streaming transfers (MegaApi::startStreaming).
         *
         * @return Received bytes since the last callback
         */
        virtual char *getLastBytes() const;

        /**
         * @brief Returns true if the transfer is a folder transfer
         * @return true if it's a folder transfer, otherwise (file transfer) it returns false
         */
        virtual bool isFolderTransfer() const;

        /**
         * @brief Returns the identifier of the folder transfer associated to this transfer
         *
         * This function is only useful for transfers automatically started in the context of a folder transfer.
         * For folder transfers (the ones directly started with startUpload), it returns -1
         * Otherwise, it returns 0
         *
         * @return Tag of the associated folder transfer.
         */
        virtual int getFolderTransferTag() const;

        /**
         * @brief Returns the application data associated with this transfer
         *
         * You can set the data returned by this function in MegaApi::startDownload
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaTransfer object is deleted.
         *
         * @return Application data associated with this transfer
         */
        virtual const char* getAppData() const;

        /**
         * @brief Returns the state of the transfer
         *
         * It can be one of these values:
         * - STATE_NONE = 0
         * Unknown state. This state should be never returned.
         *
         * - STATE_QUEUED = 1
         * The transfer is queued. No data related to it is being transferred.
         *
         * - STATE_ACTIVE = 2
         * The transfer is active. Its data is being transferred.
         *
         * - STATE_PAUSED = 3
         * The transfer is paused. It won't be activated until it's resumed.
         *
         * - STATE_RETRYING = 4
         * The transfer is waiting to be retried due to a temporary error.
         *
         * - STATE_COMPLETING = 5
         * The transfer is being completed. All data has been transferred
         * but it's still needed to attach the resulting node to the
         * account (uploads), to attach thumbnails/previews to the
         * node (uploads of images) or to create the resulting local
         * file (downloads). The transfer should be completed in a short time.
         *
         * - STATE_COMPLETED = 6
         * The transfer has beeing finished.
         *
         * - STATE_CANCELLED = 7
         * The transfer was cancelled by the user.
         *
         * - STATE_FAILED = 8
         * The transfer was cancelled by the SDK due to a fatal error or
         * after a high number of retries.
         *
         * @return State of the transfer
         */
        virtual int getState() const;

        /**
         * @brief Returns the priority of the transfer
         *
         * This value is intended to keep the order of the transfer queue on apps.
         *
         * @return Priority of the transfer
         */
        virtual unsigned long long getPriority() const;

        /**
         * @brief Returns the notification number of the SDK when this MegaTransfer was generated
         *
         * The notification number of the SDK is increased every time the SDK sends a callback
         * to the app.
         *
         * @return Notification number
         */
        virtual long long getNotificationNumber() const;
};

/**
 * @brief Provides information about transfer queues
 *
 * This object is used as the return value of the function MegaApi::getTransferData
 *
 * Objects of this class aren't live, they are snapshots of the state of the transfer
 * queues when the object is created, they are immutable.
 *
 */
class MegaTransferData
{
public:
    virtual ~MegaTransferData();

    /**
     * @brief Creates a copy of this MegaTransferData object
     *
     * The resulting object is fully independent of the source MegaTransferData,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaTransferData object
     */
    virtual MegaTransferData *copy() const;

    /**
     * @brief Returns the number of downloads in the transfer queue
     * @return Number of downloads in the transfer queue
     */
    virtual int getNumDownloads() const;

    /**
     * @brief Returns the number of uploads in the transfer queue
     * @return Number of uploads in the transfer queue
     */
    virtual int getNumUploads() const;

    /**
     * @brief Returns the tag of the download at index i
     * @param i index of the selected download. It must be between 0 and MegaTransferData::getNumDownloads (not included)
     * @return Tag of the download at index i
     */
    virtual int getDownloadTag(int i) const;

    /**
     * @brief Returns the tag of the upload at index i
     * @param i index of the selected upload. It must be between 0 and MegaTransferData::getNumUploads (not included)
     * @return Tag of the upload at index i
     */
    virtual int getUploadTag(int i) const;

    /**
     * @brief Returns the priority of the download at index i
     * @param i index of the selected download. It must be between 0 and MegaTransferData::getNumDownloads (not included)
     * @return Priority of the download at index i
     */
    virtual unsigned long long getDownloadPriority(int i) const;

    /**
     * @brief Returns the priority of the upload at index i
     * @param i index of the selected upload. It must be between 0 and MegaTransferData::getNumUploads (not included)
     * @return Priority of the upload at index i
     */
    virtual unsigned long long getUploadPriority(int i) const;

    /**
     * @brief Returns the notification number of the SDK when this MegaTransferData was generated
     *
     * The notification number of the SDK is increased every time the SDK sends a callback
     * to the app.
     *
     * @return Notification number
     */
    virtual long long getNotificationNumber() const;
};


/**
 * @brief Provides information about a contact request
 *
 * Developers can use listeners (MegaListener, MegaGlobalListener)
 * to track the progress of each contact. MegaContactRequest objects are provided in callbacks sent
 * to these listeners and allow developers to know the state of the contact requests, their parameters
 * and their results.
 *
 * Objects of this class aren't live, they are snapshots of the state of the contact request
 * when the object is created, they are immutable.
 *
 */
class MegaContactRequest
{
public:
    enum {
        STATUS_UNRESOLVED = 0,
        STATUS_ACCEPTED,
        STATUS_DENIED,
        STATUS_IGNORED,
        STATUS_DELETED,
        STATUS_REMINDED
    };

    enum {
        REPLY_ACTION_ACCEPT = 0,
        REPLY_ACTION_DENY,
        REPLY_ACTION_IGNORE
    };

    enum {
        INVITE_ACTION_ADD = 0,
        INVITE_ACTION_DELETE,
        INVITE_ACTION_REMIND
    };

    virtual ~MegaContactRequest();

    /**
     * @brief Creates a copy of this MegaContactRequest object
     *
     * The resulting object is fully independent of the source MegaContactRequest,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaContactRequest object
     */
    virtual MegaContactRequest *copy() const;

    /**
     * @brief Returns the handle of this MegaContactRequest object
     * @return Handle of the object
     */
    virtual MegaHandle getHandle() const;

    /**
     * @brief Returns the email of the request creator
     * @return Email of the request creator
     */
    virtual char* getSourceEmail() const;

    /**
     * @brief Return the message that the creator of the contact request has added
     * @return Message sent by the request creator
     */
    virtual char* getSourceMessage() const;

    /**
     * @brief Returns the email of the recipient or NULL if the current account is the recipient
     * @return Email of the recipient or NULL if the request is for us
     */
    virtual char* getTargetEmail() const;

    /**
     * @brief Returns the creation time of the contact request
     * @return Creation time of the contact request (in seconds since the Epoch)
     */
    virtual int64_t getCreationTime() const;

    /**
     * @brief Returns the last update time of the contact request
     * @return Last update time of the contact request (in seconds since the Epoch)
     */
    virtual int64_t getModificationTime() const;

    /**
     * @brief Returns the status of the contact request
     *
     * It can be one of the following values:
     * - STATUS_UNRESOLVED = 0
     * The request is pending
     *
     * - STATUS_ACCEPTED = 1
     * The request has been accepted
     *
     * - STATUS_DENIED = 2
     * The request has been denied
     *
     * - STATUS_IGNORED = 3
     * The request has been ignored
     *
     * - STATUS_DELETED = 4
     * The request has been deleted
     *
     * - STATUS_REMINDED = 5
     * The request has been reminded
     *
     * @return Status of the contact request
     */
    virtual int getStatus() const;

    /**
     * @brief Direction of the request
     * @return True if the request is outgoing and false if it's incoming
     */
    virtual bool isOutgoing() const;
};


#ifdef ENABLE_SYNC

/**
 * @brief Provides information about a synchronization event
 *
 * This object is provided in callbacks related to the synchronization engine
 * (MegaListener::onSyncEvent MegaSyncListener::onSyncEvent)
 */
class MegaSyncEvent
{
public:

    /**
     * Event types.
     */
    enum {
        TYPE_LOCAL_FOLDER_ADITION, TYPE_LOCAL_FOLDER_DELETION,
        TYPE_LOCAL_FILE_ADDITION, TYPE_LOCAL_FILE_DELETION,
        TYPE_LOCAL_FILE_CHANGED, TYPE_LOCAL_MOVE,
        TYPE_REMOTE_FOLDER_ADDITION, TYPE_REMOTE_FOLDER_DELETION,
        TYPE_REMOTE_FILE_ADDITION, TYPE_REMOTE_FILE_DELETION,
        TYPE_REMOTE_MOVE, TYPE_REMOTE_RENAME,
        TYPE_FILE_GET, TYPE_FILE_PUT
    };

    virtual ~MegaSyncEvent();

    virtual MegaSyncEvent *copy();

    /**
     * @brief Returns the type of event
     * @return Type of event
     */
    virtual int getType() const;

    /**
     * @brief Returns the local path related to the event.
     *
     * If there isn't any local path related to the event (remote events)
     * this function returns NULL
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaSyncEvent object is deleted.
     *
     * @return Local path related to the event
     */
    virtual const char* getPath() const;

    /**
     * @brief getNodeHandle Returns the node handle related to the event
     *
     * If there isn't any local path related to the event (remote events)
     * this function returns mega::INVALID_HANDLE
     *
     * @return Node handle related to the event
     */
    virtual MegaHandle getNodeHandle() const;

    /**
     * @brief Returns the previous path of the local file.
     *
     * This data is only valid when the event type is TYPE_LOCAL_MOVE
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaSyncEvent object is deleted.
     *
     * @return Previous path of the local file.
     */
    virtual const char* getNewPath() const;

    /**
     * @brief Returns the previous name of the remote node
     *
     * This data is only valid when the event type is TYPE_REMOTE_RENAME
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaSyncEvent object is deleted.
     *
     * @return Previous name of the remote node
     */
    virtual const char* getPrevName() const;

    /**
     * @brief Returns the handle of the previous parent of the remote node
     *
     * This data is only valid when the event type is TYPE_REMOTE_MOVE
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaSyncEvent object is deleted.
     *
     * @return Handle of the previous parent of the remote node
     */
    virtual MegaHandle getPrevParent() const;
};

class MegaRegExpPrivate;

/**
 * @brief Provides a mechanism to handle Regular Expressions
 */
class MegaRegExp
{
public:
    MegaRegExp();
    ~MegaRegExp();

    /**
     * @brief Creates a copy of this MegaRegExp object
     *
     * The resulting object is fully independent of the source MegaRegExp,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaRegExp object
     */
    MegaRegExp *copy();

    bool addRegExp(const char *regExp);
    int getNumRegExp();
    const char *getRegExp(int index);
    bool match(const char *s);

    const char *getFullPattern();

private:
    MegaRegExpPrivate *pImpl;
    MegaRegExp(MegaRegExpPrivate *pImpl);
};

/**
 * @brief Provides information about a synchronization
 *
 * Developers can use listeners (MegaListener, MegaSyncListener)
 * to track the progress of each synchronization. MegaSync objects are provided in callbacks sent
 * to these listeners and allow developers to know the state of the synchronizations and their parameters
 * and
 *
 * The implementation will receive callbacks from an internal worker thread.
 *
 **/
class MegaSyncListener
{
public:
    /**
     * @brief This function is called when the state of a synced file or folder changes
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
     * @param sync MegaSync object related that manages the file
     * @param localPath Local path of the file or folder
     * @param newState New state of the file
     */
    virtual void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState);

    /**
     * @brief This function is called when the state of the synchronization changes
     *
     * The SDK calls this function when the state of the synchronization changes, for example
     * from 'scanning' to 'syncing' or 'failed'.
     *
     * You can use MegaSync::getState to get the new state.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object that has changed the state
     */
    virtual void onSyncStateChanged(MegaApi *api,  MegaSync *sync);

    /**
     * @brief This function is called when there is a synchronization event
     *
     * Synchronization events can be local deletions, local additions, remote deletions,
     * remote additions, etc. See MegaSyncEvent to know the full list of event types
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object that detects the event
     * @param event Information about the event
     *
     * This parameter will be deleted just after the callback. If you want to save it use
     * MegaSyncEvent::copy
     */
    virtual void onSyncEvent(MegaApi *api, MegaSync *sync,  MegaSyncEvent *event);
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

    virtual ~MegaSync();

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
    virtual MegaSync *copy();

    /**
     * @brief Get the handle of the folder that is being synced
     * @return Handle of the folder that is being synced in MEGA
     */
    virtual MegaHandle getMegaHandle() const;

    /**
     * @brief Get the path of the local folder that is being synced
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaRequest object is deleted.
     *
     * @return Local folder that is being synced
     */
    virtual const char* getLocalFolder() const;

    /**
     * @brief Gets an unique identifier of the local folder that is being synced
     * @return Unique identifier of the local folder that is being synced
     */
    virtual long long getLocalFingerprint() const;

    /**
     * @brief Returns the identifier of this synchronization
     *
     * Identifiers of synchronizations are always negative numbers.
     *
     * @return Identifier of the synchronization
     */
    virtual int getTag() const;

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
    virtual int getState() const;
};

#endif

/**
 * @brief Provides information about an error
 */
class MegaError
{
public:
    /**
     * @brief Declaration of API error codes.
     */
    enum
    {
        API_OK = 0,             ///< Everything OK
        API_EINTERNAL = -1,     ///< Internal error.
        API_EARGS = -2,         ///< Bad arguments.
        API_EAGAIN = -3,        ///< Request failed, retry with exponential back-off.
        API_ERATELIMIT = -4,    ///< Too many requests, slow down.
        API_EFAILED = -5,       ///< Request failed permanently.
        API_ETOOMANY = -6,      ///< Too many requests for this resource.
        API_ERANGE = -7,        ///< Resource access out of rage.
        API_EEXPIRED = -8,      ///< Resource expired.
        API_ENOENT = -9,        ///< Resource does not exist.
        API_ECIRCULAR = -10,    ///< Circular linkage.
        API_EACCESS = -11,      ///< Access denied.
        API_EEXIST = -12,       ///< Resource already exists.
        API_EINCOMPLETE = -13,  ///< Request incomplete.
        API_EKEY = -14,         ///< Cryptographic error.
        API_ESID = -15,         ///< Bad session ID.
        API_EBLOCKED = -16,     ///< Resource administratively blocked.
        API_EOVERQUOTA = -17,   ///< Quota exceeded.
        API_ETEMPUNAVAIL = -18, ///< Resource temporarily not available.
        API_ETOOMANYCONNECTIONS = -19, ///< Too many connections on this resource.
        API_EWRITE = -20,       ///< File could not be written to (or failed post-write integrity check).
        API_EREAD = -21,        ///< File could not be read from (or changed unexpectedly during reading).
        API_EAPPKEY = -22,      ///< Invalid or missing application key.
        API_ESSL = -23,         ///< SSL verification failed
        API_EGOINGOVERQUOTA = -24,  ///< Not enough quota

        PAYMENT_ECARD = -101,
        PAYMENT_EBILLING = -102,
        PAYMENT_EFRAUD = -103,
        PAYMENT_ETOOMANY = -104,
        PAYMENT_EBALANCE = -105,
        PAYMENT_EGENERIC = -106
    };

    /**
     * @brief Creates a new MegaError object
     * @param errorCode Error code for this error
     */
    MegaError(int errorCode = MegaError::API_OK);

    /**
     * @brief Creates a new MegaError object
     * @param errorCode Error code for this error
     * @param value Value associated to the error
     */
    MegaError(int errorCode, long long value);

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
         * @brief Returns a value associated with the error
         *
         * Currently, this value is only useful when it is related to an API_EOVERQUOTA
         * error related to a transfer. In that case, it's the number of seconds until
         * the more bandwidth will be available for the account.
         *
         * In any other case, this value will be 0
         *
         * @return Value associated with the error
         */
        long long getValue() const;

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
		 * @brief Returns a readable description of the error
		 *
		 * This function returns a pointer to a statically allocated buffer.
		 * You don't have to free the returned pointer
		 *
		 * This function provides exactly the same result as MegaError::getErrorString.
		 * It's provided for a better PHP compatibility
		 *
		 * @return Readable description of the error
		 */
		const char* __toString() const;

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
        long long value;
};

/**
 * @brief Interface to process node trees
 *
 * An implementation of this class can be used to process a node tree passing a pointer to
 * MegaApi::processMegaTree
 *
 * The implementation will receive callbacks from an internal worker thread.
 *
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
 * The implementation will receive callbacks from an internal worker thread.
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
 * @brief This class extendes the functionality of MegaRequestListener
 * allowing a synchronous behaviour
 * It can be used the same way as a MegaRequestListener by overriding doOnRequestFinish
 * instead of onRequestFinish. This function will be called
 * when onRequestFinish is called by the SDK.
 *
 * For a synchronous usage, a client for this listener may wait() until the request is finished and doOnRequestFinish is completed.
 * Alternatively a trywait function is included which waits for an ammount of time or until the request is finished.
 * Then it can gather the MegaError and MegaRequest objects to process the outcome of the request.
 *
 * @see MegaRequestListener
 */
class SynchronousRequestListener : public MegaRequestListener
{
private:
    MegaSemaphore* semaphore;
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error);

protected:
    MegaRequestListener *listener;
    MegaApi *megaApi;
    MegaRequest *megaRequest;
    MegaError *megaError;

public:
    SynchronousRequestListener();

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
     * @param error Error information
     */
    virtual void doOnRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error);

    /**
     * @brief Wait untill the request is finished. This means that the request has been processed and
     * doOnRequestFinish is completed.
     * After successfully waiting for the request to be finished, the caller can use getError() and getRequest()
     * to gather the output and errors produced by the request. Thus, implementing the callback doOnRequestFinish
     * is not required and the processing can be coded more linearly.
     *
     */
    void wait();

    /**
     * @brief Waits untill either the request is finished or the provided time is passed.
     *
     * After successfully waiting for the request to be finished, the caller can use getError() and getRequest()
     * to gather the output and errors produced by the request. Thus, implementing the callback doOnRequestFinish
     * is not required and the processing can be coded more linearly.
     * @param milliseconds Max number of milliseconds to wait.
     * @return returns 0 if the request had finished and a value different to 0 if timeout passed.
     */
    int trywait(int milliseconds);

    /**
     * @brief Get the MegaError object produced by the request.
     * The RequestListener retains the ownership of the object and will delete upon its destruction
     * @return the error
     */
    MegaError *getError() const;

    /**
     * @brief Get the MegaRequest object produced by the request.
     * The RequestListener retains the ownership of the object and will delete upon its destruction
     * @return the request
     */
    MegaRequest *getRequest() const;

    /**
     * @brief Getter for the MegaApi object that started the request.
     * @return the MegaApi object that started the request.
     */
    MegaApi *getApi() const;

    virtual ~SynchronousRequestListener();
};

/**
 * @brief Interface to receive information about transfers
 *
 * All transfers allows to pass a pointer to an implementation of this interface in the last parameter.
 * You can also get information about all transfers using MegaApi::addTransferListener
 *
 * MegaListener objects can also receive information about transfers
 *
 * The implementation will receive callbacks from an internal worker thread.
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
 * @brief This class extendes the functionality of MegaTransferListener
 * allowing a synchronous behaviour
 * It can be used the same way as a MegaTransferListener by overriding doOnTransferFinish
 * instead of onTransferFinish. This function will be called
 * when onTransferFinish is called by the SDK.
 *
 * For a synchronous usage, a client for this listener may wait() until the transfer is finished and doOnTransferFinish is completed.
 * Alternatively a trywait function is included which waits for an ammount of time or until the transfer is finished.
 * Then it can gather the MegaError and MegaTransfer objects to process the outcome of the transfer.
 *
 * @see MegaTransferListener
 */
class SynchronousTransferListener : public MegaTransferListener
{
private:
    MegaSemaphore* semaphore;
    void onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error);

protected:
    MegaTransferListener *listener;
    MegaApi *megaApi;
    MegaTransfer *megaTransfer;
    MegaError *megaError;

public:
    SynchronousTransferListener();

    /**
     * @brief This function is called when a transfer has finished
     *
     * There won't be more callbacks about this transfer.
     * The last parameter provides the result of the transfer. If the transfer finished without problems,
     * the error code will be API_OK
     *
     * The SDK retains the ownership of the transfer and error parameters.
     * Don't use them after this functions returns.
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * @param api MegaApi object that started the transfer
     * @param transfer Information about the transfer
     * @param error Error information
     */
    virtual void doOnTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error);

    /**
     * @brief Wait untill the transfer is finished. This means that the transfer has been processed and
     * doOnTransferFinish is completed.
     * After successfully waiting for the transfer to be finished, the caller can use getError() and getTransfer()
     * to gather the output and errors produced by the transfer. Thus, implementing the callback doOnTransferFinish
     * is not required and the processing can be coded more linearly.
     *
     */
    void wait();

    /**
     * @brief Waits untill either the transfer is finished or the provided time is passed.
     *
     * After successfully waiting for the transfer to be finished, the caller can use getError() and getTransfer()
     * to gather the output and errors produced by the transfer. Thus, implementing the callback doOnTransferFinish
     * is not required and the processing can be coded more linearly.
     * @param milliseconds Max number of milliseconds to wait.
     * @return returns 0 if the transfer had finished and a value different to 0 if timeout passed.
     */
    int trywait(int milliseconds);

    /**
     * @brief Get the MegaError object produced by the transfer.
     * The TransferListener retains the ownership of the object and will delete upon its destruction
     * @return the error
     */
    MegaError *getError() const;

    /**
     * @brief Get the MegaTransfer object produced by the transfer.
     * The TransferListener retains the ownership of the object and will delete upon its destruction
     * @return the transfer
     */
    MegaTransfer *getTransfer() const;

    /**
     * @brief Getter for the MegaApi object that started the transfer.
     * @return the MegaApi object that started the transfer.
     */
    MegaApi *getApi() const;

    virtual ~SynchronousTransferListener();
};



/**
 * @brief Interface to get information about global events
 *
 * You can implement this interface and start receiving events calling MegaApi::addGlobalListener
 *
 * MegaListener objects can also receive global events
 *
 * The implementation will receive callbacks from an internal worker thread.
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
         * @brief This function is called when the account has been updated (confirmed/upgraded/downgraded)
         *
         * The usage of this callback to handle the external account confirmation is deprecated.
         * Instead, you should use MegaGlobalListener::onEvent.
         *
         * @param api MegaApi object connected to the account
         */
        virtual void onAccountUpdate(MegaApi *api);

        /**
         * @brief This function is called when there are new or updated contact requests in the account
         *
         * When the full account is reloaded or a large number of server notifications arrives at once, the
         * second parameter will be NULL.
         *
         * The SDK retains the ownership of the MegaContactRequestList in the second parameter. The list and all the
         * MegaContactRequest objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaContactRequestList::copy. If you want to save only some of the MegaContactRequest objects, use MegaContactRequest::copy
         * for them.
         *
         * @param api MegaApi object connected to the account
         * @param requests List that contains the new or updated contact requests
         */
        virtual void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests);

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

#ifdef ENABLE_CHAT
        /**
         * @brief This function is called when there are new or updated chats
         *
         * This callback is also used to initialize the list of chats available during the fetchnodes request.
         *
         * The SDK retains the ownership of the MegaTextChatList in the second parameter. The list and all the
         * MegaTextChat objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaTextChatList::copy. If you want to save only some of the MegaTextChat objects, use
         * MegaTextChat::copy for those objects.
         *
         * @param api MegaApi object connected to the account
         * @param chats List that contains the new or updated chats
         */
        virtual void onChatsUpdate(MegaApi* api, MegaTextChatList *chats);
#endif
        /**
         * The details about the event, like the type of event and optionally any
         * additional parameter, is received in the \c params parameter.
         *
         * Currently, the following type of events are notified:
         *
         *  - MegaEvent::EVENT_COMMIT_DB: when the SDK commits the ongoing DB transaction.
         *  This event can be used to keep synchronization between the SDK cache and the
         *  cache managed by the app thanks to the sequence number.
         *
         *  Valid data in the MegaEvent object received in the callback:
         *      - MegaEvent::getText: sequence number recorded by the SDK when this event happened
         *
         *  - MegaEvent::EVENT_ACCOUNT_CONFIRMATION: when a new account is finally confirmed
         * by the user by confirming the signup link.
         *
         *   Valid data in the MegaEvent object received in the callback:
         *      - MegaEvent::getText: email address used to confirm the account
         *
         *  - MegaEvent::EVENT_CHANGE_TO_HTTPS: when the SDK automatically starts using HTTPS for all
         * its communications. This happens when the SDK is able to detect that MEGA servers can't be
         * reached using HTTP or that HTTP communications are being tampered. Transfers of files and
         * file attributes (thumbnails and previews) use HTTP by default to save CPU usage. Since all data
         * is already end-to-end encrypted, it's only needed to use HTTPS if HTTP doesn't work. Anyway,
         * applications can force the SDK to always use HTTPS using MegaApi::useHttpsOnly. It's recommended
         * that applications that receive one of these events save that information on its settings and
         * automatically enable HTTPS on next executions of the app to not force the SDK to detect the problem
         * and automatically switch to HTTPS every time that the application starts.
         *
         *  - MegaEvent::EVENT_DISCONNECT: when the SDK performs a disconnect to reset all the
         * existing open-connections, since they have become unusable. It's recommended that the app
         * receiving this event reset its connections with other servers, since the disconnect
         * performed by the SDK is due to a network change or IP addresses becoming invalid.
         *
         * You can check the type of event by calling MegaEvent::getType
         *
         * The SDK retains the ownership of the details of the event (\c event).
         * Don't use them after this functions returns.
         *
         * @param api MegaApi object connected to the account
         * @param event Details about the event
         */
        virtual void onEvent(MegaApi* api, MegaEvent *event);

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
 * The implementation will receive callbacks from an internal worker thread.
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
         * @brief This function is called when the account has been updated (confirmed/upgraded/downgraded)
         *
         * The usage of this callback to handle the external account confirmation is deprecated.
         * Instead, you should use MegaListener::onEvent.
         *
         * @param api MegaApi object connected to the account
         */
        virtual void onAccountUpdate(MegaApi *api);

        /**
         * @brief This function is called when there are new or updated contact requests in the account
         *
         * When the full account is reloaded or a large number of server notifications arrives at once, the
         * second parameter will be NULL.
         *
         * The SDK retains the ownership of the MegaContactRequestList in the second parameter. The list and all the
         * MegaContactRequest objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaContactRequestList::copy. If you want to save only some of the MegaContactRequest objects, use MegaContactRequest::copy
         * for them.
         *
         * @param api MegaApi object connected to the account
         * @param requests List that contains the new or updated contact requests
         */
        virtual void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests);

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
     * @brief This function is called when the state of a synced file or folder changes
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
     * @param sync MegaSync object manages the file
     * @param localPath Local path of the file or folder
     * @param newState New state of the file
     */
    virtual void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState);

    /**
     * @brief This function is called when there is a synchronization event
     *
     * Synchronization events can be local deletions, local additions, remote deletions,
     * remote additions, etc. See MegaSyncEvent to know the full list of event types
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object that detects the event
     * @param event Information about the event
     *
     * This parameter will be deleted just after the callback. If you want to save it use
     * MegaSyncEvent::copy
     */
    virtual void onSyncEvent(MegaApi *api, MegaSync *sync,  MegaSyncEvent *event);

    /**
     * @brief This function is called when the state of the synchronization changes
     *
     * The SDK calls this function when the state of the synchronization changes. you can use
     * MegaSync::getState to get the new state of the synchronization
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object that has changed its state
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

#ifdef ENABLE_CHAT
    /**
     * @brief This function is called when there are new or updated chats
     *
     * The SDK retains the ownership of the MegaTextChatList in the second parameter. The list and all the
     * MegaTextChat objects that it contains will be valid until this function returns. If you want to save the
     * list, use MegaTextChatList::copy. If you want to save only some of the MegaTextChat objects, use
     * MegaTextChat::copy for those objects.
     *
     * @param api MegaApi object connected to the account
     * @param chats List that contains the new or updated chats
     */
    virtual void onChatsUpdate(MegaApi* api, MegaTextChatList *chats);
#endif

        /**
         * The details about the event, like the type of event and optionally any
         * additional parameter, is received in the \c params parameter.
         *
         * Currently, the following type of events are notified:
         *  - MegaEvent::EVENT_COMMIT_DB: when the SDK commits the ongoing DB transaction.
         *  This event can be used to keep synchronization between the SDK cache and the
         *  cache managed by the app thanks to the sequence number, available at MegaEvent::getText.
         *
         *  - MegaEvent::EVENT_ACCOUNT_CONFIRMATION: when a new account is finally confirmed
         * by the user by confirming the signup link.
         *
         *   Valid data in the MegaEvent object received in the callback:
         *      - MegaEvent::getText: email address used to confirm the account
         *
         *  - MegaEvent::EVENT_CHANGE_TO_HTTPS: when the SDK automatically starts using HTTPS for all
         * its communications. This happens when the SDK is able to detect that MEGA servers can't be
         * reached using HTTP or that HTTP communications are being tampered. Transfers of files and
         * file attributes (thumbnails and previews) use HTTP by default to save CPU usage. Since all data
         * is already end-to-end encrypted, it's only needed to use HTTPS if HTTP doesn't work. Anyway,
         * applications can force the SDK to always use HTTPS using MegaApi::useHttpsOnly. It's recommended
         * that applications that receive one of these events save that information on its settings and
         * automatically enable HTTPS on next executions of the app to not force the SDK to detect the problem
         * and automatically switch to HTTPS every time that the application starts.
         *
         *  - MegaEvent::EVENT_DISCONNECT: when the SDK performs a disconnect to reset all the
         * existing open-connections, since they have become unusable. It's recommended that the app
         * receiving this event reset its connections with other servers, since the disconnect
         * performed by the SDK is due to a network change or IP addresses becoming invalid.
         *
         * You can check the type of event by calling MegaEvent::getType
         *
         * The SDK retains the ownership of the details of the event (\c event).
         * Don't use them after this functions returns.
         *
         * @param api MegaApi object connected to the account
         * @param event Details about the event
         */
        virtual void onEvent(MegaApi* api, MegaEvent *event);

        virtual ~MegaListener();
};

class MegaInputStream
{
public:
    virtual int64_t getSize();
    virtual bool read(char *buffer, size_t size);
    virtual ~MegaInputStream();
};

class MegaApiImpl;

/**
 * @brief Allows to control a MEGA account or a shared folder
 *
 * You must provide an appKey to use this SDK. You can generate an appKey for your app for free here:
 * - https://mega.nz/#sdk
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
 * Some functions in this class return a pointer and give you the ownership. In all of them, memory allocations
 * are made using new (for single objects) and new[] (for arrays) so you should use delete and delete[] to free them.
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

        enum {
            USER_ATTR_AVATAR = 0,               // public - char array
            USER_ATTR_FIRSTNAME = 1,            // public - char array
            USER_ATTR_LASTNAME = 2,             // public - char array
            USER_ATTR_AUTHRING = 3,             // private - byte array
            USER_ATTR_LAST_INTERACTION = 4,     // private - byte array
            USER_ATTR_ED25519_PUBLIC_KEY = 5,   // public - byte array
            USER_ATTR_CU25519_PUBLIC_KEY = 6,   // public - byte array
            USER_ATTR_KEYRING = 7,              // private - byte array
            USER_ATTR_SIG_RSA_PUBLIC_KEY = 8,   // public - byte array
            USER_ATTR_SIG_CU255_PUBLIC_KEY = 9, // public - byte array
            USER_ATTR_LANGUAGE = 14,            // private - char array
            USER_ATTR_PWD_REMINDER = 15,        // private - char array
            USER_ATTR_DISABLE_VERSIONS = 16     // private - byte array
        };

        enum {
            NODE_ATTR_DURATION = 0,
            NODE_ATTR_COORDINATES = 1
        };

        enum {
            PAYMENT_METHOD_BALANCE = 0,
            PAYMENT_METHOD_PAYPAL = 1,
            PAYMENT_METHOD_ITUNES = 2,
            PAYMENT_METHOD_GOOGLE_WALLET = 3,
            PAYMENT_METHOD_BITCOIN = 4,
            PAYMENT_METHOD_UNIONPAY = 5,
            PAYMENT_METHOD_FORTUMO = 6,
            PAYMENT_METHOD_CREDIT_CARD = 8,
            PAYMENT_METHOD_CENTILI = 9,
            PAYMENT_METHOD_WINDOWS_STORE = 13
        };

        enum {
            TRANSFER_METHOD_NORMAL = 0,
            TRANSFER_METHOD_ALTERNATIVE_PORT = 1,
            TRANSFER_METHOD_AUTO = 2,
            TRANSFER_METHOD_AUTO_NORMAL = 3,
            TRANSFER_METHOD_AUTO_ALTERNATIVE = 4
        };

        enum {
            PUSH_NOTIFICATION_ANDROID = 1,
            PUSH_NOTIFICATION_IOS_VOIP = 2,
            PUSH_NOTIFICATION_IOS_STD = 3
        };

        enum {
            PASSWORD_STRENGTH_VERYWEAK = 0,
            PASSWORD_STRENGTH_WEAK = 1,
            PASSWORD_STRENGTH_MEDIUM = 2,
            PASSWORD_STRENGTH_GOOD = 3,
            PASSWORD_STRENGTH_STRONG = 4
        };

        /**
         * @brief Constructor suitable for most applications
         * @param appKey AppKey of your application
         * You can generate your AppKey for free here:
         * - https://mega.nz/#sdk
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
         * - https://mega.nz/#sdk
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
         * - https://mega.nz/#sdk
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
         * @brief Get the current request
         *
         * The return value is only valid when this function is synchronously
         * called inside a callback related to a request. The return value is
         * the same as the received in the parameter of the callback.
         * This function is provided to support the creation of bindings for
         * some programming languaguages like PHP.
         *
         * @return Current request
         */
        MegaRequest *getCurrentRequest();

        /**
         * @brief Get the current transfer
         *
         * The return value is only valid when this function is synchronously
         * called inside a callback related to a transfer. The return value is
         * the same as the received in the parameter of the callback.
         * This function is provided to support the creation of bindings for
         * some programming languaguages like PHP.
         *
         * @return Current transfer
         */
        MegaTransfer *getCurrentTransfer();

        /**
         * @brief Get the current error
         *
         * The return value is only valid when this function is synchronously
         * called inside a callback. The return value is
         * the same as the received in the parameter of the callback.
         * This function is provided to support the creation of bindings for
         * some programming languaguages like PHP.
         *
         * @return Current error
         */
        MegaError *getCurrentError();

        /**
         * @brief Get the current nodes
         *
         * The return value is only valid when this function is synchronously
         * called inside a onNodesUpdate callback. The return value is
         * the same as the received in the parameter of the callback.
         * This function is provided to support the creation of bindings for
         * some programming languaguages like PHP.
         *
         * @return Current nodes
         */
        MegaNodeList *getCurrentNodes();

        /**
         * @brief Get the current users
         *
         * The return value is only valid when this function is synchronously
         * called inside a onUsersUpdate callback. The return value is
         * the same as the received in the parameter of the callback.
         * This function is provided to support the creation of bindings for
         * some programming languaguages like PHP.
         *
         * @return Current users
         */
        MegaUserList *getCurrentUsers();

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
        char* getBase64PwKey(const char *password);

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
        char* getStringHash(const char* base64pwkey, const char* email);

        /**
         * @brief Get internal timestamp used by the SDK
         *
         * This is a time used in certain internal operations.
         *
         * @return actual SDK time in deciseconds
         */
        long long getSDKtime();

        /**
         * @brief Get an URL to transfer the current session to the webclient
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
        void getSessionTransferURL(const char *path, MegaRequestListener *listener = NULL);

        /**
         * @brief Converts a Base32-encoded user handle (JID) to a MegaHandle
         *
         * @param base32Handle Base32-encoded handle (JID)
         * @return User handle
         */
        static MegaHandle base32ToHandle(const char* base32Handle);

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
         * @brief Converts a Base64-encoded user handle to a MegaHandle
         *
         * You can revert this operation using MegaApi::userHandleToBase64
         *
         * @param base64Handle Base64-encoded user handle
         * @return User handle
         */
        static MegaHandle base64ToUserHandle(const char* base64Handle);

        /**
         * @brief Converts the handle of a node to a Base64-encoded string
         *
         * You take the ownership of the returned value
         * You can revert this operation using MegaApi::base64ToHandle
         *
         * @param handle Node handle to be converted
         * @return Base64-encoded node handle
         */
        static char* handleToBase64(MegaHandle handle);

        /**
         * @brief Converts a MegaHandle to a Base64-encoded string
         *
         * You take the ownership of the returned value
         * You can revert this operation using MegaApi::base64ToUserHandle
         *
         * @param User handle to be converted
         * @return Base64-encoded user handle
         */
        static char* userHandleToBase64(MegaHandle handle);

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

#ifdef WINDOWS_PHONE
        /**
         * @brief Set the ID for statistics
         *
         * This function is not thread-safe so it must be used before
         * the creation of instances of MegaApi to not interfere with
         * the internal thread. Otherwise, the behavior of this
         * function is undefined and it could even crash.
         *
         * Only the first call to this function will correctly set the ID.
         * If you call this function more times, it won't have any effect.
         *
         * The id parameter is hashed before being used
         *
         * @param id ID for statistics
         */
        static void setStatsID(const char *id);
#endif

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
         * Using true in this parameter will trigger the callback MegaGlobalListener::onEvent and the callback
         * MegaListener::onEvent with the event type MegaEvent::EVENT_DISCONNECT.
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
         * @brief Close a MEGA session
         *
         * All clients using this session will be automatically logged out.
         *
         * You can get session information using MegaApi::getExtendedAccountDetails.
         * Then use MegaAccountDetails::getNumSessions and MegaAccountDetails::getSession
         * to get session info.
         * MegaAccountSession::getHandle provides the handle that this function needs.
         *
         * If you use mega::INVALID_HANDLE, all sessions except the current one will be closed
         *
         * @param Handle of the session. Use mega::INVALID_HANDLE to cancel all sessions except the current one
         * @param listener MegaRequestListener to track this request
         */
        void killSession(MegaHandle sessionHandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Get data about the logged account
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getName - Returns the name of the logged user
         * - MegaRequest::getPassword - Returns the the public RSA key of the account, Base64-encoded
         * - MegaRequest::getPrivateKey - Returns the private RSA key of the account, Base64-encoded
         * - MegaRequest::getText - Returns the XMPP JID of the logged user
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
         * @brief Get information about a MEGA user
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_USER_DATA.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email or the Base64 handle of the user,
         * depending on the value provided as user parameter
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Returns the XMPP ID of the user
         * - MegaRequest::getPassword - Returns the public RSA key of the user, Base64-encoded
         *
         * @param user Email or Base64 handle of the user
         * @param listener MegaRequestListener to track this request
         */
        void getUserData(const char *user, MegaRequestListener *listener = NULL);

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
        char *dumpSession();

        /**
         * @brief Returns the current sequence number
         *
         * The sequence number indicates the state of a MEGA account known by the SDK.
         * When external changes are received via actionpackets, the sequence number is
         * updated and changes are commited to the local cache.
         *
         * You take the ownership of the returned value.
         *
         * @return The current sequence number
         */
        char *getSequenceNumber();

        /**
         * @brief Returns the current XMPP session key
         *
         * You have to be logged in to get a valid session key. Otherwise,
         * this function returns NULL.
         *
         * You take the ownership of the returned value.
         *
         * @return Current XMPP session key
         */
        char *dumpXMPPSession();

        /**
         * @brief Get an authentication token that can be used to identify the user account
         *
         * If this MegaApi object is not logged into an account, this function will return NULL
         *
         * The value returned by this function can be used in other instances of MegaApi
         * thanks to the function MegaApi::setAccountAuth.
         *
         * You take the ownership of the returned value
         *
         * @return Authentication token
         */
        char *getAccountAuth();

        /**
         * @brief Use an authentication token to identify an account while accessing public folders
         *
         * This function is useful to preserve the PRO status when a public folder is being
         * used. The identifier will be sent in all API requests made after the call to this function.
         *
         * To stop using the current authentication token, it's needed to explicitly call
         * this function with NULL as parameter. Otherwise, the value set would continue
         * being used despite this MegaApi object is logged in or logged out.
         *
         * It's recommended to call this function before the usage of MegaApi::loginToFolder
         *
         * @param auth Authentication token used to identify the account of the user.
         * You can get it using MegaApi::getAccountAuth with an instance of MegaApi logged into
         * an account.
         */
        void setAccountAuth(const char* auth);

        /**
         * @brief Initialize the creation of a new MEGA account
         *
         * This function automatically imports a Welcome PDF file into the new account. The file is
         * automatically imported in the language used for the account. In case there is no file
         * available for the language of the account, it will not be imported.
         *
         * @note If the account has been created correctly, but there is any error related to the
         * importing of the file, this request will still return API_OK. However, the nodehandle
         * at the MegaRequest::getNodeHandle will be INVALID_HANDLE.
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email for the account
         * - MegaRequest::getPassword - Returns the password for the account
         * - MegaRequest::getName - Returns the name of the user
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNodeHandle - Returns the nodehandle of the Welcome PDF file, if it
         * was imported successfully.
         *
         * If this request succeeds, a confirmation email will be sent to the users.
         * If an account with the same email already exists, you will get the error code
         * MegaError::API_EEXIST in onRequestFinish
         *
         * @param email Email for the account
         * @param password Password for the account
         * @param name Name of the user
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This function is deprecated and will eventually be removed. Instead,
         * use the new version of MegaApi::createAccount with firstname and lastname.
         */
        void createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the creation of a new MEGA account, with firstname and lastname
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
        void createAccount(const char* email, const char* password, const char* firstname, const char* lastname, MegaRequestListener *listener = NULL);

        /**
         * @brief Resume a registration process
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
        void resumeCreateAccount(const char* sid, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the creation of a new MEGA account with precomputed keys
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email for the account
         * - MegaRequest::getPrivateKey - Returns the private key calculated with MegaApi::getBase64PwKey
         * - MegaRequest::getName - Returns the name of the user
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
         * @param base64pwkey Private key calculated with MegaApi::getBase64PwKey
         * @param name Name of the user
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This function is deprecated and will eventually be removed. Instead,
         * use the new version of MegaApi::createAccount with firstname and lastname.
         */
        void fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener = NULL);

        /**
         * @brief Sends the confirmation email for a new account
         *
         * This function is useful to send the confirmation link again or to send it to a different
         * email address, in case the user mistyped the email at the registration form.
         *
         * @param email Email for the account
         * @param name Firstname of the user
         * @param password Password for the account
         * @param listener MegaRequestListener to track this request
         */
        void sendSignupLink(const char* email, const char *name, const char *password, MegaRequestListener *listener = NULL);

        /**
         * @brief Sends the confirmation email for a new account
         *
         * This function is useful to send the confirmation link again or to send it to a different
         * email address, in case the user mistyped the email at the registration form.
         *
         * @param email Email for the account
         * @param name Firstname of the user
         * @param base64pwkey Private key calculated with MegaApi::getBase64PwKey
         * @param listener MegaRequestListener to track this request
         */
        void fastSendSignupLink(const char* email, const char *base64pwkey, const char *name, MegaRequestListener *listener = NULL);

        /**
         * @brief Get information about a confirmation link or a new signup link
         *
         * The associated request type with this request is MegaRequest::TYPE_QUERY_SIGNUP_LINK.
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getLink - Returns the confirmation link
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getEmail - Return the email associated with the link
         * - MegaRequest::getName - Returns the name associated with the link (available only for confirmation links)
         *
         * @param link Confirmation link (#confirm) or new signup link (#newsignup)
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
         * As a result of a successfull confirmation, the app will receive the callback
         * MegaListener::onEvent and MegaGlobalListener::onEvent with an event of type
         * MegaEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
         * the account by checking MegaEvent::getText. @see MegaListener::onEvent.
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
         * As a result of a successfull confirmation, the app will receive the callback
         * MegaListener::onEvent and MegaGlobalListener::onEvent with an event of type
         * MegaEvent::EVENT_ACCOUNT_CONFIRMATION. You can check the email used to confirm
         * the account by checking MegaEvent::getText. @see MegaListener::onEvent.
         *
         * @param link Confirmation link
         * @param base64pwkey Private key precomputed with MegaApi::getBase64PwKey
         * @param listener MegaRequestListener to track this request
         */
        void fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the reset of the existing password, with and without the Master Key.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_RECOVERY_LINK.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email for the account
         * - MegaRequest::getFlag - Returns whether the user has a backup of the master key or not.
         *
         * If this request succeeds, a recovery link will be sent to the user.
         * If no account is registered under the provided email, you will get the error code
         * MegaError::API_ENOENT in onRequestFinish
         *
         * @param email Email used to register the account whose password wants to be reset.
         * @param hasMasterKey True if the user has a backup of the master key. Otherwise, false.
         * @param listener MegaRequestListener to track this request
         */
        void resetPassword(const char *email, bool hasMasterKey, MegaRequestListener *listener = NULL);

        /**
         * @brief Get information about a recovery link created by MegaApi::resetPassword.
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
        void queryResetPasswordLink(const char *link, MegaRequestListener *listener = NULL);

        /**
         * @brief Set a new password for the account pointed by the recovery link.
         *
         * Recovery links are created by calling MegaApi::resetPassword and may or may not
         * require to provide the Master Key.
         *
         * @see The flag of the MegaRequest::TYPE_QUERY_RECOVERY_LINK in MegaApi::queryResetPasswordLink.
         *
         * The associated request type with this request is MegaRequest::TYPE_CONFIRM_RECOVERY_LINK
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
        void confirmResetPassword(const char *link, const char *newPwd, const char *masterKey = NULL, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the cancellation of an account.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
         *
         * If this request succeeds, a cancellation link will be sent to the email address of the user.
         * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
         *
         * @see MegaApi::confirmCancelAccount
         *
         * @param listener MegaRequestListener to track this request
         */
        void cancelAccount(MegaRequestListener *listener = NULL);

        /**
         * @brief Get information about a cancel link created by MegaApi::cancelAccount.
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
        void queryCancelLink(const char *link, MegaRequestListener *listener = NULL);

        /**
         * @brief Effectively parks the user's account without creating a new fresh account.
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
        void confirmCancelAccount(const char *link, const char *pwd, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the change of the email address associated to the account.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK.
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getEmail - Returns the email for the account
         *
         * If this request succeeds, a change-email link will be sent to the specified email address.
         * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
         *
         * @param email The new email to be associated to the account.
         * @param listener MegaRequestListener to track this request
         */
        void changeEmail(const char *email, MegaRequestListener *listener = NULL);

        /**
         * @brief Get information about a change-email link created by MegaApi::changeEmail.
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
        void queryChangeEmailLink(const char *link, MegaRequestListener *listener = NULL);

        /**
         * @brief Effectively changes the email address associated to the account.
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
        void confirmChangeEmail(const char *link, const char *pwd, MegaRequestListener *listener = NULL);

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
        char* getMyEmail();

        /**
         * @brief Returns the user handle of the currently open account
         *
         * If the MegaApi object isn't logged in,
         * this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @return User handle of the account
         */
        char* getMyUserHandle();

        /**
         * @brief Returns the user handle of the currently open account
         *
         * If the MegaApi object isn't logged in,
         * this function returns INVALID_HANDLE
         *
         * @return User handle of the account
         */
        MegaHandle getMyUserHandleBinary();

        /**
         * @brief Get the MegaUser of the currently open account
         *
         * If the MegaApi object isn't logged in, this function returns NULL.
         *
         * You take the ownership of the returned value
         *
         * @note The visibility of your own user is undefined and shouldn't be used.
         * @return MegaUser of the currently open account, otherwise NULL
         */
        MegaUser* getMyUser();

        /**
         * @brief Returns the XMPP JID of the currently open account
         *
         * If the MegaApi object isn't logged in,
         * this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @return XMPP JID of the current account
         */
        char* getMyXMPPJid();

        /**
         * @brief Returns whether MEGA Achievements are enabled for the open account
         * @return True if enabled, false otherwise.
         */
        bool isAchievementsEnabled();

#ifdef ENABLE_CHAT
        /**
         * @brief Returns the fingerprint of the signing key of the currently open account
         *
         * If the MegaApi object isn't logged in or there's no signing key available,
         * this function returns NULL
         *
         * You take the ownership of the returned value.
         * Use delete [] to free it.
         *
         * @return Fingerprint of the signing key of the current account
         */
        char* getMyFingerprint();
#endif

        /**
         * @brief Set the active log level
         *
         * This function sets the log level of the logging system. Any log listener registered by
         * MegaApi::addLoggerObject will receive logs with the same or a lower level than
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
         * @brief Enable log to console
         *
         * By default, log to console is false.
         *
         * @param enable True to show messages in console, false to skip them.
         */
        static void setLogToConsole(bool enable);

        /**
         * @brief Add a MegaLogger implementation to receive SDK logs
         *
         * Logs received by this objects depends on the active log level.
         * By default, it is MegaApi::LOG_LEVEL_INFO. You can change it
         * using MegaApi::setLogLevel.
         *
         * You can remove the existing logger by using MegaApi::removeLoggerObject.
         *
         * @param megaLogger MegaLogger implementation
         */
        static void addLoggerObject(MegaLogger *megaLogger);

        /**
         * @brief Remove a MegaLogger implementation to stop receiving SDK logs
         *
         * If the logger was registered in the past, it will stop receiving log
         * messages after the call to this function.
         *
         * @param megaLogger Previously registered MegaLogger implementation
         */
        static void removeLoggerObject(MegaLogger *megaLogger);

        /**
         * @brief Send a log to the logging system
         *
         * This log will be received by the active logger object (MegaApi::setLoggerObject) if
         * the log level is the same or lower than the active log level (MegaApi::setLogLevel)
         *
         * The third and the fouth parameter are optional. You may want to use  __FILE__ and __LINE__
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
         * @brief Create a new empty folder in your local file system
         *
         * @param localPath Path of the new folder
         * @return True if the local folder was successfully created, otherwise false.
         */
        bool createLocalFolder(const char* localPath);

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
         * @brief Copy a node in the MEGA account changing the file name
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
         * @param node Node to copy
         * @param newParent Parent for the new node
         * @param newName Name for the new node
         *
         * @param listener MegaRequestListener to track this request
         */
        void copyNode(MegaNode* node, MegaNode *newParent, const char* newName, MegaRequestListener *listener = NULL);

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
         * If the node has previous versions, they will be deleted too
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node to remove
         * - MegaRequest::getFlag - Returns false because previous versions won't be preserved
         *
         * @param node Node to remove
         * @param listener MegaRequestListener to track this request
         */
        void remove(MegaNode* node, MegaRequestListener *listener = NULL);

        /**
         * @brief Remove all versions from the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_VERSIONS
         *
         * When the request finishes, file versions might not be deleted yet.
         * Deletions are notified using onNodesUpdate callbacks.
         *
         * @param listener MegaRequestListener to track this request
         */
        void removeVersions(MegaRequestListener *listener = NULL);

        /**
         * @brief Remove a version of a file from the MEGA account
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
         * @param node Node to remove
         * @param listener MegaRequestListener to track this request
         */
        void removeVersion(MegaNode* node, MegaRequestListener *listener = NULL);

        /**
         * @brief Restore a previous version of a file
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
         * @param version Node with the version to restore
         * @param listener MegaRequestListener to track this request
         */
        void restoreVersion(MegaNode *version, MegaRequestListener *listener = NULL);

        /**
         * @brief Clean the Rubbish Bin in the MEGA account
         *
         * This function effectively removes every node contained in the Rubbish Bin. In order to
         * avoid accidental deletions, you might want to warn the user about the action.
         *
         * The associated request type with this request is MegaRequest::TYPE_CLEAN_RUBBISH_BIN. This
         * request returns MegaError::API_ENOENT if the Rubbish bin is already empty.
         *
         * @param listener MegaRequestListener to track this request
         */
        void cleanRubbishBin(MegaRequestListener *listener = NULL);

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
        * @brief Send a node to the Inbox of another MEGA user using his email
        *
        * The associated request type with this request is MegaRequest::TYPE_COPY
        * Valid data in the MegaRequest object received on callbacks:
        * - MegaRequest::getNodeHandle - Returns the handle of the node to send
        * - MegaRequest::getEmail - Returns the email of the user that receives the node
        *
        * @param node Node to send
        * @param email Email of the user that receives the node        
        * @param listener MegaRequestListener to track this request
        */
        void sendFileToUser(MegaNode *node, const char* email, MegaRequestListener *listener = NULL);

        /**
         * @brief Share or stop sharing a folder in MEGA with another user using a MegaUser
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
         * The associated request type with this request is MegaRequest::TYPE_SHARE
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
         * @brief Decrypt password-protected public link
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
         * @param listener MegaRequestListener to track this request
         */
        void decryptPasswordProtectedLink(const char* link, const char* password, MegaRequestListener *listener = NULL);

        /**
         * @brief Encrypt public link with password
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
         * @param listener MegaRequestListener to track this request
         */
        void encryptLinkWithPassword(const char* link, const char* password, MegaRequestListener *listener = NULL);

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
         * - MegaRequest::getFlag - Return true if the provided key along the link is invalid.
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
         * - MegaRequest::getText - Returns the file attribute string if \c node is an attached node from chats. NULL otherwise
         * - MegaRequest::getFile - Returns the destination path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         * - MegaRequest::getBase64Key - Returns the nodekey in Base64 (only when node has file attributes)
         * - MegaRequest::getPrivateKey - Returns the file-attribute string (only when node has file attributes)
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
         * - MegaRequest::getText - Returns the file attribute string if \c node is an attached node from chats. NULL otherwise
         * - MegaRequest::getFile - Returns the destination path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
         * - MegaRequest::getBase64Key - Returns the nodekey in Base64 (only when node has file attributes)
         * - MegaRequest::getPrivateKey - Returns the file-attribute string (only when node has file attributes)
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
         * @param user MegaUser to get the avatar. If this parameter is set to NULL, the avatar is obtained
         * for the active account
         * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
         * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
         * will be used as the file name inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the avatar of any user in MEGA
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFile - Returns the destination path
         * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
         *
         * @param user email_or_user Email or user handle (Base64 encoded) to get the avatar. If this parameter is
         * set to NULL, the avatar is obtained for the active account
         * @param dstFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
         * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
         * will be used as the file name inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserAvatar(const char *email_or_handle, const char *dstFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the avatar of the active account
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
         * @param listener MegaRequestListener to track this request
         */
        void getUserAvatar(const char *dstFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the default color for the avatar.
         *
         * This color should be used only when the user doesn't have an avatar.
         *
         * You take the ownership of the returned value.
         *
         * @param user MegaUser to get the color of the avatar.
         * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
         */
        static char *getUserAvatarColor(MegaUser *user);

        /**
         * @brief Get the default color for the avatar.
         *
         * This color should be used only when the user doesn't have an avatar.
         *
         * You take the ownership of the returned value.
         *
         * @param userhandle User handle (Base64 encoded) to get the avatar.
         * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
         */
        static char *getUserAvatarColor(const char *userhandle);

        /**
         * @brief Get an attribute of a MegaUser.
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
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener = NULL);

        /**
         * @brief Get an attribute of any user in MEGA.
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
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserAttribute(const char *email_or_handle, int type, MegaRequestListener *listener = NULL);

        /**
         * @brief Get an attribute of the current account.
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
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserAttribute(int type, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the email address of any user in MEGA.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_USER_EMAIL
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the user (the provided one as parameter)
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getEmail - Returns the email address
         *
         * @param handle Handle of the user to get the attribute.
         * @param listener MegaRequestListener to track this request
         */
        void getUserEmail(MegaHandle handle, MegaRequestListener *listener = NULL);

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
         * @brief Set/Remove the avatar of the MEGA account
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFile - Returns the source path (optional)
         *
         * @param srcFilePath Source path of the file that will be set as avatar.
         * If NULL, the existing avatar will be removed (if any).
         * In case the avatar never existed before, removing the avatar returns MegaError::API_ENOENT
         * @param listener MegaRequestListener to track this request
         */
        void setAvatar(const char *srcFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Set a public attribute of the current user
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
         *
         * @param value New attribute value
         * @param listener MegaRequestListener to track this request
         */
        void setUserAttribute(int type, const char* value, MegaRequestListener *listener = NULL);

        /**
         * @brief Set a private attribute of the current user
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type
         * - MegaRequest::getMegaStringMap - Returns the new value for the attribute
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
         *
         * @param value New attribute value
         * @param listener MegaRequestListener to track this request
         */
        void setUserAttribute(int type, const MegaStringMap *value, MegaRequestListener *listener = NULL);

        /**
         * @brief Set a custom attribute for the node
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
         * @param node Node that will receive the attribute
         * @param attrName Name of the custom attribute.
         * The length of this parameter must be between 1 and 7 UTF8 bytes
         * @param value Value for the attribute
         * @param listener MegaRequestListener to track this request
         */
        void setCustomNodeAttribute(MegaNode *node, const char *attrName, const char* value,  MegaRequestListener *listener = NULL);

        /**
         * @brief Set the duration of audio/video files as a node attribute.
         *
         * To remove the existing duration, set it to MegaNode::INVALID_DURATION.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
         * - MegaRequest::getNumber - Returns the number of seconds for the node
         * - MegaRequest::getFlag - Returns true (official attribute)
         * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_DURATION
         *
         * @param node Node that will receive the information.
         * @param duration Length of the audio/video in seconds.
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated Since the SDK started processing media information internally,
         * it is no longer needed nor recommended to use this function, so it will
         * be removed in a short time.
         */
        void setNodeDuration(MegaNode *node, int duration,  MegaRequestListener *listener = NULL);

        /**
         * @brief Set the GPS coordinates of image files as a node attribute.
         *
         * To remove the existing coordinates, set both the latitude and longitud to
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
         * @param node Node that will receive the information.
         * @param latitude Latitude in signed decimal degrees notation
         * @param longitude Longitude in signed decimal degrees notation
         * @param listener MegaRequestListener to track this request
         */
        void setNodeCoordinates(MegaNode *node, double latitude, double longitude,  MegaRequestListener *listener = NULL);

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
         * @brief Generate a temporary public link of a file/folder in MEGA
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
         * @param expireTime Unix timestamp until the public link will be valid
         * @param listener MegaRequestListener to track this request
         *
         * @note A Unix timestamp represents the number of seconds since 00:00 hours, Jan 1, 1970 UTC
         */
        void exportNode(MegaNode *node, int64_t expireTime, MegaRequestListener *listener = NULL);

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
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getFlag - Return true if logged in into a folder and the provided key is invalid. Otherwise, false.
         *
         * @param listener MegaRequestListener to track this request
         */
        void fetchNodes(MegaRequestListener *listener = NULL);

        /**
         * @brief Get details about the MEGA account
         *
         * Only basic data will be available. If you can get more data (sessions, transactions, purchases),
         * use MegaApi::getExtendedAccountDetails.
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
         * @brief Get details about the MEGA account
         *
         * This function allows to optionally get data about sessions, transactions and purchases related to the account.
         *
         * The associated request type with this request is MegaRequest::TYPE_ACCOUNT_DETAILS
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaAccountDetails - Details of the MEGA account
         *
         * @param listener MegaRequestListener to track this request
         */
        void getExtendedAccountDetails(bool sessions = false, bool purchases = false, bool transactions = false, MegaRequestListener *listener = NULL);

        /**
         * @brief Check if the available transfer quota is enough to transfer an amount of bytes
         *
         * The associated request type with this request is MegaRequest::TYPE_QUERY_TRANSFER_QUOTA
         *
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the amount of bytes to be transferred
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getFlag - True if it is expected to get an overquota error, otherwise false
         *
         * @param size Amount of bytes to be transferred
         * @param listener MegaRequestListener to track this request
         */
        void queryTransferQuota(long long size, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the available pricing plans to upgrade a MEGA account
         *
         * You can get a payment ID for any of the pricing plans provided by this function
         * using MegaApi::getPaymentId
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_PRICING
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getPricing - MegaPricing object with all pricing plans
         *
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::getPaymentId
         */
        void getPricing(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the payment URL for an upgrade
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
        void getPaymentId(MegaHandle productHandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Upgrade an account
         * @param productHandle Product handle to purchase
         *
         * It's possible to get all pricing plans with their product handles using
         * MegaApi::getPricing
         *
         * The associated request type with this request is MegaRequest::TYPE_UPGRADE_ACCOUNT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the product
         * - MegaRequest::getNumber - Returns the payment method
         *
         * @param paymentMethod Payment method
         * Valid values are:
         * - MegaApi::PAYMENT_METHOD_BALANCE = 0
         * Use the account balance for the payment
         *
         * - MegaApi::PAYMENT_METHOD_CREDIT_CARD = 8
         * Complete the payment with your credit card. Use MegaApi::creditCardStore to add
         * a credit card to your account
         *
         * @param listener MegaRequestListener to track this request
         */
        void upgradeAccount(MegaHandle productHandle, int paymentMethod, MegaRequestListener *listener = NULL);

        /**
         * @brief Submit a purchase receipt for verification
         *
         * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
         *
         * @param receipt Purchase receipt
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This function is only compatible with Google Play payments.
         * It only exists for compatibility with previous apps and will be removed soon.
         * Please use the other version of MegaApi::submitPurchaseReceipt that allows
         * to select the payment gateway.
         */
        void submitPurchaseReceipt(const char* receipt, MegaRequestListener *listener = NULL);

        /**
         * @brief Submit a purchase receipt for verification
         *
         * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
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
        void submitPurchaseReceipt(int gateway, const char* receipt, MegaRequestListener *listener = NULL);

        /**
         * @brief Store a credit card
         *
         * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_STORE
         *
         * @param address1 Billing address
         * @param address2 Second line of the billing address (optional)
         * @param city City of the billing address
         * @param province Province of the billing address
         * @param country Contry of the billing address
         * @param postalcode Postal code of the billing address
         * @param firstname Firstname of the owner of the credit card
         * @param lastname Lastname of the owner of the credit card
         * @param creditcard Credit card number. Only digits, no spaces nor dashes
         * @param expire_month Expire month of the credit card. Must have two digits ("03" for example)
         * @param expire_year Expire year of the credit card. Must have four digits ("2010" for example)
         * @param cv2 Security code of the credit card (3 digits)
         * @param listener MegaRequestListener to track this request
         */
        void creditCardStore(const char* address1, const char* address2, const char* city,
                             const char* province, const char* country, const char *postalcode,
                             const char* firstname, const char* lastname, const char* creditcard,
                             const char* expire_month, const char* expire_year, const char* cv2,
                             MegaRequestListener *listener = NULL);

        /**
         * @brief Get the credit card subscriptions of the account
         *
         * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Number of credit card subscriptions
         *
         * @param listener MegaRequestListener to track this request
         */
        void creditCardQuerySubscriptions(MegaRequestListener *listener = NULL);

        /**
         * @brief Cancel credit card subscriptions if the account
         *
         * The associated request type with this request is MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS
         * @param reason Reason for the cancellation. It can be NULL.
         * @param listener MegaRequestListener to track this request
         */
        void creditCardCancelSubscriptions(const char* reason, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the available payment methods
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_PAYMENT_METHODS
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Bitfield with available payment methods
         *
         * To know if a payment method is available, you can do a check like this one:
         * request->getNumber() & (1 << MegaApi::PAYMENT_METHOD_CREDIT_CARD)
         *
         * @param listener MegaRequestListener to track this request
         */
        void getPaymentMethods(MegaRequestListener *listener = NULL);

        /**
         * @brief Export the master key of the account
         *
         * The returned value is a Base64-encoded string
         *
         * With the master key, it's possible to start the recovery of an account when the
         * password is lost:
         * - https://mega.nz/#recovery
         * - MegaApi::resetPassword()
         *
         * You take the ownership of the returned value.
         *
         * @return Base64-encoded master key
         */
        char *exportMasterKey();

        /**
         * @brief Notify the user has exported the master key
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
        void masterKeyExported(MegaRequestListener *listener = NULL);

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
         * @brief Invite another person to be your MEGA contact
         *
         * The user doesn't need to be registered on MEGA. If the email isn't associated with
         * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
         *
         * The associated request type with this request is MegaRequest::TYPE_INVITE_CONTACT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email of the contact
         * - MegaRequest::getText - Returns the text of the invitation
         * - MegaRequest::getNumber - Returns the action
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
         *
         * @param listener MegaRequestListener to track this request
         */
        void inviteContact(const char* email, const char* message, int action, MegaRequestListener* listener = NULL);

        /**
         * @brief Reply to a contact request
         * @param request Contact request. You can get your pending contact requests using MegaApi::getIncomingContactRequests
         * @param action Action for this contact request. Valid values are:
         * - MegaContactRequest::REPLY_ACTION_ACCEPT = 0
         * - MegaContactRequest::REPLY_ACTION_DENY = 1
         * - MegaContactRequest::REPLY_ACTION_IGNORE = 2
         *
         * The associated request type with this request is MegaRequest::TYPE_REPLY_CONTACT_REQUEST
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the contact request
         * - MegaRequest::getNumber - Returns the action
         *
         * @param listener MegaRequestListener to track this request
         */
        void replyContactRequest(MegaContactRequest *request, int action, MegaRequestListener* listener = NULL);

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
         * @brief Logout of the MEGA account invalidating the session
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGOUT
         *
         * Under certain circumstances, this request might return the error code
         * MegaError::API_ESID. It should not be taken as an error, since the reason
         * is that the logout action has been notified before the reception of the
         * logout response itself.
         *
         * @param listener MegaRequestListener to track this request
         */
        void logout(MegaRequestListener *listener = NULL);

        /**
         * @brief Logout of the MEGA account without invalidating the session
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGOUT
         *
         * @param listener MegaRequestListener to track this request
         */
        void localLogout(MegaRequestListener *listener = NULL);

        /**
         * @brief Invalidate the existing cache and create a fresh one
         */
        void invalidateCache();

        /**
         * @brief Estimate the strength of a password
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
        int getPasswordStrength(const char *password);

        /**
         * @brief Submit feedback about the app
         *
         * The associated request type with this request is MegaRequest::TYPE_SUBMIT_FEEDBACK
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - Retuns the comment about the app
         * - MegaRequest::getNumber - Returns the rating for the app
         *
         * @param rating Integer to rate the app. Valid values: from 1 to 5.
         * @param comment Comment about the app
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This function is for internal usage of MEGA apps. This feedback
         * is sent to MEGA servers.
         */
        void submitFeedback(int rating, const char *comment, MegaRequestListener *listener = NULL);

        /**
         * @brief Send events to the stats server
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_EVENT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the event type
         * - MegaRequest::getText - Returns the event message
         *
         * @param eventType Event type
         * @param message Event message
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This function is for internal usage of MEGA apps for debug purposes. This info
         * is sent to MEGA servers.
         */
        void sendEvent(int eventType, const char* message, MegaRequestListener *listener = NULL);

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

        /**
         * @brief Use HTTPS communications only
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
         * @param listener MegaRequestListener to track this request
         */
        void useHttpsOnly(bool httpsOnly, MegaRequestListener *listener = NULL);

        /**
         * @brief Check if the SDK is using HTTPS communications only
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
        bool usingHttpsOnly();

        ///////////////////   TRANSFERS ///////////////////

        /**
         * @brief Upload a file or a folder
         * @param localPath Local path of the file or folder
         * @param parent Parent node for the file or folder in the MEGA account
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char* localPath, MegaNode *parent, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file or a folder, saving custom app data during the transfer
         * @param localPath Local path of the file or folder
         * @param parent Parent node for the file or folder in the MEGA account
         * @param appData Custom app data to save in the MegaTransfer object
         * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
         * related to the transfer.
         * @param listener MegaTransferListener to track this transfer
         */
        void startUploadWithData(const char* localPath, MegaNode *parent, const char* appData, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file or a folder, saving custom app data during the transfer
         * @param localPath Local path of the file or folder
         * @param parent Parent node for the file or folder in the MEGA account
         * @param appData Custom app data to save in the MegaTransfer object
         * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
         * related to the transfer.
         * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
         * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
         * Use this parameter with caution. Set it to true only if you are sure about what are you doing.
         * @param listener MegaTransferListener to track this transfer
         */
        void startUploadWithData(const char* localPath, MegaNode *parent, const char* appData, bool isSourceTemporary, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file or a folder with a custom modification time
         * @param localPath Local path of the file
         * @param parent Parent node for the file in the MEGA account
         * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
         * @param listener MegaTransferListener to track this transfer
         *
         * The custom modification time will be only applied for file transfers. If a folder
         * is transferred using this function, the custom modification time won't have any effect,
         */
        void startUpload(const char* localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file or a folder with a custom modification time
         * @param localPath Local path of the file
         * @param parent Parent node for the file in the MEGA account
         * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
         * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
         * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char* localPath, MegaNode *parent, int64_t mtime, bool isSourceTemporary, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file or folder with a custom name
         * @param localPath Local path of the file or folder
         * @param parent Parent node for the file or folder in the MEGA account
         * @param fileName Custom file name for the file or folder in MEGA
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener = NULL);

        /**
         * @brief Upload a file or a folder with a custom name and a custom modification time
         * @param localPath Local path of the file
         * @param parent Parent node for the file in the MEGA account
         * @param fileName Custom file name for the file in MEGA
         * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
         * @param listener MegaTransferListener to track this transfer
         *
         * The custom modification time will be only applied for file transfers. If a folder
         * is transferred using this function, the custom modification time won't have any effect,
         */
        void startUpload(const char* localPath, MegaNode* parent, const char* fileName, int64_t mtime, MegaTransferListener *listener = NULL);

        /**
         * @brief Download a file or a folder from MEGA
         * @param node MegaNode that identifies the file or folder
         * @param localPath Destination path for the file or folder
         * If this path is a local folder, it must end with a '\' or '/' character and the file name
         * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         *
         * @param listener MegaTransferListener to track this transfer
         */
        void startDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener = NULL);

        /**
         * @brief Download a file or a folder from MEGA, saving custom app data during the transfer
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
        void startDownloadWithData(MegaNode* node, const char* localPath, const char *appData, MegaTransferListener *listener = NULL);

        /**
         * @brief Start an streaming download for a file in MEGA
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
         * @param node MegaNode that identifies the file
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
         * @brief Retry a transfer
         *
         * This function allows to start a transfer based on a MegaTransfer object. It can be used,
         * for example, to retry transfers that finished with an error. To do it, you can retain the
         * MegaTransfer object in onTransferFinish (calling MegaTransfer::copy to take the ownership)
         * and use it later with this function.
         *
         * If the transfer parameter is NULL or is not of type MegaTransfer::TYPE_DOWNLOAD or
         * MegaTransfer::TYPE_UPLOAD (transfers started with MegaApi::startDownload or
         * MegaApi::startUpload) the function returns without doing anything.
         *
         * @param transfer Transfer to be retried
         * @param listener MegaTransferListener to track this transfer
         */
        void retryTransfer(MegaTransfer *transfer, MegaTransferListener *listener = NULL);

        /**
         * @brief Move a transfer one position up in the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_UP
         *
         * @param transfer Transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferUp(MegaTransfer *transfer, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer one position up in the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_UP
         *
         * @param transferTag Tag of the transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferUpByTag(int transferTag, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer one position down in the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_DOWN
         *
         * @param transfer Transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferDown(MegaTransfer *transfer, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer one position down in the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_DOWN
         *
         * @param transferTag Tag of the transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferDownByTag(int transferTag, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer to the top of the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_TOP
         *
         * @param transfer Transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferToFirst(MegaTransfer *transfer, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer to the top of the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_TOP
         *
         * @param transferTag Tag of the transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferToFirstByTag(int transferTag, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer to the bottom of the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_BOTTOM
         *
         * @param transfer Transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferToLast(MegaTransfer *transfer, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer to the bottom of the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns true (it means that it's an automatic move)
         * - MegaRequest::getNumber - Returns MegaTransfer::MOVE_TYPE_BOTTOM
         *
         * @param transferTag Tag of the transfer to move
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferToLastByTag(int transferTag, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer before another one in the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns false (it means that it's a manual move)
         * - MegaRequest::getNumber - Returns the tag of the transfer with the target position
         *
         * @param transfer Transfer to move
         * @param prevTransfer Transfer with the target position
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferBefore(MegaTransfer *transfer, MegaTransfer *prevTransfer, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a transfer before another one in the transfer queue
         *
         * If the transfer is successfully moved, onTransferUpdate will be called
         * for the corresponding listeners of the moved transfer and the new priority
         * of the transfer will be available using MegaTransfer::getPriority
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the transfer to move
         * - MegaRequest::getFlag - Returns false (it means that it's a manual move)
         * - MegaRequest::getNumber - Returns the tag of the transfer with the target position
         *
         * @param transferTag Tag of the transfer to move
         * @param prevTransferTag Tag of the transfer with the target position
         * @param listener MegaRequestListener to track this request
         */
        void moveTransferBeforeByTag(int transferTag, int prevTransferTag, MegaRequestListener *listener = NULL);

        /**
         * @brief Cancel the transfer with a specific tag
         *
         * When a transfer is cancelled, it will finish and will provide the error code
         * MegaError::API_EINCOMPLETE in MegaTransferListener::onTransferFinish and
         * MegaListener::onTransferFinish
         *
         * The associated request type with this request is MegaRequest::TYPE_CANCEL_TRANSFER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the tag of the cancelled transfer (MegaTransfer::getTag)
         *
         * @param transferTag tag that identifies the transfer
         * You can get this tag using MegaTransfer::getTag
         *
         * @param listener MegaRequestListener to track this request
         */
        void cancelTransferByTag(int transferTag, MegaRequestListener *listener = NULL);

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
         * @brief  Pause/resume all transfers in one direction (uploads or downloads)
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
         * @param listener MegaRequestListener to track this request
         */
        void pauseTransfers(bool pause, int direction, MegaRequestListener* listener = NULL);

        /**
         * @brief Pause/resume a transfer
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
        void pauseTransfer(MegaTransfer *transfer, bool pause, MegaRequestListener* listener = NULL);

        /**
         * @brief Pause/resume a transfer
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
        void pauseTransferByTag(int transferTag, bool pause, MegaRequestListener* listener = NULL);

        /**
         * @brief Enable the resumption of transfers 
         *
         * This function enables the cache of transfers, so they can be resumed later.
         * Additionally, if a previous cache already exists (from previous executions),
         * then this function also resumes the existing cached transfers.
         * 
         * @note Cached uploads expire after 24 hours since the last time they were active.
         * @note Cached transfers related to files that have been modified since they were
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
        void enableTransferResumption(const char* loggedOutId = NULL);

        /**
         * @brief Disable the resumption of transfers
         *
         * This function disables the resumption of transfers and also deletes
         * the transfer cache if it exists. See also MegaApi.enableTransferResumption.
         *
         * @param loggedOutId Identifier for a non logged in instance of MegaApi.
         * It doesn't have any effect if MegaApi is logged in.
         */
        void disableTransferResumption(const char* loggedOutId = NULL);

        /**
         * @brief Returns the state (paused/unpaused) of transfers
         * @param direction Direction of transfers to check
         * Valid values for this parameter are:
         * - MegaTransfer::TYPE_DOWNLOAD = 0
         * - MegaTransfer::TYPE_UPLOAD = 1
         *
         * @return true if transfers on that direction are paused, false otherwise
         */
        bool areTransfersPaused(int direction);

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
         * @brief Set the maximum number of connections per transfer
         *
         * The maximum number of allowed connections is 6. If a higher number of connections is passed
         * to this function, it will fail with the error code API_ETOOMANY.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_MAX_CONNECTIONS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the value for \c direction parameter
         * - MegaRequest::getNumber - Returns the number of \c connections
         *
         * @param direction Direction of transfers
         * Valid values for this parameter are:
         * - MegaTransfer::TYPE_DOWNLOAD = 0
         * - MegaTransfer::TYPE_UPLOAD = 1
         * @param connections Maximum number of connection (it should between 1 and 6)
         */
        void setMaxConnections(int direction, int connections, MegaRequestListener* listener = NULL);

        /**
         * @brief Set the maximum number of connections per transfer for downloads and uploads
         *
         * The maximum number of allowed connections is 6. If a higher number of connections is passed
         * to this function, it will fail with the error code API_ETOOMANY.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_MAX_CONNECTIONS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the number of connections
         *
         * @param connections Maximum number of connection (it should between 1 and 6)
         */
        void setMaxConnections(int connections, MegaRequestListener* listener = NULL);

        /**
         * @brief Set the transfer method for downloads
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
        void setDownloadMethod(int method);

        /**
         * @brief Set the transfer method for uploads
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
        void setUploadMethod(int method);

        /**
         * @brief Set the maximum download speed in bytes per second
         *
         * Currently, this method is only available using the cURL-based network layer.
         * It doesn't work with WinHTTP. You can check if the function will have effect
         * by checking the return value. If it's true, the value will be applied. Otherwise,
         * this function returns false.
         *
         * A value <= 0 means unlimited speed
         *
         * @param bpslimit Download speed in bytes per second
         * @return true if the network layer allows to control the download speed, otherwise false
         */
        bool setMaxDownloadSpeed(long long bpslimit);

        /**
         * @brief Set the maximum upload speed in bytes per second
         *
         * Currently, this method is only available using the cURL-based network layer.
         * It doesn't work with WinHTTP. You can check if the function will have effect
         * by checking the return value. If it's true, the value will be applied. Otherwise,
         * this function returns false.
         *
         * A value <= 0 means unlimited speed
         *
         * @param bpslimit Upload speed in bytes per second
         * @return true if the network layer allows to control the upload speed, otherwise false
         */
        bool setMaxUploadSpeed(long long bpslimit);

        /**
         * @brief Get the maximum download speed in bytes per second
         *
         * The value 0 means unlimited speed
         *
         * @return Download speed in bytes per second
         */
        int getMaxDownloadSpeed();

        /**
         * @brief Get the maximum upload speed in bytes per second
         *
         * The value 0 means unlimited speed
         *
         * @return Upload speed in bytes per second
         */
        int getMaxUploadSpeed();

        /**
         * @brief Return the current download speed
         * @return Download speed in bytes per second
         */
        int getCurrentDownloadSpeed();

        /**
         * @brief Return the current download speed
         * @return Download speed in bytes per second
         */
        int getCurrentUploadSpeed();

        /**
         * @brief Return the current transfer speed
         * @param type Type of transfer to get the speed.
         * Valid values are MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD
         * @return Transfer speed for the transfer type, or 0 if the parameter is invalid
         */
        int getCurrentSpeed(int type);

        /**
         * @brief Get the active transfer method for downloads
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
        int getDownloadMethod();

        /**
         * @brief Get the active transfer method for uploads
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
        int getUploadMethod();

        /**
         * @brief Get information about transfer queues
         * @param listener MegaTransferListener to start receiving information about transfers
         * @return Information about transfer queues
         */
        MegaTransferData *getTransferData(MegaTransferListener *listener = NULL);

        /**
         * @brief Get the first transfer in a transfer queue
         *
         * You take the ownership of the returned value.
         *
         * @param type Transfer queue to get the first transfer (MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD)
         * @return MegaTransfer object related to the first transfer in the queue or NULL if there isn't any transfer
         */
        MegaTransfer *getFirstTransfer(int type);

        /**
         * @brief Force an onTransferUpdate callback for the specified transfer
         *
         * The callback will be received by transfer listeners registered to receive all
         * callbacks related to callbacks and additionally by the listener in the last
         * parameter of this function, if it's not NULL.
         *
         * @param transfer Transfer that will be provided in the onTransferUpdate callback
         * @param listener Listener that will receive the callback
         */
        void notifyTransfer(MegaTransfer *transfer, MegaTransferListener *listener = NULL);

        /**
         * @brief Force an onTransferUpdate callback for the specified transfer
         *
         * The callback will be received by transfer listeners registered to receive all
         * callbacks related to callbacks and additionally by the listener in the last
         * parameter of this function, if it's not NULL.
         *
         * @param transferTag Tag of the transfer that will be provided in the onTransferUpdate callback
         * @param listener Listener that will receive the callback
         */
        void notifyTransferByTag(int transferTag, MegaTransferListener *listener = NULL);

        /**
         * @brief Get all active transfers
         *
         * You take the ownership of the returned value
         *
         * @return List with all active transfers
         * @see MegaApi::startUpload, MegaApi::startDownload
         */
        MegaTransferList *getTransfers();

        /**
         * @brief Get all active streaming transfers
         *
         * You take the ownership of the returned value
         *
         * @return List with all active streaming transfers
         * @see MegaApi::startStreaming
         */
        MegaTransferList *getStreamingTransfers();

        /**
         * @brief Get the transfer with a transfer tag
         *
         * That tag can be got using MegaTransfer::getTag
         *
         * You take the ownership of the returned value
         *
         * @param transferTag tag to check
         * @return MegaTransfer object with that tag, or NULL if there isn't any
         * active transfer with it
         *
         */
        MegaTransfer* getTransferByTag(int transferTag);

        /**
         * @brief Get all transfers of a specific type (downloads or uploads)
         *
         * If the parameter isn't MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD
         * this function returns an empty list.
         *
         * You take the ownership of the returned value
         *
         * @param type MegaTransfer::TYPE_DOWNLOAD or MegaTransfer::TYPE_UPLOAD
         * @return List with transfers of the desired type
         */
        MegaTransferList *getTransfers(int type);

        /**
         * @brief Get a list of transfers that belong to a folder transfer
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
         * You take the ownership of the returned value
         *
         * @param transferTag Tag of the folder transfer to check
         * @return List of transfers in the context of the selected folder transfer
         * @see MegaTransfer::isFolderTransfer, MegaTransfer::getFolderTransferTag
         */
        MegaTransferList *getChildTransfers(int transferTag);

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
        void syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRequestListener *listener = NULL);

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
        void resumeSync(const char *localFolder, MegaNode *megaFolder, long long localfp, MegaRequestListener *listener = NULL);

#ifdef USE_PCRE
        /**
         * @brief Synchronize a local folder and a folder in MEGA, having an exclusion list
         *
         * This function should be used to add a new synchronized pair of folders. To resume a previously
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
         * @param regExp Regular expressions to handle excluded files/folders
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::resumeSync
         */
        void syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRegExp *regExp, MegaRequestListener *listener = NULL);

        /**
         * @brief Resume a previously synced folder, having an exclusion list
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
         * @param regExp Regular expressions to handle excluded files/folders
         * @param listener MegaRequestListener to track this request
         */
        void resumeSync(const char *localFolder, MegaNode *megaFolder, long long localfp, MegaRegExp *regExp, MegaRequestListener *listener = NULL);
#endif

        /**
         * @brief Remove a synced folder
         *
         * The folder will stop being synced. No files in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The synchronization will stop and the cache of local files will be deleted
         * If you don't want to delete the local cache use MegaApi::disableSync
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFlag - Returns true
         *
         * @param megaFolder MEGA folder
         * @param listener MegaRequestListener to track this request
         */
        void removeSync(MegaNode *megaFolder, MegaRequestListener *listener = NULL);

        /**
         * @brief Remove a synced folder
         *
         * The folder will stop being synced. No files in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The synchronization will stop and the cache of local files will be deleted
         * If you don't want to delete the local cache use MegaApi::disableSync
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFlag - Returns true
         *
         * @param sync Synchronization to cancel
         * @param listener MegaRequestListener to track this request
         */
        void removeSync(MegaSync *sync, MegaRequestListener *listener = NULL);

        /**
         * @brief Disable a synced folder
         *
         * The folder will stop being synced. No files in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The synchronization will stop but the cache of local files won't be deleted.
         * If you want to also delete the local cache use MegaApi::removeSync
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFlag - Returns false
         *
         * @param megaFolder MEGA folder
         * @param listener MegaRequestListener to track this request
         */
        void disableSync(MegaNode *megaFolder, MegaRequestListener *listener=NULL);

        /**
         * @brief Disable a synced folder
         *
         * The folder will stop being synced. No files in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The synchronization will stop but the cache of local files won't be deleted.
         * If you want to also delete the local cache use MegaApi::removeSync
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFlag - Returns false
         *
         * @param sync Synchronization to disable
         * @param listener MegaRequestListener to track this request
         */
        void disableSync(MegaSync *sync, MegaRequestListener *listener = NULL);

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
         * @brief Set a list of excluded paths
         *
         * Wildcards (* and ?) are allowed
         *
         * @param List of excluded paths
         * @deprecated A more powerful exclusion system based on regular expresions is being developed. This
         * function will be removed in future updates
         */
        void setExcludedPaths(std::vector<std::string> *excludedPaths);

        /**
         * @brief Set a lower limit for synchronized files
         *
         * Files with a size lower than this limit won't be synchronized
         * To disable the limit, you can set it to 0
         *
         * If both limits are enabled and the lower one is greater than the upper one,
         * only files between both limits will be excluded
         *
         * @param limit Lower limit for synchronized files
         */
        void setExclusionLowerSizeLimit(long long limit);

        /**
         * @brief Set an upper limit for synchronized files
         *
         * Files with a size greater than this limit won't be synchronized
         * To disable the limit, you can set it to 0
         *
         * If both limits are enabled and the lower one is greater than the upper one,
         * only files between both limits will be excluded
         *
         * @param limit Upper limit for synchronized files
         */
        void setExclusionUpperSizeLimit(long long limit);

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
         * @brief Check if a path is syncable based on the excluded names and paths and sizes
         * @param name Path to check
         * @param size Size of the file or -1 to ignore the size
         * @return true if the path is syncable, otherwise false
         */
        bool isSyncable(const char *path, long long size);

        /**
         * @brief Check if it's possible to start synchronizing a folder node.
         *
         * Possible return values for this function are:
         * - MegaError::API_OK if the folder is syncable
         * - MegaError::API_ENOENT if the node doesn't exist in the account
         * - MegaError::API_EARGS if the node is NULL or is not a folder
         * - MegaError::API_EACCESS if the node doesn't have full access
         * - MegaError::API_EEXIST if there is a conflicting synchronization (nodes can't be synced twice)
         * - MegaError::API_EINCOMPLETE if the SDK hasn't been built with support for synchronization
         *
         * @param Folder node to check
         * @return MegaError::API_OK if the node is syncable, otherwise it returns an error.
         */
        int isNodeSyncable(MegaNode *node);

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

        /**
         * @brief Get the synchronization identified with a tag
         *
         * You take the ownership of the returned value
         *
         * @param tag Tag that identifies the synchronization
         * @return Synchronization identified by the tag
         */
        MegaSync *getSyncByTag(int tag);

        /**
         * @brief getSyncByNode Get the synchronization associated with a node
         *
         * You take the ownership of the returned value
         *
         * @param node Root node of the synchronization
         * @return Synchronization with the specified root node
         */
        MegaSync *getSyncByNode(MegaNode *node);

        /**
         * @brief getSyncByPath Get the synchronization associated with a local path
         *
         * You take the ownership of the returned value
         *
         * @param localPath Root local path of the synchronization
         * @return Synchronization with the specified root local path
         */
        MegaSync *getSyncByPath(const char *localPath);

#ifdef USE_PCRE
        /**
        * @brief Set a list of rules to exclude files and folders for a given synchronized folder
        * @param sync Synchronization whose rules want to be updated
        * @param regExp List of regular expressions (rules) to exclude file / folders
        */
        void setExcludedRegularExpressions(MegaSync *sync, MegaRegExp *regExp);
#endif

        /**
         * @brief Get the total number of local nodes in the account
         * @return Total number of local nodes in the account
         */
        long long getNumLocalNodes();

        /**
         * @brief Get the path if the file/folder that is blocking the sync engine
         *
         * If the sync engine is not blocked, this function returns NULL
         * You take the ownership of the returned value
         *
         * @return Path of the file that is blocking the sync engine, or NULL if it isn't blocked
         */
        char *getBlockedPath();
#endif

        /**
         * @brief Force a loop of the SDK thread
         * @deprecated This function is only here for debugging purposes. It will probably
         * be removed in future updates
         */
        void update();

        /**
         * @brief Check if the SDK is waiting for something external (filesystem lock or a server)
         * @return true if the SDK is waiting for the server to complete a request
         */
        bool isWaiting();

        /**
         * @brief Check if the SDK is waiting for the server
         * @return true if the SDK is waiting for the server to complete a request
         */
        bool areServersBusy();

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
         * @brief Get the total downloaded bytes
         * @return Total downloaded bytes
         *
         * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalDownloads
         * or just before a log in or a log out.
         *
         * Only regular downloads are taken into account, not streaming nor folder transfers.
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         */
        long long getTotalDownloadedBytes();

        /**
         * @brief Get the total uploaded bytes
         * @return Total uploaded bytes
         *
         * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalUploads
         * or just before a log in or a log out.
         *
         * Only regular uploads are taken into account, not folder transfers.
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         *
         */
        long long getTotalUploadedBytes();

        /**
         * @brief Get the total bytes of started downloads
         * @return Total bytes of started downloads
         *
         * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalDownloads
         * or just before a log in or a log out.
         *
         * Only regular downloads are taken into account, not streaming nor folder transfers.
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         */
        long long getTotalDownloadBytes();

        /**
         * @brief Get the total bytes of started uploads
         * @return Total bytes of started uploads
         *
         * The count starts with the creation of MegaApi and is reset with calls to MegaApi::resetTotalUploads
         * or just before a log in or a log out.
         *
         * Only regular uploads are taken into account, not folder transfers.
         *
         * @deprecated Function related to statistics will be reviewed in future updates to
         * provide more data and avoid race conditions. They could change or be removed in the current form.
         *
         */
        long long getTotalUploadBytes();

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

        /**
         * @brief Get the total number of nodes in the account
         * @return Total number of nodes in the account
         */
        long long getNumNodes();

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
         * @brief Get all versions of a file
         * @param node Node to check
         * @return List with all versions of the node, including the current version
         */
        MegaNodeList* getVersions(MegaNode *node);

        /**
         * @brief Get the number of versions of a file
         * @param node Node to check
         * @return Number of versions of the node, including the current version
         */
        int getNumVersions(MegaNode *node);

        /**
         * @brief Check if a file has previous versions
         * @param node Node to check
         * @return true if the node has any previous version
         */
        bool hasVersions(MegaNode *node);

        /**
         * @brief Get file and folder children of a MegaNode separatedly
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
         * @return Lists with files and folders child MegaNode objects
         */
        MegaChildrenLists* getFileFolderChildren(MegaNode *p, int order = 1);

        /**
         * @brief Returns true if the node has children
         * @return true if the node has children
         */
        bool hasChildren(MegaNode *parent);

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
        char* getNodePath(MegaNode *node);

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
         * @param h Node handle to check
         * @return MegaNode object with the handle, otherwise NULL
         */
        MegaNode *getNodeByHandle(MegaHandle h);

        /**
         * @brief Get the MegaContactRequest that has a specific handle
         *
         * You can get the handle of a MegaContactRequest using MegaContactRequest::getHandle.
         *
         * You take the ownership of the returned value.
         *
         * @param handle Contact request handle to check
         * @return MegaContactRequest object with the handle, otherwise NULL
         */
        MegaContactRequest *getContactRequestByHandle(MegaHandle handle);

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
         * @param user Email or Base64 handle of the user
         * @return MegaUser that has the email address, otherwise NULL
         */
        MegaUser* getContact(const char *user);

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
         * @brief Get a list with all active inboud sharings
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaShare objects that other users are sharing with this account
         */
        MegaShareList *getInSharesList();

        /**
         * @brief Get the user relative to an incoming share
         *
         * This function will return NULL if the node is not found or doesn't represent
         * the root of an incoming share.
         *
         * You take the ownership of the returned value
         *
         * @param node Incoming share
         * @return MegaUser relative to the incoming share
         */
        MegaUser *getUserFromInShare(MegaNode *node);

        /**
          * @brief Check if a MegaNode is being shared by/with your own user
          *
          * For nodes that are being shared, you can get a list of MegaShare
          * objects using MegaApi::getOutShares, or a list of MegaNode objects
          * using MegaApi::getInShares
          *
          * @param node Node to check
          * @return true is the MegaNode is being shared, otherwise false
          * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
          * Use MegaNode::isShared instead
         */
         bool isShared(MegaNode *node);

         /**
          * @brief Check if a MegaNode is being shared with other users
          *
          * For nodes that are being shared, you can get a list of MegaShare
          * objects using MegaApi::getOutShares
          *
          * @param node Node to check
          * @return true is the MegaNode is being shared, otherwise false
          * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
          * Use MegaNode::isOutShare instead
          */
         bool isOutShare(MegaNode *node);

         /**
          * @brief Check if a MegaNode belong to another User, but it is shared with you
          *
          * For nodes that are being shared, you can get a list of MegaNode
          * objects using MegaApi::getInShares
          *
          * @param node Node to check
          * @return true is the MegaNode is being shared, otherwise false
          * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
          * Use MegaNode::isInShare instead
          */
         bool isInShare(MegaNode *node);

        /**
         * @brief Check if a MegaNode is pending to be shared with another User. This situation
         * happens when a node is to be shared with a User which is not a contact yet.
         *
         * For nodes that are pending to be shared, you can get a list of MegaNode
         * objects using MegaApi::getPendingShares
         *
         * @param node Node to check
         * @return true is the MegaNode is pending to be shared, otherwise false
         */
        bool isPendingShare(MegaNode *node);

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
         * @brief Get a list with all pending outbound sharings
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaShare objects
         */
        MegaShareList *getPendingOutShares();

        /**
         * @brief Get a list with all pending outbound sharings
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaShare objects
         */
        MegaShareList *getPendingOutShares(MegaNode *node);

        /**
         * @brief Get a list with all public links
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaNode objects that are shared with everyone via public link
         */
        MegaNodeList *getPublicLinks();

        /**
         * @brief Get a list with all incoming contact requests
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaContactRequest objects
         */
        MegaContactRequestList *getIncomingContactRequests();

        /**
         * @brief Get a list with all outgoing contact requests
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaContactRequest objects
         */
        MegaContactRequestList *getOutgoingContactRequests();

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
        char* getFingerprint(const char *filePath);

        /**
         * @brief Get a Base64-encoded fingerprint for a node
         *
         * If the node doesn't exist or doesn't have a fingerprint, this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @param node Node for which we want to get the fingerprint
         * @return Base64-encoded fingerprint for the file
         * @deprecated Use MegaNode::getFingerprint instead of this function
         */
        char *getFingerprint(MegaNode *node);

        /**
         * @brief Get a Base64-encoded fingerprint from an input stream and a modification time
         *
         * If the input stream is NULL, has a negative size or can't be read, this function returns NULL
         *
         * You take the ownership of the returned value
         *
         * @param inputStream Input stream that provides the data to create the fingerprint
         * @param mtime Modification time that will be taken into account for the creation of the fingerprint
         * @return Base64-encoded fingerprint
         */
        char* getFingerprint(MegaInputStream *inputStream, int64_t mtime);

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
         * @brief Returns a node with the provided fingerprint
         *
         * If there isn't any node in the account with that fingerprint, this function returns NULL.
         * If there are several nodes with the same fingerprint, nodes in the preferred
         * parent folder take precedence.
         *
         * You take the ownership of the returned value.
         *
         * @param fingerprint Fingerprint to check
         * @param parent Preferred parent node
         * @return MegaNode object with the provided fingerprint
         */
        MegaNode *getNodeByFingerprint(const char *fingerprint, MegaNode* parent);

        /**
         * @brief Returns all nodes that have a fingerprint
         *
         * If there isn't any node in the account with that fingerprint, this function returns an empty MegaNodeList.
         *
         * You take the ownership of the returned value.
         *
         * @param fingerprint Fingerprint to check
         * @return List of nodes with the same fingerprint
         */
        MegaNodeList *getNodesByFingerprint(const char* fingerprint);

        /**
         * @brief Returns a node with the provided fingerprint that can be exported
         *
         * If there isn't any node in the account with that fingerprint, this function returns NULL.
         * If a file name is passed in the second parameter, it's also checked if nodes with a matching
         * fingerprint has that name. If there isn't any matching node, this function returns NULL.
         * This function ignores nodes that are inside the Rubbish Bin because public links to those nodes
         * can't be downloaded.
         *
         * You take the ownership of the returned value.
         *
         * @param fingerprint Fingerprint to check
         * @param name Name that the node should have (optional)
         * @return Exportable node that meet the requirements
         */
        MegaNode *getExportableNodeByFingerprint(const char *fingerprint, const char *name = NULL);

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
         * @brief getCRC Get the CRC of a file
         *
         * The CRC of a file is a hash of its contents.
         * If you need a more realiable method to check files, use fingerprint functions
         * (MegaApi::getFingerprint, MegaApi::getNodeByFingerprint) that also takes into
         * account the size and the modification time of the file to create the fingerprint.
         *
         * You take the ownership of the returned value.
         *
         * @param filePath Local file path
         * @return Base64-encoded CRC of the file
         */
        char* getCRC(const char *filePath);
    
        /**
         * @brief Get the CRC from a fingerprint
         *
         * You take the ownership of the returned value.
         *
         * @param fingerprint fingerprint from which we want to get the CRC
         * @return Base64-encoded CRC from the fingerprint
         */
        char *getCRCFromFingerprint(const char *fingerprint);

        /**
         * @brief getCRC Get the CRC of a node
         *
         * The CRC of a node is a hash of its contents.
         * If you need a more realiable method to check files, use fingerprint functions
         * (MegaApi::getFingerprint, MegaApi::getNodeByFingerprint) that also takes into
         * account the size and the modification time of the node to create the fingerprint.
         *
         * You take the ownership of the returned value.
         *
         * @param node Node for which we want to get the CRC
         * @return Base64-encoded CRC of the node
         */
        char* getCRC(MegaNode *node);

        /**
         * @brief getNodeByCRC Returns a node with the provided CRC
         *
         * If there isn't any node in the selected folder with that CRC, this function returns NULL.
         * If there are several nodes with the same CRC, anyone can be returned.
         *
         * You take the ownership of the returned value.
         *
         * @param crc CRC to check
         * @param parent Parent node to scan. It must be a folder.
         * @return  Node with the selected CRC in the selected folder, or NULL
         * if it's not found.
         */
        MegaNode* getNodeByCRC(const char *crc, MegaNode* parent);

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
         * @brief Check if the MEGA filesystem is available in the local computer
         *
         * This function returns true after a successful call to MegaApi::fetchNodes,
         * otherwise it returns false
         *
         * @return True if the MEGA filesystem is available
         */
        bool isFilesystemAvailable();

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
         * @brief Returns the root node of one node
         *
         * You take the ownership of the returned value
         *
         * @param node Node to check
         * @return Root node for the \c node
         */
        MegaNode *getRootNode(MegaNode *node);

        /**
         * @brief Check if a node is in the Cloud Drive tree
         *
         * @param node Node to check
         * @return True if the node is in the cloud drive
         */
        bool isInCloud(MegaNode *node);

        /**
         * @brief Check if a node is in the Rubbish bin tree
         *
         * @param node Node to check
         * @return True if the node is in the Rubbish bin
         */
        bool isInRubbish(MegaNode *node);

        /**
         * @brief Check if a node is in the Inbox tree
         *
         * @param node Node to check
         * @return True if the node is in the Inbox
         */
        bool isInInbox(MegaNode *node);

        /**
         * @brief Set default permissions for new files
         *
         * This function allows to change the permissions that will be received
         * by newly created files.
         *
         * It's possible to change group permissions, public permissions and the
         * executable permission for the user. "rw" permissions for the user will
         * be always granted to prevent synchronization problems.
         *
         * To check the effective permissions that will be applied, please use
         * MegaApi::getDefaultFilePermissions
         *
         * Currently, this function only works for OS X and Linux (or any other
         * platform using the Posix filesystem layer). On Windows, it doesn't have
         * any effect.
         *
         * @param permissions Permissions for new files in the same format accepted by chmod() (0755, for example)
         */
        void setDefaultFilePermissions(int permissions);

        /**
         * @brief Get default permissions for new files
         *
         * This function returns the permissions that will be applied to new files.
         *
         * Currently, this function only works on OS X and Linux (or any other
         * platform using the Posix filesystem layer). On Windows it returns 0600
         *
         * @return Permissions for new files in the same format accepted by chmod() (0755, for example)
         */
        int getDefaultFilePermissions();

        /**
         * @brief Set default permissions for new folders
         *
         * This function allows to change the permissions that will be received
         * by newly created folders.
         *
         * It's possible to change group permissions and public permissions.
         * "rwx" permissions for the user will be always granted to prevent
         * synchronization problems.
         *
         * To check the effective permissions that will be applied, please use
         * MegaApi::getDefaultFolderPermissions
         *
         * Currently, this function only works for OS X and Linux (or any other
         * platform using the Posix filesystem layer). On Windows, it doesn't have
         * any effect.
         *
         * @param permissions Permissions for new folders in the same format accepted by chmod() (0755, for example)
         */
        void setDefaultFolderPermissions(int permissions);

        /**
         * @brief Get default permissions for new folders
         *
         * This function returns the permissions that will be applied to new folders.
         *
         * Currently, this function only works on OS X and Linux (or any other
         * platform using the Posix filesystem layer). On Windows, it returns 0700
         *
         * @return Permissions for new folders in the same format accepted by chmod() (0755, for example)
         */
        int getDefaultFolderPermissions();

        /**
         * @brief Get the time (in seconds) during which transfers will be stopped due to a bandwidth overquota
         * @return Time (in seconds) during which transfers will be stopped, otherwise 0
         */
        long long getBandwidthOverquotaDelay();

        /**
         * @brief Search nodes containing a search string in their name
         *
         * The search is case-insensitive.
         *
         * You take the ownership of the returned value.
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
         * @brief Search nodes containing a search string in their name
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
        MegaNodeList* search(const char* searchString);

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

        /**
         * @brief Create a MegaNode that represents a file of a different account
         *
         * The resulting node can be used in MegaApi::startDownload and MegaApi::startStreaming but
         * can not be copied.
         *
         * At least the parameters handle, key, size, mtime and auth must be correct to be able to use the resulting node.
         *
         * You take the ownership of the returned value.
         *
         * @param handle Handle of the node
         * @param key Key of the node (Base64 encoded)
         * @param name Name of the node (Base64 encoded)
         * @param size Size of the node
         * @param mtime Modification time of the node
         * @param parentHandle Handle of the parent node
         * @param privateAuth Private authentication token to access the node
         * @param publicAuth Public authentication token to access the node
         * @return MegaNode object
         */
        MegaNode *createForeignFileNode(MegaHandle handle, const char *key, const char *name,
                                       int64_t size, int64_t mtime, MegaHandle parentHandle, const char *privateAuth, const char *publicAuth);

        /**
         * @brief Create a MegaNode that represents a folder of a different account
         *
         * The resulting node can not be successfully used in any other function of MegaApi.
         * The resulting object is only useful to store the values passed as parameters.
         *
         * You take the ownership of the returned value.

         * @param handle Handle of the node
         * @param name Name of the node (Base64 encoded)
         * @param parentHandle Handle of the parent node
         * @param privateAuth Private authentication token to access the node
         * @param publicAuth Public authentication token to access the node
         * @return MegaNode object
         */
        MegaNode *createForeignFolderNode(MegaHandle handle, const char *name, MegaHandle parentHandle, const char *privateAuth, const char *publicAuth);

        /**
         * @brief Returns a MegaNode that can be downloaded with any instance of MegaApi
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
         * @return Authorized node, or NULL if the node can't be authorized
         */
        MegaNode *authorizeNode(MegaNode *node);

        /**
         * @brief Get the SDK version
         *
         * The returned string is an statically allocated array.
         * Do not delete it.
         *
         * @return SDK version
         */
        const char *getVersion();

        /**
         * @brief Get a string with the version of the operating system
         *
         * You take the ownership of the returned string
         *
         * @return Version of the operating system
         */
        char *getOperatingSystemVersion();

        /**
         * @brief Get the last available version of the app
         *
         * It returns the last available version corresponding to an app token
         *
         * The associated request type with this request is MegaRequest::TYPE_APP_VERSION
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - Returns the app token
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Returns the last available version code of the app
         * - MegaRequest::getName - Returns the last available version string of the app
         *
         * Usually, the version code is used to internally control updates, and the version
         * string is intended to be shown to final users.
         *
         * @param appKey Token of the app to check or NULL to use the same value as in the
         * initialization of the MegaApi object
         * @param listener MegaRequestListener to track this request
         */
        void getLastAvailableVersion(const char *appKey = NULL, MegaRequestListener *listener = NULL);

        /**
         * @brief Get a SSL certificate for communications with the webclient
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_LOCAL_SSL_CERT
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Returns the expiration time of the certificate (in seconds since the Epoch)
         * - MegaRequest::getMegaStringMap - Returns the data of the certificate
         *
         * The data returned in the string map is encoded in PEM format.
         * The key "key" of the map contains the private key of the certificate.
         * The key "cert" of the map contains the certificate.
         * Intermediate certificates are provided in keys "intermediate_1" - "intermediate_X".
         *
         * @param listener MegaRequestListener to track this request
         */
        void getLocalSSLCertificate(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the IP of a MegaChat server
         *
         * This function allows to get the correct IP to connect to a MEGAchat server
         * using Websockets.
         *
         * The associated request type with this request is MegaRequest::TYPE_QUERY_DNS
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Returns the IP of the hostname. 
         * IPv6 addresses are returned between brackets
         *
         * @param hostname Hostname to resolve
         * @param listener MegaRequestListener to track this request
         */
        void queryDNS(const char *hostname, MegaRequestListener *listener = NULL);

        /**
         * @brief queryGeLB Query the GeLB server for a given service
         *
         * The associated request type with this request is MegaRequest::TYPE_QUERY_GELB
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Return the HTTP status code from the GeLB server
         * - MegaRequest::getText - Returns the JSON response from the GeLB server
         * - MegaRequest::getTotalBytes - Returns the number of bytes in the response
         *
         * @param service Service to check
         * @param timeoutms Timeout for the request, including all possible retries
         * @param maxretries Maximum number of retries for the request
         * @param listener MegaRequestListener to track this request
         */
        void queryGeLB(const char *service, int timeoutms = 4000, int maxretries = 4, MegaRequestListener *listener = NULL);

        /**
         * @brief Download a file using a HTTP GET request
         *
         * The associated request type with this request is MegaRequest::TYPE_DOWNLOAD_FILE
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Return the HTTP status code from the server
         * - MegaRequest::getTotalBytes - Returns the number of bytes of the file
         *
         * If the request finishes with the error code MegaError::API_OK, the destination path
         * contains the downloaded file. If it's not possible to write in the destination path
         * the error code will be MegaError::API_EWRITE
         *
         * @param url URL of the file
         * @param dstpath Destination path for the downloaded file
         * @param listener MegaRequestListener to track this request
         */
        void downloadFile(const char *url, const char *dstpath, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the User-Agent header used by the SDK
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaApi object is deleted.
         *
         * @return User-Agent used by the SDK
         */
        const char *getUserAgent();

        /**
         * @brief Get the base path set during initialization
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaApi object is deleted.
         *
         * @return Base path
         */
        const char *getBasePath();

        /**
         * @brief Change the API URL
         *
         * This function allows to change the API URL.
         * It's only useful for testing or debugging purposes.
         *
         * @param apiURL New API URL
         * @param disablepkp true to disable public key pinning for this URL
         */
        void changeApiUrl(const char *apiURL, bool disablepkp = false);

        /**
         * @brief Set the language code used by the app
         * @param Language code used by the app
         *
         * @return True if the language code is known for the SDK, otherwise false
         */
        bool setLanguage(const char* languageCode);

        /**
         * @brief Set the preferred language of the user
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
        void setLanguagePreference(const char* languageCode, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the preferred language of the user
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Return the language code
         *
         * @param listener MegaRequestListener to track this request
         */
        void getLanguagePreference(MegaRequestListener *listener = NULL);

        /**
         * @brief Enable or disable file versioning
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
        void setFileVersionsOption(bool disable, MegaRequestListener *listener = NULL);

        /**
         * @brief Check if file versioning is enabled or disabled
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
        void getFileVersionsOption(MegaRequestListener *listener = NULL);

        /**
         * @brief Keep retrying when public key pinning fails
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
        void retrySSLerrors(bool enable);

        /**
         * @brief Enable / disable the public key pinning
         *
         * Public key pinning is enabled by default for all sensible communications.
         * It is strongly discouraged to disable this feature.
         *
         * @param enable true to keep public key pinning enabled, false to disable it
         */
        void setPublicKeyPinning(bool enable);

        /**
         * @brief Pause the reception of action packets
         *
         * This function is intended to help apps to initialize themselves
         * after the reception of nodes (MegaApi::fetchNodes) but before the reception
         * of action packets.
         *
         * For that purpose, this function can be called synchronously in the callback
         * onRequestFinish related to the fetchNodes request.
         *
         * After your initialization is finished, you can call MegaApi::resumeActionPackets
         * to start receiving external updates.
         *
         * If you forget to call MegaApi::resumeActionPackets after the usage of this function
         * the SDK won't work properly. Do not use this function for other purposes.
         */
        void pauseActionPackets();

        /**
         * @brief Resume the reception of action packets
         * @see MegaApi::pauseActionPackets
         */
        void resumeActionPackets();

	#ifdef _WIN32
		/**
		 * @brief Convert an UTF16 string to UTF8 (Windows only)
         *
         * If the conversion fails, the size of the string will be 0
         * If the input string is empty, the size of the result will be also 0
         * You can know that the conversion failed checking if the size of the input
         * is not 0 and the size of the output is zero
         *
		 * @param utf16data UTF16 buffer
		 * @param utf16size Size of the UTF16 buffer (in characters)
		 * @param utf8string Pointer to a string that will be filled with UTF8 characters
		 */
        static void utf16ToUtf8(const wchar_t* utf16data, int utf16size, std::string* utf8string);

        /**
         * @brief Convert an UTF8 string to UTF16 (Windows only)
         *
         * The converted string will always be a valid UTF16 string. It will have a trailing null byte
         * added to the string, that along with the null character of the string itself forms a valid
         * UTF16 string terminator character. Thus, it's valid to pass utf16string->data() to any function
         * accepting a UTF16 string.
         *
         * If the conversion fails, the size of the string will be 1 (null character)
         * If the input string is empty, the size of the result will be also 1 (null character)
         * You can know that the conversion failed checking if the size of the input
         * is not 0 (or NULL) and the size of the output is zero
         *
         * @param utf8data NULL-terminated UTF8 character array
         * @param utf16string Pointer to a string that will be filled with UTF16 characters
         */
        static void utf8ToUtf16(const char* utf8data, std::string* utf16string);
    #endif


        /**
         * @brief Make a name suitable for a file name in the local filesystem
         *
         * This function escapes (%xx) forbidden characters in the local filesystem if needed.
         * You can revert this operation using MegaApi::unescapeFsIncompatible
         *
         * The input string must be UTF8 encoded. The returned value will be UTF8 too.
         *
         * You take the ownership of the returned value
         *
         * @param filename Name to convert (UTF8)
         * @return Converted name (UTF8)
         */
        char* escapeFsIncompatible(const char *filename);

        /**
         * @brief Unescape a file name escaped with MegaApi::escapeFsIncompatible
         *
         * The input string must be UTF8 encoded. The returned value will be UTF8 too.
         *
         * You take the ownership of the returned value
         *
         * @param name Escaped name to convert (UTF8)
         * @return Converted name (UTF8)
         */
        char* unescapeFsIncompatible(const char* name);


        /**
         * @brief Create a thumbnail for an image
         * @param imagePath Image path
         * @param dstPath Destination path for the thumbnail (including the file name)
         * @return True if the thumbnail was successfully created, otherwise false.
         */
        bool createThumbnail(const char *imagePath, const char *dstPath);

        /**
         * @brief Create a preview for an image
         * @param imagePath Image path
         * @param dstPath Destination path for the preview (including the file name)
         * @return True if the preview was successfully created, otherwise false.
         */
        bool createPreview(const char *imagePath, const char *dstPath);

        /**
         * @brief Create an avatar from an image
         * @param imagePath Image path
         * @param dstPath Destination path for the avatar (including the file name)
         * @return True if the avatar was successfully created, otherwise false.
         */
        bool createAvatar(const char *imagePath, const char *dstPath);

        /**
         * @brief Convert a Base64 string to Base32
         *
         * If the input pointer is NULL, this function will return NULL.
         * If the input character array isn't a valid base64 string
         * the effect is undefined
         *
         * You take the ownership of the returned value
         *
         * @param base64 NULL-terminated Base64 character array
         * @return NULL-terminated Base32 character array
         */
        static char *base64ToBase32(const char *base64);

        /**
         * @brief Convert a Base32 string to Base64
         *
         * If the input pointer is NULL, this function will return NULL.
         * If the input character array isn't a valid base32 string
         * the effect is undefined
         *
         * You take the ownership of the returned value
         *
         * @param base32 NULL-terminated Base32 character array
         * @return NULL-terminated Base64 character array
         */
        static char *base32ToBase64(const char *base32);

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

        /**
         * @brief Recursively remove all local files/folders inside a local path
         * @param path Local path of a folder to start the recursive deletion
         * The folder itself is not deleted
         */
        static void removeRecursively(const char *path);

        /**
         * @brief Check if the connection with MEGA servers is OK
         *
         * It can briefly return false even if the connection is good enough when
         * some storage servers are temporarily not available or the load of API
         * servers is high.
         *
         * @return true if the connection is perfectly OK, otherwise false
         */
        bool isOnline();

#ifdef HAVE_LIBUV

        enum {
            HTTP_SERVER_DENY_ALL = -1,
            HTTP_SERVER_ALLOW_ALL = 0,
            HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1,
            HTTP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
        };

        /**
         * @brief Start an HTTP proxy server in specified port
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
        bool httpServerStart(bool localOnly = true, int port = 4443);

        /**
         * @brief Stop the HTTP proxy server
         *
         * When this function returns, the server is already shutdown.
         * If the HTTP proxy server isn't running, this functions does nothing
         */
        void httpServerStop();

        /**
         * @brief Check if the HTTP proxy server is running
         * @return 0 if the server is not running. Otherwise the port in which it's listening to
         */
        int httpServerIsRunning();

        /**
         * @brief Check if the HTTP proxy server is listening on all network interfaces
         * @return true if the HTTP proxy server is listening on 127.0.0.1 only, or it's not started.
         * If it's started and listening on all network interfaces, this function returns false
         */
        bool httpServerIsLocalOnly();

        /**
         * @brief Allow/forbid to serve files
         *
         * By default, files are served (when the server is running)
         *
         * Even if files are allowed to be served by this function, restrictions related to
         * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
         *
         * @param enable true to allow to server files, false to forbid it
         */
        void httpServerEnableFileServer(bool enable);

        /**
         * @brief Check if it's allowed to serve files
         *
         * This function can return true even if the HTTP proxy server is not running
         *
         * Even if files are allowed to be served by this function, restrictions related to
         * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
         *
         * @return true if it's allowed to serve files, otherwise false
         */
        bool httpServerIsFileServerEnabled();

        /**
         * @brief Allow/forbid to serve folders
         *
         * By default, folders are NOT served
         *
         * Even if folders are allowed to be served by this function, restrictions related to
         * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
         *
         * @param enable true to allow to server folders, false to forbid it
         */
        void httpServerEnableFolderServer(bool enable);

        /**
         * @brief Check if it's allowed to serve folders
         *
         * This function can return true even if the HTTP proxy server is not running
         *
         * Even if folders are allowed to be served by this function, restrictions related to
         * other configuration options (MegaApi::httpServerSetRestrictedMode) are still applied.
         *
         * @return true if it's allowed to serve folders, otherwise false
         */
        bool httpServerIsFolderServerEnabled();

        /**
         * @brief Enable/disable the restricted mode of the HTTP server
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
         * @param Required state for the restricted mode of the HTTP proxy server
         */
        void httpServerSetRestrictedMode(int mode);

        /**
         * @brief Check if the HTTP proxy server is working in restricted mode
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
        int httpServerGetRestrictedMode();

        /**
         * @brief Enable/disable the support for subtitles
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
        void httpServerEnableSubtitlesSupport(bool enable);

        /**
         * @brief Check if the support for subtitles is enabled
         *
         * See MegaApi::httpServerEnableSubtitlesSupport.
         *
         * This feature is disabled by default.
         *
         * @return true of the support for subtibles is enables, otherwise false
         */
        bool httpServerIsSubtitlesSupportEnabled();

        /**
         * @brief Add a listener to receive information about the HTTP proxy server
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
        void httpServerAddListener(MegaTransferListener *listener);

        /**
         * @brief Stop the reception of callbacks related to the HTTP proxy server on this listener
         * @param listener Listener that won't continue receiving information
         */
        void httpServerRemoveListener(MegaTransferListener *listener);

        /**
         * @brief Returns a URL to a node in the local HTTP proxy server
         *
         * The HTTP proxy server must be running before using this function, otherwise
         * it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @param node Node to generate the local HTTP link
         * @return URL to the node in the local HTTP proxy server, otherwise NULL
         */
        char *httpServerGetLocalLink(MegaNode *node);

        /**
         * @brief Set the maximum buffer size for the internal buffer
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
        void httpServerSetMaxBufferSize(int bufferSize);

        /**
         * @brief Get the maximum size of the internal buffer size
         *
         * See MegaApi::httpServerSetMaxBufferSize
         *
         * @return Maximum size of the internal buffer size (in bytes)
         */
        int httpServerGetMaxBufferSize();

        /**
         * @brief Set the maximum size of packets sent to clients
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
        void httpServerSetMaxOutputSize(int outputSize);

        /**
         * @brief Get the maximum size of the packets sent to clients
         *
         * See MegaApi::httpServerSetMaxOutputSize
         *
         * @return Maximum size of the packets sent to clients (in bytes)
         */
        int httpServerGetMaxOutputSize();

        /**
         * @brief Get the MIME type associated with the extension
         *
         * You take the ownership of the returned value
         *
         * @param extension File extension (with or without a leading dot)
         * @return MIME type associated with the extension
         */
        static char *getMimeType(const char* extension);
#endif

#ifdef ENABLE_CHAT
        /**
         * @brief Creates a chat for one or more participants, allowing you to specify their
         * permissions and if the chat should be a group chat or not (when it is just for 2 participants).
         *
         * There are two types of chat: permanent an group. A permanent chat is between two people, and
         * participants can not leave it. It's also called 1on1 or 1:1.
         *
         * The creator of the chat will have moderator level privilege and should not be included in the
         * list of peers.
         *
         * On 1:1 chats, the other participant has also moderator level privilege, regardless the
         * privilege level specified.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_CREATE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFlag - Returns if the new chat is a group chat or permanent chat
         * - MegaRequest::getMegaTextChatPeerList - List of participants and their privilege level
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaTextChatList - Returns the new chat's information
         *
         * @note If you are trying to create a chat with more than 1 other person, then it will be forced
         * to be a group chat.
         *
         * @note If peers list contains only one person, group chat is not set and a permament chat already
         * exists with that person, then this call will return the information for the existing chat, rather
         * than a new chat.
         *
         * @param group Flag to indicate if the chat is a group chat or not
         * @param peers MegaTextChatPeerList including other users and their privilege level
         * @param listener MegaRequestListener to track this request
         */
        void createChat(bool group, MegaTextChatPeerList *peers, MegaRequestListener *listener = NULL);

        /**
         * @brief Adds a user to an existing chat. To do this you must have the
         * operator privilege in the chat, and the chat must be a group chat.
         *
         * In case the chat has a title already set, the title must be encrypted for the new
         * peer and passed to this function. Note that only participants with privilege level
         * MegaTextChatPeerList::PRIV_MODERATOR are allowed to set the title of a chat.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_INVITE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the MegaHandle of the user to be invited
         * - MegaRequest::getAccess - Returns the privilege level wanted for the user
         * - MegaRequest::getText - Returns the title of the chat.
         *
         * On the onTransferFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EACCESS - If the logged in user doesn't have privileges to invite peers.
         * - MegaError::API_EARGS - If there's a title and it's not Base64url encoded.

         * @param chatid MegaHandle that identifies the chat room
         * @param uh MegaHandle that identifies the user
         * @param privilege Privilege level for the new peers. Valid values are:
         * - MegaTextChatPeerList::PRIV_UNKNOWN = -2
         * - MegaTextChatPeerList::PRIV_RM = -1
         * - MegaTextChatPeerList::PRIV_RO = 0
         * - MegaTextChatPeerList::PRIV_STANDARD = 2
         * - MegaTextChatPeerList::PRIV_MODERATOR = 3
         * @param title Byte array representing the title that wants to be set, already encrypted and
         * converted to Base64url encoding (optional).
         * @param listener MegaRequestListener to track this request
         */
        void inviteToChat(MegaHandle chatid, MegaHandle uh, int privilege, const char *title = NULL, MegaRequestListener *listener = NULL);

        /**
         * @brief Remove yourself or another user from a chat. To remove a user other than
         * yourself you need to have the operator privilege. Only a group chat may be left.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_REMOVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the MegaHandle of the user to be removed
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param uh MegaHandle that identifies the user. If not provided (INVALID_HANDLE), the requester is removed
         * @param listener MegaRequestListener to track this request
         */
        void removeFromChat(MegaHandle chatid, MegaHandle uh = INVALID_HANDLE, MegaRequestListener *listener = NULL);

        /**
         * @brief Get your current, user-specific url to connect to chatd with
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_URL
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getLink - Returns the user-specific URL for the chat
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param listener MegaRequestListener to track this request
         */
        void getUrlChat(MegaHandle chatid, MegaRequestListener *listener = NULL);

        /**
         * @brief Grants another user access to download a file using MegaApi::startDownload like
         * a user would do so for their own file, rather than a public link.
         *
         * Currently, this method only supports files, not folders.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_GRANT_ACCESS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the node handle
         * - MegaRequest::getParentHandle - Returns the chat identifier
         * - MegaRequest::getEmail - Returns the MegaHandle of the user in Base64 enconding
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param n MegaNode that wants to be shared
         * @param uh MegaHandle that identifies the user
         * @param listener MegaRequestListener to track this request
         */
        void grantAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener = NULL);

        /**
         * @brief Removes access to a node from a user you previously granted access to.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_REMOVE_ACCESS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the node handle
         * - MegaRequest::getParentHandle - Returns the chat identifier
         * - MegaRequest::getEmail - Returns the MegaHandle of the user in Base64 enconding
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param n MegaNode whose access wants to be revokesd
         * @param uh MegaHandle that identifies the user
         * @param listener MegaRequestListener to track this request
         */
        void removeAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener = NULL);

        /**
         * @brief Allows a logged in operator/moderator to adjust the permissions on any other user
         * in their group chat. This does not work for a 1:1 chat.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the MegaHandle of the user whose permission
         * is to be upgraded
         * - MegaRequest::getAccess - Returns the privilege level wanted for the user
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param uh MegaHandle that identifies the user
         * @param privilege Privilege level for the existing peer. Valid values are:
         * - MegaTextChatPeerList::PRIV_RO = 0
         * - MegaTextChatPeerList::PRIV_STANDARD = 2
         * - MegaTextChatPeerList::PRIV_MODERATOR = 3
         * @param listener MegaRequestListener to track this request
         */
        void updateChatPermissions(MegaHandle chatid, MegaHandle uh, int privilege, MegaRequestListener *listener = NULL);

        /**
         * @brief Allows a logged in operator/moderator to truncate their chat, i.e. to clear
         * the entire chat history up to a certain message. All earlier messages are wiped,
         * but his specific message gets overridden with an API message.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_TRUNCATE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the message identifier to truncate from.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param messageid MegaHandle that identifies the message to truncate from
         * @param listener MegaRequestListener to track this request
         */
        void truncateChat(MegaHandle chatid, MegaHandle messageid, MegaRequestListener *listener = NULL);


        /**
         * @brief Allows to set the title of a chat
         *
         * Only participants with privilege level MegaTextChatPeerList::PRIV_MODERATOR are allowed to
         * set the title of a chat.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_SET_TITLE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - Returns the title of the chat.
         *
         * On the onTransferFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EACCESS - If the logged in user doesn't have privileges to invite peers.
         * - MegaError::API_EARGS - If there's a title and it's not Base64url encoded.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param title Byte array representing the title that wants to be set, already encrypted and
         * converted to Base64url encoding.
         * @param listener MegaRequestListener to track this request
         */
        void setChatTitle(MegaHandle chatid, const char *title, MegaRequestListener *listener = NULL);

        /**
         * @brief Get your current URL to connect to the presence server
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_PRESENCE_URL
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getLink - Returns the user-specific URL for the chat presence server
         *
         * @param listener MegaRequestListener to track this request
         */
        void getChatPresenceURL(MegaRequestListener *listener = NULL);

        /**
         * @brief Register a token for push notifications
         *
         * This function attach a token to the current session, which is intended to get push notifications
         * on mobile platforms like Android and iOS.
         *
         * The push notification mechanism is platform-dependent. Hence, the app should indicate the
         * type of push notification to be registered. Currently, the different types are:
         *  - MegaApi::PUSH_NOTIFICATION_ANDROID    = 1
         *  - MegaApi::PUSH_NOTIFICATION_IOS_VOIP   = 2
         *  - MegaApi::PUSH_NOTIFICATION_IOS_STD    = 3
         *
         * The associated request type with this request is MegaRequest::TYPE_REGISTER_PUSH_NOTIFICATION
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - Returns the token provided.
         * - MegaRequest::getNumber - Returns the device type provided.
         *
         * @param deviceType Type of notification to be registered.
         * @param token Character array representing the token to be registered.
         * @param listener MegaRequestListener to track this request
         */
        void registerPushNotifications(int deviceType, const char *token, MegaRequestListener *listener = NULL);

        /**
         * @brief Send data related to MEGAchat to the stats server
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_STATS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the data provided.
         * - MegaRequest::getParamType - Returns number 1
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Return the HTTP status code from the stats server
         * - MegaRequest::getText - Returns the JSON response from the stats server
         * - MegaRequest::getTotalBytes - Returns the number of bytes in the response
         *
         * @param data JSON data to send to the stats server
         * @param listener MegaRequestListener to track this request
         */
        void sendChatStats(const char *data, MegaRequestListener *listener = NULL);

        /**
         * @brief Send logs related to MEGAchat to the logs server
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_STATS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the data provided.
         * - MegaRequest::getSessionKey - Returns the aid provided
         * - MegaRequest::getParamType - Returns number 2
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Return the HTTP status code from the stats server
         * - MegaRequest::getText - Returns the JSON response from the stats server
         * - MegaRequest::getTotalBytes - Returns the number of bytes in the response
         *
         * @param data JSON data to send to the logs server
         * @param aid User's anonymous identifier for logging
         * @param listener MegaRequestListener to track this request
         */
        void sendChatLogs(const char *data, const char *aid, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the list of chatrooms for this account
         *
         * You take the ownership of the returned value
         *
         * @return A list of MegaTextChat objects with detailed information about each chatroom.
         */
        MegaTextChatList *getChatList();

        /**
         * @brief Get the list of users with access to the specified node
         *
         * You take the ownership of the returned value
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param h MegaNode to check the access
         *
         * @return A list of user handles that have access to the node
         */
        MegaHandleList *getAttachmentAccess(MegaHandle chatid, MegaHandle h);

        /**
         * @brief Check if the logged-in user has access to the specified node
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param h MegaHandle that identifies the node to check the access
         * @param uh MegaHandle that identifies the user to check the access
         *
         * @return True the user has access to the node in that chat. Otherwise, it returns false
         */
        bool hasAccessToAttachment(MegaHandle chatid, MegaHandle h, MegaHandle uh);

        /**
         * @brief Get files attributes from a node
         * You take the ownership of the returned value
         * @param handle handle from node
         * @return char array with files attributes from the node.
         */
        const char* getFileAttribute(MegaHandle h);
#endif

        /**
         * @brief Get the MEGA Achievements of the account logged in
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ACHIEVEMENTS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFlag - Always false
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaAchievementsDetails - Details of the MEGA Achievements of this account
         *
         * @param listener MegaRequestListener to track this request
         */
        void getAccountAchievements(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the list of existing MEGA Achievements
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
         * @param listener MegaRequestListener to track this request
         */
        void getMegaAchievements(MegaRequestListener *listener = NULL);

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
 * @brief Details about a MEGA balance
 */
class MegaAccountBalance
{
public:
    virtual ~MegaAccountBalance();

    /**
     * @brief Get the amount of the balance
     * @return Amount
     */
    virtual double getAmount() const;

    /**
     * @brief Get the currency of the amount
     *
     * You take the ownership of the returned value
     *
     * @return Currency of the amount
     */
    virtual char *getCurrency() const;
};

/**
 * @brief Details about a MEGA session
 */
class MegaAccountSession
{
public:
    virtual ~MegaAccountSession();

    /**
     * @brief Get the creation date of the session
     *
     * In seconds since the Epoch
     *
     * @return Creation date of the session
     */
    virtual int64_t getCreationTimestamp() const;

    /**
     * @brief Get the timestamp of the most recent usage of the session
     * @return Timestamp of the most recent usage of the session (in seconds since the Epoch)
     */
    virtual int64_t getMostRecentUsage() const;

    /**
     * @brief Get the User-Agent of the client that created the session
     *
     * You take the ownership of the returned value
     *
     * @return User-Agent of the creator of the session
     */
    virtual char *getUserAgent() const;

    /**
     * @brief Get the IP address of the client that created the session
     *
     * You take the ownership of the returned value
     *
     * @return IP address of the creator of the session
     */
    virtual char *getIP() const;

    /**
     * @brief Get the country of the client that created the session
     *
     * You take the ownership of the returned value
     *
     * @return Country of the creator of the session
     */
    virtual char *getCountry() const;

    /**
     * @brief Retuns true if the session is the current one
     * @return True if the session is the current one. Otherwise false.
     */
    virtual bool isCurrent() const;

    /**
     * @brief Get the state of the session
     * @return True if the session is alive, false otherwise
     */
    virtual bool isAlive() const;

    /**
     * @brief Get the handle of the session
     * @return Handle of the session
     */
    virtual MegaHandle getHandle() const;
};

/**
 * @brief Details about a MEGA purchase
 */
class MegaAccountPurchase
{
public:
    virtual ~MegaAccountPurchase();

    /**
     * @brief Get the timestamp of the purchase
     * @return Timestamp of the purchase (in seconds since the Epoch)
     */
    virtual int64_t getTimestamp() const;

    /**
     * @brief Get the handle of the purchase
     *
     * You take the ownership of the returned value
     *
     * @return Handle of the purchase
     */
    virtual char *getHandle() const;

    /**
     * @brief Get the currency of the purchase
     *
     * You take the ownership of the returned value
     *
     * @return Currency of the purchase
     */
    virtual char* getCurrency() const;

    /**
     * @brief Get the amount of the purchase
     * @return Amount of the purchase
     */
    virtual double getAmount() const;

    /**
     * @brief Get the method of the purchase
     *
     * These are the valid methods:
     * - MegaApi::PAYMENT_METHOD_BALANCE = 0,
     * - MegaApi::PAYMENT_METHOD_PAYPAL = 1,
     * - MegaApi::PAYMENT_METHOD_ITUNES = 2,
     * - MegaApi::PAYMENT_METHOD_GOOGLE_WALLET = 3,
     * - MegaApi::PAYMENT_METHOD_BITCOIN = 4,
     * - MegaApi::PAYMENT_METHOD_UNIONPAY = 5,
     * - MegaApi::PAYMENT_METHOD_FORTUMO = 6,
     * - MegaApi::PAYMENT_METHOD_CREDIT_CARD = 8
     * - MegaApi::PAYMENT_METHOD_CENTILI = 9
     * - MegaApi::PAYMENT_METHOD_WINDOWS_STORE = 13
     *
     * @return Method of the purchase
     */
    virtual int getMethod() const;
};

/**
 * @brief Details about a MEGA transaction
 */
class MegaAccountTransaction
{
public:
    virtual ~MegaAccountTransaction();

    /**
     * @brief Get the timestamp of the transaction
     * @return Timestamp of the transaction (in seconds since the Epoch)
     */
    virtual int64_t getTimestamp() const;

    /**
     * @brief Get the handle of the transaction
     *
     * You take the ownership of the returned value
     *
     * @return Handle of the transaction
     */
    virtual char *getHandle() const;

    /**
     * @brief Get the currency of the transaction
     *
     * You take the ownership of the returned value
     *
     * @return Currency of the transaction
     */
    virtual char* getCurrency() const;

    /**
     * @brief Get the amount of the transaction
     * @return Amount of the transaction
     */
    virtual double getAmount() const;
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
        ACCOUNT_TYPE_PROIII = 3,
        ACCOUNT_TYPE_LITE = 4
    };

    enum
    {
        SUBSCRIPTION_STATUS_NONE = 0,
        SUBSCRIPTION_STATUS_VALID = 1,
        SUBSCRIPTION_STATUS_INVALID = 2
    };

    virtual ~MegaAccountDetails();
    /**
     * @brief Get the PRO level of the MEGA account
     * @return PRO level of the MEGA account.
     * Valid values are:
     * - MegaAccountDetails::ACCOUNT_TYPE_FREE = 0
     * - MegaAccountDetails::ACCOUNT_TYPE_PROI = 1
     * - MegaAccountDetails::ACCOUNT_TYPE_PROII = 2
     * - MegaAccountDetails::ACCOUNT_TYPE_PROIII = 3
     * - MegaAccountDetails::ACCOUNT_TYPE_LITE = 4
     */
    virtual int getProLevel();

    /**
     * @brief Get the expiration time for the current PRO status
     * @return Expiration time for the current PRO status (in seconds since the Epoch)
     */
    virtual int64_t getProExpiration();

    /**
     * @brief Check if there is a valid subscription
     *
     * If this function returns MegaAccountDetails::SUBSCRIPTION_STATUS_VALID,
     * the PRO account will be automatically renewed.
     * See MegaAccountDetails::getSubscriptionRenewTime
     *
     * @return Information about about the subscription status
     *
     * Valid return values are:
     * - MegaAccountDetails::SUBSCRIPTION_STATUS_NONE = 0
     * There isn't any active subscription
     *
     * - MegaAccountDetails::SUBSCRIPTION_STATUS_VALID = 1
     * There is an active subscription
     *
     * - MegaAccountDetails::SUBSCRIPTION_STATUS_INVALID = 2
     * A subscription exists, but it uses a payment gateway that is no longer valid
     *
     */
    virtual int getSubscriptionStatus();

    /**
     * @brief Get the time when the the PRO account will be renewed
     * @return Renewal time (in seconds since the Epoch)
     */
    virtual int64_t getSubscriptionRenewTime();

    /**
     * @brief Get the subscryption method
     *
     * You take the ownership of the returned value
     *
     * @return Subscription method. For example "Credit Card".
     */
    virtual char* getSubscriptionMethod();

    /**
     * @brief Get the subscription cycle
     *
     * The return value will show if the subscription will be montly or yearly renewed.
     * Example return values: "1 M", "1 Y".
     *
     * @return Subscription cycle
     */
    virtual char* getSubscriptionCycle();

    /**
     * @brief Get the maximum storage for the account (in bytes)
     * @return Maximum storage for the account (in bytes)
     */
    virtual long long getStorageMax();

    /**
     * @brief Get the used storage
     * @return Used storage (in bytes)
     */
    virtual long long getStorageUsed();

    /**
     * @brief Get the used storage by versions
     * @return Used storage by versions (in bytes)
     */
    virtual long long getVersionStorageUsed();

    /**
     * @brief Get the maximum available bandwidth for the account
     * @return Maximum available bandwidth (in bytes)
     */
    virtual long long getTransferMax();

    /**
     * @brief Get the used bandwidth
     * @return Used bandwidth (in bytes)
     */
    virtual long long getTransferOwnUsed();

    /**
     * @brief Returns the number of nodes with account usage info
     *
     * You can get information about each node using MegaAccountDetails::getStorageUsed,
     * MegaAccountDetails::getNumFiles, MegaAccountDetails::getNumFolders
     *
     * This function can return:
     * - 0 (no info about any node)
     * - 3 (info about the root node, the inbox node and the rubbish node)
     * Use MegaApi::getRootNode MegaApi::getInboxNode and MegaApi::getRubbishNode to get those nodes.
     *
     * - >3 (info about root, inbox, rubbish and incoming shares)
     * Use MegaApi::getInShares to get the incoming shares
     *
     * @return Number of items with account usage info
     */
    virtual int getNumUsageItems();

    /**
     * @brief Get the used storage in for a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Used storage (in bytes)
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getStorageUsed(MegaHandle handle);

    /**
     * @brief Get the number of files in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of files in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getNumFiles(MegaHandle handle);

    /**
     * @brief Get the number of folders in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of folders in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getNumFolders(MegaHandle handle);

    /**
     * @brief Get the used storage by versions in for a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Used storage by versions (in bytes)
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getVersionStorageUsed(MegaHandle handle);

    /**
     * @brief Get the number of versioned files in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of versioned files in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getInboxNode
     */
    virtual long long getNumVersionFiles(MegaHandle handle);

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
    virtual MegaAccountDetails* copy();

    /**
     * @brief Get the number of MegaAccountBalance objects associated with the account
     *
     * You can use MegaAccountDetails::getBalance to get those objects.
     *
     * @return Number of MegaAccountBalance objects
     */
    virtual int getNumBalances() const;

    /**
     * @brief Returns the MegaAccountBalance object associated with an index
     *
     * You take the ownership of the returned value
     *
     * @param i Index of the object
     * @return MegaAccountBalance object
     */
    virtual MegaAccountBalance* getBalance(int i) const;

    /**
     * @brief Get the number of MegaAccountSession objects associated with the account
     *
     * You can use MegaAccountDetails::getSession to get those objects.
     *
     * @return Number of MegaAccountSession objects
     */
    virtual int getNumSessions() const;

    /**
     * @brief Returns the MegaAccountSession object associated with an index
     *
     * You take the ownership of the returned value
     *
     * @param i Index of the object
     * @return MegaAccountSession object
     */
    virtual MegaAccountSession* getSession(int i) const;

    /**
     * @brief Get the number of MegaAccountPurchase objects associated with the account
     *
     * You can use MegaAccountDetails::getPurchase to get those objects.
     *
     * @return Number of MegaAccountPurchase objects
     */
    virtual int getNumPurchases() const;

    /**
     * @brief Returns the MegaAccountPurchase object associated with an index
     *
     * You take the ownership of the returned value
     *
     * @param i Index of the object
     * @return MegaAccountPurchase object
     */
    virtual MegaAccountPurchase* getPurchase(int i) const;

    /**
     * @brief Get the number of MegaAccountTransaction objects associated with the account
     *
     * You can use MegaAccountDetails::getTransaction to get those objects.
     *
     * @return Number of MegaAccountTransaction objects
     */
    virtual int getNumTransactions() const;

    /**
     * @brief Returns the MegaAccountTransaction object associated with an index
     *
     * You take the ownership of the returned value
     *
     * @param i Index of the object
     * @return MegaAccountTransaction object
     */
    virtual MegaAccountTransaction* getTransaction(int i) const;

    /**
     * @brief Get the number of hours that are taken into account to calculate the free bandwidth quota
     *
     * The number of bytes transferred in that time is provided using MegaAccountDetails::getTemporalBandwidth
     *
     * @return Number of hours taken into account to calculate the free bandwidth quota
     */
    virtual int getTemporalBandwidthInterval();

    /**
     * @brief Get the number of bytes that were recently transferred
     *
     * The time interval in which those bytes were transferred
     * is provided (in hours) using MegaAccountDetails::getTemporalBandwidthInterval
     *
     * @return Number of bytes that were recently transferred
     */
    virtual long long getTemporalBandwidth();

    /**
     * @brief Check if the temporal bandwidth usage is valid after an overquota error
     * @return True if the temporal bandwidth is valid, otherwise false
     */
    virtual bool isTemporalBandwidthValid();
};

/**
 * @brief Details about pricing plans
 *
 * Use MegaApi::getPricing to get the pricing plans to upgrade MEGA accounts
 */
class MegaPricing
{
public:
    virtual ~MegaPricing();

    /**
     * @brief Get the number of available products to upgrade the account
     * @return Number of available products
     */
    virtual int getNumProducts();

    /**
     * @brief Get the handle of a product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Handle of the product
     * @see MegaApi::getPaymentId
     */
    virtual MegaHandle getHandle(int productIndex);

    /**
     * @brief Get the PRO level associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return PRO level associated with the product:
     * Valid values are:
     * - MegaAccountDetails::ACCOUNT_TYPE_FREE = 0
     * - MegaAccountDetails::ACCOUNT_TYPE_PROI = 1
     * - MegaAccountDetails::ACCOUNT_TYPE_PROII = 2
     * - MegaAccountDetails::ACCOUNT_TYPE_PROIII = 3
     * - MegaAccountDetails::ACCOUNT_TYPE_LITE = 4
     */
    virtual int getProLevel(int productIndex);

    /**
     * @brief Get the number of GB of storage associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of storage
     */
    virtual unsigned int getGBStorage(int productIndex);

    /**
     * @brief Get the number of GB of bandwidth associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of bandwidth
     */
    virtual unsigned int getGBTransfer(int productIndex);

    /**
     * @brief Get the duration of the product (in months)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Duration of the product (in months)
     */
    virtual int getMonths(int productIndex);

    /**
     * @brief getAmount Get the price of the product (in cents)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Price of the product (in cents)
     */
    virtual int getAmount(int productIndex);

    /**
     * @brief Get the currency associated with MegaPricing::getAmount
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Currency associated with MegaPricing::getAmount
     */
    virtual const char* getCurrency(int productIndex);

    /**
     * @brief Get a description of the product
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Description of the product
     */
    virtual const char* getDescription(int productIndex);

    /**
     * @brief getIosID Get the iOS ID of the product
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return iOS ID of the product
     */
    virtual const char* getIosID(int productIndex);

    /**
     * @brief Get the Android ID of the product
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Android ID of the product
     */
    virtual const char* getAndroidID(int productIndex);

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
    virtual MegaPricing *copy();
};

/**
 * @brief The MegaAchievementsDetails class
 *
 * There are several MEGA Achievements that a user can unlock, resulting in a
 * temporary extension of the storage and/or transfer quota during a period of
 * time.
 *
 * Currently there are 4 different classes of MEGA Achievements:
 *
 *  - Welcome: Create your free account and get 35 GB of complimentary storage space,
 *      valid for 30 days.
 *
 *  - Invite: Invite as many friends or coworkers as you want. For every signup under the
 *      invited email address, you will receive 10 GB of complimentary storage plus 20 GB
 *      of transfer quota, both valid for 365 days, provided that the new user installs
 *      either MEGAsync or a mobile app and starts using MEGA.
 *
 *  - Desktop install: When you install MEGAsync you get 20 GB of complimentary storage
 *      space plus 40 GB of transfer quota, both valid for 180 days.
 *
 *  - Mobile install: When you install our mobile app you get 15 GB of complimentary
 *      storage space plus 30 GB transfer quota, both valid for 180 days.
 *
 * When the user unlocks one of the achievements above, it unlocks an "Award". The award
 * includes a timestamps to indicate when it was unlocked, plus an expiration timestamp.
 * Afterwards, the award will not be active. Additionally, each award results in a "Reward".
 * The reward is linked to the corresponding award and includes the storage and transfer
 * quota obtained thanks to the unlocked award.
 *
 * @note It may take 2-3 days for achievements to show on the account after they have been completed.
 */
class MegaAchievementsDetails
{
public:

    enum {
        MEGA_ACHIEVEMENT_WELCOME            = 1,
        MEGA_ACHIEVEMENT_INVITE             = 3,
        MEGA_ACHIEVEMENT_DESKTOP_INSTALL    = 4,
        MEGA_ACHIEVEMENT_MOBILE_INSTALL     = 5
    };

    virtual ~MegaAchievementsDetails();

    /**
     * @brief Get the base storage value for this account
     * @return The base storage value, in bytes
     */
    virtual long long getBaseStorage();

    /**
     * @brief Get the storage granted by a MEGA achievement class
     *
     * The following classes are valid:
     *  - MEGA_ACHIEVEMENT_WELCOME = 1
     *  - MEGA_ACHIEVEMENT_INVITE = 3
     *  - MEGA_ACHIEVEMENT_DESKTOP_INSTALL = 4
     *  - MEGA_ACHIEVEMENT_MOBILE_INSTALL = 5
     *
     * @param class_id Id of the MEGA achievement
     * @return Storage granted by this MEGA achievement class, in bytes
     */
    virtual long long getClassStorage(int class_id);

    /**
     * @brief Get the transfer quota granted by a MEGA achievement class
     *
     * The following classes are valid:
     *  - MEGA_ACHIEVEMENT_WELCOME = 1
     *  - MEGA_ACHIEVEMENT_INVITE = 3
     *  - MEGA_ACHIEVEMENT_DESKTOP_INSTALL = 4
     *  - MEGA_ACHIEVEMENT_MOBILE_INSTALL = 5
     *
     * @param class_id Id of the MEGA achievement
     * @return Transfer quota granted by this MEGA achievement class, in bytes
     */
    virtual long long getClassTransfer(int class_id);

    /**
     * @brief Get the duration of storage/transfer quota granted by a MEGA achievement class
     *
     * The following classes are valid:
     *  - MEGA_ACHIEVEMENT_WELCOME = 1
     *  - MEGA_ACHIEVEMENT_INVITE = 3
     *  - MEGA_ACHIEVEMENT_DESKTOP_INSTALL = 4
     *  - MEGA_ACHIEVEMENT_MOBILE_INSTALL = 5
     *
     * The storage and transfer quota resulting from a MEGA achievement may expire after
     * certain number of days. In example, the "Welcome" reward lasts for 30 days and afterwards
     * the granted storage and transfer quota is revoked.
     *
     * @param class_id Id of the MEGA achievement
     * @return Number of days for the storage/transfer quota granted by this MEGA achievement class
     */
    virtual int getClassExpire(int class_id);

    /**
     * @brief Get the number of unlocked awards for this account
     * @return Number of unlocked awards
     */
    virtual unsigned int getAwardsCount();

    /**
     * @brief Get the MEGA achievement class of the award
     * @param index Position of the award in the list of unlocked awards
     * @return The achievement class associated to the award in position \c index
     */
    virtual int getAwardClass(unsigned int index);

    /**
     * @brief Get the id of the award
     * @param index Position of the award in the list of unlocked awards
     * @return The id of the award in position \c index
     */
    virtual int getAwardId(unsigned int index);

    /**
     * @brief Get the timestamp of the award (when it was unlocked)
     * @param index Position of the award in the list of unlocked awards
     * @return The timestamp of the award (when it was unlocked) in position \c index
     */
    virtual int64_t getAwardTimestamp(unsigned int index);

    /**
     * @brief Get the expiration timestamp of the award
     *
     * After this moment, the storage and transfer quota granted as result of the award
     * will not be valid anymore.
     *
     * @note The expiration time may not be the \c getAwardTimestamp plus the number of days
     * returned by \c getClassExpire, since the award can be unlocked but not yet granted. It
     * typically takes 2 days from unlocking the award until the user is actually rewarded.
     *
     * @param index Position of the award in the list of unlocked awards
     * @return The expiration timestamp of the award in position \c index
     */
    virtual int64_t getAwardExpirationTs(unsigned int index);

    /**
     * @brief Get the list of referred emails for the award
     *
     * This function is specific for the achievements of class MEGA_ACHIEVEMENT_INVITE.
     *
     * You take ownership of the returned value.
     *
     * @param index Position of the award in the list of unlocked awards
     * @return The list of invited emails for the award in position \c index
     */
    virtual MegaStringList* getAwardEmails(unsigned int index);

    /**
     * @brief Get the number of active rewards for this account
     * @return Number of active rewards
     */
    virtual int getRewardsCount();
    
    /**
     * @brief Get the id of the award associated with the reward
     * @param index Position of the reward in the list of active rewards
     * @return The id of the award associated with the reward
     */
    virtual int getRewardAwardId(unsigned int index);
    
    /**
     * @brief Get the storage rewarded by the award
     * @param index Position of the reward in the list of active rewards
     * @return The storage rewarded by the award
     */
    virtual long long getRewardStorage(unsigned int index);
    
    /**
     * @brief Get the transfer quota rewarded by the award
     * @param index Position of the reward in the list of active rewards
     * @return The transfer quota rewarded by the award
     */
    virtual long long getRewardTransfer(unsigned int index);
    
    /**
     * @brief Get the storage rewarded by the award_id
     * @param award_id The id of the award
     * @return The storage rewarded by the award_id
     */
    virtual long long getRewardStorageByAwardId(int award_id);
    
    /**
     * @brief Get the transfer rewarded by the award_id
     * @param award_id The id of the award
     * @return The transfer rewarded by the award_id
     */
    virtual long long getRewardTransferByAwardId(int award_id);
    
    /**
     * @brief Get the duration of the reward
     * @param index Position of the reward in the list of active rewards
     * @return The duration of the reward, in days
     */
    virtual int getRewardExpire(unsigned int index);
    
    /**
     * @brief Creates a copy of this MegaAchievementsDetails object.
     *
     * The resulting object is fully independent of the source MegaAchievementsDetails,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaAchievementsDetails object
     */
    virtual MegaAchievementsDetails *copy();

    /**
     * @brief Returns the actual storage achieved by this account
     *
     * This function considers all the storage granted to the logged in
     * account as result of the unlocked achievements. It does not consider
     * the expired achievements nor the permanent base storage.
     *
     * @return The achieved storage for this account
     */
    virtual long long currentStorage();

    /**
     * @brief Returns the actual transfer quota achieved by this account
     *
     * This function considers all the transfer quota granted to the logged
     * in account as result of the unlocked achievements. It does not consider
     * the expired achievements.
     *
     * @return The achieved transfer quota for this account
     */
    virtual long long currentTransfer();

    /**
     * @brief Returns the actual achieved storage due to referrals
     *
     * This function considers all the storage granted to the logged in account
     * as result of the successful invitations (referrals). It does not consider
     * the expired achievements.
     *
     * @return The achieved storage by this account as result of referrals
     */
    virtual long long currentStorageReferrals();

    /**
     * @brief Returns the actual achieved transfer quota due to referrals
     *
     * This function considers all the transfer quota granted to the logged
     * in account as result of the successful invitations (referrals). It
     * does not consider the expired achievements.
     *
     * @return The transfer achieved quota by this account as result of referrals
     */
    virtual long long currentTransferReferrals();
};

}

#endif //MEGAAPI_H
