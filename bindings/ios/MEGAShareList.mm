//
//  MEGAShareList.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAShareList.h"
#import "MEGAShare+init.h"

using namespace mega;

@interface MEGAShareList ()

@property MegaShareList *shareList;
@property BOOL cMemoryOwn;

@end

@implementation MEGAShareList

- (instancetype)initWithShareList:(MegaShareList *)shareList cMemoryOwn:(BOOL)cMemoryOwn {
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

- (MegaShareList *)getCPtr {
    return self.shareList;
}

-(MEGAShare *)getShareAtPosition:(NSInteger)position {
    return self.shareList ? [[MEGAShare alloc] initWithMegaShare:self.shareList->get((int)position)->copy() cMemoryOwn:YES] : nil;
}

-(NSNumber *)size {
    return self.shareList ? [[NSNumber alloc] initWithInt:self.shareList->size()] : nil;
}

@end
