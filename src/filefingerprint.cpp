/**
 * @file filefingerprint.cpp
 * @brief Sparse file fingerprint
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#include "mega/filefingerprint.h"
#include "mega/serialize64.h"
#include "mega/base64.h"
#include "mega/logging.h"
#include "mega/utils.h"

namespace {

constexpr int MAXFULL = 8192;

} // anonymous

namespace mega {

bool operator==(const FileFingerprint& lhs, const FileFingerprint& rhs)
{
    // size differs - cannot be equal
    if (lhs.size != rhs.size)
    {
        return false;
    }

#ifndef __ANDROID__
    // mtime check disabled on Android due to this bug:
    // https://code.google.com/p/android/issues/detail?id=18624

#ifndef WINDOWS_PHONE
    // disabled on Windows Phone too because SetFileTime() isn't available

    // mtime differs - cannot be equal
    if (abs(lhs.mtime-rhs.mtime) > 2)
    {
        return false;
    }
#endif
#endif

    // FileFingerprints not fully available - give it the benefit of the doubt
    if (!lhs.isvalid || !rhs.isvalid)
    {
        return true;
    }

    return !memcmp(lhs.crc, rhs.crc, sizeof lhs.crc);
}

bool FileFingerprint::serialize(string *d)
{
    d->append((const char*)&size, sizeof(size));
    d->append((const char*)&mtime, sizeof(mtime));
    d->append((const char*)crc, sizeof(crc));
    d->append((const char*)&isvalid, sizeof(isvalid));

    return true;
}

FileFingerprint *FileFingerprint::unserialize(string *d)
{
    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(m_off_t) + sizeof(m_time_t) + 4 * sizeof(int32_t) + sizeof(bool) > end)
    {
        LOG_err << "FileFingerprint unserialization failed - serialized string too short";
        return NULL;
    }

    FileFingerprint *fp = new FileFingerprint();

    fp->size = MemAccess::get<m_off_t>(ptr);
    ptr += sizeof(m_off_t);

    fp->mtime = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof(m_time_t);

    memcpy(fp->crc, ptr, sizeof(fp->crc));
    ptr += sizeof(fp->crc);

    fp->isvalid = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    d->erase(0, ptr - d->data());
    return fp;
}

bool FileFingerprint::genfingerprint(FileAccess* fa, bool ignoremtime)
{
    bool changed = false;
    int32_t newcrc[sizeof crc / sizeof *crc], crcval;

    if (mtime != fa->mtime)
    {
        mtime = fa->mtime;
        changed = !ignoremtime;
    }

    if (size != fa->size)
    {
        size = fa->size;
        changed = true;
    }

    if (size <= (m_off_t)sizeof crc)
    {
        // tiny file: read verbatim, NUL pad
        if (!fa->frawread((byte*)newcrc, static_cast<unsigned>(size), 0))
        {
            size = -1;
            return true;
        }

        if (size < (m_off_t)sizeof(crc))
        {
            memset((byte*)newcrc + size, 0, size_t(sizeof(crc) - size));
        }
    }
    else if (size <= MAXFULL)
    {
        // small file: full coverage, four full CRC32s
        HashCRC32 crc32;
        byte buf[MAXFULL];

        if (!fa->frawread(buf, static_cast<unsigned>(size), 0))
        {
            size = -1;
            return true;
        }

        for (unsigned i = 0; i < sizeof crc / sizeof *crc; i++)
        {
            int begin = int(i * size / (sizeof crc / sizeof *crc));
            int end = int((i + 1) * size / (sizeof crc / sizeof *crc));

            crc32.add(buf + begin, end - begin);
            crc32.get((byte*)&crcval);

            newcrc[i] = htonl(crcval);
        }
    }
    else
    {
        // large file: sparse coverage, four sparse CRC32s
        HashCRC32 crc32;
        byte block[4 * sizeof crc];
        const unsigned blocks = MAXFULL / (sizeof block * sizeof crc / sizeof *crc);

        for (unsigned i = 0; i < sizeof crc / sizeof *crc; i++)
        {
            for (unsigned j = 0; j < blocks; j++)
            {
                if (!fa->frawread(block, sizeof block,
                                  (size - sizeof block)
                                  * (i * blocks + j)
                                  / (sizeof crc / sizeof *crc * blocks - 1)))
                {
                    size = -1;
                    return true;
                }

                crc32.add(block, sizeof block);
            }

            crc32.get((byte*)&crcval);
            newcrc[i] = htonl(crcval);
        }
    }

    if (memcmp(crc, newcrc, sizeof crc))
    {
        memcpy(crc, newcrc, sizeof crc);
        changed = true;
    }

    if (!isvalid)
    {
        isvalid = true;
        changed = true;
    }

    return changed;
}

bool FileFingerprint::genfingerprint(InputStreamAccess *is, m_time_t cmtime, bool ignoremtime)
{
    bool changed = false;
    int32_t newcrc[sizeof crc / sizeof *crc], crcval;

    if (mtime != cmtime)
    {
        mtime = cmtime;
        changed = !ignoremtime;
    }

    if (size != is->size())
    {
        size = is->size();
        changed = true;
    }

    if (size < 0)
    {
        size = -1;
        return true;
    }

    if (size <= (m_off_t)sizeof crc)
    {
        // tiny file: read verbatim, NUL pad
        if (!is->read((byte*)newcrc, (unsigned int)size))
        {
            size = -1;
            return true;
        }

        if (size < (m_off_t)sizeof(crc))
        {
            memset((byte*)newcrc + size, 0, size_t(sizeof(crc) - size));
        }
    }
    else if (size <= MAXFULL)
    {
        // small file: full coverage, four full CRC32s
        HashCRC32 crc32;
        byte buf[MAXFULL];

        if (!is->read(buf, int(size)))
        {
            size = -1;
            return true;
        }

        for (unsigned i = 0; i < sizeof crc / sizeof *crc; i++)
        {
            int begin = int(i * size / (sizeof crc / sizeof *crc));
            int end = int((i + 1) * size / (sizeof crc / sizeof *crc));

            crc32.add(buf + begin, end - begin);
            crc32.get((byte*)&crcval);

            newcrc[i] = htonl(crcval);
        }
    }
    else
    {
        // large file: sparse coverage, four sparse CRC32s
        HashCRC32 crc32;
        byte block[4 * sizeof crc];
        const unsigned blocks = MAXFULL / (sizeof block * sizeof crc / sizeof *crc);
        m_off_t current = 0;

        for (unsigned i = 0; i < sizeof crc / sizeof *crc; i++)
        {
            for (unsigned j = 0; j < blocks; j++)
            {
                m_off_t offset = (size - sizeof block)
                        * (i * blocks + j)
                        / (sizeof crc / sizeof *crc * blocks - 1);

                //Seek
                for (m_off_t fullstep = offset - current; fullstep > 0; )  // 500G or more and the step doesn't fit in 32 bits
                {
                    unsigned step = fullstep > UINT_MAX ? UINT_MAX : unsigned(fullstep);
                    if (!is->read(NULL, step))
                    {
                        size = -1;
                        return true;
                    }
                    fullstep -= (uint64_t)step;
                }

                current += (offset - current);

                if (!is->read(block, sizeof block))
                {
                    size = -1;
                    return true;
                }
                current += sizeof block;

                crc32.add(block, sizeof block);
            }

            crc32.get((byte*)&crcval);
            newcrc[i] = htonl(crcval);
        }
    }

    if (memcmp(crc, newcrc, sizeof crc))
    {
        memcpy(crc, newcrc, sizeof crc);
        changed = true;
    }

    if (!isvalid)
    {
        isvalid = true;
        changed = true;
    }

    return changed;
}

// convert this FileFingerprint to string
void FileFingerprint::serializefingerprint(string* d) const
{
    byte buf[sizeof crc + 1 + sizeof mtime];
    int l;

    memcpy(buf, crc, sizeof crc);
    l = Serialize64::serialize(buf + sizeof crc, mtime);

    d->resize((sizeof crc + l) * 4 / 3 + 4);
    d->resize(Base64::btoa(buf, sizeof crc + l, (char*)d->c_str()));
}

// decode and set base64-encoded fingerprint
int FileFingerprint::unserializefingerprint(string* d)
{
    byte buf[sizeof crc + sizeof mtime + 1];
    unsigned l;
    uint64_t t;

    if ((l = Base64::atob(d->c_str(), buf, sizeof buf)) < sizeof crc + 1)
    {
        return 0;
    }

    if (Serialize64::unserialize(buf + sizeof crc, l - sizeof crc, &t) < 0)
    {
        return 0;
    }

    memcpy(crc, buf, sizeof crc);

    mtime = t;

    isvalid = true;

    return 1;
}

size_t FileFingerprint::getHash() const
{
    assert(isvalid);
    size_t value = 0;
    hashCombine(value, size);
    hashCombine(value, mtime);
    for (const auto val : crc)
    {
        hashCombine(value, val);
    }
    return value;
}

FileFingerprint& FileFingerprint::operator=(const FileFingerprint& other)
{
    if (this != &other)
    {
        size = other.size;
        mtime = other.mtime;
        memcpy(crc, other.crc, sizeof(crc));
        isvalid = other.isvalid;
    }
    return *this;
}

bool FileFingerprintCmp::operator()(const FileFingerprint* a, const FileFingerprint* b) const
{
    if (a->size < b->size)
    {
        return true;
    }

    if (a->size > b->size)
    {
        return false;
    }

    if (a->mtime < b->mtime)
    {
        return true;
    }

    if (a->mtime > b->mtime)
    {
        return false;
    }

    return memcmp(a->crc, b->crc, sizeof a->crc) < 0;
}
} // namespace
