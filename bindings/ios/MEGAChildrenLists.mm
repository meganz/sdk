/**
 * @file MEGAChildrenLists.m
 * @brief Provides information about node's children organize
 * them into two list (files and folders)
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

#import "MEGAChildrenLists.h"
#import "MEGAChildrenLists+init.h"
#import "MEGANodeList+init.h"

using namespace mega;

@interface MEGAChildrenLists ()

@property MegaChildrenLists *megaChildrenLists;
@property BOOL cMemoryOwn;

@end

@implementation MEGAChildrenLists

- (instancetype)initWithMegaChildrenLists:(mega::MegaChildrenLists *)megaChildrenLists cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaChildrenLists = megaChildrenLists;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaChildrenLists;
    }
}

- (instancetype)clone {
    return self.megaChildrenLists ? [[MEGAChildrenLists alloc] initWithMegaChildrenLists:self.megaChildrenLists->copy() cMemoryOwn:YES] : nil;
}

- (MegaChildrenLists *)getCPtr {
    return self.megaChildrenLists;
}

- (MEGANodeList *)folderList {
    return self.megaChildrenLists ? [[MEGANodeList alloc] initWithNodeList:self.megaChildrenLists->getFolderList()->copy() cMemoryOwn:YES] : nil;
}

- (MEGANodeList *)fileList {
    return self.megaChildrenLists ? [[MEGANodeList alloc] initWithNodeList:self.megaChildrenLists->getFileList()->copy() cMemoryOwn:YES] : nil;
}

@end
