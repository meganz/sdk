/**
 * @file MEGAFileInputStream.mm
 * @brief Implementation of MegaInputStream
 *
 * (c) 2018 by Mega Limited, Auckland, New Zealand
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

#import "MEGAFileInputStream.h"

MEGAFileInputStream::MEGAFileInputStream(NSString *filePath) {
    this->fileHandle = [NSFileHandle fileHandleForReadingAtPath:filePath];
    this->fileSize = [this->fileHandle seekToEndOfFile];
    [this->fileHandle seekToFileOffset:0];
}

int64_t MEGAFileInputStream::getSize() {
    return this->fileSize;
}

bool MEGAFileInputStream::read(char *buffer, size_t size) {
    if (this->fileHandle == nil) {
        return false;
    }
    
    unsigned long long currentOffset = this->fileHandle.offsetInFile;
    if (currentOffset + size > this->fileSize) {
        return false;
    }
    
    if (buffer == NULL) {
        [this->fileHandle seekToFileOffset:currentOffset + size];
        return true;
    }
    
    memcpy(buffer, [this->fileHandle readDataOfLength:size].bytes, size);
    return true;
}
