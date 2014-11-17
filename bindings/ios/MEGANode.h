//
//  MEGANode.h
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGANodeType) {
    MEGANodeTypeUnknown = -1,
    MEGANodeTypeFile = 0,
    MEGANodeTypeFolder,	
    MEGANodeTypeRoot,
    MEGANodeTypeIncoming,
    MEGANodeTypeRubbish
};

/**
 * @brief Represents a node (file/folder) in the MEGA account
 *
 * It allows to get all data related to a file/folder in MEGA. It can be also used
 * to start SDK requests ([MEGASdk renameNode:newName:], [MEGASdk moveNode:newParent:], etc.)
 *
 * Objects of this class aren't live, they are snapshots of the state of a node
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can inspect the MEGA filesystem and get these objects using
 * [MEGASdk childrenWithParent], [MEGASdk childNodeWithParent:name:] and other MEGASdk functions.
 *
 */
@interface MEGANode : NSObject

/**
 * @brief The type of the node
 *
 * Valid values are:
 * - MEGANodeTypeUnknown = -1,
 * Unknown node type
 *
 * - MEGANodeTypeFile = 0,
 * The MEGANode object represents a file in MEGA
 *
 * - MEGANodeTypeFolder = 1
 * The MEGANode object represents a folder in MEGA
 *
 * - MEGANodeTypeRoot = 2
 * The MEGANode object represents root of the MEGA Cloud Drive
 *
 * - MEGANodeTypeIncoming = 3
 * The MEGANode object represents root of the MEGA Inbox
 *
 * - MEGANodeTypeRubbish = 4
 * The MEGANode object represents root of the MEGA Rubbish Bin
 *
 * @return Type of the node
 */
@property (readonly, nonatomic) MEGANodeType type;

/**
 * @brief The name of the node
 *
 * The name is only valid for nodes of type MEGANodeTypeFile or MEGANodeTypeFolder.
 * For other MEGANode types, the name is undefined.
 *
 * The MEGANode object retains the ownership of the returned string. It will
 * be valid until the MEGANode object is deleted.
 *
 * @return Name of the node
 */
@property (readonly, nonatomic) NSString *name;

/**
 * @brief The handle of this MEGANode in a Base64-encoded string
 *
 * You take the ownership of the returned string.
 *
 * @return Base64-encoded handle of the node
 */
@property (readonly, nonatomic) NSString *base64Handle;

/**
 * @brief The size of the node
 *
 * The returned value is only valid for nodes of type MEGANodeTypeFile.
 *
 * @return Size of the node
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief The creation time of the node in MEGA (in seconds since the epoch)
 *
 * The returned value is only valid for nodes of type MEGANodeTypeFile or MEGANodeTypeFolder.
 *
 * @return Creation time of the node (in seconds since the epoch)
 */
@property (readonly, nonatomic) NSDate *creationTime;

/**
 * @brief Returns the modification time of the file that was uploaded to MEGA (in seconds since the epoch)
 *
 * The returned value is only valid for nodes of type MEGANodeTypeFile.
 *
 * @return Modification time of the file that was uploaded to MEGA (in seconds since the epoch)
 */
@property (readonly, nonatomic) NSDate *modificationTime;

/**
 * @brief A handle to identify this MEGANode
 *
 * You can use [MEGASdk nodeWithHandle] to recover the node later.
 *
 * @return Handle that identifies this MEGANode
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * @brief The tag of the operation that created/modified this node in MEGA
 *
 * Every request and every synchronization has a tag that identifies it.
 * When a request creates or modifies a node, the tag is associated with the node
 * at runtime, this association is lost after a reload of the filesystem or when
 * the SDK is closed.
 *
 * This tag is specially useful to know if a node reported in [MEGADelegate onNodesUpdate] or
 * [MEGAGlobalDelegate onNodesUpdate] was modified by a local operation (tag != 0) or by an
 * external operation, made by another MEGA client (tag == 0).
 *
 * If the node hasn't been created/modified during the current execution, this function returns 0
 *
 * @return The tag associated with the node.
 */
@property (readonly, nonatomic) NSInteger tag;

/**
 * @brief Creates a copy of this MEGANode object.
 *
 * The resulting object is fully independent of the source MEGANode,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object
 *
 * @return Copy of the MEGANode object
 */
- (instancetype)clone;

/**
 * @brief Returns a Boolean value that indicates if the node represents a file (type == MEGANodeTypeFile)
 * @return YES if the node represents a file, otherwise NO
 */
- (BOOL)isFile;

/**
 * @brief Returns a Boolean value that indicates if the node represents a folder or a root node
 *
 * @return YES if the node represents a folder or a root node, otherwise NO
 */
- (BOOL)isFolder;

/**
 * @brief Returns a Boolean value that indicates if the node has been removed from the MEGA account
 *
 * This value is only useful for nodes notified by [MEGADelegate onNodesUpdate] or
 * [MEGAGlobalDelegate onNodesUpdate] that can notify about deleted nodes.
 *
 * In other cases, the return value of this function will be always false.
 *
 * @return YES if the node has been removed from the MEGA account, otherwise NO
 */
- (BOOL)isRemoved;

/**
 * @brief Returns a Boolean value that indicates if the node has an associated thumbnail
 * @return YES if the node has an associated thumbnail, otherwise NO
 */
- (BOOL)hasThumbnail;

/**
 * @brief Returns a Boolean value that indicates if the node has an associated preview
 * @return YES if the node has an associated preview, otherwise NO
 */
- (BOOL)hasPreview;

@end
