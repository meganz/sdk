/**
 * @file utils.cpp
 * @brief Mega SDK various utilities and helper classes
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

#include "mega/utils.h"
#include "mega/logging.h"
#include "mega/megaclient.h"
#include "mega/base64.h"
#include "mega/serialize64.h"
#include "mega/filesystem.h"

#include <iomanip>
#include <cctype>

#if defined(_WIN32) && defined(_MSC_VER)
#include <sys/timeb.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#ifdef WIN32
#include <direct.h>
#else
#include <sys/time.h>
#include <sys/resource.h>
#endif // ! WIN32

namespace mega {

std::atomic<uint32_t> CancelToken::tokensCancelledCount{0};

string toNodeHandle(handle nodeHandle)
{
    char base64Handle[12];
    Base64::btoa((byte*)&(nodeHandle), MegaClient::NODEHANDLE, base64Handle);
    return string(base64Handle);
}

string toNodeHandle(NodeHandle nodeHandle)
{
    return toNodeHandle(nodeHandle.as8byte());
}

NodeHandle toNodeHandle(const byte* data)
{
    NodeHandle ret;
    if (data)
    {
        handle h = 0;  // most significant non-used-for-the-handle bytes must be zeroed
        memcpy(&h, data, MegaClient::NODEHANDLE);
        ret.set6byte(h);
    }

    return ret;
}

NodeHandle toNodeHandle(const std::string* data)
{
    if(data) return toNodeHandle(reinterpret_cast<const byte*>(data->c_str()));

    return NodeHandle{};
}

string toHandle(handle h)
{
    char base64Handle[14];
    Base64::btoa((byte*)&(h), sizeof h, base64Handle);
    return string(base64Handle);
}

std::pair<bool, TypeOfLink> toTypeOfLink(nodetype_t type)
{
    bool error = false;
    TypeOfLink lType = TypeOfLink::FOLDER;
    switch(type)
    {
    case FOLDERNODE: break;
    case FILENODE:
        lType = TypeOfLink::FILE;
        break;
    default:
        error = true;
        break;
    }

    return std::make_pair(error, lType);
}

std::ostream& operator<<(std::ostream& s, NodeHandle h)
{
    return s << toNodeHandle(h);
}

SimpleLogger& operator<<(SimpleLogger& s, NodeHandle h)
{
    return s << toNodeHandle(h);
}

SimpleLogger& operator<<(SimpleLogger& s, UploadHandle h)
{
    return s << toHandle(h.h);
}

SimpleLogger& operator<<(SimpleLogger& s, NodeOrUploadHandle h)
{
    if (h.isNodeHandle())
    {
        return s << "nh:" << h.nodeHandle();
    }
    else
    {
        return s << "uh:" << h.uploadHandle();
    }
}

SimpleLogger& operator<<(SimpleLogger& s, const LocalPath& lp)
{
    // when logging, do not normalize the string, or we can't diagnose failures to match differently encoded utf8 strings
    return s << lp.toPath(false);
}


string backupTypeToStr(BackupType type)
{
    switch (type)
    {
    case BackupType::INVALID:
            return "INVALID";
    case BackupType::TWO_WAY:
            return "TWO_WAY";
    case BackupType::UP_SYNC:
            return "UP_SYNC";
    case BackupType::DOWN_SYNC:
            return "DOWN_SYNC";
    case BackupType::CAMERA_UPLOAD:
            return "CAMERA_UPLOAD";
    case BackupType::MEDIA_UPLOAD:
            return "MEDIA_UPLOAD";
    case BackupType::BACKUP_UPLOAD:
            return "BACKUP_UPLOAD";
    }

    return "UNKNOWN";
}

void AddHiddenFileAttribute(mega::LocalPath& path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path.localpath.data(), GetFileExInfoStandard, &fad))
        SetFileAttributesW(path.localpath.data(), fad.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
#endif
}

void RemoveHiddenFileAttribute(mega::LocalPath& path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path.localpath.data(), GetFileExInfoStandard, &fad))
        SetFileAttributesW(path.localpath.data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
#endif
}


CacheableWriter::CacheableWriter(string& d)
    : dest(d)
{
}

void CacheableWriter::serializebinary(byte* data, size_t len)
{
    dest.append((char*)data, len);
}

void CacheableWriter::serializechunkmacs(const chunkmac_map& m)
{
    m.serialize(dest);
}

void CacheableWriter::serializecstr(const char* field, bool storeNull)
{
    unsigned short ll = (unsigned short)(field ? strlen(field) + (storeNull ? 1 : 0) : 0);
    dest.append((char*)&ll, sizeof(ll));
    dest.append(field, ll);
}

void CacheableWriter::serializepstr(const string* field)
{
    unsigned short ll = (unsigned short)(field ? field->size() : 0);
    dest.append((char*)&ll, sizeof(ll));
    if (field) dest.append(field->data(), ll);
}

void CacheableWriter::serializestring(const string& field)
{
    unsigned short ll = (unsigned short)field.size();
    dest.append((char*)&ll, sizeof(ll));
    dest.append(field.data(), ll);
}

void CacheableWriter::serializestring_u32(const string& field)
{
    uint32_t ll = (uint32_t)field.size();
    dest.append((char*)&ll, sizeof(ll));
    dest.append(field.data(), ll);
}

void CacheableWriter::serializecompressedu64(uint64_t field)
{
    byte buf[sizeof field+1];
    dest.append((const char*)buf, Serialize64::serialize(buf, field));
}

void CacheableWriter::serializei8(int8_t field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializei32(int32_t field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializei64(int64_t field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializeu64(uint64_t field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializeu32(uint32_t field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializeu16(uint16_t field)
{
    dest.append((char*)&field, sizeof(field));
}
void CacheableWriter::serializeu8(uint8_t field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializehandle(handle field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializenodehandle(handle field)
{
    dest.append((const char*)&field, MegaClient::NODEHANDLE);
}

void CacheableWriter::serializeNodeHandle(NodeHandle field)
{
    serializenodehandle(field.as8byte());
}

void CacheableWriter::serializebool(bool field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializebyte(byte field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializedouble(double field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializeexpansionflags(bool b0, bool b1, bool b2, bool b3, bool b4, bool b5, bool b6, bool b7)
{
    unsigned char b[8];
    b[0] = b0;
    b[1] = b1;
    b[2] = b2;
    b[3] = b3;
    b[4] = b4;
    b[5] = b5;
    b[6] = b6;
    b[7] = b7;
    dest.append((char*)b, 8);
}


CacheableReader::CacheableReader(const string& d)
    : ptr(d.data())
    , end(ptr + d.size())
    , fieldnum(0)
{
}

void CacheableReader::eraseused(string& d)
{
    assert(end == d.data() + d.size());
    d.erase(0, ptr - d.data());
}

bool CacheableReader::unserializecstr(string& s, bool removeNull)
{
    if (ptr + sizeof(unsigned short) > end)
    {
        return false;
    }

    unsigned short len = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(len);

    if (ptr + len > end)
    {
        return false;
    }

    if (len)
    {
        s.assign(ptr, len - (removeNull ? 1 : 0));
    }
    ptr += len;
    fieldnum += 1;
    return true;
}


bool CacheableReader::unserializestring(string& s)
{
    if (ptr + sizeof(unsigned short) > end)
    {
        return false;
    }

    unsigned short len = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(len);

    if (ptr + len > end)
    {
        return false;
    }

    if (len)
    {
        s.assign(ptr, len);
    }
    ptr += len;
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializestring_u32(string& s)
{
    if (ptr + sizeof(uint32_t) > end)
    {
        return false;
    }

    uint32_t len = MemAccess::get<uint32_t>(ptr);
    ptr += sizeof(len);

    if (ptr + len > end)
    {
        return false;
    }

    if (len)
    {
        s.assign(ptr, len);
    }
    ptr += len;
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializebinary(byte* data, size_t len)
{
    if (ptr + len > end)
    {
        return false;
    }

    memcpy(data, ptr, len);
    ptr += len;
    fieldnum += 1;
    return true;
}


void chunkmac_map::serialize(string& d) const
{
    unsigned short ll = (unsigned short)size();
    d.append((char*)&ll, sizeof(ll));
    for (auto& it : mMacMap)
    {
        d.append((char*)&it.first, sizeof(it.first));
        d.append((char*)&it.second, sizeof(it.second));
    }
}

bool chunkmac_map::unserialize(const char*& ptr, const char* end)
{
    unsigned short ll;
    if ((ptr + sizeof(ll) > end) || ptr + (ll = MemAccess::get<unsigned short>(ptr)) * (sizeof(m_off_t) + sizeof(ChunkMAC)) + sizeof(ll) > end)
    {
        return false;
    }

    ptr += sizeof(ll);

    for (int i = 0; i < ll; i++)
    {
        m_off_t pos = MemAccess::get<m_off_t>(ptr);
        ptr += sizeof(m_off_t);

        memcpy(&(mMacMap[pos]), ptr, sizeof(ChunkMAC));
        ptr += sizeof(ChunkMAC);

        if (mMacMap[pos].isMacsmacSoFar())
        {
            macsmacSoFarPos = pos;
            assert(i == 0);
        }
        else
        {
            assert(pos > macsmacSoFarPos);
        }
    }
    return true;
}

void chunkmac_map::calcprogress(m_off_t size, m_off_t& chunkpos, m_off_t& progresscompleted, m_off_t* sumOfPartialChunks)
{
    chunkpos = 0;
    progresscompleted = 0;

    for (auto& it : mMacMap)
    {
        m_off_t chunkceil = ChunkedHash::chunkceil(it.first, size);

        if (it.second.isMacsmacSoFar())
        {
            assert(chunkpos == 0);
            macsmacSoFarPos = it.first;

            chunkpos = chunkceil;
            progresscompleted = chunkceil;
        }
        else if (chunkpos == it.first && it.second.finished)
        {
            chunkpos = chunkceil;
            progresscompleted = chunkceil;
        }
        else if (it.second.finished)
        {
            m_off_t chunksize = chunkceil - ChunkedHash::chunkfloor(it.first);
            progresscompleted += chunksize;
        }
        else
        {
            progresscompleted += it.second.offset;  // sum of completed portions
            if (sumOfPartialChunks)
            {
                *sumOfPartialChunks += it.second.offset;
            }
        }
    }

    progresscontiguous = chunkpos;
}

m_off_t chunkmac_map::nextUnprocessedPosFrom(m_off_t pos)
{
    assert(pos > macsmacSoFarPos);

    for (auto it = mMacMap.find(ChunkedHash::chunkfloor(pos));
        it != mMacMap.end();
        it = mMacMap.find(ChunkedHash::chunkfloor(pos)))
    {
        if (it->second.finished)
        {
            pos = ChunkedHash::chunkceil(pos);
        }
        else
        {
            pos += it->second.offset;
            break;
        }
    }
    return pos;
}

m_off_t chunkmac_map::expandUnprocessedPiece(m_off_t pos, m_off_t npos, m_off_t fileSize, m_off_t maxReqSize)
{
    assert(pos > macsmacSoFarPos);

    for (auto it = mMacMap.find(npos);
        npos < fileSize &&
        (npos - pos) < maxReqSize &&
        (it == mMacMap.end() || it->second.notStarted());
        it = mMacMap.find(npos))
    {
        npos = ChunkedHash::chunkceil(npos, fileSize);
    }
    return npos;
}

m_off_t chunkmac_map::hasUnfinishedGap(m_off_t fileSize)
{
    bool sawUnfinished = false;

    for (auto it = mMacMap.begin();
        it != mMacMap.end(); )
    {
        if (!it->second.finished)
        {
            sawUnfinished = true;
        }

        auto nextpos = ChunkedHash::chunkceil(it->first, fileSize);
        auto expected_it = mMacMap.find(nextpos);

        if (sawUnfinished && expected_it != mMacMap.end() && expected_it->second.finished)
        {
            return true;
        }

        ++it;
        if (it != expected_it)
        {
            sawUnfinished = true;
        }
    }
    return false;
}


void chunkmac_map::ctr_encrypt(m_off_t chunkid, SymmCipher *cipher, byte *chunkstart, unsigned chunksize, m_off_t startpos, int64_t ctriv, bool finishesChunk)
{
    assert(chunkid == startpos);
    assert(startpos > macsmacSoFarPos);

    // encrypt is always done on whole chunks
    auto& chunk = mMacMap[chunkid];
    cipher->ctr_crypt(chunkstart, unsigned(chunksize), startpos, ctriv, chunk.mac, true, true);
    chunk.offset = 0;
    chunk.finished = finishesChunk;  // when encrypting for uploads, only set finished after confirmation of the chunk uploading.
}


void chunkmac_map::ctr_decrypt(m_off_t chunkid, SymmCipher *cipher, byte *chunkstart, unsigned chunksize, m_off_t startpos, int64_t ctriv, bool finishesChunk)
{
    assert(chunkid > macsmacSoFarPos);
    assert(startpos >= chunkid);
    assert(startpos + chunksize <= ChunkedHash::chunkceil(chunkid));
    ChunkMAC& chunk = mMacMap[chunkid];

    cipher->ctr_crypt(chunkstart, chunksize, startpos, ctriv, chunk.mac, false, chunk.notStarted());

    if (finishesChunk)
    {
        chunk.finished = true;
        chunk.offset = 0;
    }
    else
    {
        assert(startpos + chunksize < ChunkedHash::chunkceil(chunkid));
        chunk.finished = false;
        chunk.offset += chunksize;
    }
}

void chunkmac_map::finishedUploadChunks(chunkmac_map& macs)
{
    for (auto& m : macs.mMacMap)
    {
        assert(m.first > macsmacSoFarPos);
        assert(mMacMap.find(m.first) == mMacMap.end() || !mMacMap[m.first].isMacsmacSoFar());

        m.second.finished = true;
        mMacMap[m.first] = m.second;
        LOG_verbose << "Upload chunk completed: " << m.first;
    }
}

bool chunkmac_map::finishedAt(m_off_t pos)
{
    assert(pos > macsmacSoFarPos);

    auto pcit = mMacMap.find(pos);
    return pcit != mMacMap.end()
        && pcit->second.finished;
}

m_off_t chunkmac_map::updateContiguousProgress(m_off_t fileSize)
{
    assert(progresscontiguous > macsmacSoFarPos);

    while (finishedAt(progresscontiguous))
    {
        progresscontiguous = ChunkedHash::chunkceil(progresscontiguous, fileSize);
    }
    return progresscontiguous;
}

void chunkmac_map::updateMacsmacProgress(SymmCipher *cipher)
{
    bool updated = false;
    while (macsmacSoFarPos + 1024 * 1024 * 5 < progresscontiguous  // never go past contiguous-from-start section
           && size() > 32 * 3 + 5)   // leave enough room for the mac-with-late-gaps corrective calculation to occur
    {
        if (mMacMap.begin()->second.isMacsmacSoFar())
        {
            auto it = mMacMap.begin();
            auto& calcSoFar = it->second;
            auto& next = (++it)->second;

            assert(it->first == ChunkedHash::chunkfloor(it->first));
            SymmCipher::xorblock(next.mac, calcSoFar.mac);
            cipher->ecb_encrypt(calcSoFar.mac);
            memcpy(next.mac, calcSoFar.mac, sizeof(next.mac));

            macsmacSoFarPos = it->first;
            next.offset = unsigned(-1);
            assert(next.isMacsmacSoFar());
            mMacMap.erase(mMacMap.begin());
        }
        else if (mMacMap.begin()->first == 0 && finishedAt(0))
        {
            auto& first = mMacMap.begin()->second;

            byte mac[SymmCipher::BLOCKSIZE] = { 0 };
            SymmCipher::xorblock(first.mac, mac);
            cipher->ecb_encrypt(mac);
            memcpy(first.mac, mac, sizeof(mac));

            first.offset = unsigned(-1);
            assert(first.isMacsmacSoFar());
            macsmacSoFarPos = 0;
        }
        updated = true;
    }

    if (updated)
    {
        LOG_verbose << "Macsmac calculation advanced to " << mMacMap.begin()->first;
    }
}

void chunkmac_map::copyEntriesTo(chunkmac_map& other)
{
    for (auto& e : mMacMap)
    {
        assert(e.first > macsmacSoFarPos);
        other.mMacMap[e.first] = e.second;
    }
}

void chunkmac_map::copyEntryTo(m_off_t pos, chunkmac_map& other)
{
    assert(pos > macsmacSoFarPos);
    mMacMap[pos] = other.mMacMap[pos];
}

void chunkmac_map::debugLogOuputMacs()
{
    for (auto& it : mMacMap)
    {
        LOG_debug << "macs: " << it.first << " " << Base64Str<SymmCipher::BLOCKSIZE>(it.second.mac) << " " << it.second.finished;
    }
}

// coalesce block macs into file mac
int64_t chunkmac_map::macsmac(SymmCipher *cipher)
{
    byte mac[SymmCipher::BLOCKSIZE] = { 0 };

    for (auto& it : mMacMap)
    {
        if (it.second.isMacsmacSoFar())
        {
            assert(it.first == mMacMap.begin()->first);
            memcpy(mac, it.second.mac, sizeof(mac));
        }
        else
        {
            assert(it.first == ChunkedHash::chunkfloor(it.first));
            SymmCipher::xorblock(it.second.mac, mac);
            cipher->ecb_encrypt(mac);
        }
    }

    uint32_t* m = (uint32_t*)mac;

    m[0] ^= m[1];
    m[1] = m[2] ^ m[3];

    return MemAccess::get<int64_t>((const char*)mac);
}

int64_t chunkmac_map::macsmac_gaps(SymmCipher *cipher, size_t g1, size_t g2, size_t g3, size_t g4)
{
    byte mac[SymmCipher::BLOCKSIZE] = { 0 };

    size_t n = 0;
    for (auto it = mMacMap.begin(); it != mMacMap.end(); it++, n++)
    {
        if (it->second.isMacsmacSoFar())
        {
            memcpy(mac, it->second.mac, sizeof(mac));
            for (m_off_t pos = 0; pos <= it->first; pos = ChunkedHash::chunkceil(pos))
            {
                ++n;
            }
        }
        else
        {
            if ((n >= g1 && n < g2) || (n >= g3 && n < g4)) continue;

            assert(it->first == ChunkedHash::chunkfloor(it->first));
            SymmCipher::xorblock(it->second.mac, mac);
            cipher->ecb_encrypt(mac);
        }
    }

    uint32_t* m = (uint32_t*)mac;

    m[0] ^= m[1];
    m[1] = m[2] ^ m[3];

    return MemAccess::get<int64_t>((const char*)mac);
}

bool CacheableReader::unserializechunkmacs(chunkmac_map& m)
{
    if (m.unserialize(ptr, end))   // ptr is adjusted by reference
    {
        fieldnum += 1;
        return true;
    }
    return false;
}

bool CacheableReader::unserializefingerprint(FileFingerprint& fp)
{
    if (auto newfp = fp.unserialize(ptr, end))   // ptr is adjusted by reference
    {
        fp = *newfp;
        fieldnum += 1;
        return true;
    }
    return false;
}

bool CacheableReader::unserializecompressedu64(uint64_t& field)
{
    int fieldSize;
    if ((fieldSize = Serialize64::unserialize((byte*)ptr, static_cast<int>(end - ptr), &field)) < 0)
    {
        LOG_err << "Serialize64 unserialization failed - malformed field";
        return false;
    }
    else
    {
        ptr += fieldSize;
    }
    return true;
}

bool CacheableReader::unserializei8(int8_t& field)
{
    if (ptr + sizeof(int8_t) > end)
    {
        return false;
    }
    field = MemAccess::get<int8_t>(ptr);
    ptr += sizeof(int8_t);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializei32(int32_t& field)
{
    if (ptr + sizeof(int32_t) > end)
    {
        return false;
    }
    field = MemAccess::get<int32_t>(ptr);
    ptr += sizeof(int32_t);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializei64(int64_t& field)
{
    if (ptr + sizeof(int64_t) > end)
    {
        return false;
    }
    field = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializeu16(uint16_t &field)
{
    if (ptr + sizeof(uint16_t) > end)
    {
        return false;
    }
    field = MemAccess::get<uint16_t>(ptr);
    ptr += sizeof(uint16_t);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializeu32(uint32_t& field)
{
    if (ptr + sizeof(uint32_t) > end)
    {
        return false;
    }
    field = MemAccess::get<uint32_t>(ptr);
    ptr += sizeof(uint32_t);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializeu8(uint8_t& field)
{
    if (ptr + sizeof(uint8_t) > end)
    {
        return false;
    }
    field = MemAccess::get<uint8_t>(ptr);
    ptr += sizeof(uint8_t);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializeu64(uint64_t& field)
{
    if (ptr + sizeof(uint64_t) > end)
    {
        return false;
    }
    field = MemAccess::get<uint64_t>(ptr);
    ptr += sizeof(uint64_t);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializehandle(handle& field)
{
    if (ptr + sizeof(handle) > end)
    {
        return false;
    }
    field = MemAccess::get<handle>(ptr);
    ptr += sizeof(handle);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializenodehandle(handle& field)
{
    if (ptr + MegaClient::NODEHANDLE > end)
    {
        return false;
    }
    field = 0;
    memcpy((char*)&field, ptr, MegaClient::NODEHANDLE);
    ptr += MegaClient::NODEHANDLE;
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializeNodeHandle(NodeHandle& field)
{
    handle h;
    if (!unserializenodehandle(h)) return false;
    field.set6byte(h);
    return true;
}

bool CacheableReader::unserializebool(bool& field)
{
    if (ptr + sizeof(bool) > end)
    {
        return false;
    }
    field = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializebyte(byte& field)
{
    if (ptr + sizeof(byte) > end)
    {
        return false;
    }
    field = MemAccess::get<byte>(ptr);
    ptr += sizeof(byte);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializedouble(double& field)
{
    if (ptr + sizeof(double) > end)
    {
        return false;
    }
    field = MemAccess::get<double>(ptr);
    ptr += sizeof(double);
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializeexpansionflags(unsigned char field[8], unsigned usedFlagCount)
{
    if (ptr + 8 > end)
    {
        return false;
    }
    memcpy(field, ptr, 8);

    for (int i = usedFlagCount;  i < 8; i++ )
    {
        if (field[i])
        {
            LOG_err << "Unserialization failed in expansion flags, invalid version detected.  Fieldnum: " << fieldnum;
            return false;
        }
    }

    ptr += 8;
    fieldnum += 1;
    return true;
}

bool CacheableReader::unserializedirection(direction_t& field)
{
    // TODO:  this one should be removed when we next update the transfer db format.  sizeof(direction_t) is not the same for all compilers.  And could even change if someone edits the enum
    if (ptr + sizeof(direction_t) > end)
    {
        return false;
    }

    field = MemAccess::get<direction_t>(ptr);
    ptr += sizeof(direction_t);
    fieldnum += 1;
    return true;
}


/**
 * @brief Encrypts a string after padding it to block length.
 *
 * Note: With an IV, only use the first 8 bytes.
 *
 * @param data Data buffer to be encrypted. Encryption is done in-place,
 *     so cipher text will be in `data` afterwards as well.
 * @param key AES key for encryption.
 * @param iv Optional initialisation vector for encryption. Will use a
 *     zero IV if not given. If `iv` is a zero length string, a new IV
 *     for encryption will be generated and available through the reference.
 * @return true if encryption was successful.
 */
