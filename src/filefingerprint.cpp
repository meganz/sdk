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

#include "mega/filesystem.h"
#include "mega/serialize64.h"
#include "mega/base64.h"
#include "mega/logging.h"
#include "mega/utils.h"

namespace
{
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

    // mtime differs - cannot be equal
    if (abs(lhs.mtime-rhs.mtime) > 2)
    {
        return false;
    }

    // FileFingerprints not fully available - we can't ensure that they are equal
    if (!lhs.isvalid || !rhs.isvalid)
    {
        return false;
    }

    // if (lhs.mac != rhs.mac) {
    //   return false;
    // }

    return !memcmp(lhs.crc.data(), rhs.crc.data(), sizeof lhs.crc);
}

bool operator!=(const FileFingerprint& lhs, const FileFingerprint& rhs)
{
    return !(lhs == rhs);
}


bool FileFingerprint::EqualExceptValidFlag(const FileFingerprint& rhs) const
{
    // same as == but not checking valid
    if (size != rhs.size) return false;
    if (abs(mtime-rhs.mtime) > 2) return false;
    return !memcmp(crc.data(), rhs.crc.data(), sizeof crc);
}

bool FileFingerprint::equalExceptMtime(const FileFingerprint& rhs) const
{
    return (memcmp(this->crc.data(), rhs.crc.data(), sizeof rhs.crc) == 0 &&
            this->size == rhs.size);
}

bool FileFingerprint::serialize(string *d) const
{
    d->append((const char*)&size, sizeof(size));
    d->append((const char*)&mtime, sizeof(mtime));
    d->append((const char*)crc.data(), sizeof(crc));
    d->append((const char*)&isvalid, sizeof(isvalid));

    return true;
}

unique_ptr<FileFingerprint> FileFingerprint::unserialize(const char*& ptr, const char* end)
{
    if (ptr + sizeof(m_off_t) + sizeof(m_time_t) + 4 * sizeof(int32_t) + sizeof(bool) > end)
    {
        LOG_err << "FileFingerprint unserialization failed - serialized string too short";
        return NULL;
    }

    unique_ptr<FileFingerprint> fp(new FileFingerprint());

    fp->size = MemAccess::get<m_off_t>(ptr);
    ptr += sizeof(m_off_t);

    fp->mtime = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof(m_time_t);

    memcpy(fp->crc.data(), ptr, sizeof(fp->crc));
    ptr += sizeof(fp->crc);

    fp->isvalid = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    return fp;
}

FileFingerprint::FileFingerprint(const FileFingerprint& other)
: size{other.size}
, mtime{other.mtime}
, crc(other.crc)
// , mac(other.mac)
, isvalid{other.isvalid}
{}

FileFingerprint& FileFingerprint::operator=(const FileFingerprint& other)
{
    assert(this != &other);
    size = other.size;
    mtime = other.mtime;
    crc = other.crc;
    isvalid = other.isvalid;
    // mac = other.mac;
    return *this;
}

// dummy mac generator
int FileFingerprint::genMAC(const string& content, const string& key) {
  int mac = (int) content[0] + (int) key[0];
  return mac;
}

bool FileFingerprint::genfingerprint(FileAccess* fa, bool ignoremtime)
{
    bool changed = false;
    decltype(crc) newcrc;
    int32_t crcval;

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

    if (!fa->openf(FSLogging::logOnError))
    {
        size = -1;
        return true;
    }

    if (size <= (m_off_t)sizeof crc)
    {
        // tiny file: read verbatim, NUL pad
        if (!fa->frawread((byte*)newcrc.data(), static_cast<unsigned>(size), 0, true, FSLogging::logOnError))
        {
            size = -1;
            fa->closef();
            return true;
        }

        if (size < (m_off_t)sizeof(crc))
        {
            memset((byte*)newcrc.data() + size, 0, sizeof(crc) - static_cast<size_t>(size));
        }
    }
    else if (size <= MAXFULL)
    {
        // small file: full coverage, four full CRC32s
        HashCRC32 crc32;
        byte buf[MAXFULL];

        if (!fa->frawread(buf, static_cast<unsigned>(size), 0, true, FSLogging::logOnError))
        {
            size = -1;
            fa->closef();
            return true;
        }

        for (unsigned i = 0; i < crc.size(); i++)
        {
            int begin = int(i * static_cast<size_t>(size) / crc.size());
            int end = int((i + 1) * static_cast<size_t>(size) / crc.size());

            crc32.add(buf + begin, static_cast<unsigned>(end - begin));
            crc32.get((byte*)&crcval);

            newcrc[i] = static_cast<int32_t>(htonl(static_cast<uint32_t>(crcval)));
        }
    }
    else
    {
        // large file: sparse coverage, four sparse CRC32s
        HashCRC32 crc32;
        byte block[4 * sizeof crc];
        const unsigned blocks = MAXFULL / unsigned(sizeof block * crc.size());

        for (unsigned i = 0; i < crc.size(); i++)
        {
            for (unsigned j = 0; j < blocks; j++)
            {
                if (!fa->frawread(block,
                                  sizeof block,
                                  static_cast<m_off_t>((static_cast<size_t>(size) - sizeof block) *
                                                       (i * blocks + j) /
                                                       (crc.size() * blocks - 1)),
                                  true,
                                  FSLogging::logOnError))
                {
                    size = -1;
                    fa->closef();
                    return true;
                }

                crc32.add(block, sizeof block);
            }

            crc32.get((byte*)&crcval);
            newcrc[i] = static_cast<int32_t>(htonl(static_cast<uint32_t>(crcval)));
        }
    }

    if (crc != newcrc)
    {
        crc = newcrc;
        changed = true;
    }

    if (!isvalid)
    {
        isvalid = true;
        changed = true;
    }

    fa->closef();

    LOG_debug << "[FileFingerprint::genfingerprint] FA debug fp: " << fingerprintDebugString();
    return changed;
}

