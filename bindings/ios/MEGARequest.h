//
//  MEGARequest.h
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGANode.h"
#import "MEGAAccountDetails.h"
#import "MEGAPricing.h"

typedef NS_ENUM (NSInteger, MEGARequestType) {
    MEGARequestTypeLogin,
    MEGARequestTypeMkdir,
    MEGARequestTypeMove,
    MEGARequestTypeCopy,
    MEGARequestTypeRename,
    MEGARequestTypeRemove,
    MEGARequestTypeShare,
    MEGARequestTypeFolderAccess,
    MEGARequestTypeImportLink,
    MEGARequestTypeImportNode,
    MEGARequestTypeExport,
    MEGARequestTypeFetchNodes,
    MEGARequestTypeAccountDetails,
    MEGARequestTypeChangePassword,
    MEGARequestTypeUpload,
    MEGARequestTypeLogout,
    MEGARequestTypeFastLogin,
    MEGARequestTypeGetPublicNode,
    MEGARequestTypeGetAttrFile,
    MEGARequestTypeSetAttrFile,
    MEGARequestTypeGetAttrUser,
    MEGARequestTypeSetAttrUser,
    MEGARequestTypeRetryPendingConnections,
    MEGARequestTypeAddContact,
    MEGARequestTypeRemoveContact,
    MEGARequestTypeCreateAccount,
    MEGARequestTypeFastCreateAccount,
    MEGARequestTypeConfirmAccount,
    MEGARequestTypeFastConfirmAccount,
    MEGARequestTypeQuearySignUpLink,
    MEGARequestTypeAddSync,
    MEGARequestTypeRemoveSync,
    MEGARequestTypeRemoveSyncs,
    MEGARequestTypePauseTransfer,
    MEGARequestTypeCancelTransfer,
    MEGARequestTypeCancelTransfers,
    MEGARequestTypeDelete,
    MEGARequestTypeGetPricing,
    MEGARequestTypeGetPaymentURL
};

typedef NS_ENUM (NSInteger, MEGANodeAccessLevel) {
    MEGANodeAccessLevelAccessUnknown = -1,
    MEGANodeAccessLevelRdOnly = 0,            // cannot add, rename or delete
    MEGANodeAccessLevelRdWr,                  // cannot rename or delete
    MEGANodeAccessLevelFull,                  // all operations that do not require ownership permitted
    MEGANodeAccessLevelOwner,                 // node is in caller's ROOT, INCOMING or RUBBISH trees
    MEGANodeAccessLevelOwnerPreLogin
};

@interface MEGARequest : NSObject

- (instancetype)clone;
- (MEGARequestType)getType;
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
- (MEGANodeAccessLevel)getAccess;
- (NSString *)getFile;
- (MEGANode *)getPublicNode;
- (NSInteger)getParamType;
- (BOOL)getFlag;
- (NSNumber *)getTransferredBytes;
- (NSNumber *)getTotalBytes;
- (MEGAAcountDetails *)getMEGAAcountDetails;
- (MEGAPricing *)getPricing;

@end