bool PaddedCBC::encrypt(PrnGen &rng, string* data, SymmCipher* key, string* iv)
{
    if (iv)
    {
        // Make a new 8-byte IV, if the one passed is zero length.
        if (iv->size() == 0)
        {
            byte* buf = new byte[8];
            rng.genblock(buf, 8);
            iv->append((char*)buf);
            delete [] buf;
        }

        // Truncate a longer IV to its first 8 bytes.
        if (iv->size() > 8)
        {
            iv->resize(8);
        }

        // Bring up the IV size to BLOCKSIZE.
        iv->resize(key->BLOCKSIZE);
    }

    // Pad to block size and encrypt.
    data->append("E");
    data->resize((data->size() + key->BLOCKSIZE - 1) & - key->BLOCKSIZE, 'P');
    byte* dd = reinterpret_cast<byte*>(const_cast<char*>(data->data())); // make sure it works for pre-C++17 compilers
    bool encrypted = iv ?
        key->cbc_encrypt(dd, data->size(), reinterpret_cast<const byte*>(iv->data())) :
        key->cbc_encrypt(dd, data->size());

    // Truncate IV back to the first 8 bytes only..
    if (iv)
    {
        iv->resize(8);
    }

    return encrypted;
}

/**
 * @brief Decrypts a string and strips the padding.
 *
 * Note: With an IV, only use the first 8 bytes.
 *
 * @param data Data buffer to be decrypted. Decryption is done in-place,
 *     so plain text will be in `data` afterwards as well.
 * @param key AES key for decryption.
 * @param iv Optional initialisation vector for encryption. Will use a
 *     zero IV if not given.
 * @return true if decryption was successful.
 */
