//
//  MegaSDK+init.h
//  mega
//
//  Created by Javier Navarro on 04/11/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MegaSDK.h"
#import "../DelegateMRequestListener.h"
#import "../DelegateMTransferListener.h"

@interface MegaSDK (init)

- (void)freeRequestListener:(DelegateMRequestListener *)delegate;
- (void)freeTransferListener:(DelegateMTransferListener *)delegate;

@end
