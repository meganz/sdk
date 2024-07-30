/**
 * @file MEGANode.h
 * @brief Represents a node (file/folder) in the MEGA account
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
#import <Foundation/Foundation.h>
#import "PasswordNodeData.h"

typedef NS_ENUM (NSInteger, MEGANodeType) {
    MEGANodeTypeUnknown = -1,
    MEGANodeTypeFile = 0,
    MEGANodeTypeFolder,
    MEGANodeTypeRoot,
    MEGANodeTypeIncoming,
    MEGANodeTypeRubbish
};

typedef NS_ENUM(NSInteger, MEGANodeLabel) {
    MEGANodeLabelUnknown = 0,
    MEGANodeLabelRed,
    MEGANodeLabelOrange,
    MEGANodeLabelYellow,
    MEGANodeLabelGreen,
    MEGANodeLabelBlue,
    MEGANodeLabelPurple,
    MEGANodeLabelGrey
};

typedef NS_ENUM(NSUInteger, MEGANodeChangeType) {
    MEGANodeChangeTypeRemoved        = 0x01,
    MEGANodeChangeTypeAttributes     = 0x02,
    MEGANodeChangeTypeOwner          = 0x04,
    MEGANodeChangeTypeTimestamp      = 0x08,
    MEGANodeChangeTypeFileAttributes = 0x10,
    MEGANodeChangeTypeInShare        = 0x20,
    MEGANodeChangeTypeOutShare       = 0x40,
    MEGANodeChangeTypeParent         = 0x80,
    MEGANodeChangeTypePendingShare   = 0x100,
    MEGANodeChangeTypePublicLink     = 0x200,
    MEGANodeChangeTypeNew            = 0x400,
    MEGANodeChangeTypeName           = 0x800,
    MEGANodeChangeTypeFavourite      = 0x1000,
    MEGANodeChangeTypeSensitive      = 0x4000
};

typedef NS_ENUM (NSInteger, MEGANodeFormatType) {
    MEGANodeFormatTypeUnknown = 0,
    MEGANodeFormatTypePhoto,
    MEGANodeFormatTypeAudio,
    MEGANodeFormatTypeVideo,
    MEGANodeFormatTypeDocument,
    MEGANodeFormatTypePdf,
    MEGANodeFormatTypePresentation,
    MEGANodeFormatTypeArchive,
    MEGANodeFormatTypeProgram,
    MEGANodeFormatTypeMisc,
    MEGANodeFormatTypeSpreadsheet,
    MEGANodeFormatTypeAllDocs,
    MEGANodeFormatTypeOthers
};

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Represents a node (file/folder) in the MEGA account.
 *
 * It allows to get all data related to a file/folder in MEGA. It can be also used
 * to start SDK requests ([MEGASdk renameNode:newName:], [MEGASdk moveNode:newParent:], etc.)
 *
 * Objects of this class aren't live, they are snapshots of the state of a node
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can inspect the MEGA filesystem and get these objects using
 * [MEGASdk childrenForParent:], [MEGASdk childNodeForParent:name:] and other MEGASdk functions.
 *
 */
@interface MEGANode : NSObject

/**
 * @brief Type of the node.
 *
 * Valid values are:
 * - MEGANodeTypeUnknown = -1,
 * Unknown node type.
 *
 * - MEGANodeTypeFile = 0,
 * The MEGANode object represents a file in MEGA.
 *
 * - MEGANodeTypeFolder = 1
 * The MEGANode object represents a folder in MEGA.
 *
 * - MEGANodeTypeRoot = 2
 * The MEGANode object represents root of the MEGA Cloud Drive.
 *
 * - MEGANodeTypeIncoming = 3
 * The MEGANode object represents root of the MEGA Inbox.
 *
 * - MEGANodeTypeRubbish = 4
 * The MEGANode object represents root of the MEGA Rubbish Bin.
 *
 */
@property (readonly, nonatomic) MEGANodeType type;

/**
 * @brief Name of the node.
 *
 * The name is only valid for nodes of type MEGANodeTypeFile or MEGANodeTypeFolder.
 * For other MEGANode types, the name is undefined.
 *
 */
@property (readonly, nonatomic, nullable) NSString *name;

/**
 * @brief The fingerprint (Base64-encoded) of the node
 *
 * Only files have a fingerprint, and there could be files without it.
 * If the node doesn't have a fingerprint, this funtion returns nil
 *
 * Base64-encoded fingerprint of the node, or nil it the node doesn't have a fingerprint.
 */
