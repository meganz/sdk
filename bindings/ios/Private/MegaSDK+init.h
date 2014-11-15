//
//  MEGASdk+init.h
//  mega
//
//  Created by Javier Navarro on 04/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGASdk.h"
#import "DelegateMEGARequestListener.h"
#import "DelegateMEGATransferListener.h"

@interface MEGASdk (init)

- (void)freeRequestListener:(DelegateMEGARequestListener *)delegate;
- (void)freeTransferListener:(DelegateMEGATransferListener *)delegate;

@end
