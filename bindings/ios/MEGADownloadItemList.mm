/**
 * @file MEGADownloadItemList.mm
 * @brief List of nodes to download in one batch, each with its own options
 *
 * (c) 2026 by Mega Limited, Auckland, New Zealand
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

#import "MEGADownloadItemList.h"
#import "MEGADownloadItemList+init.h"
#import "MEGANode+init.h"
#import "megaapi.h"

using namespace mega;

@interface MEGADownloadItemList ()

@property MegaDownloadItemList *downloadItemList;
@property BOOL cMemoryOwn;

@end

@implementation MEGADownloadItemList

- (instancetype)init {
    self = [super init];

    if (self != nil) {
        _downloadItemList = MegaDownloadItemList::createInstance();
        _cMemoryOwn = YES;
    }

    return self;
}

- (instancetype)initWithMegaDownloadItemList:(MegaDownloadItemList *)downloadItemList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];

    if (self) {
        _downloadItemList = downloadItemList;
        _cMemoryOwn = cMemoryOwn;
    }

    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _downloadItemList;
    }
}

- (MegaDownloadItemList *)getCPtr {
    return self.downloadItemList;
}

- (NSString *)description {
    return [NSString stringWithFormat:@"<%@: size=%ld>", [self class], (long)self.size];
}

- (NSInteger)size {
    return self.downloadItemList ? self.downloadItemList->size() : 0;
}

- (void)addNode:(MEGANode *)node
      localPath:(nullable NSString *)localPath
     customName:(nullable NSString *)customName
        appData:(nullable NSString *)appData
     startFirst:(BOOL)startFirst
       undelete:(BOOL)undelete {
    if (!self.downloadItemList) return;

    self.downloadItemList->add(node.getCPtr,
                               localPath.UTF8String,
                               customName.UTF8String,
                               appData.UTF8String,
                               startFirst,
                               undelete);
}

- (nullable MEGANode *)nodeAtIndex:(NSInteger)index {
    if (!self.downloadItemList) return nil;

    MegaNode *node = self.downloadItemList->getNode((int)index);
    return node ? [[MEGANode alloc] initWithMegaNode:node->copy() cMemoryOwn:YES] : nil;
}

- (nullable NSString *)localPathAtIndex:(NSInteger)index {
    if (!self.downloadItemList) return nil;

    const char *localPath = self.downloadItemList->getLocalPath((int)index);
    return localPath ? [NSString stringWithUTF8String:localPath] : nil;
}

- (nullable NSString *)customNameAtIndex:(NSInteger)index {
    if (!self.downloadItemList) return nil;

    const char *customName = self.downloadItemList->getCustomName((int)index);
    return customName ? [NSString stringWithUTF8String:customName] : nil;
}

- (nullable NSString *)appDataAtIndex:(NSInteger)index {
    if (!self.downloadItemList) return nil;

    const char *appData = self.downloadItemList->getAppData((int)index);
    return appData ? [NSString stringWithUTF8String:appData] : nil;
}

- (BOOL)startFirstAtIndex:(NSInteger)index {
    return self.downloadItemList ? self.downloadItemList->getStartFirst((int)index) : NO;
}

- (BOOL)undeleteAtIndex:(NSInteger)index {
    return self.downloadItemList ? self.downloadItemList->getUndelete((int)index) : NO;
}

@end