@property (readonly, nonatomic, nullable) NSString *fingerprint;

/**
 * @brief Duration of the node for audio/video files, in seconds. -1 if not set.
 */
@property (readonly, nonatomic) NSInteger duration;

/**
 * @brief Width of the node for video files, in pixels. -1 if not set.
 */
@property (readonly, nonatomic) NSInteger width;

/**
 * @brief Height of the node for video files, in pixels. -1 if not set.
 */
@property (readonly, nonatomic) NSInteger height;

/**
 * @brief ShortCode of the node for video files. -1 if not set.
 */
@property (readonly, nonatomic) NSInteger shortFormat;

/**
 * @brief VideoCodecId of the node for video files. -1 if not set.
 */
@property (readonly, nonatomic) NSInteger videoCodecId;

/**
 * @brief Get the attribute of the node representing if node is marked as favourite.
 *
 * @return YES if node is marked as favourite, otherwise return NO (attribute is not set).
 */
@property (readonly, nonatomic, getter=isFavourite) BOOL favourite;

/**
 * @brief Get the attribute of the node representing if node is marked as sensitive.
 *
 * @return YES if node is marked as sensitive, otherwise return NO (attribute is not set).
 */
@property (readonly, nonatomic) BOOL isMarkedSensitive;

/**
 * @brief Get the attribute of the node representing the description
 *
 * @return Node description
 */
@property (readonly, nonatomic, nullable) NSString *description;

/**
 * @brief Get the attribute of the node representing its label.
 *
 * @return The label of the node, valid values are:
 *  - MEGANodeLabelUnknown = 0
 *  - MEGANodeLabelRed = 1
 *  - MEGANodeLabelOrange = 2
 *  - MEGANodeLabelYellow = 3
 *  - MEGANodeLabelGreen = 4
 *  - MEGANodeLabelBlue = 5
 *  - MEGANodeLabelPurple = 6
 *  - MEGANodeLabelGrey = 7
 */
@property (readonly, nonatomic) MEGANodeLabel label;

/**
 * @brief Attribute of the node representing the latitude coordinate in its decimal
 * degree notation, or nil if this attribute is not set.
 *
 * The purpose of this attribute is to store the latitude coordinate where a photo was taken.
 *
 */
@property (readonly, nonatomic, nullable) NSNumber *latitude;

/**
 * @brief Attribute of the node representing the longitude coordinate in its decimal
 * degree notation, or nil if this attribute is not set.
 *
 * The purpose of this attribute is to store the longitude coordinate where a photo was taken.
 *
 */
@property (readonly, nonatomic, nullable) NSNumber *longitude;

/**
 * @brief Handle of this MEGANode in a Base64-encoded string.
 */
@property (readonly, nonatomic, nullable) NSString *base64Handle;

/**
 * @brief Size of the node.
 *
 * The value is only valid for nodes of type MEGANodeTypeFile.
 *
 */
@property (readonly, nonatomic, nullable) NSNumber *size;

/**
 * @brief Creation time of the node in MEGA (in seconds since the epoch).
 *
 * The value is only valid for nodes of type MEGANodeTypeFile or MEGANodeTypeFolder.
 *
 */
@property (readonly, nonatomic, nullable) NSDate *creationTime;

/**
 * @brief Modification time of the file that was uploaded to MEGA (in seconds since the epoch).
 *
 * The value is only valid for nodes of type MEGANodeTypeFile.
 *
 */
@property (readonly, nonatomic, nullable) NSDate *modificationTime;

/**
 * @brief Public link creation time of the file to MEGA (in seconds since the epoch).
 *
 * The value is only valid for nodes of type MEGANodeTypeFile.
 *
 */
@property (readonly, nonatomic, nullable) NSDate *publicLinkCreationTime;

/**
 * @brief Handle to identify this MEGANode.
 *
 * You can use [MEGASdk nodeForHandle:] to recover the node later.
 *
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * The handle of the previous parent of this node.
 *
 * This attribute is set when nodes are moved to the Rubbish Bin to
 * ease their restoration. If the attribute is not set for the node,
 * this function returns MegaApi::INVALID_HANDLE
 */
@property (readonly, nonatomic) uint64_t restoreHandle;

/**
 * @brief The handle of the parent node
 *
 * You can use [MEGASdk nodeForHandle:] to recover the node later.
 *
 */
@property (readonly, nonatomic) uint64_t parentHandle;

/**
 * @brief The expiration time of a public link (in seconds since the epoch), if any
 *
 * 0 for non-expire links, and -1 if the MEGANode is not exported.
 */