bool PaddedCBC::decrypt(string* data, SymmCipher* key, string* iv)
{
    if (iv)
    {
        // Truncate a longer IV to its first 8 bytes.
        if (iv->size() > 8)
        {
            iv->resize(8);
        }

        // Bring up the IV size to BLOCKSIZE.
        iv->resize(key->BLOCKSIZE);
    }

    if ((data->size() & (key->BLOCKSIZE - 1)))
    {
        return false;
    }

    // Decrypt and unpad.
    byte* dd = reinterpret_cast<byte*>(const_cast<char*>(data->data())); // make sure it works for pre-C++17 compilers
    bool encrypted = iv ?
        key->cbc_decrypt(dd, data->size(), reinterpret_cast<const byte*>(iv->data())) :
        key->cbc_decrypt(dd, data->size());
    if (!encrypted)
    {
        return false;
    }

    size_t p = data->find_last_of('E');

    if (p == string::npos)
    {
        return false;
    }

    data->resize(p);

    return true;
}

// start of chunk
m_off_t ChunkedHash::chunkfloor(m_off_t p)
{
    m_off_t cp, np;

    cp = 0;

    for (unsigned i = 1; i <= 8; i++)
    {
        np = cp + i * SEGSIZE;

        if ((p >= cp) && (p < np))
        {
            return cp;
        }

        cp = np;
    }

    return ((p - cp) & - (8 * SEGSIZE)) + cp;
}

// end of chunk (== start of next chunk)
m_off_t ChunkedHash::chunkceil(m_off_t p, m_off_t limit)
{
    m_off_t cp, np;

    cp = 0;

    for (unsigned i = 1; i <= 8; i++)
    {
        np = cp + i * SEGSIZE;

        if ((p >= cp) && (p < np))
        {
            return (limit < 0 || np < limit) ? np : limit;
        }

        cp = np;
    }

    np = ((p - cp) & - (8 * SEGSIZE)) + cp + 8 * SEGSIZE;
    return (limit < 0 || np < limit) ? np : limit;
}


// cryptographic signature generation/verification
HashSignature::HashSignature(Hash* h)
{
    hash = h;
}

HashSignature::~HashSignature()
{
    delete hash;
}

void HashSignature::add(const byte* data, unsigned len)
{
    hash->add(data, len);
}

unsigned HashSignature::get(AsymmCipher* privk, byte* sigbuf, unsigned sigbuflen)
{
    string h;

    hash->get(&h);

    return privk->rawdecrypt((const byte*)h.data(), h.size(), sigbuf, sigbuflen);
}

bool HashSignature::checksignature(AsymmCipher* pubk, const byte* sig, unsigned len)
{
    string h, s;
    unsigned size;

    hash->get(&h);

    s.resize(h.size());

    if (!(size = pubk->rawencrypt(sig, len, (byte*)s.data(), s.size())))
    {
        return 0;
    }

    if (size < h.size())
    {
        // left-pad with 0
        s.insert(0, h.size() - size, 0);
        s.resize(h.size());
    }

    return s == h;
}

PayCrypter::PayCrypter(PrnGen &rng)
    : rng(rng)
{
    rng.genblock(keys, ENC_KEY_BYTES + MAC_KEY_BYTES);
    encKey = keys;
    hmacKey = keys+ENC_KEY_BYTES;

    rng.genblock(iv, IV_BYTES);
}

void PayCrypter::setKeys(const byte *newEncKey, const byte *newHmacKey, const byte *newIv)
{
    memcpy(encKey, newEncKey, ENC_KEY_BYTES);
    memcpy(hmacKey, newHmacKey, MAC_KEY_BYTES);
    memcpy(iv, newIv, IV_BYTES);
}

bool PayCrypter::encryptPayload(const string *cleartext, string *result)
{
    //Check parameters
    if(!cleartext || !result)
    {
        return false;
    }

    //AES-CBC encryption
    string encResult;
    SymmCipher sym(encKey);
    if (!sym.cbc_encrypt_pkcs_padding(cleartext, iv, &encResult))
    {
        return false;
    }

    //Prepare the message to authenticate (IV + cipher text)
    string toAuthenticate((char *)iv, IV_BYTES);
    toAuthenticate.append(encResult);

    //HMAC-SHA256
    HMACSHA256 hmacProcessor(hmacKey, MAC_KEY_BYTES);
    hmacProcessor.add((byte *)toAuthenticate.data(), toAuthenticate.size());
    result->resize(32);
    hmacProcessor.get((byte *)result->data());

    //Complete the result (HMAC + IV - ciphertext)
    result->append((char *)iv, IV_BYTES);
    result->append(encResult);
    return true;
}

bool PayCrypter::rsaEncryptKeys(const string *cleartext, const byte *pubkdata, int pubkdatalen, string *result, bool randompadding)
{
    //Check parameters
    if(!cleartext || !pubkdata || !result)
    {
        return false;
    }

    //Create an AsymmCipher with the public key
    AsymmCipher asym;
    asym.setkey(AsymmCipher::PUBKEY, pubkdata, pubkdatalen);

    //Prepare the message to encrypt (2-byte header + clear text)
    string keyString;
    keyString.append(1, (byte)(cleartext->size() >> 8));
    keyString.append(1, (byte)(cleartext->size()));
    keyString.append(*cleartext);

    //Save the length of the valid message
    size_t keylen = keyString.size();

    //Resize to add padding
    keyString.resize(asym.getKey(AsymmCipher::PUB_PQ).ByteCount() - 2);

    //Add padding
    if(randompadding)
    {
        rng.genblock((byte *)keyString.data() + keylen, keyString.size() - keylen);
    }

    //RSA encryption
    result->resize(pubkdatalen);
    result->resize(asym.rawencrypt((byte *)keyString.data(), keyString.size(), (byte *)result->data(), result->size()));

    //Complete the result (2-byte header + RSA result)
    size_t reslen = result->size();
    result->insert(0, 1, (byte)(reslen >> 8));
    result->insert(1, 1, (byte)(reslen));
    return true;
}

bool PayCrypter::hybridEncrypt(const string *cleartext, const byte *pubkdata, int pubkdatalen, string *result, bool randompadding)
{
    if(!cleartext || !pubkdata || !result)
    {
        return false;
    }

    //Generate the payload
    string payloadString;
    encryptPayload(cleartext, &payloadString);

    //RSA encryption
    string rsaKeyCipher;
    string keysString;
    keysString.assign((char *)keys, ENC_KEY_BYTES + MAC_KEY_BYTES);
    rsaEncryptKeys(&keysString, pubkdata, pubkdatalen, &rsaKeyCipher, randompadding);

    //Complete the result
    *result = rsaKeyCipher + payloadString;
    return true;
}

string * TLVstore::tlvRecordsToContainer(PrnGen &rng, SymmCipher *key, encryptionsetting_t encSetting)
{
    // decide nonce/IV and auth. tag lengths based on the `mode`
    unsigned ivlen = TLVstore::getIvlen(encSetting);
    unsigned taglen = TLVstore::getTaglen(encSetting);
    encryptionmode_t encMode = TLVstore::getMode(encSetting);

    if (!ivlen || !taglen || encMode == AES_MODE_UNKNOWN)
    {
        return NULL;
    }

    // serialize the TLV records
    std::unique_ptr<string> container(tlvRecordsToContainer());

    // generate IV array
    std::vector<byte> iv(ivlen);
    rng.genblock(iv.data(), ivlen);

    string cipherText;

    // encrypt the bytes using the specified mode

    if (encMode == AES_MODE_CCM)   // CCM or GCM_BROKEN (same than CCM)
    {
        if (!key->ccm_encrypt(container.get(), iv.data(), ivlen, taglen, &cipherText))
        {
            return nullptr;
        }
    }
    else if (encMode == AES_MODE_GCM)   // then use GCM
    {
        if (!key->gcm_encrypt(container.get(), iv.data(), ivlen, taglen, &cipherText))
        {
            return nullptr;
        }
    }

    string *result = new string;
    result->resize(1);
    result->at(0) = static_cast<char>(encSetting);
    result->append((char*) iv.data(), ivlen);
    result->append((char*) cipherText.data(), cipherText.length()); // includes auth. tag

    return result;
}

string* TLVstore::tlvRecordsToContainer()
{
    string *result = new string;
    size_t offset = 0;
    size_t length;

    for (TLV_map::iterator it = tlv.begin(); it != tlv.end(); it++)
    {
        // copy Type
        result->append(it->first);
        offset += it->first.length() + 1;   // keep the NULL-char for Type string

        // set Length of value
        length = it->second.length();
        if (length > 0xFFFF)
        {
            assert(it->first == "" && tlv.size() == 1 && "Only records with a single empty key can be larger");
            LOG_warn << "Overflow of Length for TLV record: " << length;
            length = 0xFFFF;
        }
        result->resize(offset + 2);
        result->at(offset) = static_cast<char>(length >> 8);
        result->at(offset + 1) = static_cast<char>(length & 0xFF);
        offset += 2;

        // copy the Value
        result->append((char*)it->second.data(), it->second.length());
        offset += it->second.length();
    }

    return result;
}

bool TLVstore::get(string type, string& value) const
{
    auto it = tlv.find(type);
    if (it == tlv.cend())
        return false;

    value = it->second;
    return true;
}

const TLV_map * TLVstore::getMap() const
{
    return &tlv;
}

vector<string> *TLVstore::getKeys() const
{
    vector<string> *keys = new vector<string>;
    for (string_map::const_iterator it = tlv.begin(); it != tlv.end(); it++)
    {
        keys->push_back(it->first);
    }
    return keys;
}

void TLVstore::set(string type, string value)
{
    tlv[type] = value;
}

void TLVstore::reset(std::string type)
{
    tlv.erase(type);
}

size_t TLVstore::size()
{
    return tlv.size();
}

unsigned TLVstore::getTaglen(int mode)
{

    switch (mode)
    {
    case AES_CCM_10_16:
    case AES_CCM_12_16:
    case AES_GCM_12_16_BROKEN:
    case AES_GCM_12_16:
        return 16;

    case AES_CCM_10_08:
    case AES_GCM_10_08_BROKEN:
    case AES_GCM_10_08:
        return 8;

    default:    // unknown block encryption mode
        return 0;
    }
}

unsigned TLVstore::getIvlen(int mode)
{
    switch (mode)
    {
    case AES_CCM_12_16:
    case AES_GCM_12_16_BROKEN:
    case AES_GCM_12_16:
        return 12;

    case AES_CCM_10_08:
    case AES_GCM_10_08_BROKEN:
    case AES_CCM_10_16:
    case AES_GCM_10_08:
        return 10;

    default:    // unknown block encryption mode
        return 0;
    }
}

encryptionmode_t TLVstore::getMode(int mode)
{
    switch (mode)
    {
    case AES_CCM_12_16:
    case AES_GCM_12_16_BROKEN:
    case AES_CCM_10_16:
    case AES_CCM_10_08:
    case AES_GCM_10_08_BROKEN:
        return AES_MODE_CCM;

    case AES_GCM_12_16:
    case AES_GCM_10_08:
        return AES_MODE_GCM;

    default:    // unknown block encryption mode
        return AES_MODE_UNKNOWN;
    }
}

TLVstore * TLVstore::containerToTLVrecords(const string *data)
{
    if (data->empty())
    {
        return NULL;
    }

    TLVstore *tlv = new TLVstore();

    size_t offset = 0;

    string type;
    size_t typelen;
    string value;
    unsigned valuelen;
    size_t pos;

    size_t datalen = data->length();

    // T is a C-string
    // L is an unsigned integer encoded in 2 bytes (aka. uint16_t)
    // V is a binary buffer of "L" bytes

    // if the size of the container is greater than the 65538 (T.1, L.2, V.>65535),
    // then the container is probably an authring whose value is greater than the
    // maximum length supported by 2 bytes. Since the T is an empty string for this
    // type of attributes, we can directly assign the value to the record
    // note: starting from Nov2022, if the size of the record is too large,
    // the Length will be truncated to indicate the maximum size 65535 = 0xFFFF
    size_t maxSize = 1 + 2 + 0xFFFF;
    if (datalen >= maxSize && !(*data)[0])
    {
        tlv->set("", data->substr(3));
        return tlv;
    }

    while (offset < datalen)
    {
        // get the length of the Type string
        pos = data->find('\0', offset);
        typelen = pos - offset;

        // if no valid TLV record in the container, but remaining bytes...
        if (pos == string::npos || offset + typelen + 3 > datalen)
        {
            delete tlv;
            return NULL;
        }

        // get the Type string
        type.assign((char*)&(data->data()[offset]), typelen);
        offset += typelen + 1;        // +1: NULL character

        // get the Length of the value
        valuelen = (unsigned char)data->at(offset) << 8
                 | (unsigned char)data->at(offset + 1);
        offset += 2;

        // if there's not enough data for value...
        if (offset + valuelen > datalen)
        {
            delete tlv;
            return NULL;
        }

        // get the Value
        value.assign((char*)&(data->data()[offset]), valuelen);  // value may include NULL characters, read as a buffer
        offset += valuelen;

        // add it to the map
        tlv->set(type, value);
    }

    return tlv;
}


