/**
 * @file MEGAInputStream.mm
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

#import "MEGAInputStream.h"

MEGAInputStream::MEGAInputStream(ALAssetRepresentation *assetRepresentation) {
    this->assetRepresentation = assetRepresentation;
    this->data = NULL;
    this->offset = 0;
}

MEGAInputStream::MEGAInputStream(NSData *data) {
    this->data = data;
    this->assetRepresentation = NULL;
    this->offset = 0;
}

int64_t MEGAInputStream::getSize() {
    if (assetRepresentation) {
        return assetRepresentation.size;
    }
    return (int64_t)data.length;
}

bool MEGAInputStream::read(char *buffer, size_t size) {
    if (assetRepresentation && (offset + (long)size) > assetRepresentation.size) {
        return false;
    } else if (data && (offset + (long)size) > data.length) {
        return false;
    }
    
    if (buffer == NULL) {
        offset += (long)size;
        return true;
    }
    
    if (assetRepresentation) {
        int numBytesToRead = (int)size;
        while (numBytesToRead > 0) {
            int n = [assetRepresentation getBytes:(uint8_t *)buffer fromOffset:offset length:numBytesToRead error:nil];
            if (n == 0) {
                return false;
            }
            
            offset += n;
            numBytesToRead -= n;
        }
    } else if (data) {
        memcpy(buffer, (unsigned char *)[data bytes] + offset, size);
        offset += size;
    }
    
    return true;
}