@property (readonly, nonatomic) int64_t expirationTime;

/**
 * @brief The public handle of an exported node. If the MEGANode
 * has not been exported, it returns UNDEF.
 *
 * Only exported nodes have a public handle.
 *
 */
@property (readonly, nonatomic) uint64_t publicHandle;

/**
 * @brief A public node for the exported node. If the MEGANode has not been
 * exported or it has expired, then it returns nil.
 */
@property (readonly, nonatomic, nullable) MEGANode *publicNode;

/**
 * @brief The URL for the public link of the exported node. If the MEGANode
 * has not been exported, it returns nil.
 */
@property (readonly, nonatomic, nullable) NSString *publicLink;

/**
 * @brief The handle of the owner of the node.
 */
@property (readonly, nonatomic) uint64_t owner;

/**
 * @brief The device id stored as a Node attribute of a Backup folder.
 * It will be an empty string for other nodes.
 */
@property (readonly, nonatomic, nullable) NSString *deviceId;

/**
* @brief The Password Node Data if the node is a Password Node.
*/
@property (readonly, nonatomic, nullable) PasswordNodeData *passwordNodeData;

/**
 * @brief Creates a copy of this MEGANode object.
 *
 * The resulting object is fully independent of the source MEGANode,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGANode object.
 */
- (nullable instancetype)clone;

/**
 * @brief Returns a BOOL value that indicates if the node represents a file (type == MEGANodeTypeFile)
 * @return YES if the node represents a file, otherwise NO.
 */
- (BOOL)isFile;

/**
 * @brief Returns a BOOL value that indicates if the node represents a folder or a root node.
 *
 * @return YES if the node represents a folder or a root node, otherwise NO.
 */
- (BOOL)isFolder;

/**
 * @brief Returns a BOOL value that indicates if the node has been removed from the MEGA account.
 *
 * This value is only useful for nodes notified by [MEGADelegate onNodesUpdate:nodeList:] or
 * [MEGAGlobalDelegate onNodesUpdate:nodeList:] that can notify about deleted nodes.
 *
 * In other cases, the return value of this function will be always NO.
 *
 * @return YES if the node has been removed from the MEGA account, otherwise NO
 */
- (BOOL)isRemoved;

/**
 * @brief Returns YES if this node has an specific change
 *
 * This value is only useful for nodes notified by [MEGADelegate onNodesUpdate:nodeList:] or
 * [MEGAGlobalDelegate onNodesUpdate:nodeList:] that can notify about node modifications.
 *
 * In other cases, the return value of this function will be always NO.
 *
 * @param changeType The type of change to check. It can be one of the following values:
 *
 * - MEGANodeChangeTypeRemoved             = 0x01
 * Check if the node is being removed
 *
 * - MEGANodeChangeTypeAttributes          = 0x02
 * Check if an attribute of the node has changed, usually the namespace name
 *
 * - MEGANodeChangeTypeOwner               = 0x04
 * Check if the owner of the node has changed
 *
 * - MEGANodeChangeTypeTimestamp           = 0x08
 * Check if the modification time of the node has changed
 *
 * - MEGANodeChangeTypeFileAttributes      = 0x10
 * Check if file attributes have changed, usually the thumbnail or the preview for images
 *
 * - MEGANodeChangeTypeInShare             = 0x20
 * Check if the node is a new or modified inshare
 *
 * - MEGANodeChangeTypeOutShare            = 0x40
 * Check if the node is a new or modified outshare
 *
 * - MEGANodeChangeTypeParent              = 0x80
 * Check if the parent of the node has changed
 *
 *  - MEGANodeChangeTypePendingShare       = 0x100
 * Check if the pending share of the node has changed
 *
 * - MEGANodeChangeTypePublicLink          = 0x200
 * Check if the public link of the node has changed
 *
 * - MEGANodeChangeTypeNew                 = 0x400
 * Check if the node is new
 *
 * @return YES if this node has an specific change
 */
- (BOOL)hasChangedType:(MEGANodeChangeType)changeType;

