//
//  MRequest.h
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MNode.h"
#import "MAccountDetails.h"

typedef NS_ENUM (NSInteger, MRequestType) {
    MRequestTypeLogin,
    MRequestTypeMkdir,
    MRequestTypeMove,
    MRequestTypeCopy,
    MRequestTypeRename,
    MRequestTypeRemove,
    MRequestTypeShare,
    MRequestTypeFolderAccess,
    MRequestTypeImportLink,
    MRequestTypeImportNode,
    MRequestTypeExport,
    MRequestTypeFetchNodes,
    MRequestTypeAccountDetails,
    MRequestTypeChangePassword,
    MRequestTypeUpload,
    MRequestTypeLogout,
    MRequestTypeFastLogin,
    MRequestTypeGetPublicNode,
    MRequestTypeGetAttrFile,
    MRequestTypeSetAttrFile,
    MRequestTypeGetAttrUser,
    MRequestTypeSetAttrUser,
    MRequestTypeRetryPendingConnections,
    MRequestTypeAddContact,
    MRequestTypeRemoveContact,
    MRequestTypeCreateAccount,
    MRequestTypeFastCreateAccount,
    MRequestTypeConfirmAccount,
    MRequestTypeFastConfirmAccount,
    MRequestTypeQuearySignUpLink,
    MRequestTypeAddSync,
    MRequestTypeRemoveSync,
    MRequestTypeRemoveSyncs,
    MRequestTypePauseTransfer,
    MRequestTypeCancelTransfer,
    MRequestTypeCancelTransfers,
    MRequestTypeDelete
};

typedef NS_ENUM (NSInteger, MNodeAccessLevel) {
    MNodeAccessLevel_ACCESS_UNKNOWN = -1,
    MNodeAccessLevel_RDONLY = 0,            // cannot add, rename or delete
    MNodeAccessLevel_RDWR,                  // cannot rename or delete
    MNodeAccessLevel_FULL,                  // all operations that do not require ownership permitted
    MNodeAccessLevel_OWNER,                 // node is in caller's ROOT, INCOMING or RUBBISH trees
    MNodeAccessLevel_OWNERPRELOGIN
};

@interface MRequest : NSObject

- (instancetype)clone;
- (MRequestType)getType;
- (NSString *)getRequestString;
- (uint64_t)getNodeHandle;
- (NSString *)getLink;
- (uint64_t)getParentHandle;
- (NSString *)getSessionKey;
- (NSString *)getName;
- (NSString *)getEmail;
- (NSString *)getPassword;
- (NSString *)getNewPassword;
- (NSString *)getPrivateKey;
- (MNodeAccessLevel)getAccess;
- (NSString *)getFile;
- (MNode *)getPublicNode;
- (NSInteger)getParamType;
- (BOOL)getFlag;
- (NSNumber *)getTransferredBytes;
- (NSNumber *)getTotalBytes;
- (MAccountDetails *)getMAccountDetails;

@end
