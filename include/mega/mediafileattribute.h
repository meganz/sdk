/**
 * @file mega/mediafileattribute.h
 * @brief Classes for file attributes fetching
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

#ifndef MEGA_MEDIAFILEATTRIBUTE_H
#define MEGA_MEDIAFILEATTRIBUTE_H 1

#include "types.h"

namespace mega {

void xxteaEncrypt(uint32_t* v, uint32_t vlen, uint32_t key[4]);
void xxteaDecrypt(uint32_t* v, uint32_t vlen, uint32_t key[4]);

struct VideoProperties
{
    bool shortformat;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t playtime;
    uint32_t container;
    uint32_t videocodec;
    uint32_t audiocodec;

    VideoProperties();
    bool operator==(const VideoProperties& o) const;

    // turn the structure into a string suitable for pfa command
    static std::string encodeVideoPropertiesAttributes(VideoProperties vp, uint32_t filekey[4]);

    // extract structure members back out of attributes
    static VideoProperties decodeVideoPropertiesAttributes(const std::string& attrs, uint32_t filekey[4]);

#ifdef USE_MEDIAINFO
    // return true if the filename extension is one that mediainfoLib can process
    static bool isVideoFilenameExt(const std::string& ext);

    // Open the specified local file with mediainfoLib and get its video parameters, and get its encoded attributes.
    static bool extractVideoPropertyFileAttributes(const std::string& localFilename, std::string& mediafileattributes, uint32_t attributekey[4]);
#endif

};


} // namespace

#endif