TLVstore * TLVstore::containerToTLVrecords(const string *data, SymmCipher *key)
{
    if (data->empty())
    {
        return NULL;
    }

    unsigned offset = 0;
    encryptionsetting_t encSetting = (encryptionsetting_t) data->at(offset);
    offset++;

    unsigned ivlen = TLVstore::getIvlen(encSetting);
    unsigned taglen = TLVstore::getTaglen(encSetting);
    encryptionmode_t encMode = TLVstore::getMode(encSetting);

    if (encMode == AES_MODE_UNKNOWN || !ivlen || !taglen ||  data->size() < offset+ivlen+taglen)
    {
        return NULL;
    }

    byte *iv = new byte[ivlen];
    memcpy(iv, &(data->data()[offset]), ivlen);
    offset += ivlen;

    unsigned cipherTextLen = unsigned(data->length() - offset);
    string cipherText = data->substr(offset, cipherTextLen);

    unsigned clearTextLen = cipherTextLen - taglen;
    string clearText;

    bool decrypted = false;
    if (encMode == AES_MODE_CCM)   // CCM or GCM_BROKEN (same than CCM)
    {
       decrypted = key->ccm_decrypt(&cipherText, iv, ivlen, taglen, &clearText);
    }
    else if (encMode == AES_MODE_GCM)  // GCM
    {
       decrypted = key->gcm_decrypt(&cipherText, iv, ivlen, taglen, &clearText);
    }

    delete [] iv;

    if (!decrypted)  // the decryption has failed (probably due to authentication)
    {
        return NULL;
    }
    else if (clearText.empty()) // If decryption succeeded but attribute is empty, generate an empty TLV
    {
        return new TLVstore();
    }

    TLVstore *tlv = TLVstore::containerToTLVrecords(&clearText);
    if (!tlv) // 'data' might be affected by the legacy bug: strings encoded in UTF-8 instead of Unicode
    {
        // retry TLV decoding after conversion from 'UTF-8 chars' to 'Unicode chars'
        LOG_warn << "Retrying TLV records decoding with UTF-8 patch";

        string clearTextUnicode;
        if (!Utils::utf8toUnicode((const byte*)clearText.data(), clearTextLen, &clearTextUnicode))
        {
            LOG_err << "Invalid UTF-8 encoding";
        }
        else
        {
            tlv = TLVstore::containerToTLVrecords(&clearTextUnicode);
        }
    }

    return tlv;
}

TLVstore::~TLVstore()
{
}

size_t Utils::utf8SequenceSize(unsigned char c)
{
    int aux = static_cast<int>(c);
    if (aux >= 0 && aux <= 127)     return 1;
    else if ((aux & 0xE0) == 0xC0)  return 2;
    else if ((aux & 0xF0) == 0xE0)  return 3;
    else if ((aux & 0xF8) == 0xF0)  return 4;
    else
    {
        LOG_err << "Malformed UTF-8 sequence, interpret character " << c << " as literal";
        return 1;
    }
}

string  Utils::toUpperUtf8(const string& text)
{
    string result;

    auto n = utf8proc_ssize_t(text.size());
    auto d = text.data();

    for (;;)
    {
        utf8proc_int32_t c;
        auto nn = utf8proc_iterate((utf8proc_uint8_t *)d, n, &c);

        if (nn == 0) break;

        assert(nn <= n);
        d += nn;
        n -= nn;

        c = utf8proc_toupper(c);

        char buff[8];
        auto charLen = utf8proc_encode_char(c, (utf8proc_uint8_t *)buff);
        result.append(buff, charLen);
    }

    return result;
}

string  Utils::toLowerUtf8(const string& text)
{
    string result;

    auto n = utf8proc_ssize_t(text.size());
    auto d = text.data();

    for (;;)
    {
        utf8proc_int32_t c;
        auto nn = utf8proc_iterate((utf8proc_uint8_t *)d, n, &c);

        if (nn == 0) break;

        assert(nn <= n);
        d += nn;
        n -= nn;

        c = utf8proc_tolower(c);

        char buff[8];
        auto charLen = utf8proc_encode_char(c, (utf8proc_uint8_t *)buff);
        result.append(buff, charLen);
    }

    return result;
}


bool Utils::utf8toUnicode(const uint8_t *src, unsigned srclen, string *result)
{
    uint8_t utf8cp1;
    uint8_t utf8cp2;
    int32_t unicodecp;

    if (!srclen)
    {
        result->clear();
        return true;
    }

    byte *res = new byte[srclen];
    unsigned rescount = 0;

    unsigned i = 0;
    while (i < srclen)
    {
        utf8cp1 = src[i++];

        if (utf8cp1 < 0x80)
        {
            res[rescount++] = utf8cp1;
        }
        else
        {
            if (i < srclen)
            {
                utf8cp2 = src[i++];

                // check codepoints are valid
                if ((utf8cp1 == 0xC2 || utf8cp1 == 0xC3) && utf8cp2 >= 0x80 && utf8cp2 <= 0xBF)
                {
                    unicodecp = ((utf8cp1 & 0x1F) <<  6) + (utf8cp2 & 0x3F);
                    res[rescount++] = static_cast<byte>(unicodecp & 0xFF);
                }
                else
                {
                    // error: one of the two-bytes UTF-8 char is not a valid UTF-8 char
                    delete [] res;
                    return false;
                }
            }
            else
            {
                // error: last byte indicates a two-bytes UTF-8 char, but only one left
                delete [] res;
                return false;
            }
        }
    }

    result->assign((const char*)res, rescount);
    delete [] res;

    return true;
}

std::string Utils::stringToHex(const std::string &input)
{
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

std::string Utils::hexToString(const std::string &input)
{
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();
    if (len & 1) throw std::invalid_argument("odd length");

    std::string output;
    output.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2)
    {
        char a = input[i];
        const char* p = std::lower_bound(lut, lut + 16, a);
        if (*p != a) throw std::invalid_argument("not a hex digit");

        char b = input[i + 1];
        const char* q = std::lower_bound(lut, lut + 16, b);
        if (*q != b) throw std::invalid_argument("not a hex digit");

        output.push_back(static_cast<char>(((p - lut) << 4) | (q - lut)));
    }
    return output;
}

uint64_t Utils::hexStringToUint64(const std::string &input)
{
    uint64_t output;
    std::stringstream outputStream;
    outputStream << std::hex << input;
    outputStream >> output;
    return output;
}

std::string Utils::uint64ToHexString(uint64_t input)
{
    std::stringstream outputStream;
    outputStream << std::hex << std::setfill('0') << std::setw(16) << input;
    std::string output = outputStream.str();
    return output;
}

int Utils::icasecmp(const std::string& lhs,
                    const std::string& rhs,
                    const size_t length)
{
    assert(lhs.size() >= length);
    assert(rhs.size() >= length);

#ifdef _WIN32
    return _strnicmp(lhs.c_str(), rhs.c_str(), length);
#else // _WIN32
    return strncasecmp(lhs.c_str(), rhs.c_str(), length);
#endif // ! _WIN32
}

int Utils::icasecmp(const std::wstring& lhs,
                    const std::wstring& rhs,
                    const size_t length)
{
    assert(lhs.size() >= length);
    assert(rhs.size() >= length);

#ifdef _WIN32
    return _wcsnicmp(lhs.c_str(), rhs.c_str(), length);
#else // _WIN32
    return wcsncasecmp(lhs.c_str(), rhs.c_str(), length);
#endif // ! _WIN32
}

int Utils::pcasecmp(const std::string& lhs,
                    const std::string& rhs,
                    const size_t length)
{
    assert(lhs.size() >= length);
    assert(rhs.size() >= length);

#ifdef _WIN32
    return icasecmp(lhs, rhs, length);
#else // _WIN32
    return lhs.compare(0, length, rhs, 0, length);
#endif // ! _WIN32
}

int Utils::pcasecmp(const std::wstring& lhs,
                    const std::wstring& rhs,
                    const size_t length)
{
    assert(lhs.size() >= length);
    assert(rhs.size() >= length);

#ifdef _WIN32
    return icasecmp(lhs, rhs, length);
#else // _WIN32
    return lhs.compare(0, length, rhs, 0, length);
#endif // ! _WIN32
}

std::string Utils::replace(const std::string& str, char search, char replacement) {
    string r;
    for (std::string::size_type o = 0;;) {
        std::string::size_type i = str.find(search, o);
        if (i == string::npos) {
            r.append(str.substr(o));
            break;
        }
        r.append(str.substr(o, i-o));
        r += replacement;
        o = i + 1;
    }
    return r;
}

std::string Utils::replace(const std::string& str, const std::string& search, const std::string& replacement) {
    if (search.empty())
        return str;
    string r;
    for (std::string::size_type o = 0;;) {
        std::string::size_type i = str.find(search, o);
        if (i == string::npos) {
            r.append(str.substr(o));
            break;
        }
        r.append(str.substr(o, i - o));
        r += replacement;
        o = i + search.length();
    }
    return r;
}

bool Utils::hasenv(const std::string &key)
{
    bool r = false;
    getenv(key, &r);
    return r;
}

std::string Utils::getenv(const std::string& key, const std::string& def)
{
    bool found = false;
    string r = getenv(key, &found);
    if (!found) return def;
    return r;
}

std::string Utils::getenv(const std::string& key, bool* out_found)\
{
    // sets *out_found to if found
#ifdef WIN32
    // on Windows the charset is not UTF-8 by default
    WCHAR buf[32 * 1024];
    wstring keyW;
    LocalPath::path2local(&key, &keyW);
    DWORD size = GetEnvironmentVariable(keyW.c_str(), buf, sizeof(buf) / sizeof(buf[0]));
    if (size == 0)
    {
        if (out_found) *out_found = false;
        return "";
    }
    else
    {
        if (out_found) *out_found = true;

        string ret;
        wstring input(buf, size);
        LocalPath::local2path(&input, &ret, false);
        return ret;
    }
#else
    const char* value = ::getenv(key.c_str());
    if (out_found)
    {
        *out_found = value != nullptr;
    }
    if (value)
    {
        return value;
    }
    return "";
#endif
}

void Utils::setenv(const std::string& key, const std::string& value)
{
#ifdef WIN32
    std::wstring keyW;
    LocalPath::path2local(&key, &keyW);

    std::wstring valueW;
    LocalPath::path2local(&value, &valueW);

    // on Windows the charset is not UTF-8 by default
    SetEnvironmentVariable(keyW.c_str(), valueW.c_str());

    // ::getenv() reads the process environment not calling the operating system
    _putenv_s(key.c_str(), value.c_str());
#else
    ::setenv(key.c_str(), value.c_str(), true);
#endif
}

void Utils::unsetenv(const std::string& key)
{
#ifdef WIN32
    std::wstring keyW;
    LocalPath::path2local(&key, &keyW);

    SetEnvironmentVariable(keyW.c_str(), L"");
    // ::getenv() reads the process environment not calling the operating system
    _putenv_s(key.c_str(), ""); // removes the env var
#else
    ::unsetenv(key.c_str());
#endif
}

std::string Utils::join(const std::vector<std::string>& items, const std::string& with)
{
    string r;
    bool first = true;
    for (const string& str : items)
    {
        if (!first) r.append(with);
        r.append(str);
        first = false;
    }
    return r;
}

bool Utils::startswith(const std::string& str, const std::string& start)
{
    if (str.length() < start.length()) return false;
    return memcmp(str.data(), start.data(), start.length()) == 0;
}

bool Utils::startswith(const std::string &str, char chr)
{
    return str.length() >= 1 && chr == str.front();
}

bool Utils::endswith(const std::string &str, char chr)
{
    return str.length() >= 1 && chr == str.back();
}

const std::string Utils::_trimDefaultChars(" \t\r\n\0", 5);
// space, \t, \0, \r, \n

// return string with trimchrs removed from front and back of given string str
string Utils::trim(const string& str, const string& trimchrs)
{
    string::size_type s = str.find_first_not_of(trimchrs);
    if (s == string::npos) return "";
    string::size_type e = str.find_last_not_of(trimchrs);
    if (e == string::npos) return "";	// impossible
    return str.substr(s, e - s + 1);
}

struct tm* m_localtime(m_time_t ttime, struct tm *dt)
{
    // works for 32 or 64 bit time_t
    time_t t = time_t(ttime);
#ifdef _WIN32
    localtime_s(&t, dt);
#else
    localtime_r(&t, dt);
#endif
    return dt;
}

struct tm* m_gmtime(m_time_t ttime, struct tm *dt)
{
    // works for 32 or 64 bit time_t
    time_t t = time_t(ttime);
#ifdef _WIN32
    gmtime_s(&t, dt);
#else
    gmtime_r(&t, dt);
#endif
    return dt;
}