bool FileFingerprint::genfingerprint(InputStreamAccess *is, m_time_t cmtime, bool ignoremtime)
{
    bool changed = false;
    decltype(crc) newcrc;
    int32_t crcval;

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
        if (!is->read((byte*)newcrc.data(), (unsigned int)size))
        {
            size = -1;
            return true;
        }

        if (size < (m_off_t)sizeof(crc))
        {
            memset((byte*)newcrc.data() + size, 0, sizeof(crc) - static_cast<size_t>(size));
        }
    }
    else if (size <= MAXFULL)
    {
        // small file: full coverage, four full CRC32s
        HashCRC32 crc32;
        byte buf[MAXFULL];

        if (!is->read(buf, static_cast<unsigned>(size)))
        {
            size = -1;
            return true;
        }

        for (unsigned i = 0; i < crc.size(); i++)
        {
            int begin = int(i * static_cast<size_t>(size) / crc.size());
            int end = int((i + 1) * static_cast<size_t>(size) / crc.size());

            crc32.add(buf + begin, static_cast<unsigned>(end - begin));
            crc32.get((byte*)&crcval);

            newcrc[i] = static_cast<int32_t>(htonl(static_cast<uint32_t>(crcval)));
        }
    }
    else
    {
        // large file: sparse coverage, four sparse CRC32s
        HashCRC32 crc32;
        byte block[4 * sizeof crc];
        const unsigned blocks = MAXFULL / unsigned(sizeof block * crc.size());
        m_off_t current = 0;

        for (unsigned i = 0; i < crc.size(); i++)
        {
            for (unsigned j = 0; j < blocks; j++)
            {
                m_off_t offset = static_cast<m_off_t>((static_cast<size_t>(size) - sizeof block) *
                                                      (i * blocks + j) / (crc.size() * blocks - 1));

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
            newcrc[i] = static_cast<int32_t>(htonl(static_cast<uint32_t>(crcval)));
        }
    }

    if (crc != newcrc)
    {
        crc = newcrc;
        changed = true;
    }

    if (!isvalid)
    {
        isvalid = true;
        changed = true;
    }

    LOG_debug << "[FileFingerprint::genfingerprint] IA debug fp: " << fingerprintDebugString();
    return changed;
}

// convert this FileFingerprint to string
void FileFingerprint::serializefingerprint(string* d) const
{
    byte buf[sizeof crc + 1 + sizeof mtime];
    int l;

    memcpy(buf, crc.data(), sizeof crc);
    l = Serialize64::serialize(buf + sizeof crc, static_cast<uint64_t>(mtime));

    d->resize((sizeof crc + static_cast<size_t>(l)) * 4 / 3 + 4);
    d->resize(static_cast<size_t>(
        Base64::btoa(buf,
                     static_cast<int>(sizeof crc + static_cast<unsigned long>(l)),
                     (char*)d->c_str())));
}

// decode and set base64-encoded fingerprint
int FileFingerprint::unserializefingerprint(const string* d)
{
    byte buf[sizeof crc + sizeof mtime + 1];
    unsigned l;
    uint64_t t;

    if ((l = static_cast<unsigned>(Base64::atob(d->c_str(), buf, sizeof buf))) < sizeof crc + 1)
    {
        return 0;
    }

    if (Serialize64::unserialize(buf + sizeof crc, static_cast<int>(l - sizeof crc), &t) < 0)
    {
        return 0;
    }

    memcpy(crc.data(), buf, sizeof crc);

    mtime = static_cast<m_time_t>(t);

    isvalid = true;

    return 1;
}

string FileFingerprint::fingerprintDebugString() const
{
    return std::to_string(size) + ":" + std::to_string(mtime) + ":" + (const char*)Base64Str<sizeof(crc)>((byte*)crc.data()) + (isvalid ? ":1" : ":0");
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

    return memcmp(a->crc.data(), b->crc.data(), sizeof a->crc) < 0;
}

bool FileFingerprintCmp::operator()(const FileFingerprint &a, const FileFingerprint &b) const
{
     return operator()(&a, &b);
}


} // mega