/**
 * @brief Returns a bit field with the changes of the node
 *
 * This value is only useful for nodes notified by [MEGADelegate onNodesUpdate:nodeList:] or
 * [MEGAGlobalDelegate onNodesUpdate:nodeList:] that can notify about node modifications.
 *
 * @return The returned value is an OR combination of these flags:
 *
 *
 * - MEGANodeChangeTypeRemoved             = 0x01
 * The node is being removed
 *
 * - MEGANodeChangeTypeAttributes          = 0x02
 * An attribute of the node has changed, usually the namespace name
 *
 * - MEGANodeChangeTypeOwner               = 0x04
 * The owner of the node has changed
 *
 * - MEGANodeChangeTypeTimestamp           = 0x08
 * The modification time of the node has changed
 *
 * - MEGANodeChangeTypeFileAttributes      = 0x10
 * File attributes have changed, usually the thumbnail or the preview for images
 *
 * - MEGANodeChangeTypeInShare             = 0x20
 * The node is a new or modified inshare
 *
 * - MEGANodeChangeTypeOutShare            = 0x40
 * The node is a new or modified outshare
 *
 * - MEGANodeChangeTypeParent              = 0x80
 * Check if the parent of the node has changed
 *
 *  - MEGANodeChangeTypePendingShare       = 0x100
 * Check if the pending share of the node has changed
 *
 * - MEGANodeChangeTypePublicLink          = 0x200
 * Check if the public link of the node has changed
 *
 * - MEGANodeChangeTypeNew                 = 0x400
 * Check if the node is new
 */
- (MEGANodeChangeType)getChanges;

/**
 * @brief Returns YES if the node has an associated thumbnail
 * @return YES if the node has an associated thumbnail, otherwise NO
 */
- (BOOL)hasThumbnail;

/**
 * @brief Returns YES if the node has an associated preview
 * @return YES if the node has an associated preview, otherwise NO
 */
- (BOOL)hasPreview;

/**
 * @brief Returns YES if this is a public node
 *
 * Only MEGANode objects generated with MegaApi::getPublicMegaNode
 * will return YES
 *
 * @return YES if this is a public node, otherwise NO
 */
- (BOOL)isPublic;

/**
 * @brief Returns a BOOL value that indicates if the node is a shared
 *
 * For nodes that are being shared, you can get a list of MEGAShare
 * objects using [MEGASdk outShares], or a list of MEGANode objects
 * using [MEGASdk inShares]
 *
 * @return YES is the MegaNode is being shared, otherwise NO
 * @note Exported nodes (public link) are not considered to be shared nodes
 */
- (BOOL)isShared;

/**
 * @brief Check if the MEGANode is being shared with other users
 *
 * For nodes that are being shared, you can get a list of MEGAShare
 * objects using [MEGASdk outShares]
 *
 * @return YES is the MEGANode is being shared, otherwise NO
 */
- (BOOL)isOutShare;

/**
 * @brief Check if a MEGANode belong to another MEGAUser, but it is shared with you
 *
 * For nodes that are being shared, you can get a list of MEGANode
 * objects using [MEGASdk inShares]
 *
 * @return YES is the MEGANode is being shared, otherwise NO
 */
- (BOOL)isInShare;

/**
 * @brief Returns YES if the node has been exported (has a public link)
 *
 * Public links are created by calling [MEGASdk exportNode]
 *
 * @return YES if this is an exported node, otherwise NO
 */
- (BOOL)isExported;

/**
 * @brief Returns YES if the node has been exported (has a temporal public link)
 * and the related public link has expired
 *
 * Public links are created by calling [MEGASdk exportNode]
 *
 * @return YES if the public link has expired, otherwise NO
 */
- (BOOL)isExpired;

/**
 * @brief Returns YES if this the node has been exported
 * and the related public link has been taken down
 *
 * Public links are created by calling [MEGASdk exportNode]
 *
 * @return YES if the public link has been taken down, otherwise NO
 */
- (BOOL)isTakenDown;

/**
 * @brief Returns true if this MEGANode is a private node from a foreign account
 *
 * Only MEGANodes created with [MEGASdk createForeignFileNode and MegaApi::createForeignFolderNode
 * returns true in this function.
 *
 * @return YES if this node is a private node from a foreign account
 */
- (BOOL)isForeign;

/**
 * @brief Returns true if the node key is decrypted
 *
 * For nodes in shared folders, there could be missing keys. Also, faulty
 * clients might create invalid keys. In those cases, the node's key might
 * not be decrypted successfully.
 *
 * @return True if the node key is decrypted
 */
- (BOOL)isNodeKeyDecrypted;

/**
 * @brief Returns true if this MegaNode is a Password Node
 *
 * Only MegaNodes created with MegaApi::createPasswordNode return true in this function.
 *
 * @return true if this node is a Password Node
 */
- (BOOL)isPasswordNode;

+ (nullable NSString *)stringForNodeLabel:(MEGANodeLabel)nodeLabel;

@end

NS_ASSUME_NONNULL_END