m_time_t m_time(m_time_t* tt )
{
    // works for 32 or 64 bit time_t
    time_t t = time(NULL);
    if (tt)
    {
        *tt = t;
    }
    return t;
}

m_time_t m_mktime(struct tm* stm)
{
    // works for 32 or 64 bit time_t
    return mktime(stm);
}

int m_clock_getmonotonictime(timespec *t)
{
#ifdef __APPLE__
    struct timeval now;
    int rv = gettimeofday(&now, NULL);
    if (rv)
    {
        return rv;
    }
    t->tv_sec = now.tv_sec;
    t->tv_nsec = now.tv_usec * 1000;
    return 0;
#elif defined(_WIN32) && defined(_MSC_VER)
    struct __timeb64 tb;
    _ftime64(&tb);
    t->tv_sec = tb.time;
    t->tv_nsec = long(tb.millitm) * 1000000;
    return 0;
#else
#ifdef CLOCK_BOOTTIME
    return clock_gettime(CLOCK_BOOTTIME, t);
#else
    return clock_gettime(CLOCK_MONOTONIC, t);
#endif
#endif

}

m_time_t m_mktime_UTC(const struct tm *src)
{
    struct tm dst = *src;
    m_time_t t = 0;
#if _MSC_VER >= 1400 || defined(__MINGW32__) // MSVCRT (2005+)
    t = mktime(&dst);
    TIME_ZONE_INFORMATION TimeZoneInfo;
    GetTimeZoneInformation(&TimeZoneInfo);
    t += TimeZoneInfo.Bias * 60 - dst.tm_isdst * 3600;
#elif _WIN32
#error "localtime is not thread safe in this compiler; please use a later one"
#else //POSIX
    t = mktime(&dst);
    t += dst.tm_gmtoff - dst.tm_isdst * 3600;
#endif
    return t;
}

extern time_t stringToTimestamp(string stime, date_time_format_t format)
{
    if ((format == FORMAT_SCHEDULED_COPY && stime.size() != 14)
       || (format == FORMAT_ISO8601 && stime.size() != 15))
    {
        return 0;
    }

    if (format == FORMAT_ISO8601)
    {
        stime.erase(8, 1); // remove T from stime (20220726T133000)
    }

    struct tm dt;
    memset(&dt, 0, sizeof(struct tm));
#ifdef _WIN32
    for (int i = 0; i < stime.size(); i++)
    {
        if ( (stime.at(i) < '0') || (stime.at(i) > '9') )
        {
            return 0; //better control of this?
        }
    }

    dt.tm_year = atoi(stime.substr(0,4).c_str()) - 1900;
    dt.tm_mon = atoi(stime.substr(4,2).c_str()) - 1;
    dt.tm_mday = atoi(stime.substr(6,2).c_str());
    dt.tm_hour = atoi(stime.substr(8,2).c_str());
    dt.tm_min = atoi(stime.substr(10,2).c_str());
    dt.tm_sec = atoi(stime.substr(12,2).c_str());
#else
    strptime(stime.c_str(), "%Y%m%d%H%M%S", &dt);
#endif

    if (format == FORMAT_SCHEDULED_COPY)
    {
        // let mktime interprete if time has Daylight Saving Time flag correction
        // TODO: would this work cross platformly? At least I believe it'll be consistent with localtime. Otherwise, we'd need to save that
        dt.tm_isdst = -1;
        return (mktime(&dt))*10;  // deciseconds
    }
    else
    {
        // user manually selects a date and a time to start the scheduled meeting in a specific time zone (independent fields on API)
        // so users should take into account daylight saving for the time zone they specified
        // this method should convert the specified string dateTime into Unix timestamp (UTC)
        dt.tm_isdst = 0;
        return mktime(&dt); // seconds
    }
}

std::string rfc1123_datetime( time_t time )
{
    struct tm * timeinfo;
    char buffer [80];
    timeinfo = gmtime(&time);
    strftime (buffer, 80, "%a, %d %b %Y %H:%M:%S GMT",timeinfo);
    return buffer;
}

string webdavurlescape(const string &value)
{
    ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i)
    {
        string::value_type c = (*i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':')
        {
            escaped << c;
        }
        else
        {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char) c);
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

