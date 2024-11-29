/**
 * @file MEGABackupInfoList.mm
 * @brief List of MEGATransfer objects
 *
 * (c) 2023 by Mega Limited, Auckland, New Zealand
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
#import "MEGABackupInfoList.h"
#import "MEGABackupInfo+init.h"

using namespace mega;

@interface MEGABackupInfoList ()

@property MegaBackupInfoList *backupInfoList;
@property BOOL cMemoryOwn;

@end

@implementation MEGABackupInfoList

- (instancetype)initWithBackupInfoList:(MegaBackupInfoList *)backupInfoList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _backupInfoList = backupInfoList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _backupInfoList;
    }
}

- (MegaBackupInfoList *)getCPtr {
    return self.backupInfoList;
}

- (MEGABackupInfo *)backupInfoAtIndex:(NSInteger)index {
    return self.backupInfoList ? [[MEGABackupInfo alloc] initWithMegaBackupInfo:self.backupInfoList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSUInteger)size {
    return self.backupInfoList ? self.backupInfoList->size() : 0;
}

@end
