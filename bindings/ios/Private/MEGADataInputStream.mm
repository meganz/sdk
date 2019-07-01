/**
 * @file MEGADataInputStream.mm
 * @brief Implementation of MegaInputStream
 *
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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

#import "MEGADataInputStream.h"

MEGADataInputStream::MEGADataInputStream(NSData *data) {
    this->data = data;
    this->offset = 0;
}

int64_t MEGADataInputStream::getSize() {
    return (int64_t)data.length;
}

bool MEGADataInputStream::read(char *buffer, size_t size) {
    if (offset + (long)size > data.length) {
        return false;
    }
    
    if (buffer == NULL) {
        offset += (long)size;
        return true;
    }
    
    memcpy(buffer, (unsigned char *)[data bytes] + offset, size);
    offset += size;
    
    return true;
}