string escapewebdavchar(const char c)
{
    static bool unintitialized = true;
    static std::map<int,const char *> escapesec;
    if (unintitialized)
    {
        escapesec[33] = "&#33;"; // !  //For some reason &Exclamation; was not properly handled (crashed) by gvfsd-dav
        escapesec[34] = "&quot;"; // "
        escapesec[37] = "&percnt;"; // %
        escapesec[38] = "&amp;"; // &
        escapesec[39] = "&apos;"; // '
        escapesec[43] = "&add;"; // +
        escapesec[60] = "&lt;"; // <
        escapesec[61] = "&#61;"; // = //For some reason &equal; was not properly handled (crashed) by gvfsd-dav
        escapesec[62] = "&gt;"; // >
        escapesec[160] = "&nbsp;"; //NO-BREAK SPACE
        escapesec[161] = "&iexcl;"; //INVERTED EXCLAMATION MARK
        escapesec[162] = "&cent;"; //CENT SIGN
        escapesec[163] = "&pound;"; //POUND SIGN
        escapesec[164] = "&curren;"; //CURRENCY SIGN
        escapesec[165] = "&yen;"; //YEN SIGN
        escapesec[166] = "&brvbar;"; //BROKEN BAR
        escapesec[167] = "&sect;"; //SECTION SIGN
        escapesec[168] = "&uml;"; //DIAERESIS
        escapesec[169] = "&copy;"; //COPYRIGHT SIGN
        escapesec[170] = "&ordf;"; //FEMININE ORDINAL INDICATOR
        escapesec[171] = "&laquo;"; //LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
        escapesec[172] = "&not;"; //NOT SIGN
        escapesec[173] = "&shy;"; //SOFT HYPHEN
        escapesec[174] = "&reg;"; //REGISTERED SIGN
        escapesec[175] = "&macr;"; //MACRON
        escapesec[176] = "&deg;"; //DEGREE SIGN
        escapesec[177] = "&plusmn;"; //PLUS-MINUS SIGN
        escapesec[178] = "&sup2;"; //SUPERSCRIPT TWO
        escapesec[179] = "&sup3;"; //SUPERSCRIPT THREE
        escapesec[180] = "&acute;"; //ACUTE ACCENT
        escapesec[181] = "&micro;"; //MICRO SIGN
        escapesec[182] = "&para;"; //PILCROW SIGN
        escapesec[183] = "&middot;"; //MIDDLE DOT
        escapesec[184] = "&cedil;"; //CEDILLA
        escapesec[185] = "&sup1;"; //SUPERSCRIPT ONE
        escapesec[186] = "&ordm;"; //MASCULINE ORDINAL INDICATOR
        escapesec[187] = "&raquo;"; //RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
        escapesec[188] = "&frac14;"; //VULGAR FRACTION ONE QUARTER
        escapesec[189] = "&frac12;"; //VULGAR FRACTION ONE HALF
        escapesec[190] = "&frac34;"; //VULGAR FRACTION THREE QUARTERS
        escapesec[191] = "&iquest;"; //INVERTED QUESTION MARK
        escapesec[192] = "&Agrave;"; //LATIN CAPITAL LETTER A WITH GRAVE
        escapesec[193] = "&Aacute;"; //LATIN CAPITAL LETTER A WITH ACUTE
        escapesec[194] = "&Acirc;"; //LATIN CAPITAL LETTER A WITH CIRCUMFLEX
        escapesec[195] = "&Atilde;"; //LATIN CAPITAL LETTER A WITH TILDE
        escapesec[196] = "&Auml;"; //LATIN CAPITAL LETTER A WITH DIAERESIS
        escapesec[197] = "&Aring;"; //LATIN CAPITAL LETTER A WITH RING ABOVE
        escapesec[198] = "&AElig;"; //LATIN CAPITAL LETTER AE
        escapesec[199] = "&Ccedil;"; //LATIN CAPITAL LETTER C WITH CEDILLA
        escapesec[200] = "&Egrave;"; //LATIN CAPITAL LETTER E WITH GRAVE
        escapesec[201] = "&Eacute;"; //LATIN CAPITAL LETTER E WITH ACUTE
        escapesec[202] = "&Ecirc;"; //LATIN CAPITAL LETTER E WITH CIRCUMFLEX
        escapesec[203] = "&Euml;"; //LATIN CAPITAL LETTER E WITH DIAERESIS
        escapesec[204] = "&Igrave;"; //LATIN CAPITAL LETTER I WITH GRAVE
        escapesec[205] = "&Iacute;"; //LATIN CAPITAL LETTER I WITH ACUTE
        escapesec[206] = "&Icirc;"; //LATIN CAPITAL LETTER I WITH CIRCUMFLEX
        escapesec[207] = "&Iuml;"; //LATIN CAPITAL LETTER I WITH DIAERESIS
        escapesec[208] = "&ETH;"; //LATIN CAPITAL LETTER ETH
        escapesec[209] = "&Ntilde;"; //LATIN CAPITAL LETTER N WITH TILDE
        escapesec[210] = "&Ograve;"; //LATIN CAPITAL LETTER O WITH GRAVE
        escapesec[211] = "&Oacute;"; //LATIN CAPITAL LETTER O WITH ACUTE
        escapesec[212] = "&Ocirc;"; //LATIN CAPITAL LETTER O WITH CIRCUMFLEX
        escapesec[213] = "&Otilde;"; //LATIN CAPITAL LETTER O WITH TILDE
        escapesec[214] = "&Ouml;"; //LATIN CAPITAL LETTER O WITH DIAERESIS
        escapesec[215] = "&times;"; //MULTIPLICATION SIGN
        escapesec[216] = "&Oslash;"; //LATIN CAPITAL LETTER O WITH STROKE
        escapesec[217] = "&Ugrave;"; //LATIN CAPITAL LETTER U WITH GRAVE
        escapesec[218] = "&Uacute;"; //LATIN CAPITAL LETTER U WITH ACUTE
        escapesec[219] = "&Ucirc;"; //LATIN CAPITAL LETTER U WITH CIRCUMFLEX
        escapesec[220] = "&Uuml;"; //LATIN CAPITAL LETTER U WITH DIAERESIS
        escapesec[221] = "&Yacute;"; //LATIN CAPITAL LETTER Y WITH ACUTE
        escapesec[222] = "&THORN;"; //LATIN CAPITAL LETTER THORN
        escapesec[223] = "&szlig;"; //LATIN SMALL LETTER SHARP S
        escapesec[224] = "&agrave;"; //LATIN SMALL LETTER A WITH GRAVE
        escapesec[225] = "&aacute;"; //LATIN SMALL LETTER A WITH ACUTE
        escapesec[226] = "&acirc;"; //LATIN SMALL LETTER A WITH CIRCUMFLEX
        escapesec[227] = "&atilde;"; //LATIN SMALL LETTER A WITH TILDE
        escapesec[228] = "&auml;"; //LATIN SMALL LETTER A WITH DIAERESIS
        escapesec[229] = "&aring;"; //LATIN SMALL LETTER A WITH RING ABOVE
        escapesec[230] = "&aelig;"; //LATIN SMALL LETTER AE
        escapesec[231] = "&ccedil;"; //LATIN SMALL LETTER C WITH CEDILLA
        escapesec[232] = "&egrave;"; //LATIN SMALL LETTER E WITH GRAVE
        escapesec[233] = "&eacute;"; //LATIN SMALL LETTER E WITH ACUTE
        escapesec[234] = "&ecirc;"; //LATIN SMALL LETTER E WITH CIRCUMFLEX
        escapesec[235] = "&euml;"; //LATIN SMALL LETTER E WITH DIAERESIS
        escapesec[236] = "&igrave;"; //LATIN SMALL LETTER I WITH GRAVE
        escapesec[237] = "&iacute;"; //LATIN SMALL LETTER I WITH ACUTE
        escapesec[238] = "&icirc;"; //LATIN SMALL LETTER I WITH CIRCUMFLEX
        escapesec[239] = "&iuml;"; //LATIN SMALL LETTER I WITH DIAERESIS
        escapesec[240] = "&eth;"; //LATIN SMALL LETTER ETH
        escapesec[241] = "&ntilde;"; //LATIN SMALL LETTER N WITH TILDE
        escapesec[242] = "&ograve;"; //LATIN SMALL LETTER O WITH GRAVE
        escapesec[243] = "&oacute;"; //LATIN SMALL LETTER O WITH ACUTE
        escapesec[244] = "&ocirc;"; //LATIN SMALL LETTER O WITH CIRCUMFLEX
        escapesec[245] = "&otilde;"; //LATIN SMALL LETTER O WITH TILDE
        escapesec[246] = "&ouml;"; //LATIN SMALL LETTER O WITH DIAERESIS
        escapesec[247] = "&divide;"; //DIVISION SIGN
        escapesec[248] = "&oslash;"; //LATIN SMALL LETTER O WITH STROKE
        escapesec[249] = "&ugrave;"; //LATIN SMALL LETTER U WITH GRAVE
        escapesec[250] = "&uacute;"; //LATIN SMALL LETTER U WITH ACUTE
        escapesec[251] = "&ucirc;"; //LATIN SMALL LETTER U WITH CIRCUMFLEX
        escapesec[252] = "&uuml;"; //LATIN SMALL LETTER U WITH DIAERESIS
        escapesec[253] = "&yacute;"; //LATIN SMALL LETTER Y WITH ACUTE
        escapesec[254] = "&thorn;"; //LATIN SMALL LETTER THORN
        escapesec[255] = "&yuml;"; //LATIN SMALL LETTER Y WITH DIAERESIS
        escapesec[338] = "&OElig;"; //LATIN CAPITAL LIGATURE OE
        escapesec[339] = "&oelig;"; //LATIN SMALL LIGATURE OE
        escapesec[352] = "&Scaron;"; //LATIN CAPITAL LETTER S WITH CARON
        escapesec[353] = "&scaron;"; //LATIN SMALL LETTER S WITH CARON
        escapesec[376] = "&Yuml;"; //LATIN CAPITAL LETTER Y WITH DIAERESIS
        escapesec[402] = "&fnof;"; //LATIN SMALL LETTER F WITH HOOK
        escapesec[710] = "&circ;"; //MODIFIER LETTER CIRCUMFLEX ACCENT
        escapesec[732] = "&tilde;"; //SMALL TILDE
        escapesec[913] = "&Alpha;"; //GREEK CAPITAL LETTER ALPHA
        escapesec[914] = "&Beta;"; //GREEK CAPITAL LETTER BETA
        escapesec[915] = "&Gamma;"; //GREEK CAPITAL LETTER GAMMA
        escapesec[916] = "&Delta;"; //GREEK CAPITAL LETTER DELTA
        escapesec[917] = "&Epsilon;"; //GREEK CAPITAL LETTER EPSILON
        escapesec[918] = "&Zeta;"; //GREEK CAPITAL LETTER ZETA
        escapesec[919] = "&Eta;"; //GREEK CAPITAL LETTER ETA
        escapesec[920] = "&Theta;"; //GREEK CAPITAL LETTER THETA
        escapesec[921] = "&Iota;"; //GREEK CAPITAL LETTER IOTA
        escapesec[922] = "&Kappa;"; //GREEK CAPITAL LETTER KAPPA
        escapesec[923] = "&Lambda;"; //GREEK CAPITAL LETTER LAMDA
        escapesec[924] = "&Mu;"; //GREEK CAPITAL LETTER MU
        escapesec[925] = "&Nu;"; //GREEK CAPITAL LETTER NU
        escapesec[926] = "&Xi;"; //GREEK CAPITAL LETTER XI
        escapesec[927] = "&Omicron;"; //GREEK CAPITAL LETTER OMICRON
        escapesec[928] = "&Pi;"; //GREEK CAPITAL LETTER PI
        escapesec[929] = "&Rho;"; //GREEK CAPITAL LETTER RHO
        escapesec[931] = "&Sigma;"; //GREEK CAPITAL LETTER SIGMA
        escapesec[932] = "&Tau;"; //GREEK CAPITAL LETTER TAU
        escapesec[933] = "&Upsilon;"; //GREEK CAPITAL LETTER UPSILON
        escapesec[934] = "&Phi;"; //GREEK CAPITAL LETTER PHI
        escapesec[935] = "&Chi;"; //GREEK CAPITAL LETTER CHI
        escapesec[936] = "&Psi;"; //GREEK CAPITAL LETTER PSI
        escapesec[937] = "&Omega;"; //GREEK CAPITAL LETTER OMEGA
        escapesec[945] = "&alpha;"; //GREEK SMALL LETTER ALPHA
        escapesec[946] = "&beta;"; //GREEK SMALL LETTER BETA
        escapesec[947] = "&gamma;"; //GREEK SMALL LETTER GAMMA
        escapesec[948] = "&delta;"; //GREEK SMALL LETTER DELTA
        escapesec[949] = "&epsilon;"; //GREEK SMALL LETTER EPSILON
        escapesec[950] = "&zeta;"; //GREEK SMALL LETTER ZETA
        escapesec[951] = "&eta;"; //GREEK SMALL LETTER ETA
        escapesec[952] = "&theta;"; //GREEK SMALL LETTER THETA
        escapesec[953] = "&iota;"; //GREEK SMALL LETTER IOTA
        escapesec[954] = "&kappa;"; //GREEK SMALL LETTER KAPPA
        escapesec[955] = "&lambda;"; //GREEK SMALL LETTER LAMDA
        escapesec[956] = "&mu;"; //GREEK SMALL LETTER MU
        escapesec[957] = "&nu;"; //GREEK SMALL LETTER NU
        escapesec[958] = "&xi;"; //GREEK SMALL LETTER XI
        escapesec[959] = "&omicron;"; //GREEK SMALL LETTER OMICRON
        escapesec[960] = "&pi;"; //GREEK SMALL LETTER PI
        escapesec[961] = "&rho;"; //GREEK SMALL LETTER RHO
        escapesec[962] = "&sigmaf;"; //GREEK SMALL LETTER FINAL SIGMA
        escapesec[963] = "&sigma;"; //GREEK SMALL LETTER SIGMA
        escapesec[964] = "&tau;"; //GREEK SMALL LETTER TAU
        escapesec[965] = "&upsilon;"; //GREEK SMALL LETTER UPSILON
        escapesec[966] = "&phi;"; //GREEK SMALL LETTER PHI
        escapesec[967] = "&chi;"; //GREEK SMALL LETTER CHI
        escapesec[968] = "&psi;"; //GREEK SMALL LETTER PSI
        escapesec[969] = "&omega;"; //GREEK SMALL LETTER OMEGA
        escapesec[977] = "&thetasym;"; //GREEK THETA SYMBOL
        escapesec[978] = "&upsih;"; //GREEK UPSILON WITH HOOK SYMBOL
        escapesec[982] = "&piv;"; //GREEK PI SYMBOL
        escapesec[8194] = "&ensp;"; //EN SPACE
        escapesec[8195] = "&emsp;"; //EM SPACE
        escapesec[8201] = "&thinsp;"; //THIN SPACE
        escapesec[8204] = "&zwnj;"; //ZERO WIDTH NON-JOINER
        escapesec[8205] = "&zwj;"; //ZERO WIDTH JOINER
        escapesec[8206] = "&lrm;"; //LEFT-TO-RIGHT MARK
        escapesec[8207] = "&rlm;"; //RIGHT-TO-LEFT MARK
        escapesec[8211] = "&ndash;"; //EN DASH
        escapesec[8212] = "&mdash;"; //EM DASH
        escapesec[8213] = "&horbar;"; //HORIZONTAL BAR
        escapesec[8216] = "&lsquo;"; //LEFT SINGLE QUOTATION MARK
        escapesec[8217] = "&rsquo;"; //RIGHT SINGLE QUOTATION MARK
        escapesec[8218] = "&sbquo;"; //SINGLE LOW-9 QUOTATION MARK
        escapesec[8220] = "&ldquo;"; //LEFT DOUBLE QUOTATION MARK
        escapesec[8221] = "&rdquo;"; //RIGHT DOUBLE QUOTATION MARK
        escapesec[8222] = "&bdquo;"; //DOUBLE LOW-9 QUOTATION MARK
        escapesec[8224] = "&dagger;"; //DAGGER
        escapesec[8225] = "&Dagger;"; //DOUBLE DAGGER
        escapesec[8226] = "&bull;"; //BULLET
        escapesec[8230] = "&hellip;"; //HORIZONTAL ELLIPSIS
        escapesec[8240] = "&permil;"; //PER MILLE SIGN
        escapesec[8242] = "&prime;"; //PRIME
        escapesec[8243] = "&Prime;"; //DOUBLE PRIME
        escapesec[8249] = "&lsaquo;"; //SINGLE LEFT-POINTING ANGLE QUOTATION MARK
        escapesec[8250] = "&rsaquo;"; //SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
        escapesec[8254] = "&oline;"; //OVERLINE
        escapesec[8260] = "&frasl;"; //FRACTION SLASH
        escapesec[8364] = "&euro;"; //EURO SIGN
        escapesec[8465] = "&image;"; //BLACK-LETTER CAPITAL I
        escapesec[8472] = "&weierp;"; //SCRIPT CAPITAL P
        escapesec[8476] = "&real;"; //BLACK-LETTER CAPITAL R
        escapesec[8482] = "&trade;"; //TRADE MARK SIGN
        escapesec[8501] = "&alefsym;"; //ALEF SYMBOL
        escapesec[8592] = "&larr;"; //LEFTWARDS ARROW
        escapesec[8593] = "&uarr;"; //UPWARDS ARROW
        escapesec[8594] = "&rarr;"; //RIGHTWARDS ARROW
        escapesec[8595] = "&darr;"; //DOWNWARDS ARROW
        escapesec[8596] = "&harr;"; //LEFT RIGHT ARROW
        escapesec[8629] = "&crarr;"; //DOWNWARDS ARROW WITH CORNER LEFTWARDS
        escapesec[8656] = "&lArr;"; //LEFTWARDS DOUBLE ARROW
        escapesec[8657] = "&uArr;"; //UPWARDS DOUBLE ARROW
        escapesec[8658] = "&rArr;"; //RIGHTWARDS DOUBLE ARROW
        escapesec[8659] = "&dArr;"; //DOWNWARDS DOUBLE ARROW
        escapesec[8660] = "&hArr;"; //LEFT RIGHT DOUBLE ARROW
        escapesec[8704] = "&forall;"; //FOR ALL
        escapesec[8706] = "&part;"; //PARTIAL DIFFERENTIAL
        escapesec[8707] = "&exist;"; //THERE EXISTS
        escapesec[8709] = "&empty;"; //EMPTY SET
        escapesec[8711] = "&nabla;"; //NABLA
        escapesec[8712] = "&isin;"; //ELEMENT OF
        escapesec[8713] = "&notin;"; //NOT AN ELEMENT OF
        escapesec[8715] = "&ni;"; //CONTAINS AS MEMBER
        escapesec[8719] = "&prod;"; //N-ARY PRODUCT
        escapesec[8721] = "&sum;"; //N-ARY SUMMATION
        escapesec[8722] = "&minus;"; //MINUS SIGN
        escapesec[8727] = "&lowast;"; //ASTERISK OPERATOR
        escapesec[8730] = "&radic;"; //SQUARE ROOT
        escapesec[8733] = "&prop;"; //PROPORTIONAL TO
        escapesec[8734] = "&infin;"; //INFINITY
        escapesec[8736] = "&ang;"; //ANGLE
        escapesec[8743] = "&and;"; //LOGICAL AND
        escapesec[8744] = "&or;"; //LOGICAL OR
        escapesec[8745] = "&cap;"; //INTERSECTION
        escapesec[8746] = "&cup;"; //UNION
        escapesec[8747] = "&int;"; //INTEGRAL
        escapesec[8756] = "&there4;"; //THEREFORE
        escapesec[8764] = "&sim;"; //TILDE OPERATOR
        escapesec[8773] = "&cong;"; //APPROXIMATELY EQUAL TO
        escapesec[8776] = "&asymp;"; //ALMOST EQUAL TO
        escapesec[8800] = "&ne;"; //NOT EQUAL TO
        escapesec[8801] = "&equiv;"; //IDENTICAL TO
        escapesec[8804] = "&le;"; //LESS-THAN OR EQUAL TO
        escapesec[8805] = "&ge;"; //GREATER-THAN OR EQUAL TO
        escapesec[8834] = "&sub;"; //SUBSET OF
        escapesec[8835] = "&sup;"; //SUPERSET OF
        escapesec[8836] = "&nsub;"; //NOT A SUBSET OF
        escapesec[8838] = "&sube;"; //SUBSET OF OR EQUAL TO
        escapesec[8839] = "&supe;"; //SUPERSET OF OR EQUAL TO
        escapesec[8853] = "&oplus;"; //CIRCLED PLUS
        escapesec[8855] = "&otimes;"; //CIRCLED TIMES
        escapesec[8869] = "&perp;"; //UP TACK
        escapesec[8901] = "&sdot;"; //DOT OPERATOR
        escapesec[8968] = "&lceil;"; //LEFT CEILING
        escapesec[8969] = "&rceil;"; //RIGHT CEILING
        escapesec[8970] = "&lfloor;"; //LEFT FLOOR
        escapesec[8971] = "&rfloor;"; //RIGHT FLOOR
        escapesec[9001] = "&lang;"; //LEFT-POINTING ANGLE BRACKET
        escapesec[9002] = "&rang;"; //RIGHT-POINTING ANGLE BRACKET
        escapesec[9674] = "&loz;"; //LOZENGE
        escapesec[9824] = "&spades;"; //BLACK SPADE SUIT
        escapesec[9827] = "&clubs;"; //BLACK CLUB SUIT
        escapesec[9829] = "&hearts;"; //BLACK HEART SUIT
        escapesec[9830] = "&diams;"; //BLACK DIAMOND SUIT

        unintitialized = false;
    }
    if (escapesec.find(c) != escapesec.end())
    {
        return escapesec[c];
    }

    return string(1,c);
}

