/**
 * @file mega/filefingerprint.h
 * @brief Sparse file fingerprint
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

#ifndef MEGA_FILEFINGERPRINT_H
#define MEGA_FILEFINGERPRINT_H 1

#include "types.h"
#include "filesystem.h"

namespace mega {
// sparse file fingerprint, including size and mtime
struct MEGA_API FileFingerprint : public Cachable
{
    m_off_t size;
    m_time_t mtime;
    int32_t crc[4];

    static const int MAXFULL = 8192;

    // if true, represents actual file data
    // if false, is constructed from node ctime/key
    bool isvalid;

    bool genfingerprint(FileAccess*, bool = false);
    bool genfingerprint(InputStreamAccess*, m_time_t, bool = false);
    void serializefingerprint(string*) const;
    int unserializefingerprint(string*);

    FileFingerprint& operator=(FileFingerprint&);

    FileFingerprint();

    virtual bool serialize(string*);
    static FileFingerprint* unserialize(string*);
};

// orders transfers by file fingerprints, ordered by size / mtime / sparse CRC
struct MEGA_API FileFingerprintCmp
{
    bool operator()(const FileFingerprint* a, const FileFingerprint* b) const;
};

bool operator==(FileFingerprint&, FileFingerprint&);
} // namespace

#endif
