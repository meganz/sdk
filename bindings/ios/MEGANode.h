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

typedef NS_ENUM (NSInteger, MEGANodeType) {
    MEGANodeTypeUnknown = -1,
    MEGANodeTypeFile = 0,
    MEGANodeTypeFolder,	
    MEGANodeTypeRoot,
    MEGANodeTypeIncoming,
    MEGANodeTypeRubbish
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
    MEGANodeChangeTypePendingShare   = 0x100
};

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
@property (readonly, nonatomic) NSString *name;

/**
 * @brief Handle of this MEGANode in a Base64-encoded string.
 */
@property (readonly, nonatomic) NSString *base64Handle;

/**
 * @brief Size of the node.
 *
 * The value is only valid for nodes of type MEGANodeTypeFile.
 *
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief Creation time of the node in MEGA (in seconds since the epoch).
 *
 * The value is only valid for nodes of type MEGANodeTypeFile or MEGANodeTypeFolder.
 *
 */
@property (readonly, nonatomic) NSDate *creationTime;

/**
 * @brief Modification time of the file that was uploaded to MEGA (in seconds since the epoch).
 *
 * The value is only valid for nodes of type MEGANodeTypeFile.
 *
 */
@property (readonly, nonatomic) NSDate *modificationTime;

/**
 * @brief Handle to identify this MEGANode.
 *
 * You can use [MEGASdk nodeForHandle:] to recover the node later.
 *
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * @brief The handle of the parent node
 *
 * You can use [MEGASdk nodeForHandle:] to recover the node later.
 *
 */
@property (readonly, nonatomic) uint64_t parentHandle;

/**
 * @brief Tag of the operation that created/modified this node in MEGA.
 *
 * Every request and every transfer has a tag that identifies it.
 * When a request creates or modifies a node, the tag is associated with the node
 * at runtime, this association is lost after a reload of the filesystem or when
 * the SDK is closed.
 *
 * This tag is specially useful to know if a node reported in [MEGADelegate onNodesUpdate:nodeList:] or
 * [MEGAGlobalDelegate onNodesUpdate:nodeList:] was modified by a local operation (tag != 0) or by an
 * external operation, made by another MEGA client (tag == 0).
 *
 * If the node hasn't been created/modified during the current execution, this function returns 0.
 *
 */
@property (readonly, nonatomic) NSInteger tag;

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
@property (readonly, nonatomic) MEGANode *publicNode;

/**
 * @brief The URL for the public link of the exported node. If the MEGANode
 * has not been exported, it returns nil.
 */
@property (readonly, nonatomic) NSString *publicLink;

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
- (instancetype)clone;

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
 * - MEGANodeChangeTypeParent          = 0x80
 * Check if the parent of the node has changed
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
 * - MEGANodeChangeTypeParent               = 0x80
 * The parent of the node has changed
 *
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

@end