string webdavnameescape(const string &value) {
    ostringstream escaped;

    for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i)
    {
        escaped << escapewebdavchar(*i);
    }

    return escaped.str();
}

void tolower_string(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](char c) {return static_cast<char>(::tolower(c)); });
}

#ifdef __APPLE__
int macOSmajorVersion()
{
    char releaseStr[256];
    size_t size = sizeof(releaseStr);
    if (!sysctlbyname("kern.osrelease", releaseStr, &size, NULL, 0)  && size > 0)
    {
        if (strchr(releaseStr,'.'))
        {
            char *token = strtok(releaseStr, ".");
            if (token)
            {
                errno = 0;
                char *endPtr = NULL;
                long majorVersion = strtol(token, &endPtr, 10);
                if (endPtr != token && errno != ERANGE && majorVersion >= INT_MIN && majorVersion <= INT_MAX)
                {
                    return int(majorVersion);
                }
            }
        }
    }

    return -1;
}
#endif

CacheableStatus::CacheableStatus(mega::CacheableStatus::Type type, int64_t value)
    : mType(type)
    , mValue(value)
{ }


CacheableStatus* CacheableStatus::unserialize(class MegaClient *client, const std::string& data)
{
    int64_t typeBuf;
    int64_t value;

    CacheableReader reader(data);
    if (!reader.unserializei64(typeBuf))
    {
        return nullptr;
    }
    if (!reader.unserializei64(value))
    {
        return nullptr;
    }

    CacheableStatus::Type type = static_cast<CacheableStatus::Type>(typeBuf);
    client->mCachedStatus.loadCachedStatus(type, value);
    return client->mCachedStatus.getPtr(type);
}

bool CacheableStatus::serialize(std::string* data) const
{
    CacheableWriter writer{*data};
    writer.serializei64(mType);
    writer.serializei64(mValue);
    return true;
}

int64_t CacheableStatus::value() const
{
    return mValue;
}

CacheableStatus::Type CacheableStatus::type() const
{
    return mType;
}

void CacheableStatus::setValue(const int64_t value)
{
    mValue = value;
}

std::string CacheableStatus::typeToStr()
{
    return CacheableStatus::typeToStr(mType);
}

std::string CacheableStatus::typeToStr(CacheableStatus::Type type)
{
    switch (type)
    {
    case STATUS_UNKNOWN:
        return "unknown";
    case STATUS_STORAGE:
        return "storage";
    case STATUS_BUSINESS:
        return "business";
    case STATUS_BLOCKED:
        return "blocked";
    case STATUS_PRO_LEVEL:
        return "pro-level";
    default:
        return "undefined";
    }
}

std::pair<bool, int64_t> generateMetaMac(SymmCipher &cipher, FileAccess &ifAccess, const int64_t iv)
{
    FileInputStream isAccess(&ifAccess);

    return generateMetaMac(cipher, isAccess, iv);
}

std::pair<bool, int64_t> generateMetaMac(SymmCipher &cipher, InputStreamAccess &isAccess, const int64_t iv)
{
    static const unsigned int SZ_1024K = 1l << 20;
    static const unsigned int SZ_128K  = 128l << 10;

    auto buffer = std::make_unique<byte[]>(SZ_1024K + SymmCipher::BLOCKSIZE);
    chunkmac_map chunkMacs;
    unsigned int chunkLength = 0;
    m_off_t current = 0;
    m_off_t remaining = isAccess.size();

    while (remaining > 0)
    {
        chunkLength =
          std::min(chunkLength + SZ_128K,
                   static_cast<unsigned int>(std::min<m_off_t>(remaining, SZ_1024K)));

        if (!isAccess.read(&buffer[0], chunkLength))
            return std::make_pair(false, 0l);

        memset(&buffer[chunkLength], 0, SymmCipher::BLOCKSIZE);

        chunkMacs.ctr_encrypt(current, &cipher, buffer.get(), chunkLength, current, iv, true);

        current += chunkLength;
        remaining -= chunkLength;
    }

    return std::make_pair(true, chunkMacs.macsmac(&cipher));
}

bool CompareLocalFileMetaMacWithNodeKey(FileAccess* fa, const std::string& nodeKey, int type)
{
    SymmCipher cipher;
    const char* iva = &nodeKey[SymmCipher::KEYLENGTH];
    int64_t remoteIv = MemAccess::get<int64_t>(iva);
    int64_t remoteMac = MemAccess::get<int64_t>(iva + sizeof(int64_t));
    cipher.setkey((byte*)&nodeKey[0], type);
    auto result = generateMetaMac(cipher, *fa, remoteIv);
    return result.first && result.second == remoteMac;
}

bool CompareLocalFileMetaMacWithNode(FileAccess* fa, Node* node)
{
    return CompareLocalFileMetaMacWithNodeKey(fa, node->nodekey(), node->type);
}

void MegaClientAsyncQueue::push(std::function<void(SymmCipher&)> f, bool discardable)
{
    if (mThreads.empty())
    {
        if (f)
        {
            f(mZeroThreadsCipher);
        }
    }
    else
    {
        {
            std::lock_guard<std::mutex> g(mMutex);
            mQueue.emplace_back(discardable, std::move(f));
        }
        mConditionVariable.notify_one();
    }
}

MegaClientAsyncQueue::MegaClientAsyncQueue(Waiter& w, unsigned threadCount)
    : mWaiter(w)
{
    for (int i = threadCount; i--; )
    {
        try
        {
            mThreads.emplace_back([this]()
            {
                asyncThreadLoop();
            });
        }
        catch (std::system_error& e)
        {
            LOG_err << "Failed to start worker thread: " << e.what();
            break;
        }
    }
    LOG_debug << "MegaClient Worker threads running: " << mThreads.size();
}

MegaClientAsyncQueue::~MegaClientAsyncQueue()
{
    clearDiscardable();
    push(nullptr, false);
    mConditionVariable.notify_all();
    LOG_warn << "~MegaClientAsyncQueue() joining threads";
    for (auto& t : mThreads)
    {
        t.join();
    }
    LOG_warn << "~MegaClientAsyncQueue() ends";
}

void MegaClientAsyncQueue::clearDiscardable()
{
    std::lock_guard<std::mutex> g(mMutex);
    auto newEnd = std::remove_if(mQueue.begin(), mQueue.end(), [](Entry& entry){ return entry.discardable; });
    mQueue.erase(newEnd, mQueue.end());
}

void MegaClientAsyncQueue::asyncThreadLoop()
{
    SymmCipher cipher;
    for (;;)
    {
        std::function<void(SymmCipher&)> f;
        {
            std::unique_lock<std::mutex> g(mMutex);
            mConditionVariable.wait(g, [this]() { return !mQueue.empty(); });
            assert(!mQueue.empty());
            f = std::move(mQueue.front().f);
            if (!f) return;   // nullptr is not popped, and causes all the threads to exit
            mQueue.pop_front();
        }
        f(cipher);
        mWaiter.notify();
    }
}

bool islchex_high(const int c)
{
    // this one constrains two characters to the 0..127 range
    return (c >= '0' && c <= '7');
}

bool islchex_low(const int c)
{
    // this one is the low nibble, unconstrained
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

std::string getSafeUrl(const std::string &posturl)
{
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ <= 4
    string safeurl;
    safeurl.append(posturl);
#else
    string safeurl = posturl;
#endif
    size_t sid = safeurl.find("sid=");
    if (sid != string::npos)
    {
        sid += 4;
        size_t end = safeurl.find("&", sid);
        if (end == string::npos)
        {
            end = safeurl.size();
        }
        safeurl.replace(sid, end - sid, end - sid, 'X');
    }
    size_t authKey = safeurl.find("&n=");
    if (authKey != string::npos)
    {
        authKey += 3/*&n=*/ + 8/*public handle*/;
        size_t end = safeurl.find("&", authKey);
        if (end == string::npos)
        {
            end = safeurl.size();
        }
        safeurl.replace(authKey, end - authKey, end - authKey, 'X');
    }
    return safeurl;
}

bool readLines(FileAccess& ifAccess, string_vector& destination)
{
    FileInputStream isAccess(&ifAccess);
    return readLines(isAccess, destination);
}

bool readLines(InputStreamAccess& isAccess, string_vector& destination)
{
    const auto length = static_cast<unsigned int>(isAccess.size());

    std::string input(length, '\0');

    return isAccess.read((byte*)input.data(), length)
           && readLines(input, destination);
}

bool readLines(const std::string& input, string_vector& destination)
{
    const char *current = input.data();
    const char *end = current + input.size();

    // we assume utf8.  Skip the BOM if there is one
    if (input.size() > 2 &&
        static_cast<unsigned char>(current[0]) == 0xEF &&
        static_cast<unsigned char>(current[1]) == 0xBB &&
        static_cast<unsigned char>(current[2]) == 0xBF)
    {
        current += 3;
    }

    while (current < end && (*current == '\r' || *current == '\n'))
    {
        ++current;
    }

    while (current < end)
    {
        const char *delim = current;
        const char *whitespace = current;

        while (delim < end && *delim != '\r' && *delim != '\n')
        {
            ++delim;
            whitespace += is_space(*whitespace);
        }

        if (delim != whitespace)
        {
            destination.emplace_back(current, delim);
        }

        while (delim < end && (*delim == '\r' || *delim == '\n'))
        {
            ++delim;
        }

        current = delim;
    }

    return true;
}

bool wildcardMatch(const string& text, const string& pattern)
{
    return wildcardMatch(text.c_str(), pattern.c_str());
}

bool wildcardMatch(const char *pszString, const char *pszMatch)
//  cf. http://www.planet-source-code.com/vb/scripts/ShowCode.asp?txtCodeId=1680&lngWId=3
{
    const char *cp = nullptr;
    const char *mp = nullptr;

    while ((*pszString) && (*pszMatch != '*'))
    {
        if ((*pszMatch != *pszString) && (*pszMatch != '?'))
        {
            return false;
        }
        pszMatch++;
        pszString++;
    }

    while (*pszString)
    {
        if (*pszMatch == '*')
        {
            if (!*++pszMatch)
            {
                return true;
            }
            mp = pszMatch;
            cp = pszString + 1;
        }
        else if ((*pszMatch == *pszString) || (*pszMatch == '?'))
        {
            pszMatch++;
            pszString++;
        }
        else
        {
            pszMatch = mp;
            pszString = cp++;
        }
    }
    while (*pszMatch == '*')
    {
        pszMatch++;
    }
    return !*pszMatch;
}

const char* syncWaitReasonDebugString(SyncWaitReason r)
{
    switch(r)
    {
        case SyncWaitReason::NoReason:                                      return "NoReason";
        case SyncWaitReason::FileIssue:                                     return "FileIssue";
        case SyncWaitReason::MoveOrRenameCannotOccur:                       return "MoveOrRenameCannotOccur";
        case SyncWaitReason::DeleteOrMoveWaitingOnScanning:                 return "DeleteOrMoveWaitingOnScanning";
        case SyncWaitReason::DeleteWaitingOnMoves:                          return "DeleteWaitingOnMoves";
        case SyncWaitReason::UploadIssue:                                   return "UploadIssue";
        case SyncWaitReason::DownloadIssue:                                 return "DownloadIssue";
        case SyncWaitReason::CannotCreateFolder:                            return "CannotCreateFolder";
        case SyncWaitReason::CannotPerformDeletion:                         return "CannotPerformDeletion";
        case SyncWaitReason::SyncItemExceedsSupportedTreeDepth:             return "SyncItemExceedsSupportedTreeDepth";
        case SyncWaitReason::FolderMatchedAgainstFile:                      return "FolderMatchedAgainstFile";
        case SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose: return "BothChangedSinceLastSynced";
        case SyncWaitReason::LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose: return "LocalAndRemotePreviouslyUnsyncedDiffer";
        case SyncWaitReason::NamesWouldClashWhenSynced:                     return "NamesWouldClashWhenSynced";

        case SyncWaitReason::SyncWaitReason_LastPlusOne: break;
    }
    return "<out of range>";
}

const char* syncPathProblemDebugString(PathProblem r)
{
    switch (r)
    {
    case PathProblem::NoProblem: return "NoProblem";
    case PathProblem::FileChangingFrequently: return "FileChangingFrequently";
    case PathProblem::IgnoreRulesUnknown: return "IgnoreRulesUnknown";
    case PathProblem::DetectedHardLink: return "DetectedHardLink";
    case PathProblem::DetectedSymlink: return "DetectedSymlink";
    case PathProblem::DetectedSpecialFile: return "DetectedSpecialFile";
    case PathProblem::DifferentFileOrFolderIsAlreadyPresent: return "DifferentFileOrFolderIsAlreadyPresent";
    case PathProblem::ParentFolderDoesNotExist: return "ParentFolderDoesNotExist";
    case PathProblem::FilesystemErrorDuringOperation: return "FilesystemErrorDuringOperation";
    case PathProblem::NameTooLongForFilesystem: return "NameTooLongForFilesystem";
    case PathProblem::CannotFingerprintFile: return "CannotFingerprintFile";
    case PathProblem::DestinationPathInUnresolvedArea: return "DestinationPathInUnresolvedArea";
    case PathProblem::MACVerificationFailure: return "MACVerificationFailure";
    case PathProblem::DeletedOrMovedByUser: return "DeletedOrMovedByUser";
    case PathProblem::FileFolderDeletedByUser: return "FileFolderDeletedByUser";
    case PathProblem::MoveToDebrisFolderFailed: return "MoveToDebrisFolderFailed";
    case PathProblem::IgnoreFileMalformed: return "IgnoreFileMalformed";
    case PathProblem::FilesystemErrorListingFolder: return "FilesystemErrorListingFolder";
    case PathProblem::FilesystemErrorIdentifyingFolderContent: return "FilesystemErrorIdentifyingFolderContent";
    case PathProblem::UndecryptedCloudNode: return "UndecryptedCloudNode";
    case PathProblem::WaitingForScanningToComplete: return "WaitingForScanningToComplete";
    case PathProblem::WaitingForAnotherMoveToComplete: return "WaitingForAnotherMoveToComplete";
    case PathProblem::SourceWasMovedElsewhere: return "SourceWasMovedElsewhere";
    case PathProblem::FilesystemCannotStoreThisName: return "FilesystemCannotStoreThisName";
    case PathProblem::CloudNodeInvalidFingerprint: return "CloudNodeInvalidFingerprint";

    case PathProblem::PutnodeDeferredByController: return "PutnodeDeferredByController";
    case PathProblem::PutnodeCompletionDeferredByController: return "PutnodeCompletionDeferredByController";
    case PathProblem::PutnodeCompletionPending: return "PutnodeCompletionPending";
    case PathProblem::UploadDeferredByController: return "UploadDeferredByController";

    case PathProblem::DetectedNestedMount: return "DetectedNestedMount";

    case PathProblem::PathProblem_LastPlusOne: break;
    }
    return "<out of range>";
};

UploadHandle UploadHandle::next()
{
    do
    {
        // Since we start with UNDEF, the first update would overwrite the whole handle and at least 1 byte further, causing data corruption
        if (h == UNDEF) h = 0;

        byte* ptr = (byte*)(&h + 1);

        while (!++*--ptr);
    }
    while ((h & 0xFFFF000000000000) == 0 || // if the top two bytes were all 0 then it could clash with NodeHandles
            h == UNDEF);


    return *this;
}

handle generateDriveId(PrnGen& rng)
{
    handle driveId;

    rng.genblock((byte *)&driveId, sizeof(driveId));
    driveId |= m_time(nullptr);

    return driveId;
}

error readDriveId(FileSystemAccess& fsAccess, const char* pathToDrive, handle& driveId)
{
    if (pathToDrive && strlen(pathToDrive))
        return readDriveId(fsAccess, LocalPath::fromAbsolutePath(pathToDrive), driveId);

    driveId = UNDEF;

    return API_EREAD;
}

error readDriveId(FileSystemAccess& fsAccess, const LocalPath& pathToDrive, handle& driveId)
{
    assert(!pathToDrive.empty());

    driveId = UNDEF;

    auto path = pathToDrive;

    path.appendWithSeparator(LocalPath::fromRelativePath(".megabackup"), false);
    path.appendWithSeparator(LocalPath::fromRelativePath("drive-id"), false);

    auto fileAccess = fsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, true, false, FSLogging::logExceptFileNotFound))
    {
        // This case is valid when only checking for file existence
        return API_ENOENT;
    }

    if (!fileAccess->frawread((byte*)&driveId, sizeof(driveId), 0, false, FSLogging::logOnError))
    {
        LOG_err << "Unable to read drive-id from file: " << path;
        return API_EREAD;
    }

    return API_OK;
}

