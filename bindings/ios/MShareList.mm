//
//  MShareList.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MShareList.h"
#import "MShare+init.h"

using namespace mega;

@interface MShareList ()

@property ShareList *shareList;
@property BOOL cMemoryOwn;

@end

@implementation MShareList

- (instancetype)initWithShareList:(ShareList *)shareList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _shareList = shareList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _shareList;
    }
}

- (ShareList *)getCPtr {
    return self.shareList;
}

-(MShare *)getShareAtPosition:(NSInteger)position {
    return self.shareList ? [[MShare alloc] initWithMegaShare:self.shareList->get((int)position)->copy() cMemoryOwn:YES] : nil;
}

-(NSNumber *)size {
    return self.shareList ? [[NSNumber alloc] initWithInt:self.shareList->size()] : nil;
}

@end
