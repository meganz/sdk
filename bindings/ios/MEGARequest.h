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
    MEGARequestTypeCreateFolder,
    MEGARequestTypeMove,
    MEGARequestTypeCopy,
    MEGARequestTypeRename,
    MEGARequestTypeRemove,
    MEGARequestTypeShare,
    MEGARequestTypeImportLink,
    MEGARequestTypeExport,
    MEGARequestTypeFetchNodes,
    MEGARequestTypeAccountDetails,
    MEGARequestTypeChangePassword,
    MEGARequestTypeUpload,
    MEGARequestTypeLogout,
    MEGARequestTypeGetPublicNode,
    MEGARequestTypeGetAttrFile,
    MEGARequestTypeSetAttrFile,
    MEGARequestTypeGetAttrUser,
    MEGARequestTypeSetAttrUser,
    MEGARequestTypeRetryPendingConnections,
    MEGARequestTypeAddContact,
    MEGARequestTypeRemoveContact,
    MEGARequestTypeCreateAccount,
    MEGARequestTypeConfirmAccount,
    MEGARequestTypeQuerySignUpLink,
    MEGARequestTypeAddSync,
    MEGARequestTypeRemoveSync,
    MEGARequestTypeRemoveSyncs,
    MEGARequestTypePauseTransfers,
    MEGARequestTypeCancelTransfer,
    MEGARequestTypeCancelTransfers,
    MEGARequestTypeDelete,
    MEGARequestTypeReportEvent,
    MEGARequestTypeCancelAttrFile,
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

@property (readonly) MEGARequestType type;
@property (readonly) NSString *requestString;
@property (readonly) uint64_t nodeHandle;
@property (readonly) NSString *link;
@property (readonly) uint64_t parentHandle;
@property (readonly) NSString *sessionKey;
@property (readonly) NSString *name;
@property (readonly) NSString *email;
@property (readonly) NSString *password;
@property (readonly) NSString *newPassword;
@property (readonly) NSString *privateKey;
@property (readonly) MEGANodeAccessLevel accessLevel;
@property (readonly) NSString *file;
@property (readonly) MEGANode *publicNode;
@property (readonly) NSInteger paramType;
@property (readonly) BOOL flag;
@property (readonly) NSNumber *transferredBytes;
@property (readonly) NSNumber *totalBytes;
@property (readonly) MEGAAcountDetails *megaAcountDetails;
@property (readonly) MEGAPricing *pricing;

- (instancetype)clone;

@end