error writeDriveId(FileSystemAccess& fsAccess, const char* pathToDrive, handle driveId)
{
    auto path = LocalPath::fromAbsolutePath(pathToDrive);

    path.appendWithSeparator(LocalPath::fromRelativePath(".megabackup"), false);

    // Try and create the backup configuration directory
    if (!(fsAccess.mkdirlocal(path, false, false) || fsAccess.target_exists))
    {
        LOG_err << "Unable to create config DB directory: " << path;

        // Couldn't create the directory and it doesn't exist.
        return API_EWRITE;
    }

    path.appendWithSeparator(LocalPath::fromRelativePath("drive-id"), false);

    // Open the file for writing
    auto fileAccess = fsAccess.newfileaccess(false);
    if (!fileAccess->fopen(path, false, true, FSLogging::logOnError))
    {
        LOG_err << "Unable to open file to write drive-id: " << path;
        return API_EWRITE;
    }

    // Write the drive-id to file
    if (!fileAccess->fwrite((byte*)&driveId, sizeof(driveId), 0))
    {
        LOG_err << "Unable to write drive-id to file: " << path;
        return API_EWRITE;
    }

    return API_OK;
}

int platformGetRLimitNumFile()
{
#ifndef WIN32
    struct rlimit rl{0,0};
    if (0 < getrlimit(RLIMIT_NOFILE, &rl))
    {
        auto e = errno;
        LOG_err << "Error calling getrlimit: " << e;
        return -1;
    }

    return int(rl.rlim_cur);
#else
    LOG_err << "Code for calling getrlimit is not available yet (or not relevant) on this platform";
    return -1;
#endif
}

bool platformSetRLimitNumFile(int newNumFileLimit)
{
#ifndef WIN32
    struct rlimit rl{0,0};
    if (0 < getrlimit(RLIMIT_NOFILE, &rl))
    {
        auto e = errno;
        LOG_err << "Error calling getrlimit: " << e;
        return false;
    }
    else
    {
        LOG_info << "rlimit for NOFILE before change is: " << rl.rlim_cur << ", " << rl.rlim_max;

        if (newNumFileLimit < 0)
        {
            rl.rlim_cur = rl.rlim_max;
        }
        else
        {
            rl.rlim_cur = rlim_t(newNumFileLimit);

            if (rl.rlim_cur > rl.rlim_max)
            {
                LOG_info << "Requested rlimit (" << newNumFileLimit << ") will be replaced by maximum allowed value (" << rl.rlim_max << ")";
                rl.rlim_cur = rl.rlim_max;
            }
        }

        if (0 < setrlimit(RLIMIT_NOFILE, &rl))
        {
            auto e = errno;
            LOG_err << "Error calling setrlimit: " << e;
            return false;
        }
        else
        {
            LOG_info << "rlimit for NOFILE is: " << rl.rlim_cur;
        }
    }
    return true;
#else
    LOG_err << "Code for calling setrlimit is not available yet (or not relevant) on this platform";
    return false;
#endif
}

void debugLogHeapUsage()
{
#ifdef DEBUG
#ifdef WIN32
    _CrtMemState state;
    _CrtMemCheckpoint(&state);

    LOG_debug << "MEM use.  Heap: " << state.lTotalCount << " highwater: " << state.lHighWaterCount
        << " _FREE_BLOCK/" << state.lCounts[_FREE_BLOCK] << "/" << state.lSizes[_FREE_BLOCK]
        << " _NORMAL_BLOCK/" << state.lCounts[_NORMAL_BLOCK] << "/" << state.lSizes[_NORMAL_BLOCK]
        << " _CRT_BLOCK/" << state.lCounts[_CRT_BLOCK] << "/" << state.lSizes[_CRT_BLOCK]
        << " _IGNORE_BLOCK/" << state.lCounts[_IGNORE_BLOCK] << "/" << state.lSizes[_IGNORE_BLOCK]
        << " _CLIENT_BLOCK/" << state.lCounts[_CLIENT_BLOCK] << "/" << state.lSizes[_CLIENT_BLOCK];
#endif
#endif
}

bool haveDuplicatedValues(const string_map& readableVals, const string_map& b64Vals)
{
    return
        any_of(readableVals.begin(), readableVals.end(), [&b64Vals](const string_map::value_type& p1)
            {
                return any_of(b64Vals.begin(), b64Vals.end(), [&p1](const string_map::value_type& p2)
                    {
                        return p1.first != p2.first && p1.second == Base64::atob(p2.second);
                    });
            });
}

void SyncTransferCount::operator-=(const SyncTransferCount& rhs)
{
    mCompleted -= rhs.mCompleted;
    mCompletedBytes -= rhs.mCompletedBytes;
    mPending -= rhs.mPending;
    mPendingBytes -= rhs.mPendingBytes;
}

bool SyncTransferCount::operator==(const SyncTransferCount& rhs) const
{
    return mCompleted == rhs.mCompleted
        && mCompletedBytes == rhs.mCompletedBytes
        && mPending == rhs.mPending
        && mPendingBytes == rhs.mPendingBytes;
}

bool SyncTransferCount::operator!=(const SyncTransferCount& rhs) const
{
    return !(*this == rhs);
}

void SyncTransferCounts::operator-=(const SyncTransferCounts& rhs)
{
    mDownloads -= rhs.mDownloads;
    mUploads -= rhs.mUploads;
}

bool SyncTransferCounts::operator==(const SyncTransferCounts& rhs) const
{
    return mDownloads == rhs.mDownloads && mUploads == rhs.mUploads;
}

bool SyncTransferCounts::operator!=(const SyncTransferCounts& rhs) const
{
    return !(*this == rhs);
}

double SyncTransferCounts::progress(m_off_t inflightProgress) const
{
    auto pending = mDownloads.mPendingBytes + mUploads.mPendingBytes;

    if (!pending)
        return 1.0; // 100%

    auto completed = mDownloads.mCompletedBytes + mUploads.mCompletedBytes + inflightProgress;
    auto progress = static_cast<double>(completed) / static_cast<double>(pending);

    return std::min(1.0, progress);
}

#ifdef WIN32

// get the Windows error message in UTF-8
std::string winErrorMessage(DWORD error)
{
    if (error == 0xFFFFFFFF)
        error = GetLastError();

    LPWSTR lpMsgBuf = nullptr;
    if (!FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPWSTR)&lpMsgBuf, // FORMAT_MESSAGE_ALLOCATE_BUFFER treats the buffer like a pointer
        0,
        NULL))
    {
        // Handle the error.
        return "[Unknown error " + std::to_string(error) + "]";
    }

    std::wstring wstr(lpMsgBuf);
    // Free the buffer.

    LocalFree(lpMsgBuf);

    std::string r;
    LocalPath::local2path(&wstr, &r, false);

    // remove trailing \r\n
    return Utils::trim(r);
}

void reportWindowsError(const std::string& message, DWORD error) {

    if (error == 0xFFFFFFFF)
        error = GetLastError();
    // in case streaming touches the operating system

    LOG_err << message << ": " << error << ": " << winErrorMessage(error);
}

#else

void reportError(const std::string& message, int aerrno) {

    if (aerrno == -1)
        aerrno = errno;
    // in case streaming touches the operating system

    LOG_err << message << ": " << aerrno << ": " << strerror(aerrno);
}

#endif

string connDirectionToStr(mega::direction_t directionType)
{
    switch (directionType)
    {
        case GET:
            return "GET";
        case PUT:
            return "PUT";
        case API:
            return "API";
        default:
            return "UNKNOWN";
    }
}

const char* toString(retryreason_t reason)
{
    switch (reason)
    {
#define DEFINE_RETRY_CLAUSE(index, name) case name: return #name;
        DEFINE_RETRY_REASONS(DEFINE_RETRY_CLAUSE)
#undef DEFINE_RETRY_CLAUSE
    }

    assert(false && "Unknown retry reason");

    return "RETRY_UNKNOWN";
}


bool is_space(unsigned int ch)
{
    return std::isspace(static_cast<unsigned char>(ch));
}

bool is_digit(unsigned int ch)
{
    return std::isdigit(static_cast<unsigned char>(ch));
}

std::set<std::string> splitString(const string& str, char delimiter)
{
    std::set<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter))
    {
        tokens.insert(token);
    }

    return tokens;
}

// Get the current process ID
unsigned long getCurrentPid()
{
#ifdef WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

} // namespace mega

