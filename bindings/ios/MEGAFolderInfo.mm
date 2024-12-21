/**
 * @file MEGAFolderInfo.mm
 * @brief Folder info
 *
 * (c) 2018 - by Mega Limited, Auckland, New Zealand
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

#import "MEGAFolderInfo.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAFolderInfo()

@property MegaFolderInfo *megaFolderInfo;
@property BOOL cMemoryOwn;

@end

@implementation MEGAFolderInfo

- (instancetype)initWithMegaFolderInfo:(MegaFolderInfo *)megaFolderInfo cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaFolderInfo = megaFolderInfo;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaFolderInfo;
    }
}

- (MegaFolderInfo *)getCPtr {
    return self.megaFolderInfo;
}

- (NSInteger)versions {
    return self.megaFolderInfo ? self.megaFolderInfo->getNumVersions() : 0;
}

- (NSInteger)files {
    return self.megaFolderInfo ? self.megaFolderInfo->getNumFiles() : 0;
}

- (NSInteger)folders {
    return self.megaFolderInfo ? self.megaFolderInfo->getNumFolders() : 0;
}

- (long long)currentSize {
    return self.megaFolderInfo ? self.megaFolderInfo->getCurrentSize() : 0;
}

- (long long)versionsSize {
    return self.megaFolderInfo ? self.megaFolderInfo->getVersionsSize() : 0;
}

@end
