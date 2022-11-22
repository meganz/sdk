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
class MegaScheduledCopyListener;
class MegaGlobalListener;
class MegaTreeProcessor;
class MegaAccountDetails;
class MegaAchievementsDetails;
class MegaPricing;
class MegaCurrency;
class MegaNode;
class MegaUser;
class MegaUserAlert;
class MegaContactRequest;
class MegaShare;
class MegaError;
class MegaRequest;
class MegaEvent;
class MegaTransfer;
class MegaScheduledCopy;
class MegaSync;
class MegaStringList;
class MegaNodeList;
class MegaUserList;
class MegaUserAlertList;
class MegaContactRequestList;
class MegaShareList;
class MegaTransferList;
class MegaFolderInfo;
class MegaTimeZoneDetails;
class MegaPushNotificationSettings;
class MegaBackgroundMediaUpload;
class MegaCancelToken;
class MegaApi;
class MegaSemaphore;

#if defined(SWIG)
    #define MEGA_DEPRECATED
#elif defined(WIN32)
    #define MEGA_DEPRECATED [[deprecated]]
#else
    #define MEGA_DEPRECATED
#endif

/**
 * @brief
 * Interface to receive filename anomaly notifications from the SDK.
 *
 * @see MegaApi::setFilenameAnomalyReporter
 */
class MegaFilenameAnomalyReporter
{
public:
    /**
     * @brief
     * Represents the type of anomaly reported by the SDK.
     */
    enum AnomalyType
    {
        /**
         * @brief
         * A file's local and remote names differ.
         *
         * An example of when this kind of anomaly can occur is when
         * downloading a file from the cloud that contains characters in its
         * name that are not valid on the local filesystem.
         *
         * Say, downloading a file called A:B on Windows.
         */
        ANOMALY_NAME_MISMATCH = 0,

        /**
         * @brief
         * A file has a reserved name.
         *
         * This kind of anomaly is reported by the SDK when it attempts to
         * download a file that has a name that is reserved on the local
         * filesystem.
         *
         * Say, downloading a file called CON on Windows.
         */
        ANOMALY_NAME_RESERVED = 1
    }; // AnomalyType

    virtual ~MegaFilenameAnomalyReporter() { };

    /**
     * @brief
     * Called by the SDK when it wants to report a filename anomaly.
     *
     * @param type
     * The anomaly that was detected by the SDK.
     *
     * @param localPath
     * The local path of the file with a filename anomaly.
     *
     * @param remotePath
     * The remote path of the file with a filename anomaly.
     */
    virtual void anomalyDetected(AnomalyType type, const char* localPath, const char* remotePath) = 0;
}; // MegaFilenameAnomalyReporter

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

    /**
    * @brief Indicate which file extensions (file/image types) are supported
    *
    * Return a string with all the supported extensions concatenated, with . separating
    * Make sure to include a trailing .   eg.  ".jpg.png.bmp.jpeg."
    *
    * The caller does not take ownership of the string.
    *
    * If not supplied, all relevant files will be attempted.
    */
    virtual const char* supportedImageFormats();

    /**
    * @brief Indicate which file extensions (file/image types) are supported
    *
    * Return a string with all the supported extensions concatenated, with . separating
    * Make sure to include a trailing .   eg.  ".mpeg.mp4.avi.mkv."
    *
    * The caller does not take ownership of the string.
    *
    * If not supplied, all relevant files will be attempted.
    */
    virtual const char* supportedVideoFormats();

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
     * @param directMessages: in ENABLE_LOG_PERFORMANCE MODE, this will indicate the logger that an array of const char* should
     * be written in the logs immediately without buffering the output. message can be discarded in that case.
     *
     * @param directMessagesSizes: size of the previous const char *.
     *
     */
    virtual void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
                     , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, int numberMessages = 0
#endif
                     );
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
            TYPE_UNKNOWN    = -1,
            TYPE_FILE       = 0,
            TYPE_FOLDER     = 1,
            TYPE_ROOT       = 2,
            TYPE_VAULT      = 3,
            TYPE_INCOMING   = TYPE_VAULT,    // kept for backwards-compatibility (renamed to Vault)
            TYPE_RUBBISH    = 4
		};

        enum {
            NODE_LBL_UNKNOWN = 0,
            NODE_LBL_RED,
            NODE_LBL_ORANGE,
            NODE_LBL_YELLOW,
            NODE_LBL_GREEN,
            NODE_LBL_BLUE,
            NODE_LBL_PURPLE,
            NODE_LBL_GREY,
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
            CHANGE_TYPE_PUBLIC_LINK     = 0x200,
            CHANGE_TYPE_NEW             = 0x400,
            CHANGE_TYPE_NAME            = 0x800,
            CHANGE_TYPE_FAVOURITE       = 0x1000,
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
         * - TYPE_VAULT = 3
         * The MegaNode object represents root of the MEGA Vault
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
         * @brief Returns the original fingerprint (Base64-encoded) of the node
         *
         * In the case where a file was modified before uploaded (eg. resized photo or gps coords removed),
         * it may have an original fingerprint set (by MegaApi::setOriginalFingerprint or
         * MegaApi::backgroundMediaUploadComplete), which is the fingerprint of the file before it was modified.
         * This can be useful on mobile devices to avoid uploading a file multiple times when only
         * the original file is kept on the device.
         *
         * The MegaNode object retains the ownership of the returned string. It will
         * be valid until the MegaNode object is deleted.
         *
         * @return Base64-encoded original fingerprint of the node, or NULL it the node doesn't have an original fingerprint.
         */
        virtual const char* getOriginalFingerprint();

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
         * @brief Get the attribute of the node representing its width.
         *
         * @return The number of pixels for width, or -1 if this attribute is not set.
         */
        virtual int getWidth();

        /**
         * @brief Get the attribute of the node representing its height.
         *
         * @return The number of pixels for height, or -1 if this attribute is not set.
         */
        virtual int getHeight();

        /**
         * @brief Get the attribute of the node representing its shortformat.
         *
         * @return The shortformat, or -1 if this attribute is not set.
         */
        virtual int getShortformat();

        /**
         * @brief Get the attribute of the node representing its videocodecid.
         *
         * @return The videocodecid, or -1 if this attribute is not set.
         */
        virtual int getVideocodecid();

        /**
         * @brief Get the attribute of the node representing if node is marked as favourite.
         *
         * @return True if node is marked as favourite, otherwise return false (attribute is not set).
         */
        virtual bool isFavourite();

        /**
         * @brief Get the attribute of the node representing its label.
         *
         * @return The label of the node, valid values are:
         *  - MegaNode::NODE_LBL_UNKNOWN = 0
         *  - MegaNode::NODE_LBL_RED = 1
         *  - MegaNode::NODE_LBL_ORANGE = 2
         *  - MegaNode::NODE_LBL_YELLOW = 3
         *  - MegaNode::NODE_LBL_GREEN = 4
         *  - MegaNode::NODE_LBL_BLUE = 5
         *  - MegaNode::NODE_LBL_PURPLE = 6
         *  - MegaNode::NODE_LBL_GREY = 7
         */
        virtual int getLabel();

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
         * @brief Returns the handle of the previous parent of this node.
         *
         * This attribute is set when nodes are moved to the Rubbish Bin to
         * ease their restoration. If the attribute is not set for the node,
         * this function returns MegaApi::INVALID_HANDLE
         *
         * @return Handle of the previous parent of this node or MegaApi::INVALID_HANDLE
         * if the attribute is not set.
         */
        virtual MegaHandle getRestoreHandle();

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
         * @brief Returns the creation time for the public link of the exported node (in seconds since the epoch).
         *
         * @return Creation time for the public link of the node. Returns 0 if the creation time is not available
         * and -1 if the MegaNode has not been exported.
         */
        virtual int64_t getPublicLinkCreationTime();

        /**
         * @brief Returns authentication key for a writable folder.
         *
         * The MegaNode object retains the ownership of the returned string. It will
         * be valid until the MegaNode object is deleted.
         *
         * @return authentication key for a writable folder. If there is no authentication key,
         * nullptr shall be returned.
         */
        virtual const char * getWritableLinkAuthKey();

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
         * - MegaNode::CHANGE_TYPE_NEW             = 0x400
         * Check if the node is new
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
         * - MegaNode::CHANGE_TYPE_NEW             = 0x400
         * Check if the node is new
         *
         * - MegaNode::CHANGE_TYPE_NAME            = 0x800
         * Check if the node name has changed
         *
         * - MegaNode::CHANGE_TYPE_FAVOURITE        = 0x1000
         * Check if the node was added to or removed from favorites
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
         * Only MegaNodes created with MegaApi::createForeignFileNode and MegaApi::createForeignFolderNode
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
         * @warning This method is not suitable for programming languages that require auto-generated bindings,
         * due to the lack of mapping of string pointers to objects in different languages.
         *
         * @return Decryption key of the file (in binary format)
         */
        virtual std::string* getNodeKey();

        /**
         * @brief Returns true if the node key is decrypted
         *
         * For nodes in shared folders, there could be missing keys. Also, faulty
         * clients might create invalid keys. In those cases, the node's key might
         * not be decrypted successfully.
         *
         * @return True if the node key is decrypted
         */
        virtual bool isNodeKeyDecrypted();

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
         * @brief Return the chat auth token to access this node
         *
         * The MegaNode object retains the ownership of the returned pointer. It will be valid until the deletion
         * of the MegaNode object.
         *
         * @return Chat auth token to access the node
         * @deprecated This function is intended for internal purposes and will be probably removed in future updates.
         */
        virtual const char *getChatAuth();

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

        virtual MegaHandle getOwner() const;

        /**
         * @brief Returns the device id stored as a Node attribute.
         * It will be an empty string for other nodes than device folders related to backups.
         *
         * The MegaNode object retains the ownership of the returned string, it will be valid until
         * the MegaNode object is deleted.
         *
         * @return The device id associated with the Node of a Backup folder.
         */
        virtual const char* getDeviceId() const;

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
         * @param d Serialization of a MegaNode object obtained from a chat message (in Base64)
         * @return A new MegaNode object, or NULL if error.
         */
        static MegaNode* unserialize(const char *d);
};


/**
 * @brief Represents a Set in MEGA
 *
 * It allows to get all data related to a Set in MEGA.
 *
 * Objects of this class aren't live, they are snapshots of the state of a Set
 * in MEGA when the object is created, they are immutable.
 *
 */
class MegaSet
{
public:
    /**
     * @brief Returns id of current Set.
     *
     * @return Set id.
     */
    virtual MegaHandle id() const { return INVALID_HANDLE; }

    /**
     * @brief Returns id of user that owns current Set.
     *
     * @return user id.
     */
    virtual MegaHandle user() const { return INVALID_HANDLE; }

    /**
     * @brief Returns timestamp of latest changes to current Set (but not to its Elements).
     *
     * @return timestamp value.
     */
    virtual int64_t ts() const { return 0; }

    /**
     * @brief Returns name of current Set.
     *
     * The MegaSet object retains the ownership of the returned string, it will be valid until
     * the MegaSet object is deleted.
     *
     * @return name of current Set.
     */
    virtual const char* name() const { return nullptr; }

    /**
     * @brief Returns id of Element set as 'cover' for current Set.
     *
     * It will return INVALID_HANDLE if no cover was set or if the Element became invalid
     * (was removed) in the meantime.
     *
     * @return Element id.
     */
    virtual MegaHandle cover() const { return INVALID_HANDLE; }

    /**
     * @brief Returns true if this Set has a specific change
     *
     * This value is only useful for Sets notified by MegaListener::onSetsUpdate or
     * MegaGlobalListener::onSetsUpdate that can notify about Set modifications.
     *
     * In other cases, the return value of this function will be always false.
     *
     * @param changeType The type of change to check. It can be one of the following values:
     *
     * - MegaSet::CHANGE_TYPE_NEW                   = 0x00
     * Check if the Set was new
     *
     * - MegaSet::CHANGE_TYPE_NAME                  = 0x01
     * Check if Set name has changed
     *
     * - MegaSet::CHANGE_TYPE_COVER                 = 0x02
     * Check if Set cover has changed
     *
     * - MegaSet::CHANGE_TYPE_REMOVED               = 0x03
     * Check if the Set was removed
     *
     * @return true if this Set has a specific change
     */
    virtual bool hasChanged(int changeType) const { return false; }

    virtual MegaSet* copy() const { return nullptr; }
    virtual ~MegaSet() = default;

    enum // match Set::CH_XXX values
    {
        CHANGE_TYPE_NEW,
        CHANGE_TYPE_NAME,
        CHANGE_TYPE_COVER,
        CHANGE_TYPE_REMOVED,

        CHANGE_TYPE_SIZE
    };
};

/**
 * @brief List of MegaSet objects
 *
 * A MegaSetList has the ownership of the MegaSet objects that it contains, so they will be
 * only valid until the MegaSetList is deleted. If you want to retain a MegaSet returned by
 * a MegaSetList, use MegaSet::copy().
 *
 * Objects of this class are immutable.
 */
class MegaSetList
{
public:
    /**
     * @brief Returns the MegaSet at the position i in the MegaSetList
     *
     * The MegaSetList retains the ownership of the returned MegaSet. It will be only valid until
     * the MegaSetList is deleted. If you want to retain a MegaSet returned by this function,
     * use MegaSet::copy().
     *
     * If the index is >= the size of the list, this function returns NULL.
     *
     * @param i Position of the MegaSet that we want to get for the list
     * @return MegaSet at the position i in the list
     */
    virtual const MegaSet* get(unsigned int i) const { return nullptr; }

    /**
     * @brief Returns the number of MegaSets in the list
     * @return Number of MegaSets in the list
     */
    virtual unsigned int size() const { return 0; }

    virtual MegaSetList* copy() const { return nullptr; }
    virtual ~MegaSetList() = default;
};


/**
 * @brief Represents an Element of a Set in MEGA
 *
 * It allows to get all data related to an Element of a Set in MEGA.
 *
 * Objects of this class aren't live, they are snapshots of the state of an Element of a Set
 * in MEGA when the object is created, they are immutable.
 *
 */
class MegaSetElement
{
public:
    /**
     * @brief Returns id of current Element.
     *
     * @return Element id.
     */
    virtual MegaHandle id() const { return INVALID_HANDLE; }

    /**
     * @brief Returns handle of file-node represented by current Element.
     *
     * @return file-node handle.
     */
    virtual MegaHandle node() const { return INVALID_HANDLE; }

    /**
     * @brief Returns order of current Element.
     *
     * If not set explicitly, the API will typically set it to multiples of 1000.
     *
     * @return order of current Element.
     */
    virtual int64_t order() const { return 0; }

    /**
     * @brief Returns timestamp of latest changes to current Element.
     *
     * @return timestamp value.
     */
    virtual int64_t ts() const { return 0; }

    /**
     * @brief Returns name of current Element.
     *
     * The MegaSetElement object retains the ownership of the returned string, it will be valid until
     * the MegaSetElement object is deleted.
     *
     * @return name of current Element.
     */
    virtual const char* name() const { return nullptr; }

    virtual bool hasChanged(int changeType) const { return false; }

    virtual MegaSetElement* copy() const { return nullptr; }
    virtual ~MegaSetElement() = default;

    enum // match SetElement::CH_EL_XXX values
    {
        CHANGE_TYPE_ELEM_NEW,
        CHANGE_TYPE_ELEM_NAME,
        CHANGE_TYPE_ELEM_ORDER,
        CHANGE_TYPE_ELEM_REMOVED,

        CHANGE_TYPE_ELEM_SIZE
    };
};

/**
 * @brief List of MegaSetElement objects
 *
 * A MegaSetElementList has the ownership of the MegaSetElement objects that it contains, so they will be
 * only valid until the MegaSetElementList is deleted. If you want to retain a MegaSetElement returned by
 * a MegaSetElementList, use MegaSetElement::copy().
 *
 * Objects of this class are immutable.
 */
class MegaSetElementList
{
public:
    /**
     * @brief Returns the MegaSetElement at the position i in the MegaSetElementList
     *
     * The MegaSetElementList retains the ownership of the returned MegaSetElement. It will be only valid until
     * the MegaSetElementList is deleted. If you want to retain a MegaSetElement returned by this function,
     * use MegaSetElement::copy().
     *
     * If the index is >= the size of the list, this function returns NULL.
     *
     * @param i Position of the MegaSetElement that we want to get for the list
     * @return MegaSetElement at the position i in the list
     */
    virtual const MegaSetElement* get(unsigned int i) const { return nullptr; }

    /**
     * @brief Returns the number of MegaSetElements in the list
     * @return Number of MegaSetElements in the list
     */
    virtual unsigned int size() const { return 0; }

    virtual MegaSetElementList* copy() const { return nullptr; }
    virtual ~MegaSetElementList() = default;
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
            CHANGE_TYPE_AUTHRING                    = 0x01,
            CHANGE_TYPE_LSTINT                      = 0x02,
            CHANGE_TYPE_AVATAR                      = 0x04,
            CHANGE_TYPE_FIRSTNAME                   = 0x08,
            CHANGE_TYPE_LASTNAME                    = 0x10,
            CHANGE_TYPE_EMAIL                       = 0x20,
            CHANGE_TYPE_KEYRING                     = 0x40,
            CHANGE_TYPE_COUNTRY                     = 0x80,
            CHANGE_TYPE_BIRTHDAY                    = 0x100,
            CHANGE_TYPE_PUBKEY_CU255                = 0x200,
            CHANGE_TYPE_PUBKEY_ED255                = 0x400,
            CHANGE_TYPE_SIG_PUBKEY_RSA              = 0x800,
            CHANGE_TYPE_SIG_PUBKEY_CU255            = 0x1000,
            CHANGE_TYPE_LANGUAGE                    = 0x2000,
            CHANGE_TYPE_PWD_REMINDER                = 0x4000,
            CHANGE_TYPE_DISABLE_VERSIONS            = 0x8000,
            CHANGE_TYPE_CONTACT_LINK_VERIFICATION   = 0x10000,
            CHANGE_TYPE_RICH_PREVIEWS               = 0x20000,
            CHANGE_TYPE_RUBBISH_TIME                = 0x40000,
            CHANGE_TYPE_STORAGE_STATE               = 0x80000,
            CHANGE_TYPE_GEOLOCATION                 = 0x100000,
            CHANGE_TYPE_CAMERA_UPLOADS_FOLDER       = 0x200000,
            CHANGE_TYPE_MY_CHAT_FILES_FOLDER        = 0x400000,
            CHANGE_TYPE_PUSH_SETTINGS               = 0x800000,
            CHANGE_TYPE_ALIAS                       = 0x1000000,
            CHANGE_TYPE_UNSHAREABLE_KEY             = 0x2000000,
            CHANGE_TYPE_DEVICE_NAMES                = 0x4000000,
            CHANGE_TYPE_MY_BACKUPS_FOLDER           = 0x8000000,
            CHANGE_TYPE_COOKIE_SETTINGS             = 0x10000000,
            CHANGE_TYPE_NO_CALLKIT                  = 0x20000000,
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
         * - MegaUser::CHANGE_TYPE_CONTACT_LINK_VERIFICATION = 0x10000
         * Check if option for automatic contact-link verification has changed
         *
         * - MegaUser::CHANGE_TYPE_RICH_PREVIEWS    = 0x20000
         * Check if option for rich links has changed
         *
         * - MegaUser::CHANGE_TYPE_RUBBISH_TIME    = 0x40000
         * Check if rubbish time for autopurge has changed
         *
         * - MegaUser::CHANGE_TYPE_STORAGE_STATE   = 0x80000
         * Check if the state of the storage has changed
         *
         * - MegaUser::CHANGE_TYPE_GEOLOCATION    = 0x100000
         * Check if option for geolocation messages has changed
         *
         * - MegaUser::CHANGE_TYPE_CAMERA_UPLOADS_FOLDER = 0x200000
         * Check if "Camera uploads" folder has changed
         *
         * - MegaUser::CHANGE_TYPE_MY_CHAT_FILES_FOLDER = 0x400000
         * Check if "My chat files" folder changed
         *
         * - MegaUser::CHANGE_TYPE_PUSH_SETTINGS = 0x800000
         * Check if settings for push notifications have changed
         *
         * - MegaUser::CHANGE_TYPE_ALIAS    = 0x1000000
         * Check if aliases have changed
         *
         * - MegaUser::CHANGE_TYPE_UNSHAREABLE_KEY = 0x2000000
         * (internal) The unshareable key has been created
         *
         * - MegaUser::CHANGE_TYPE_DEVICE_NAMES = 0x4000000
         * Check if device names have changed
         *
         * - MegaUser::CHANGE_TYPE_MY_BACKUPS_FOLDER = 0x8000000
         * Check if "My Backups" folder has changed
         *
         * - MegaUser::CHANGE_TYPE_COOKIE_SETTINGS     = 0x10000000
         * Check if option for cookie settings has changed
         *
         * - MegaUser::CHANGE_TYPE_NO_CALLKIT     = 0x20000000
         * Check if option for iOS CallKit has changed
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
         *
         * - MegaUser::CHANGE_TYPE_CONTACT_LINK_VERIFICATION = 0x10000
         * Check if option for automatic contact-link verification has changed
         *
         * - MegaUser::CHANGE_TYPE_RICH_PREVIEWS    = 0x20000
         * Check if option for rich links has changed
         *
         * - MegaUser::CHANGE_TYPE_RUBBISH_TIME    = 0x40000
         * Check if rubbish time for autopurge has changed
         *
         * - MegaUser::CHANGE_TYPE_STORAGE_STATE   = 0x80000
         * Check if the state of the storage has changed
         *
         * - MegaUser::CHANGE_TYPE_GEOLOCATION    = 0x100000
         * Check if option for geolocation messages has changed
         *
         * - MegaUser::CHANGE_TYPE_PUSH_SETTINGS = 0x800000
         * Check if settings for push notifications have changed
         *
         * - MegaUser::CHANGE_TYPE_ALIAS    = 0x1000000
         * Check if aliases have changed
         *
         * - MegaUser::CHANGE_TYPE_UNSHAREABLE_KEY = 0x2000000
         * (internal) The unshareable key has been created
         *
         * - MegaUser::CHANGE_TYPE_DEVICE_NAMES = 0x4000000
         * Check if device names have changed
         *
         * - MegaUser::CHANGE_TYPE_BACKUP_NAMES = 0x8000000
         *
         * - MegaUser::CHANGE_TYPE_COOKIE_SETTINGS     = 0x10000000
         * Check if option for cookie settings has changed
         *
         * - MegaUser::CHANGE_TYPE_NO_CALLKIT     = 0x20000000
         * Check if option for iOS CallKit has changed
         *
         * Check if backup names have changed         */
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
* @brief Represents a user alert in MEGA.
* Alerts are the notifictions appearing under the bell in the webclient
*
* Objects of this class aren't live, they are snapshots of the state
* in MEGA when the object is created, they are immutable.
*
* MegaUserAlerts can be retrieved with MegaApi::getUserAlerts
*
*/
class MegaUserAlert
{
public:

    enum {
        TYPE_INCOMINGPENDINGCONTACT_REQUEST,
        TYPE_INCOMINGPENDINGCONTACT_CANCELLED,
        TYPE_INCOMINGPENDINGCONTACT_REMINDER,
        TYPE_CONTACTCHANGE_DELETEDYOU,
        TYPE_CONTACTCHANGE_CONTACTESTABLISHED,
        TYPE_CONTACTCHANGE_ACCOUNTDELETED,
        TYPE_CONTACTCHANGE_BLOCKEDYOU,
        TYPE_UPDATEDPENDINGCONTACTINCOMING_IGNORED,
        TYPE_UPDATEDPENDINGCONTACTINCOMING_ACCEPTED,
        TYPE_UPDATEDPENDINGCONTACTINCOMING_DENIED,
        TYPE_UPDATEDPENDINGCONTACTOUTGOING_ACCEPTED,
        TYPE_UPDATEDPENDINGCONTACTOUTGOING_DENIED,
        TYPE_NEWSHARE,
        TYPE_DELETEDSHARE,
        TYPE_NEWSHAREDNODES,
        TYPE_REMOVEDSHAREDNODES,
        TYPE_UPDATEDSHAREDNODES,
        TYPE_PAYMENT_SUCCEEDED,
        TYPE_PAYMENT_FAILED,
        TYPE_PAYMENTREMINDER,
        TYPE_TAKEDOWN,
        TYPE_TAKEDOWN_REINSTATED,

        TOTAL_OF_ALERT_TYPES
    };

    virtual ~MegaUserAlert();

    /**
    * @brief Creates a copy of this MegaUserAlert object.
    *
    * The resulting object is fully independent of the source MegaUserAlert,
    * it contains a copy of all internal attributes, so it will be valid after
    * the original object is deleted.
    *
    * You are the owner of the returned object
    *
    * @return Copy of the MegaUserAlert object
    */
    virtual MegaUserAlert *copy() const;

    /**
    * @brief Returns the id of the alert
    *
    * The ids are assigned to alerts sequentially from program start,
    * however there may be gaps. The id can be used to create an
    * association with a UI element in order to process updates in callbacks.
    *
    * @return Type of alert associated with the object
    */
    virtual unsigned getId() const;

    /**
    * @brief Returns whether the alert has been acknowledged by this client or another
    *
    * @return Flag indicating whether the alert has been seen
    */
    virtual bool getSeen() const;

    /**
    * @brief Returns whether the alert is still relevant to the logged in user.
    *
    * An alert may be relevant initially but become non-relevant, eg. payment reminder.
    * Alerts which are no longer relevant are usually removed from the visible list.
    *
    * @return Flag indicting whether the alert is still relevant
    */
    virtual bool getRelevant() const;

    /**
    * @brief Returns the type of alert associated with the object
    * @return Type of alert associated with the object
    */
    virtual int getType() const;

    /**
    * @brief Returns a readable string that shows the type of alert
    *
    * This function returns a pointer to a statically allocated buffer.
    * You don't have to free the returned pointer
    *
    * @return Readable string showing the type of alert
    */
    virtual const char *getTypeString() const;

    /**
    * @brief Returns the handle of a user related to the alert
    *
    * This value is valid for user related alerts:
    *  TYPE_UPDATEDPENDINGCONTACTINCOMING_IGNORED, TYPE_UPDATEDPENDINGCONTACTOUTGOING_ACCEPTED,
    *  TYPE_UPDATEDPENDINGCONTACTOUTGOING_DENIED,
    *  TYPE_CONTACTCHANGE_CONTACTESTABLISHED, TYPE_CONTACTCHANGE_ACCOUNTDELETED,
    *  TYPE_CONTACTCHANGE_BLOCKEDYOU, TYPE_CONTACTCHANGE_DELETEDYOU,
    *  TYPE_NEWSHARE, TYPE_DELETEDSHARE, TYPE_NEWSHAREDNODES, TYPE_REMOVEDSHAREDNODES
    *
    * @warning This value is still valid for user related alerts:
    *  TYPE_INCOMINGPENDINGCONTACT_CANCELLED, TYPE_INCOMINGPENDINGCONTACT_REMINDER,
    *  TYPE_INCOMINGPENDINGCONTACT_REQUEST
    * However, the returned value is the handle of the Pending Contact Request. There is no
    * user's handle associated to these type of alerts. Use MegaUserAlert::getPcrHandle.
    *
    * @return the associated user's handle, otherwise UNDEF
    */
    virtual MegaHandle getUserHandle() const;

    /**
    * @brief Returns the handle of a node related to the alert
    *
    * This value is valid for alerts that relate to a single node.
    *  TYPE_NEWSHARE (folder handle), TYPE_DELETEDSHARE (folder handle), TYPE_NEWSHAREDNODES (parent handle), TYPE_TAKEDOWN (node handle),
    *  TYPE_TAKEDOWN_REINSTATED (node handle)
    *
    * @return the relevant node handle, or UNDEF if this alert does not have one.
    */
    virtual MegaHandle getNodeHandle() const;

    /**
    * @brief Returns the handle of a Pending Contact Request related to the alert
    *
    * This value is valid for user related alerts:
    *  TYPE_INCOMINGPENDINGCONTACT_CANCELLED, TYPE_INCOMINGPENDINGCONTACT_REMINDER,
    *  TYPE_INCOMINGPENDINGCONTACT_REQUEST
    *
    * @return the relevant PCR handle, or UNDEF if this alert does not have one.
    */
    virtual MegaHandle getPcrHandle() const;

    /**
    * @brief Returns an email related to the alert
    *
    * This value is valid for alerts that relate to another user, provided the
    * user could be looked up at the time the alert arrived. If it was not available,
    * this function will return false and the client can request it via the userHandle.
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaUserAlert object is deleted.
    *   TYPE_CONTACTCHANGE_ACCOUNTDELETED,TYPE_CONTACTCHANGE_BLOCKEDYOU,
    *   TYPE_CONTACTCHANGE_CONTACTESTABLISHED, TYPE_CONTACTCHANGE_DELETEDYOU,
    *   TYPE_DELETEDSHARE,
    *   TYPE_INCOMINGPENDINGCONTACT_CANCELLED, TYPE_INCOMINGPENDINGCONTACT_REMINDER,
    *   TYPE_INCOMINGPENDINGCONTACT_REQUEST,
    *   TYPE_NEWSHARE, TYPE_NEWSHAREDNODES, TYPE_REMOVEDSHAREDNODES
    *   TYPE_UPDATEDPENDINGCONTACTINCOMING_IGNORED, TYPE_UPDATEDPENDINGCONTACTOUTGOING_ACCEPTED,
    *   TYPE_UPDATEDPENDINGCONTACTOUTGOING_DENIED,
    *
    * @return email string of the relevant user, or NULL if not available
    */
    virtual const char* getEmail() const;

    /**
    * @brief Returns the path of a file, folder, or node related to the alert
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaUserAlert object is deleted.
    *
    * This value is valid for those alerts that relate to a single path, provided
    * it could be looked up from the cached nodes at the time the alert arrived.
    * Otherwise, it may be obtainable via the nodeHandle.
    *   TYPE_DELETEDSHARE, TYPE_NEWSHARE?, TYPE_TAKEDOWN?, TYPE_TAKEDOWN_REINSTATED?
    *
    * @return the path string if relevant and available, otherwise NULL
    */
    virtual const char* getPath() const;

    /**
     * @brief Returns the name of a file, folder, or node related to the alert
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaUserAlert object is deleted.
     *
     * This value is valid for those alerts that relate to a single name, provided
     * it could be looked up from the cached nodes at the time the alert arrived.
     * Otherwise, it may be obtainable via the nodeHandle.
     *   TYPE_DELETEDSHARE, TYPE_NEWSHARE?, TYPE_TAKEDOWN?, TYPE_TAKEDOWN_REINSTATED?
     *
     * @return the name string if relevant and available, otherwise NULL
     */
    virtual const char* getName() const;

    /**
    * @brief Returns the heading related to this alert
    *
    * The SDK retains the ownership of the returned value. They will be valid until
    * the MegaUserAlert object is deleted.
    *
    * This value is valid for all alerts, and similar to the strings displayed in the
    * webclient alerts.
    *
    * @return heading related to this alert.
    */
    virtual const char* getHeading() const;

    /**
    * @brief Returns the title related to this alert
    *
    * The SDK retains the ownership of the returned value. They will be valid until
    * the MegaUserAlert object is deleted.
    *
    * This value is valid for all alerts, and similar to the strings displayed in the
    * webclient alerts.
    *
    * @return title related to this alert.
    */
    virtual const char* getTitle() const;

    /**
    * @brief Returns a number related to this alert
    *
    * This value is valid for these alerts:
    *   TYPE_DELETEDSHARE (0: value 1 if access for this user was removed by the share owner, otherwise
    *                        value 0 if someone left the folder)
    *   TYPE_NEWSHAREDNODES (0: folder count 1: file count)
    *   TYPE_REMOVEDSHAREDNODES (0: item count)
    *   TYPE_UPDATEDSHAREDNODES (0: item count)
    *
    * @return Number related to this request, or -1 if the index is invalid
    */
    virtual int64_t getNumber(unsigned index) const;

    /**
    * @brief Returns a timestamp related to this alert
    *
    * This value is valid for index 0 for all requests, indicating when the alert occurred.
    * Additionally TYPE_PAYMENTREMINDER index 1 is the timestamp of the expiry of the period.
    *
    * @return Timestamp related to this request, or -1 if the index is invalid
    */
    virtual int64_t getTimestamp(unsigned index) const;

    /**
    * @brief Returns a handle related to this alert
    *
    * TYPE_NEWSHAREDNODES (folder and files)
    *
    * @return MegaHandle related to this request, or INVALID_HANDLE if the index is invalid
    */
    virtual MegaHandle getHandle(unsigned index) const;

    /**
    * @brief Returns an additional string, related to the alert
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaUserAlert object is deleted.
    *
    * This value is currently only valid for
    *   TYPE_PAYMENT_FAILED      index 0: the plan name
    *   TYPE_PAYMENT_SUCCEEDED   index 0: the plan name
    *
    * @return a pointer to the string if index is valid; otherwise NULL
    */
    virtual const char* getString(unsigned index) const;

    /**
     * @brief Indicates if the user alert is changed by yourself or by another client.
     *
     * This value is only useful for user alerts notified by MegaListener::onUserAlertsUpdate or
     * MegaGlobalListener::onUserAlertsUpdate that can notify about user alerts modifications.
     *
     * @return false if the change is external. true if the change is the result of a
     * request sent by this instance of the SDK.
     */
    virtual bool isOwnChange() const;
};

/**
 * @brief List of MegaHandle objects
 *
 */
class MegaHandleList
{
protected:
    MegaHandleList();

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

class MegaIntegerList
{
public:
    virtual ~MegaIntegerList();
    virtual MegaIntegerList *copy() const;

    /**
     * @brief Returns the integer at the position i in the MegaIntegerList
     *
     * If the index is >= the size of the list, this function returns -1.
     *
     * @param i Position of the integer that we want to get for the list
     * @return Integer at the position i in the list
     */
    virtual int64_t get(int i) const;

    /**
     * @brief Returns the number of integer values in the list
     * @return Number of integer values in the list
     */
    virtual int size() const;
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

        /**
         * @brief Returns true if the sharing is pending
         *
         * A sharing is pending when the folder has been shared with a user (or email) that
         * is not still a contact of this account.
         *
         * @return True if the sharing is pending, otherwise false.
         */
        virtual bool isPending();
};

#ifdef ENABLE_CHAT
class MegaTextChatPeerList
{
protected:
    MegaTextChatPeerList();

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
};

class MegaTextChat
{
public:

    enum
    {
        CHANGE_TYPE_ATTACHMENT      = 0x01,
        CHANGE_TYPE_FLAGS           = 0x02,
        CHANGE_TYPE_MODE            = 0x04,
        CHANGE_TYPE_CHAT_OPTIONS    = 0x08,
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
     * @brief Returns the Unified key of the chat, if it's a public chat.
     *
     * The MegaTextChat retains the ownership of the returned string. It will
     * be only valid until the MegaTextChat is deleted.
     *
     * @return The Unified key [<senderid><uk>] of the chat as a byte array encoded in Base64URL, or NULL if not available.
     */
    virtual const char *getUnifiedKey() const;

    /**
     * @brief Returns the chat options.
     *
     * The returned value contains the chat options represented in 1 Byte, where each individual option is stored in 1 bit.
     * Check ChatOptions struct at types.h
     *
     * @return The chat options in a numeric format
     */
    virtual unsigned char getChatOptions() const;

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
     * - MegaTextChat::CHANGE_TYPE_ATTACHMENT       = 0x01
     * Check if the access to nodes have been granted/revoked
     *
     * - MegaTextChat::CHANGE_TYPE_FLAGS            = 0x02
     * Check if flags have changed (like archive flag)
     *
     * - MegaTextChat::CHANGE_TYPE_MODE             = 0x04
     * Check if operation mode has changed to private mode (from public mode)
     *
     * - MegaTextChat::CHANGE_TYPE_CHAT_OPTIONS     = 0x08
     * Check if chat options have changed
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
     * - MegaTextChat::CHANGE_TYPE_ATTACHMENT       = 0x01
     * Check if the access to nodes have been granted/revoked
     *
     * - MegaTextChat::CHANGE_TYPE_FLAGS            = 0x02
     * Check if flags have changed (like archive flag)
     *
     * - MegaTextChat::CHANGE_TYPE_MODE             = 0x04
     * Check if operation mode has changed to private mode (from public mode)
     *
     * - MegaTextChat::CHANGE_TYPE_CHAT_OPTIONS     = 0x08
     * Check if chat options have changed
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

    /**
     * @brief Returns whether this chat has been archived by the user or not
     * @return True if this chat is archived.
     */
    virtual bool isArchived() const;

    /**
     * @brief Returns whether this chat is public or private
     * @return True if this chat is public
     */
    virtual bool isPublicChat() const;

    /**
     * @brief Returns whether this chat is a meeting room
     * @return True if this chat is a meeting room
     */
    virtual bool isMeeting() const;
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
    virtual const MegaTextChat *get(unsigned int i) const;

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
protected:
    MegaStringMap();

public:
    /**
     * @brief Creates a new instance of MegaStringMap
     * @return A pointer to the superclass of the private object
     */
    static MegaStringMap *createInstance();

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
    /**
     * @brief Creates a new instance of MegaStringList
     *
     * @return A pointer to the superclass of the private object
     */
    static MegaStringList *createInstance();

    virtual ~MegaStringList();

    virtual MegaStringList *copy() const;

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
    virtual const char* get(int i) const;

    /**
     * @brief Returns the number of strings in the list
     * @return Number of strings in the list
     */
    virtual int size() const;

    /**
     * @brief Add element to the list
     *
     * @param value String to add to list
     */
    virtual void add(const char* value);
};

/**
* @brief A map of strings to string lists
*
* A MegaStringListMap takes owership of the MegaStringList objects passed to it. It does
* NOT take ownership of the keys passed to it but makes a local copy.
*/
class MegaStringListMap
{
protected:
    MegaStringListMap();

public:
    virtual ~MegaStringListMap();

    static MegaStringListMap* createInstance();

    virtual MegaStringListMap* copy() const;

    /**
     * @brief Returns the string list at the given key in the map
     *
     * The MegaStringMap retains the ownership of the returned string list. It will be only
     * valid until the MegaStringMap is deleted.
     *
     * If the key is not found in the map, this function returns NULL.
     *
     * @param key Key to lookup in the map. Must be null-terminated
     * @return String list at the given key in the map
     */
    virtual const MegaStringList* get(const char* key) const;

    /**
     * @brief Returns the list of keys in the MegaStringListMap
     *
     * You take the ownership of the returned value
     *
     * @return A MegaStringList containing the keys present in the MegaStringListMap
     */
    virtual MegaStringList *getKeys() const;

    /**
     * @brief Sets a value in the map for the given key.
     *
     * If the key already exists, the value will be overwritten by the
     * new value.
     *
     * The map does not take ownership of the passed key, it makes
     * a local copy. However, it does take ownership of the passed value.
     *
     * @param key The key in the map. It must be a null-terminated string.
     * @param value The new value for the key in the map.
     */
    virtual void set(const char* key, const MegaStringList* value);

    /**
     * @brief Returns the number of (string, string list) pairs in the map
     * @return Number of pairs in the map
     */
    virtual int size() const;
};

/**
* @brief A list of string lists forming a table of strings.
*
* Each row can have a different number of columns.
* However, ideally this class should be used as a table only.
*
* A MegaStringTable takes owership of the MegaStringList objects passed to it.
*/
class MegaStringTable
{
protected:
    MegaStringTable();

public:
    virtual ~MegaStringTable();

    static MegaStringTable *createInstance();

    virtual MegaStringTable* copy() const;

    /**
     * @brief Appends a new string list to the end of the table
     *
     * The table takes ownership of the passed value.
     *
     * @param value The string list to append
     */
    virtual void append(const MegaStringList* value);

    /**
     * @brief Returns the string list at position i
     *
     * The table retains the ownership of the returned string list. It will be only valid until
     * the table is deleted.
     *
     * The returned pointer is null if i is out of range.
     *
     * @return The string list at position i
     */
    virtual const MegaStringList* get(int i) const;

    /**
     * @brief Returns the number of string lists in the table
     * @return Number of string lists in the table
     */
    virtual int size() const;
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
    protected:
        MegaNodeList();

    public:
        /**
         * @brief Creates a new instance of MegaNodeList
         * @return A pointer to the superclass of the private object
         */
        static MegaNodeList * createInstance();

		virtual ~MegaNodeList();

        virtual MegaNodeList *copy() const;

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
        virtual MegaNode* get(int i) const;

        /**
         * @brief Returns the number of MegaNode objects in the list
         * @return Number of MegaNode objects in the list
         */
        virtual int size() const;

        /**
         * @brief Add new node to list
         * @param node MegaNode to be added. The node inserted is a copy from 'node'
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
* @brief List of MegaUserAlert objects
*
* A MegaUserAlertList has the ownership of the MegaUserAlert objects that it contains, so they will be
* only valid until the MegaUserAlertList is deleted. If you want to retain a MegaUserAlert returned by
* a MegaUserAlertList, use MegaUserAlert::copy.
*
* Objects of this class are immutable.
*
* @see MegaApi::getUserAlerts
*
*/
class MegaUserAlertList
{
public:
    virtual ~MegaUserAlertList();

    virtual MegaUserAlertList *copy() const;

    /**
    * @brief Returns the MegaUserAlert at the position i in the MegaUserAlertList
    *
    * The MegaUserAlertList retains the ownership of the returned MegaUserAlert. It will be only valid until
    * the MegaUserAlertList is deleted.
    *
    * If the index is >= the size of the list, this function returns NULL.
    *
    * @param i Position of the MegaUserAlert that we want to get for the list
    * @return MegaUserAlert at the position i in the list
    */
    virtual MegaUserAlert* get(int i) const;

    /**
    * @brief Returns the number of MegaUserAlert objects in the list
    * @return Number of MegaUserAlert objects in the list
    */
    virtual int size() const;

    /**
     * @brief Removes all MegaUserAlert objects from the list (does not delete them)
     */
    virtual void clear();
};


/**
* @brief Represents a set of files uploaded or updated in MEGA.
* These are used to display the recent changes to an account.
*
* Objects of this class aren't live, they are snapshots of the state
* in MEGA when the object is created, they are immutable.
*
* MegaRecentActionBuckets can be retrieved with MegaApi::getRecentActions
*
*/
class MegaRecentActionBucket
{
public:

    virtual ~MegaRecentActionBucket();

    /**
    * @brief Creates a copy of this MegaRecentActionBucket object.
    *
    * The resulting object is fully independent of the source MegaRecentActionBucket,
    * it contains a copy of all internal attributes, so it will be valid after
    * the original object is deleted.
    *
    * You are the owner of the returned object
    *
    * @return Copy of the MegaRecentActionBucket object
    */
    virtual MegaRecentActionBucket *copy() const;

    /**
    * @brief Returns a timestamp reflecting when these changes occurred
    *
    * @return Timestamp indicating when the changes occurred (in seconds since the Epoch)
    */
    virtual int64_t getTimestamp() const;

    /**
    * @brief Returns the email of the user who made the changes
    *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaRecentActionBucket object is deleted.
    *
    * @return The associated user's email
    */
    virtual const char* getUserEmail() const;

    /**
    * @brief Returns the handle of the parent folder these changes occurred in
    *
    * @return The handle of the parent folder for these changes.
    */
    virtual MegaHandle getParentHandle() const;

    /**
    * @brief Returns whether the changes are updated files, or new files
    *
    * @return True if the changes are updates rather than newly uploaded files.
    */
    virtual bool isUpdate() const;

    /**
    * @brief Returns whether the files are photos or videos
    *
    * @return True if the files in this change are media files.
    */
    virtual bool isMedia() const;

    /**
    * @brief Returns nodes representing the files changed in this bucket
    *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaRecentActionBucket object is deleted.
     *
    * @return A MegaNodeList containing the files in the bucket
    */
    virtual const MegaNodeList* getNodes() const;
};

/**
* @brief List of MegaRecentActionBucket objects
*
* A MegaRecentActionBucketList has the ownership of the MegaRecentActionBucket objects that it contains, so they will be
* only valid until the MegaRecentActionBucketList is deleted. If you want to retain a MegaRecentActionBucket returned by
* a MegaRecentActionBucketList, use MegaRecentActionBucket::copy.
*
* Objects of this class are immutable.
*
* @see MegaApi::getRecentActions
*
*/
class MegaRecentActionBucketList
{
public:
    virtual ~MegaRecentActionBucketList();

    /**
    * @brief Creates a copy of this MegaRecentActionBucketList object.
    *
    * The resulting object is fully independent of the source MegaRecentActionBucketList,
    * it contains a copy of all internal attributes, so it will be valid after
    * the original object is deleted.
    *
    * You are the owner of the returned object
    *
    * @return Copy of the MegaRecentActionBucketList object
    */
    virtual MegaRecentActionBucketList *copy() const;

    /**
    * @brief Returns the MegaRecentActionBucket at the position i in the MegaRecentActionBucketList
    *
    * The MegaRecentActionBucketList retains the ownership of the returned MegaRecentActionBucket. It will be only valid until
    * the MegaRecentActionBucketList is deleted.
    *
    * If the index is >= the size of the list, this function returns NULL.
    *
    * @param i Position of the MegaRecentActionBucket that we want to get for the list
    * @return MegaRecentActionBucket at the position i in the list
    */
    virtual MegaRecentActionBucket* get(int i) const;

    /**
    * @brief Returns the number of MegaRecentActionBucket objects in the list
    * @return Number of MegaRecentActionBucket objects in the list
    */
    virtual int size() const;
};


/**
* @brief Represents a set of properties that define a SmartBanner.
* These are used to display a banner in mobile apps.
*
* MegaBanner-s can be retrieved from MegaBannerList
*
*/
class MegaBanner
{
public:
    virtual ~MegaBanner();

    /**
    * @brief Creates a copy of this MegaBanner object.
    *
    * The resulting object is fully independent of the source MegaBanner,
    * it contains a copy of all internal attributes, so it will be valid after
    * the original object is deleted.
    *
    * You are the owner of the returned object
    *
    * @return Copy of the MegaBanner object
    */
    virtual MegaBanner* copy() const;

    /**
    * @brief Returns the id of the MegaBanner
    *
    * @return Id of this banner
    */
    virtual int getId() const;

    /**
    * @brief Returns the title associated with the MegaBanner object
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaBanner object is deleted.
    *
    * @return Title associated with the MegaBanner object
    */
    virtual const char* getTitle() const;

    /**
    * @brief Returns the description associated with the MegaBanner object
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaBanner object is deleted.
    *
    * @return Description associated with the MegaBanner object
    */
    virtual const char* getDescription() const;

    /**
    * @brief Returns the filename of the image used by the MegaBanner object
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaBanner object is deleted.
    *
    * @return Filename of the image used by the MegaBanner object
    */
    virtual const char* getImage() const;

    /**
    * @brief Returns the URL associated with the MegaBanner object
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaBanner object is deleted.
    *
    * @return URL associated with the MegaBanner object
    */
    virtual const char* getUrl() const;

    /**
    * @brief Returns the filename of the background image used by the MegaBanner object
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaBanner object is deleted.
    *
    * @return Filename of the background image used by the MegaBanner object
    */
    virtual const char* getBackgroundImage() const;

    /**
    * @brief Returns the URL where images are located
    *
    * The SDK retains the ownership of the returned value. It will be valid until
    * the MegaBanner object is deleted.
    *
    * @return URL where images are located
    */
    virtual const char* getImageLocation() const;

protected:
    MegaBanner();
};

/**
* @brief List of MegaBanner objects
*
* A MegaBannerList has the ownership of the MegaBanner objects that it contains, so they will be
* only valid until the MegaBannerList is deleted.
*
*/
class MegaBannerList
{
public:
    virtual ~MegaBannerList();

    /**
    * @brief Creates a copy of this MegaBannerList object.
    *
    * The resulting object is fully independent of the source MegaBannerList,
    * it contains a copy of all internal objects, so it will be valid after
    * the original object is deleted.
    *
    * You are the owner of the returned object
    *
    * @return Copy of the MegaBannerList object
    */
    virtual MegaBannerList* copy() const;

    /**
    * @brief Returns the MegaBanner at position i in the MegaBannerList
    *
    * The MegaBannerList retains the ownership of the returned MegaBanner. It will be only valid until
    * the MegaBannerList is deleted.
    *
    * If the index is >= the size of the list, this function returns NULL.
    *
    * @param i Position of the MegaBanner that we want to get for the list
    * @return MegaBanner at position i in the list
    */
    virtual const MegaBanner* get(int i) const;

    /**
    * @brief Returns the number of MegaBanner objects in the list
    * @return Number of MegaBanner objects in the list
    */
    virtual int size() const;

protected:
    MegaBannerList();
};

/**
 * @brief Provides information about an asynchronous request
 *
 * Most functions in this API are asynchronous, except the ones that never require to
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
            TYPE_LOGIN                                                      = 0,
            TYPE_CREATE_FOLDER                                              = 1,
            TYPE_MOVE                                                       = 2,
            TYPE_COPY                                                       = 3,
            TYPE_RENAME                                                     = 4,
            TYPE_REMOVE                                                     = 5,
            TYPE_SHARE                                                      = 6,
            TYPE_IMPORT_LINK                                                = 7,
            TYPE_EXPORT                                                     = 8,
            TYPE_FETCH_NODES                                                = 9,
            TYPE_ACCOUNT_DETAILS                                            = 10,
            TYPE_CHANGE_PW                                                  = 11,
            TYPE_UPLOAD                                                     = 12,
            TYPE_LOGOUT                                                     = 13,
            TYPE_GET_PUBLIC_NODE                                            = 14,
            TYPE_GET_ATTR_FILE                                              = 15,
            TYPE_SET_ATTR_FILE                                              = 16,
            TYPE_GET_ATTR_USER                                              = 17,
            TYPE_SET_ATTR_USER                                              = 18,
            TYPE_RETRY_PENDING_CONNECTIONS                                  = 19,
            TYPE_REMOVE_CONTACT                                             = 20,
            TYPE_CREATE_ACCOUNT                                             = 21,
            TYPE_CONFIRM_ACCOUNT                                            = 22,
            TYPE_QUERY_SIGNUP_LINK                                          = 23,
            TYPE_ADD_SYNC                                                   = 24,
            TYPE_REMOVE_SYNC                                                = 25,
            TYPE_DISABLE_SYNC                                               = 26,
            TYPE_ENABLE_SYNC                                                = 27,
            TYPE_COPY_SYNC_CONFIG                                           = 28,
            TYPE_COPY_CACHED_STATUS                                         = 29,
            TYPE_IMPORT_SYNC_CONFIGS                                        = 30,
            TYPE_REMOVE_SYNCS                                               = 31,
            TYPE_PAUSE_TRANSFERS                                            = 32,
            TYPE_CANCEL_TRANSFER                                            = 33,
            TYPE_CANCEL_TRANSFERS                                           = 34,
            TYPE_DELETE                                                     = 35,
            TYPE_REPORT_EVENT                                               = 36,
            TYPE_CANCEL_ATTR_FILE                                           = 37,
            TYPE_GET_PRICING                                                = 38,
            TYPE_GET_PAYMENT_ID                                             = 39,
            TYPE_GET_USER_DATA                                              = 40,
            TYPE_LOAD_BALANCING                                             = 41,
            TYPE_KILL_SESSION                                               = 42,
            TYPE_SUBMIT_PURCHASE_RECEIPT                                    = 43,
            TYPE_CREDIT_CARD_STORE                                          = 44,
            TYPE_UPGRADE_ACCOUNT                                            = 45,
            TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS                            = 46,
            TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS                           = 47,
            TYPE_GET_SESSION_TRANSFER_URL                                   = 48,
            TYPE_GET_PAYMENT_METHODS                                        = 49,
            TYPE_INVITE_CONTACT                                             = 50,
            TYPE_REPLY_CONTACT_REQUEST                                      = 51,
            TYPE_SUBMIT_FEEDBACK                                            = 52,
            TYPE_SEND_EVENT                                                 = 53,
            TYPE_CLEAN_RUBBISH_BIN                                          = 54,
            TYPE_SET_ATTR_NODE                                              = 55,
            TYPE_CHAT_CREATE                                                = 56,
            TYPE_CHAT_FETCH                                                 = 57,
            TYPE_CHAT_INVITE                                                = 58,
            TYPE_CHAT_REMOVE                                                = 59,
            TYPE_CHAT_URL                                                   = 60,
            TYPE_CHAT_GRANT_ACCESS                                          = 61,
            TYPE_CHAT_REMOVE_ACCESS                                         = 62,
            TYPE_USE_HTTPS_ONLY                                             = 63,
            TYPE_SET_PROXY                                                  = 64,
            TYPE_GET_RECOVERY_LINK                                          = 65,
            TYPE_QUERY_RECOVERY_LINK                                        = 66,
            TYPE_CONFIRM_RECOVERY_LINK                                      = 67,
            TYPE_GET_CANCEL_LINK                                            = 68,
            TYPE_CONFIRM_CANCEL_LINK                                        = 69,
            TYPE_GET_CHANGE_EMAIL_LINK                                      = 70,
            TYPE_CONFIRM_CHANGE_EMAIL_LINK                                  = 71,
            TYPE_CHAT_UPDATE_PERMISSIONS                                    = 72,
            TYPE_CHAT_TRUNCATE                                              = 73,
            TYPE_CHAT_SET_TITLE                                             = 74,
            TYPE_SET_MAX_CONNECTIONS                                        = 75,
            TYPE_PAUSE_TRANSFER                                             = 76,
            TYPE_MOVE_TRANSFER                                              = 77,
            TYPE_CHAT_PRESENCE_URL                                          = 78,
            TYPE_REGISTER_PUSH_NOTIFICATION                                 = 79,
            TYPE_GET_USER_EMAIL                                             = 80,
            TYPE_APP_VERSION                                                = 81,
            TYPE_GET_LOCAL_SSL_CERT                                         = 82,
            TYPE_SEND_SIGNUP_LINK                                           = 83,
            TYPE_QUERY_DNS                                                  = 84,
            TYPE_QUERY_GELB                                                 = 85,   // (obsolete)
            TYPE_CHAT_STATS                                                 = 86,
            TYPE_DOWNLOAD_FILE                                              = 87,
            TYPE_QUERY_TRANSFER_QUOTA                                       = 88,
            TYPE_PASSWORD_LINK                                              = 89,
            TYPE_GET_ACHIEVEMENTS                                           = 90,
            TYPE_RESTORE                                                    = 91,
            TYPE_REMOVE_VERSIONS                                            = 92,
            TYPE_CHAT_ARCHIVE                                               = 93,
            TYPE_WHY_AM_I_BLOCKED                                           = 94,
            TYPE_CONTACT_LINK_CREATE                                        = 95,
            TYPE_CONTACT_LINK_QUERY                                         = 96,
            TYPE_CONTACT_LINK_DELETE                                        = 97,
            TYPE_FOLDER_INFO                                                = 98,
            TYPE_RICH_LINK                                                  = 99,
            TYPE_KEEP_ME_ALIVE                                              = 100,
            TYPE_MULTI_FACTOR_AUTH_CHECK                                    = 101,
            TYPE_MULTI_FACTOR_AUTH_GET                                      = 102,
            TYPE_MULTI_FACTOR_AUTH_SET                                      = 103,
            TYPE_ADD_SCHEDULED_COPY                                         = 104,
            TYPE_REMOVE_SCHEDULED_COPY                                      = 105,
            TYPE_TIMER                                                      = 106,
            TYPE_ABORT_CURRENT_SCHEDULED_COPY                               = 107,
            TYPE_GET_PSA                                                    = 108,
            TYPE_FETCH_TIMEZONE                                             = 109,
            TYPE_USERALERT_ACKNOWLEDGE                                      = 110,
            TYPE_CHAT_LINK_HANDLE                                           = 111,
            TYPE_CHAT_LINK_URL                                              = 112,
            TYPE_SET_PRIVATE_MODE                                           = 113,
            TYPE_AUTOJOIN_PUBLIC_CHAT                                       = 114,
            TYPE_CATCHUP                                                    = 115,
            TYPE_PUBLIC_LINK_INFORMATION                                    = 116,
            TYPE_GET_BACKGROUND_UPLOAD_URL                                  = 117,
            TYPE_COMPLETE_BACKGROUND_UPLOAD                                 = 118,
            TYPE_GET_CLOUD_STORAGE_USED                                     = 119,
            TYPE_SEND_SMS_VERIFICATIONCODE                                  = 120,
            TYPE_CHECK_SMS_VERIFICATIONCODE                                 = 121,
            TYPE_GET_REGISTERED_CONTACTS                                    = 122,
            TYPE_GET_COUNTRY_CALLING_CODES                                  = 123,
            TYPE_VERIFY_CREDENTIALS                                         = 124,
            TYPE_GET_MISC_FLAGS                                             = 125,
            TYPE_RESEND_VERIFICATION_EMAIL                                  = 126,
            TYPE_SUPPORT_TICKET                                             = 127,
            TYPE_SET_RETENTION_TIME                                         = 128,
            TYPE_RESET_SMS_VERIFIED_NUMBER                                  = 129,
            TYPE_SEND_DEV_COMMAND                                           = 130,
            TYPE_GET_BANNERS                                                = 131,
            TYPE_DISMISS_BANNER                                             = 132,
            TYPE_BACKUP_PUT                                                 = 133,
            TYPE_BACKUP_REMOVE                                              = 134,
            TYPE_BACKUP_PUT_HEART_BEAT                                      = 135,
            TYPE_FETCH_GOOGLE_ADS                                           = 136,  // deprecated
            TYPE_QUERY_GOOGLE_ADS                                           = 137,  // deprecated
            TYPE_GET_ATTR_NODE                                              = 138,
            TYPE_LOAD_EXTERNAL_DRIVE_BACKUPS                                = 139,
            TYPE_CLOSE_EXTERNAL_DRIVE_BACKUPS                               = 140,
            TYPE_GET_DOWNLOAD_URLS                                          = 141,
            TYPE_START_CHAT_CALL                                            = 142,
            TYPE_JOIN_CHAT_CALL                                             = 143,
            TYPE_END_CHAT_CALL                                              = 144,
            TYPE_GET_FA_UPLOAD_URL                                          = 145,
            TYPE_EXECUTE_ON_THREAD                                          = 146,
            TYPE_SET_CHAT_OPTIONS                                           = 147,
            TYPE_GET_RECENT_ACTIONS                                         = 148,
            TYPE_CHECK_RECOVERY_KEY                                         = 149,
            TYPE_SET_MY_BACKUPS                                             = 150,
            TYPE_PUT_SET                                                    = 151,
            TYPE_REMOVE_SET                                                 = 152,
            TYPE_FETCH_SET                                                  = 153,
            TYPE_PUT_SET_ELEMENT                                            = 154,
            TYPE_REMOVE_SET_ELEMENT                                         = 155,
            TYPE_REMOVE_OLD_BACKUP_NODES                                    = 156,
            TOTAL_OF_REQUEST_TYPES                                          = 157,
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
         * - MegaApi::removeSync - Returns the handle of the folder in MEGA
         * - MegaApi::upgradeAccount - Returns that handle of the product
         * - MegaApi::replyContactRequest - Returns the handle of the contact request
         * - MegaApi::inviteToChat - Returns the handle of the chat
         * - MegaApi::removeFromChat - Returns the handle of the chat
         * - MegaApi::getUrlChat - Returns the handle of the chat
         * - MegaApi::grantAccessInChat - Returns the handle of the node
         * - MegaApi::removeAccessInChat - Returns the handle of the node
         * - MegaApi::setScheduledCopy - Returns the target node of the backup
         * - MegaApi::updateBackup - Returns the target node of the backup
         * - MegaApi::sendBackupHeartbeat - Returns the last node backed up
         * - MegaApi::getChatLinkURL - Returns the public handle
         * - MegaApi::sendChatLogs - Returns the user handle
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
         * - MegaApi::copySyncDataToCache - Returns the path of the remote folder
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::exportNode - Returns the public link
         * - MegaApi::getPaymentId - Returns the payment identifier
         * - MegaApi::getUrlChat - Returns the user-specific URL for the chat
         * - MegaApi::getChatPresenceURL - Returns the user-specific URL for the chat presence server
         * - MegaApi::getUploadURL - Returns the upload IPv4
         * - MegaApi::getThumbnailUploadURL - Returns the upload IPv4
         * - MegaApi::getPreviewUploadURL - Returns the upload IPv4
         * - MegaApi::getDownloadUrl - Returns semicolon-separated IPv4 of the server in the URL(s)
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
         * - MegaApi::removeBackup - Returns the backupId
         * - MegaApi::updateBackup - Returns the backupId
         * - MegaApi::sendBackupHeartbeat - Returns the backupId
         * - MegaApi::syncFolder - Returns the backupId asociated with the sync
         * - MegaApi::copySyncDataToCache - Returns the backupId asociated with the sync
         * - MegaApi::getChatLinkURL - Returns the chatid
         * - MegaApi::sendChatLogs - Returns the callid (if exits)
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::syncFolder - Returns a fingerprint of the local folder
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
         * - MegaApi::createFolder - Returns the name of the new folder
         * - MegaApi::renameNode - Returns the new name for the node
         * - MegaApi::syncFolder - Returns the name for the sync
         * - MegaApi::copySyncDataToCache - Returns the name for the sync
         * - MegaApi::setScheduledCopy - Returns the device id hash of the backup source device
         * - MegaApi::updateBackup - Returns the device id hash of the backup source device
         * - MegaApi::getUploadURL - Returns the upload URL
         * - MegaApi::getThumbnailUploadURL - Returns the upload URL
         * - MegaApi::getPreviewUploadURL - Returns the upload URL
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::querySignupLink - Returns the name of the user
         * - MegaApi::confirmAccount - Returns the name of the user
         * - MegaApi::fastConfirmAccount - Returns the name of the user
         * - MegaApi::getUserData - Returns the name of the user
         * - MegaApi::getDownloadUrl - Returns semicolon-separated download URL(s) to the file
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
         * - MegaApi::loginToFolder - Returns the authentication key to write in public folder
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
         * - MegaApi::fastConfirmAccount - Returns the base64pwKey parameter
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getUserData - Returns the private RSA key of the account, Base64-encoded
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
         * - MegaApi::setScheduledCopy - Returns the backup state
         * - MegaApi::updateBackup - Returns the backup state
         * - MegaApi::sendBackupHeartbeat - Returns the backup state
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
         * - MegaApi::setScheduledCopy - Returns the path of the local folder
         * - MegaApi::updateBackup - Returns the path of the local folder
         *
         * @return Path of a file related to the request
         */
        virtual const char* getFile() const;

        /**
         * @brief Return the number of times that a request has temporarily failed
         * @return Number of times that a request has temporarily failed
         * This value is valid for these requests:
         * - MegaApi::setScheduledCopy - Returns the maximun number of backups to keep
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
         * - MegaApi::dismissBanner - Returns the id of the banner
         * - MegaApi::sendBackupHeartbeat - Returns the number of backup files uploaded
         * - MegaApi::getRecentActions - Returns the maximum number of nodes
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
         * - MegaApi::setScheduledCopy - Returns the cron like time string to define period
         * - MegaApi::getUploadURL - Returns the upload IPv6
         * - MegaApi::getThumbnailUploadURL - Returns the upload IPv6
         * - MegaApi::getPreviewUploadURL - Returns the upload IPv6
         * - MegaApi::getDownloadUrl - Returns semicolon-separated IPv6 of the server in the URL(s)
         * - MegaApi::startChatCall - Returns the url sfu
         * - MegaApi::joinChatCall - Returns the url sfu
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
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
         * - MegaApi::setScheduledCopy - Returns the period between backups in deciseconds (-1 if cron time used)
         * - MegaApi::abortCurrentScheduledCopy - Returns the tag of the aborted backup
         * - MegaApi::removeScheduledCopy - Returns the tag of the deleted backup
         * - MegaApi::startTimer - Returns the selected period
         * - MegaApi::sendChatStats - Returns the connection port
         * - MegaApi::dismissBanner - Returns the timestamp of the request
         * - MegaApi::sendBackupHeartbeat - Returns the time associated with the request
         * - MegaApi::createPublicChat - Returns if chat room is a meeting room
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::creditCardQuerySubscriptions - Returns the number of credit card subscriptions
         * - MegaApi::getPaymentMethods - Returns a bitfield with the available payment methods
         * - MegaApi::getCloudStorageUsed - Returns the sum of the sizes of file cloud nodes.
         * - MegaApi::getRecentActions - Returns the number of days since nodes will be considerated
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
         * - MegaApi::exportNode - Makes the folder writable
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
         * - MegaApi::setBackup - Returns if backups that should have happen in the past should be taken care of
         * - MegaApi::getChatLinkURL - Returns a vector with one element (callid), if call doesn't exit it will be NULL
         * - MegaApi::setScheduledCopy - Returns if backups that should have happen in the past should be taken care of
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
         *
         * In addition, this value is also valid for these requests:
         * - MegaApi::setScheduledCopy - Returns the backup type
         * - MegaApi::updateBackup - Returns the backup type
         *
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
         * @brief Returns currency data related to prices
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getPricing - Returns available pricing plans to upgrade a MEGA account
         *
         * You take the ownership of the returned value.
         *
         * @return Currency data related to prices
         */
        virtual MegaCurrency *getCurrency() const;

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
         * @brief Get details about timezones and the current default
         *
         * This value is valid for these request in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::fetchTimeZone - Details about timezones and the current default
         *
         * In any other case, this function returns NULL.
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return Details about timezones and the current default
         */
        virtual MegaTimeZoneDetails *getMegaTimeZoneDetails() const;

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
         * - MegaApi::setScheduledCopy - Returns the tag asociated with the backup
         * - MegaApi::sendBackupHeartbeat - Returns the number of backup files downloaded
         *
         * @return Tag of a transfer related to the request
         */
        virtual int getTransferTag() const;

        /**
         * @brief Returns the number of details related to this request
         *
         * This value is valid for these requests:
         *  - MegaApi::getAccountDetails
         *  - MegaApi::getSpecificAccountDetails
         *  - MegaApi::getExtendedAccountDetails
         *  - MegaApi::disableSync
         *  - MegaApi::removeSync
         *  - MegaApi::enableSync
         *  - MegaApi::syncFolder
         *  - MegaApi::setScheduledCopy
         *  - MegaApi::updateBackup
         *  - MegaApi::sendBackupHeartbeat
         *
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

        /**
         * @brief Returns the string list map
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return String list map
         */
        virtual MegaStringListMap* getMegaStringListMap() const;

        /**
         * @brief Returns the string table
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return String table
         */
        virtual MegaStringTable* getMegaStringTable() const;

        /**
         * @brief Returns information about the contents of a folder
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getFolderInfo - Returns the information related to the folder
         *
         * @return Object with information about the contents of a folder
         */
        virtual MegaFolderInfo *getMegaFolderInfo() const;

        /**
         * @brief Returns settings for push notifications
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getPushNotificationSettings - Returns settings for push notifications
         *
         * @return Object with settings for push notifications
         */
        virtual const MegaPushNotificationSettings *getMegaPushNotificationSettings() const;

        /**
         * @brief Returns information about background uploads (used in iOS)
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for requests relating to background uploads. The returned
         * pointer is to the relevant background upload object.
         *
         * @return Object with information about the contents of a folder
         */
        virtual MegaBackgroundMediaUpload* getMegaBackgroundMediaUploadPtr() const;

        /**
         * @brief Returns the list of all Smart Banners available for current user
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaApi::getBanners - Requests all Smart Banners available for current user
         *
         * @return List of all Smart Banners available for current user
         */
        virtual MegaBannerList* getMegaBannerList() const;

        /**
         * @brief Returns the string list
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * @return String list
         */
        virtual MegaStringList* getMegaStringList() const;

        /**
         * @brief Returns the MegaHandle list
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::getFavourites - A list of MegaHandle objects
         * - MegaApi::getChatLinkURL - Returns a vector with the callid (if call exits in other case it will be NULL)
         *
         * @return MegaHandle list
         */
        virtual MegaHandleList* getMegaHandleList() const;

        /**
         * @brief Returns the recent actions bucket list
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::getRecentActions
         *
         * @return MegaRecentActionBucketList list
         */
        virtual MegaRecentActionBucketList *getRecentActions() const;

        /**
         * @brief Returns a MegaSet explicitly fetched from online API (typically using 'aft' command)
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::fetchSet
         *
         * @return requested MegaSet or null if not found
         */
        virtual MegaSet* getMegaSet() const;

        /**
         * @brief Returns the list of elements, part of the MegaSet explicitly fetched from online API (typically using 'aft' command)
         *
         * The SDK retains the ownership of the returned value. It will be valid until
         * the MegaRequest object is deleted.
         *
         * This value is valid for these requests:
         * - MegaApi::fetchSet
         *
         * @return lis of elements in the requested MegaSet, or null if Set not found
         */
        virtual MegaSetElementList* getMegaSetElementList() const;
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
        EVENT_COMMIT_DB                 = 0,
        EVENT_ACCOUNT_CONFIRMATION      = 1,
        EVENT_CHANGE_TO_HTTPS           = 2,
        EVENT_DISCONNECT                = 3,
        EVENT_ACCOUNT_BLOCKED           = 4,
        EVENT_STORAGE                   = 5,
        EVENT_NODES_CURRENT             = 6,
        EVENT_MEDIA_INFO_READY          = 7,
        EVENT_STORAGE_SUM_CHANGED       = 8,
        EVENT_BUSINESS_STATUS           = 9,
        EVENT_KEY_MODIFIED              = 10,
        EVENT_MISC_FLAGS_READY          = 11,
#ifdef ENABLE_SYNC
        //EVENT_FIRST_SYNC_RESUMING       = 12, // (deprecated) when a first sync is about to be resumed
        EVENT_SYNCS_DISABLED            = 13, // Syncs were bulk-disabled due to a situation encountered, eg storage overquota
        EVENT_SYNCS_RESTORED            = 14, // Indicate to the app that the process of starting existing syncs after login+fetchnodes is complete.
#endif
        EVENT_REQSTAT_PROGRESS          = 15, // Provides the per mil progress of a long-running API operation in MegaEvent::getNumber,
                                              // or -1 if there isn't any operation in progress.
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

    /**
     * @brief Returns a number relative to this event
     *
     * For event EVENT_STORAGE_SUM_CHANGED, this number is the new storage sum.
     *
     * For event EVENT_REQSTAT_PROGRESS, this number is the per mil progress of
     * a long-running API operation, or -1 if there isn't any operation in progress.
     *
     * @return Number relative to this event
     */
    virtual int64_t getNumber() const;

    /**
     * @brief Returns the handle relative to this event
     * @return Handle relative to this event
     */
    virtual MegaHandle getHandle() const;

    /**
     * @brief Returns a readable description of the event
     *
     * This function returns a pointer to a statically allocated buffer.
     * You don't have to free the returned pointer
     *
     * @return Readable description of the event
     */
    virtual const char* getEventString() const;
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
            TYPE_UPLOAD = 1,
            TYPE_LOCAL_TCP_DOWNLOAD = 2,
            TYPE_LOCAL_HTTP_DOWNLOAD = 2 //kept for backwards compatibility
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

        enum {
            STAGE_NONE = 0,
            STAGE_SCAN,
            STAGE_CREATE_TREE,
            STAGE_TRANSFERRING_FILES,
            STAGE_MAX = STAGE_TRANSFERRING_FILES,
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
         *  except when uploading files for support: it will return the support account then.
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
         * @brief Returns the current stage in case this transfer represents a folder upload/download operation.
         * This method can return the following values:
         *  - MegaTransfer::STAGE_SCAN                      = 1
         *  - MegaTransfer::STAGE_CREATE_TREE               = 2
         *  - MegaTransfer::STAGE_TRANSFERRING_FILES        = 3
         * Any other returned value, must be ignored.
         *
         * The value returned by this method, can only be considered as valid, when we receive MegaTransferListener::onTransferUpdate
         * or MegaListener::onTransferUpdate, and the returned value is in between the range specified above.
         *
         * Note: any specific stage can only be notified once at most.
         *
         * @deprecated use the stage in the onFolderTransferUpdate callback instead
         *
         * @return The current stage for a folder upload/download operation
         */
        virtual unsigned getStage() const;

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
         * @brief Returns true if this transfer belongs to the backups engine
         *
         * This data is important to know if the transfer will resume when enableTransferResumption is called.
         * Regular transfers are resumed, but backup transfers aren't.
         *
         * @return true if this transfer belongs to the backups engine, otherwise false
         */
        virtual bool isBackupTransfer() const;

        /**
         * @brief Returns true if the transfer has failed with API_EOVERQUOTA
         * and the target is foreign.
         *
         * @return true if the transfer has failed with API_EOVERQUOTA and the target is foreign.
         */
        virtual bool isForeignOverquota() const;

        /**
         * @brief Returns true if the transfer may force a new upload.
         *
         * @return true if the transfer can force a new upload.
         */
        virtual bool isForceNewUpload() const;

        /**
         * @brief Returns true is this is a streaming transfer
         * @return true if this is a streaming transfer, false otherwise
         * @see MegaApi::startStreaming
         */
        virtual bool isStreamingTransfer() const;

        /**
         * @brief Returns true is the transfer is at finished state (COMPLETED, CANCELLED OR FAILED)
         * @return true if this transfer is finished, false otherwise
         */
        virtual bool isFinished() const;

        /**
         * @brief Returns the received bytes since the last callback
         *
         * The returned value is only valid for streaming transfers (MegaApi::startStreaming).
         *
         * @return Received bytes since the last callback
         */
        virtual char *getLastBytes() const;

        /**
         * @brief Returns the last error related to the transfer
         *
         * @note This method returns a MegaError with the error code, but
         * the extra info is not valid. If you need to use MegaError::getUserStatus, in
         * example, you need to use MegaTransfer::getLastErrorExtended.
         *
         * @deprecated User use MegaTransfer::getLastErrorExtended.
         *
         * @return Last error related to the transfer
         */
        virtual MegaError getLastError() const;

        /**
         * @brief Returns the last error related to the transfer with extra info
         *
         * The MegaTransfer object retains the ownership of the returned pointer. It will
         * be valid until the deletion of the MegaTransfer object.
         *
         * @return Last error related to the transfer, with extended info
         */
        virtual const MegaError* getLastErrorExtended() const;

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

        /**
         * @brief Returns whether the target folder of the transfer was overriden by the API server
         *
         * It may happen that the target folder fo a transfer is deleted by the time the node
         * is going to be added. Hence, the API will create the node in the rubbish bin.
         *
         * @return True if target folder was overriden (apps can check the final parent)
         */
        virtual bool getTargetOverride() const;

        /**
         * @brief Returns a pointer to the cancel token associated to a MegaTransfer in case it exists.
         *
         * CancelToken can be used to cancel a batch of transfers (upload or download) that contains at least one folder.
         *
         * When user wants to upload/download a batch of items that at least contains one folder, SDK mutex will be partially
         * locked until:
         *  - we have received onTransferStart for every file in the batch
         *  - we have received onTransferUpdate with MegaTransfer::getStage == MegaTransfer::STAGE_TRANSFERRING_FILES
         *    for every folder in the batch
         *
         * During this period, the only safe method (to avoid deadlocks) to cancel transfers is by calling CancelToken::cancel(true).
         * This method will cancel all transfers(not finished yet).
         *
         * Important considerations:
         *  - A cancel token instance can be shared by multiple transfers, and calling CancelToken::cancel(true) will affect all
         *    of those transfers.
         *
         * @return A pointer to a cancelToken instance associated to the transfer in case it exists
         */
        virtual MegaCancelToken* getCancelToken();

        /**
         * @brief Returns a string that identify the recursive operation stage
         *
         * @return A string that identify the recursive operation stage
         */
        static const char* stageToString(unsigned stage);
};

/**
 * @brief Provides information about the contents of a folder
 *
 * This object is related to provide the results of the function MegaApi::getFolderInfo
 *
 * Objects of this class aren't live, they are snapshots of the state of the contents of the
 * folder when the object is created, they are immutable.
 *
 */
class MegaFolderInfo
{
public:
    virtual ~MegaFolderInfo();

    /**
     * @brief Creates a copy of this MegaFolderInfo object
     *
     * The resulting object is fully independent of the source MegaFolderInfo,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaFolderInfo object
     */
    virtual MegaFolderInfo *copy() const;

    /**
     * @brief Return the number of file versions inside the folder
     *
     * The current version of files is not taken into account for the return value of this function
     *
     * @return Number of file versions inside the folder
     */
    virtual int getNumVersions() const;

    /**
     * @brief Returns the number of files inside the folder
     *
     * File versions are not counted for the return value of this function
     *
     * @return Number of files inside the folder
     */
    virtual int getNumFiles() const;

    /**
     * @brief Returns the number of folders inside the folder
     * @return Number of folders inside the folder
     */
    virtual int getNumFolders() const;

    /**
     * @brief Returns the total size of files inside the folder
     *
     * File versions are not taken into account for the return value of this function
     *
     * @return Total size of files inside the folder
     */
    virtual long long getCurrentSize() const;

    /**
     * @brief Returns the total size of file versions inside the folder
     *
     * The current version of files is not taken into account for the return value of this function
     *
     * @return Total size of file versions inside the folder
     */
    virtual long long getVersionsSize() const;
};

/**
 * @brief Provides information about timezones and the current default
 *
 * This object is related to results of the function MegaApi::fetchTimeZone
 *
 * Objects of this class aren't live, they contain details about timezones and the
 * default when the object is created, they are immutable.
 *
 */
class MegaTimeZoneDetails
{
public:
    virtual ~MegaTimeZoneDetails();

    /**
     * @brief Creates a copy of this MegaTimeZoneDetails object
     *
     * The resulting object is fully independent of the source MegaTimeZoneDetails,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaTimeZoneDetails object
     */
    virtual MegaTimeZoneDetails *copy() const;

    /**
     * @brief Returns the number of timezones in this object
     *
     * @return Number of timezones in this object
     */
    virtual int getNumTimeZones() const;

    /**
     * @brief Returns the timezone at an index
     *
     * The MegaTimeZoneDetails object retains the ownership of the returned string.
     * It will be only valid until the MegaTimeZoneDetails object is deleted.
     *
     * @param index Index in the list (it must be lower than MegaTimeZoneDetails::getNumTimeZones)
     * @return Timezone at an index
     */
    virtual const char *getTimeZone(int index) const;

    /**
     * @brief Returns the current time offset of the time zone at an index, respect to UTC (in seconds, it can be negative)
     *
     * @param index Index in the list (it must be lower than MegaTimeZoneDetails::getNumTimeZones)
     * @return Current time offset of the time zone at an index, respect to UTC (in seconds, it can be negative)
     * @see MegaTimeZoneDetails::getTimeZone
     */
    virtual int getTimeOffset(int index) const;

    /**
     * @brief Get the default time zone index
     *
     * If there isn't any good default known, this function will return -1
     *
     * @return Default time zone index, or -1 if there isn't a good default known
     */
    virtual int getDefault() const;
};

/**
 * @brief Provides information about the notification settings
 *
 * The notifications can be configured:
 *
 * 1. Globally
 *  1.1. Mute all notifications
 *  1.2. Notify only during a schedule: from one time to another time of the day, specifying the timezone of reference
 *  1.3. Do Not Disturb for a period of time: it overrides the schedule, if any (no notification will be generated)
 *
 * 2. Chats: Mute for all chats notifications
 *
 * 3. Per chat:
 *  2.1. Mute all notifications from the specified chat
 *  2.2. Always notify for the specified chat
 *  2.3. Do Not Disturb for a period of time for the specified chat
 *
 * @note Notification settings per chat override any global notification setting.
 * @note The DND mode per chat is not compatible with the option to always notify and viceversa.
 *
 * 4. Contacts: new incoming contact request, outgoing contact request accepted...
 * 5. Shared folders: new shared folder, access removed...
 *
 */
class MegaPushNotificationSettings
{
protected:
    MegaPushNotificationSettings();

public:

    /**
     * @brief Creates a new instance of MegaPushNotificationSettings
     * @return A pointer to the superclass of the private object
     */
    static MegaPushNotificationSettings *createInstance();

    virtual ~MegaPushNotificationSettings();

    /**
     * @brief Creates a copy of this MegaPushNotificationSettings object
     *
     * The resulting object is fully independent of the source MegaPushNotificationSettings,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaPushNotificationSettings object
     */
    virtual MegaPushNotificationSettings *copy() const;

    /**
     * @brief Returns whether notifications are globaly enabled or not
     *
     * The purpose of this method is to control the UI in order to enable
     * the modification of the global parameters (dnd & schedule) or not.
     *
     * @return True if notifications are enabled, false if disabled
     *
     * @deprecated This method is deprecated, use isGlobalDndEnabled instead of this.
     * Note that isGlobalDndEnabled returns the opposite value to isGlobalEnabled
     */
    virtual bool isGlobalEnabled() const;

    /**
     * @brief Returns whether Do-Not-Disturb mode is enabled or not
     * @return True if enabled, false otherwise
     */
    virtual bool isGlobalDndEnabled() const;

    /**
     * @brief Returns whether Do-Not-Disturb mode for chats is enabled or not

     * @return True if enabled, false otherwise
     */
    virtual bool isGlobalChatsDndEnabled() const;

    /**
     * @brief Returns the timestamp until the DND mode is enabled
     *
     * This method returns a valid value only if MegaPushNotificationSettings::isGlobalDndEnabled
     * returns true.
     *
     * If there's no DND mode established, this function returns -1.
     * @note a DND value of 0 means the DND does not expire.
     *
     * @return Timestamp until DND mode is enabled (in seconds since the Epoch)
     */
    virtual int64_t getGlobalDnd() const;

    /**
     * @brief Returns whether there is a schedule for notifications or not
     * @return True if enabled, false otherwise
     */
    virtual bool isGlobalScheduleEnabled() const;

    /**
     * @brief Returns the time of the day when notifications start
     *
     * This method returns a valid value only if MegaPushNotificationSettings::isGlobalScheduleEnabled
     * returns true.
     *
     * @return Minutes counting from 00:00 (based on the configured timezone)
     */
    virtual int getGlobalScheduleStart() const;

    /**
     * @brief Returns the time of the day when notifications stop
     *
     * This method returns a valid value only if MegaPushNotificationSettings::isGlobalScheduleEnabled
     * returns true.
     *
     * @return Minutes counting from 00:00 (based on the configured timezone)
     */
    virtual int getGlobalScheduleEnd() const;

    /**
     * @brief Returns the timezone of reference for the notification schedule
     *
     * This method returns a valid value only if MegaPushNotificationSettings::isGlobalScheduleEnabled
     * returns true.
     *
     * You take the ownership of the returned value
     *
     * @return Minutes counting from 00:00 (based on the configured timezone)
     */
    virtual const char *getGlobalScheduleTimezone() const;

    /**
     * @brief Returns whether notifications for a chat are enabled or not
     *
     * The purpose of this method is to control the UI in order to enable
     * the modification of the chat parameters (dnd & always notify) or not.
     *
     * @param chatid MegaHandle that identifies the chat room
     * @return True if enabled, false otherwise
     *
     * @deprecated This method is deprecated, use isChatDndEnabled instead of this.
     * Note that isChatDndEnabled returns the opposite value to isChatEnabled
     */
    virtual bool isChatEnabled(MegaHandle chatid) const;

    /**
     * @brief Returns whether Do-Not-Disturb mode for a chat is enabled or not
     *
     * @param chatid MegaHandle that identifies the chat room
     * @return True if enabled, false otherwise
     */
    virtual bool isChatDndEnabled(MegaHandle chatid) const;

    /**
     * @brief Returns the timestamp until the Do-Not-Disturb mode for a chat
     *
     * This method returns a valid value only if MegaPushNotificationSettings::isChatDndEnabled
     * returns true.
     *
     * If there's no DND mode established for the specified chat, this function returns -1.
     * @note a DND value of 0 means the DND does not expire.
     *
     * @param chatid MegaHandle that identifies the chat room
     * @return Timestamp until DND mode is enabled (in seconds since the Epoch)
     */
    virtual int64_t getChatDnd(MegaHandle chatid) const;

    /**
     * @brief Returns whether always notify for a chat or not
     *
     * This option overrides the global notification settings.
     *
     * @param chatid MegaHandle that identifies the chat room
     * @return True if enabled, false otherwise
     */
    virtual bool isChatAlwaysNotifyEnabled(MegaHandle chatid) const;

    /**
     * @brief Returns whether notifications about Contacts are enabled or not
     * @return True if enabled, false otherwise
     */
    virtual bool isContactsEnabled() const;

    /**
     * @brief Returns whether notifications about shared-folders are enabled or not
     * @return True if enabled, false otherwise
     */
    virtual bool isSharesEnabled() const;

    /**
     * @brief Returns whether notifications about chats are enabled or not
     * @return True if enabled, false otherwise
     *
     * @deprecated This method is deprecated, use isGlobalChatsDndEnabled instead of this.
     * Note that isGlobalChatsDndEnabled returns the opposite result to isChatsEnabled;
     */
    virtual bool isChatsEnabled() const;

    /**
     * @brief Returns the timestamp until the chats DND mode is enabled
     *
     * This method returns a valid value only if MegaPushNotificationSettings::isGlobalChatsDndEnabled
     * returns true.
     *
     * If there's no DND mode established, this function returns -1.
     * @note a DND value of 0 means the DND does not expire.
     *
     * @return Timestamp until chats DND mode is enabled (in seconds since the Epoch)
     */
    virtual int64_t getGlobalChatsDnd() const;

    /**
     * @brief Enable or disable notifications globally
     *
     * If notifications are globally disabled, the DND global setting will be
     * cleared and the specified schedule, if any, will have no effect.
     *
     * @note When notifications are globally disabled, settings per chat still apply.
     *
     * @param enable True to enable, false to disable
     */
    virtual void enableGlobal(bool enable);

    /**
     * @brief Set the global DND mode for a period of time
     *
     * No notifications will be generated until the specified timestamp.
     *
     * If notifications were globally disabled, this function will enable them
     * back (but will not generate notification until the specified timestamp).
     *
     * @param timestamp Timestamp until DND mode is enabled (in seconds since the Epoch)
     */
    virtual void setGlobalDnd(int64_t timestamp);

    /**
     * @brief Disable the globally specified DND mode
     */
    virtual void disableGlobalDnd();

    /**
     * @brief Set the schedule for notifications globally
     *
     * Notifications, if globally enabled, will be generated only from \c start
     * to \c end time, using the \c timezone as reference.
     *
     * The timezone should be one of the values returned by MegaTimeZoneDetails::getTimeZone.
     * @see MegaApi::fetchTimeZone for more details.
     *
     * @param start Minutes counting from 00:00 (based on the configured timezone)
     * @param end Minutes counting from 00:00 (based on the configured timezone)
     * @param timezone C-String representing the timezone
     */
    virtual void setGlobalSchedule(int start, int end, const char *timezone);

    /**
     * @brief Disable the schedule for notifications globally
     */
    virtual void disableGlobalSchedule();

    /**
     * @brief Enable or disable notifications for a chat
     *
     * If notifications for this chat are disabled, the DND settings for this chat,
     * if any, will be cleared.
     *
     * @note Settings per chat override any global notification setting.
     *
     * @param chatid MegaHandle that identifies the chat room
     * @param enable True to enable, false to disable
     */
    virtual void enableChat(MegaHandle chatid, bool enable);

    /**
     * @brief Set the DND mode for a chat for a period of time
     *
     * No notifications will be generated until the specified timestamp.
     *
     * This setting is not compatible with the "Always notify". If DND mode is
     * configured, the "Always notify" will be disabled.
     *
     * If chat notifications were totally disabled for the specified chat, this
     * function will enable them back (but will not generate notification until
     * the specified timestamp).
     *
     * @param timestamp Timestamp until DND mode is enabled (in seconds since the Epoch)
     */
    virtual void setChatDnd(MegaHandle chatid, int64_t timestamp);

    /**
     * @brief Set the Global DND for chats for a period of time
     *
     * No chat notifications will be generated until the specified timestamp.
     *
     * @param timestamp Timestamp until DND mode is enabled (in seconds since the Epoch)
     */
    virtual void setGlobalChatsDnd(int64_t timestamp);

    /**
     * @brief Enable or disable "Always notify" setting
     *
     * Notifications for this chat will always be generated, even if they are globally
     * disabled, out of the global schedule or a global DND mode is set.
     *
     * This setting is not compatible with the DND mode for the specified chat. In consequence,
     * if "Always notify" is enabled and the DND mode was configured, it will be disabled.
     * Also, if notifications were disabled for the specified chat, they will be enabled.
     *
     * @note Settings per chat override any global notification setting.
     *
     * @param chatid MegaHandle that identifies the chat room
     * @param enable True to enable, false to disable
     */
    virtual void enableChatAlwaysNotify(MegaHandle chatid, bool enable);

    /**
     * @brief Enable or disable notifications related to contacts
     * @param enable True to enable, false to disable
     */
    virtual void enableContacts(bool enable);

    /**
     * @brief Enable or disable notifications related to shared-folders
     * @param enable True to enable, false to disable
     */
    virtual void enableShares(bool enable);

    /**
     * @brief Enable or disable notifications related to all chats
     * @param enable True to enable, false to disable
     */
    virtual void enableChats(bool enable);
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

    /**
     * @brief Returns true is the incoming contact request is being automatically accepted
     * @return True if the incoming contact request is being automatically accepted
     */
    virtual bool isAutoAccepted() const;
};


#ifdef ENABLE_SYNC

/**
 * @brief Provides information about a synchronization
 */
class MegaSync
{
public:

    enum Error
    {
        NO_SYNC_ERROR = 0,
        UNKNOWN_ERROR = 1,
        UNSUPPORTED_FILE_SYSTEM = 2, //File system type is not supported
        INVALID_REMOTE_TYPE = 3, //Remote type is not a folder that can be synced
        INVALID_LOCAL_TYPE = 4, //Local path does not refer to a folder
        INITIAL_SCAN_FAILED = 5, //The initial scan failed
        LOCAL_PATH_TEMPORARY_UNAVAILABLE = 6, //Local path is temporarily unavailable: this is fatal when adding a sync
        LOCAL_PATH_UNAVAILABLE = 7, //Local path is not available (can't be open)
        REMOTE_NODE_NOT_FOUND = 8, //Remote node does no longer exists
        STORAGE_OVERQUOTA = 9, //Account reached storage overquota
        ACCOUNT_EXPIRED = 10, //Account expired (business or pro flexi)
        FOREIGN_TARGET_OVERSTORAGE = 11, //Sync transfer fails (upload into an inshare whose account is overquota)
        REMOTE_PATH_HAS_CHANGED = 12, // Remote path has changed (currently unused: not an error)
        //REMOTE_PATH_DELETED = 13, // (obsolete -> unified with REMOTE_NODE_NOT_FOUND) Remote path has been deleted
        SHARE_NON_FULL_ACCESS = 14, //Existing inbound share sync or part thereof lost full access
        LOCAL_FILESYSTEM_MISMATCH = 15, //Filesystem fingerprint does not match the one stored for the synchronization
        PUT_NODES_ERROR = 16, // Error processing put nodes result
        ACTIVE_SYNC_BELOW_PATH = 17, // There's a synced node below the path to be synced
        ACTIVE_SYNC_ABOVE_PATH = 18, // There's a synced node above the path to be synced
        REMOTE_NODE_MOVED_TO_RUBBISH = 19, // Moved to rubbish
        REMOTE_NODE_INSIDE_RUBBISH = 20, // Attempted to be added in rubbish
        VBOXSHAREDFOLDER_UNSUPPORTED = 21, // Found unsupported VBoxSharedFolderFS
        LOCAL_PATH_SYNC_COLLISION = 22, //Local path includes a synced path or is included within one
        ACCOUNT_BLOCKED= 23, // Account blocked
        UNKNOWN_TEMPORARY_ERROR = 24, // unknown temporary error
        TOO_MANY_ACTION_PACKETS = 25, // Too many changes in account, local state discarded
        LOGGED_OUT = 26, // Logged out
        WHOLE_ACCOUNT_REFETCHED = 27, // The whole account was reloaded, missed actionpacket changes could not have been applied
        MISSING_PARENT_NODE = 28, // Setting a new parent to a parent whose LocalNode is missing its corresponding Node crossref
        BACKUP_MODIFIED = 29, // Backup has been externally modified.
        BACKUP_SOURCE_NOT_BELOW_DRIVE = 30,     // Backup source path not below drive path.
        SYNC_CONFIG_WRITE_FAILURE = 31,         // Unable to write sync config to disk.
        ACTIVE_SYNC_SAME_PATH = 32,             // There's a synced node at the path to be synced
        COULD_NOT_MOVE_CLOUD_NODES = 33,        // rename() failed
        COULD_NOT_CREATE_IGNORE_FILE = 34,      // Couldn't create a sync's initial ignore file.
        SYNC_CONFIG_READ_FAILURE = 35,          // Couldn't read sync configs from disk.
        UNKNOWN_DRIVE_PATH = 36,                // Sync's drive path isn't known.
        INVALID_SCAN_INTERVAL = 37,             // The user's specified an invalid scan interval.
        NOTIFICATION_SYSTEM_UNAVAILABLE = 38,   // Filesystem notification subsystem has encountered an unrecoverable error.
        UNABLE_TO_ADD_WATCH = 39,               // Unable to add a filesystem watch.
        UNABLE_TO_RETRIEVE_ROOT_FSID = 40,      // Unable to retrieve a sync root's FSID.
        UNABLE_TO_OPEN_DATABASE = 41,           // Unable to open state cache database.
        INSUFFICIENT_DISK_SPACE = 42,           // Insufficient space for download.
    };

    enum Warning
    {
        NO_SYNC_WARNING = 0,
        LOCAL_IS_FAT = 1, // Found FAT (not a failure per se)
        LOCAL_IS_HGFS= 2, // Found HGFS (not a failure per se)
    };

    enum SyncAdded
    {
        NEW  = 1, //new sync added and activated
        FROM_CACHE = 2, // just restored from cache (keeping its former state: active if it was active)
        FROM_CACHE_FAILED_TO_RESUME = 3, // restored from cache, but activation failed: implies change in state
        FROM_CACHE_REENABLED  = 4, // restored from cache: reenabled after some failure: implies change in state
        REENABLED_FAILED = 5, //attempt to reenable lead to a failure: might not imply change in state, and does not change "active" state
        NEW_TEMP_DISABLED = 6, // new sync added as temporarily disabled due to a temporary error
    };

    enum SyncType
    {
        TYPE_UNKNOWN = 0x00,
        TYPE_UP = 0x01, // sync up from local to remote
        TYPE_DOWN = 0x02, // sync down from remote to local
        TYPE_TWOWAY = TYPE_UP | TYPE_DOWN, // Two-way sync
        TYPE_BACKUP, // special sync up from local to remote, automatically disabled when remote changed
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
     * the MegaSync object is deleted.
     *
     * @return Local folder that is being synced
     */
    virtual const char* getLocalFolder() const;

    /**
     * @brief Get the name of the sync
     *
     * When the app did not provide an specific name, it will return the leaf
     * name of the local folder.
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaSync object is deleted.
     *
     * @return Name given to the sync
     */
    virtual const char* getName() const;

    /**
     * @brief Get the last known path of the remote folder that is being synced
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaSync object is deleted.
     *
     * @return The path of the Remote folder from when it was last being synced
     */
    virtual const char* getLastKnownMegaFolder() const;

    /**
     * @brief Gets an unique identifier of the local filesystem that is being synced
     * @return Unique identifier of the local file system that is being synced
     */
    virtual long long getLocalFingerprint() const;

    /**
     * @brief Returns the identifier of this synchronization
     *
     * Identifiers of synchronizations are always negative numbers.
     *
     * @return Identifier of the synchronization
     */
    virtual MegaHandle getBackupId() const;

    /**
     * @brief Get the error of a synchronization
     *
     * Possible values are:
     *  - NO_SYNC_ERROR = 0: No error
     *  - UNKNOWN_ERROR = 1: Undefined error
     *  - UNSUPPORTED_FILE_SYSTEM = 2: File system type is not supported
     *  - INVALID_REMOTE_TYPE = 3: Remote type is not a folder that can be synced
     *  - INVALID_LOCAL_TYPE = 4: Local path does not refer to a folder
     *  - INITIAL_SCAN_FAILED = 5: The initial scan failed
     *  - LOCAL_PATH_TEMPORARY_UNAVAILABLE = 6: Local path is temporarily unavailable: this is fatal when adding a sync
     *  - LOCAL_PATH_UNAVAILABLE = 7: Local path is not available (can't be open)
     *  - REMOTE_NODE_NOT_FOUND = 8: Remote node does no longer exists
     *  - STORAGE_OVERQUOTA = 9: Account reached storage overquota
     *  - ACCOUNT_EXPIRED = 10: Account expired (business or pro flexi)
     *  - FOREIGN_TARGET_OVERSTORAGE = 11: Sync transfer fails (upload into an inshare whose account is overquota)
     *  - REMOTE_PATH_HAS_CHANGED = 12: Remote path changed
     *  - SHARE_NON_FULL_ACCESS = 14: Existing inbound share sync or part thereof lost full access
     *  - LOCAL_FILESYSTEM_MISMATCH = 15: Filesystem fingerprint does not match the one stored for the synchronization
     *  - PUT_NODES_ERROR = 16:  Error processing put nodes result
     *  - ACTIVE_SYNC_BELOW_PATH = 17: There's a synced node below the path to be synced
     *  - ACTIVE_SYNC_ABOVE_PATH = 18: There's a synced node above the path to be synced
     *  - REMOTE_NODE_MOVED_TO_RUBBISH = 19: Moved to rubbish
     *  - REMOTE_NODE_INSIDE_RUBBISH = 20: Attempted to be added in rubbish
     *  - VBOXSHAREDFOLDER_UNSUPPORTED = 21: Found unsupported VBoxSharedFolderFS
     *  - LOCAL_PATH_SYNC_COLLISION = 22: Local path includes a synced path or is included within one
     *  - ACCOUNT_BLOCKED = 23: Account blocked
     *  - UNKNOWN_TEMPORARY_ERROR = 24: Unknown temporary error
     *  - TOO_MANY_ACTION_PACKETS = 25: Too many changes in account, local state discarded
     *  - LOGGED_OUT = 26: Logged out
     *  - WHOLE_ACCOUNT_REFETCHED = 27: The whole account was reloaded, missed actionpacket changes could not have been applied
     *  - MISSING_PARENT_NODE = 28: Setting a new parent to a parent whose LocalNode is missing its corresponding Node crossref
     *  - BACKUP_MODIFIED = 29: Backup has been externally modified.
     *  - BACKUP_SOURCE_NOT_BELOW_DRIVE = 30: Backup source path not below drive path.
     *  - SYNC_CONFIG_WRITE_FAILURE = 31: Unable to write sync config to disk.
     *  - ACTIVE_SYNC_SAME_PATH = 32: There's a synced node at the path to be synced
     *
     * @return Error of a synchronization
     */
    virtual int getError() const;

    /**
     * @brief Get the warning of a synchronization
     *
     * Possible values are:
     *  - NO_SYNC_WARNING = 0: No warning
     *  - LOCAL_IS_FAT = 1: Found FAT (not a failure per se)
     *  - LOCAL_IS_HGFS = 2: Found HGFS (not a failure per se)
     *
     * @return Warning of a synchronization
     */
    virtual int getWarning() const;

    /**
     * @brief Get the type of sync
     *
     * See possible values in MegaSync::SyncType.
     *
     * @return Type of sync
     */
    virtual int getType() const;

    /**
     * @brief Returns if the sync is set as enabled by the user
     *
     * Notice that this will return true even when the sync is failed (fatal error)
     * or temporary disabled (transient error/circumstance).
     *
     * @return if the sync is set as enabled by the user
     */
    virtual bool isEnabled() const;

    /**
     * @brief Returns if the sync is active
     *
     * This means that the sync is expected to be working.
     *
     * It will be false if not disabled nor failed (nor being removed)
     *
     * @return If the sync is active
     */
    virtual bool isActive() const;

    /**
     * @brief Returns if the sync is temporary disabled (transient error/circumstance).
     *
     * @return If the sync is temporary disabled (transient error/circumstance).
     */
    virtual bool isTemporaryDisabled() const;


    /**
     * @brief Returns a readable description of the sync error
     *
     * This function returns a pointer to a statically allocated buffer.
     * You don't have to free the returned pointer
     *
     * @return Readable description of the error
     */
    const char * getMegaSyncErrorCode();

    /**
     * @brief Provides the error description associated with a sync error code
     *
     * This function returns a pointer to a statically allocated buffer.
     * You don't have to free the returned pointer
     *
     * @param errorCode Error code for which the description will be returned
     * @return Description associated with the error code
     */
    static const char* getMegaSyncErrorCode(int errorCode);

    /**
     * @brief Returns a readable description of the sync warning
     *
     * This function returns a pointer to a statically allocated buffer.
     * You don't have to free the returned pointer
     *
     * @return Readable description of the warning
     */
    const char * getMegaSyncWarningCode();

    /**
     * @brief Provides the warning description associated with a sync warning code
     *
     * This function returns a pointer to a statically allocated buffer.
     * You don't have to free the returned pointer
     *
     * @param warningCode Warning code for which the description will be returned
     * @return Description associated with the warning code
     */
    static const char *getMegaSyncWarningCode(int warningCode);

};


/**
 * @brief List of MegaSync objects
 *
 * A MegaSyncList has the ownership of the MegaSync objects that it contains, so they will be
 * only valid until the SyncList is deleted. If you want to retain a MegaMode returned by
 * a MegaSyncList, use MegaSync::copy.
 *
 * Objects of this class are immutable.
 *
 * @see MegaApi::getChildren, MegaApi::search, MegaApi::getInShares
 */
class MegaSyncList
{
    protected:
        MegaSyncList();

    public:
        /**
         * @brief Creates a new instance of MegaSyncList
         * @return A pointer to the superclass of the private object
         */
        static MegaSyncList * createInstance();

        virtual ~MegaSyncList();

        virtual MegaSyncList *copy() const;

        /**
         * @brief Returns the MegaSync at the position i in the MegaSyncList
         *
         * The MegaSyncList retains the ownership of the returned MegaSync. It will be only valid until
         * the MegaSyncList is deleted.
         *
         * If the index is >= the size of the list, this function returns NULL.
         *
         * @param i Position of the MegaSync that we want to get for the list
         * @return MegaSync at the position i in the list
         */
        virtual MegaSync* get(int i) const;

        /**
         * @brief Returns the number of MegaSync objects in the list
         * @return Number of MegaSync objects in the list
         */
        virtual int size() const;

        /**
         * @brief Add new sync to list
         * @param sync MegaSync to be added. The sync inserted is a copy from 'sync'
         */
        virtual void addSync(MegaSync* sync);
};



#endif


/**
 * @brief Provides information about a backup
 *
 * Developers can use listeners (MegaListener, MegaScheduledCopyListener)
 * to track the progress of each backup. MegaScheduledCopy objects are provided in callbacks sent
 * to these listeners and allow developers to know the state of the backups and their parameters
 * and their results.
 *
 * The implementation will receive callbacks from an internal worker thread.
 *
 **/
class MegaScheduledCopyListener
{
public:

    virtual ~MegaScheduledCopyListener();

    /**
     * @brief This function is called when the state of the backup changes
     *
     * The SDK calls this function when the state of the backup changes, for example
     * from 'active' to 'ongoing' or 'removing exceeding'.
     *
     * You can use MegaScheduledCopy::getState to get the new state.
     *
     * @param api MegaApi object that is backing up files
     * @param backup MegaScheduledCopy object that has changed the state
     */
    virtual void onBackupStateChanged(MegaApi *api, MegaScheduledCopy *backup);

    /**
     * @brief This function is called when a backup is about to start being processed
     *
     * The SDK retains the ownership of the backup parameter.
     * Don't use it after this functions returns.
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     */
    virtual void onBackupStart(MegaApi *api, MegaScheduledCopy *backup);

    /**
     * @brief This function is called when a backup has finished
     *
     * The SDK retains the ownership of the backup and error parameters.
     * Don't use them after this functions returns.
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * There won't be more callbacks about this backup.
     * The last parameter provides the result of the backup:
     * If the backup finished without problems,
     * the error code will be API_OK.
     * If some transfer failed, the error code will be API_EINCOMPLETE.
     * If the backup has been skipped the error code will be API_EEXPIRED.
     * If the backup folder cannot be found, the error will be API_ENOENT.
     *
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     * @param error Error information
     */
    virtual void onBackupFinish(MegaApi* api, MegaScheduledCopy *backup, MegaError* error);

    /**
     * @brief This function is called to inform about the progress of a backup
     *
     * The SDK retains the ownership of the backup parameter.
     * Don't use it after this functions returns.
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     *
     * @see MegaScheduledCopy::getTransferredBytes, MegaScheduledCopy::getSpeed
     */
    virtual void onBackupUpdate(MegaApi *api, MegaScheduledCopy *backup);

    /**
     * @brief This function is called when there is a temporary error processing a backup
     *
     * The backup continues after this callback, so expect more MegaScheduledCopyListener::onBackupTemporaryError or
     * a MegaScheduledCopyListener::onBackupFinish callback
     *
     * The SDK retains the ownership of the backup and error parameters.
     * Don't use them after this functions returns.
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     * @param error Error information
     */
    virtual void onBackupTemporaryError(MegaApi *api, MegaScheduledCopy *backup, MegaError* error);

};


/**
 * @brief Provides information about a backup
 */
class MegaScheduledCopy
{
public:
    enum
    {
        SCHEDULED_COPY_FAILED = -2,
        SCHEDULED_COPY_CANCELED = -1,
        SCHEDULED_COPY_INITIALSCAN = 0,
        SCHEDULED_COPY_ACTIVE,
        SCHEDULED_COPY_ONGOING,
        SCHEDULED_COPY_SKIPPING,
        SCHEDULED_COPY_REMOVING_EXCEEDING
    };

    virtual ~MegaScheduledCopy();

    /**
     * @brief Creates a copy of this MegaScheduledCopy object
     *
     * The resulting object is fully independent of the source MegaScheduledCopy,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaScheduledCopy object
     */
    virtual MegaScheduledCopy *copy();

    /**
     * @brief Get the handle of the folder that is being backed up
     * @return Handle of the folder that is being backed up in MEGA
     */
    virtual MegaHandle getMegaHandle() const;

    /**
     * @brief Get the path of the local folder that is being backed up
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaScheduledCopy object is deleted.
     *
     * @return Local folder that is being backed up
     */
    virtual const char* getLocalFolder() const;

    /**
     * @brief Returns the identifier of this backup
     *
     * @return Identifier of the backup
     */
    virtual int getTag() const;

    /**
     * @brief Returns if backups that should have happen in the past should be taken care of
     *
     * @return Whether past backups should be taken care of
     */
    virtual bool getAttendPastBackups() const;

    /**
     * @brief Returns the period of the backup
     *
     * @return The period of the backup in deciseconds
     */
    virtual int64_t getPeriod() const;

    /**
     * @brief Returns the period string of the backup
     * Any of these 6 fields may be an asterisk (*). This would mean the entire range of possible values, i.e. each minute, each hour, etc.
     *
     * Period is formatted as follows
     *  - - - - - -
     *  | | | | | |
     *  | | | | | |
     *  | | | | | +---- Day of the Week   (range: 1-7, 1 standing for Monday)
     *  | | | | +------ Month of the Year (range: 1-12)
     *  | | | +-------- Day of the Month  (range: 1-31)
     *  | | +---------- Hour              (range: 0-23)
     *  | +------------ Minute            (range: 0-59)
     *  +-------------- Second            (range: 0-59)
     *
     * E.g:
     * - daily at 04:00:00 (UTC): "0 0 4 * * *"
     * - every 15th day at 00:00:00 (UTC) "0 0 0 15 * *"
     * - mondays at 04.30.00 (UTC): "0 30 4 * * 1"
     *
     * @return The period string of the backup
     */
    virtual const char *getPeriodString() const;

    /**
     * @brief Returns the next absolute timestamp of the next backup.
     * @param oldStartTimeAbsolute Reference timestamp of the previous backup. If none provided it'll use current one.
     *
     * Successive nested calls to this functions will give you a full schedule of the next backups.
     *
     * Timestamp measures are given in number of seconds that elapsed since January 1, 1970 (midnight UTC/GMT),
     * not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
     *
     * @return timestamp of the next backup.
     */
    virtual long long getNextStartTime(long long oldStartTimeAbsolute = -1) const;


    /**
     * @brief Returns the number of backups to keep
     *
     * @return Maximun number of Backups to store
     */
    virtual int getMaxBackups() const;

    /**
     * @brief Get the state of the backup
     *
     * Possible values are:
     * - SCHEDULED_COPY_FAILED = -2
     * The backup has failed and has been disabled
     *
     * - SCHEDULED_COPY_CANCELED = -1,
     * The backup has failed and has been disabled
     *
     * - SCHEDULED_COPY_INITIALSCAN = 0,
     * The backup is doing the initial scan
     *
     * - SCHEDULED_COPY_ACTIVE
     * The backup is active
     *
     * - SCHEDULED_COPY_ONGOING
     * A backup is being performed
     *
     * - SCHEDULED_COPY_SKIPPING
     * A backup is being skipped
     *
     * - SCHEDULED_COPY_REMOVING_EXCEEDING
     * The backup is active and an exceeding backup is being removed
     * @return State of the backup
     */
    virtual int getState() const;


    // Current backup data:
    /**
     * @brief Returns the number of folders created in the backup
     * @return number of folders created in the backup
     */
    virtual long long getNumberFolders() const;

    /**
     * @brief Returns the number of files created in the backup
     * @return number of files created in the backup
     */
    virtual long long getNumberFiles() const;

    /**
     * @brief Returns the number of files to be created in the backup
     * @return number of files to be created in the backup
     */
    virtual long long getTotalFiles() const;

    /**
     * @brief Returns the starting time of the current backup being processed (in deciseconds)
     *
     * The returned value is a monotonic time since some unspecified starting point expressed in
     * deciseconds.
     *
     * @return Starting time of the backup (in deciseconds)
     */
    virtual int64_t getCurrentBKStartTime() const;

    /**
     * @brief Returns the number of transferred bytes during last backup
     * @return Transferred bytes during this backup
     */
    virtual long long getTransferredBytes() const;

    /**
     * @brief Returns the total bytes to be transferred to complete last backup
     * @return Total bytes to be transferred to complete the backup
     */
    virtual long long getTotalBytes() const;

    /**
     * @brief Returns the current speed of last backup
     * @return Current speed of this backup
     */
    virtual long long getSpeed() const;

    /**
     * @brief Returns the average speed of last backup
     * @return Average speed of this backup
     */
    virtual long long getMeanSpeed() const;

    /**
     * @brief Returns the timestamp when the last data was received (in deciseconds)
     *
     * This timestamp doesn't have a defined starting point. Use the difference between
     * the return value of this function and MegaScheduledCopy::getCurrentBKStartTime to know how
     * much time the backup has been running.
     *
     * @return Timestamp when the last data was received (in deciseconds)
     */
    virtual int64_t getUpdateTime() const;

    /**
     * @brief Returns the list with the transfers that have failed for during last backup
     *
     * You take the ownership of the returned value
     *
     * @return Names of the custom attributes of the node
     * @see MegaApi::setCustomNodeAttribute
     */
    virtual MegaTransferList *getFailedTransfers();
};

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
        API_OK = 0,                     ///< Everything OK
        API_EINTERNAL = -1,             ///< Internal error.
        API_EARGS = -2,                 ///< Bad arguments.
        API_EAGAIN = -3,                ///< Request failed, retry with exponential back-off.
        API_ERATELIMIT = -4,            ///< Too many requests, slow down.
        API_EFAILED = -5,               ///< Request failed permanently.
        API_ETOOMANY = -6,              ///< Too many requests for this resource.
        API_ERANGE = -7,                ///< Resource access out of range.
        API_EEXPIRED = -8,              ///< Resource expired.
        API_ENOENT = -9,                ///< Resource does not exist.
        API_ECIRCULAR = -10,            ///< Circular linkage.
        API_EACCESS = -11,              ///< Access denied.
        API_EEXIST = -12,               ///< Resource already exists.
        API_EINCOMPLETE = -13,          ///< Request incomplete.
        API_EKEY = -14,                 ///< Cryptographic error.
        API_ESID = -15,                 ///< Bad session ID.
        API_EBLOCKED = -16,             ///< Resource administratively blocked.
        API_EOVERQUOTA = -17,           ///< Quota exceeded.
        API_ETEMPUNAVAIL = -18,         ///< Resource temporarily not available.
        API_ETOOMANYCONNECTIONS = -19,  ///< Too many connections on this resource.
        API_EWRITE = -20,               ///< File could not be written to (or failed post-write integrity check).
        API_EREAD = -21,                ///< File could not be read from (or changed unexpectedly during reading).
        API_EAPPKEY = -22,              ///< Invalid or missing application key.
        API_ESSL = -23,                 ///< SSL verification failed
        API_EGOINGOVERQUOTA = -24,      ///< Not enough quota
        API_EMFAREQUIRED = -26,         ///< Multi-factor authentication required
        API_EMASTERONLY = -27,          ///< Access denied for sub-users (only for business accounts)
        API_EBUSINESSPASTDUE = -28,     ///< Business account expired
        API_EPAYWALL = -29,             ///< Over Disk Quota Paywall

        PAYMENT_ECARD = -101,
        PAYMENT_EBILLING = -102,
        PAYMENT_EFRAUD = -103,
        PAYMENT_ETOOMANY = -104,
        PAYMENT_EBALANCE = -105,
        PAYMENT_EGENERIC = -106,

        LOCAL_ENOSPC = -1000, ///< Insufficient space.
    };

    /**
     * @brief Api error code context.
     */
    enum ErrorContexts
    {
        API_EC_DEFAULT = 0,         ///< Default error code context
        API_EC_DOWNLOAD = 1,        ///< Download transfer context.
        API_EC_IMPORT = 2,          ///< Import context.
        API_EC_UPLOAD = 3,          ///< Upload transfer context.
    };

    /**
     * @brief User custom error details
     */
    enum UserErrorCode
    {
        USER_ETD_UNKNOWN = -1,          ///< Unknown state
        USER_COPYRIGHT_SUSPENSION = 4,  /// Account suspended by copyright
        USER_ETD_SUSPENSION = 7,        ///< Account suspend by an ETD/ToS 'severe'
    };

    /**
     * @brief Link custom error details
     */
    enum LinkErrorCode
    {
        LINK_UNKNOWN = -1,      ///< Unknown state
        LINK_UNDELETED = 0,     ///< Link is undeleted
        LINK_DELETED_DOWN = 1,  ///< Link is deleted or down
        LINK_DOWN_ETD = 2,      ///< Link is down due to an ETD specifically
    };

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
        virtual MegaError* copy() const;

		/**
		 * @brief Returns the error code associated with this MegaError
         *
		 * @return Error code, an Errors enum, associated with this MegaError
		 */
        virtual int getErrorCode() const;

        /**
         * @brief Returns the sync error associated with this MegaError
         *
         * @return MegaSync::Error associated with this MegaError
         */
        virtual int getSyncError() const;

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
        virtual long long getValue() const;

        /**
         * @brief Returns true if error has extra info
         *
         * @note This method can return true for:
         *   - MegaRequest::TYPE_FETCH_NODES with error ENOENT
         *   - MegaRequest::TYPE_GET_PUBLIC_NODE with error ETOOMANY
         *   - MegaRequest::TYPE_IMPORT_LINK with error ETOOMANY
         *   - MegaTransferListener::onTransferFinish with error ETOOMANY
         *
         * @return True if error has extra info
         */
        virtual bool hasExtraInfo() const;

        /**
         * @brief Returns the user status
         *
         * This method only returns a valid value when hasExtraInfo is true
         * Possible values:
         *  MegaError::UserErrorCode::USER_COPYRIGHT_SUSPENSION
         *  MegaError::UserErrorCode::USER_ETD_SUSPENSION
         *
         * Otherwise, it returns MegaError::UserErrorCode::USER_ETD_UNKNOWN
         *
         * @return user status
         */
        virtual long long getUserStatus() const;

        /**
         * @brief Returns the link status
         *
         * This method only returns a valid value when hasExtraInfo is true
         * Possible values:
         *  MegaError::LinkErrorCode::LINK_UNDELETED
         *  MegaError::LinkErrorCode::LINK_DELETED_DOWN
         *  MegaError::LinkErrorCode::LINK_DOWN_ETD
         *
         * Otherwise, it returns MegaError::LinkErrorCode::LINK_UNKNOWN
         *
         * @return link status
         */
        virtual long long getLinkStatus() const;

		/**
		 * @brief Returns a readable description of the error
		 *
		 * This function returns a pointer to a statically allocated buffer.
		 * You don't have to free the returned pointer
		 *
		 * @return Readable description of the error
		 */
        virtual const char* getErrorString() const;

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
        virtual const char* toString() const;

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
        virtual const char* __str__() const;

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
        virtual const char* __toString() const;

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

        /**
         * @brief Provides the error description associated with an error code
         * given a certain context.
         *
         * This function returns a pointer to a statically allocated buffer.
         * You don't have to free the returned pointer
         *
         * @param errorCode Error code for which the description will be returned
         * @param context Context to provide a more accurate description (MegaError::ErrorContexts)
         * @return Description associated with the error code
         */
        static const char *getErrorString(int errorCode, ErrorContexts context);


protected:
        MegaError(int e);
        MegaError(int e, int se);

        //< 0 = API error code, > 0 = http error, 0 = No error
        // MegaError::Errors enum/ErrorCodes
        int errorCode;

        // SyncError/MegaSync::Error
        int syncError;

        friend class MegaTransfer;
        friend class MegaApiImpl;
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
 * Alternatively a trywait function is included which waits for an amount of time or until the request is finished.
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
         * In case this transfer represents a recursive operation (folder upload/download) SDK will
         * notify apps about the stages transition.
         *
         * Current recursive operation stage can be retrieved with method MegaTransfer::getStage.
         * This method returns the following values:
         *  - MegaTransfer::STAGE_SCAN                      = 1
         *  - MegaTransfer::STAGE_CREATE_TREE               = 2
         *  - MegaTransfer::STAGE_TRANSFERRING_FILES        = 3
         * For more information about stages refer to MegaTransfer::getStage
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         *
         * @see MegaTransfer::getTransferredBytes, MegaTransfer::getSpeed, MegaTransfer::getStage
         */
        virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);

        /**
         * @brief This function is called to inform about the progress of a folder transfer
         *
         * The SDK retains the ownership of all parameters.
         * Don't use any after this functions returns.
         *
         * The api object is the one created by the application, it will be valid until
         * the application deletes it.
         *
         * This callback is only made for folder transfers, and only to the listener for that
         * transfer, not for any globally registered listeners.  The callback is only made
         * during the scanning phase.
         *
         * This function can be used to give feedback to the user as to how scanning is progressing,
         * since scanning may take a while and the application may be showing a modal dialog during
         * this time.
         *
         * Note that this function could be called from a variety of threads during the
         * overall operation, so proper thread safety should be observed.
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         * @stage MegaTransfer::STAGE_SCAN or a later value in that enum
         * @param foldercount The count of folders scanned so far
         * @param foldercount The count of folders created so far (only relevant in MegaTransfer::STAGE_CREATE_TREE)
         * @param filecount The count of files scanned (and fingerprinted) so far.  0 if not in scanning stage
         * @param currentFolder The path of the folder currently being scanned (NULL except in the scan stage)
         * @param currentFileLeafname The leaft name of the file currently being fingerprinted (can be NULL for the first call in a new folder, and when not scanning anymore)
         */
        virtual void onFolderTransferUpdate(MegaApi *api, MegaTransfer *transfer, int stage, uint32_t foldercount, uint32_t createdfoldercount, uint32_t filecount, const char* currentFolder, const char* currentFileLeafname);

        /**
         * @brief This function is called when there is a temporary error processing a transfer
         *
         * The transfer continues after this callback, so expect more MegaTransferListener::onTransferTemporaryError or
         * a MegaTransferListener::onTransferFinish callback
         *
         * The SDK retains the ownership of the transfer and error parameters.
         * Don't use them after this functions returns.
         *
         * If the error code is API_EOVERQUOTA we need to call to MegaTransfer::isForeignOverquota to determine if
         * our own storage, or a foreign storage is in overquota. If MegaTransfer::isForeignOverquota returns true
         * a foreign storage is in overquota, otherwise our own storage is in overquota.
         *
         * @param api MegaApi object that started the transfer
         * @param transfer Information about the transfer
         * @param error Error information
         */
        virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error);

        virtual ~MegaTransferListener();

        /**
         * @brief This function is called to provide the last read bytes of streaming downloads
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
         * @param buffer Buffer with the last read bytes
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
 * Alternatively a trywait function is included which waits for an amount of time or until the transfer is finished.
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
        * @brief This function is called when there are new or updated user alerts in the account
        *
        * The SDK retains the ownership of the MegaUserAlertList in the second parameter. The list and all the
        * MegaUserAlert objects that it contains will be valid until this function returns. If you want to save the
        * list, use MegaUserAlertList::copy. If you want to save only some of the MegaUserAlert objects, use MegaUserAlert::copy
        * for those objects.
        *
        * @param api MegaApi object connected to the account
        * @param alerts List that contains the new or updated alerts
        */
        virtual void onUserAlertsUpdate(MegaApi* api, MegaUserAlertList *alerts);

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
         * @brief This function is called when a Set has been updated (created / updated / removed)
         *
         * The SDK retains the ownership of the MegaSetList in the second parameter. The list and all the
         * MegaSet objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaSetList::copy. If you want to save only some of the MegaSet objects, use MegaSet::copy
         * for them.
         *
         * @param api MegaApi object connected to the account
         * @param sets List that contains the new or updated Sets
         */
        virtual void onSetsUpdate(MegaApi* api, MegaSetList* sets);

        /**
         * @brief This function is called when a Set-Element has been updated (created / updated / removed)
         *
         * The SDK retains the ownership of the MegaSetElementList in the second parameter. The list and all the
         * MegaSetElement objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaSetElementList::copy. If you want to save only some of the MegaSetElement objects, use
         * MegaSetElement::copy for them.
         *
         * @param api MegaApi object connected to the account
         * @param elements List that contains the new or updated Set-Elements
         */
        virtual void onSetElementsUpdate(MegaApi* api, MegaSetElementList* elements);

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
         * You can check the type of event by calling MegaEvent::getType
         *
         * The SDK retains the ownership of the details of the event (\c event).
         * Don't use them after this functions returns.
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
         *  - MegaEvent::EVENT_ACCOUNT_BLOCKED: when the account get blocked, typically because of
         * infringement of the Mega's terms of service repeatedly. This event is followed by an automatic
         * logout, except for the temporary blockings (ACCOUNT_BLOCKED_VERIFICATION_SMS and
         * ACCOUNT_BLOCKED_VERIFICATION_EMAIL)
         *
         *  Valid data in the MegaEvent object received in the callback:
         *      - MegaEvent::getText: message to show to the user.
         *      - MegaEvent::getNumber: code representing the reason for being blocked.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_TOS_COPYRIGHT = 200
         *              Suspension only for multiple copyright violations.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT = 300
         *              Suspension message for any type of suspension, but copyright suspension.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_SUBUSER_DISABLED = 400
         *              Subuser of the business account has been disabled.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_SUBUSER_REMOVED = 401
         *              Subuser of business account has been removed.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_SMS = 500
         *              The account is temporary blocked and needs to be verified by an SMS code.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_EMAIL = 700
         *              The account is temporary blocked and needs to be verified by email (Weak Account Protection).
         *
         * - MegaEvent::EVENT_STORAGE: when the status of the storage changes.
         *
         * For this event type, MegaEvent::getNumber provides the current status of the storage
         *
         * There are three possible storage states:
         *     - MegaApi::STORAGE_STATE_GREEN = 0
         *     There are no storage problems
         *
         *     - MegaApi::STORAGE_STATE_ORANGE = 1
         *     The account is almost full
         *
         *     - MegaApi::STORAGE_STATE_RED = 2
         *     The account is full. Uploads have been stopped
         *
         *     - MegaApi::STORAGE_STATE_CHANGE = 3
         *     There is a possible significant change in the storage state.
         *     It's needed to call MegaApi::getAccountDetails to check the storage status.
         *     After calling it, this callback will be called again with the corresponding
         *     state if there is really a change.
         *
         *     - MegaApi::STORAGE_STATE_PAYWALL = 4
         *     The account has been full for a long time. Now most of actions are disallowed.
         *     It's needed to call MegaApi::getUserData in order to retrieve the deadline/warnings
         *     timestamps. @see MegaApi::getOverquotaDeadlineTs and MegaApi::getOverquotaWarningsTs.
         *
         * - MegaEvent::EVENT_NODES_CURRENT: when all external changes have been received
         *
         * - MegaEvent::EVENT_MEDIA_INFO_READY: when codec-mappings have been received
         *
         * - MegaEvent::EVENT_STORAGE_SUM_CHANGED: when the storage sum has changed.
         *
         * For this event type, MegaEvent::getNumber provides the new storage sum.
         *
         * - MegaEvent::EVENT_BUSINESS_STATUS: when the status of a business account has changed.
         *
         * For this event type, MegaEvent::getNumber provides the new business status.
         *
         * The posible values are:
         *  - BUSINESS_STATUS_EXPIRED = -1
         *  - BUSINESS_STATUS_INACTIVE = 0
         *  - BUSINESS_STATUS_ACTIVE = 1
         *  - BUSINESS_STATUS_GRACE_PERIOD = 2
         *
         * - MegaEvent::EVENT_KEY_MODIFIED: when the key of a user has changed.
         *
         * For this event type, MegaEvent::getHandle provides the handle of the user whose key has been modified.
         * For this event type, MegaEvent::getNumber provides type of key that has been modified.
         *
         * The posible values are:
         *  - Public chat key (Cu25519)     = 0
         *  - Public signing key (Ed25519)  = 1
         *  - Public RSA key                = 2
         *  - Signature of chat key         = 3
         *  - Signature of RSA key          = 4
         *
         * - MegaEvent::EVENT_MISC_FLAGS_READY: when the miscellaneous flags are available/updated.
         *
         * @param api MegaApi object connected to the account
         * @param event Details about the event
         */
        virtual void onEvent(MegaApi* api, MegaEvent *event);

        /**
         * @brief This function is called when external drives are connected or disconnected
         *
         * The SDK retains the ownership of the char* in the third parameter, which will be valid until this function returns.
         *
         * @param api MegaApi object connected to the account
         * @param present Indicator of the drive status after this change (true: drive was connected; false: drive was disconnected)
         * @param rootPathInUtf8 Root path of the drive that determined this change (i.e. "D:", "/mnt/usbdrive")
         */
        virtual void onDrivePresenceChanged(MegaApi* api, bool present, const char* rootPathInUtf8);

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
         * In case that we are uploading a file into an incoming share, and our write permissions over the share
         * are revoked before the transfer has finished, API will put the node into our rubbish-bin.
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
         * In case this transfer represents a recursive operation (folder upload/download) SDK will
         * notify apps about the stages transition.
         *
         * Current recursive operation stage can be retrieved with method MegaTransfer::getStage.
         * This method returns the following values:
         *  - MegaTransfer::STAGE_SCAN                      = 1
         *  - MegaTransfer::STAGE_CREATE_TREE               = 2
         *  - MegaTransfer::STAGE_TRANSFERRING_FILES        = 3
         * For more information about stages refer to MegaTransfer::getStage
         *
         * @see MegaTransfer::getTransferredBytes, MegaTransfer::getSpeed, MegaTransfer::getStage
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
         * If the error code is API_EOVERQUOTA we need to call to MegaTransfer::isForeignOverquota to determine if
         * our own storage, or a foreign storage is in overquota. If MegaTransfer::isForeignOverquota returns true
         * a foreign storage is in overquota, otherwise our own storage is in overquota.
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
        * @brief This function is called when there are new or updated user alerts in the account
        *
        * The SDK retains the ownership of the MegaUserAlertList in the second parameter. The list and all the
        * MegaUserAlert objects that it contains will be valid until this function returns. If you want to save the
        * list, use MegaUserAlertList::copy. If you want to save only some of the MegaUserAlert objects, use MegaUserAlert::copy
        * for those objects.
        *
        * @param api MegaApi object connected to the account
        * @param alerts List that contains the new or updated alerts
        */
        virtual void onUserAlertsUpdate(MegaApi* api, MegaUserAlertList *alerts);

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
         * @brief This function is called when a Set has been updated (created / updated / removed)
         *
         * The SDK retains the ownership of the MegaSetList in the second parameter. The list and all the
         * MegaSet objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaSetList::copy. If you want to save only some of the MegaSet objects, use MegaSet::copy
         * for them.
         *
         * @param api MegaApi object connected to the account
         * @param sets List that contains the new or updated Sets
         */
        virtual void onSetsUpdate(MegaApi* api, MegaSetList* sets);

        /**
         * @brief This function is called when a Set-Element has been updated (created / updated / removed)
         *
         * The SDK retains the ownership of the MegaSetElementList in the second parameter. The list and all the
         * MegaSetElement objects that it contains will be valid until this function returns. If you want to save the
         * list, use MegaSetElementList::copy. If you want to save only some of the MegaSetElement objects, use
         * MegaSetElement::copy for them.
         *
         * @param api MegaApi object connected to the account
         * @param elements List that contains the new or updated Set-Elements
         */
        virtual void onSetElementsUpdate(MegaApi* api, MegaSetElementList* elements);

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
     * The SDK retains the ownership of the sync and localPath parameters.
     * Don't use them after this functions returns.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object manages the file
     * @param localPath Local path of the file or folder
     * @param newState New state of the file
     */
    virtual void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState);

    /**
     * @brief This callback will be called when a sync is added
     *
     * The SDK will call this after loading (and attempt to resume) syncs from cache or whenever a new
     * Synchronization is configured.
     *
     * Notice that adding a sync will not cause onSyncStateChanged to be called.
     *
     * As to the additionState can be:
     * - MegaSync::SyncAdded::NEW = 1
     * Sync added anew and activated
     *
     * - MegaSync::SyncAdded::FROM_CACHE = 2
     * Sync loaded from cache. If the sync was enabled, it will be enabled.
     *
     * - MegaSync::SyncAdded::FROM_CACHE_FAILED_TO_RESUME = 3
     * Sync loaded from cache, but failed to be resumed.
     *
     * - MegaSync::SyncAdded::FROM_CACHE_REENABLED = 4
     * Sync loaded from cache, and reenabled. The sync was temporary disabled but could be succesfully
     * resumed
     *
     * - MegaSync::SyncAdded::REENABLED_FAILED = 5
     * Sync loaded from cache and attempted to be reenabled. The sync will have the error for that
     *
     * - MegaSync::SyncAdded::NEW_DISABLED = 6
     * Sync added anew, but set as temporarily disabled due to a temporary error
     *
     * The SDK retains the ownership of the sync parameter.
     * Don't use it after this functions returns.
     *
     * @param sync MegaSync object representing a sync
     * @param api MegaApi object that is synchronizing files
     * @param additionState conditions in which the sync is added
     */
    virtual void onSyncAdded(MegaApi *api, MegaSync *sync, int additionState);

    /**
     * @brief This callback will be called when a sync is disabled.
     *
     * This can happen in the following situations
     *
     * - There’s a condition that cause the sync to fail
     *
     * - There’s a condition that cause the sync to be temporarily disabled
     *
     * - The users tries to disable a sync that had been previously failed permanently
     *
     * - The users tries to disable a sync that had been previously temporarily disabled.
     *
     * - The user tries to disable a sync that was active
     *
     * - The sdk tries to resume a sync that had been temporarily disabled and a failure happens
     * This does not imply a transition from active to inactive, but the callback is necessary to inform the user
     * that the sync is no longer in a temporary error, but in a fatal one.
     *
     * The SDK retains the ownership of the sync parameter.
     * Don't use it after this functions returns.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object representing a sync
     */
    virtual void onSyncDisabled(MegaApi *api, MegaSync *sync);

    /**
     * @brief This callback will be called when a sync is enabled.
     *
     * This can happen in the following situations
     *
     * - The users enables a sync that was disabled
     *
     * - The sdk tries resumes a sync that had been temporarily disabled
     *
     * The SDK retains the ownership of the sync parameter.
     * Don't use it after this functions returns.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object representing a sync
     */
    virtual void onSyncEnabled(MegaApi *api, MegaSync *sync);

    /**
     * @brief This callback will be called when a sync is removed.
     *
     * This entail that the sync is completely removed from cache
     *
     * The SDK retains the ownership of the sync parameter.
     * Don't use it after this functions returns.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object representing a sync
     */
    virtual void onSyncDeleted(MegaApi *api, MegaSync *sync);

    /**
     * @brief This function is called when the state of the synchronization changes
     *
     * The SDK calls this function when the state of the synchronization changes. you can use
     * MegaSync::getState to get the new state of the synchronization
     * and MegaSync::getError to get the error if any.
     *
     * Notice, for changes that imply other callbacks, expect that the SDK
     * will call onSyncStateChanged first, so that you can update your model only using this one.
     *
     * The SDK retains the ownership of the sync parameter.
     * Don't use it after this functions returns.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object that has changed its state
     */
    virtual void onSyncStateChanged(MegaApi *api, MegaSync *sync);

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

    /**
     * @brief This function is called when the state of the backup changes
     *
     * The SDK calls this function when the state of the backup changes, for example
     * from 'active' to 'ongoing' or 'removing exceeding'.
     *
     * You can use MegaScheduledCopy::getState to get the new state.
     *
     * @param api MegaApi object that is backing up files
     * @param backup MegaScheduledCopy object that has changed the state
     */
    virtual void onBackupStateChanged(MegaApi *api, MegaScheduledCopy *backup);

    /**
     * @brief This function is called when a backup is about to start being processed
     *
     * The SDK retains the ownership of the backup parameter.
     * Don't use it after this functions returns.
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     */
    virtual void onBackupStart(MegaApi *api, MegaScheduledCopy *backup);

    /**
     * @brief This function is called when a backup has finished
     *
     * The SDK retains the ownership of the backup and error parameters.
     * Don't use them after this functions returns.
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * There won't be more callbacks about this backup.
     * The last parameter provides the result of the backup. If the backup finished without problems,
     * the error code will be API_OK
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     * @param error Error information
     */
    virtual void onBackupFinish(MegaApi* api, MegaScheduledCopy *backup, MegaError* error);

    /**
     * @brief This function is called to inform about the progress of a backup
     *
     * The SDK retains the ownership of the backup parameter.
     * Don't use it after this functions returns.
     *
     * The api object is the one created by the application, it will be valid until
     * the application deletes it.
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     *
     * @see MegaScheduledCopy::getTransferredBytes, MegaScheduledCopy::getSpeed
     */
    virtual void onBackupUpdate(MegaApi *api, MegaScheduledCopy *backup);

    /**
     * @brief This function is called when there is a temporary error processing a backup
     *
     * The backup continues after this callback, so expect more MegaScheduledCopyListener::onBackupTemporaryError or
     * a MegaScheduledCopyListener::onBackupFinish callback
     *
     * The SDK retains the ownership of the backup and error parameters.
     * Don't use them after this functions returns.
     *
     * @param api MegaApi object that started the backup
     * @param backup Information about the backup
     * @param error Error information
     */
    virtual void onBackupTemporaryError(MegaApi *api, MegaScheduledCopy *backup, MegaError* error);

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
         * You can check the type of event by calling MegaEvent::getType
         *
         * The SDK retains the ownership of the details of the event (\c event).
         * Don't use them after this functions returns.
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
         *  - MegaEvent::EVENT_ACCOUNT_BLOCKED: when the account get blocked, typically because of
         * infringement of the Mega's terms of service repeatedly. This event is followed by an automatic
         * logout.
         *
         *  Valid data in the MegaEvent object received in the callback:
         *      - MegaEvent::getText: message to show to the user.
         *      - MegaEvent::getNumber: code representing the reason for being blocked.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_TOS_COPYRIGHT = 200
         *              Suspension only for multiple copyright violations.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT = 300
         *              Suspension message for any type of suspension, but copyright suspension.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_SUBUSER_DISABLED = 400
         *              Subuser of the business account has been disabled.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_SUBUSER_REMOVED = 401
         *              Subuser of business account has been removed.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_SMS = 500
         *              The account is temporary blocked and needs to be verified by an SMS code.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_EMAIL = 700
         *              The account is temporary blocked and needs to be verified by email (Weak Account Protection).
         *
         * - MegaEvent::EVENT_STORAGE: when the status of the storage changes.
         *
         * For this event type, MegaEvent::getNumber provides the current status of the storage
         *
         * There are three possible storage states:
         *     - MegaApi::STORAGE_STATE_GREEN = 0
         *     There are no storage problems
         *
         *     - MegaApi::STORAGE_STATE_ORANGE = 1
         *     The account is almost full
         *
         *     - MegaApi::STORAGE_STATE_RED = 2
         *     The account is full. Uploads have been stopped
         *
         *     - MegaApi::STORAGE_STATE_CHANGE = 3
         *     There is a possible significant change in the storage state.
         *     It's needed to call MegaApi::getAccountDetails to check the storage status.
         *     After calling it, this callback will be called again with the corresponding
         *     state if there is really a change.
         *
         *     - MegaApi::STORAGE_STATE_PAYWALL = 4
         *     The account has been full for a long time. Now most of actions are disallowed.
         *     It's needed to call MegaApi::getUserData in order to retrieve the deadline/warnings
         *     timestamps. @see MegaApi::getOverquotaDeadlineTs and MegaApi::getOverquotaWarningsTs.
         *
         * - MegaEvent::EVENT_NODES_CURRENT: when all external changes have been received
         *
         * - MegaEvent::EVENT_MEDIA_INFO_READY: when codec-mappings have been received
         *
         * - MegaEvent::EVENT_STORAGE_SUM_CHANGED: when the storage sum has changed.
         *
         * For this event type, MegaEvent::getNumber provides the new storage sum.
         *
         * - MegaEvent::EVENT_BUSINESS_STATUS: when the status of a business account has changed.
         *
         * For this event type, MegaEvent::getNumber provides the new business status.
         *
         * The posible values are:
         *  - BUSINESS_STATUS_EXPIRED = -1
         *  - BUSINESS_STATUS_INACTIVE = 0
         *  - BUSINESS_STATUS_ACTIVE = 1
         *  - BUSINESS_STATUS_GRACE_PERIOD = 2
         *
         * - MegaEvent::EVENT_KEY_MODIFIED: when the key of a user has changed.
         *
         * For this event type, MegaEvent::getHandle provides the handle of the user whose key has been modified.
         * For this event type, MegaEvent::getNumber provides type of key that has been modified.
         *
         * The posible values are:
         *  - Public chat key (Cu25519)     = 0
         *  - Public signing key (Ed25519)  = 1
         *  - Public RSA key                = 2
         *  - Signature of chat key         = 3
         *  - Signature of RSA key          = 4
         *
         * - MegaEvent::EVENT_MISC_FLAGS_READY: when the miscellaneous flags are available/updated.
         *
         * @param api MegaApi object connected to the account
         * @param event Details about the event
         */
        virtual void onEvent(MegaApi* api, MegaEvent *event);

        virtual ~MegaListener();
};

/**
 * @brief Stores information about a background photo/video upload, used in iOS to take advantage of power saving features
 *
 * This object can be serialised so it can be stored in case your app is unloaded by its OS, and the background operation
 * completed afterward.
 *
 */
class MegaBackgroundMediaUpload
{
protected:
    MegaBackgroundMediaUpload();

public:

    /**
     * @brief Initial step to upload a photo/video via iOS low-power background upload feature
     *
     * Creates an object which can be used to encrypt a media file, and upload it outside of the SDK,
     * eg. in order to take advantage of a particular platform's low power background upload functionality.
     *
     * You take ownership of the returned value.
     *
     * @param api The MegaApi the new object will be used with. It must live longer than the new object.
     * @return A pointer to an object that keeps some needed state through the process of
     *         uploading a media file via iOS low power background uploads (or similar).
     */
    static MegaBackgroundMediaUpload* createInstance(MegaApi *api);

    /**
     * @brief Extract mediainfo information about the photo or video.
     *
     * Call this function once with the file to be uploaded. It uses mediainfo to extract information that will
     * help other clients to show or to play the files. The information is stored in this object until the whole
     * operation completes.
     *
     * Call MegaApi::ensureMediaInfo first in order prepare the library to attach file attributes
     * that enable videos to be identified and played in the web browser.
     *
     * @param inputFilepath The file to analyse with MediaInfo.
     * @return true if analysis was performed (and any relevant attributes stored ready for upload), false if mediainfo was not ready yet.
     */
    virtual bool analyseMediaInfo(const char* inputFilepath);

    /**
     * @brief Encrypt the file or a portion of it
     *
     * Call this function once with the file to be uploaded. It uses mediainfo to extract information that will
     * help the webclient show or play the file in various browsers. The information is stored in this object
     * until the whole operation completes. The encrypted data is stored in a new file.
     *
     * In order to save space on mobile devices, this function can be called in such a way that the last portion
     * of the file is encrypted (to a new file), and then that last portion of the file is removed by file truncation.
     * That operation can be repeated until the file is completely encrypted, and only the encrypted version remains,
     * and takes up the same amount of space on the device. The size of the portions must first be calculated by using
     * the 'adjustsizeonly' parameter, and iterating from the start of the file, specifying the approximate sizes of the portions.
     *
     * Encryption is done by reading small pieces of the file, encrypting them, and outputting to the new file,
     * so that RAM usage is not excessive.
     *
     * You take ownership of the returned value.
     *
     * @param inputFilepath The file to encrypt a portion of (and the one that is ultimately being uploaded).
     * @param startPos The index of the first byte of the file to encrypt
     * @param length The number of bytes of the file to encrypt. The function will round this value up by up to 1MB to fit the
     *        MEGA internal chunking algorithm. The number of bytes actually encrypted and stored in the new file is the updated number.
     *        You can supply -1 as input to request the remainder file (from startPos) be encrypted.
     * @param outputFilepath The name of the new file to create, and store the encrypted data in.
     * @param adjustsizeonly If this is set true, then encryption is not performed, and only the length parameter is adjusted.
     *        This feature is to enable precalculating the exact sizes of the file portions for upload.
     * @return If the function tries to encrypt and succeeds, the return value is the suffix to append to the URL when uploading this enrypted chunk.
     *         If adjustsizeonly was set, and the function succeeds, the return value will be non-NULL (and will need deallocation as usual).
     *         If the function fails, the return value is NULL, and an error will have been logged.
     */
    virtual char *encryptFile(const char* inputFilepath, int64_t startPos, int64_t* length, const char* outputFilepath, bool adjustsizeonly);

    /**
     * @brief Retrieves the value of the uploadURL once it has been successfully requested via MegaApi::backgroundMediaUploadRequestUploadURL
     *
     * You take ownership of the returned value.
     *
     * @return The URL to upload to (after appending the suffix), if one has been received. Otherwise NULL.
     */
    virtual char *getUploadURL();

    /**
     * @brief Attach a thumbnail by its file attribute handle.
     *
     * The thumbnail will implictly be attached to the node created as part of MegaApi::backgroundMediaUploadComplete.
     * The thumbnail file attibrute must have been obtained by MegaApi::putThumbnail.
     * If the result of MegaApi::putThumbnail is not available by the time MegaApi::backgroundMediaUploadComplete
     * is called, it can be attached to the node later using MegaApi::setThumbnailByHandle.
     *
     * @param h The handle obtained via MegaApi::putThumbnail
     */
    virtual void setThumbnail(MegaHandle h);

    /**
     * @brief Attach a preview by its file attribute handle.
     *
     * The preview will implictly be attached to the node created as part of MegaApi::backgroundMediaUploadComplete.
     * The preview file attibrute must have been obtained by MegaApi::putPreview.
     * If the result of MegaApi::putPreview is not available by the time MegaApi::backgroundMediaUploadComplete
     * is called, it can be attached to the node later using MegaApi::setPreviewByHandle.
     *
     * @param h The handle obtained via MegaApi::putPreview
     */
    virtual void setPreview(MegaHandle h);

    /**
     * @brief Sets the GPS coordinates for the node
     *
     * The node created via MegaApi::backgroundMediaUploadComplete will gain these coordinates as part of the
     * node creation. If the unshareable flag is set, the coodinates are encrypted in a way that even if the
     * node is later shared, the GPS coordinates cannot be decrypted by a different account.
     *
     * @param latitude The GPS latitude
     * @param longitude The GPS longitude
     * @param unshareable Set this true to prevent the coordinates being readable by other accounts.
     */
    virtual void setCoordinates(double latitude, double longitude, bool unshareable);

    /**
     * @brief Turns the data stored in this object into a base 64 encoded string.
     *
     * The object can then be recreated via MegaBackgroundMediaUpload::unserialize and supplying the returned string.
     *
     * You take ownership of the returned value.
     *
     * @return serialized version of this object (including URL, mediainfo attributes, and internal data suitable to resume uploading with in future)
     */
    virtual char *serialize();

    /**
     * @brief Get back the needed MegaBackgroundMediaUpload after the iOS app exited and restarted
     *
     * In case the iOS app exits while a background upload is going on, and the app is started again
     * to complete the operation, call this function to recreate the MegaBackgroundMediaUpload object
     * needed for a call to MegaApi::backgroundMediaUploadComplete. The object must have been serialised
     * before the app was unloaded by using MegaBackgroundMediaUpload::serialize.
     *
     * You take ownership of the returned value.
     *
     * @param data The string the object was serialized to previously.
     * @param api The MegaApi this object will be used with. It must live longer than this object.
     * @return A pointer to a new MegaBackgroundMediaUpload with all fields set to the data that was
     *         stored in the serialized string.
     */
    static MegaBackgroundMediaUpload* unserialize(const char* data, MegaApi* api);

    /**
     * @brief Destructor
     */
    virtual ~MegaBackgroundMediaUpload();
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
 * @brief Allows calling many synchronous operations on MegaApi without being blocked by SDK activity.
 *
 * Call MegaApi::getMegaApiLock() to get an instance of this class to use.
 */
class MegaApiLock
{
public:
    /**
     * @brief Lock the MegaApi if this instance does not currently have a lock on it yet.
     *
     * There is no harm in calling this more than once, the MegaApi will only be locked
     * once, and the first unlock() call will release it.    Sometimes it is useful eg.
     * in a loop which may or may not need to use a locking function, or may need to use
     * many, to call lockOnce() before any such usage, and know that the MegaApi will
     * be locked once from that point, until the end of the loop (when unlockOnce() can
     * be called, or the MegaApiLock destroyed.
     */
    void lockOnce();

    /**
     * @brief Tries to lock the MegaApi if this instance does not currently have a lock on it yet.
     *
     * If the lock is succeeded in the expected time, the behaviour is the same as lockOnce().
     *
     * @param time Milliseconds to wait for locking
     *
     * @return if the locking succeded
     */
    bool tryLockFor(long long time);


    /**
     * @brief Release the lock on the MegaApi if one is still held by this instance
     *
     * The MegaApi will be unable to continue work until all MegaApiLock objects release
     * their locks.  Only use multiple of these if you need nested locking.  The destructor
     * of the object will release the lock, so it is sufficient to delete it when finished.
     * However, when using it from a garbage collected language it may be prudent to call unlock() directly.
     *
     * This function must be called from the same thread that called MegaApiLock::lockOnce().
     */
    void unlockOnce();

    /**
     * @brief Destructor.  This will call unlock() if the MegaApi is still locked by this instance.
     */
    ~MegaApiLock();

private:
    MegaApiImpl* api;
    bool locked = false;
    MegaApiLock(MegaApiImpl*, bool lock);
    MegaApiLock(const MegaApiLock&) = delete;
    void operator=(const MegaApiLock&) = delete;
    friend class MegaApi;
};

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
            USER_ATTR_UNKNOWN = -1,
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
            USER_ATTR_DISABLE_VERSIONS = 16,    // private - byte array
            USER_ATTR_CONTACT_LINK_VERIFICATION = 17,     // private - byte array
            USER_ATTR_RICH_PREVIEWS = 18,        // private - byte array
            USER_ATTR_RUBBISH_TIME = 19,         // private - byte array
            USER_ATTR_LAST_PSA = 20,             // private - char array
            USER_ATTR_STORAGE_STATE = 21,        // private - char array
            USER_ATTR_GEOLOCATION = 22,          // private - byte array
            USER_ATTR_CAMERA_UPLOADS_FOLDER = 23,// private - byte array
            USER_ATTR_MY_CHAT_FILES_FOLDER = 24, // private - byte array
            USER_ATTR_PUSH_SETTINGS = 25,        // private - char array
            // ATTR_UNSHAREABLE_KEY = 26         // it's internal for SDK, not exposed to apps
            USER_ATTR_ALIAS = 27,                // private - byte array
            USER_ATTR_DEVICE_NAMES = 30,         // private - byte array
            USER_ATTR_MY_BACKUPS_FOLDER = 31,    // protected - char array in B64
            // USER_ATTR_BACKUP_NAMES = 32,      // (deprecated) private - byte array
            USER_ATTR_COOKIE_SETTINGS = 33,      // private - byte array
            USER_ATTR_JSON_SYNC_CONFIG_DATA = 34,// private - byte array
            USER_ATTR_DRIVE_NAMES = 35,          // private - byte array
            USER_ATTR_NO_CALLKIT = 36,           // private - byte array
        };

        enum {
            NODE_ATTR_DURATION = 0,
            NODE_ATTR_COORDINATES = 1,
            NODE_ATTR_ORIGINALFINGERPRINT = 2,
            NODE_ATTR_LABEL = 3,
            NODE_ATTR_FAV = 4,
        };

        enum {
            PAYMENT_METHOD_BALANCE = 0,
            PAYMENT_METHOD_PAYPAL = 1,
            PAYMENT_METHOD_ITUNES = 2,
            PAYMENT_METHOD_GOOGLE_WALLET = 3,
            PAYMENT_METHOD_BITCOIN = 4,
            PAYMENT_METHOD_UNIONPAY = 5,
            PAYMENT_METHOD_FORTUMO = 6,
            PAYMENT_METHOD_STRIPE = 7,          // credit-card (stripe)
            PAYMENT_METHOD_CREDIT_CARD = 8,
            PAYMENT_METHOD_CENTILI = 9,
            PAYMENT_METHOD_PAYSAFE_CARD = 10,
            PAYMENT_METHOD_ASTROPAY = 11,
            PAYMENT_METHOD_RESERVED = 12,       // TBD
            PAYMENT_METHOD_WINDOWS_STORE = 13,
            PAYMENT_METHOD_TPAY = 14,
            PAYMENT_METHOD_DIRECT_RESELLER = 15,
            PAYMENT_METHOD_ECP = 16,
            PAYMENT_METHOD_SABADELL = 17,
            PAYMENT_METHOD_HUAWEI_WALLET = 18,
            PAYMENT_METHOD_STRIPE2 = 19,        // credit-card (stripe)
            PAYMENT_METHOD_WIRE_TRANSFER = 999
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
            PUSH_NOTIFICATION_IOS_STD = 3,
            PUSH_NOTIFICATION_ANDROID_HUAWEI = 4
        };

        enum {
            PASSWORD_STRENGTH_VERYWEAK = 0,
            PASSWORD_STRENGTH_WEAK = 1,
            PASSWORD_STRENGTH_MEDIUM = 2,
            PASSWORD_STRENGTH_GOOD = 3,
            PASSWORD_STRENGTH_STRONG = 4
        };

        enum {
            RETRY_NONE = 0,
            RETRY_CONNECTIVITY = 1,
            RETRY_SERVERS_BUSY = 2,
            RETRY_API_LOCK = 3,
            RETRY_RATE_LIMIT = 4,
            RETRY_LOCAL_LOCK = 5,
            RETRY_UNKNOWN = 6
        };

        enum {
            KEEP_ALIVE_CAMERA_UPLOADS = 0
        };

        enum {
            STORAGE_STATE_UNKNOWN = -9,
            STORAGE_STATE_GREEN = 0,
            STORAGE_STATE_ORANGE = 1,
            STORAGE_STATE_RED = 2,
            STORAGE_STATE_CHANGE = 3,
            STORAGE_STATE_PAYWALL = 4,
        };

        enum {
            BUSINESS_STATUS_EXPIRED = -1,
            BUSINESS_STATUS_INACTIVE = 0,   // no business subscription
            BUSINESS_STATUS_ACTIVE = 1,
            BUSINESS_STATUS_GRACE_PERIOD = 2
        };

        enum {
            AFFILIATE_TYPE_INVALID = 0, // legacy mode
            AFFILIATE_TYPE_ID = 1,
            AFFILIATE_TYPE_FILE_FOLDER = 2,
            AFFILIATE_TYPE_CHAT = 3,
            AFFILIATE_TYPE_CONTACT = 4,
        };

        enum {
            ACCOUNT_NOT_BLOCKED = 0,
            ACCOUNT_BLOCKED_EXCESS_DATA_USAGE = 100,        // (deprecated)
            ACCOUNT_BLOCKED_TOS_COPYRIGHT = 200,            // suspended due to copyright violations
            ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT = 300,        // suspended due to multiple breaches of MEGA ToS
            ACCOUNT_BLOCKED_SUBUSER_DISABLED = 400,         // subuser disabled by business administrator
            ACCOUNT_BLOCKED_SUBUSER_REMOVED = 401,          // subuser removed by business administrator
            ACCOUNT_BLOCKED_VERIFICATION_SMS = 500,         // temporary blocked, require SMS verification
            ACCOUNT_BLOCKED_VERIFICATION_EMAIL = 700,       // temporary blocked, require email verification
        };

        enum {
            BACKUP_TYPE_INVALID = -1,
            BACKUP_TYPE_TWO_WAY_SYNC = 0,
            BACKUP_TYPE_UP_SYNC = 1,
            BACKUP_TYPE_DOWN_SYNC = 2,
            BACKUP_TYPE_CAMERA_UPLOADS = 3,
            BACKUP_TYPE_MEDIA_UPLOADS = 4,   // Android has a secondary CU
        };

        enum {
            GOOGLE_ADS_DEFAULT = 0x0,                        // If you don't want to set any overrides/flags, then please provide 0
            GOOGLE_ADS_FORCE_ADS = 0x200,                    // Force enable ads regardless of any other factors.
            GOOGLE_ADS_IGNORE_MEGA = 0x400,                  // Show ads even if the current user or file owner is a MEGA employee.
            GOOGLE_ADS_IGNORE_COUNTRY = 0x800,               // Show ads even if the user is not within an enabled country.
            GOOGLE_ADS_IGNORE_IP = 0x1000,                   // Show ads even if the user is on a blacklisted IP (MEGA ips).
            GOOGLE_ADS_IGNORE_PRO = 0x2000,                  // Show ads even if the current user or file owner is a PRO user.
            GOOGLE_ADS_FLAG_IGNORE_ROLLOUT = 0x4000,         // Ignore the rollout logic which only servers ads to 10% of users based on their IP.
        };

        enum
        {
            CREATE_ACCOUNT              = 0,
            RESUME_ACCOUNT              = 1,
            CANCEL_ACCOUNT              = 2,
            CREATE_EPLUSPLUS_ACCOUNT    = 3,
            RESUME_EPLUSPLUS_ACCOUNT    = 4,
        };

        enum
        {
            CREATE_SET                  = (1 << 0),
            OPTION_SET_NAME             = (1 << 1),
            OPTION_SET_COVER            = (1 << 2),
        };
        enum
        {
            CREATE_ELEMENT              = (1 << 0),
            OPTION_ELEMENT_NAME         = (1 << 1),
            OPTION_ELEMENT_ORDER        = (1 << 2),
        };

        static constexpr int64_t INVALID_CUSTOM_MOD_TIME = -1;
        static constexpr int CHAT_OPTIONS_EMPTY = 0;

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
         * @param workerThreadCount The number of worker threads for encryption or other operations
         * Using worker threads means that synchronous function calls on MegaApi will be blocked less,
         * and uploads and downloads can proceed more quickly on very fast connections.
         *
         */
        MegaApi(const char *appKey, const char *basePath = NULL, const char *userAgent = NULL, unsigned workerThreadCount = 1);

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
         * @param workerThreadCount The number of worker threads for encryption or other operations
         * Using worker threads means that synchronous function calls on MegaApi will be blocked less,
         * and uploads and downloads can proceed more quickly on very fast connections.
         *
         */
        MegaApi(const char *appKey, MegaGfxProcessor* processor, const char *basePath = NULL, const char *userAgent = NULL, unsigned workerThreadCount = 1);

#ifdef HAVE_MEGAAPI_RPC
        MegaApi();
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
        virtual void addRequestListener(MegaRequestListener* listener);

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
         * @brief Add a listener for all events related to backups
         * @param listener Listener that will receive backup events
         */
        void addScheduledCopyListener(MegaScheduledCopyListener *listener);

        /**
         * @brief Unregister a backup listener
         * @param listener Objet that will be unregistered
         */
        void removeScheduledCopyListener(MegaScheduledCopyListener *listener);

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
         * @brief Generates a hash based in the provided private key and email
         *
         * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
         * required to log in, this function allows to do this step in a separate function. You should run this function
         * in a background thread, to prevent UI hangs. The resulting key can be used in MegaApi::fastLogin
         *
         * You take the ownership of the returned value.
         *
         * @param base64pwkey Private key returned by MegaRequest::getPrivateKey in the onRequestFinish callback of createAccount
         * @param email Email to create the hash
         * @return Base64-encoded hash
         *
         * @deprecated This function is only useful for old accounts. Once enabled the new registration logic,
         * this function will return an empty string for new accounts and will be removed few time after.
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
         * If the client is logged in, but the account is not fully confirmed (ie. singup not completed yet),
         * this method will return API_EACCESS.
         *
         * If the client is not logged in, there won't be any session to transfer, but this method will still
         * return the https://mega.nz/#<path>.
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
         * @brief
         * Converts a Base64-encoded backup ID to a MegaHandle.
         *
         * You can revert this operation using MegaApi::backupIdToBase64.
         *
         * @param backupId
         * Base64-encoded Backup ID.
         *
         * @return
         * Backup ID.
         */
        static MegaHandle base64ToBackupId(const char* backupId);

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
         * @param handle User handle to be converted
         * @return Base64-encoded user handle
         */
        static char* userHandleToBase64(MegaHandle handle);

        /**
         * @brief
         * Converts a Backup ID into a Base64-encoded string.
         *
         * You take ownership of the returned value.
         *
         * @param backupId
         * Backup ID to be converted.
         *
         * @return
         * Base64-encoded backup ID.
         */
        static const char* backupIdToBase64(MegaHandle backupId);

        /**
         * @brief Convert binary data to a base 64 encoded string
         *
         * For some operations such as background uploads, binary data must be converted to a format
         * suitable to be passed to the MegaApi interface. Use this function to do so.
         *
         * You take the ownership of the returned value
         *
         * @param binaryData A pointer to the start of the binary data
         * @param length The number of bytes in the binary data
         * @return A newly allocated NULL-terminated string consisting of base64 characters.
         */
        static char *binaryToBase64(const char* binaryData, size_t length);

        /**
         * @brief Convert data encoded in a base 64 string back to binary.
         *
         * This operation is the inverse of binaryToString64.
         *
         * You take ownership of the pointer assigned to *binary.
         *
         * @param base64string The base 64 encoded string to decode.
         * @param binary A pointer to a pointer to assign with a `new unsigned char[]`
         *        allocated buffer containing the decoded binary data.
         * @param binarysize A pointer to a variable that will be assigned the size of the buffer allocated.
         */
        static void base64ToBinary(const char *base64string, unsigned char **binary, size_t* binarysize);

        /**
         * @brief Add entropy to internal random number generators
         *
         * It's recommended to call this function with random data specially to
         * enhance security,
         *
         * @param data Byte array with random data
         * @param size Size of the byte array (in bytes)
         */
        void addEntropy(char* data, unsigned int size);

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
         * If not possible to retrieve the DNS servers from the system, in iOS, this request will fail with
         * the error code MegaError::API_EACCESS in onRequestFinish().
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
         * @brief Use custom DNS servers
         *
         * The SDK tries to automatically get and use DNS servers configured in the system at startup. This function can be used
         * to override that automatic detection and use a custom list of DNS servers. It is also useful to provide working
         * DNS servers to the SDK in platforms in which it can't get them from the system (Windows Phone and Universal Windows Platform).
         *
         * Since the usage of this function implies a change in DNS servers used by the SDK, all connections are
         * closed and restarted using the new list of new DNS servers, so calling this function too often can cause
         * many retries and problems to complete requests. Please use it only at startup or when DNS servers need to be changed.
         *
         * The associated request type with this request is MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - Returns the new list of DNS servers
         *
         * @param dnsServers New list of DNS servers. It must be a list of IPs separated by a comma character ",".
         * IPv6 servers are allowed (without brackets).
         *
         * The usage of this function will trigger the callback MegaGlobalListener::onEvent and the callback
         * MegaListener::onEvent with the event type MegaEvent::EVENT_DISCONNECT.
         *
         * @param listener MegaRequestListener to track this request
         */
        void setDnsServers(const char *dnsServers, MegaRequestListener* listener = NULL);

        /**
         * @brief Check if server-side Rubbish Bin autopurging is enabled for the current account
         *
         * This function will NOT return a valid value until the callback onEvent with
         * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
         * a fetchnodes to check this value, but only when it follows a login with user and password,
         * not when an existing session is resumed.
         *
         * @return True if this feature is enabled. Otherwise false.
         */
        bool serverSideRubbishBinAutopurgeEnabled();

        /**
         * @brief Check if the account has VOIP push enabled
         *
         * This function will NOT return a valid value until the callback onEvent with
         * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
         * a fetchnodes to check this value, but only when it follows a login with user and password,
         * not when an existing session is resumed.
         *
         * @return True if this feature is enabled. Otherwise false.
         */
        bool appleVoipPushEnabled();

        /**
         * @brief Check if the new format for public links is enabled
         *
         * This function will NOT return a valid value until the callback onEvent with
         * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
         * a fetchnodes to check this value, but only when it follows a login with user and password,
         * not when an existing session is resumed.
         *
         * For not logged-in mode, you need to call MegaApi::getMiscFlags first.
         *
         * @return True if this feature is enabled. Otherwise, false.
         */
        bool newLinkFormatEnabled();

        /**
         * @brief Check if the opt-in or account unblocking SMS is allowed
         *
         * The result indicated whether the MegaApi::sendSMSVerificationCode function can be used.
         *
         * This function will NOT return a valid value until the callback onEvent with
         * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
         * a fetchnodes to check this value, but only when it follows a login with user and password,
         * not when an existing session is resumed.
         *
         * For not logged-in mode, you need to call MegaApi::getMiscFlags first.
         *
         * @return 2 = Opt-in and unblock SMS allowed.  1 = Only unblock SMS allowed.  0 = No SMS allowed
         */
        int smsAllowedState();

        /**
         * @brief Get the verified phone number for the account logged in
         *
         * Returns the phone number previously confirmed with MegaApi::sendSMSVerificationCode
         * and MegaApi::checkSMSVerificationCode.
         *
         * You take the ownership of the returned value.
         *
         * @return NULL if there is no verified number, otherwise a string containing that phone number.
         */
        char* smsVerifiedPhoneNumber();

        /**
         * @brief Check if multi-factor authentication can be enabled for the current account.
         *
         * This function will NOT return a valid value until the callback onEvent with
         * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
         * a fetchnodes to check this value, but only when it follows a login with user and password,
         * not when an existing session is resumed.
         *
         * For not logged-in mode, you need to call MegaApi::getMiscFlags first.
         *
         * @return True if multi-factor authentication can be enabled for the current account, otherwise false.
         */
        bool multiFactorAuthAvailable();

        /**
         * @brief Reset the verified phone number for the account logged in.
         *
         * The associated request type with this request is MegaRequest::TYPE_RESET_SMS_VERIFIED_NUMBER
         * If there's no verified phone number associated for the account logged in, the error code
         * provided in onRequestFinish is MegaError::API_ENOENT.
         *
         * @param listener MegaRequestListener to track this request
         */
        void resetSmsVerifiedPhoneNumber(MegaRequestListener *listener = NULL);

        /**
         * @brief Check if multi-factor authentication is enabled for an account
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
        void multiFactorAuthCheck(const char *email, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the secret code of the account to enable multi-factor authentication
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
        void multiFactorAuthGetCode(MegaRequestListener *listener = NULL);

        /**
         * @brief Enable multi-factor authentication for the account
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
        void multiFactorAuthEnable(const char *pin, MegaRequestListener *listener = NULL);

        /**
         * @brief Disable multi-factor authentication for the account
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
        void multiFactorAuthDisable(const char *pin, MegaRequestListener *listener = NULL);

        /**
         * @brief Log in to a MEGA account with multi-factor authentication enabled
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
        void multiFactorAuthLogin(const char* email, const char* password, const char* pin, MegaRequestListener *listener = NULL);

        /**
         * @brief Change the password of a MEGA account with multi-factor authentication enabled
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
        void multiFactorAuthChangePassword(const char *oldPassword, const char *newPassword, const char* pin, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the change of the email address associated to an account with multi-factor authentication enabled.
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
        void multiFactorAuthChangeEmail(const char *email, const char* pin, MegaRequestListener *listener = NULL);


        /**
         * @brief Initialize the cancellation of an account.
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
        void multiFactorAuthCancelAccount(const char* pin, MegaRequestListener *listener = NULL);

        /**
         * @brief Fetch details related to time zones and the current default
         *
         * The associated request type with this request is MegaRequest::TYPE_FETCH_TIMEZONE.
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaTimeZoneDetails - Returns details about timezones and the current default
         *
         * @param listener MegaRequestListener to track this request
         */
        void fetchTimeZone(MegaRequestListener *listener = NULL);

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
        virtual void login(const char* email, const char* password, MegaRequestListener *listener = NULL);

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
         * @brief Log in to a public folder using a folder link
         *
         * After a successful login, you should call MegaApi::fetchNodes to get filesystem and
         * start working with the folder.
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGIN.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Retuns the string "FOLDER"
         * - MegaRequest::getLink - Returns the public link to the folder
         * - MegaRequest::getPassword - Returns the auth link used for writting
         *
         * If the provided authKey is not valid, onRequestFinish will
         * be called with the error code MegaError::API_EACCESS.
         *
         * @param megaFolderLink Public link to a folder in MEGA
         * @param authKey Authentication key to write into the folder link
         * @param listener MegaRequestListener to track this request
         */
        void loginToFolder(const char* megaFolderLink, const char *authKey, MegaRequestListener *listener = NULL);
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
         * @param base64pwkey Private key returned by MegaRequest::getPrivateKey in the onRequestFinish callback of createAccount
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated The parameter stringHash is no longer for new accounts so this function will be replaced by another
         * one soon. Please use MegaApi::login (with email and password) or MegaApi::fastLogin (with session) instead when possible.
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
         * @param sessionHandle Handle of the session. Use mega::INVALID_HANDLE to cancel all sessions except the current one
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
         * - MegaRequest::getPassword - Returns the public RSA key of the user, Base64-encoded
         *
         * @param user Email or Base64 handle of the user
         * @param listener MegaRequestListener to track this request
         */
        void getUserData(const char *user, MegaRequestListener *listener = NULL);

        /**
         * @brief Fetch miscellaneous flags when not logged in
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_MISC_FLAGS.
         *
         * When onRequestFinish is called with MegaError::API_OK, the miscellaneous flags are available.
         * If you are logged in into an account, the error code provided in onRequestFinish is
         * MegaError::API_EACCESS.
         *
         * @see MegaApi::multiFactorAuthAvailable
         * @see MegaApi::newLinkFormatEnabled
         * @see MegaApi::smsAllowedState
         *
         * @param listener MegaRequestListener to track this request
         */
        void getMiscFlags(MegaRequestListener *listener = NULL);

        /**
         * @brief Trigger special account state changes for own accounts, for testing
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_DEV_COMMAND.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the first parameter
         * - MegaRequest::getEmail - Returns the second parameter
         *
         * Possible errors are:
         *  - EACCESS if the calling account is not allowed to perform this method (not a mega email account, not the right IP, etc).
         *  - EARGS if the subcommand is not present or is invalid
         *  - EBLOCKED if the target account is not allowed (this could also happen if the target account does not exist)
         *
         * Possible commands:
         *  - "aodq" - Advance ODQ Warning State
         *      If called, this will advance your ODQ warning state until the final warning state,
         *      at which point it will turn on the ODQ paywall for your account. It requires an account lock on the target account.
         *      This subcommand will return the 'step' of the warning flow you have advanced to - 1, 2, 3 or 4
         *      (the paywall is turned on at step 4)
         *
         *      Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
         *       + MegaRequest::getNumber - Returns the number of warnings (1, 2, 3 or 4).
         *
         *      Possible errors in addition to the standard dev ones are:
         *       + EFAILED - your account is not in the RED stoplight state
         *
         * @param command The subcommand for the specific operation
         * @param email Optional email of the target email's account. If null, it will use the logged-in account
         * @param listener MegaRequestListener to track this request
         * @deprecated Use MegaApi::sendOdqDevCommand instead, for new API dev commands, a new public method will
         * be created for each one
         */
        void sendDevCommand(const char *command, const char *email = nullptr, MegaRequestListener *listener = nullptr);

        /**
         * @brief Send dev API command, Advance ODQ Warning State for own accounts (for testing purposes)
         *
         * If called, this will advance your ODQ warning state until the final warning state,
         * at which point it will turn on the ODQ paywall for your account. It requires an account lock on the target account.
         * This subcommand will return the 'step' of the warning flow you have advanced to - 1, 2, 3 or 4
         * (the paywall is turned on at step 4)
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_DEV_COMMAND.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the API dev command ("aodq")
         * - MegaRequest::getEmail - Returns the target email account, or NULL if target is the logged-in account
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code is MegaError::API_OK:
         * - MegaRequest::getNumber - Returns the number of warnings (1, 2, 3 or 4).
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         *  - EACCESS if the calling account is not allowed to perform this method (not a mega email account, not the right IP, etc).
         *  - EARGS if the subcommand is not present or is invalid
         *  - EBLOCKED if the target account is not allowed (this could also happen if the target account does not exist)
         *  - EFAILED - your account is not in the RED stoplight state
         *
         * @param email Optional email of the target email's account. If null, it will use the logged-in account
         * @param listener MegaRequestListener to track this request
         */
        void sendOdqDevCommand(const char *email = nullptr, MegaRequestListener *listener = nullptr);

        /**
         * @brief Send dev API command, Set used transfer quota for own accounts (for testing purposes)
         *
         * Sets the amount of transfer quota the target user has used from their PRO allocation.
         * This subcommand can only be run with PRO users.
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_DEV_COMMAND.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the API dev command ("tq")
         * - MegaRequest::getEmail - Returns the target email account, or NULL if target is the logged-in account
         * - MegaRequest::getTotalBytes - Returns the amount of transfer quota the target has used, in bytes
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         *  - EACCESS if the calling account is not allowed to perform this method (not a mega email account, not the right IP, etc).
         *  - EARGS if the subcommand is not present or is invalid, or if target account is not PRO
         *  - EBLOCKED if the target account is not allowed (this could also happen if the target account does not exist)
         *  - EMASTERONLY - if the target is the business internal account as they don't login
         *
         * @param quota The amount of transfer quota the target has used, in bytes
         * @param email Optional email of the target email's account. If null, it will use the logged-in account
         * @param listener MegaRequestListener to track this request
         */
        void sendUsedTransferQuotaDevCommand(long long quota, const char *email = nullptr, MegaRequestListener *listener = nullptr);

        /**
         * @brief Send dev API command, Set business status for own accounts (for testing purposes)
         *
         * Sets the status of the business account. The target user can be the business internal account,
         * or a master or sub-user in the business. The status set by this method is permanent until removed,
         * it does not transition to grace period and expired over time.
         *
         * The following values are valid for business status:
         *  - set the business expired = -1
         *  - clear the status override and set the business back to status of their payments = 0
         *  - set the business active = 1
         *  - set the business in grace period = 2
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_DEV_COMMAND.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the API dev command ("bs")
         * - MegaRequest::getEmail - Returns the target email account, or NULL if target is the logged-in account
         * - MegaRequest::getAccess - Returns the business status
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         *  - EACCESS if the calling account is not allowed to perform this method (not a mega email account, not the right IP, etc).
         *  - EARGS if the subcommand is not present or is invalid, or if target account is not part of a business account
         *  - EBLOCKED if the target account is not allowed (this could also happen if the target account does not exist)
         *
         * @param businessStatus The business status
         * @param email Optional email of the target email's account. If null, it will use the logged-in account
         * @param listener MegaRequestListener to track this request
         */
        void sendBusinessStatusDevCommand(int businessStatus, const char *email = nullptr, MegaRequestListener *listener = nullptr);

        /**
         * @brief Send dev API command, Set user status for own accounts (for testing purposes)
         *
         * Sets the status of a user.
         *
         * The following values are valid for user status:
         * - Enabled = 0
         * - Suspended (generic) = 2
         * - Suspended (for payment, but not used) = 3
         * - Suspended (copyright complaint) = 4
         * - Suspended (admin full disable) = 5
         * - Suspended (admin partial disable) = 6
         * - Suspended (Emergency Takedown) = 7
         * - Suspended until SMS verified = 8
         * - Suspended until Email verified = 9
         *
         * Note that Action packets are not sent for a suspended user, but next command or action packet request will give a EBLOCKED error.
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_DEV_COMMAND.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the API dev command ("us")
         * - MegaRequest::getEmail - Returns the target email account, or NULL if target is the logged-in account
         * - MegaRequest::getNumDetails - Returns the user status
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         *  - EACCESS if the calling account is not allowed to perform this method (not a mega email account, not the right IP, etc).
         *  - EARGS if the subcommand is not present or is invalid, if the status is out of range or the target is a business internal account
         *  - EBLOCKED if the target account is not allowed (this could also happen if the target account does not exist)
         *
         * @param userStatus The user status
         * @param email Optional email of the target email's account. If null, it will use the logged-in account
         * @param listener MegaRequestListener to track this request
         */
        void sendUserStatusDevCommand(int userStatus, const char *email = nullptr, MegaRequestListener *listener = nullptr);

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
         * @brief Initialize the creation of a new MEGA account, with firstname and lastname
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getEmail - Returns the email for the account
         * - MegaRequest::getPassword - Returns the password for the account
         * - MegaRequest::getName - Returns the firstname of the user
         * - MegaRequest::getText - Returns the lastname of the user
         * - MegaRequest::getParamType - Returns the value MegaApi::CREATE_ACCOUNT
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getSessionKey - Returns the session id to resume the process
         *
         * If this request succeeds, a new ephemeral account will be created for the new user
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
         * @brief Create Ephemeral++ account
         *
         * This kind of account allows to join chat links and to keep the session in the device
         * where it was created.
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the firstname of the user
         * - MegaRequest::getText - Returns the lastname of the user
         * - MegaRequest::getParamType - Returns the value MegaApi:CREATE_EPLUSPLUS_ACCOUNT
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getSessionKey - Returns the session id to resume the process
         *
         * If this request succeeds, a new ephemeral++ account will be created for the new user.
         * The app may resume the create-account process by using MegaApi::resumeCreateAccountEphemeralPlusPlus.
         *
         * @note This account should be confirmed in same device it was created
         *
         * @param firstname Firstname of the user
         * @param lastname Lastname of the user
         * @param listener MegaRequestListener to track this request
         */
        void createEphemeralAccountPlusPlus(const char* firstname, const char* lastname, MegaRequestListener *listener = NULL);

        /**
         * @brief Initialize the creation of a new MEGA account, with firstname and lastname
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
         * - MegaRequest::getParamType - Returns the value MegaApi::CREATE_ACCOUNT
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
        void createAccount(const char* email, const char* password, const char* firstname, const char* lastname, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener = NULL);

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
         * MegaListener::onAccountUpdate). It is also possible to cancel the registration process by
         * MegaApi::cancelCreateAccount, which invalidates the signup link associated to the ephemeral
         * session (the session will be still valid).
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getSessionKey - Returns the session id to resume the process
         * - MegaRequest::getParamType - Returns the value MegaApi::RESUME_ACCOUNT
         *
         * In case the account is already confirmed, the associated request will fail with
         * error MegaError::API_EARGS.
         *
         * @param sid Session id valid for the ephemeral account (@see MegaApi::createAccount)
         * @param listener MegaRequestListener to track this request
         */
        void resumeCreateAccount(const char* sid, MegaRequestListener *listener = NULL);


        /**
         * @brief Resume a registration process for an Ephemeral++ account
         *
         * When a user begins the account registration process by calling
         * MegaApi::createEphemeralAccountPlusPlus an ephemeral++ account is created.
         *
         * Until the user successfully confirms the signup link sent to the provided email address,
         * you can resume the ephemeral session in order to change the email address, resend the
         * signup link (@see MegaApi::sendSignupLink) and also to receive notifications in case the
         * user confirms the account using another client (MegaGlobalListener::onAccountUpdate or
         * MegaListener::onAccountUpdate). It is also possible to cancel the registration process by
         * MegaApi::cancelCreateAccount, which invalidates the signup link associated to the ephemeral
         * session (the session will be still valid).
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getSessionKey - Returns the session id to resume the process
         * - MegaRequest::getParamType - Returns the value MegaApi::RESUME_EPLUSPLUS_ACCOUNT
         *
         * In case the account is already confirmed, the associated request will fail with
         * error MegaError::API_EARGS.
         *
         * @param sid Session id valid for the ephemeral++ account (@see MegaApi::createEphemeralAccountPlusPlus)
         * @param listener MegaRequestListener to track this request
         */
        void resumeCreateAccountEphemeralPlusPlus(const char* sid, MegaRequestListener *listener = NULL);


        /**
         * @brief Cancel a registration process
         *
         * If a signup link has been generated during registration process, call this function
         * to invalidate it. The ephemeral session will not be invalidated, only the signup link.
         *
         * The associated request type with this request is MegaRequest::TYPE_CREATE_ACCOUNT.
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the value MegaApi::CANCEL_ACCOUNT
         *
         * @param listener MegaRequestListener to track this request
         */
        void cancelCreateAccount(MegaRequestListener *listener = NULL);

        /**
         * @brief Sends the confirmation email for a new account
         *
         * This function is useful to send the confirmation link again or to send it to a different
         * email address, in case the user mistyped the email at the registration form. It can only
         * be used after a successful call to MegaApi::createAccount or MegaApi::resumeCreateAccount.
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_SIGNUP_LINK.
         *
         * @param email Email for the account
         * @param name Fullname of the user (firstname + lastname)
         * @param password Password for the account
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This method will be eventually removed. Please, use the new version without the 'password' parameter
         */
        void sendSignupLink(const char* email, const char *name, const char *password, MegaRequestListener *listener = NULL);

        /**
         * @brief Sends the confirmation email for a new account
         *
         * This function is useful to send the confirmation link again or to send it to a different
         * email address, in case the user mistyped the email at the registration form. It can only
         * be used after a successful call to MegaApi::createAccount or MegaApi::resumeCreateAccount.
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_SIGNUP_LINK.
         *
         * @param email Email for the account
         * @param name Fullname of the user (firstname + lastname)
         * @param listener MegaRequestListener to track this request
         */
        void resendSignupLink(const char* email, const char *name, MegaRequestListener *listener = NULL);

        /**
         * @obsolete  This method cannot be used anymore by apps. It will always result on API_EINTERNAL.
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
         * - MegaRequest::getFlag - Returns true if the account was automatically confirmed, otherwise false
         *
         * If already logged-in into a different account, you will get the error code MegaError::API_EACCESS
         * in onRequestFinish.
         * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
         * will get the error code MegaError::API_EEXPIRED in onRequestFinish.
         * In both cases, the MegaRequest::getEmail will return the email of the account that was attempted
         * to confirm, and the MegaRequest::getName will return the name.
         *
         * @param link Confirmation link (confirm) or new signup link (newsignup)
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
         * If already logged-in into a different account, you will get the error code MegaError::API_EACCESS
         * in onRequestFinish.
         * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
         * will get the error code MegaError::API_EEXPIRED in onRequestFinish.
         * In both cases, the MegaRequest::getEmail will return the email of the account that was attempted
         * to confirm, and the MegaRequest::getName will return the name.
         *
         * @param link Confirmation link
         * @param password Password of the account
         * @param listener MegaRequestListener to track this request
         */
        void confirmAccount(const char* link, const char *password, MegaRequestListener *listener = NULL);

        /**
         * @obsolete This method cannot be used anymore by apps. It will always result on API_EINTERNAL.
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
         * @param link Recovery link (recover)
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
         * If the account is logged-in into a different account than the account for which the link
         * was generated, onRequestFinish will be called with the error code MegaError::API_EACCESS.
         *
         * @param link The recovery link sent to the user's email address.
         * @param newPwd The new password to be set.
         * @param masterKey Base64-encoded string containing the master key (optional).
         * @param listener MegaRequestListener to track this request
         */
        void confirmResetPassword(const char *link, const char *newPwd, const char *masterKey = NULL, MegaRequestListener *listener = NULL);


        /**
         * @brief Check that the provided recovery key (master key) is correct
         *
         * The associated request type with this request is MegaRequest::TYPE_CHECK_RECOVERY_KEY
         * No data in the MegaRequest object received on all callbacks
         *
         * @param link The recovery link sent to the user's email address.
         * @param recoveryKey Base64-encoded string containing the recoveryKey (masterKey).
         * @param listener MegaRequestListener to track this request
         */
        void checkRecoveryKey(const char* link, const char* recoveryKey, MegaRequestListener* listener = NULL);

        /**
         * @brief Initialize the cancellation of an account.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_CANCEL_LINK.
         *
         * If this request succeeds, a cancellation link will be sent to the email address of the user.
         * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
         *
         * If the MEGA account is a sub-user business account, onRequestFinish will
         * be called with the error code MegaError::API_EMASTERONLY.
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
         * @param link Cancel link (cancel)
         * @param listener MegaRequestListener to track this request
         */
        void queryCancelLink(const char *link, MegaRequestListener *listener = NULL);

        /**
         * @brief Effectively parks the user's account without creating a new fresh account.
         *
         * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
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
         * @brief Allow to resend the verification email for Weak Account Protection
         *
         * The verification email will be resent to the same address as it was previously sent to.
         *
         * This function can be called if the reason for being blocked is:
         *      700: the account is supended for Weak Account Protection.
         *
         * If the logged in account is not suspended or is suspended for some other reason,
         * onRequestFinish will be called with the error code MegaError::API_EACCESS.
         *
         * If the logged in account has not been sent the unlock email before,
         * onRequestFinish will be called with the error code MegaError::API_EARGS.
         *
         * If the logged in account has already sent the unlock email and until it's available again,
         * onRequestFinish will be called with the error code MegaError::API_ETEMPUNAVAIL.
         *
         * @param listener MegaRequestListener to track this request
         */
        void resendVerificationEmail(MegaRequestListener *listener = NULL);

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
         * If the MEGA account is a sub-user business account, onRequestFinish will
         * be called with the error code MegaError::API_EMASTERONLY.
         *
         * @param email The new email to be associated to the account.
         * @param listener MegaRequestListener to track this request
         */
        void changeEmail(const char *email, MegaRequestListener *listener = NULL);

        /**
         * @brief Get information about a change-email link created by MegaApi::changeEmail.
         *
         * The associated request type with this request is MegaRequest::TYPE_QUERY_RECOVERY_LINK
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getLink - Returns the change-email link
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getEmail - Return the email associated with the link
         *
         * If the account is logged-in into a different account than the account for which the link
         * was generated, onRequestFinish will be called with the error code MegaError::API_EACCESS.
         *
         * @param link Change-email link (verify)
         * @param listener MegaRequestListener to track this request
         */
        void queryChangeEmailLink(const char *link, MegaRequestListener *listener = NULL);

        /**
         * @brief Effectively changes the email address associated to the account.
         *
         * If no user is logged in, you will get the error code MegaError::API_EACCESS in onRequestFinish().
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
         * If the account is logged-in into a different account than the account for which the link
         * was generated, onRequestFinish will be called with the error code MegaError::API_EACCESS.
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
         * @param listener MegaRequestListener to track this request
         * @see MegaProxy
         */
        void setProxySettings(MegaProxy *proxySettings, MegaRequestListener *listener = NULL);

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
         * @return 0 if not logged in, Otherwise, a number > 0
         */
        int isLoggedIn();

        /**
         * @brief Check if we are logged in into an Ephemeral account ++
         * @return true if logged into an Ephemeral account ++, Otherwise return false
         */
        bool isEphemeralPlusPlus();

        /**
         * @brief Check the reason of being blocked.
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
         *          - MegaApi::ACCOUNT_NOT_BLOCKED = 0
         *              Account is not blocked in any way.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_TOS_COPYRIGHT = 200
         *              Suspension only for multiple copyright violations.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT = 300
         *              Suspension message for any type of suspension, but copyright suspension.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_SUBUSER_DISABLED = 400
         *              Subuser of the business account has been disabled.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_SUBUSER_REMOVED = 401
         *              Subuser of business account has been removed.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_SMS = 500
         *              The account is temporary blocked and needs to be verified by an SMS code.
         *
         *          - MegaApi::ACCOUNT_BLOCKED_VERIFICATION_EMAIL = 700
         *              The account is temporary blocked and needs to be verified by email (Weak Account Protection).
         *
         * If the error code in the MegaRequest object received in onRequestFinish
         * is MegaError::API_OK, the user is not blocked.
         */
        void whyAmIBlocked(MegaRequestListener *listener = NULL);

        /**
         * @brief Create a contact link
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
        void contactLinkCreate(bool renew = false, MegaRequestListener *listener = NULL);

        /**
         * @brief Get information about a contact link
         *
         * The associated request type with this request is MegaRequest::TYPE_CONTACT_LINK_QUERY.
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the contact link
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getParentHandle - Returns the userhandle of the contact
         * - MegaRequest::getEmail - Returns the email of the contact
         * - MegaRequest::getName - Returns the first name of the contact
         * - MegaRequest::getText - Returns the last name of the contact
         * - MegaRequest::getFile - Returns the avatar of the contact (JPG with Base64 encoding)
         *
         * @param handle Handle of the contact link to check
         * @param listener MegaRequestListener to track this request
         */
        void contactLinkQuery(MegaHandle handle, MegaRequestListener *listener = NULL);

        /**
         * @brief Delete a contact link
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
        void contactLinkDelete(MegaHandle handle = INVALID_HANDLE, MegaRequestListener *listener = NULL);

        /**
         * @brief Command to keep mobile apps alive when needed
         *
         * When this feature is enabled, API servers will regularly send push notifications
         * to keep the application running. Before using this function, it's needed to register
         * a notification token using MegaApi::registerPushNotifications
         *
         * The associated request type with this request is MegaRequest::TYPE_KEEP_ME_ALIVE.
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getParamType - Returns the type send in the first parameter
         * - MegaRequest::getFlag - Returns true when the feature is being enabled, otherwise false
         *
         * @param type Type of keep alive desired
         * Valid values for this parameter:
         * - MegaApi::KEEP_ALIVE_CAMERA_UPLOADS = 0
         *
         * @param enable True to enable this feature, false to disable it
         * @param listener MegaRequestListener to track this request
         *
         * @see MegaApi::registerPushNotifications
         */
        void keepMeAlive(int type, bool enable, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the next PSA (Public Service Announcement) that should be shown to the user
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
         * code MegaError::API_ENOENT.
         *
         * @param listener MegaRequestListener to track this request
         * @see MegaApi::setPSA
         */
        void getPSA(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the next PSA (Public Service Announcement) that should be shown to the user
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
         * Depending on the format of the PSA, the request may additionally return, for the new format:
         * - MegaRequest::getEmail - Returns the URL (or an empty string))
         * - MegaRequest::getName - Returns the title of the PSA
         * - MegaRequest::getText - Returns the text of the PSA
         * - MegaRequest::getFile - Returns the URL of the image of the PSA
         * - MegaRequest::getPassword - Returns the text for the possitive button (or an empty string)
         * - MegaRequest::getLink - Returns the link for the possitive button (or an empty string)
         *
         * If there isn't any new PSA to show, onRequestFinish will be called with the error
         * code MegaError::API_ENOENT.
         *
         * @param listener MegaRequestListener to track this request
         * @see MegaApi::setPSA
         */
        void getPSAWithUrl(MegaRequestListener *listener = NULL);

        /**
         * @brief Notify API servers that a PSA (Public Service Announcement) has been already seen
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
        void setPSA(int id, MegaRequestListener *listener = NULL);

        /**
        * @brief Command to acknowledge user alerts.
        *
        * Other clients will be notified that alerts to this point have been seen.
        *
        * The associated request type with this request is MegaRequest::TYPE_USERALERT_ACKNOWLEDGE.
        *
        * @param listener MegaRequestListener to track this request
        *
        * @see MegaApi::getUserAlerts
        */
        void acknowledgeUserAlerts(MegaRequestListener *listener = NULL);

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
         * @brief Get the timestamp when the account was created
         * @return Timestamp when the account was created
         */
        int64_t getAccountCreationTs();

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
         * @brief Returns whether MEGA Achievements are enabled for the open account
         * @return True if enabled, false otherwise.
         */
        bool isAchievementsEnabled();

        /**
         * @brief Check if the account is a business account.
         *
         * For accounts under Pro Flexi plans, this method also returns true.
         *
         * @return returns true if it's a business account, otherwise false
         */
        bool isBusinessAccount();

        /**
         * @brief Check if the account is a master account.
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
        bool isMasterBusinessAccount();

        /**
         * @brief Check if the business account is active or not.
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
        bool isBusinessAccountActive();

        /**
         * @brief Get the status of a business account.
         *
         * @return Returns the business account status, possible values:
         *      MegaApi::BUSINESS_STATUS_EXPIRED = -1
         *      MegaApi::BUSINESS_STATUS_INACTIVE = 0
         *      MegaApi::BUSINESS_STATUS_ACTIVE = 1
         *      MegaApi::BUSINESS_STATUS_GRACE_PERIOD = 2
         */
        int getBusinessStatus();

        /**
         * @brief Returns the deadline to remedy the storage overquota situation
         *
         * This value is valid only when MegaApi::getUserData has been called after
         * receiving a callback MegaListener/MegaGlobalListener::onEvent of type
         * MegaEvent::EVENT_STORAGE, reporting STORAGE_STATE_PAYWALL.
         * The value will become invalid once the state of storage changes.
         *
         * @return Timestamp representing the deadline to remedy the overquota
         */
        int64_t getOverquotaDeadlineTs();

        /**
         * @brief Returns when the user was warned about overquota state
         *
         * This value is valid only when MegaApi::getUserData has been called after
         * receiving a callback MegaListener/MegaGlobalListener::onEvent of type
         * MegaEvent::EVENT_STORAGE, reporting STORAGE_STATE_PAYWALL.
         * The value will become invalid once the state of storage changes.
         *
         * You take the ownership of the returned value.
         *
         * @return MegaIntegerList with the timestamp corresponding to each warning
         */
        MegaIntegerList *getOverquotaWarningsTs();

        /**
         * @brief Check if the password is correct for the current account
         * @param password Password to check
         * @return True if the password is correct for the current account, otherwise false.
         */
        bool checkPassword(const char *password);

        /**
         * @brief Returns the credentials of the currently open account
         *
         * If the MegaApi object isn't logged in or there's no signing key available,
         * this function returns NULL
         *
         * You take the ownership of the returned value.
         * Use delete [] to free it.
         *
         * @return Fingerprint of the signing key of the current account
         */
        char* getMyCredentials();

        /**
         * Returns the credentials of a given user
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns MegaApi::USER_ATTR_ED25519_PUBLIC_KEY
         * - MegaRequest::getFlag - Returns true
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getPassword - Returns the credentials in hexadecimal format
         *
         * @param user MegaUser of the contact (see MegaApi::getContact) to get the fingerprint
         * @param listener MegaRequestListener to track this request
         */
        void getUserCredentials(MegaUser *user, MegaRequestListener *listener = NULL);

        /**
         * @brief Checks if credentials are verified for the given user
         *
         * @param user MegaUser of the contact whose credentiasl want to be checked
         * @return true if verified, false otherwise
         */
        bool areCredentialsVerified(MegaUser *user);

        /**
         * @brief Verify credentials of a given user
         *
         * This function allow to tag credentials of a user as verified. It should be called when the
         * logged in user compares the fingerprint of the user (provided by an independent and secure
         * method) with the fingerprint shown by the app (@see MegaApi::getUserCredentials).
         *
         * The associated request type with this request is MegaRequest::TYPE_VERIFY_CREDENTIALS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns userhandle
         *
         * @param user MegaUser of the contact whose credentials want to be verified
         * @param listener MegaRequestListener to track this request
         */
        void verifyCredentials(MegaUser *user, MegaRequestListener *listener = NULL);

        /**
         * @brief Reset credentials of a given user
         *
         * Call this function to forget the existing authentication of keys and signatures for a given
         * user. A full reload of the account will start the authentication process again.
         *
         * The associated request type with this request is MegaRequest::TYPE_VERIFY_CREDENTIALS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns userhandle
         * - MegaRequest::getFlag - Returns true
         *
         * @param user MegaUser of the contact whose credentials want to be reset
         * @param listener MegaRequestListener to track this request
         */
        void resetCredentials(MegaUser *user, MegaRequestListener *listener = NULL);

        /**
         * @brief Returns RSA private key of the currently logged-in account
         *
         * If the MegaApi object is not logged-in or there is no private key available,
         * this function returns NULL.
         *
         * You take the ownership of the returned value.
         * Use delete [] to free it.
         *
         * @return RSA private key of the current account
         */
        char *getMyRSAPrivateKey();

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
        * @brief Turn on extra detailed logging for some modules
        *
        * Sometimes we need super detailed logging to investigate complicated issues
        * However for log size under normal conditions it's not practical to turn that on
        * This function allows that super detailed logging to be enabled just for
        * the module in question.
        *
        * @param networking Enable detailed extra logging for networking
        * @param syncs Enable detailed extra logging for syncs
        */
        void setLogExtraForModules(bool networking, bool syncs);

        /**
         * @brief Set the limit of size to requests payload
         *
         * This functions sets the max size that will be allowed for requests payload
         * If the payload exceeds that, the line will be truncated in the midle with [...] in between
         */
        static void setMaxPayloadLogSize(long long maxSize);

        /**
         * @brief Enable log to console
         *
         * This function is only relevant if non-exclusive loggers are used.
         * For exclusive logging (ie, only one logger and no locking before it's called back)
         * the exclusive logger can easily output to console itself.
         *
         * By default, log to console is false. Logging to console is serialized via a mutex to
         * avoid interleaving by multiple threads, even in performance mode.
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
         * In performance mode, it is assumed that this is only called on startup and
         * not while actively logging.
         *
         * @param megaLogger MegaLogger implementation
         * @param singleExclusiveLogger If set, this is the only logger that will be called, and no mutexes will be locked before calling it.
         */
        static void addLoggerObject(MegaLogger *megaLogger, bool singleExclusiveLogger = false);

        /**
         * @brief Remove a MegaLogger implementation to stop receiving SDK logs
         *
         * If the logger was registered in the past, it will stop receiving log
         * messages after the call to this function.
         *
         * In exclusive mode, it is assumed that this is only called on shutdown and
         * not while actively logging.  There is no locking on the exclusive log callback pointer,
         * so there may already be threads deep in the logging functions.  Clearing this
         * callback pointer won't stop those or wait for them to complete.  So you can't
         * immediately delete the logger after calling this, unless you know for sure
         * that no threads are logging.  Recommendation is to stop all other threads before calling this.
         *
         * @param megaLogger Previously registered MegaLogger implementation
         * @param singleExclusiveLogger If an exclusive logger was previously set, use this flag to remove it.
         */
        static void removeLoggerObject(MegaLogger *megaLogger, bool singleExclusiveLogger = false);

        /**
         * @brief
         * Specify a reporter to receive filename anomaly messages from the SDK.
         *
         * @param reporter
         * The reporter that should receive filename anomaly messages.
         *
         * Note that null is a valid value for this parameter and if
         * specified, will prevent the SDK from sending messages to the
         * reporter previously specified using this function.
         *
         * @see MegaFilenameAnomalyReporter
         */
        void setFilenameAnomalyReporter(MegaFilenameAnomalyReporter* reporter);

        /**
         * @brief Send a log to the logging system
         *
         * This log will be received by the active logger object (MegaApi::setLoggerObject) if
         * the log level is the same or lower than the active log level (MegaApi::setLogLevel)
         *
         * The third and the fouth parameter are optional. You may want to use __FILE__ and __LINE__
         * to complete them.
         *
         * In performance mode, only logging to console is serialized through a mutex.
         * Logging to `MegaLogger`s is not serialized and has to be done by the subclasses if needed.
         *
         * @param logLevel Log level for this message
         * @param message Message for the logging system
         * @param filename Origin of the log message
         * @param line Line of code where this message was generated
         */
        static void log(int logLevel, const char* message, const char *filename = "", int line = -1);

        /**
         * @brief Differentiate MegaApi log output from different instances.
         *
         * If multiple MegaApi instances are used in a single application, it can be useful to
         * distinguish their activity in the log. Setting a name here for this instance will
         * cause some particularly relevant log lines to contain it.
         * A very short name is best to avoid increasing the log size too much.
         *
         * @param loggingName Name of this instance, to be output in log messages from this MegaApi
         * or NULL to clear a previous logging name.
         */
        void setLoggingName(const char* loggingName);

#ifdef USE_ROTATIVEPERFORMANCELOGGER
        /**
         * @brief Enable rotative performance logger
         *
         * Rotative performance logger is a logger that optimizes performance by carrying
         * most of the logging tasks (write to file, duplicate log detection, log archive
         * rotation, compression and cleanup) in a separate background thread.
         * Also provides log rotation: archived log files are suffixed with the timestamp
         * of the moment when they are created. For more information about log archive
         * control see RotativePerformanceLogger::setArchiveTimestamps().
         *
         * @param logPath Absolute path pointing to the base directory for both active log file and archived logs
         * @param logFileName Log file name (without path).¡
         * @param logToStdOut if true, logs are also output to standard output
         * @param archivedFilesAgeSeconds Number of seconds before archived files are removed. Defaults to one month.
         */
        static void setUseRotativePerformanceLogger(const char * logPath, const char * logFileName, bool logToStdOut = true, long int archivedFilesAgeSeconds = 30 * 86400);
#endif
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
         * - MegaRequest::getFlag - True if target folder (\c parent) was overriden
         *
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node Node to move
         * @param newParent New parent for the node
         * @param listener MegaRequestListener to track this request
         */
        void moveNode(MegaNode* node, MegaNode* newParent, MegaRequestListener *listener = NULL);

        /**
         * @brief Move a node in the MEGA account changing the file name
         *
         * The associated request type with this request is MegaRequest::TYPE_MOVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node to move
         * - MegaRequest::getParentHandle - Returns the handle of the new parent for the node
         * - MegaRequest::getName - Returns the name for the new node
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getFlag - True if target folder (\c newParent) was overriden
         *
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node Node to move
         * @param newParent New parent for the node
         * @param newName Name for the new node
         * @param listener MegaRequestListener to track this request
         */
        void moveNode(MegaNode* node, MegaNode* newParent, const char* newName, MegaRequestListener *listener = NULL);

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
         * - MegaRequest::getFlag - True if target folder (\c newParent) was overriden
         *
         * @note In case the target folder was overriden, the MegaRequest::getParentHandle still keeps
         * the handle of the original target folder. You can check the final parent by checking the
         * value returned by MegaNode::getParentHandle
         *
         * If the status of the business account is expired, onRequestFinish will be called with the error
         * code MegaError::API_EBUSINESSPASTDUE.
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
         * If the status of the business account is expired, onRequestFinish will be called with the error
         * code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node Node to copy
         * @param newParent Parent for the new node
         * @param newName Name for the new node
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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
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
         * If the MEGA account is a sub-user business account, onRequestFinish will
         * be called with the error code MegaError::API_EMASTERONLY.
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
         * If the MEGA account is a sub-user business account, onRequestFinish will
         * be called with the error code MegaError::API_EMASTERONLY.
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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @obsolete The Inbox rootnode has been recycled for Vault and will no longer
         * accept to put nodes in user's Inbox. This method could be removed in the future.
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
        * If the MEGA account is a business account and it's status is expired, onRequestFinish will
        * be called with the error code MegaError::API_EBUSINESSPASTDUE.
        *
        * @obsolete The Inbox rootnode has been recycled for Vault and will no longer
        * accept to put nodes in user's Inbox. This method could be removed in the future.
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
         * - MegaRequest::getFlag - True if target folder (\c parent) was overriden
         *
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param megaFileLink Public link to a file in MEGA
         * @param listener MegaRequestListener to track this request
         */
        void getPublicNode(const char* megaFileLink, MegaRequestListener *listener = NULL);

        /**
         * @brief Get downloads urls for a node
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_DOWNLOAD_URLS
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getName - Returns semicolon-separated download URL(s) to the file
         * - MegaRequest::getLink - Returns semicolon-separated IPv4 of the server in the URL(s)
         * - MegaRequest::getText - Returns semicolon-separated IPv6 of the server in the URL(s)
         *
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node Node to get the downloads URLs
         * @param singleUrl Always return one URL (even for raided files)
         * @param listener MegaRequestListener to track this request
         */
        void getDownloadUrl(MegaNode* node, bool singleUrl, MegaRequestListener *listener = nullptr);

        /**
         * @brief Build the URL for a public link
         *
         * @note This function does not create the public link itself. It simply builds the URL
         * from the provided data.
         *
         * You take the ownership of the returned value.
         *
         * @param publicHandle Public handle of the link, in B64url encoding.
         * @param key Encryption key of the link.
         * @param isFolder True for folder links, false for file links.
         * @return The public link for the provided data
         */
        const char *buildPublicLink(const char *publicHandle, const char *key, bool isFolder);

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
         * @param email_or_handle Email or user handle (Base64 encoded) to get the avatar. If this parameter is
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
         * @brief Get the secondary color for the avatar.
         *
         * This color should be used only when the user doesn't have an avatar, making a
         * gradient in combination with the color returned from getUserAvatarColor.
         *
         * You take the ownership of the returned value.
         *
         * @param user MegaUser to get the color of the avatar.
         * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
         */
        static char *getUserAvatarSecondaryColor(MegaUser *user);

        /**
         * @brief Get the secondary color for the avatar.
         *
         * This color should be used only when the user doesn't have an avatar, making a
         * gradient in combination with the color returned from getUserAvatarColor.
         *
         * You take the ownership of the returned value.
         *
         * @param userhandle User handle (Base64 encoded) to get the avatar.
         * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
         */
        static char *getUserAvatarSecondaryColor(const char *userhandle);

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
         * MegaApi::USER_ATTR_RICH_PREVIEWS = 18
         * Get whether user generates rich-link messages or not (private)
         * MegaApi::USER_ATTR_RUBBISH_TIME = 19
         * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
         * MegaApi::USER_ATTR_STORAGE_STATE = 21
         * Get the state of the storage (private non-encrypted)
         * MegaApi::USER_ATTR_GEOLOCATION = 22
         * Get the user geolocation (private)
         * MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER = 23
         * Get the target folder for Camera Uploads (private)
         * MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER = 24
         * Get the target folder for My chat files (private)
         * MegaApi::USER_ATTR_PUSH_SETTINGS = 25
         * Get whether user has push settings enabled (private)
         * MegaApi::USER_ATTR_ALIAS = 27
         * Get the list of the users's aliases (private)
         * MegaApi::USER_ATTR_DEVICE_NAMES = 30
         * Get the list of device names (private)
         * MegaApi::USER_ATTR_MY_BACKUPS_FOLDER = 31
         * Get the target folder for My Backups (private)
         * MegaApi::USER_ATTR_COOKIE_SETTINGS = 33
         * Get whether user has Cookie Settings enabled
         * MegaApi::USER_ATTR_JSON_SYNC_CONFIG_DATA = 34
         * Get name and key to cypher sync-configs file
         * MegaApi::USER_ATTR_DRIVE_NAMES = 35
         * Get external drive names by id
         * MegaApi::USER_ATTR_NO_CALLKIT = 36
         * Get whether user has iOS CallKit disabled or enabled (private, non-encrypted)
         *
         * @param listener MegaRequestListener to track this request
         */
        void getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener = NULL);

        /**
         * @brief Get public attributes of participants of public chats during preview mode.
         *
         * Other's public attributes are retrievable by contacts and users who participates in your chats.
         * During a preview of a public chat, the user does not fullfil the above requirements, so the
         * public handle of the chat being previewed is required as authorization.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type
         * - MegaRequest::getEmail - Returns the email or the handle of the user (the provided one as parameter)
         * - MegaRequest::getSessionKey - Returns the public handle of the chat
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Returns the value for public attributes
         *
         * @param email_or_handle Email or user handle (Base64 encoded) to get the attribute.
         * This parameter cannot be NULL.
         * @param type Attribute type
         *
         * Valid values are:
         *
         * MegaApi::USER_ATTR_AVATAR = 0
         * Get the avatar of the user (public)
         * MegaApi::USER_ATTR_FIRSTNAME = 1
         * Get the firstname of the user (public)
         * MegaApi::USER_ATTR_LASTNAME = 2
         * Get the lastname of the user (public)
         * MegaApi::USER_ATTR_ED25519_PUBLIC_KEY = 5
         * Get the public key Ed25519 of the user (public)
         * MegaApi::USER_ATTR_CU25519_PUBLIC_KEY = 6
         * Get the public key Cu25519 of the user (public)
         *
         * @param listener MegaRequestListener to track this request
         */
        void getChatUserAttribute(const char *email_or_handle, int type, const char *ph, MegaRequestListener *listener = NULL);

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
         * MegaApi::USER_ATTR_RUBBISH_TIME = 19
         * Get number of days for rubbish-bin cleaning scheduler (private non-encrypted)
         * MegaApi::USER_ATTR_STORAGE_STATE = 21
         * Get the state of the storage (private non-encrypted)
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
        void getUserAttribute(int type, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the name associated to a user attribute
         *
         * You take the ownership of the returned value.
         *
         * @param attr Attribute
         * @return name associated to the user attribute
         */
        const char *userAttributeToString(int attr);

        /**
         * @brief Get the long descriptive name associated to a user attribute
         *
         * You take the ownership of the returned value.
         *
         * @param attr Attribute
         * @return descriptive name associated to the user attribute
         */
        const char *userAttributeToLongName(int attr);

        /**
         * @brief Get numeric value for user attribute given a string
         * @param name Name of the attribute
         * @return numeric value for user attribute
         */
        int userAttributeFromString(const char *name);

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
         * @brief Uploads a thumbnail as part of a background media file upload
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getMegaBackgroundMediaUploadPtr - Returns the background upload object
         * - MegaRequest::getFile - Returns the source path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaRequest::getNodeHandle - The handle of the uploaded file attribute.
         *
         * Use the result in the MegaRequest::getNodeHandle as the thumbnail handle in the
         * call to MegaApi::backgroundMediaUploadComplete.
         *
         * @param bu the MegaBackgroundMediaUpload that the fingernail will be assoicated with
         * @param srcFilePath Source path of the file that will be set as thumbnail
         * @param listener MegaRequestListener to track this request
         */
        void putThumbnail(MegaBackgroundMediaUpload* bu, const char *srcFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Set the thumbnail of a MegaNode, via the result of MegaApi::putThumbnail
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getNumber - Returns the attribute handle
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         *
         * @param node MegaNode to set the thumbnail
         * @param fileattribute The result handle from a previous call to MegaApi::putThumbnail
         * @param listener MegaRequestListener to track this request
         */
        void setThumbnailByHandle(MegaNode* node, MegaHandle fileattribute, MegaRequestListener *listener = NULL);

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
         * @brief Uploads a preview as part of a background media file upload
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getMegaBackgroundMediaUploadPtr - Returns the background upload object
         * - MegaRequest::getFile - Returns the source path
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_THUMBNAIL
         *
         * This value is valid for these requests in onRequestFinish when the
         * error code is MegaError::API_OK:
         * - MegaRequest::getNodeHandle - The handle of the uploaded file attribute.
         *
         * Use the result in the MegaRequest::getNodeHandle as the preview handle in the
         * call to MegaApi::backgroundMediaUploadComplete.
         *
         * @param bu the MegaBackgroundMediaUpload that the fingernail will be assoicated with
         * @param srcFilePath Source path of the file that will be set as thumbnail
         * @param listener MegaRequestListener to track this request
         */
        void putPreview(MegaBackgroundMediaUpload* bu, const char *srcFilePath, MegaRequestListener *listener = NULL);

        /**
         * @brief Set the preview of a MegaNode, via the result of MegaApi::putPreview
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_FILE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getNumber - Returns the attribute handle
         * - MegaRequest::getParamType - Returns MegaApi::ATTR_TYPE_PREVIEW
         *
         * @param node MegaNode to set the preview of
         * @param fileattribute The result handle from a previous call to MegaApi::putPreview
         * @param listener MegaRequestListener to track this request
         */
        void setPreviewByHandle(MegaNode* node, MegaHandle fileattribute, MegaRequestListener *listener = NULL);

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
         * @brief Confirm available memory to avoid OOM situations
         *
         * Before queueing a thumbnail or preview upload (or other memory intensive task),
         * it may be useful on some devices to check if there is plenty of memory available
         * in the memory pool used by MegaApi (especially since some platforms may not have
         * the facility to check for themselves, and/or deallocation may need to wait on a GC)
         * and if not, delay until any current resource constraints (eg. other current operations,
         * or other RAM-hungry apps in the device), have finished. This function just
         * makes several memory allocations and then immediately releases them. If all allocations
         * succeeded, it returns true, indicating that memory is (probably) available.
         * Of course, another app or operation may grab that memory immediately so it's not a
         * guarantee. However it may help to reduce the frequency of OOM situations on phones for example.
         *
         * @param allocCount The number of allocations to make
         * @param allocSize The size of those memory allocations.
         * @return True if all the allocations succeeded
         */
        bool testAllocation(unsigned allocCount, size_t allocSize);

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
         * MegaApi::USER_ATTR_RUBBISH_TIME = 19
         * Set number of days for rubbish-bin cleaning scheduler (private non-encrypted)
         * MegaApi::USER_ATTR_NO_CALLKIT = 36
         * Set whether user has iOS CallKit disabled or enabled (private, non-encrypted)
         *
         * If the MEGA account is a sub-user business account, and the value of the parameter
         * type is equal to MegaApi::USER_ATTR_FIRSTNAME or MegaApi::USER_ATTR_LASTNAME
         * onRequestFinish will be called with the error code MegaError::API_EMASTERONLY.
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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node Node that will receive the attribute
         * @param attrName Name of the custom attribute.
         * The length of this parameter must be between 1 and 7 UTF8 bytes
         * @param value Value for the attribute
         * @param listener MegaRequestListener to track this request
         */
        void setCustomNodeAttribute(MegaNode *node, const char *attrName, const char* value, MegaRequestListener *listener = NULL);

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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node Node that will receive the information.
         * @param duration Length of the audio/video in seconds.
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated Since the SDK started processing media information internally,
         * it is no longer needed nor recommended to use this function, so it will
         * be removed in a short time.
         */
        void setNodeDuration(MegaNode *node, int duration, MegaRequestListener *listener = NULL);

        /**
         * @brief Set node label as a node attribute.
         * Valid values for label attribute are:
         *  - MegaNode::NODE_LBL_RED = 1
         *  - MegaNode::NODE_LBL_ORANGE = 2
         *  - MegaNode::NODE_LBL_YELLOW = 3
         *  - MegaNode::NODE_LBL_GREEN = 4
         *  - MegaNode::NODE_LBL_BLUE = 5
         *  - MegaNode::NODE_LBL_PURPLE = 6
         *  - MegaNode::NODE_LBL_GREY = 7
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
         * - MegaRequest::getNumDetails - Returns the label for the node
         * - MegaRequest::getFlag - Returns true (official attribute)
         * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_LABEL
         *
         * @param node Node that will receive the information.
         * @param label Label of the node
         * @param listener MegaRequestListener to track this request
         */
        void setNodeLabel(MegaNode *node, int label, MegaRequestListener *listener =  NULL);

        /**
         * @brief Remove node label
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
         * - MegaRequest::getFlag - Returns true (official attribute)
         * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_LABEL
         *
         * @param node Node that will receive the information.
         * @param listener MegaRequestListener to track this request
         */
        void resetNodeLabel(MegaNode *node, MegaRequestListener *listener =  NULL);

        /**
         * @brief Set node favourite as a node attribute.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node that receive the attribute
         * - MegaRequest::getNumDetails - Returns 1 if node is set as favourite, otherwise return 0
         * - MegaRequest::getFlag - Returns true (official attribute)
         * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_FAV
         *
         * @param node Node that will receive the information.
         * @param fav if true set node as favourite, otherwise remove the attribute
         * @param listener MegaRequestListener to track this request
         */
        void setNodeFavourite(MegaNode *node, bool fav, MegaRequestListener *listener = NULL);

        /**
         * @brief Get a list of favourite nodes.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_NODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node provided
         * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_FAV
         * - MegaRequest::getNumDetails - Returns the count requested
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaHandleList - List of handles of favourite nodes
         *
         * @param node Node and its children that will be searched for favourites. Search all nodes if null
         * @param count if count is zero return all favourite nodes, otherwise return only 'count' favourite nodes
         * @param listener MegaRequestListener to track this request
         */
        void getFavourites(MegaNode* node, int count, MegaRequestListener* listener = nullptr);

        /**
         * @brief Set the GPS coordinates of image files as a node attribute.
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
        void setNodeCoordinates(MegaNode *node, double latitude, double longitude, MegaRequestListener *listener = NULL);

        /**
         * @brief Set the GPS coordinates of media files as a node attribute that is private
         *
         * To remove the existing coordinates, set both the latitude and longitude to
         * the value MegaNode::INVALID_COORDINATE.
         *
         * Compared to MegaApi::setNodeCoordinates, this function stores the coordinates with an extra
         * layer of encryption which only this user can decrypt, so that even if this node is shared
         * with others, they cannot read the coordinates.
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
        void setUnshareableNodeCoordinates(MegaNode *node, double latitude, double longitude, MegaRequestListener *listener = NULL);

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
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node MegaNode to get the public link
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This method will be removed in future versions. Please, start using
         * the MegaApi::exportNode signature that is not deprecated.
         */
        void exportNode(MegaNode *node, MegaRequestListener *listener = NULL);

        /**
         * @brief Generate a temporary public link of a file/folder in MEGA
         *
         * The associated request type with this request is MegaRequest::TYPE_EXPORT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getNumber - Returns expire time
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
         * @note A Unix timestamp represents the number of seconds since 00:00 hours, Jan 1, 1970 UTC
         *
         * @deprecated This method will be removed in future versions. Please, start using
         * the MegaApi::exportNode signature that is not deprecated.
         */
        void exportNode(MegaNode *node, int64_t expireTime, MegaRequestListener *listener = NULL);

        /**
         * @brief Generate a public link of a file/folder in MEGA
         *
         * The associated request type with this request is MegaRequest::TYPE_EXPORT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getAccess - Returns true
         * - MegaRequest::getNumber - Returns expire time
         * - MegaRequest::getFlag - Returns true if writable
         * - MegaRequest::getTransferTag - Returns if share key is shared with mega
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getLink - Public link
         *
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param node MegaNode to get the public link
         * @param writable if the link should be writable.
         * @param megaHosted if true, the share key of this specific folder would be shared with MEGA.
         * This is intended to be used for folders accessible though MEGA's S4 service.
         * Encryption will occur nonetheless within MEGA's S4 service.
         * @param listener MegaRequestListener to track this request
         *
         * @deprecated This method will be removed in future versions. Please, start using
         * the MegaApi::exportNode signature that is not deprecated.
         */
        void exportNode(MegaNode *node, bool writable, bool megaHosted, MegaRequestListener *listener = NULL);

        /**
         * @brief Generate a public link of a file/folder in MEGA
         *
         * The associated request type with this request is MegaRequest::TYPE_EXPORT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getAccess - Returns true
         * - MegaRequest::getNumber - Returns expire time
         * - MegaRequest::getFlag - Returns true if writable
         * - MegaRequest::getTransferTag - Returns if share key is shared with mega
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
         * @param writable if the link should be writable.
         * @param megaHosted if the share key should be shared with MEGA
         * @param listener MegaRequestListener to track this request
         */
        void exportNode(MegaNode *node, int64_t expireTime, bool writable, bool megaHosted, MegaRequestListener *listener = NULL);

        /**
         * @brief Stop sharing a file/folder
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
        void disableExport(MegaNode *node, MegaRequestListener *listener = NULL);

        /**
         * @brief Fetch the filesystem in MEGA and resumes syncs following a successful fetch
         *
         * The MegaApi object must be logged in in an account or a public folder
         * to successfully complete this request.
         *
         * The associated request type with this request is MegaRequest::TYPE_FETCH_NODES
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getFlag - Returns true if logged in into a folder and the provided key is invalid. Otherwise, false.
         * - MegaRequest::getNodeHandle - Returns the public handle if logged into a public folder. Otherwise, INVALID_HANDLE
         *
         * @param listener MegaRequestListener to track this request
         */
        void fetchNodes(MegaRequestListener *listener = NULL);

        /**
         * @brief Get the sum of sizes of all the files stored in the MEGA cloud.
         *
         * The SDK keeps a running total of the sum of the sizes of all the files stored in the cloud.
         * This function retrieves that sum, via listener in order to avoid any blocking when called
         * from a GUI thread. Provided the local state is caught up, the number will match the
         * storageUsed from MegaApi::getAccountDetails which requests data from the servers, and is much
         * quicker to retrieve.
         *
         * The MegaApi object must be logged in in an account or a public folder
         * to successfully complete this request.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_CLOUDSTORAGEUSED
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - returns the cloud storage bytes used (calculated locally from the node data structures)
         *
         * @param listener MegaRequestListener to track this request
         *
         * @obsolete The cloud storage used should not include the storage used by incoming shares, but this method does it.
         */
        void getCloudStorageUsed(MegaRequestListener *listener = NULL);

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
         * - MegaRequest::getNumDetails - Requested flags
         *
         * The available flags are:
         *  - storage quota: (numDetails & 0x01)
         *  - transfer quota: (numDetails & 0x02)
         *  - pro level: (numDetails & 0x04)
         *
         * @param listener MegaRequestListener to track this request
         */
        void getAccountDetails(MegaRequestListener *listener = NULL);

        /**
         * @brief Get details about the MEGA account
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
         * @param source code associated to trace the origin of storage requests, used for debugging purposes
         * @param listener MegaRequestListener to track this request
         */
        void getSpecificAccountDetails(bool storage, bool transfer, bool pro, int source = -1, MegaRequestListener *listener = NULL);

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
         * - MegaRequest::getCurrency - MegaCurrency object with currency data related to prices
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
         * @brief Get the payment URL for an upgrade
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
        void getPaymentId(MegaHandle productHandle, MegaHandle lastPublicHandle, MegaRequestListener *listener = NULL);

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
        void getPaymentId(MegaHandle productHandle, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener = NULL);

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
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - Returns the purchase receipt
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
        void submitPurchaseReceipt(int gateway, const char* receipt, MegaRequestListener *listener = NULL);

        /**
         * @brief Submit a purchase receipt for verification
         *
         * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the payment gateway
         * - MegaRequest::getText - Returns the purchase receipt
         * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
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
        void submitPurchaseReceipt(int gateway, const char* receipt, MegaHandle lastPublicHandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Submit a purchase receipt for verification
         *
         * The associated request type with this request is MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the payment gateway
         * - MegaRequest::getText - Returns the purchase receipt
         * - MegaRequest::getNodeHandle - Returns the last public node handle accessed
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
        void submitPurchaseReceipt(int gateway, const char *receipt, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener =  NULL);

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
         * @brief Cancel credit card subscriptions of the account
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
         * @brief Notify the user has successfully checked his password
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
        void passwordReminderDialogSucceeded(MegaRequestListener *listener = NULL);

        /**
         * @brief Notify the user has successfully skipped the password check
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
        void passwordReminderDialogSkipped(MegaRequestListener *listener = NULL);

        /**
         * @brief Notify the user wants to totally disable the password check
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
        void passwordReminderDialogBlocked(MegaRequestListener *listener = NULL);

        /**
         * @brief Check if the app should show the password reminder dialog to the user
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PWD_REMINDER
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getFlag - Returns true if the password reminder dialog should be shown
         *
         * If the corresponding user attribute is not set yet, the request will fail with the
         * error code MegaError::API_ENOENT but the value of MegaRequest::getFlag will still
         * be valid.
         *
         * @param atLogout True if the check is being done just before a logout
         * @param listener MegaRequestListener to track this request
         */
        void shouldShowPasswordReminderDialog(bool atLogout, MegaRequestListener *listener = NULL);

        /**
         * @brief Check if the master key has been exported
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
        void isMasterKeyExported(MegaRequestListener *listener = NULL);

#ifdef ENABLE_CHAT
        /**
         * @brief Enable or disable the generation of rich previews
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
         *
         * @param enable True to enable the generation of rich previews
         * @param listener MegaRequestListener to track this request
         */
        void enableRichPreviews(bool enable, MegaRequestListener *listener = NULL);

        /**
         * @brief Check if rich previews are automatically generated
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
        void isRichPreviewsEnabled(MegaRequestListener *listener = NULL);

        /**
         * @brief Check if the app should show the rich link warning dialog to the user
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
        void shouldShowRichLinkWarning(MegaRequestListener *listener = NULL);

        /**
         * @brief Set the number of times "Not now" option has been selected in the rich link warning dialog
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_RICH_PREVIEWS
         *
         * @param value Number of times "Not now" option has been selected
         * @param listener MegaRequestListener to track this request
         */
        void setRichLinkWarningCounterValue(int value, MegaRequestListener *listener = NULL);

        /**
         * @brief Enable the sending of geolocation messages
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_GEOLOCATION
         *
         * @param listener MegaRequestListener to track this request
         */
        void enableGeolocation(MegaRequestListener *listener = NULL);

        /**
         * @brief Check if the sending of geolocation messages is enabled
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
        void isGeolocationEnabled(MegaRequestListener *listener = NULL);
#endif

        /**
         * @brief Set My Chat Files target folder.
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
        void setMyChatFilesFolder(MegaHandle nodehandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Gets My chat files target folder.
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
        void getMyChatFilesFolder(MegaRequestListener *listener = NULL);

        /**
         * @brief Set Camera Uploads primary target folder.
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
        void setCameraUploadsFolder(MegaHandle nodehandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Set Camera Uploads secondary target folder.
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
         *
         * @deprecated Use MegaApi::setCameraUploadsFolders instead
         */
        void setCameraUploadsFolderSecondary(MegaHandle nodehandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Set Camera Uploads for both primary and secondary target folder.
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
        void setCameraUploadsFolders(MegaHandle primaryFolder, MegaHandle secondaryFolder, MegaRequestListener *listener = NULL);

        /**
         * @brief Gets Camera Uploads primary target folder.
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
        void getCameraUploadsFolder(MegaRequestListener *listener = NULL);

        /**
         * @brief Gets Camera Uploads secondary target folder.
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
        void getCameraUploadsFolderSecondary(MegaRequestListener *listener = NULL);

        /**
         * @brief Creates the special folder for backups ("My backups")
         *
         * It creates a new folder inside the Vault rootnode and later stores the node's
         * handle in a user's attribute, MegaApi::USER_ATTR_MY_BACKUPS_FOLDER.
         *
         * Apps should first check if this folder exists already, by calling
         * MegaApi::getUserAttribute for the corresponding attribute.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_MY_BACKUPS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - Returns the name provided as parameter
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNodehandle - Returns the node handle of the folder created
         *
         * If no user was logged in, the request will fail with the error API_EACCESS.
         * If the folder for backups already existed, the request will fail with the error API_EEXIST.
         *
         * @param localizedName Localized name for "My backups" folder
         * @param listener MegaRequestListener to track this request
         */
        void setMyBackupsFolder(const char *localizedName, MegaRequestListener *listener = nullptr);

        /**
         * @brief Gets the alias for an user
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
        void getUserAlias(MegaHandle uh, MegaRequestListener *listener = NULL);

        /**
         * @brief Set or reset an alias for a user
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
        void setUserAlias(MegaHandle uh, const char *alias, MegaRequestListener *listener = NULL);

        /**
         * @brief Get push notification settings
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PUSH_SETTINGS
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaPushNotificationSettings - Returns settings for push notifications
         *
         * @see MegaPushNotificationSettings class for more details.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getPushNotificationSettings(MegaRequestListener *listener = NULL);

        /**
         * @brief Set push notification settings
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_PUSH_SETTINGS
         * - MegaRequest::getMegaPushNotificationSettings - Returns settings for push notifications
         *
         * @see MegaPushNotificationSettings class for more details. You can prepare a new object by
         * calling MegaPushNotificationSettings::createInstance.
         *
         * @param settings MegaPushNotificationSettings with the new settings
         * @param listener MegaRequestListener to track this request
         */
        void setPushNotificationSettings(MegaPushNotificationSettings *settings, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the number of days for rubbish-bin cleaning scheduler
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
        void getRubbishBinAutopurgePeriod(MegaRequestListener *listener = NULL);

        /**
         * @brief Set the number of days for rubbish-bin cleaning scheduler
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
        void setRubbishBinAutopurgePeriod(int days, MegaRequestListener *listener = NULL);

        /**
         * @brief Returns the id of this device
         *
         * You take the ownership of the returned value.
         *
         * @return The id of this device
         */
        const char* getDeviceId() const;

        /**
         * @brief Returns the name set for this device
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DEVICE_NAMES
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getName - Returns device name.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getDeviceName(MegaRequestListener *listener = NULL);

        /**
         * @brief Sets device name
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DEVICE_NAMES
         * - MegaRequest::getName - Returns device name.
         *
         * @param deviceName String with device name
         * @param listener MegaRequestListener to track this request
         */
        void setDeviceName(const char* deviceName, MegaRequestListener *listener = NULL);

        /**
         * @brief Returns the name set for this drive
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DRIVE_NAMES
         * - MegaRequest::getFile - Returns the path to the drive
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getName - Returns drive name.
         *
         * @param pathToDrive Path to the root of the external drive
         * @param listener MegaRequestListener to track this request
         */
        void getDriveName(const char *pathToDrive, MegaRequestListener *listener = NULL);

        /**
         * @brief Sets drive name
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_DRIVE_NAMES
         * - MegaRequest::getName - Returns drive name.
         * - MegaRequest::getFile - Returns the path to the drive
         *
         * @param pathToDrive Path to the root of the external drive
         * @param driveName String with drive name
         * @param listener MegaRequestListener to track this request
         */
        void setDriveName(const char* pathToDrive, const char *driveName, MegaRequestListener *listener = NULL);

        /**
         * @brief Change the password of the MEGA account
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
         * @brief Invite another person to be your MEGA contact using a contact link handle
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
        void inviteContact(const char* email, const char* message, int action, MegaHandle contactLink, MegaRequestListener* listener = NULL);

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
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getTransferTag - Returns the keepSyncConfigsFile
         * - MegaRequest::getFlag - Returns true
         *
         * Under certain circumstances, this request might return the error code
         * MegaError::API_ESID. It should not be taken as an error, since the reason
         * is that the logout action has been notified before the reception of the
         * logout response itself.
         *
         * In case of an automatic logout (ie. when the account become blocked by
         * ToS infringment), the MegaRequest::getParamType indicates the error that
         * triggered the automatic logout (MegaError::API_EBLOCKED for the example).
         *
         * @param keepSyncConfigsFile Allow sync configs to be recovered if the same user logs in again
         *        The file containing sync configs is encrypted so there's no privacy issue.
         *        This is provided for backward compatibility for MEGAsync.
         * @param listener MegaRequestListener to track this request
         */
#ifdef ENABLE_SYNC
        void logout(bool keepSyncConfigsFile, MegaRequestListener *listener);
#else
        void logout(MegaRequestListener *listener = nullptr);
#endif
        /**
         * @brief Logout of the MEGA account without invalidating the session
         *
         * The associated request type with this request is MegaRequest::TYPE_LOGOUT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFlag - Returns false
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
         *
         * @note Event types are restricted to the following ranges:
         *  - MEGAcmd:   [98900, 99000)
         *  - MEGAchat:  [99000, 99199)
         *  - Android:   [99200, 99300)
         *  - iOS:       [99300, 99400)
         *  - MEGA SDK:  [99400, 99500)
         *  - MEGAsync:  [99500, 99600)
         *  - Webclient: [99600, 99800]
         */
        void sendEvent(int eventType, const char* message, MegaRequestListener *listener = NULL);

        /**
         * @brief Create a new ticket for support with attached description
         *
         * The associated request type with this request is MegaRequest::TYPE_SUPPORT_TICKET
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParamType - Returns the type of the ticket
         * - MegaRequest::getText - Returns the description of the issue
         *
         * @param message Description of the issue for support
         * @param type Ticket type. These are the available types:
         *          0  for General Enquiry
         *          1  for Technical Issue
         *          2  for Payment Issue
         *          3  for Forgotten Password
         *          4  for Transfer Issue
         *          5  for Contact/Sharing Issue
         *          6  for MEGAsync Issue
         *          7  for Missing/Invisible Data
         *          8  for help-centre clarifications
         *          9  for iOS issue
         *          10 for Android issue
         * @param listener MegaRequestListener to track this request
         */
        void createSupportTicket(const char* message, int type = 1, MegaRequestListener *listener = NULL);

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
         * @brief Upload a file to support
         *
         * If the status of the business account is expired, onTransferFinish will be called with the error
         * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
         * "Your business account is overdue, please contact your administrator."
         *
         * For folders, onTransferFinish will be called with error MegaError:API_EARGS;
         *
         * @param localPath Local path of the file
         * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
         * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
         * Use this parameter with caution. Set it to true only if you are sure about what are you doing.
         * @param listener MegaTransferListener to track this transfer
         */
        void startUploadForSupport(const char* localPath, bool isSourceTemporary = false, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file or a folder
         *
         * If the status of the business account is expired, onTransferFinish will be called with the error
         * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
         * "Your business account is overdue, please contact your administrator."
         *
         * When user wants to upload a batch of items that at least contains one folder, SDK mutex will be partially
         * locked until:
         *  - we have received onTransferStart for every file in the batch
         *  - we have received onTransferUpdate with MegaTransfer::getStage == MegaTransfer::STAGE_TRANSFERRING_FILES
         *    for every folder in the batch
         *
         * During this period, the only safe method (to avoid deadlocks) to cancel transfers is by calling CancelToken::cancel(true).
         * This method will cancel all transfers(not finished yet).
         *
         * Important considerations:
         *  - A cancel token instance can be shared by multiple transfers, and calling CancelToken::cancel(true) will affect all
         *    of those transfers.
         *
         *  - It's app responsibility, to keep cancel token instance alive until receive MegaTransferListener::onTransferFinish for all MegaTransfers
         *    that shares the same cancel token instance.
         *
         * For more information about MegaTransfer stages please refer to onTransferUpdate documentation.
         *
         * @param localPath Local path of the file or folder
         * @param parent Parent node for the file or folder in the MEGA account
         * @param fileName Custom file name for the file or folder in MEGA
         *  + If you don't need this param provide NULL as value
         * @param mtime Custom modification time for the file in MEGA (in seconds since the epoch)
         *  + If you don't need this param provide MegaApi::INVALID_CUSTOM_MOD_TIME as value
         * @param appData Custom app data to save in the MegaTransfer object
         * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
         * related to the transfer. If a transfer is started with exactly the same data
         * (local path and target parent) as another one in the transfer queue, the new transfer
         * fails with the error API_EEXISTS and the appData of the new transfer is appended to
         * the appData of the old transfer, using a '!' separator if the old transfer had already
         * appData.
         *  + If you don't need this param provide NULL as value
         * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
         * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
         * Use this parameter with caution. Set it to true only if you are sure about what are you doing.
         *  + If you don't need this param provide false as value
         * @param startFirst puts the transfer on top of the upload queue
         *  + If you don't need this param provide false as value
         * @param cancelToken MegaCancelToken to be able to cancel a folder/file upload process.
         * This param is required to be able to cancel the transfer safely.
         * App retains the ownership of this param.
         * @param listener MegaTransferListener to track this transfer
         */
        void startUpload(const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, const char *appData, bool isSourceTemporary, bool startFirst, MegaCancelToken *cancelToken, MegaTransferListener *listener=NULL);

        /**
         * @brief Upload a file or a folder
         *
         * This method should be used ONLY to share by chat a local file. In case the file
         * is already uploaded, but the corresponding node is missing the thumbnail and/or preview,
         * this method will force a new upload from the scratch (ensuring the file attributes are set),
         * instead of doing a remote copy.
         *
         * This method always puts the transfer on top of the upload queue.
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
         * @param fileName Custom file name for the file or folder in MEGA
         * @param listener MegaTransferListener to track this transfer
         */
        void startUploadForChat(const char *localPath, MegaNode *parent, const char *appData, bool isSourceTemporary, const char* fileName, MegaTransferListener *listener = NULL);

        /**
         * @brief Download a file or a folder from MEGA, saving custom app data during the transfer
         *
         * If the status of the business account is expired, onTransferFinish will be called with the error
         * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
         * "Your business account is overdue, please contact your administrator."
         *
         * When user wants to download a batch of items that at least contains one folder, SDK mutex will be partially
         * locked until:
         *  - we have received onTransferStart for every file in the batch
         *  - we have received onTransferUpdate with MegaTransfer::getStage == MegaTransfer::STAGE_TRANSFERRING_FILES
         *    for every folder in the batch
         *
         * During this period, the only safe method (to avoid deadlocks) to cancel transfers is by calling CancelToken::cancel(true).
         * This method will cancel all transfers(not finished yet).
         *
         * Important considerations:
         *  - A cancel token instance can be shared by multiple transfers, and calling CancelToken::cancel(true) will affect all
         *    of those transfers.
         *
         *  - It's app responsibility, to keep cancel token instance alive until receive MegaTransferListener::onTransferFinish for all MegaTransfers
         *    that shares the same cancel token instance.
         *
         * For more information about MegaTransfer stages please refer to onTransferUpdate documentation.
         *
         * @param node MegaNode that identifies the file or folder
         * @param localPath Destination path for the file or folder
         * If this path is a local folder, it must end with a '\' or '/' character and the file name
         * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
         * one of these characters, the file will be downloaded to a file in that path.
         * @param customName Custom file name for the file or folder in local destination
         *  + If you don't need this param provide NULL as value
         * @param appData Custom app data to save in the MegaTransfer object
         * The data in this parameter can be accessed using MegaTransfer::getAppData in callbacks
         * related to the transfer.
         *  + If you don't need this param provide NULL as value
         * @param startFirst puts the transfer on top of the download queue
         *  + If you don't need this param provide false as value
         * @param cancelToken MegaCancelToken to be able to cancel a folder/file download process.
         * This param is required to be able to cancel transfers safely.
         * App retains the ownership of this param.
         * @param listener MegaTransferListener to track this transfer
         */
        void startDownload(MegaNode* node, const char* localPath, const char *customName, const char *appData, bool startFirst, MegaCancelToken *cancelToken, MegaTransferListener *listener = NULL);

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
         * If the status of the business account is expired, onTransferFinish will be called with the error
         * code MegaError::API_EBUSINESSPASTDUE. In this case, apps should show a warning message similar to
         * "Your business account is overdue, please contact your administrator."
         *
         * @param node MegaNode that identifies the file
         * @param startPos First byte to download from the file
         * @param size Size of the data to download
         * @param listener MegaTransferListener to track this transfer
         */
        void startStreaming(MegaNode* node, int64_t startPos, int64_t size, MegaTransferListener *listener);

        /**
         * @brief Set the miniumum acceptable streaming speed for streaming transfers
         *
         * When streaming a file with startStreaming(), the SDK monitors the transfer rate.
         * After a few seconds grace period, the monitoring starts. If the average rate is below
         * the minimum rate specified (determined by this function, or by default a reasonable rate
         * for audio/video, then the streaming operation will fail with MegaError::API_EAGAIN.
         *
         * @param bytesPerSecond The minimum acceptable rate for streaming.
         *                       Use -1 to use the default built into the library.
         *                       Use 0 to prevent the check.
         */
        void setStreamingMinimumRate(int bytesPerSecond);

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
         * Note that retried transfers will always start first, so the user see them progressing
         * immediately.
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
         * @brief Pause/resume all transfers in one direction (uploads or downloads)
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


        /**
         * @brief Returns the folder paths of a backup
         *
         * You take ownership of the returned value.
         *
         * @param backuptag backup tag
         * @return Folder paths that contain each of the backups or NULL if tag not found.
         */
        MegaStringList *getBackupFolders(int backuptag) const;


        /**
         * @brief Starts a backup of a local folder into a remote location
         *
         * Determined by the selected period several backups will be stored in the selected location
         * If a backup with the same local folder and remote location exists, its parameters will be updated
         *
         * The associated request type with this request is MegaRequest::TYPE_ADD_SCHEDULED_COPY
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the period between backups in deciseconds (-1 if cron time used)
         * - MegaRequest::getText - Returns the cron like time string to define period
         * - MegaRequest::getFile - Returns the path of the local folder
         * - MegaRequest::getNumRetry - Returns the maximun number of backups to keep
         * - MegaRequest::getTransferTag - Returns the tag asociated with the backup
         * - MegaRequest::getFlag - Returns whether to attend past backups (ocurred while not running)
         *
         *
         * @param localPath Local path of the folder
         * @param parent MEGA folder to hold the backups
         * @param attendPastBackups attend backups that ought to have started before
         * @param period period between backups in deciseconds
         * @param periodstring cron like time string to define period
         * @param numBackups maximun number of backups to keep
         * @param listener MegaRequestListener to track this request
         *
         */
        void setScheduledCopy(const char* localPath, MegaNode *parent, bool attendPastBackups, int64_t period, const char *periodstring, int numBackups, MegaRequestListener *listener=NULL);

        /**
         * @brief Remove a backup
         *
         * The backup will stop being performed. No files in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SCHEDULED_COPY
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the tag of the deleted backup
         *
         * @param tag tag of the backup to delete
         * @param listener MegaRequestListener to track this request
         */
        void removeScheduledCopy(int tag, MegaRequestListener *listener=NULL);

        /**
         * @brief Aborts current ONGOING backup.
         *
         * This will cancell all current active backups.
         *
         * The associated request type with this request is MegaRequest::TYPE_ABORT_CURRENT_SCHEDULED_COPY
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the tag of the aborted backup
         *
         * Possible return values for this function are:
         * - MegaError::API_OK if successfully aborted an ongoing backup
         * - MegaError::API_ENOENT if backup could not be found or no ongoing backup found
         *
         * @param tag tag of the backup to delete
         */
        void abortCurrentScheduledCopy(int tag, MegaRequestListener *listener=NULL);

        /**
         * @brief Starts a timer.
         *
         * This, besides the classic timer usage, can be used to enforce a loop of the SDK thread when the time passes
         *
         * The associated request type with this request is MegaRequest::TYPE_TIMER
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the selected period

         * An OnRequestFinish will be caled when the time is passed
         *
         * @param period time to wait
         * @param listener MegaRequestListener to track this request
         *
        */
        void startTimer(int64_t period, MegaRequestListener *listener = NULL);


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
        * @deprecated This version of the function is deprecated.  Please use the non-deprecated one below.
         */
        MEGA_DEPRECATED
        void syncFolder(const char* localFolder, const char* name, MegaNode* megaFolder, MegaRequestListener* listener = NULL);

        /**
        * @deprecated This version of the function is deprecated.  Please use the non-deprecated one below.
         */
        MEGA_DEPRECATED
        void syncFolder(const char* localFolder, MegaNode* megaFolder, MegaRequestListener* listener = NULL);

        /**
        * @deprecated This version of the function is deprecated.  Please use the non-deprecated one below.
         */
        MEGA_DEPRECATED
        void syncFolder(const char* localFolder, MegaHandle megaHandle, MegaRequestListener* listener = NULL);

        /**
        * @deprecated This version of the function is deprecated.  Please use the non-deprecated one below.
         */
        MEGA_DEPRECATED
        void syncFolder(const char* localFolder, const char* name, MegaHandle megaHandle, MegaRequestListener* listener = NULL);


        /**
         * @brief Start a Sync or Backup between a local folder and a folder in MEGA
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
        void syncFolder(MegaSync::SyncType syncType, const char *localSyncRootFolder, const char *name, MegaHandle remoteSyncRootFolder,
            const char* driveRootIfExternal,
            MegaRequestListener *listener);

        /**
         * @brief Copy sync data to SDK cache.
         *
         * This function is destined to allow transition from Sync management based on Apps cache into SDK
         * based cache. You will need to call copyCachedStatus prior to this one, so that disable sync reasons are properly
         * adjusted.
         *
         * The associated request type with this request is MegaRequest::TYPE_COPY_SYNC_CONFIG
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFile - Returns the path of the local folder
         * - MegaRequest::getName - Returns the name of the sync
         * - MegaRequest::getLink - Returns the path of the remote folder
         * - MegaRequest::getNumber - Returns the local filesystem fingerprint
         * - MegaRequest::getNumDetails - Returns if sync is temporarily disabled
         * - MegaRequest::getFlag - if sync is enabled

         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getParentHandle - backupId assigned to the sync (MegaApi::copySyncDataToCache)
         *
         * @param localFolder Local folder
         * @param name Name given to the sync
         * @param megaHandle MEGA folder
         * @param remotePath MEGA folder path
         * @param localfp Filesystem fingerprint
         * @param enabled If the sync is enabled by the user
         * @param temporaryDisabled If the sync is temporarily disabled
         * @param listener MegaRequestListener to track this request
         */
        void copySyncDataToCache(const char *localFolder, const char *name, MegaHandle megaHandle, const char *remotePath,
                                 long long localfp, bool enabled, bool temporaryDisabled, MegaRequestListener *listener = NULL);
        /**
         * @brief Copy sync data to SDK cache.
         *
         * This function is destined to allow transition from Sync management based on Apps cache into SDK
         * based cache. You will need to call copyCachedStatus prior to this one, so that disable sync reasons are properly
         * adjusted.
         *
         * The associated request type with this request is MegaRequest::TYPE_COPY_SYNC_CONFIG
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
         * - MegaRequest::getFile - Returns the path of the local folder
         * - MegaRequest::getName - Returns the name of the sync
         * - MegaRequest::getLink - Returns the path of the remote folder
         * - MegaRequest::getNumber - Returns the local filesystem fingreprint
         * - MegaRequest::getNumDetails - Returns if sync is temporarily disabled
         * - MegaRequest::getFlag - if sync is enabled

         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getParentHandle - backupId assigned to the sync (MegaApi::copySyncDataToCache)
         *
         * @param localFolder Local folder
         * @param megaHandle MEGA folder
         * @param remotePath MEGA folder path
         * @param localfp Filesystem fingerprint
         * @param enabled If the sync is enabled by the user
         * @param temporaryDisabled If the sync is temporarily disabled
         * @param listener MegaRequestListener to track this request
         */
        void copySyncDataToCache(const char *localFolder, MegaHandle megaHandle, const char *remotePath,
                                 long long localfp, bool enabled, bool temporaryDisabled, MegaRequestListener *listener = NULL);
        /**
         * @brief Copy sync data to SDK cache.
         *
         * This function is destined to allow transition from some account status cached in Apps into SDK cached values.
         * This should be called before fetching nodes and copySyncDataToCache.
         *
         * The associated request type with this request is MegaRequest::TYPE_COPY_CACHED_STATUS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns storageStatus+1000*blockStatus+1000000*businessStatus
         *
         * @param storageStatus storage status. Pass 999 if not valid
         * @param blockStatus block status (0 = blocked, != 0 otherwise). Pass 999 if not valid
         * @param businessStatus business status. Pass 999 if not valid
         * @param listener MegaRequestListener to track this request
         */
        void copyCachedStatus(int storageStatus, int blockStatus, int businessStatus, MegaRequestListener *listener = NULL);

        /**
         * @brief Check the external drive specified to see if it has any Backup Syncs set up on it.
         *
         * If any are present, their configurations will be loaded, in a disabled state.
         * External Backups will overwrite whatever is in the corrsponding cloud folder, making it the same on disk.
         * Therefore you must check with the user if they wish to resume any or all of them, otherwise they may lose data.
         * You can find the one added by iterating all syncs and checking the external drive path for each.
         * For those that the user wishes to resume, use MegaApi::enableSync()
         *
         * The associated request type with this request is MegaRequest::TYPE_LOAD_EXTERNAL_BACKUPS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFile - Returns the path of the drive root
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - The MegaSync records have been added.  You can access them with MegaApi::getSyncs()
         *   and check each one's drive path.
         *
         * @param externalDriveRoot The filesystem path to the root of the external drive.
         * @param listener MegaRequestListener to track this request
         *
         */
        void loadExternalBackupSyncsFromExternalDrive(const char* externalDriveRoot, MegaRequestListener* listener);


        /**
         * @brief Prepare any external backup syncs on the specified drive, for that drive be ejected/disconnected
         *
         * If any are present and active, the sync activity is stopped, any changes to sync/backup config for
         * those backups is flushed to disk,and all assoicated file/folder handles are closed, allowing the
         * drive to be ejected.   All backup sync configs from that drive are removed from memory.
         *
         * This function may also be useful if the user just prefers not to have any sync/backup activity
         * going on ont that disk.
         *
         * The associated request type with this request is MegaRequest::TYPE_LOAD_EXTERNAL_BACKUPS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getFile - Returns the path of the drive root
         *
         * @param externalDriveRoot The filesystem path to the root of the external drive.
         * @param listener MegaRequestListener to track this request
         *
         */
        void closeExternalBackupSyncsFromExternalDrive(const char* externalDriveRoot, MegaRequestListener* listener);

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
         * - MegaRequest::getParentHandle - Returns sync backupId
         * - MegaRequest::getFlag - Returns true
         * - MegaRequest::getFile - Returns the path of the local folder (for active syncs only)
         *
         * @param backupId Identifier of the Sync (unique per user, provided by API)
         * @param backupDestination Used only by MegaSync::SyncType::TYPE_BACKUP syncs.
         *                          If INVALID_HANDLE, files will be permanently deleted, otherwise files will be moved there.
         * @param listener MegaRequestListener to track this request
         */
        void removeSync(MegaHandle backupId, MegaRequestListener *listener = NULL);

        /**
         * @brief Move or Remove the nodes that used to be part of backup.
         *
         * The folder must be in folder Vault/<device>/, and will be moved, or permanently deleted.
         * Deletion is permanent (not to trash) and is selected with destination INVALID_HANDLE.
         * To move the nodes instead, specify the destination folder in backupDestination.
         *
         * These nodes cannot be deleted with the usual remove() function as they are in the Vault.
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_OLD_BACKUP_NODES
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the deconfiguredBackupRoot handle
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - deconfiguredBackupRoot was not valid
         * - MegaError::API_EARGS - deconfiguredBackupRoot was not in the Vault,
         *                          or backupDestination was not in Files or Rubbish
         *
         * @param deconfiguredBackupRoot Identifier of the Sync (unique per user, provided by API)
         * @param backupDestination If INVALID_HANDLE, files will be permanently deleted, otherwise files will be moved there.
         * @param listener MegaRequestListener to track this request
         */
        void moveOrRemoveDeconfiguredBackupNodes(MegaHandle deconfiguredBackupRoot, MegaHandle backupDestination, MegaRequestListener *listener = NULL);

        /**
         * @brief Disable a synced folder
         *
         * The folder will stop being synced. No files in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The synchronization will stop but the cache of local files won't be deleted.
         * If you want to also delete the local cache use MegaApi::removeSync
         *
         * The associated request type with this request is MegaRequest::TYPE_DISABLE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the folder in MEGA
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
         * The associated request type with this request is MegaRequest::TYPE_DISABLE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns sync backupId
         *
         * @param backupId Identifier of the Sync (unique per user, provided by API)
         * @param listener MegaRequestListener to track this request
         */
        void disableSync(MegaHandle backupId, MegaRequestListener *listener = NULL);

        /**
         * @brief Disable a synced folder
         *
         * The folder will stop being synced. No files in the local nor in the remote folder
         * will be deleted due to the usage of this function.
         *
         * The synchronization will stop but the cache of local files won't be deleted.
         * If you want to also delete the local cache use MegaApi::removeSync
         *
         * The associated request type with this request is MegaRequest::TYPE_DISABLE_SYNC
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns sync backupId
         *
         * @param sync Synchronization to disable
         * @param listener MegaRequestListener to track this request
         */
        void disableSync(MegaSync *sync, MegaRequestListener *listener = NULL);

        /**
        * @brief Enables a synced folder
        *
        * The folder will start being synced. No files in the local nor in the remote folder
        * will be deleted due to the usage of this function.
        *
        * The associated request type with this request is MegaRequest::TYPE_ENABLE_SYNC
        * Valid data in the MegaRequest object received on callbacks:
        * - MegaRequest::getParentHandle - Returns the sync error (MegaSync::Error) in case of failure
        *
        * @param sync Synchronization to enable
        * @param listener MegaRequestListener to track this request
        */
        void enableSync(MegaSync *sync, MegaRequestListener *listener = NULL);

        /**
        * @brief Enables a synced folder
        *
        * The folder will start being synced. No files in the local nor in the remote folder
        * will be deleted due to the usage of this function.
        *
        * The associated request type with this request is MegaRequest::TYPE_ENABLE_SYNC
        * Valid data in the MegaRequest object received on callbacks:
        * - MegaRequest::getParentHandle - Returns the sync error (MegaSync::Error) in case of failure
        *
        * @param backupId Identifier of the Sync (unique per user, provided by API)
        * @param listener MegaRequestListener to track this request
        */
        void enableSync(MegaHandle backupId, MegaRequestListener *listener = NULL);

        /**
         * @brief
         * Imports internal sync configs from JSON.
         *
         * @param configs
         * A JSON string encoding the internal sync configs to import.
         *
         * @param listener
         * Listener to call back when the import has completed.
         *
         * @see exportSyncConfigs
         */
        void importSyncConfigs(const char* configs, MegaRequestListener* listener);

        /**
         * @brief
         * Exports all internal sync configs to JSON.
         *
         * @return
         * A JSON string encoding all internal sync configs.
         *
         * @see importSyncConfigs
         */
        const char* exportSyncConfigs();

        /**
         * @brief Get all configured syncs
         *
         * You take the ownership of the returned value
         *
         * @return List of MegaSync objects with all syncs
         */
        MegaSyncList* getSyncs();

        /**
         * @brief Check if the synchronization engine is scanning files
         * @return true if it is scanning, otherwise false
         */
        bool isScanning();

        /**
         * @brief Check if any synchronization is in state syncing or pending
         * @return true if it is syncing, otherwise false
         */
        bool isSyncing();

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
         * @brief Check if it's possible to start synchronizing a folder node. Return SyncError errors.
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
        MegaError* isNodeSyncableWithError(MegaNode* node);

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
         * @brief Get the synchronization identified with a backupId
         *
         * You take the ownership of the returned value
         *
         * @param backupId Identifier of the Sync (unique per user, provided by API)
         * @return Synchronization identified by the backupId
         */
        MegaSync *getSyncByBackupId(MegaHandle backupId);

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
         * @brief Get the backup identified with a tag
         *
         * You take the ownership of the returned value
         *
         * @param tag Tag that identifies the backup
         * @return Backup identified by the tag
         */
        MegaScheduledCopy *getScheduledCopyByTag(int tag);

        /**
         * @brief getScheduledCopyByNode Get the backup associated with a node
         *
         * You take the ownership of the returned value
         * Caveat: Two backups can have the same parent node, the first one encountered is returned
         *
         * @param node Root node of the backup
         * @return Backup with the specified root node
         */
        MegaScheduledCopy *getScheduledCopyByNode(MegaNode *node);

        /**
         * @brief getScheduledCopyByPath Get the backup associated with a local path
         *
         * You take the ownership of the returned value
         *
         * @param localPath Root local path of the backup
         * @return Backup with the specified root local path
         */
        MegaScheduledCopy *getScheduledCopyByPath(const char *localPath);

        /**
         * @brief Force a loop of the SDK thread
         * @deprecated This function is only here for debugging purposes. It will probably
         * be removed in future updates
         */
        void update();

        /**
         * @brief Check if the SDK is waiting to complete a request and get the reason
         * @return State of SDK.
         *
         * Valid values are:
         * - MegaApi::RETRY_NONE = 0
         * SDK is not waiting for the server to complete a request
         *
         * - MegaApi::RETRY_CONNECTIVITY = 1
         * SDK is waiting for the server to complete a request due to connectivity issues
         *
         * - MegaApi::RETRY_SERVERS_BUSY = 2
         * SDK is waiting for the server to complete a request due to a HTTP error 500
         *
         * - MegaApi::RETRY_API_LOCK = 3
         * SDK is waiting for the server to complete a request due to an API lock (API error -3)
         *
         * - MegaApi::RETRY_RATE_LIMIT = 4,
         * SDK is waiting for the server to complete a request due to a rate limit (API error -4)
         *
         * - MegaApi::RETRY_LOCAL_LOCK = 5
         * SDK is waiting for a local locked file
         *
         * - MegaApi::RETRY_UNKNOWN = 6
         * SDK is waiting for the server to complete a request with unknown reason
         *
         */
        int isWaiting();

        /**
         * @brief Check if the SDK is waiting to complete a request and get the reason
         * @return State of SDK.
         *
         * Valid values are:
         * - MegaApi::RETRY_NONE = 0
         * SDK is not waiting for the server to complete a request
         *
         * - MegaApi::RETRY_CONNECTIVITY = 1
         * SDK is waiting for the server to complete a request due to connectivity issues
         *
         * - MegaApi::RETRY_SERVERS_BUSY = 2
         * SDK is waiting for the server to complete a request due to a HTTP error 500
         *
         * - MegaApi::RETRY_API_LOCK = 3
         * SDK is waiting for the server to complete a request due to an API lock (API error -3)
         *
         * - MegaApi::RETRY_RATE_LIMIT = 4,
         * SDK is waiting for the server to complete a request due to a rate limit (API error -4)
         *
         * - MegaApi::RETRY_LOCAL_LOCK = 5
         * SDK is waiting for a local locked file
         *
         * - MegaApi::RETRY_UNKNOWN = 6
         * SDK is waiting for the server to complete a request with unknown reason
         *
         * @deprecated Use MegaApi::isWaiting instead of this function.
         */
        int areServersBusy();

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

        enum { ORDER_NONE = 0, ORDER_DEFAULT_ASC, ORDER_DEFAULT_DESC,
            ORDER_SIZE_ASC, ORDER_SIZE_DESC,
            ORDER_CREATION_ASC, ORDER_CREATION_DESC,
            ORDER_MODIFICATION_ASC, ORDER_MODIFICATION_DESC,
            ORDER_ALPHABETICAL_ASC, ORDER_ALPHABETICAL_DESC,
            ORDER_PHOTO_ASC, ORDER_PHOTO_DESC,
            ORDER_VIDEO_ASC, ORDER_VIDEO_DESC,
            ORDER_LINK_CREATION_ASC, ORDER_LINK_CREATION_DESC,
            ORDER_LABEL_ASC, ORDER_LABEL_DESC, ORDER_FAV_ASC, ORDER_FAV_DESC,};


        enum { FILE_TYPE_DEFAULT = 0, // FILE_TYPE_UNKNOWN already exists at WinBase.h
               FILE_TYPE_PHOTO,
               FILE_TYPE_AUDIO,
               FILE_TYPE_VIDEO,
               FILE_TYPE_DOCUMENT,
             };

        enum { SEARCH_TARGET_INSHARE = 0,
               SEARCH_TARGET_OUTSHARE,
               SEARCH_TARGET_PUBLICLINK,
               SEARCH_TARGET_ROOTNODE,      // search in Cloud and Vault rootnodes
               SEARCH_TARGET_ALL, };

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
         * returns an empty list
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * @return List with all child MegaNode objects
         */
        MegaNodeList* getChildren(MegaNode *parent, int order = 1);

        /**
         * @brief Get all children of a list of MegaNodes
         *
         * If any parent node doesn't exist or it isn't a folder, that parent
         * will be skipped.
         *
         * You take the ownership of the returned value
         *
         * @param parentNodes List of parent nodes
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
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List with all child MegaNode objects
         */
        MegaNodeList* getChildren(MegaNodeList *parentNodes, int order = 1);

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
         * @brief Get information about the contents of a folder
         *
         * The associated request type with this request is MegaRequest::TYPE_FOLDER_INFO
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaFolderInfo - MegaFolderInfo object with the information related to the folder
         *
         * @param node Folder node to inspect
         * @param listener MegaRequestListener to track this request
         */
        void getFolderInfo(MegaNode *node, MegaRequestListener *listener = NULL);

        /**
         * @brief Get file and folder children of a MegaNode separatedly
         *
         * If the parent node doesn't exist or it isn't a folder, this function
         * returns NULL
         *
         * You take the ownership of the returned value
         *
         * @param p Parent node
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return Lists with files and folders child MegaNode objects
         */
        MegaChildrenLists* getFileFolderChildren(MegaNode *p, int order = 1);

        /**
         * @brief Returns true if the node has children
         * @return true if the node has children
         */
        bool hasChildren(MegaNode *parent);

        /**
         * @brief Get the first child node with the provided name
         *
         * If the node doesn't exist, this function returns NULL
         * It's possible to have multiple nodes with the same name.
         * This function will return one of them.
         *
         * You take the ownership of the returned value
         *
         * @param parent Parent node
         * @param name Name of the node
         * @return The MegaNode that has the selected parent and name
         */
        MegaNode *getChildNode(MegaNode *parent, const char* name);

        /**
         * @brief Get the first child node with the name and type provided
         *
         * Allowed types for type parameter: MegaNode::TYPE_FILE, MegaNode::TYPE_FOLDER
         *
         * If the node doesn't exist, this function returns nullptr
         * It's possible to have multiple nodes with the same name.
         * This function will return one of them.
         *
         * You take the ownership of the returned value
         *
         * @param parent Parent node
         * @param name Name of the node
         * @param type Type of the node.
         * @return The MegaNode that has the selected parent, name and type
         */
        MegaNode* getChildNodeOfType(MegaNode *parent, const char *name, int type);

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
         * except if the path contains names with '/', '\' or ':' characters.
         *
         * You take the ownership of the returned value
         *
         * @param node MegaNode for which the path will be returned
         * @return The path of the node
         */
        char* getNodePath(MegaNode *node);

        /**
         * @brief Get the path of a Node given its MegaHandle
         *
         * If the node doesn't exist, this function returns NULL.
         * You can recover the node later using MegaApi::getNodeByPath
         * except if the path contains names with '/', '\' or ':' characters.
         *
         * You take the ownership of the returned value
         *
         * @param handle MegaNode handle for which the path will be returned
         * @return The path of the node
         */
        char* getNodePathByNodeHandle(MegaHandle handle);

        /**
         * @brief Get the MegaNode in a specific path in the MEGA account
         *
         * The path separator character is '/'
         * The Root node is /
         * The Vault root node is //in/
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
        * @brief Get all MegaUserAlerts for the logged in user
        *
        * You take the ownership of the returned value
        *
        * @return List of MegaUserAlert objects
        */
        MegaUserAlertList* getUserAlerts();

        /**
         * @brief Get the number of unread user alerts for the logged in user
         *
         * @return Number of unread user alerts
         */
        int getNumUnreadUserAlerts();

        /**
         * @brief Get a list with all inbound sharings from one MegaUser
         *
         * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
         * MegaApi::ORDER_DEFAULT_DESC
         *
         * You take the ownership of the returned value
         *
         * @param user MegaUser sharing folders with this account
         * @param order Sorting order to use
         * @return List of MegaNode objects that this user is sharing with this account
         */
        MegaNodeList *getInShares(MegaUser* user, int order = ORDER_NONE);

        /**
         * @brief Get a list with all inboud sharings
         *
         * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
         * MegaApi::ORDER_DEFAULT_DESC
         *
         * You take the ownership of the returned value
         *
         * @param order Sorting order to use
         * @return List of MegaNode objects that other users are sharing with this account
         */
        MegaNodeList *getInShares(int order = ORDER_NONE);

        /**
         * @brief Get a list with all active inboud sharings
         *
         * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
         * MegaApi::ORDER_DEFAULT_DESC
         *
         * You take the ownership of the returned value
         *
         * @param order Sorting order to use
         * @return List of MegaShare objects that other users are sharing with this account
         */
        MegaShareList *getInSharesList(int order = ORDER_NONE);

        /**
         * @brief Get the user relative to an incoming share
         *
         * This function will return NULL if the node is not found
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
        MegaUser *getUserFromInShare(MegaNode *node, bool recurse = false);

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
         * @brief Get a list with all active and pending outbound sharings
         *
         * Valid value for order are: MegaApi::ORDER_NONE, MegaApi::ORDER_DEFAULT_ASC,
         * MegaApi::ORDER_DEFAULT_DESC
         *
         * You take the ownership of the returned value
         *
         * @param order Sorting order to use
         * @return List of MegaShare objects
         */
        MegaShareList *getOutShares(int order = ORDER_NONE);

        /**
         * @brief Get a list with the active and pending outbound sharings for a MegaNode
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
         * @deprecated Use MegaNode::getOutShares instead of this function
         */
        MegaShareList *getPendingOutShares();

        /**
         * @brief Get a list with all pending outbound sharings
         *
         * You take the ownership of the returned value
         *
         * @deprecated Use MegaNode::getOutShares instead of this function
         * @return List of MegaShare objects
         */
        MegaShareList *getPendingOutShares(MegaNode *node);

        /**
         * @brief Check if a node belongs to your own cloud
         * @param handle Node to check
         * @return True if it belongs to your own cloud
         */
        bool isPrivateNode(MegaHandle handle);

        /**
         * @brief Check if a node does NOT belong to your own cloud
         *
         * In example, nodes from incoming shared folders do not belong to your cloud.
         *
         * @param handle Node to check
         * @return True if it does NOT belong to your own cloud
         */
        bool isForeignNode(MegaHandle handle);

        /**
         * @brief Get a list with all public links
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
        MegaNodeList *getPublicLinks(int order = ORDER_NONE);

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
         * @brief Returns nodes that have an originalFingerprint equal to the supplied value
         *
         * Search the node tree and return a list of nodes that have an originalFingerprint, which
         * matches the supplied originalfingerprint.
         *
         * If the parent node supplied is not NULL, it only searches nodes below that parent folder,
         * otherwise all nodes are searched. If no nodes are found with that original fingerprint,
         * this function returns an empty MegaNodeList.
         *
         * You take the ownership of the returned value.
         *
         * @param originalFingerprint Original Fingerprint to check
         * @param parent Only return nodes below this specified parent folder. Pass NULL to consider all nodes.
         * @return List of nodes with the same original fingerprint
         */
        MegaNodeList *getNodesByOriginalFingerprint(const char* originalFingerprint, MegaNode* parent);

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
         * @return Node with the selected CRC in the selected folder, or NULL
         * if it's not found.
         */
        MegaNode* getNodeByCRC(const char *crc, MegaNode* parent);

        /**
         * @brief Check if a node has an access level
         *
         * @deprecated Use checkAccessErrorExtended
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
         * @brief Check if a node has an access level
         *
         * You take the ownership of the returned value
         *
         * @param node Node to check
         * @param level Access level to check
         * Valid values for this parameter are:
         * - MegaShare::ACCESS_OWNER
         * - MegaShare::ACCESS_FULL
         * - MegaShare::ACCESS_READWRITE
         * - MegaShare::ACCESS_READ
         *
         * @return Pointer to MegaError with the result.
         * Valid values for the error code are:
         * - MegaError::API_OK - The node has the required access level
         * - MegaError::API_EACCESS - The node doesn't have the required access level
         * - MegaError::API_ENOENT - The node doesn't exist in the account
         * - MegaError::API_EARGS - Invalid parameters
         */
        MegaError* checkAccessErrorExtended(MegaNode* node, int level);

        /**
         * @brief Check if a node can be moved to a target node
         *
         * @deprecated Use checkMoveErrorExtended
         *
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
         * @brief Check if a node can be moved to a target node
         *
         * You take the ownership of the returned value
         *
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
        MegaError* checkMoveErrorExtended(MegaNode* node, MegaNode* target);

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
         * @brief Returns the Vault node of the account
         *
         * You take the ownership of the returned value
         *
         * If you haven't successfully called MegaApi::fetchNodes before,
         * this function returns NULL
         *
         * @return Vault node of the account
         */
        MegaNode *getVaultNode();

        /**
         * @deprecated Renamed to getVaultNode(). Should be replaced in external bindings before being removed here.
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
         * @brief Check if a node is in the Vault tree
         *
         * @param node Node to check
         * @return True if the node is in the Vault
         */
        bool isInVault(MegaNode *node);

        /**
         * @deprecated Renamed to isInVault(). Should be replaced in external bindings before being removed here.
         */
        bool isInInbox(MegaNode* node) { return isInVault(node); }

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
         * @param recursive True if you want to search recursively in the node tree.
         * False if you want to search in the children of the node only
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* search(MegaNode* node, const char* searchString, bool recursive = 1, int order = ORDER_NONE);

        /**
         * @brief Search nodes containing a search string in their name
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
         * @param recursive True if you want to search recursively in the node tree.
         * False if you want to search in the children of the node only
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* search(MegaNode* node, const char* searchString, MegaCancelToken *cancelToken, bool recursive = 1, int order = ORDER_NONE);

        /**
         * @brief Search nodes containing a search string in their name
         *
         * The search is case-insensitive.
         *
         * The search will consider every accessible node for the account:
         *  - Cloud drive
         *  - Vault
         *  - Rubbish bin
         *  - Incoming shares from other users
         *
         * You take the ownership of the returned value.
         *
         * @param searchString Search string. The search is case-insensitive
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* search(const char* searchString, int order = ORDER_NONE);

        /**
         * @brief Search nodes containing a search string in their name
         *
         * The search is case-insensitive.
         *
         * The search will consider every accessible node for the account:
         *  - Cloud drive
         *  - Vault
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* search(const char* searchString, MegaCancelToken *cancelToken, int order = ORDER_NONE);

        /**
         * @brief Search nodes on incoming shares containing a search string in their name
         *
         * The search is case-insensitive.
         *
         * The method will search exclusively on incoming shares
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* searchOnInShares(const char *searchString, MegaCancelToken *cancelToken, int order = ORDER_NONE);

        /**
         * @brief Search nodes on outbound shares containing a search string in their name
         *
         * The search is case-insensitive.
         *
         * The method will search exclusively on outbound shares
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* searchOnOutShares(const char *searchString, MegaCancelToken *cancelToken, int order = ORDER_NONE);

        /**
         * @brief Search nodes on public links containing a search string in their name
         *
         * The search is case-insensitive.
         *
         * The method will search exclusively on public links
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
         * Same behavior than MegaApi::ORDER_DEFAULT_ASC
         *
         * - MegaApi::ORDER_ALPHABETICAL_DESC = 10
         * Same behavior than MegaApi::ORDER_DEFAULT_DESC
         *
         * Deprecated: MegaApi::ORDER_ALPHABETICAL_ASC and MegaApi::ORDER_ALPHABETICAL_DESC
         * are equivalent to MegaApi::ORDER_DEFAULT_ASC and MegaApi::ORDER_DEFAULT_DESC.
         * They will be eventually removed.
         *
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @return List of nodes that contain the desired string in their name
         */
        MegaNodeList* searchOnPublicLinks(const char *searchString, MegaCancelToken *cancelToken, int order = ORDER_NONE);

        /**
         * @brief Allow to search nodes with the following options:
         * - Search given a parent node of the tree to explore, or on the contrary search in a
         *   specific target (root nodes, inshares, outshares, public links)
         * - Search recursively
         * - Containing a search string in their name
         * - Filter by the type of the node
         * - Order the returned list
         *
         * If node is provided, it will be the parent node of the tree to explore,
         * search string and/or nodeType can be added to search parameters
         *
         * If node and searchString are not provided, and node type is not valid, this method will
         * return an empty list.
         *
         * If parameter type is different of MegaApi::FILE_TYPE_DEFAULT, the following values for parameter
         * order are invalid: MegaApi::ORDER_PHOTO_ASC, MegaApi::ORDER_PHOTO_DESC,
         * MegaApi::ORDER_VIDEO_ASC, MegaApi::ORDER_VIDEO_DESC
         *
         * The search is case-insensitive. If the search string is not provided but type has any value
         * defined at nodefiletype_t (except FILE_TYPE_DEFAULT),
         * this method will return a list that contains nodes of the same type as provided.
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
         * @param recursive True if you want to search recursively in the node tree.
         * False if you want to search in the children of the node only
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
         * - MegaApi::ORDER_PHOTO_ASC = 11
         * Sort with photos first, then by date ascending
         *
         * - MegaApi::ORDER_PHOTO_DESC = 12
         * Sort with photos first, then by date descending
         *
         * - MegaApi::ORDER_VIDEO_ASC = 13
         * Sort with videos first, then by date ascending
         *
         * - MegaApi::ORDER_VIDEO_DESC = 14
         * Sort with videos first, then by date descending
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
         * @param type Type of nodes requested in the search
         * Valid values for this parameter are:
         * - MegaApi::FILE_TYPE_DEFAULT = 0  --> all types
         * - MegaApi::FILE_TYPE_PHOTO = 1
         * - MegaApi::FILE_TYPE_AUDIO = 2
         * - MegaApi::FILE_TYPE_VIDEO = 3
         * - MegaApi::FILE_TYPE_DOCUMENT = 4
         *
         * @param target Target type where this method will search
         * Valid values for this parameter are
         * - SEARCH_TARGET_INSHARE = 0
         * - SEARCH_TARGET_OUTSHARE = 1
         * - SEARCH_TARGET_PUBLICLINK = 2
         * - SEARCH_TARGET_ROOTNODE = 3 --> search in Cloud and Vault rootnodes
         * - SEARCH_TARGET_ALL = 4
         *
         * @return List of nodes that match with the search parameters
         */
        MegaNodeList* searchByType(MegaNode *node, const char *searchString, MegaCancelToken *cancelToken, bool recursive = true, int order = ORDER_NONE, int type = FILE_TYPE_DEFAULT, int target = SEARCH_TARGET_ALL);

        /**
         * @brief Return a list of buckets, each bucket containing a list of recently added/modified nodes
         *
         * Each bucket contains files that were added/modified in a set, by a single user.
         *
         * @deprecated use getRecentActionsAsync
         *
         * @param days Age of actions since added/modified nodes will be considered (in days)
         * @param maxnodes Maximum amount of nodes to be considered
         *
         * @return List of buckets containing nodes that were added/modifed as a set
         */
        MegaRecentActionBucketList* getRecentActions(unsigned days, unsigned maxnodes);

        /**
         * @brief Return a list of buckets, each bucket containing a list of recently added/modified nodes
         *
         * Each bucket contains files that were added/modified in a set, by a single user.
         *
         * This function uses the default parameters for the MEGA apps, which consider (currently)
         * interactions during the last 30 days and max 10.000 nodes.
         *
         * You take the ownership of the returned value.
         *
         * @deprecated use getRecentActionsAsync
         *
         * @return List of buckets containing nodes that were added/modifed as a set
         */
        MegaRecentActionBucketList* getRecentActions();


        /**
         * @brief Get a list of buckets, each bucket containing a list of recently added/modified nodes
         *
         * Each bucket contains files that were added/modified in a set, by a single user.
         *
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNumber - Returns the number of days since nodes will be considerated
         * - MegaRequest::getParamType - Returns the maximun number of nodes
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_RECENT_ACTIONS
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getRecentsBucket - Returns buckets with a list of recently added/modified nodes
         *
         * The recommended values for the following parameters are to consider
         * interactions during the last 30 days and maximum 500 nodes.
         *
         * @param days Age of actions since added/modified nodes will be considered (in days)
         * @param maxnodes Maximum amount of nodes to be considered

         * @param listener MegaRequestListener to track this request
         */
        void getRecentActionsAsync(unsigned days, unsigned maxnodes, MegaRequestListener *listener = NULL);

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
         * @param chatAuth Chat authentication token to access the node
         * @return MegaNode object
         */
        MegaNode *createForeignFileNode(MegaHandle handle, const char *key, const char *name,
                                       int64_t size, int64_t mtime, MegaHandle parentHandle, const char *privateAuth, const char *publicAuth, const char *chatAuth);

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

#ifdef ENABLE_CHAT
        /**
         * @brief Returns a MegaNode that can be downloaded/copied with a chat-authorization
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
        MegaNode *authorizeChatNode(MegaNode *node, const char *cauth);
#endif

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
         * @brief Disable special features related to images and videos
         *
         * Disabling these features will avoid the upload of previews and thumbnails
         * for images and videos.
         *
         * It's only recommended to disable these features before uploading files
         * with image or video extensions that are not really images or videos,
         * or that are encrypted in the local drive so they can't be analyzed anyway.
         *
         * By default, graphic features are enabled if the SDK was built with a valid
         * graphic processor or a valid graphic processor was provided in the constructor
         * of MegaApi.
         *
         * @param disable True to disable special features related to images and videos
         */
        void disableGfxFeatures(bool disable);

        /**
         * @brief Check if special graphic features are disabled
         *
         * By default, graphic features are enabled so this function will return false.
         * If graphic features were previously disabled, or the SDK wasn't built with
         * a valid graphic processor and it wasn't provided in the constructor on MegaApi,
         * this function will return true.
         *
         * @return True if special features related to images and videos are disabled
         */
        bool areGfxFeaturesDisabled();

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
         * @param languageCode Language code used by the app
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
         * @param languageCode Language code to be set
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
         * @brief Enable or disable the automatic approval of incoming contact requests using a contact link
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
        void setContactLinksOption(bool disable, MegaRequestListener *listener = NULL);

        /**
         * @brief Check if file versioning is enabled or disabled
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
         * If the option has never been set, the error code will be MegaError::API_ENOENT.
         * In that case, file versioning is enabled by default and MegaRequest::getFlag returns false.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getFileVersionsOption(MegaRequestListener *listener = NULL);

        /**
         * @brief Check if the automatic approval of incoming contact requests using contact links is enabled or disabled
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
        void getContactLinksOption(MegaRequestListener *listener = NULL);

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
         * This function escapes (%xx) the characters contained in the following list: \/:?\"<>|*
         * You can revert this operation using MegaApi::unescapeFsIncompatible
         *
         * The input string must be UTF8 encoded. The returned value will be UTF8 too.
         *
         * You take the ownership of the returned value
         *
         * @param filename Name to convert (UTF8)
         * @return Converted name (UTF8)
         * @deprecated There is a new prototype that includes path for filesystem detection
         */
        char* escapeFsIncompatible(const char *filename);

        /**
         * @brief Make a name suitable for a file name in the local filesystem
         *
         * This function escapes (%xx) forbidden characters in the local filesystem if needed.
         * You can revert this operation using MegaApi::unescapeFsIncompatible
         *
         * If no dstPath is provided or filesystem type it's not supported this method will
         * escape characters contained in the following list: \/:?\"<>|*
         * Otherwise it will check forbidden characters for local filesystem type
         *
         * The input string must be UTF8 encoded. The returned value will be UTF8 too.
         *
         * You take the ownership of the returned value
         *
         * @param filename Name to convert (UTF8)
         * @param dstPath Destination path
         * @return Converted name (UTF8)
         */
        char* escapeFsIncompatible(const char *filename, const char *dstPath);

        /**
         * @brief Unescape a file name escaped with MegaApi::escapeFsIncompatible
         *
         * This method will unescape those sequences that once has been unescaped results
         * in any character of the following list: \/:?\"<>|*
         *
         * The input string must be UTF8 encoded. The returned value will be UTF8 too.
         * You take the ownership of the returned value
         *
         * @param name Escaped name to convert (UTF8)
         * @return Converted name (UTF8)
         * @deprecated There is a new prototype that includes path for filesystem detection
         */
        char* unescapeFsIncompatible(const char* name);

        /**
         * @brief Unescape a file name escaped with MegaApi::escapeFsIncompatible
         *
         * If no localPath is provided or filesystem type it's not supported, this method will
         * unescape those sequences that once has been unescaped results in any character
         * of the following list: \/:?\"<>|*
         * Otherwise it will unescape those characters forbidden in local filesystem type
         *
         * The input string must be UTF8 encoded. The returned value will be UTF8 too.
         * You take the ownership of the returned value
         *
         * @param name Escaped name to convert (UTF8)
         * @param localPath Local path
         * @return Converted name (UTF8)
         */
        char* unescapeFsIncompatible(const char *name, const char *localPath);


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
         * @brief Request the URL suitable for uploading a media file.
         *
         * This function requests the URL needed for uploading the file. The URL will need the urlSuffix
         * from the MegaBackgroundMediaUpload::encryptFile to be appended before actually sending.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_BACKGROUND_UPLOAD_URL
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaBackgroundMediaUpload - The updated state of the upload with the URL in the MegaBackgroundMediaUpload::getUploadUrl
         *
         * Call this function just once (per file) to find out the URL to upload to, and upload all the pieces to the same
         * URL. If errors are encountered and the operation must be restarted from scratch, then a new URL should be requested.
         * A new URL could specify a different upload server for example.
         *
         * @param fullFileSize The size of the file
         * @param state A pointer to the MegaBackgroundMediaUpload object tracking this upload
         * @param listener MegaRequestListener to track this request
         */
        void backgroundMediaUploadRequestUploadURL(int64_t fullFileSize, MegaBackgroundMediaUpload* state, MegaRequestListener *listener);

        /**
         * @brief Create the node after completing the upload of the file by the app.
         *
         * Note: added for the use of MEGAproxy and not otherwise supported
		 *
         * Call this function after completing the upload of all the file data
         * The node representing the file will be created in the cloud, with all the suitable
         * attributes and file attributes attached.
         *
         * The associated request type with this request is MegaRequest::TYPE_COMPLETE_BACKGROUND_UPLOAD
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getPassword - Returns the original fingerprint
         * - MegaRequest::getNewPassword - Returns the fingerprint
         * - MegaRequest::getName - Returns the name
         * - MegaRequest::getParentHandle - Returns the parent nodehandle
         * - MegaRequest::getSessionKey - Returns the upload token converted to B64url encoding
         * - MegaRequest::getPrivateKey - Returns the file key provided in B64url encoding
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNodeHandle - Returns the handle of the uploaded node
         * - MegaRequest::getFlag - True if target folder (\c parent) was overriden
         *
         * @param utf8Name The leaf name of the file, utf-8 encoded
         * @param parent The folder node under which this new file should appear
         * @param fingerprint The fingerprint for the uploaded file (use MegaApi::getFingerprint to generate this)
         * @param fingerprintoriginal If the file uploaded is modified from the original,
         *        pass the fingerprint of the original file here, otherwise NULL.
         * @param string64UploadToken The token returned from the upload of the last portion of the file,
         *        which is exactly 36 binary bytes, converted to a base 64 string with MegaApi::binaryToString64.
         * @param string64FileKey file encryption key converted to a base 64 string with MegaApi::binaryToString64.
         * @param listener MegaRequestListener to track this request
         */
        void completeUpload(const char* utf8Name, MegaNode *parent, const char* fingerprint, const char* fingerprintoriginal,
                                          const char *string64UploadToken, const char *string64FileKey,  MegaRequestListener *listener);

        /**
         * @brief Request the URL suitable for uploading a file.
         *
         * Note: added for the use of MEGAproxy and not otherwise supported
		 *
         * This function requests the base URL needed for uploading the file.
         * The URL will need the urlSuffix resulting from encryption.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_BACKGROUND_UPLOAD_URL
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getName - The URL to use
         * - MegaRequest::getLink - The IPv4 of the upload server
         * - MegaRequest::getText - The IPv6 of the upload server
         *
         * Call this function just once (per file) to find out the URL to upload to, and upload all the pieces to the same
         * URL. If errors are encountered and the operation must be restarted from scratch, then a new URL should be requested.
         * A new URL could specify a different upload server for example.
         *
         * @param fullFileSize The size of the file
         * @param forceSSL Enforce getting a https URL
         * @param listener MegaRequestListener to track this request
         */
         void getUploadURL(int64_t fullFileSize, bool forceSSL, MegaRequestListener *listener);

         /**
          * @brief Request the URL suitable for uploading a thubmnail for a node.
          *
          * Note: added for the use of MEGAproxy
          *
          * This function requests the base URL needed for uploading the thumbnail.
          *
          * The associated request type with this request is MegaRequest::TYPE_GET_FA_UPLOAD_URL
          * Valid data in the MegaRequest object received in onRequestFinish when the error code
          * is MegaError::API_OK:
          * - MegaRequest::getName - The URL to use
          * - MegaRequest::getLink - The IPv4 of the upload server
          * - MegaRequest::getText - The IPv6 of the upload server
          *
          * Call this function just once (per file) to find out the URL to upload to, and upload all the pieces to the same
          * URL. If errors are encountered and the operation must be restarted from scratch, then a new URL should be requested.
          * A new URL could specify a different upload server for example.
          *
          * @param nodehandle handle of the node
          * @param fullFileSize The size of the thumbnail
          * @param forceSSL Enforce getting a https URL
          * @param listener MegaRequestListener to track this request
          */
         void getThumbnailUploadURL(MegaHandle nodehandle, int64_t fullFileSize, bool forceSSL, MegaRequestListener *listener);

         /**
          * @brief Request the URL suitable for uploading a preview for a node.
          *
          * Note: added for the use of MEGAproxy
          *
          * This function requests the base URL needed for uploading the preview.
          *
          * The associated request type with this request is MegaRequest::TYPE_GET_FA_UPLOAD_URL
          * Valid data in the MegaRequest object received in onRequestFinish when the error code
          * is MegaError::API_OK:
          * - MegaRequest::getName - The URL to use
          * - MegaRequest::getLink - The IPv4 of the upload server
          * - MegaRequest::getText - The IPv6 of the upload server
          *
          * Call this function just once (per file) to find out the URL to upload to, and upload all the pieces to the same
          * URL. If errors are encountered and the operation must be restarted from scratch, then a new URL should be requested.
          * A new URL could specify a different upload server for example.
          *
          * @param nodehandle handle of the node
          * @param fullFileSize The size of the preview
          * @param forceSSL Enforce getting a https URL
          * @param listener MegaRequestListener to track this request
          */
         void getPreviewUploadURL(MegaHandle nodehandle, int64_t fullFileSize, bool forceSSL, MegaRequestListener *listener);

        /**
         * @brief Create the node after completing the background upload of the file.
         *
         * Call this function after completing the background upload of all the file data
         * The node representing the file will be created in the cloud, with all the suitable
         * attributes and file attributes attached.
         *
         * The associated request type with this request is MegaRequest::TYPE_COMPLETE_BACKGROUND_UPLOAD
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getMegaBackgroundMediaUploadPtr() - Returns the provided state
         * - MegaRequest::getPassword - Returns the original fingerprint
         * - MegaRequest::getNewPassword - Returns the fingerprint
         * - MegaRequest::getName - Returns the name
         * - MegaRequest::getParentHandle - Returns the parent nodehandle
         * - MegaRequest::getSessionKey - Returns the upload token converted to B64url encoding
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNodeHandle - Returns the handle of the uploaded node
         * - MegaRequest::getFlag - True if target folder (\c parent) was overriden
         *
         * @param state The MegaBackgroundMediaUpload object tracking this upload
         * @param utf8Name The leaf name of the file, utf-8 encoded
         * @param parent The folder node under which this new file should appear
         * @param fingerprint The fingerprint for the uploaded file (use MegaApi::getFingerprint to generate this)
         * @param fingerprintoriginal If the file uploaded is modified from the original,
         *        pass the fingerprint of the original file here, otherwise NULL.
         * @param string64UploadToken The token returned from the upload of the last portion of the file,
         *        which is exactly 36 binary bytes, converted to a base 64 string with MegaApi::binaryToString64.
         * @param listener MegaRequestListener to track this request
         */
        void backgroundMediaUploadComplete(MegaBackgroundMediaUpload* state, const char *utf8Name, MegaNode *parent,
            const char *fingerprint, const char *fingerprintoriginal, const char *string64UploadToken, MegaRequestListener *listener);

        /**
         * @brief Call this to enable the library to attach media info attributes
         *
         * Those attributes allows to know if a file is a video, and play it with the correct codec.
         *
         * If media info is not ready, this function returns false and automatically retrieves the mappings for type names
         * and MEGA encodings, required to analyse media files. When media info is received, the callbacks
         * MegaListener::onEvent and MegaGlobalListener::onEvent are called with the MegaEvent::EVENT_MEDIA_INFO_READY.
         *
         * @return True if the library is ready, otherwise false (the request for media translation data is sent to MEGA)
         */
        bool ensureMediaInfo();

        /**
         * @brief Set the OriginalFingerprint of a node.
         *
         * Use this call to attach an originalFingerprint to a node. The fingerprint must
         * be generated from the file prior to modification, where this node is the modified file.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_NODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the handle of the node
         * - MegaRequest::getText - Returns the specified fingerprint
         * - MegaRequest::getFlag - Returns true (official attribute)
         * - MegaRequest::getParamType - Returns MegaApi::NODE_ATTR_ORIGINALFINGERPRINT
         *
         * @param node The node to attach the originalFingerprint to.
         * @param originalFingerprint The fingerprint of the file before modification
         * @param listener MegaRequestListener to track this request
         */
        void setOriginalFingerprint(MegaNode* node, const char* originalFingerprint, MegaRequestListener *listener);

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
         * If the function is passed NULL, it will return NULL.
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
            TCP_SERVER_DENY_ALL = -1,
            TCP_SERVER_ALLOW_ALL = 0,
            TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1,
            TCP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
        };

        //kept for backwards compatibility
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
         * - The restricted mode of the server is set to MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS
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
         * @param useTLS Use TLS (default false).
         * If the SDK compilation does not support TLS,
         * enabling this flag will cause the function to return false.
         * @param certificatepath path to certificate (PEM format)
         * @param keypath path to certificate key
         * @param useIPv6 true to use [::1] as host, false to use 127.0.0.1
         * @return True if the server is ready, false if the initialization failed
         */
        bool httpServerStart(bool localOnly = true, int port = 4443, bool useTLS = false, const char *certificatepath = NULL, const char * keypath = NULL, bool useIPv6 = false);

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
         * @brief Stablish FILE_ATTRIBUTE_OFFLINE attribute
         *
         * By default, it is not enabled
         *
         * This is used when serving files in WEBDAV, it will cause windows clients to not load a file
         * when it is selected. It is intended to reduce unnecessary traffic.
         *
         * @param enable true to enable the FILE_ATTRIBUTE_OFFLINE attribute, false to disable it
         */
        void httpServerEnableOfflineAttribute(bool enable);

        /**
         * @brief Check if FILE_ATTRIBUTE_OFFLINE it's enabled
         *
         * @return true if the FILE_ATTRIBUTE_OFFLINE attribute is enabled, otherwise false
         */
        bool httpServerIsOfflineAttributeEnabled();

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
         * The state of this option is preserved even if the HTTP server is restarted, but
         * the HTTP proxy server only remembers the generated links since the last call to
         * MegaApi::httpServerStart
         *
         * Even if nodes are allowed to be served by this function, restrictions related to
         * other configuration options (MegaApi::httpServerEnableFileServer,
         * MegaApi::httpServerEnableFolderServer) are still applied.
         *
         * @param mode Required state for the restricted mode of the HTTP proxy server
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
         * If a matching file is found, the name is exactly the same as the node with the specified handle
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
         * - MegaTransfer::getType - It will be MegaTransfer::TYPE_LOCAL_TCP_DOWNLOAD
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
         * @brief Returns a WEBDAV valid URL to a node in the local HTTP proxy server
         *
         * The HTTP proxy server must be running before using this function, otherwise
         * it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @param node Node to generate the local HTTP link
         * @return URL to the node in the local HTTP proxy server, otherwise NULL
         */
        char *httpServerGetLocalWebDavLink(MegaNode *node);

        /**
         * @brief Returns the list with the links of locations served via WEBDAV
         *
         * The HTTP server must be running before using this function, otherwise
         * it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @return URL to the node in the local HTTP server, otherwise NULL
         */
        MegaStringList *httpServerGetWebDavLinks();

        /**
         * @brief Returns the list of nodes served via WEBDAV
         *
         * The HTTP server must be running before using this function, otherwise
         * it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @return URL to the node in the local HTTP server, otherwise NULL
         */
        MegaNodeList *httpServerGetWebDavAllowedNodes();

        /**
         * @brief Stops serving a node via webdav.
         * The webdav link will no longer be valid.
         *
         * @param handle Handle of the node to stop serving
         */
        void httpServerRemoveWebDavAllowedNode(MegaHandle handle);

        /**
         * @brief Stops serving all nodes served via webdav.
         * The webdav links will no longer be valid.
         *
         */
        void httpServerRemoveWebDavAllowedNodes();

        /**
         * @brief Set the maximum buffer size for the internal buffer and the size of packets
         * sent to clients (MaxOutputSize)
         *
         * Current policy is to set MaxOutputSize to 10% of the param passed in this function.
         * Be aware that calling this method will overwrite any previous value of MaxOutputSize.
         * Therefore, any call to httpServerSetMaxOutputSize should be performed after a call to
         * this method.
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
         * The new value will be taken into account since the next request received by
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
         * @brief Start an FTP server in specified port
         *
         * If this function returns true, that means that the server is
         * ready to accept connections. The initialization is synchronous.
         *
         * The server will serve files using this URL format:
         * ftp://127.0.0.1:PORT/<NodeHandle>/<NodeName>
         *
         * The node name must be URL encoded and must match with the node handle.
         * You can generate a correct link for a MegaNode using MegaApi::ftpServerGetLocalLink
         *
         * It's important to know that the FTP server has several configuration options
         * that can restrict the nodes that will be served and the connections that will be accepted.
         *
         * These are the default options:
         * - The restricted mode of the server is set to MegaApi::FTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
         * (see MegaApi::ftpServerSetRestrictedMode)
         *
         * The FTP server will only stream a node if it's allowed by all configuration options.
         *
         * @param localOnly true to listen on 127.0.0.1 only, false to listen on all network interfaces
         * @param port Port in which the server must accept connections
         * @param dataportBegin Initial port for FTP data channel
         * @param dataPortEnd Final port for FTP data channel (included)
         * @param useTLS Use TLS (default false)
         * @param certificatepath path to certificate (PEM format)
         * @param keypath path to certificate key
         * @return True if the server is ready, false if the initialization failed
         */
        bool ftpServerStart(bool localOnly = true, int port = 22, int dataportBegin = 1500, int dataPortEnd = 1600, bool useTLS = false, const char *certificatepath = NULL, const char * keypath = NULL);

        /**
         * @brief Stop the FTP server
         *
         * When this function returns, the server is already shutdown.
         * If the FTP server isn't running, this functions does nothing
         */
        void ftpServerStop();

        /**
         * @brief Check if the FTP server is running
         * @return 0 if the server is not running. Otherwise the port in which it's listening to
         */
        int ftpServerIsRunning();

         /**
         * @brief Check if the FTP server is listening on all network interfaces
         * @return true if the FTP server is listening on 127.0.0.1 only, or it's not started.
         * If it's started and listening on all network interfaces, this function returns false
         */
        bool ftpServerIsLocalOnly();

        /**
         * @brief Enable/disable the restricted mode of the FTP server
         *
         * This function allows to restrict the nodes that are allowed to be served.
         * For not allowed links, the server will return a corresponding "550" error.
         *
         * Possible values are:
         * - TCP_SERVER_DENY_ALL = -1
         * All nodes are forbidden
         *
         * - TCP_SERVER_ALLOW_ALL = 0
         * All nodes are allowed to be served
         *
         * - TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1 (default)
         * Only links created with MegaApi::ftpServerGetLocalLink are allowed to be served
         *
         * - TCP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
         * Only the last link created with MegaApi::ftpServerGetLocalLink is allowed to be served
         *
         * If a different value from the list above is passed to this function, it won't have any effect and the previous
         * state of this option will be preserved.
         *
         * The default value of this property is MegaApi::FTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
         *
         * The state of this option is preserved even if the FTP server is restarted, but the
         * the FTP server only remembers the generated links since the last call to
         * MegaApi::ftpServerStart
         *
         * @param mode State for the restricted mode of the FTP server
         */
        void ftpServerSetRestrictedMode(int mode);

        /**
         * @brief Check if the FTP server is working in restricted mode
         *
         * Possible return values are:
         * - TCP_SERVER_DENY_ALL = -1
         * All nodes are forbidden
         *
         * - TCP_SERVER_ALLOW_ALL = 0
         * All nodes are allowed to be served
         *
         * - TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS = 1
         * Only links created with MegaApi::ftpServerGetLocalLink are allowed to be served
         *
         * - TCP_SERVER_ALLOW_LAST_LOCAL_LINK = 2
         * Only the last link created with MegaApi::ftpServerGetLocalLink is allowed to be served
         *
         * The default value of this property is MegaApi::FTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
         *
         * See MegaApi::ftpServerEnableRestrictedMode and MegaApi::ftpServerStart
         *
         * @return State of the restricted mode of the FTP server
         */
        int ftpServerGetRestrictedMode();

        /**
         * @brief Add a listener to receive information about the FTP server
         *
         * This is the valid data that will be provided on callbacks:
         * - MegaTransfer::getType - It will be MegaTransfer::TYPE_LOCAL_TCP_DOWNLOAD
         * - MegaTransfer::getPath - URL requested to the FTP server
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
         * - A number > 0 means an FTP error code returned to the client
         *
         * @param listener Listener to receive information about the FTP server
         */
        void ftpServerAddListener(MegaTransferListener *listener);

        /**
         * @brief Stop the reception of callbacks related to the FTP server on this listener
         * @param listener Listener that won't continue receiving information
         */
        void ftpServerRemoveListener(MegaTransferListener *listener);

        /**
         * @brief Returns a URL to a node in the local FTP server
         *
         * The FTP server must be running before using this function, otherwise
         * it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @param node Node to generate the local FTP link
         * @return URL to the node in the local FTP server, otherwise NULL
         */
        char *ftpServerGetLocalLink(MegaNode *node);

        /**
         * @brief Returns the list with the links of locations served via FTP
         *
         * The FTP server must be running before using this function, otherwise
         * it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @return URL to the node in the local FTP server, otherwise NULL
         */
        MegaStringList *ftpServerGetLinks();

        /**
         * @brief Returns the list of nodes served via FTP
         *
         * The FTP server must be running before using this function, otherwise
         * it will return NULL.
         *
         * You take the ownership of the returned value
         *
         * @return URL to the node in the local FTP server, otherwise NULL
         */
        MegaNodeList *ftpServerGetAllowedNodes();

        /**
         * @brief Stops serving a node via ftp.
         * The ftp link will no longer be valid.
         *
         * @param handle Handle of the node to stop serving
         */
        void ftpServerRemoveAllowedNode(MegaHandle handle);

        /**
         * @brief Stops serving all nodes served via ftp.
         * The ftp links will no longer be valid.
         *
         */
        void ftpServerRemoveAllowedNodes();

        /**
         * @brief Set the maximum buffer size for the internal buffer
         *
         * The FTP server has an internal buffer to store the data received from MEGA
         * while it's being sent to clients. When the buffer is full, the connection with
         * the MEGA storage server is closed, when the buffer has few data, the connection
         * with the MEGA storage server is started again.
         *
         * Even with very fast connections, due to the possible latency starting new connections,
         * if this buffer is small the streaming can have problems due to the overhead caused by
         * the excessive number of RETR/REST requests.
         *
         * It's recommended to set this buffer at least to 1MB
         *
         * For connections that request less data than the buffer size, the FTP server
         * will only allocate the required memory to complete the request to minimize the
         * memory usage.
         *
         * The new value will be taken into account since the next request received by
         * the FTP server, not for ongoing requests. It's possible and effective
         * to call this function even before the server has been started, and the value
         * will be still active even if the server is stopped and started again.
         *
         * @param bufferSize Maximum buffer size (in bytes) or a number <= 0 to use the
         * internal default value
         */
        void ftpServerSetMaxBufferSize(int bufferSize);

        /**
         * @brief Get the maximum size of the internal buffer size
         *
         * See MegaApi::ftpServerSetMaxBufferSize
         *
         * @return Maximum size of the internal buffer size (in bytes)
         */
        int ftpServerGetMaxBufferSize();

        /**
         * @brief Set the maximum size of packets sent to clients
         *
         * For each connection, the FTP server only sends one write to the underlying
         * socket at once. This parameter allows to set the size of that write.
         *
         * A small value could cause a lot of writes and would lower the performance.
         *
         * A big value could send too much data to the output buffer of the socket. That could
         * keep the internal buffer full of data that hasn't been sent to the client yet,
         * preventing the retrieval of additional data from the MEGA storage server. In that
         * circumstances, the client could read a lot of data at once and the FTP server
         * could not have enough time to get more data fast enough.
         *
         * It's recommended to set this value to at least 8192 and no more than the 25% of
         * the maximum buffer size (MegaApi::ftpServerSetMaxBufferSize).
         *
         * The new value will be taken into account since the next request received by
         * the FTP server, not for ongoing requests. It's possible and effective
         * to call this function even before the server has been started, and the value
         * will be still active even if the server is stopped and started again.
         *
         * @param outputSize Maximun size of data packets sent to clients (in bytes) or
         * a number <= 0 to use the internal default value
         */
        void ftpServerSetMaxOutputSize(int outputSize);

        /**
         * @brief Get the maximum size of the packets sent to clients
         *
         * See MegaApi::ftpServerSetMaxOutputSize
         *
         * @return Maximum size of the packets sent to clients (in bytes)
         */
        int ftpServerGetMaxOutputSize();

#endif

        /**
         * @brief Get the MIME type associated with the extension
         *
         * You take the ownership of the returned value
         *
         * @param extension File extension (with or without a leading dot)
         * @return MIME type associated with the extension
         */
        static char *getMimeType(const char* extension);

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
         * - MegaRequest::getAccess - Returns zero (private mode)
         * - MegaRequest::getMegaTextChatPeerList - List of participants and their privilege level
         * - MegaRequest::getText - Returns the title of the chat.
         * - MegaRequest::getParamType - Returns a Bitmask with the chat options that will be enabled in creation
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaTextChatList - Returns the new chat's information
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EACCESS - If more than 1 peer is provided for a 1on1 chatroom.
         * - MegaError::API_EARGS   - If chatOptions param is provided for a 1on1 chat
         *
         * @note If peers list contains only one person, group chat is not set and a permament chat already
         * exists with that person, then this call will return the information for the existing chat, rather
         * than a new chat.
         *
         * @param group Flag to indicate if the chat is a group chat or not
         * @param peers MegaTextChatPeerList including other users and their privilege level
         * @param title Byte array that contains the chat topic if exists. NULL if no custom title is required.
         * @param chatOptions Bitmask that contains the chat options to create the chat
         * @param listener MegaRequestListener to track this request
         */
        void createChat(bool group, MegaTextChatPeerList* peers, const char* title = NULL, int chatOptions = CHAT_OPTIONS_EMPTY, MegaRequestListener* listener = NULL);

        /**
         * @brief Creates a public chatroom for multiple participants (groupchat)
         *
         * This function allows to create public chats, where the moderator can create chat links to share
         * the access to the chatroom via a URL (chat-link). In order to create a public chat-link, the
         * moderator needs to create / get a public handle for the chatroom by using \c MegaApi::chatLinkCreate.
         *
         * The resulting chat-link allows anyone (even users without an account in MEGA) to review the
         * history of the chatroom. The \c MegaApi::getChatLinkURL provides the chatd URL to connect.
         *
         * Users with an account in MEGA can freely join the room by themselves (the privilege
         * upon join will be standard / read-write) by using \c MegaApi::chatLinkJoin.
         *
         * The creator of the chat will have moderator level privilege and should not be included in the
         * list of peers.
         *
         * The associated request type with this request is MegaChatRequest::TYPE_CREATE_CHATROOM
         * Valid data in the MegaChatRequest object received on callbacks:
         * - MegaChatRequest::getFlag - Returns if the new chat is a group chat or permanent chat
         * - MegaRequest::getAccess - Returns one (public mode)
         * - MegaChatRequest::getMegaChatPeerList - List of participants and their privilege level
         * - MegaChatRequest::getMegaStringMap - MegaStringMap with handles and unified keys or each peer
         * - MegaRequest::getText - Returns the title of the chat.
         * - MegaRequest::getNumber - Returns if chat room is a meeting room
         * - MegaRequest::getParamType - Returns a Bitmask with the chat options that will be enabled in creation
         *
         * Valid data in the MegaChatRequest object received in onRequestFinish when the error code
         * is MegaError::ERROR_OK:
         * - MegaChatRequest::getChatHandle - Returns the handle of the new chatroom
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS - If the number of keys doesn't match the number of peers plus one (own user)
         *
         * @param peers MegaChatPeerList including other users and their privilege level
         * @param title Byte array that contains the chat topic if exists. NULL if no custom title is required.
         * @param userKeyMap MegaStringMap of user handles in B64 as keys, and unified keys in B64 as values. Own user included
         * @param meetingRoom Boolean indicating if room is a meeting room
         * @param chatOptions Bitmask that contains the chat options to create the chat
         * @param listener MegaChatRequestListener to track this request
         */
        void createPublicChat(MegaTextChatPeerList* peers, const MegaStringMap* userKeyMap, const char *title = NULL, bool meetingRoom = false, int chatOptions = CHAT_OPTIONS_EMPTY, MegaRequestListener* listener = NULL);

        /**
         * @brief Enable or disable a option for a chatroom
         *
         * This function allows to enable or disable one of the following chatroom options:
         * - 0x01:  SpeakRequest: during calls non-operator users must request permission to speak.
         * - 0x02:  WaitingRoom: during calls non-operator members will be placed into a waiting room, an operator level user must grant each user access to the call.
         * - 0x04:  OpenInvite: when enabled allows non-operator level users to invite others into the chat room.
         *
         * The associated request type with this request is MegaChatRequest::TYPE_SET_CHAT_OPTIONS
         * Valid data in the MegaChatRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::Access  - Returns the chat option we want to enable disable
         * - MegaRequest::getFlag - Returns true if enabled was set true, otherwise it will return false
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS  - If the chatid is invalid
         * - MegaError::API_EARGS  - If this method is called for a 1on1 chat
         * - MegaError::API_ENOENT - If the chatroom does not exists
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param option Chat option that we want to enable/disable
         * @param enabled True if we want to enable the option, otherwise false.
         * @param listener MegaChatRequestListener to track this request
         */
        void setChatOption(MegaHandle chatid, int option, bool enabled, MegaRequestListener* listener = NULL);

        /**
         * @brief Adds a user to an existing chat. To do this you must have the
         * operator privilege in the chat, and the chat must be a group chat in private mode.
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
         * - MegaRequest::getText - Returns the title of the chat
         * - MegaRequest::getFlag - Returns false (private/closed mode)
         * - MegaRequest::getSessionKey - Returns the unified key for the new peer
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EACCESS - If the logged in user doesn't have privileges to invite peers or the chatroom is in public mode.
         * - MegaError::API_EINCOMPLETE - If no valid title is provided and the chatroom has a custom title already.
         * - MegaError::API_ENOENT- If no valid chatid or user handle is provided, of if the chatroom does not exists.
         *
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
         * @brief Adds a user to an existing chat. To do this you must have the
         * operator privilege in the chat, and the chat must be a group chat in public mode.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_INVITE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the MegaHandle of the user to be invited
         * - MegaRequest::getAccess - Returns the privilege level wanted for the user
         * - MegaRequest::getFlag - Returns true (open/public mode)
         * - MegaRequest::getSessionKey - Returns the unified key for the new peer
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EACCESS - If the logged in user doesn't have privileges to invite peers or the chatroom is in private mode.
         * - MegaError::API_EARGS - If there's a title and it's not Base64url encoded.
         * - MegaError::API_ENOENT- If no valid chatid or user handle is provided, of if the chatroom does not exists.
         * - MegaError::API_EINCOMPLETE - If no unified key is provided.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param uh MegaHandle that identifies the user
         * @param privilege Privilege level for the new peers. Valid values are:
         * - MegaTextChatPeerList::PRIV_UNKNOWN = -2
         * - MegaTextChatPeerList::PRIV_RM = -1
         * - MegaTextChatPeerList::PRIV_RO = 0
         * - MegaTextChatPeerList::PRIV_STANDARD = 2
         * - MegaTextChatPeerList::PRIV_MODERATOR = 3
         * @param unifiedKey Byte array that contains the unified key, already encrypted and
         * converted to Base64url encoding.
         * @param listener MegaRequestListener to track this request
         */
        void inviteToPublicChat(MegaHandle chatid, MegaHandle uh, int privilege, const char *unifiedKey = NULL, MegaRequestListener *listener = NULL);

        /**
         * @brief Remove yourself or another user from a chat. To remove a user other than
         * yourself you need to have the operator privilege. Only a group chat may be left.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_REMOVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the MegaHandle of the user to be removed
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT- If no valid chatid is provided or the chatroom does not exists.
         * - MegaError::API_EACCESS - If the chatroom is 1on1 or the caller is not operator or is not a
         * chat member, or the target is not a chat member.
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
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT- If the chatroom, the node or the target user don't exist.
         * - MegaError::API_EACCESS- If the target user is the same as caller, or if the target
         * user is anonymous but the chatroom is in private mode, or if caller is not an operator
         * or the target user is not a chat member.
         *
         * If the MEGA account is a business account and it's status is expired, onRequestFinish will
         * be called with the error code MegaError::API_EBUSINESSPASTDUE.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param n MegaNode that wants to be shared
         * @param uh MegaHandle that identifies the user
         * @param listener MegaRequestListener to track this request
         */
        void grantAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh, MegaRequestListener *listener = NULL);

        /**
         * @brief Removes access to a node from a user you previously granted access to.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_REMOVE_ACCESS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the node handle
         * - MegaRequest::getParentHandle - Returns the chat identifier
         * - MegaRequest::getEmail - Returns the MegaHandle of the user in Base64 enconding
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT- If the chatroom, the node or the target user don't exist.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param n MegaNode whose access wants to be revokesd
         * @param uh MegaHandle that identifies the user
         * @param listener MegaRequestListener to track this request
         */
        void removeAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh, MegaRequestListener *listener = NULL);

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
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT- If the chatroom doesn't exist or if the user specified is not a participant.
         * - MegaError::API_EACCESS- If caller is not operator or the chatroom is 1on1.
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
         * On the onRequestFinish error, the error code associated to the MegaError can be:
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
         *  - MegaApi::PUSH_NOTIFICATION_ANDROID_HUAWEI = 4
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
         * - MegaRequest::getNumber - Returns the connection port
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Return the HTTP status code from the stats server
         * - MegaRequest::getText - Returns the JSON response from the stats server
         * - MegaRequest::getTotalBytes - Returns the number of bytes in the response
         *
         * @param data JSON data to send to the stats server
         * @param port Server port to connect
         * @param listener MegaRequestListener to track this request
         */
        void sendChatStats(const char *data, int port = 0, MegaRequestListener *listener = NULL);

        /**
         * @brief Send logs related to MEGAchat to the logs server
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_STATS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getName - Returns the data provided.
         * - MegaRequest::getNodeHandle - Returns the userid
         * - MegaRequest::getParentHandle - Returns the provided callid
         * - MegaRequest::getParamType - Returns number 2
         * - MegaRequest::getNumber - Returns the connection port
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumber - Return the HTTP status code from the stats server
         * - MegaRequest::getText - Returns the JSON response from the stats server
         * - MegaRequest::getTotalBytes - Returns the number of bytes in the response
         *
         * @param data JSON data to send to the logs server
         * @param userid handle of the user
         * @param callid handle of the call
         * @param port Server port to connect
         * @param listener MegaRequestListener to track this request
         */
        void sendChatLogs(const char *data, MegaHandle userid, MegaHandle callid = INVALID_HANDLE, int port = 0, MegaRequestListener *listener = NULL);

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
         * @param h Handle from node
         * @return char array with files attributes from the node.
         */
        const char* getFileAttribute(MegaHandle h);

        /**
         * @brief Archive a chat
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_ARCHIVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getFlag - Returns chat desired state
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - If the chatroom does not exists.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param archive Desired chat state
         * @param listener MegaRequestListener to track this request
         */
        void archiveChat(MegaHandle chatid, int archive, MegaRequestListener *listener = NULL);

        /**
         * @brief Set a retention timeframe after which older messages in the chat are automatically deleted.
         *
         * Allows a logged in operator/moderator to specify a message retention timeframe in seconds,
         * after which older messages in the chat are automatically deleted.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_RETENTION_TIME
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getTotalBytes - Returns the retention timeframe
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS - If the chatid is invalid
         * - MegaError::API_ENOENT - If there isn't any chat with the specified chatid.
         * - MegaError::API_EACCESS - If the logged in user doesn't have operator privileges
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param period retention timeframe in seconds, after which older messages in the chat are automatically deleted
         * @param listener MegaRequestListener to track this request
         */
        void setChatRetentionTime(MegaHandle chatid, unsigned period, MegaRequestListener *listener = NULL);

        /**
         * @brief Request rich preview information for specified URL
         *
         * The associated request type with this request is MegaRequest::TYPE_RICH_LINK
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getLink - Returns the requested URL
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Returns a JSON containing metadata from the URL
         *
         * @param url URL to request metadata (format: http://servername.domain)
         * @param listener MegaRequestListener to track this request
         */
        void requestRichPreview(const char *url, MegaRequestListener *listener = NULL);

        /**
         * @brief Query if there is a chat link for this chatroom
         *
         * This function can be called by any chat member to check and retrieve the current
         * public handle for the specified chat without creating it.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_LINK_HANDLE.
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getParentHandle - Returns the public handle of the chat link, if any
         *
         * On the onTransferFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - If the chatroom does not have a valid chatlink, or the chatroom does not exists.
         * - MegaError::API_EACCESS - If caller is not operator or the chat is not a public chat or it's a 1on1 room.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param listener MegaRequestListener to track this request
         */
        void chatLinkQuery(MegaHandle chatid, MegaRequestListener *listener = NULL);

        /**
         * @brief Create or retrieve the public handle of a chat link
         *
         * This function can be called by a chat operator to create or retrieve the current
         * public handle for the specified chat. It will create a management message.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_LINK_HANDLE.
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getParentHandle - Returns the public handle of the chat link
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - If the chatroom does not have a valid chatlink, or the chatroom does not exists.
         * - MegaError::API_EACCESS - If caller is not operator or the chat is not a public chat or it's a 1on1 room.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param listener MegaRequestListener to track this request
         */
        void chatLinkCreate(MegaHandle chatid, MegaRequestListener *listener = NULL);

        /**
         * @brief Delete the public handle of a chat link
         *
         * This function can be called by a chat operator to remove the current public handle
         * for the specified chat. It will create a management message.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_LINK_HANDLE.
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - If the chatroom does not have a valid chatlink, or the chatroom does not exists.
         * - MegaError::API_EACCESS - If caller is not operator or the chat is not a public chat or it's a 1on1 room.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param listener MegaRequestListener to track this request
         */
        void chatLinkDelete(MegaHandle chatid, MegaRequestListener *listener = NULL);

        /**
         * @brief Get the URL to connect to chatd for a chat link
         *
         * This function can be used by anonymous and registered users to request the URL to connect
         * to chatd, for a given public handle. @see \c MegaApi::chatLinkCreate.
         * It also returns the shard hosting the chatroom, the real chatid and the title (if any).
         * The chat-topic, for public chats, can be decrypted by using the unified-key, already
         * available as part of the link for previewers and available to participants as part of
         * the room's information. @see \c MegaTextChat::getUnifiedKey.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHAT_LINK_URL
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the public handle of the chat link
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNodeHandle - Returns the public hanle
         * - MegaRequest::getLink - Returns the URL to connect to chatd for the chat link
         * - MegaRequest::getParentHandle - Returns the chat identifier
         * - MegaRequest::getAccess - Returns the shard
         * - MegaRequest::getText - Returns the chat-topic (if any)
         * - MegaRequest::getNumDetails - Returns the current number of participants
         * - MegaRequest::getNumber - Returns the creation timestamp
         * - MegaRequest::getFlag - Returns if chatRoom is a meeting Room
         * - MegaRequest::getMegaHandleList - Returns a vector with one element (callid), if call doesn't exit it will be NULL
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - If the public handle is not valid or the chatroom does not exists.
         *
         * @note This function can be called without being logged in. In that case, the returned
         * URL will be different than for logged in users, so chatd knows whether user has a session.
         *
         * @param publichandle MegaHandle that represents the public handle of the chat link
         * @param listener MegaRequestListener to track this request
         */
        void getChatLinkURL(MegaHandle publichandle, MegaRequestListener *listener = NULL);

        /**
         * @brief Convert an public chat into a private private mode chat
         *
         * This function allows a chat operator to convert an existing public chat into a private
         * chat (closed mode, key rotation enabled). It will create a management message.
         *
         * If the groupchat already has a customized title, it's required to provide the title encrypted
         * to a new key, so it becomes private for non-participants.
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_PRIVATE_MODE.
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getText - Returns the title of the chat
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - If the chatroom does not exists.
         * - MegaError::API_EACCESS - If caller is not operator or it's a 1on1 room.
         * - MegaError::API_EEXIST - If the chat is already in private mode.
         * - MegaError::API_EARGS - If custom title is set and no title is provided.
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param title Byte array representing the title, already encrypted and converted to Base64url
         * encoding. If the chatroom doesn't have a title yet, this parameter should be NULL.
         * @param listener MegaRequestListener to track this request
         */
        void chatLinkClose(MegaHandle chatid, const char *title, MegaRequestListener *listener = NULL);

        /**
         * @brief Allows to join a public chat
         *
         * This function allows any user with a MEGA account to join an open chat that has the
         * specified public handle. It will create a management message like any new user join.
         *
         * @see \c MegaApi::chatLinkCreate
         *
         * The associated request type with this request is MegaRequest::TYPE_AUTOJOIN_PUBLIC_CHAT
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the public handle of the chat link
         * - MegaRequest::getSessionKey - Returns the unified key of the chat link
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - If the public handle is not valid.
         * - MegaError::API_EINCOMPLETE - If the no unified key is provided.
         *
         * @param publichandle MegaHandle that represents the public handle of the chat link
         * @param unifiedKey Byte array that contains the unified key, already encrypted and
         * converted to Base64url encoding.
         * @param listener MegaRequestListener to track this request
         */
        void chatLinkJoin(MegaHandle publichandle, const char *unifiedKey, MegaRequestListener *listener = NULL);

        /**
         * @brief Returns whether notifications about a chat have to be generated
         *
         * @param chatid MegaHandle that identifies the chat room
         * @return true if notification has to be created
         */
        bool isChatNotifiable(MegaHandle chatid);

        /**
         * @brief Allows to start chat call in a chat room
         *
         * The associated request type with this request is MegaRequest::TYPE_START_CHAT_CALL
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Returns the sfu url
         * - MegaRequest::getNodeHandle - Returns the call identifier
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS - If the chatid is invalid
         * - MegaError::API_EEXIST - If there is a call in the chatroom
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param listener MegaRequestListener to track this request
         */
        void startChatCall(MegaHandle chatid, MegaRequestListener* listener = nullptr);

        /**
         * @brief Allow to join chat call
         *
         * The associated request type with this request is MegaRequest::TYPE_JOIN_CHAT_CALL
         *
         * * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the call identifier
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getText - Returns the sfu url
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS - If the chatid or callid is invalid
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param callid MegaHandle that identifies the call
         * @param listener MegaRequestListener to track this request
         */
        void joinChatCall(MegaHandle chatid, MegaHandle callid, MegaRequestListener* listener = nullptr);

        /**
         * @brief Allow to end chat call
         *
         * The associated request type with this request is MegaRequest::TYPE_END_CHAT_CALL
         *
         * Valid data in the MegaRequest object received on all callbacks:
         * - MegaRequest::getNodeHandle - Returns the chat identifier
         * - MegaRequest::getParentHandle - Returns the call identifier
         * - MegaRequest::getAccess - Returns the reason to end call
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS - If the chatid or callid is invalid
         *
         * @param chatid MegaHandle that identifies the chat room
         * @param callid MegaHandle that identifies the call
         * @param reason Reason to end call (Valid value END_CALL_REASON_REJECTED)
         * @param listener MegaRequestListener to track this request
         */
        void endChatCall(MegaHandle chatid, MegaHandle callid, int reason = 0, MegaRequestListener *listener = nullptr);

#endif

        /**
         * @brief Returns whether notifications about incoming have to be generated
         *
         * @return true if notification has to be created
         */
        bool isSharesNotifiable();

        /**
         * @brief Returns whether notifications about pending contact requests have to be generated
         *
         * @return true if notification has to be created
         */
        bool isContactsNotifiable();

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

       /**
         * @brief Catch up with API for pending actionpackets
         *
         * The associated request type with this request is MegaRequest::TYPE_CATCHUP
         *
         * When onRequestFinish is called with MegaError::API_OK, the SDK is guaranteed to be
         * up to date (as for the time this function is called).
         *
         * @param listener MegaRequestListener to track this request
         */
        void catchup(MegaRequestListener *listener = NULL);

        /**
         * @brief Send a verification code txt to the supplied phone number
         *
         * Sends a 6 digit code to the user's phone. The phone number is supplied in this function call.
         * The code is sent by SMS to the user. Once the user receives it, they can type it into the app
         * and the call MegaApi::checkSMSVerificationCode can be used to validate the user did
         * receive the verification code, so that really is their phone number.
         *
         * The frequency with which this call can be used is very limited (the API allows at most
         * two SMS mssages sent for phone number per 24 hour period), so it's important to get the
         * number right on the first try. The result will be MegaError::API_ETEMPUNAVAIL if it has
         * been tried too frequently.
         *
         * Make sure to test the result of MegaApi::smsAllowedState before calling this function.
         *
         * The associated request type with this request is MegaRequest::TYPE_SEND_SMS_VERIFICATIONCODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - the phoneNumber as supplied to this function
         *
         * When the operation completes, onRequestFinish is called and the MegaError object can be:
         * - MegaError::API_ETEMPUNAVAIL if a limit is reached.
         * - MegaError::API_EACCESS if your account is already verified with an SMS number
         * - MegaError::API_EEXIST if the number is already verified for some other account.
         * - MegaError::API_EARGS if the phone number is badly formatted or invalid.
         * - MegaError::API_OK is returned upon success.
         *
         * @param phoneNumber The phone number to txt the code to, supplied by the user.
         * @param listener MegaRequestListener to track this request
         * @param reverifying_whitelisted debug usage only.  May be removed in future.
         */
        void sendSMSVerificationCode(const char* phoneNumber, MegaRequestListener *listener = NULL, bool reverifying_whitelisted = false);

        /**
         * @brief Check a verification code that the user should have received via txt
         *
         * This function validates that the user received the verification code sent by MegaApi::sendSMSVerificationCode.
         *
         * The associated request type with this request is MegaRequest::TYPE_CHECK_SMS_VERIFICATIONCODE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getText - the verificationCode as supplied to this function
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getName - the phone number that has been verified
         *
         * When the operation completes, onRequestFinish is called and the MegaError object can be:
         * - MegaError::API_EEACCESS if you have reached the verification limits.
         * - MegaError::API_EFAILED if the verification code does not match.
         * - MegaError::API_EEXPIRED if the phone number was verified on a different account.
         * - MegaError::API_OK is returned upon success.
         *
         * @param verificationCode A string supplied by the user, that they should have received via txt.
         * @param listener MegaRequestListener to track this request
         */
        void checkSMSVerificationCode(const char* verificationCode, MegaRequestListener *listener = NULL);

        /**
         * @brief Requests the contacts that are registered at MEGA (currently verified through SMS)
         *
         * The request will return any of the provided contacts that are registered at MEGA, i.e.,
         * are verified through SMS (currently).
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_REGISTERED_CONTACTS
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getMegaStringMap - Returns the contacts that are to be checked
         * \c contacts is a MegaStringMap from 'user detail' to the user's name. For instance:
         * {
         *   "+0000000010": "John Smith",
         *   "+0000000011": "Peter Smith",
         * }
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaStringTable - Returns the information about the contacts with three columns:
         *  1. entry user detail (the user detail as it was provided in the request)
         *  2. identifier (the user's identifier)
         *  3. user detail (the normalized user detail, e.g., +00 0000 0010)
         *
         * There is a limit on how many unique details can be looked up per account, to prevent
         * abuse and iterating over the phone number space to find users in Mega.
         * An API_ETOOMANY error will be returned if you hit one of these limits.
         * An API_EARGS error will be returned if your contact details are invalid (malformed SMS number for example)
         *
         * @param contacts The map of contacts to get registered contacts from
         * @param listener MegaRequestListener to track this request
         */
        void getRegisteredContacts(const MegaStringMap* contacts, MegaRequestListener *listener = NULL);

        /**
         * @brief Requests the currently available country calling codes
         *
         * The response value is stored as a MegaStringListMap mapping from two-letter country code
         * to a list of calling codes. For instance:
         * {
         *   "AD": ["376"],
         *   "AE": ["971", "13"],
         * }
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaStringListMap where the keys are two-letter country codes and the
         *   values a list of calling codes.
         *
         * For this command, there are currently no command specific error codes returned by the API.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getCountryCallingCodes(MegaRequestListener *listener = NULL);

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
         * - MegaError::API_EARGS - If the link is not a valid folder link
         * - MegaError::API_EKEY - If the public link does not contain the key or it is invalid
         *
         * @param megaFolderLink Public link to a folder in MEGA
         * @param listener MegaRequestListener to track this request
         */
        void getPublicLinkInformation(const char *megaFolderLink, MegaRequestListener *listener = NULL);

        /**
         * @brief Get an object that can lock the MegaApi, allowing multiple quick synchronous calls.
         *
         * This object must be used very carefully.  It is meant to be used  when the application is about
         * to make a burst of synchronous calls (that return data immediately, without using a listener)
         * to the API over a very short time period, which could otherwise be blocked multiple times
         * interrupted by the MegaApi's operation.
         *
         * The MegaApiLock usual use is to request it already locked, and the caller must destroy it
         * when its sequence of operations are complete, which will allow the MegaApi to continue again.
         * However explicit lock and unlock calls can also be made on it, which are protected from
         * making more than one lock, and the destructor will make sure the lock is released.
         *
         * You take ownership of the returned value, and you must delete it when the sequence is complete.
         */
        MegaApiLock* getMegaApiLock(bool lockNow);

        /**
         * @brief Call the low level function setrlimit() for NOFILE, needed for some platforms.
         *
         * Particularly on phones, the system default limit for the number of open files (and sockets)
         * is quite low.   When the SDK can be working on many files and many sockets at once,
         * we need a higher limit.   Those limits need to take into account the needs of the whole
         * app and not just the SDK, of course.   This function is provided in order that the app
         * can make that call and set appropriate limits.
         *
         * @param newNumFileLimit The new limit of file and socket handles for the whole app.
         *
         * @return True when there were no errors setting the new limit (even when clipped to the maximum
         * allowed value). It returns false when setting a new limit failed.
         */
        bool platformSetRLimitNumFile(int newNumFileLimit) const;

        /**
         * @brief Call the low level function getrlimit() for NOFILE, needed for some platforms.
         *
         * @return The current limit for the number of open files (and sockets) for the app, or -1 if error.
         */
        int platformGetRLimitNumFile() const;

        /**
         * @brief Requests a list of all Smart Banners available for current user.
         *
         * The response value is stored as a MegaBannerList.
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_BANNERS
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaBannerList: the list of banners
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EACCESS - If called with no user being logged in.
         * - MegaError::API_EINTERNAL - If the internally used user attribute exists but can't be decoded.
         * - MegaError::API_ENOENT if there are no banners to return to the user.
         *
         * @param listener MegaRequestListener to track this request
         */
        void getBanners(MegaRequestListener *listener = nullptr);

        /**
         * @brief No longer show the Smart Banner with the specified id to the current user.
         */
        void dismissBanner(int id, MegaRequestListener *listener = nullptr);

        /**
         * @brief Registers a backup to display in Backup Centre
         *
         * Apps should register backups, like CameraUploads, in order to be listed in the
         * BackupCentre. The client should send heartbeats to indicate the progress of the
         * backup (see \c MegaApi::sendBackupHeartbeats).
         *
         * Possible types of backups:
         *  BACKUP_TYPE_CAMERA_UPLOADS = 3,
         *  BACKUP_TYPE_MEDIA_UPLOADS = 4,   // Android has a secondary CU
         *
         * The associated request type with this request is MegaRequest::TYPE_BACKUP_PUT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns the backupId
         * - MegaRequest::getNodeHandle - Returns the target node of the backup
         * - MegaRequest::getName - Returns the backup name of the remote location
         * - MegaRequest::getAccess - Returns the backup state
         * - MegaRequest::getFile - Returns the path of the local folder
         * - MegaRequest::getTotalBytes - Returns the backup type
         * - MegaRequest::getNumDetails - Returns the backup substate
         * - MegaRequest::getFlag - Returns true
         * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
         *
         * @param backupType back up type requested for the service
         * @param targetNode MEGA folder to hold the backups
         * @param localFolder Local path of the folder
         * @param backupName Name of the backup
         * @param state state
         * @param subState subState
         * @param listener MegaRequestListener to track this request
        */
        void setBackup(int backupType, MegaHandle targetNode, const char* localFolder, const char* backupName, int state, int subState, MegaRequestListener* listener = nullptr);

        /**
         * @brief Update the information about a registered backup for Backup Centre
         *
         * Possible types of backups:
         *  BACKUP_TYPE_INVALID = -1,
         *  BACKUP_TYPE_CAMERA_UPLOADS = 3,
         *  BACKUP_TYPE_MEDIA_UPLOADS = 4,   // Android has a secondary CU
         *
         *  Params that keep the same value are passed with invalid value to avoid to send to the server
         *    Invalid values:
         *    - type: BACKUP_TYPE_INVALID
         *    - nodeHandle: UNDEF
         *    - backupName: nullptr
         *    - localFolder: nullptr
         *    - deviceId: nullptr
         *    - state: -1
         *    - subState: -1
         *
         * The associated request type with this request is MegaRequest::TYPE_BACKUP_PUT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns the backupId
         * - MegaRequest::getTotalBytes - Returns the backup type
         * - MegaRequest::getNodeHandle - Returns the target node of the backup
         * - MegaRequest::getName - Returns the backup name of the remote location
         * - MegaRequest::getFile - Returns the path of the local folder
         * - MegaRequest::getAccess - Returns the backup state
         * - MegaRequest::getNumDetails - Returns the backup substate
         * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
         *
         * @param backupId backup id identifying the backup to be updated
         * @param backupType back up type requested for the service
         * @param targetNode MEGA folder to hold the backups
         * @param localFolder Local path of the folder
         * @param backupName Name of the backup
         * @param state backup state
         * @param subState backup subState
         * @param listener MegaRequestListener to track this request
        */
        void updateBackup(MegaHandle backupId, int backupType, MegaHandle targetNode, const char* localFolder,  const char* backupName, int state, int subState, MegaRequestListener* listener = nullptr);

        /**
         * @brief Unregister a backup already registered for the Backup Centre
         *
         * This method allows to remove a backup from the list of backups displayed in the
         * Backup Centre. @see \c MegaApi::setScheduledCopy.
         *
         * The associated request type with this request is MegaRequest::TYPE_BACKUP_REMOVE
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns the backupId
         * - MegaRequest::getListener - Returns the MegaRequestListener to track this request
         *
         * @param backupId backup id identifying the backup to be removed
         * @param listener MegaRequestListener to track this request
        */
        void removeBackup(MegaHandle backupId, MegaRequestListener *listener = nullptr);

        /**
         * @brief Send heartbeat associated with an existing backup
         *
         * The client should call this method regularly for every registered backup, in order to
         * inform about the status of the backup.
         *
         * Progress, last timestamp and last node are not always meaningful (ie. when the Camera
         * Uploads starts a new batch, there isn't a last node, or when the CU up to date and
         * inactive for long time, the progress doesn't make sense). In consequence, these parameters
         * are optional. They will not be sent to API if they take the following values:
         * - lastNode = INVALID_HANDLE
         * - lastTs = -1
         * - progress = -1
         *
         * The associated request type with this request is MegaRequest::TYPE_BACKUP_PUT_HEART_BEAT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns the backupId
         * - MegaRequest::getAccess - Returns the backup state
         * - MegaRequest::getNumDetails - Returns the backup substate
         * - MegaRequest::getParamType - Returns the number of pending upload transfers
         * - MegaRequest::getTransferTag - Returns the number of pending download transfers
         * - MegaRequest::getNumber - Returns the last action timestamp
         * - MegaRequest::getNodeHandle - Returns the last node handle to be synced
         *
         * @param backupId backup id identifying the backup
         * @param status backup status
         * @param progress backup progress
         * @param ups Number of pending upload transfers
         * @param downs Number of pending download transfers
         * @param ts Last action timestamp
         * @param lastNode Last node handle to be synced
         * @param listener MegaRequestListener to track this request
        */
        void sendBackupHeartbeat(MegaHandle backupId, int status, int progress, int ups, int downs, long long ts, MegaHandle lastNode, MegaRequestListener *listener = nullptr);

        /**
         * @brief Fetch Google ads
         *
         * The associated request type with this request is MegaRequest::TYPE_FETCH_GOOGLE_ADS
         *
         * @deprecated It returns API_EEXPIRED at onRequestFinish
         */
        void fetchGoogleAds(int adFlags, MegaStringList *adUnits, MegaHandle publicHandle = INVALID_HANDLE, MegaRequestListener *listener = nullptr);

        /**
         * @brief Check if Google ads should show or not
         *
         * The associated request type with this request is MegaRequest::TYPE_QUERY_GOOGLE_ADS
         *
         * @deprecated It returns API_EEXPIRED at onRequestFinish
         */
        void queryGoogleAds(int adFlags, MegaHandle publicHandle = INVALID_HANDLE, MegaRequestListener *listener = nullptr);

        /**
         * @brief Set a bitmap to indicate whether some cookies are enabled or not
         *
         * The associated request type with this request is MegaRequest::TYPE_SET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         *  - MegaRequest::getParamType - Returns the attribute type MegaApi::USER_ATTR_COOKIE_SETTINGS
         *  - MegaRequest::getNumDetails - Return a bitmap with cookie settings
         *  - MegaRequest::getListener - Returns the MegaRequestListener to track this request
         *
         * @param settings A bitmap with cookie settings
         * Valid bits are:
         *      - Bit 0: essential
         *      - Bit 1: preference
         *      - Bit 2: analytics
         *      - Bit 3: ads
         *      - Bit 4: thirdparty
         * @param listener MegaRequestListener to track this request
         */
        void setCookieSettings(int settings, MegaRequestListener *listener = nullptr);

        /**
         * @brief Get a bitmap to indicate whether some cookies are enabled or not
         *
         * The associated request type with this request is MegaRequest::TYPE_GET_ATTR_USER
         * Valid data in the MegaRequest object received on callbacks:
         *  - MegaRequest::getParamType - Returns the value USER_ATTR_COOKIE_SETTINGS
         *  - MegaRequest::getListener - Returns the MegaRequestListener to track this request
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getNumDetails Return the bitmap with cookie settings
         *   Valid bits are:
         *      - Bit 0: essential
         *      - Bit 1: preference
         *      - Bit 2: analytics
         *      - Bit 3: ads
         *      - Bit 4: thirdparty
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EINTERNAL - If the value for cookie settings bitmap was invalid
         *
         * @param listener MegaRequestListener to track this request
         */
        void getCookieSettings(MegaRequestListener *listener = nullptr);

        /**
         * @brief Check if the app can start showing the cookie banner
         *
         * This function will NOT return a valid value until the callback onEvent with
         * type MegaApi::EVENT_MISC_FLAGS_READY is received. You can also rely on the completion of
         * a fetchnodes to check this value, but only when it follows a login with user and password,
         * not when an existing session is resumed.
         *
         * For not logged-in mode, you need to call MegaApi::getMiscFlags first.
         *
         * @return True if this feature is enabled. Otherwise, false.
         */
        bool cookieBannerEnabled();

        /**
         * @brief Start receiving notifications for [dis]connected external drives, from the OS
         *
         * After a call to this function, and before another one, stopDriveMonitor() must be called,
         * otherwise it will fail.
         *
         * @return True when notifications have been started.
         *         False when called while already receiving notifications, or
         *         notifications could not have been started due to errors or missing implementation,
         */
        bool startDriveMonitor();

        /**
         * @brief Stop receiving notifications for [dis]connected external drives, from the OS
         */
        void stopDriveMonitor();

        /**
         * @brief Check if drive monitor is running
         * @return True if it is running, false otherwise.
         */
        bool driveMonitorEnabled();

        /**
         * @brief Request creation of a new Set
         *
         * The associated request type with this request is MegaRequest::TYPE_PUT_SET
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns INVALID_HANDLE
         * - MegaRequest::getText - Returns name of the Set
         * - MegaRequest::getParamType - Returns CREATE_SET, possibly combined with OPTION_SET_NAME
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaSet - Returns either the new Set, or null if it was not created.
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param name the name that should be given to the new Set
         * @param listener MegaRequestListener to track this request
         */
        void createSet(const char* name = nullptr, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request to update the name of a Set
         *
         * The associated request type with this request is MegaRequest::TYPE_PUT_SET
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns id of the Set to be updated
         * - MegaRequest::getText - Returns new name of the Set
         * - MegaRequest::getParamType - Returns OPTION_SET_NAME
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - Set with the given id could not be found (before or after the request).
         * - MegaError::API_EINTERNAL - Received answer could not be read.
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set to be updated
         * @param name the new name that should be given to the Set
         * @param listener MegaRequestListener to track this request
         */
        void updateSetName(MegaHandle sid, const char* name, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request to update the cover of a Set
         *
         * The associated request type with this request is MegaRequest::TYPE_PUT_SET
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns id of the Set to be updated
         * - MegaRequest::getNodeHandle - Returns Element id to be set as the new cover
         * - MegaRequest::getParamType - Returns OPTION_SET_COVER
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_EARGS - Given Element id was not part of the current Set; Malformed (from API).
         * - MegaError::API_ENOENT - Set with the given id could not be found (before or after the request).
         * - MegaError::API_EINTERNAL - Received answer could not be read.
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set to be updated
         * @param eid the id of the Element to be set as cover
         * @param listener MegaRequestListener to track this request
         */
        void putSetCover(MegaHandle sid, MegaHandle eid, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request to remove a Set
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SET
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns id of the Set to be removed
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - Set could not be found.
         * - MegaError::API_EINTERNAL - Received answer could not be read.
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set to be removed
         * @param listener MegaRequestListener to track this request
         */
        void removeSet(MegaHandle sid, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request to fetch a Set and its Elements
         *
         * The associated request type with this request is MegaRequest::TYPE_FETCH_SET
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns id of the Set to be fetched
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaSet - Returns the Set
         * - MegaRequest::getMegaSetElementList - Returns the list of Elements
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - Set could not be found.
         * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set to be fetched
         * @param listener MegaRequestListener to track this request
         */
        void fetchSet(MegaHandle sid, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request creation of a new Element for a Set
         *
         * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns INVALID_HANDLE
         * - MegaRequest::getTotalBytes - Returns the id of the Set
         * - MegaRequest::getParamType - Returns CREATE_ELEMENT, possibly combined with OPTION_ELEMENT_NAME
         * - MegaRequest::getText - Returns name of the Element
         *
         * Valid data in the MegaRequest object received in onRequestFinish when the error code
         * is MegaError::API_OK:
         * - MegaRequest::getMegaSetElementList - Returns a list containing only the new Element
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - Set could not be found, or node could not be found.
         * - MegaError::API_EKEY - File-node had no key.
         * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set that will own the new Element
         * @param node the handle of the file-node that will be represented by the new Element
         * @param name the name that should be given to the new Element
         * @param listener MegaRequestListener to track this request
         */
        void createSetElement(MegaHandle sid, MegaHandle node, const char* name = nullptr, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request to update the name of an Element
         *
         * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns id of the Element to be updated
         * - MegaRequest::getTotalBytes - Returns the id of the Set
         * - MegaRequest::getParamType - Returns OPTION_ELEMENT_NAME
         * - MegaRequest::getText - Returns name of the Element
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - Element could not be found.
         * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set that owns the Element
         * @param eid the id of the Element that will be updated
         * @param name the new name that should be given to the Element
         * @param listener MegaRequestListener to track this request
         */
        void updateSetElementName(MegaHandle sid, MegaHandle eid, const char* name, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request to update the order of an Element
         *
         * The associated request type with this request is MegaRequest::TYPE_PUT_SET_ELEMENT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns id of the Element to be updated
         * - MegaRequest::getTotalBytes - Returns the id of the Set
         * - MegaRequest::getParamType - Returns OPTION_ELEMENT_ORDER
         * - MegaRequest::getNumber - Returns order of the Element
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - Element could not be found.
         * - MegaError::API_EINTERNAL - Received answer could not be read or decrypted.
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set that owns the Element
         * @param eid the id of the Element that will be updated
         * @param order the new order of the Element
         * @param listener MegaRequestListener to track this request
         */
        void updateSetElementOrder(MegaHandle sid, MegaHandle eid, int64_t order, MegaRequestListener* listener = nullptr);

        /**
         * @brief Request to remove an Element
         *
         * The associated request type with this request is MegaRequest::TYPE_REMOVE_SET_ELEMENT
         * Valid data in the MegaRequest object received on callbacks:
         * - MegaRequest::getParentHandle - Returns id of the Element to be removed
         * - MegaRequest::getTotalBytes - Returns the id of the Set
         *
         * On the onRequestFinish error, the error code associated to the MegaError can be:
         * - MegaError::API_ENOENT - No Set or no Element with given ids could be found (before or after the request).
         * - MegaError::API_EINTERNAL - Received answer could not be read.
         * - MegaError::API_EARGS - Malformed (from API).
         * - MegaError::API_EACCESS - Permissions Error (from API).
         *
         * @param sid the id of the Set that owns the Element
         * @param eid the id of the Element to be removed
         * @param listener MegaRequestListener to track this request
         */
        void removeSetElement(MegaHandle sid, MegaHandle eid, MegaRequestListener* listener = nullptr);

        /**
         * @brief Get a list of all Sets available for current user.
         *
         * The response value is stored as a MegaSetList.
         *
         * You take the ownership of the returned value
         *
         * @return list of Sets
         */
        MegaSetList* getSets();

        /**
         * @brief Get the Set with the given id, for current user.
         *
         * The response value is stored as a MegaSet.
         *
         * You take the ownership of the returned value
         *
         * @param sid the id of the Set to be retrieved
         *
         * @return the requested Set, or null if not found
         */
        MegaSet* getSet(MegaHandle sid);

        /**
         * @brief Get the cover (Element id) of the Set with the given id, for current user.
         *
         * @param sid the id of the Set to retrieve the cover for
         *
         * @return Element id of the cover, or INVALIDHANDLE if not set or invalid id
         */
        MegaHandle getSetCover(MegaHandle sid);

        /**
         * @brief Get all Elements in the Set with given id, for current user.
         *
         * The response value is stored as a MegaSetElementList.
         *
         * You take the ownership of the returned value
         *
         * @param sid the id of the Set owning the Elements
         *
         * @return all Elements in that Set, or null if not found or none added
         */
        MegaSetElementList* getSetElements(MegaHandle sid);

        /**
         * @brief Get a particular Element in a particular Set, for current user.
         *
         * The response value is stored as a MegaSetElement.
         *
         * You take the ownership of the returned value
         *
         * @param sid the id of the Set owning the Element
         * @param eid the id of the Element to be retrieved
         *
         * @return requested Element, or null if not found
         */
        MegaSetElement* getSetElement(MegaHandle sid, MegaHandle eid);

        /**
         * @brief Enable or disable the request status monitor
         *
         * When it's enabled, the request status monitor generates events of type
         * MegaEvent::EVENT_REQSTAT_PROGRESS with the per mille progress in
         * the field MegaEvent::getNumber(), or -1 if there isn't any operation in progress.
         *
         * @param enable True to enable the request status monitor, or false to disable it
         */
        void enableRequestStatusMonitor(bool enable);

        /**
         * @brief Get the status of the request status monitor
         * @return True when the request status monitor is enabled, or false if it's disabled
         */
        bool requestStatusMonitorEnabled();

 private:
        MegaApiImpl *pImpl = nullptr;
        friend class MegaApiImpl;
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
        ACCOUNT_TYPE_LITE = 4,
        ACCOUNT_TYPE_BUSINESS = 100,
        ACCOUNT_TYPE_PRO_FLEXI = 101    // also known as PRO 4
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
     * - MegaAccountDetails::ACCOUNT_TYPE_BUSINESS = 100
     * - MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI = 101
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
     * @brief Get the subscription method
     *
     * You take the ownership of the returned value
     *
     * @return Subscription method. For example "Credit Card".
     */
    virtual char* getSubscriptionMethod();

    /**
     * @brief Get the subscription method id
     *
     * @return Subscription method. For example 16.
     */
    virtual int getSubscriptionMethodId();

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
     * @brief Get the used bandwidth for own user allowance
     * @see: MegaAccountDetails::getTransferUsed
     * @return Used bandwidth (in bytes)
     */
    virtual long long getTransferOwnUsed();

    /**
     * @brief Get the used bandwidth served to other users
     * @see: MegaAccountDetails::getTransferUsed
     * @return Used bandwidth (in bytes)
     */
    virtual long long getTransferSrvUsed();

    /**
     * @brief Get the used bandwidth allowance including own, free and served to other users
     * @see: MegaAccountDetails::getTransferOwnUsed, MegaAccountDetails::getTemporalBandwidth, MegaAccountDetails::getTransferSrvUsed
     * @return Used bandwidth (in bytes)
     */
    virtual long long getTransferUsed();

    /**
     * @brief Returns the number of nodes with account usage info
     *
     * You can get information about each node using MegaAccountDetails::getStorageUsed,
     * MegaAccountDetails::getNumFiles, MegaAccountDetails::getNumFolders
     *
     * This function can return:
     * - 0 (no info about any node)
     * - 3 (info about the root node, the vault node and the rubbish node)
     * Use MegaApi::getRootNode MegaApi::getVaultNode and MegaApi::getRubbishNode to get those nodes.
     *
     * - >3 (info about root, vault, rubbish and incoming shares)
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
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getVaultNode
     */
    virtual long long getStorageUsed(MegaHandle handle);

    /**
     * @brief Get the number of files in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of files in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getVaultNode
     */
    virtual long long getNumFiles(MegaHandle handle);

    /**
     * @brief Get the number of folders in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of folders in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getVaultNode
     */
    virtual long long getNumFolders(MegaHandle handle);

    /**
     * @brief Get the used storage by versions in for a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Used storage by versions (in bytes)
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getVaultNode
     */
    virtual long long getVersionStorageUsed(MegaHandle handle);

    /**
     * @brief Get the number of versioned files in a node
     *
     * Only root nodes are supported.
     *
     * @param handle Handle of the node to check
     * @return Number of versioned files in the node
     * @see MegaApi::getRootNode, MegaApi::getRubbishNode, MegaApi::getVaultNode
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
     * @brief Get the number of bytes that were recently transferred using free allowance
     *
     * The time interval in which those bytes were transferred
     * is provided (in hours) using MegaAccountDetails::getTemporalBandwidthInterval
     *
     * @see: MegaAccountDetails::getTransferUsed
     * @return Number of bytes that were recently transferred
     */
    virtual long long getTemporalBandwidth();

    /**
     * @brief Check if the temporal bandwidth usage is valid after an overquota error
     * @return True if the temporal bandwidth is valid, otherwise false
     */
    virtual bool isTemporalBandwidthValid();
};

class MegaCurrency
{
public:
    virtual ~MegaCurrency();

    /**
     * @brief Creates a copy of this MegaCurrency object.
     *
     * The resulting object is fully independent of the source MegaCurrency,
     * it contains a copy of all internal attributes, so it will be valid after
     * the original object is deleted.
     *
     * You are the owner of the returned object
     *
     * @return Copy of the MegaCurrency object
     */
    virtual MegaCurrency *copy();

    /**
     * @brief Get the currency symbol of prices
     *
     * The currency symbol is encoded in B64url, since it may be a UTF-8 char.
     * In example, for €, it returns "4oKs".
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @return currency symbol of price
     */
    virtual const char* getCurrencySymbol();

    /**
     * @brief Get the currency name of prices, ie. EUR
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @return currency name of price
     */
    virtual const char* getCurrencyName();

    /**
     * @brief Get the currency symbol of local prices
     *
     * The currency symbol is encoded in B64url, since it may be a UTF-8 char.
     * In example, for €, it returns "4oKs".
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @return currency symbol of local price
     */
    virtual const char* getLocalCurrencySymbol();

    /**
     * @brief Get the currency name of local prices, ie. NZD
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @return currency name of local price
     */
    virtual const char* getLocalCurrencyName();
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
     * - MegaAccountDetails::ACCOUNT_TYPE_BUSINESS = 100
     * - MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI = 101
     */
    virtual int getProLevel(int productIndex);

    /**
     * @brief Get the number of GB of storage associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @note business plans have unlimited storage
     * @return number of GB of storage, zero if index is invalid, or -1
     * if pricing plan is a business plan
     */
    virtual int getGBStorage(int productIndex);

    /**
     * @brief Get the number of GB of bandwidth associated with the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @note business plans have unlimited bandwidth
     * @return number of GB of bandwidth, zero if index is invalid, or -1,
     * if pricing plan is a business plan
     */
    virtual int getGBTransfer(int productIndex);

    /**
     * @brief Get the duration of the product (in months)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Duration of the product (in months)
     */
    virtual int getMonths(int productIndex);

    /**
     * @brief Get the price of the product (in cents)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Price of the product (in cents)
     */
    virtual int getAmount(int productIndex);

    /**
     * @brief Get the price in the local currency (in cents)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Price of the product (in cents)
     */
    virtual int getLocalPrice(int productIndex);

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
     * @return iOS ID of the product, NULL if index is invalid or an empty string
     * if pricing plan is a business plan.
     */
    virtual const char* getIosID(int productIndex);

    /**
     * @brief Get the Android ID of the product
     *
     * The SDK retains the ownership of the returned value. It will be valid until
     * the MegaPricing object is deleted.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Android ID of the product, NULL if index is invalid or an empty string
     * if pricing plan is a business plan.
     */
    virtual const char* getAndroidID(int productIndex);

    /**
     * @brief Returns if the pricing plan is a business or Pro Flexi plan
     *
     * You can check if the plan is pure buiness or Pro Flexi by calling
     * the method MegaApi::getProLevel
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return true if the pricing plan is a business or Pro Flexi plan, otherwise return false
     */
    virtual bool isBusinessType(int productIndex);

    /**
     * @brief Get the monthly price of the product (in cents)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return Monthly price of the product (in cents)
     */
    virtual int getAmountMonth(int productIndex);

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

    /**
     * @brief Get the number of GB of storage associated with the product, per user
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of storage associated with the product, per user
     */
    virtual int getGBStoragePerUser(int productIndex);

    /**
     * @brief Get the number of GB of transfer associated with the product, per user
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of transfer associated with the product, per user
     */
    virtual int getGBTransferPerUser(int productIndex);

    /**
     * @brief Get the minimum number of users to purchase the product
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return minimum number of users to purchase the product
     */
    virtual unsigned int getMinUsers(int productIndex);

    /**
     * @brief Get the monthly price of the product, per user (in cents)
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return monthly price of the product, per user (in cents)
     */
    virtual unsigned int getPricePerUser(int productIndex);

    /**
     * @brief Get the monthly local price of the product, per user (in cents)
     *
     * Local prices are only available if the account will be charged in a different
     * currency than local.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return monthly local price of the product, per user (in cents)
     */
    virtual unsigned int getLocalPricePerUser(int productIndex);

    /**
     * @brief Get the price per storage block
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return price per storage block
     */
    virtual unsigned int getPricePerStorage(int productIndex);

    /**
     * @brief Get the local price per storage block
     *
     * Local prices are only available if the account will be charged in a different
     * currency than local.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return local price per storage block
     */
    virtual unsigned int getLocalPricePerStorage(int productIndex);

    /**
     * @brief Get the number of GB of storage, per block
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of storage, per block
     */
    virtual int getGBPerStorage(int productIndex);

    /**
     * @brief Get the price per transfer block
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return price per transfer block
     */
    virtual unsigned int getPricePerTransfer(int productIndex);

    /**
     * @brief Get the local price per transfer block
     *
     * Local prices are only available if the account will be charged in a different
     * currency than local.
     *
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return local price per storage block
     */
    virtual unsigned int getLocalPricePerTransfer(int productIndex);

    /**
     * @brief Get the number of GB of transfer, per block
     * @param productIndex Product index (from 0 to MegaPricing::getNumProducts)
     * @return number of GB of transfer, per block
     */
    virtual int getGBPerTransfer(int productIndex);
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
        MEGA_ACHIEVEMENT_MOBILE_INSTALL     = 5,
        MEGA_ACHIEVEMENT_ADD_PHONE          = 9
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
     *  - MEGA_ACHIEVEMENT_ADD_PHONE = 9
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
     *  - MEGA_ACHIEVEMENT_ADD_PHONE = 9
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
     *  - MEGA_ACHIEVEMENT_ADD_PHONE = 9
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

class MegaCancelToken
{
protected:
    MegaCancelToken();

public:

    /**
     * @brief Creates an object which can be passed as parameter for some MegaApi methods in order to
     * request the cancellation of the processing associated to the function. @see MegaApi::search
     *
     * You take ownership of the returned value.
     *
     * @return A pointer to an object that allows to cancel the processing of some functions.
     */
    static MegaCancelToken* createInstance();

    virtual ~MegaCancelToken();

    /**
     * @brief Allows to set the value of the flag
     */
    virtual void cancel() = 0;

    /**
     * @brief Returns the state of the flag
     * @return The state of the flag
     */
    virtual bool isCancelled() const = 0;
};

}

#endif //MEGAAPI_H
