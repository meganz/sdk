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
#include <string>

namespace mega {

void xxteaEncrypt(uint32_t* v, uint32_t vlen, uint32_t key[4]);
void xxteaDecrypt(uint32_t* v, uint32_t vlen, uint32_t key[4]);

struct MediaFileInfo;

struct MediaProperties
{
    byte shortformat;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t playtime;
    std::string containerName;
    std::string videocodecName;
    std::string audiocodecName;
    uint32_t containerid;
    uint32_t videocodecid;
    uint32_t audiocodecid;

    bool is_VFR;
    bool no_audio;


    MediaProperties();
    bool operator==(const MediaProperties& o) const;

    // turn the structure into a string suitable for pfa command
    static std::string encodeMediaPropertiesAttributes(MediaProperties vp, uint32_t filekey[4]);

    // extract structure members back out of attributes
    static MediaProperties decodeMediaPropertiesAttributes(const std::string& attrs, uint32_t filekey[4]);

#ifdef USE_MEDIAINFO
    // return true if the filename extension is one that mediainfoLib can process
    static bool isMediaFilenameExt(const std::string& ext);

    // Open the specified local file with mediainfoLib and get its video parameters.  This function fills in the names but not the IDs
    void extractMediaPropertyFileAttributes(const std::string& localFilename);

    // Look up the IDs of the codecs and container, and encode and encrypt all the info into a string with file attribute 8, and possibly file attribute 9.
    std::string convertMediaPropertyFileAttributes(uint32_t attributekey[4], MediaFileInfo& mediaInfo);

#endif
};


struct MediaFileInfo
{
    struct MediaCodecs
    {
        struct idrecord
        {
            unsigned id;
            std::string mediainfoname;
            std::string mediasourcemimetype;
        };

        struct shortformatrec
        {
            byte shortformatid;
            unsigned containerid;
            unsigned videocodecid;
            unsigned audiocodecid;
        };

        std::map<std::string, idrecord> containers;
        std::map<std::string, idrecord> videocodecs;
        std::map<std::string, idrecord> audiocodecs;
        std::vector<shortformatrec> shortformats;
    };

    // a set of codec <-> id mappings supplied by Mega
    bool mediaCodecsRequested;
    bool mediaCodecsReceived;
    uint32_t downloadedCodecMapsVersion;
    MediaCodecs mediaCodecs;

    // look up IDs from the various maps
    unsigned Lookup(const std::string& name, std::map<std::string, MediaFileInfo::MediaCodecs::idrecord>& data, unsigned notfoundvalue);
    byte LookupShortFormat(unsigned containerid, unsigned videocodecid, unsigned audiocodecid);

    
    // In case we don't have the MediaCodecs yet, remember the media attributes until we can add them to the file.
    struct queuedvp;
    std::vector< queuedvp > queuedForDownloadTranslation;
    std::map<handle, queuedvp> uploadFileAttributes;

    // request MediaCodecs from Mega.  Only do this the first time we know we will need them.
    void requestCodecMappingsOneTime(MegaClient* client, string* ifSuitableFilename);
    static void onCodecMappingsReceiptStatic(MegaClient* client);
    void onCodecMappingsReceipt(MegaClient* client);
    void ReadIdRecords(std::map<std::string, MediaCodecs::idrecord>&  data, MegaClient* client);

    // get the cached media attributes for a file just before sending CommandPutNodes (for a newly uploaded file)
    void addUploadMediaFileAttributes(handle& fh, std::string* s);

    // we figured out the properties, now attach them to a file.  Queues the action if we don't have the MediaCodecs yet.  Works for uploaded or downloaded files.
    void sendOrQueueMediaPropertiesFileAttributes(handle fh, MediaProperties& vp, uint32_t fakey[4], MegaClient* client, handle* uploadTransferHandle);

    // Check if we should retry video property extraction, due to previous failure with older library
    bool timeToRetryMediaPropertyExtraction(const std::string& fileattributes, uint32_t fakey[4]);

    MediaFileInfo();
};

struct MediaFileInfo::queuedvp
{
    handle filehandle;
    handle transferhandle;
    MediaProperties vp;
    uint32_t fakey[4];
};



} // namespace

#endif
