//
//  MEGABackgroundMediaUpload+init.h
//  MEGASDK
//
//  Created by Simon Wang on 15/10/18.
//  Copyright © 2018 MEGA. All rights reserved.
//

#import "MEGABackgroundMediaUpload.h"
#import "megaapi.h"

NS_ASSUME_NONNULL_BEGIN

@interface MEGABackgroundMediaUpload (init)

- (instancetype)initWithBackgroundMediaUpload:(mega::MegaBackgroundMediaUpload *)mediaUpload;
- (mega::MegaBackgroundMediaUpload *)getCPtr;

@end

NS_ASSUME_NONNULL_END
