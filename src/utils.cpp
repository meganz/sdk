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

#if defined(_WIN32) && defined(_MSC_VER)
#include <sys/timeb.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace mega {

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

string toHandle(handle h)
{
    char base64Handle[14];
    Base64::btoa((byte*)&(h), sizeof h, base64Handle);
    return string(base64Handle);
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

void CacheableWriter::serializecompressed64(int64_t field)
{
    byte buf[sizeof field+1];
    dest.append((const char*)buf, Serialize64::serialize(buf, field));
}

void CacheableWriter::serializei64(int64_t field)
{
    dest.append((char*)&field, sizeof(field));
}

void CacheableWriter::serializeu32(uint32_t field)
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

void CacheableWriter::serializefsfp(fsfp_t field)
{
    dest.append((char*)&field, sizeof(field));
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
        (npos - pos) <= maxReqSize &&
        (it == mMacMap.end() || it->second.notStarted());
        it = mMacMap.find(npos))
    {
        npos = ChunkedHash::chunkceil(npos, fileSize);
    }
    return npos;
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

bool CacheableReader::unserializecompressed64(uint64_t& field)
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

bool CacheableReader::unserializefsfp(fsfp_t& field)
{
    if (ptr + sizeof(fsfp_t) > end)
    {
        return false;
    }
    field = MemAccess::get<fsfp_t>(ptr);
    ptr += sizeof(fsfp_t);
    fieldnum += 1;
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

#ifdef ENABLE_CHAT
TextChat::TextChat()
{
    id = UNDEF;
    priv = PRIV_UNKNOWN;
    shard = -1;
    userpriv = NULL;
    group = false;
    ou = UNDEF;
    resetTag();
    ts = 0;
    flags = 0;
    publicchat = false;

    memset(&changed, 0, sizeof(changed));
}

TextChat::~TextChat()
{
    delete userpriv;
}

bool TextChat::serialize(string *d)
{
    unsigned short ll;

    d->append((char*)&id, sizeof id);
    d->append((char*)&priv, sizeof priv);
    d->append((char*)&shard, sizeof shard);

    ll = (unsigned short)(userpriv ? userpriv->size() : 0);
    d->append((char*)&ll, sizeof ll);
    if (userpriv)
    {
        userpriv_vector::iterator it = userpriv->begin();
        while (it != userpriv->end())
        {
            handle uh = it->first;
            d->append((char*)&uh, sizeof uh);

            privilege_t priv = it->second;
            d->append((char*)&priv, sizeof priv);

            it++;
        }
    }

    d->append((char*)&group, sizeof group);

    // title is a binary array
    ll = (unsigned short)title.size();
    d->append((char*)&ll, sizeof ll);
    d->append(title.data(), ll);

    d->append((char*)&ou, sizeof ou);
    d->append((char*)&ts, sizeof(ts));

    char hasAttachments = attachedNodes.size() != 0;
    d->append((char*)&hasAttachments, 1);

    d->append((char*)&flags, 1);

    char mode = publicchat ? 1 : 0;
    d->append((char*)&mode, 1);

    char hasUnifiedKey = unifiedKey.size() ? 1 : 0;
    d->append((char *)&hasUnifiedKey, 1);

    char meetingRoom = meeting ? 1 : 0;
    d->append((char*)&meetingRoom, 1);

    d->append("\0\0\0\0\0", 5); // additional bytes for backwards compatibility

    if (hasAttachments)
    {
        ll = (unsigned short)attachedNodes.size();  // number of nodes with granted access
        d->append((char*)&ll, sizeof ll);

        for (attachments_map::iterator it = attachedNodes.begin(); it != attachedNodes.end(); it++)
        {
            d->append((char*)&it->first, sizeof it->first); // nodehandle

            ll = (unsigned short)it->second.size(); // number of users with granted access to the node
            d->append((char*)&ll, sizeof ll);
            for (set<handle>::iterator ituh = it->second.begin(); ituh != it->second.end(); ituh++)
            {
                d->append((char*)&(*ituh), sizeof *ituh);   // userhandle
            }
        }
    }

    if (hasUnifiedKey)
    {
        ll = (unsigned short) unifiedKey.size();
        d->append((char *)&ll, sizeof ll);
        d->append((char*) unifiedKey.data(), unifiedKey.size());
    }

    return true;
}

TextChat* TextChat::unserialize(class MegaClient *client, string *d)
{
    handle id;
    privilege_t priv;
    int shard;
    userpriv_vector *userpriv = NULL;
    bool group;
    string title;   // byte array
    handle ou;
    m_time_t ts;
    byte flags;
    char hasAttachments;
    attachments_map attachedNodes;
    bool publicchat;
    string unifiedKey;

    unsigned short ll;
    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(handle) + sizeof(privilege_t) + sizeof(int) + sizeof(short) > end)
    {
        return NULL;
    }

    id = MemAccess::get<handle>(ptr);
    ptr += sizeof id;

    priv = MemAccess::get<privilege_t>(ptr);
    ptr += sizeof priv;

    shard = MemAccess::get<int>(ptr);
    ptr += sizeof shard;

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof ll;
    if (ll)
    {
        if (ptr + ll * (sizeof(handle) + sizeof(privilege_t)) > end)
        {
            return NULL;
        }

        userpriv = new userpriv_vector();

        for (unsigned short i = 0; i < ll; i++)
        {
            handle uh = MemAccess::get<handle>(ptr);
            ptr += sizeof uh;

            privilege_t priv = MemAccess::get<privilege_t>(ptr);
            ptr += sizeof priv;

            userpriv->push_back(userpriv_pair(uh, priv));
        }

        if (priv == PRIV_RM)    // clear peerlist if removed
        {
            delete userpriv;
            userpriv = NULL;
        }
    }

    if (ptr + sizeof(bool) + sizeof(unsigned short) > end)
    {
        delete userpriv;
        return NULL;
    }

    group = MemAccess::get<bool>(ptr);
    ptr += sizeof group;

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof ll;
    if (ll)
    {
        if (ptr + ll > end)
        {
            delete userpriv;
            return NULL;
        }
        title.assign(ptr, ll);
    }
    ptr += ll;

    if (ptr + sizeof(handle) + sizeof(m_time_t) + sizeof(char) + 9 > end)
    {
        delete userpriv;
        return NULL;
    }

    ou = MemAccess::get<handle>(ptr);
    ptr += sizeof ou;

    ts = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof(m_time_t);

    hasAttachments = MemAccess::get<char>(ptr);
    ptr += sizeof hasAttachments;

    flags = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    char mode = MemAccess::get<char>(ptr);
    publicchat = (mode == 1);
    ptr += sizeof(char);

    char hasUnifiedKey = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    char meetingRoom = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    for (int i = 5; i--;)
    {
        if (ptr + MemAccess::get<unsigned char>(ptr) < end)
        {
            ptr += MemAccess::get<unsigned char>(ptr) + 1;
        }
    }

    if (hasAttachments)
    {
        unsigned short numNodes = 0;
        if (ptr + sizeof numNodes > end)
        {
            delete userpriv;
            return NULL;
        }

        numNodes = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof numNodes;

        for (int i = 0; i < numNodes; i++)
        {
            handle h = UNDEF;
            unsigned short numUsers = 0;
            if (ptr + sizeof h + sizeof numUsers > end)
            {
                delete userpriv;
                return NULL;
            }

            h = MemAccess::get<handle>(ptr);
            ptr += sizeof h;

            numUsers = MemAccess::get<unsigned short>(ptr);
            ptr += sizeof numUsers;

            handle uh = UNDEF;
            if (ptr + (numUsers * sizeof(uh)) > end)
            {
                delete userpriv;
                return NULL;
            }

            for (int j = 0; j < numUsers; j++)
            {
                uh = MemAccess::get<handle>(ptr);
                ptr += sizeof uh;

                attachedNodes[h].insert(uh);
            }
        }
    }

    if (hasUnifiedKey)
    {
        unsigned short keylen = 0;
        if (ptr + sizeof keylen > end)
        {
            delete userpriv;
            return NULL;
        }

        keylen = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof keylen;

        if (ptr + keylen > end)
        {
            delete userpriv;
            return NULL;
        }

        unifiedKey.assign(ptr, keylen);
        ptr += keylen;
    }

    if (ptr < end)
    {
        delete userpriv;
        return NULL;
    }

    if (client->chats.find(id) == client->chats.end())
    {
        client->chats[id] = new TextChat();
    }
    else
    {
        LOG_warn << "Unserialized a chat already in RAM";
    }
    TextChat* chat = client->chats[id];
    chat->id = id;
    chat->priv = priv;
    chat->shard = shard;
    chat->userpriv = userpriv;
    chat->group = group;
    chat->title = title;
    chat->ou = ou;
    chat->resetTag();
    chat->ts = ts;
    chat->flags = flags;
    chat->attachedNodes = attachedNodes;
    chat->publicchat = publicchat;
    chat->unifiedKey = unifiedKey;
    chat->meeting = meetingRoom;

    memset(&chat->changed, 0, sizeof(chat->changed));

    return chat;
}

void TextChat::setTag(int tag)
{
    if (this->tag != 0)    // external changes prevail
    {
        this->tag = tag;
    }
}

int TextChat::getTag()
{
    return tag;
}

void TextChat::resetTag()
{
    tag = -1;
}

bool TextChat::setNodeUserAccess(handle h, handle uh, bool revoke)
{
    if (revoke)
    {
        attachments_map::iterator uhit = attachedNodes.find(h);
        if (uhit != attachedNodes.end())
        {
            uhit->second.erase(uh);
            if (uhit->second.empty())
            {
                attachedNodes.erase(h);
                changed.attachments = true;
            }
            return true;
        }
    }
    else
    {
        attachedNodes[h].insert(uh);
        changed.attachments = true;
        return true;
    }

    return false;
}

bool TextChat::setFlags(byte newFlags)
{
    if (flags == newFlags)
    {
        return false;
    }

    flags = newFlags;
    changed.flags = true;

    return true;
}

bool TextChat::isFlagSet(uint8_t offset) const
{
    return (flags >> offset) & 1U;
}

bool TextChat::setMode(bool publicchat)
{
    if (this->publicchat == publicchat)
    {
        return false;
    }

    this->publicchat = publicchat;
    changed.mode = true;

    return true;
}

bool TextChat::setFlag(bool value, uint8_t offset)
{
    if (bool((flags >> offset) & 1U) == value)
    {
        return false;
    }

    flags ^= (1U << offset);
    changed.flags = true;

    return true;
}
#endif

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
 * @return Void.
 */
void PaddedCBC::encrypt(PrnGen &rng, string* data, SymmCipher* key, string* iv)
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
    if (iv)
    {
        key->cbc_encrypt((byte*)data->data(), data->size(),
                         (const byte*)iv->data());
    }
    else
    {
        key->cbc_encrypt((byte*)data->data(), data->size());
    }

    // Truncate IV back to the first 8 bytes only..
    if (iv)
    {
        iv->resize(8);
    }
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
 * @return Void.
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
    if (iv)
    {
        key->cbc_decrypt((byte*)data->data(), data->size(),
                         (const byte*)iv->data());
    }
    else
    {
        key->cbc_decrypt((byte*)data->data(), data->size());
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
    sym.cbc_encrypt_pkcs_padding(cleartext, iv, &encResult);

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
    keyString.resize(asym.key[AsymmCipher::PUB_PQ].ByteCount() - 2);

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

#ifdef _WIN32
int mega_snprintf(char *s, size_t n, const char *format, ...)
{
    va_list args;
    int ret;

    if (!s || n <= 0)
    {
        return -1;
    }

    va_start(args, format);
    ret = vsnprintf(s, n, format, args);
    va_end(args);

    s[n - 1] = '\0';
    return ret;
}
#endif

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
    string *container = tlvRecordsToContainer();

    // generate IV array
    byte *iv = new byte[ivlen];
    rng.genblock(iv, ivlen);

    string cipherText;

    // encrypt the bytes using the specified mode

    if (encMode == AES_MODE_CCM)   // CCM or GCM_BROKEN (same than CCM)
    {
        key->ccm_encrypt(container, iv, ivlen, taglen, &cipherText);
    }
    else if (encMode == AES_MODE_GCM)   // then use GCM
    {
        key->gcm_encrypt(container, iv, ivlen, taglen, &cipherText);
    }

    string *result = new string;
    result->resize(1);
    result->at(0) = static_cast<char>(encSetting);
    result->append((char*) iv, ivlen);
    result->append((char*) cipherText.data(), cipherText.length()); // includes auth. tag

    delete [] iv;
    delete container;

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

long long abs(long long n)
{
    // for pre-c++11 where this version is not defined yet
    return n >= 0 ? n : -n;
}

struct tm* m_localtime(m_time_t ttime, struct tm *dt)
{
    // works for 32 or 64 bit time_t
    time_t t = time_t(ttime);
#if (__cplusplus >= 201103L) && defined (__STDC_LIB_EXT1__) && defined(__STDC_WANT_LIB_EXT1__)
    localtime_s(&t, dt);
#elif _MSC_VER >= 1400 || defined(__MINGW32__) // MSVCRT (2005+): std::localtime is threadsafe
    struct tm *newtm = localtime(&t);
    if (newtm)
    {
        memcpy(dt, newtm, sizeof(struct tm));
    }
    else
    {
        memset(dt, 0, sizeof(struct tm));
    }
#elif _WIN32
#error "localtime is not thread safe in this compiler; please use a later one"
#else //POSIX
    localtime_r(&t, dt);
#endif
    return dt;
}

struct tm* m_gmtime(m_time_t ttime, struct tm *dt)
{
    // works for 32 or 64 bit time_t
    time_t t = time_t(ttime);
#if (__cplusplus >= 201103L) && defined (__STDC_LIB_EXT1__) && defined(__STDC_WANT_LIB_EXT1__)
    gmtime_s(&t, dt);
#elif _MSC_VER >= 1400 || defined(__MINGW32__) // MSVCRT (2005+): std::gmtime is threadsafe
    struct tm *newtm = gmtime(&t);
    if (newtm)
    {
        memcpy(dt, newtm, sizeof(struct tm));
    }
    else
    {
        memset(dt, 0, sizeof(struct tm));
    }
#elif _WIN32
#error "gmtime is not thread safe in this compiler; please use a later one"
#else //POSIX
    gmtime_r(&t, dt);
#endif
    return dt;
}

m_time_t m_time(m_time_t* tt)
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
        escapesec[0x000C6] = "&#x000C6;"; // AElig;
        escapesec[0x00026] = "&#x00026;"; // AMP;
        escapesec[0x000C1] = "&#x000C1;"; // Aacute;
        escapesec[0x00102] = "&#x00102;"; // Abreve;
        escapesec[0x000C2] = "&#x000C2;"; // Acirc;
        escapesec[0x00410] = "&#x00410;"; // Acy;
        escapesec[0x1D504] = "&#x1D504;"; // Afr;
        escapesec[0x000C0] = "&#x000C0;"; // Agrave;
        escapesec[0x00391] = "&#x00391;"; // Alpha;
        escapesec[0x00100] = "&#x00100;"; // Amacr;
        escapesec[0x02A53] = "&#x02A53;"; // And;
        escapesec[0x00104] = "&#x00104;"; // Aogon;
        escapesec[0x1D538] = "&#x1D538;"; // Aopf;
        escapesec[0x02061] = "&#x02061;"; // ApplyFunction;
        escapesec[0x000C5] = "&#x000C5;"; // Aring;
        escapesec[0x1D49C] = "&#x1D49C;"; // Ascr;
        escapesec[0x02254] = "&#x02254;"; // Assign;
        escapesec[0x000C3] = "&#x000C3;"; // Atilde;
        escapesec[0x000C4] = "&#x000C4;"; // Auml;
        escapesec[0x02216] = "&#x02216;"; // Backslash;
        escapesec[0x02AE7] = "&#x02AE7;"; // Barv;
        escapesec[0x02306] = "&#x02306;"; // Barwed;
        escapesec[0x00411] = "&#x00411;"; // Bcy;
        escapesec[0x02235] = "&#x02235;"; // Because;
        escapesec[0x0212C] = "&#x0212C;"; // Bernoullis;
        escapesec[0x00392] = "&#x00392;"; // Beta;
        escapesec[0x1D505] = "&#x1D505;"; // Bfr;
        escapesec[0x1D539] = "&#x1D539;"; // Bopf;
        escapesec[0x002D8] = "&#x002D8;"; // Breve;
        escapesec[0x0212C] = "&#x0212C;"; // Bscr;
        escapesec[0x0224E] = "&#x0224E;"; // Bumpeq;
        escapesec[0x00427] = "&#x00427;"; // CHcy;
        escapesec[0x000A9] = "&#x000A9;"; // COPY;
        escapesec[0x00106] = "&#x00106;"; // Cacute;
        escapesec[0x022D2] = "&#x022D2;"; // Cap;
        escapesec[0x02145] = "&#x02145;"; // CapitalDifferentialD;
        escapesec[0x0212D] = "&#x0212D;"; // Cayleys;
        escapesec[0x0010C] = "&#x0010C;"; // Ccaron;
        escapesec[0x000C7] = "&#x000C7;"; // Ccedil;
        escapesec[0x00108] = "&#x00108;"; // Ccirc;
        escapesec[0x02230] = "&#x02230;"; // Cconint;
        escapesec[0x0010A] = "&#x0010A;"; // Cdot;
        escapesec[0x000B8] = "&#x000B8;"; // Cedilla;
        escapesec[0x000B7] = "&#x000B7;"; // CenterDot;
        escapesec[0x0212D] = "&#x0212D;"; // Cfr;
        escapesec[0x003A7] = "&#x003A7;"; // Chi;
        escapesec[0x02299] = "&#x02299;"; // CircleDot;
        escapesec[0x02296] = "&#x02296;"; // CircleMinus;
        escapesec[0x02295] = "&#x02295;"; // CirclePlus;
        escapesec[0x02297] = "&#x02297;"; // CircleTimes;
        escapesec[0x02232] = "&#x02232;"; // ClockwiseContourIntegral;
        escapesec[0x0201D] = "&#x0201D;"; // CloseCurlyDoubleQuote;
        escapesec[0x02019] = "&#x02019;"; // CloseCurlyQuote;
        escapesec[0x02237] = "&#x02237;"; // Colon;
        escapesec[0x02A74] = "&#x02A74;"; // Colone;
        escapesec[0x02261] = "&#x02261;"; // Congruent;
        escapesec[0x0222F] = "&#x0222F;"; // Conint;
        escapesec[0x0222E] = "&#x0222E;"; // ContourIntegral;
        escapesec[0x02102] = "&#x02102;"; // Copf;
        escapesec[0x02210] = "&#x02210;"; // Coproduct;
        escapesec[0x02233] = "&#x02233;"; // CounterClockwiseContourIntegral;
        escapesec[0x02A2F] = "&#x02A2F;"; // Cross;
        escapesec[0x1D49E] = "&#x1D49E;"; // Cscr;
        escapesec[0x022D3] = "&#x022D3;"; // Cup;
        escapesec[0x0224D] = "&#x0224D;"; // CupCap;
        escapesec[0x02145] = "&#x02145;"; // DD;
        escapesec[0x02911] = "&#x02911;"; // DDotrahd;
        escapesec[0x00402] = "&#x00402;"; // DJcy;
        escapesec[0x00405] = "&#x00405;"; // DScy;
        escapesec[0x0040F] = "&#x0040F;"; // DZcy;
        escapesec[0x02021] = "&#x02021;"; // Dagger;
        escapesec[0x021A1] = "&#x021A1;"; // Darr;
        escapesec[0x02AE4] = "&#x02AE4;"; // Dashv;
        escapesec[0x0010E] = "&#x0010E;"; // Dcaron;
        escapesec[0x00414] = "&#x00414;"; // Dcy;
        escapesec[0x02207] = "&#x02207;"; // Del;
        escapesec[0x00394] = "&#x00394;"; // Delta;
        escapesec[0x1D507] = "&#x1D507;"; // Dfr;
        escapesec[0x000B4] = "&#x000B4;"; // DiacriticalAcute;
        escapesec[0x002D9] = "&#x002D9;"; // DiacriticalDot;
        escapesec[0x002DD] = "&#x002DD;"; // DiacriticalDoubleAcute;
        escapesec[0x00060] = "&#x00060;"; // DiacriticalGrave;
        escapesec[0x002DC] = "&#x002DC;"; // DiacriticalTilde;
        escapesec[0x022C4] = "&#x022C4;"; // Diamond;
        escapesec[0x02146] = "&#x02146;"; // DifferentialD;
        escapesec[0x1D53B] = "&#x1D53B;"; // Dopf;
        escapesec[0x000A8] = "&#x000A8;"; // Dot;
        escapesec[0x020DC] = "&#x020DC;"; // DotDot;
        escapesec[0x02250] = "&#x02250;"; // DotEqual;
        escapesec[0x0222F] = "&#x0222F;"; // DoubleContourIntegral;
        escapesec[0x000A8] = "&#x000A8;"; // DoubleDot;
        escapesec[0x021D3] = "&#x021D3;"; // DoubleDownArrow;
        escapesec[0x021D0] = "&#x021D0;"; // DoubleLeftArrow;
        escapesec[0x021D4] = "&#x021D4;"; // DoubleLeftRightArrow;
        escapesec[0x02AE4] = "&#x02AE4;"; // DoubleLeftTee;
        escapesec[0x027F8] = "&#x027F8;"; // DoubleLongLeftArrow;
        escapesec[0x027FA] = "&#x027FA;"; // DoubleLongLeftRightArrow;
        escapesec[0x027F9] = "&#x027F9;"; // DoubleLongRightArrow;
        escapesec[0x021D2] = "&#x021D2;"; // DoubleRightArrow;
        escapesec[0x022A8] = "&#x022A8;"; // DoubleRightTee;
        escapesec[0x021D1] = "&#x021D1;"; // DoubleUpArrow;
        escapesec[0x021D5] = "&#x021D5;"; // DoubleUpDownArrow;
        escapesec[0x02225] = "&#x02225;"; // DoubleVerticalBar;
        escapesec[0x02193] = "&#x02193;"; // DownArrow;
        escapesec[0x02913] = "&#x02913;"; // DownArrowBar;
        escapesec[0x021F5] = "&#x021F5;"; // DownArrowUpArrow;
        escapesec[0x00311] = "&#x00311;"; // DownBreve;
        escapesec[0x02950] = "&#x02950;"; // DownLeftRightVector;
        escapesec[0x0295E] = "&#x0295E;"; // DownLeftTeeVector;
        escapesec[0x021BD] = "&#x021BD;"; // DownLeftVector;
        escapesec[0x02956] = "&#x02956;"; // DownLeftVectorBar;
        escapesec[0x0295F] = "&#x0295F;"; // DownRightTeeVector;
        escapesec[0x021C1] = "&#x021C1;"; // DownRightVector;
        escapesec[0x02957] = "&#x02957;"; // DownRightVectorBar;
        escapesec[0x022A4] = "&#x022A4;"; // DownTee;
        escapesec[0x021A7] = "&#x021A7;"; // DownTeeArrow;
        escapesec[0x021D3] = "&#x021D3;"; // Downarrow;
        escapesec[0x1D49F] = "&#x1D49F;"; // Dscr;
        escapesec[0x00110] = "&#x00110;"; // Dstrok;
        escapesec[0x0014A] = "&#x0014A;"; // ENG;
        escapesec[0x000D0] = "&#x000D0;"; // ETH;
        escapesec[0x000C9] = "&#x000C9;"; // Eacute;
        escapesec[0x0011A] = "&#x0011A;"; // Ecaron;
        escapesec[0x000CA] = "&#x000CA;"; // Ecirc;
        escapesec[0x0042D] = "&#x0042D;"; // Ecy;
        escapesec[0x00116] = "&#x00116;"; // Edot;
        escapesec[0x1D508] = "&#x1D508;"; // Efr;
        escapesec[0x000C8] = "&#x000C8;"; // Egrave;
        escapesec[0x02208] = "&#x02208;"; // Element;
        escapesec[0x00112] = "&#x00112;"; // Emacr;
        escapesec[0x025FB] = "&#x025FB;"; // EmptySmallSquare;
        escapesec[0x025AB] = "&#x025AB;"; // EmptyVerySmallSquare;
        escapesec[0x00118] = "&#x00118;"; // Eogon;
        escapesec[0x1D53C] = "&#x1D53C;"; // Eopf;
        escapesec[0x00395] = "&#x00395;"; // Epsilon;
        escapesec[0x02A75] = "&#x02A75;"; // Equal;
        escapesec[0x02242] = "&#x02242;"; // EqualTilde;
        escapesec[0x021CC] = "&#x021CC;"; // Equilibrium;
        escapesec[0x02130] = "&#x02130;"; // Escr;
        escapesec[0x02A73] = "&#x02A73;"; // Esim;
        escapesec[0x00397] = "&#x00397;"; // Eta;
        escapesec[0x000CB] = "&#x000CB;"; // Euml;
        escapesec[0x02203] = "&#x02203;"; // Exists;
        escapesec[0x02147] = "&#x02147;"; // ExponentialE;
        escapesec[0x00424] = "&#x00424;"; // Fcy;
        escapesec[0x1D509] = "&#x1D509;"; // Ffr;
        escapesec[0x025FC] = "&#x025FC;"; // FilledSmallSquare;
        escapesec[0x025AA] = "&#x025AA;"; // FilledVerySmallSquare;
        escapesec[0x1D53D] = "&#x1D53D;"; // Fopf;
        escapesec[0x02200] = "&#x02200;"; // ForAll;
        escapesec[0x02131] = "&#x02131;"; // Fouriertrf;
        escapesec[0x00403] = "&#x00403;"; // GJcy;
        escapesec[0x0003E] = "&#x0003E;"; // GT;
        escapesec[0x00393] = "&#x00393;"; // Gamma;
        escapesec[0x003DC] = "&#x003DC;"; // Gammad;
        escapesec[0x0011E] = "&#x0011E;"; // Gbreve;
        escapesec[0x00122] = "&#x00122;"; // Gcedil;
        escapesec[0x0011C] = "&#x0011C;"; // Gcirc;
        escapesec[0x00413] = "&#x00413;"; // Gcy;
        escapesec[0x00120] = "&#x00120;"; // Gdot;
        escapesec[0x1D50A] = "&#x1D50A;"; // Gfr;
        escapesec[0x022D9] = "&#x022D9;"; // Gg;
        escapesec[0x1D53E] = "&#x1D53E;"; // Gopf;
        escapesec[0x02265] = "&#x02265;"; // GreaterEqual;
        escapesec[0x022DB] = "&#x022DB;"; // GreaterEqualLess;
        escapesec[0x02267] = "&#x02267;"; // GreaterFullEqual;
        escapesec[0x02AA2] = "&#x02AA2;"; // GreaterGreater;
        escapesec[0x02277] = "&#x02277;"; // GreaterLess;
        escapesec[0x02A7E] = "&#x02A7E;"; // GreaterSlantEqual;
        escapesec[0x02273] = "&#x02273;"; // GreaterTilde;
        escapesec[0x1D4A2] = "&#x1D4A2;"; // Gscr;
        escapesec[0x0226B] = "&#x0226B;"; // Gt;
        escapesec[0x0042A] = "&#x0042A;"; // HARDcy;
        escapesec[0x002C7] = "&#x002C7;"; // Hacek;
        escapesec[0x0005E] = "&#x0005E;"; // Hat;
        escapesec[0x00124] = "&#x00124;"; // Hcirc;
        escapesec[0x0210C] = "&#x0210C;"; // Hfr;
        escapesec[0x0210B] = "&#x0210B;"; // HilbertSpace;
        escapesec[0x0210D] = "&#x0210D;"; // Hopf;
        escapesec[0x02500] = "&#x02500;"; // HorizontalLine;
        escapesec[0x0210B] = "&#x0210B;"; // Hscr;
        escapesec[0x00126] = "&#x00126;"; // Hstrok;
        escapesec[0x0224E] = "&#x0224E;"; // HumpDownHump;
        escapesec[0x0224F] = "&#x0224F;"; // HumpEqual;
        escapesec[0x00415] = "&#x00415;"; // IEcy;
        escapesec[0x00132] = "&#x00132;"; // IJlig;
        escapesec[0x00401] = "&#x00401;"; // IOcy;
        escapesec[0x000CD] = "&#x000CD;"; // Iacute;
        escapesec[0x000CE] = "&#x000CE;"; // Icirc;
        escapesec[0x00418] = "&#x00418;"; // Icy;
        escapesec[0x00130] = "&#x00130;"; // Idot;
        escapesec[0x02111] = "&#x02111;"; // Ifr;
        escapesec[0x000CC] = "&#x000CC;"; // Igrave;
        escapesec[0x02111] = "&#x02111;"; // Im;
        escapesec[0x0012A] = "&#x0012A;"; // Imacr;
        escapesec[0x02148] = "&#x02148;"; // ImaginaryI;
        escapesec[0x021D2] = "&#x021D2;"; // Implies;
        escapesec[0x0222C] = "&#x0222C;"; // Int;
        escapesec[0x0222B] = "&#x0222B;"; // Integral;
        escapesec[0x022C2] = "&#x022C2;"; // Intersection;
        escapesec[0x02063] = "&#x02063;"; // InvisibleComma;
        escapesec[0x02062] = "&#x02062;"; // InvisibleTimes;
        escapesec[0x0012E] = "&#x0012E;"; // Iogon;
        escapesec[0x1D540] = "&#x1D540;"; // Iopf;
        escapesec[0x00399] = "&#x00399;"; // Iota;
        escapesec[0x02110] = "&#x02110;"; // Iscr;
        escapesec[0x00128] = "&#x00128;"; // Itilde;
        escapesec[0x00406] = "&#x00406;"; // Iukcy;
        escapesec[0x000CF] = "&#x000CF;"; // Iuml;
        escapesec[0x00134] = "&#x00134;"; // Jcirc;
        escapesec[0x00419] = "&#x00419;"; // Jcy;
        escapesec[0x1D50D] = "&#x1D50D;"; // Jfr;
        escapesec[0x1D541] = "&#x1D541;"; // Jopf;
        escapesec[0x1D4A5] = "&#x1D4A5;"; // Jscr;
        escapesec[0x00408] = "&#x00408;"; // Jsercy;
        escapesec[0x00404] = "&#x00404;"; // Jukcy;
        escapesec[0x00425] = "&#x00425;"; // KHcy;
        escapesec[0x0040C] = "&#x0040C;"; // KJcy;
        escapesec[0x0039A] = "&#x0039A;"; // Kappa;
        escapesec[0x00136] = "&#x00136;"; // Kcedil;
        escapesec[0x0041A] = "&#x0041A;"; // Kcy;
        escapesec[0x1D50E] = "&#x1D50E;"; // Kfr;
        escapesec[0x1D542] = "&#x1D542;"; // Kopf;
        escapesec[0x1D4A6] = "&#x1D4A6;"; // Kscr;
        escapesec[0x00409] = "&#x00409;"; // LJcy;
        escapesec[0x0003C] = "&#x0003C;"; // LT;
        escapesec[0x00139] = "&#x00139;"; // Lacute;
        escapesec[0x0039B] = "&#x0039B;"; // Lambda;
        escapesec[0x027EA] = "&#x027EA;"; // Lang;
        escapesec[0x02112] = "&#x02112;"; // Laplacetrf;
        escapesec[0x0219E] = "&#x0219E;"; // Larr;
        escapesec[0x0013D] = "&#x0013D;"; // Lcaron;
        escapesec[0x0013B] = "&#x0013B;"; // Lcedil;
        escapesec[0x0041B] = "&#x0041B;"; // Lcy;
        escapesec[0x027E8] = "&#x027E8;"; // LeftAngleBracket;
        escapesec[0x02190] = "&#x02190;"; // LeftArrow;
        escapesec[0x021E4] = "&#x021E4;"; // LeftArrowBar;
        escapesec[0x021C6] = "&#x021C6;"; // LeftArrowRightArrow;
        escapesec[0x02308] = "&#x02308;"; // LeftCeiling;
        escapesec[0x027E6] = "&#x027E6;"; // LeftDoubleBracket;
        escapesec[0x02961] = "&#x02961;"; // LeftDownTeeVector;
        escapesec[0x021C3] = "&#x021C3;"; // LeftDownVector;
        escapesec[0x02959] = "&#x02959;"; // LeftDownVectorBar;
        escapesec[0x0230A] = "&#x0230A;"; // LeftFloor;
        escapesec[0x02194] = "&#x02194;"; // LeftRightArrow;
        escapesec[0x0294E] = "&#x0294E;"; // LeftRightVector;
        escapesec[0x022A3] = "&#x022A3;"; // LeftTee;
        escapesec[0x021A4] = "&#x021A4;"; // LeftTeeArrow;
        escapesec[0x0295A] = "&#x0295A;"; // LeftTeeVector;
        escapesec[0x022B2] = "&#x022B2;"; // LeftTriangle;
        escapesec[0x029CF] = "&#x029CF;"; // LeftTriangleBar;
        escapesec[0x022B4] = "&#x022B4;"; // LeftTriangleEqual;
        escapesec[0x02951] = "&#x02951;"; // LeftUpDownVector;
        escapesec[0x02960] = "&#x02960;"; // LeftUpTeeVector;
        escapesec[0x021BF] = "&#x021BF;"; // LeftUpVector;
        escapesec[0x02958] = "&#x02958;"; // LeftUpVectorBar;
        escapesec[0x021BC] = "&#x021BC;"; // LeftVector;
        escapesec[0x02952] = "&#x02952;"; // LeftVectorBar;
        escapesec[0x021D0] = "&#x021D0;"; // Leftarrow;
        escapesec[0x021D4] = "&#x021D4;"; // Leftrightarrow;
        escapesec[0x022DA] = "&#x022DA;"; // LessEqualGreater;
        escapesec[0x02266] = "&#x02266;"; // LessFullEqual;
        escapesec[0x02276] = "&#x02276;"; // LessGreater;
        escapesec[0x02AA1] = "&#x02AA1;"; // LessLess;
        escapesec[0x02A7D] = "&#x02A7D;"; // LessSlantEqual;
        escapesec[0x02272] = "&#x02272;"; // LessTilde;
        escapesec[0x1D50F] = "&#x1D50F;"; // Lfr;
        escapesec[0x022D8] = "&#x022D8;"; // Ll;
        escapesec[0x021DA] = "&#x021DA;"; // Lleftarrow;
        escapesec[0x0013F] = "&#x0013F;"; // Lmidot;
        escapesec[0x027F5] = "&#x027F5;"; // LongLeftArrow;
        escapesec[0x027F7] = "&#x027F7;"; // LongLeftRightArrow;
        escapesec[0x027F6] = "&#x027F6;"; // LongRightArrow;
        escapesec[0x027F8] = "&#x027F8;"; // Longleftarrow;
        escapesec[0x027FA] = "&#x027FA;"; // Longleftrightarrow;
        escapesec[0x027F9] = "&#x027F9;"; // Longrightarrow;
        escapesec[0x1D543] = "&#x1D543;"; // Lopf;
        escapesec[0x02199] = "&#x02199;"; // LowerLeftArrow;
        escapesec[0x02198] = "&#x02198;"; // LowerRightArrow;
        escapesec[0x02112] = "&#x02112;"; // Lscr;
        escapesec[0x021B0] = "&#x021B0;"; // Lsh;
        escapesec[0x00141] = "&#x00141;"; // Lstrok;
        escapesec[0x0226A] = "&#x0226A;"; // Lt;
        escapesec[0x02905] = "&#x02905;"; // Map;
        escapesec[0x0041C] = "&#x0041C;"; // Mcy;
        escapesec[0x0205F] = "&#x0205F;"; // MediumSpace;
        escapesec[0x02133] = "&#x02133;"; // Mellintrf;
        escapesec[0x1D510] = "&#x1D510;"; // Mfr;
        escapesec[0x02213] = "&#x02213;"; // MinusPlus;
        escapesec[0x1D544] = "&#x1D544;"; // Mopf;
        escapesec[0x02133] = "&#x02133;"; // Mscr;
        escapesec[0x0039C] = "&#x0039C;"; // Mu;
        escapesec[0x0040A] = "&#x0040A;"; // NJcy;
        escapesec[0x00143] = "&#x00143;"; // Nacute;
        escapesec[0x00147] = "&#x00147;"; // Ncaron;
        escapesec[0x00145] = "&#x00145;"; // Ncedil;
        escapesec[0x0041D] = "&#x0041D;"; // Ncy;
        escapesec[0x0200B] = "&#x0200B;"; // NegativeMediumSpace;
        escapesec[0x0200B] = "&#x0200B;"; // NegativeThinSpace;
        escapesec[0x0226B] = "&#x0226B;"; // NestedGreaterGreater;
        escapesec[0x0226A] = "&#x0226A;"; // NestedLessLess;
        escapesec[0x0000A] = "&#x0000A;"; // NewLine;
        escapesec[0x1D511] = "&#x1D511;"; // Nfr;
        escapesec[0x02060] = "&#x02060;"; // NoBreak;
        escapesec[0x000A0] = "&#x000A0;"; // NonBreakingSpace;
        escapesec[0x02115] = "&#x02115;"; // Nopf;
        escapesec[0x02AEC] = "&#x02AEC;"; // Not;
        escapesec[0x02262] = "&#x02262;"; // NotCongruent;
        escapesec[0x0226D] = "&#x0226D;"; // NotCupCap;
        escapesec[0x02226] = "&#x02226;"; // NotDoubleVerticalBar;
        escapesec[0x02209] = "&#x02209;"; // NotElement;
        escapesec[0x02260] = "&#x02260;"; // NotEqual;
        escapesec[0x02204] = "&#x02204;"; // NotExists;
        escapesec[0x0226F] = "&#x0226F;"; // NotGreater;
        escapesec[0x02271] = "&#x02271;"; // NotGreaterEqual;
        escapesec[0x02279] = "&#x02279;"; // NotGreaterLess;
        escapesec[0x02275] = "&#x02275;"; // NotGreaterTilde;
        escapesec[0x022EA] = "&#x022EA;"; // NotLeftTriangle;
        escapesec[0x022EC] = "&#x022EC;"; // NotLeftTriangleEqual;
        escapesec[0x0226E] = "&#x0226E;"; // NotLess;
        escapesec[0x02270] = "&#x02270;"; // NotLessEqual;
        escapesec[0x02278] = "&#x02278;"; // NotLessGreater;
        escapesec[0x02274] = "&#x02274;"; // NotLessTilde;
        escapesec[0x02280] = "&#x02280;"; // NotPrecedes;
        escapesec[0x022E0] = "&#x022E0;"; // NotPrecedesSlantEqual;
        escapesec[0x0220C] = "&#x0220C;"; // NotReverseElement;
        escapesec[0x022EB] = "&#x022EB;"; // NotRightTriangle;
        escapesec[0x022ED] = "&#x022ED;"; // NotRightTriangleEqual;
        escapesec[0x022E2] = "&#x022E2;"; // NotSquareSubsetEqual;
        escapesec[0x022E3] = "&#x022E3;"; // NotSquareSupersetEqual;
        escapesec[0x02288] = "&#x02288;"; // NotSubsetEqual;
        escapesec[0x02281] = "&#x02281;"; // NotSucceeds;
        escapesec[0x022E1] = "&#x022E1;"; // NotSucceedsSlantEqual;
        escapesec[0x02289] = "&#x02289;"; // NotSupersetEqual;
        escapesec[0x02241] = "&#x02241;"; // NotTilde;
        escapesec[0x02244] = "&#x02244;"; // NotTildeEqual;
        escapesec[0x02247] = "&#x02247;"; // NotTildeFullEqual;
        escapesec[0x02249] = "&#x02249;"; // NotTildeTilde;
        escapesec[0x02224] = "&#x02224;"; // NotVerticalBar;
        escapesec[0x1D4A9] = "&#x1D4A9;"; // Nscr;
        escapesec[0x000D1] = "&#x000D1;"; // Ntilde;
        escapesec[0x0039D] = "&#x0039D;"; // Nu;
        escapesec[0x00152] = "&#x00152;"; // OElig;
        escapesec[0x000D3] = "&#x000D3;"; // Oacute;
        escapesec[0x000D4] = "&#x000D4;"; // Ocirc;
        escapesec[0x0041E] = "&#x0041E;"; // Ocy;
        escapesec[0x00150] = "&#x00150;"; // Odblac;
        escapesec[0x1D512] = "&#x1D512;"; // Ofr;
        escapesec[0x000D2] = "&#x000D2;"; // Ograve;
        escapesec[0x0014C] = "&#x0014C;"; // Omacr;
        escapesec[0x003A9] = "&#x003A9;"; // Omega;
        escapesec[0x0039F] = "&#x0039F;"; // Omicron;
        escapesec[0x1D546] = "&#x1D546;"; // Oopf;
        escapesec[0x0201C] = "&#x0201C;"; // OpenCurlyDoubleQuote;
        escapesec[0x02018] = "&#x02018;"; // OpenCurlyQuote;
        escapesec[0x02A54] = "&#x02A54;"; // Or;
        escapesec[0x1D4AA] = "&#x1D4AA;"; // Oscr;
        escapesec[0x000D8] = "&#x000D8;"; // Oslash;
        escapesec[0x000D5] = "&#x000D5;"; // Otilde;
        escapesec[0x02A37] = "&#x02A37;"; // Otimes;
        escapesec[0x000D6] = "&#x000D6;"; // Ouml;
        escapesec[0x0203E] = "&#x0203E;"; // OverBar;
        escapesec[0x023DE] = "&#x023DE;"; // OverBrace;
        escapesec[0x023B4] = "&#x023B4;"; // OverBracket;
        escapesec[0x023DC] = "&#x023DC;"; // OverParenthesis;
        escapesec[0x02202] = "&#x02202;"; // PartialD;
        escapesec[0x0041F] = "&#x0041F;"; // Pcy;
        escapesec[0x1D513] = "&#x1D513;"; // Pfr;
        escapesec[0x003A6] = "&#x003A6;"; // Phi;
        escapesec[0x003A0] = "&#x003A0;"; // Pi;
        escapesec[0x000B1] = "&#x000B1;"; // PlusMinus;
        escapesec[0x0210C] = "&#x0210C;"; // Poincareplane;
        escapesec[0x02119] = "&#x02119;"; // Popf;
        escapesec[0x02ABB] = "&#x02ABB;"; // Pr;
        escapesec[0x0227A] = "&#x0227A;"; // Precedes;
        escapesec[0x02AAF] = "&#x02AAF;"; // PrecedesEqual;
        escapesec[0x0227C] = "&#x0227C;"; // PrecedesSlantEqual;
        escapesec[0x0227E] = "&#x0227E;"; // PrecedesTilde;
        escapesec[0x02033] = "&#x02033;"; // Prime;
        escapesec[0x0220F] = "&#x0220F;"; // Product;
        escapesec[0x02237] = "&#x02237;"; // Proportion;
        escapesec[0x0221D] = "&#x0221D;"; // Proportional;
        escapesec[0x1D4AB] = "&#x1D4AB;"; // Pscr;
        escapesec[0x003A8] = "&#x003A8;"; // Psi;
        escapesec[0x00022] = "&#x00022;"; // QUOT;
        escapesec[0x1D514] = "&#x1D514;"; // Qfr;
        escapesec[0x0211A] = "&#x0211A;"; // Qopf;
        escapesec[0x1D4AC] = "&#x1D4AC;"; // Qscr;
        escapesec[0x02910] = "&#x02910;"; // RBarr;
        escapesec[0x000AE] = "&#x000AE;"; // REG;
        escapesec[0x00154] = "&#x00154;"; // Racute;
        escapesec[0x027EB] = "&#x027EB;"; // Rang;
        escapesec[0x021A0] = "&#x021A0;"; // Rarr;
        escapesec[0x02916] = "&#x02916;"; // Rarrtl;
        escapesec[0x00158] = "&#x00158;"; // Rcaron;
        escapesec[0x00156] = "&#x00156;"; // Rcedil;
        escapesec[0x00420] = "&#x00420;"; // Rcy;
        escapesec[0x0211C] = "&#x0211C;"; // Re;
        escapesec[0x0220B] = "&#x0220B;"; // ReverseElement;
        escapesec[0x021CB] = "&#x021CB;"; // ReverseEquilibrium;
        escapesec[0x0296F] = "&#x0296F;"; // ReverseUpEquilibrium;
        escapesec[0x0211C] = "&#x0211C;"; // Rfr;
        escapesec[0x003A1] = "&#x003A1;"; // Rho;
        escapesec[0x027E9] = "&#x027E9;"; // RightAngleBracket;
        escapesec[0x02192] = "&#x02192;"; // RightArrow;
        escapesec[0x021E5] = "&#x021E5;"; // RightArrowBar;
        escapesec[0x021C4] = "&#x021C4;"; // RightArrowLeftArrow;
        escapesec[0x02309] = "&#x02309;"; // RightCeiling;
        escapesec[0x027E7] = "&#x027E7;"; // RightDoubleBracket;
        escapesec[0x0295D] = "&#x0295D;"; // RightDownTeeVector;
        escapesec[0x021C2] = "&#x021C2;"; // RightDownVector;
        escapesec[0x02955] = "&#x02955;"; // RightDownVectorBar;
        escapesec[0x0230B] = "&#x0230B;"; // RightFloor;
        escapesec[0x022A2] = "&#x022A2;"; // RightTee;
        escapesec[0x021A6] = "&#x021A6;"; // RightTeeArrow;
        escapesec[0x0295B] = "&#x0295B;"; // RightTeeVector;
        escapesec[0x022B3] = "&#x022B3;"; // RightTriangle;
        escapesec[0x029D0] = "&#x029D0;"; // RightTriangleBar;
        escapesec[0x022B5] = "&#x022B5;"; // RightTriangleEqual;
        escapesec[0x0294F] = "&#x0294F;"; // RightUpDownVector;
        escapesec[0x0295C] = "&#x0295C;"; // RightUpTeeVector;
        escapesec[0x021BE] = "&#x021BE;"; // RightUpVector;
        escapesec[0x02954] = "&#x02954;"; // RightUpVectorBar;
        escapesec[0x021C0] = "&#x021C0;"; // RightVector;
        escapesec[0x02953] = "&#x02953;"; // RightVectorBar;
        escapesec[0x021D2] = "&#x021D2;"; // Rightarrow;
        escapesec[0x0211D] = "&#x0211D;"; // Ropf;
        escapesec[0x02970] = "&#x02970;"; // RoundImplies;
        escapesec[0x021DB] = "&#x021DB;"; // Rrightarrow;
        escapesec[0x0211B] = "&#x0211B;"; // Rscr;
        escapesec[0x021B1] = "&#x021B1;"; // Rsh;
        escapesec[0x029F4] = "&#x029F4;"; // RuleDelayed;
        escapesec[0x00429] = "&#x00429;"; // SHCHcy;
        escapesec[0x00428] = "&#x00428;"; // SHcy;
        escapesec[0x0042C] = "&#x0042C;"; // SOFTcy;
        escapesec[0x0015A] = "&#x0015A;"; // Sacute;
        escapesec[0x02ABC] = "&#x02ABC;"; // Sc;
        escapesec[0x00160] = "&#x00160;"; // Scaron;
        escapesec[0x0015E] = "&#x0015E;"; // Scedil;
        escapesec[0x0015C] = "&#x0015C;"; // Scirc;
        escapesec[0x00421] = "&#x00421;"; // Scy;
        escapesec[0x1D516] = "&#x1D516;"; // Sfr;
        escapesec[0x02193] = "&#x02193;"; // ShortDownArrow;
        escapesec[0x02190] = "&#x02190;"; // ShortLeftArrow;
        escapesec[0x02192] = "&#x02192;"; // ShortRightArrow;
        escapesec[0x02191] = "&#x02191;"; // ShortUpArrow;
        escapesec[0x003A3] = "&#x003A3;"; // Sigma;
        escapesec[0x02218] = "&#x02218;"; // SmallCircle;
        escapesec[0x1D54A] = "&#x1D54A;"; // Sopf;
        escapesec[0x0221A] = "&#x0221A;"; // Sqrt;
        escapesec[0x025A1] = "&#x025A1;"; // Square;
        escapesec[0x02293] = "&#x02293;"; // SquareIntersection;
        escapesec[0x0228F] = "&#x0228F;"; // SquareSubset;
        escapesec[0x02291] = "&#x02291;"; // SquareSubsetEqual;
        escapesec[0x02290] = "&#x02290;"; // SquareSuperset;
        escapesec[0x02292] = "&#x02292;"; // SquareSupersetEqual;
        escapesec[0x02294] = "&#x02294;"; // SquareUnion;
        escapesec[0x1D4AE] = "&#x1D4AE;"; // Sscr;
        escapesec[0x022C6] = "&#x022C6;"; // Star;
        escapesec[0x022D0] = "&#x022D0;"; // Sub;
        escapesec[0x02286] = "&#x02286;"; // SubsetEqual;
        escapesec[0x0227B] = "&#x0227B;"; // Succeeds;
        escapesec[0x02AB0] = "&#x02AB0;"; // SucceedsEqual;
        escapesec[0x0227D] = "&#x0227D;"; // SucceedsSlantEqual;
        escapesec[0x0227F] = "&#x0227F;"; // SucceedsTilde;
        escapesec[0x0220B] = "&#x0220B;"; // SuchThat;
        escapesec[0x02211] = "&#x02211;"; // Sum;
        escapesec[0x022D1] = "&#x022D1;"; // Sup;
        escapesec[0x02283] = "&#x02283;"; // Superset;
        escapesec[0x02287] = "&#x02287;"; // SupersetEqual;
        escapesec[0x022D1] = "&#x022D1;"; // Supset;
        escapesec[0x000DE] = "&#x000DE;"; // THORN;
        escapesec[0x02122] = "&#x02122;"; // TRADE;
        escapesec[0x0040B] = "&#x0040B;"; // TSHcy;
        escapesec[0x00426] = "&#x00426;"; // TScy;
        escapesec[0x00009] = "&#x00009;"; // Tab;
        escapesec[0x003A4] = "&#x003A4;"; // Tau;
        escapesec[0x00164] = "&#x00164;"; // Tcaron;
        escapesec[0x00162] = "&#x00162;"; // Tcedil;
        escapesec[0x00422] = "&#x00422;"; // Tcy;
        escapesec[0x1D517] = "&#x1D517;"; // Tfr;
        escapesec[0x02234] = "&#x02234;"; // Therefore;
        escapesec[0x00398] = "&#x00398;"; // Theta;
        escapesec[0x02009] = "&#x02009;"; // ThinSpace;
        escapesec[0x0223C] = "&#x0223C;"; // Tilde;
        escapesec[0x02243] = "&#x02243;"; // TildeEqual;
        escapesec[0x02245] = "&#x02245;"; // TildeFullEqual;
        escapesec[0x02248] = "&#x02248;"; // TildeTilde;
        escapesec[0x1D54B] = "&#x1D54B;"; // Topf;
        escapesec[0x020DB] = "&#x020DB;"; // TripleDot;
        escapesec[0x1D4AF] = "&#x1D4AF;"; // Tscr;
        escapesec[0x00166] = "&#x00166;"; // Tstrok;
        escapesec[0x000DA] = "&#x000DA;"; // Uacute;
        escapesec[0x0219F] = "&#x0219F;"; // Uarr;
        escapesec[0x02949] = "&#x02949;"; // Uarrocir;
        escapesec[0x0040E] = "&#x0040E;"; // Ubrcy;
        escapesec[0x0016C] = "&#x0016C;"; // Ubreve;
        escapesec[0x000DB] = "&#x000DB;"; // Ucirc;
        escapesec[0x00423] = "&#x00423;"; // Ucy;
        escapesec[0x00170] = "&#x00170;"; // Udblac;
        escapesec[0x1D518] = "&#x1D518;"; // Ufr;
        escapesec[0x000D9] = "&#x000D9;"; // Ugrave;
        escapesec[0x0016A] = "&#x0016A;"; // Umacr;
        escapesec[0x0005F] = "&#x0005F;"; // UnderBar;
        escapesec[0x023DF] = "&#x023DF;"; // UnderBrace;
        escapesec[0x023B5] = "&#x023B5;"; // UnderBracket;
        escapesec[0x023DD] = "&#x023DD;"; // UnderParenthesis;
        escapesec[0x022C3] = "&#x022C3;"; // Union;
        escapesec[0x0228E] = "&#x0228E;"; // UnionPlus;
        escapesec[0x00172] = "&#x00172;"; // Uogon;
        escapesec[0x1D54C] = "&#x1D54C;"; // Uopf;
        escapesec[0x02191] = "&#x02191;"; // UpArrow;
        escapesec[0x02912] = "&#x02912;"; // UpArrowBar;
        escapesec[0x021C5] = "&#x021C5;"; // UpArrowDownArrow;
        escapesec[0x02195] = "&#x02195;"; // UpDownArrow;
        escapesec[0x0296E] = "&#x0296E;"; // UpEquilibrium;
        escapesec[0x022A5] = "&#x022A5;"; // UpTee;
        escapesec[0x021A5] = "&#x021A5;"; // UpTeeArrow;
        escapesec[0x021D1] = "&#x021D1;"; // Uparrow;
        escapesec[0x021D5] = "&#x021D5;"; // Updownarrow;
        escapesec[0x02196] = "&#x02196;"; // UpperLeftArrow;
        escapesec[0x02197] = "&#x02197;"; // UpperRightArrow;
        escapesec[0x003D2] = "&#x003D2;"; // Upsi;
        escapesec[0x003A5] = "&#x003A5;"; // Upsilon;
        escapesec[0x0016E] = "&#x0016E;"; // Uring;
        escapesec[0x1D4B0] = "&#x1D4B0;"; // Uscr;
        escapesec[0x00168] = "&#x00168;"; // Utilde;
        escapesec[0x000DC] = "&#x000DC;"; // Uuml;
        escapesec[0x022AB] = "&#x022AB;"; // VDash;
        escapesec[0x02AEB] = "&#x02AEB;"; // Vbar;
        escapesec[0x00412] = "&#x00412;"; // Vcy;
        escapesec[0x022A9] = "&#x022A9;"; // Vdash;
        escapesec[0x02AE6] = "&#x02AE6;"; // Vdashl;
        escapesec[0x022C1] = "&#x022C1;"; // Vee;
        escapesec[0x02016] = "&#x02016;"; // Verbar;
        escapesec[0x02223] = "&#x02223;"; // VerticalBar;
        escapesec[0x0007C] = "&#x0007C;"; // VerticalLine;
        escapesec[0x02758] = "&#x02758;"; // VerticalSeparator;
        escapesec[0x02240] = "&#x02240;"; // VerticalTilde;
        escapesec[0x0200A] = "&#x0200A;"; // VeryThinSpace;
        escapesec[0x1D519] = "&#x1D519;"; // Vfr;
        escapesec[0x1D54D] = "&#x1D54D;"; // Vopf;
        escapesec[0x1D4B1] = "&#x1D4B1;"; // Vscr;
        escapesec[0x022AA] = "&#x022AA;"; // Vvdash;
        escapesec[0x00174] = "&#x00174;"; // Wcirc;
        escapesec[0x022C0] = "&#x022C0;"; // Wedge;
        escapesec[0x1D51A] = "&#x1D51A;"; // Wfr;
        escapesec[0x1D54E] = "&#x1D54E;"; // Wopf;
        escapesec[0x1D4B2] = "&#x1D4B2;"; // Wscr;
        escapesec[0x1D51B] = "&#x1D51B;"; // Xfr;
        escapesec[0x0039E] = "&#x0039E;"; // Xi;
        escapesec[0x1D54F] = "&#x1D54F;"; // Xopf;
        escapesec[0x1D4B3] = "&#x1D4B3;"; // Xscr;
        escapesec[0x0042F] = "&#x0042F;"; // YAcy;
        escapesec[0x00407] = "&#x00407;"; // YIcy;
        escapesec[0x0042E] = "&#x0042E;"; // YUcy;
        escapesec[0x000DD] = "&#x000DD;"; // Yacute;
        escapesec[0x00176] = "&#x00176;"; // Ycirc;
        escapesec[0x0042B] = "&#x0042B;"; // Ycy;
        escapesec[0x1D51C] = "&#x1D51C;"; // Yfr;
        escapesec[0x1D550] = "&#x1D550;"; // Yopf;
        escapesec[0x1D4B4] = "&#x1D4B4;"; // Yscr;
        escapesec[0x00178] = "&#x00178;"; // Yuml;
        escapesec[0x00416] = "&#x00416;"; // ZHcy;
        escapesec[0x00179] = "&#x00179;"; // Zacute;
        escapesec[0x0017D] = "&#x0017D;"; // Zcaron;
        escapesec[0x00417] = "&#x00417;"; // Zcy;
        escapesec[0x0017B] = "&#x0017B;"; // Zdot;
        escapesec[0x0200B] = "&#x0200B;"; // ZeroWidthSpace;
        escapesec[0x00396] = "&#x00396;"; // Zeta;
        escapesec[0x02128] = "&#x02128;"; // Zfr;
        escapesec[0x02124] = "&#x02124;"; // Zopf;
        escapesec[0x1D4B5] = "&#x1D4B5;"; // Zscr;
        escapesec[0x000E1] = "&#x000E1;"; // aacute;
        escapesec[0x00103] = "&#x00103;"; // abreve;
        escapesec[0x0223E] = "&#x0223E;"; // ac;
        escapesec[0x0223F] = "&#x0223F;"; // acd;
        escapesec[0x000E2] = "&#x000E2;"; // acirc;
        escapesec[0x000B4] = "&#x000B4;"; // acute;
        escapesec[0x00430] = "&#x00430;"; // acy;
        escapesec[0x000E6] = "&#x000E6;"; // aelig;
        escapesec[0x02061] = "&#x02061;"; // af;
        escapesec[0x1D51E] = "&#x1D51E;"; // afr;
        escapesec[0x000E0] = "&#x000E0;"; // agrave;
        escapesec[0x02135] = "&#x02135;"; // alefsym;
        escapesec[0x003B1] = "&#x003B1;"; // alpha;
        escapesec[0x00101] = "&#x00101;"; // amacr;
        escapesec[0x02A3F] = "&#x02A3F;"; // amalg;
        escapesec[0x00026] = "&#x00026;"; // amp;
        escapesec[0x02227] = "&#x02227;"; // and;
        escapesec[0x02A55] = "&#x02A55;"; // andand;
        escapesec[0x02A5C] = "&#x02A5C;"; // andd;
        escapesec[0x02A58] = "&#x02A58;"; // andslope;
        escapesec[0x02A5A] = "&#x02A5A;"; // andv;
        escapesec[0x02220] = "&#x02220;"; // ang;
        escapesec[0x029A4] = "&#x029A4;"; // ange;
        escapesec[0x02220] = "&#x02220;"; // angle;
        escapesec[0x02221] = "&#x02221;"; // angmsd;
        escapesec[0x029A8] = "&#x029A8;"; // angmsdaa;
        escapesec[0x029A9] = "&#x029A9;"; // angmsdab;
        escapesec[0x029AA] = "&#x029AA;"; // angmsdac;
        escapesec[0x029AB] = "&#x029AB;"; // angmsdad;
        escapesec[0x029AC] = "&#x029AC;"; // angmsdae;
        escapesec[0x029AD] = "&#x029AD;"; // angmsdaf;
        escapesec[0x029AE] = "&#x029AE;"; // angmsdag;
        escapesec[0x029AF] = "&#x029AF;"; // angmsdah;
        escapesec[0x0221F] = "&#x0221F;"; // angrt;
        escapesec[0x022BE] = "&#x022BE;"; // angrtvb;
        escapesec[0x0299D] = "&#x0299D;"; // angrtvbd;
        escapesec[0x02222] = "&#x02222;"; // angsph;
        escapesec[0x000C5] = "&#x000C5;"; // angst;
        escapesec[0x0237C] = "&#x0237C;"; // angzarr;
        escapesec[0x00105] = "&#x00105;"; // aogon;
        escapesec[0x1D552] = "&#x1D552;"; // aopf;
        escapesec[0x02248] = "&#x02248;"; // ap;
        escapesec[0x02A70] = "&#x02A70;"; // apE;
        escapesec[0x02A6F] = "&#x02A6F;"; // apacir;
        escapesec[0x0224A] = "&#x0224A;"; // ape;
        escapesec[0x0224B] = "&#x0224B;"; // apid;
        escapesec[0x00027] = "&#x00027;"; // apos;
        escapesec[0x02248] = "&#x02248;"; // approx;
        escapesec[0x0224A] = "&#x0224A;"; // approxeq;
        escapesec[0x000E5] = "&#x000E5;"; // aring;
        escapesec[0x1D4B6] = "&#x1D4B6;"; // ascr;
        escapesec[0x0002A] = "&#x0002A;"; // ast;
        escapesec[0x02248] = "&#x02248;"; // asymp;
        escapesec[0x0224D] = "&#x0224D;"; // asympeq;
        escapesec[0x000E3] = "&#x000E3;"; // atilde;
        escapesec[0x000E4] = "&#x000E4;"; // auml;
        escapesec[0x02233] = "&#x02233;"; // awconint;
        escapesec[0x02A11] = "&#x02A11;"; // awint;
        escapesec[0x02AED] = "&#x02AED;"; // bNot;
        escapesec[0x0224C] = "&#x0224C;"; // backcong;
        escapesec[0x003F6] = "&#x003F6;"; // backepsilon;
        escapesec[0x02035] = "&#x02035;"; // backprime;
        escapesec[0x0223D] = "&#x0223D;"; // backsim;
        escapesec[0x022CD] = "&#x022CD;"; // backsimeq;
        escapesec[0x022BD] = "&#x022BD;"; // barvee;
        escapesec[0x02305] = "&#x02305;"; // barwed;
        escapesec[0x023B5] = "&#x023B5;"; // bbrk;
        escapesec[0x023B6] = "&#x023B6;"; // bbrktbrk;
        escapesec[0x0224C] = "&#x0224C;"; // bcong;
        escapesec[0x00431] = "&#x00431;"; // bcy;
        escapesec[0x0201E] = "&#x0201E;"; // bdquo;
        escapesec[0x02235] = "&#x02235;"; // becaus;
        escapesec[0x029B0] = "&#x029B0;"; // bemptyv;
        escapesec[0x003F6] = "&#x003F6;"; // bepsi;
        escapesec[0x0212C] = "&#x0212C;"; // bernou;
        escapesec[0x003B2] = "&#x003B2;"; // beta;
        escapesec[0x02136] = "&#x02136;"; // beth;
        escapesec[0x0226C] = "&#x0226C;"; // between;
        escapesec[0x1D51F] = "&#x1D51F;"; // bfr;
        escapesec[0x022C2] = "&#x022C2;"; // bigcap;
        escapesec[0x025EF] = "&#x025EF;"; // bigcirc;
        escapesec[0x022C3] = "&#x022C3;"; // bigcup;
        escapesec[0x02A00] = "&#x02A00;"; // bigodot;
        escapesec[0x02A01] = "&#x02A01;"; // bigoplus;
        escapesec[0x02A02] = "&#x02A02;"; // bigotimes;
        escapesec[0x02A06] = "&#x02A06;"; // bigsqcup;
        escapesec[0x02605] = "&#x02605;"; // bigstar;
        escapesec[0x025BD] = "&#x025BD;"; // bigtriangledown;
        escapesec[0x025B3] = "&#x025B3;"; // bigtriangleup;
        escapesec[0x02A04] = "&#x02A04;"; // biguplus;
        escapesec[0x022C1] = "&#x022C1;"; // bigvee;
        escapesec[0x022C0] = "&#x022C0;"; // bigwedge;
        escapesec[0x0290D] = "&#x0290D;"; // bkarow;
        escapesec[0x029EB] = "&#x029EB;"; // blacklozenge;
        escapesec[0x025AA] = "&#x025AA;"; // blacksquare;
        escapesec[0x025B4] = "&#x025B4;"; // blacktriangle;
        escapesec[0x025BE] = "&#x025BE;"; // blacktriangledown;
        escapesec[0x025C2] = "&#x025C2;"; // blacktriangleleft;
        escapesec[0x025B8] = "&#x025B8;"; // blacktriangleright;
        escapesec[0x02423] = "&#x02423;"; // blank;
        escapesec[0x02592] = "&#x02592;"; // blk12;
        escapesec[0x02591] = "&#x02591;"; // blk14;
        escapesec[0x02593] = "&#x02593;"; // blk34;
        escapesec[0x02588] = "&#x02588;"; // block;
        escapesec[0x02310] = "&#x02310;"; // bnot;
        escapesec[0x1D553] = "&#x1D553;"; // bopf;
        escapesec[0x022A5] = "&#x022A5;"; // bot;
        escapesec[0x022C8] = "&#x022C8;"; // bowtie;
        escapesec[0x02557] = "&#x02557;"; // boxDL;
        escapesec[0x02554] = "&#x02554;"; // boxDR;
        escapesec[0x02556] = "&#x02556;"; // boxDl;
        escapesec[0x02553] = "&#x02553;"; // boxDr;
        escapesec[0x02550] = "&#x02550;"; // boxH;
        escapesec[0x02566] = "&#x02566;"; // boxHD;
        escapesec[0x02569] = "&#x02569;"; // boxHU;
        escapesec[0x02564] = "&#x02564;"; // boxHd;
        escapesec[0x02567] = "&#x02567;"; // boxHu;
        escapesec[0x0255D] = "&#x0255D;"; // boxUL;
        escapesec[0x0255A] = "&#x0255A;"; // boxUR;
        escapesec[0x0255C] = "&#x0255C;"; // boxUl;
        escapesec[0x02559] = "&#x02559;"; // boxUr;
        escapesec[0x02551] = "&#x02551;"; // boxV;
        escapesec[0x0256C] = "&#x0256C;"; // boxVH;
        escapesec[0x02563] = "&#x02563;"; // boxVL;
        escapesec[0x02560] = "&#x02560;"; // boxVR;
        escapesec[0x0256B] = "&#x0256B;"; // boxVh;
        escapesec[0x02562] = "&#x02562;"; // boxVl;
        escapesec[0x0255F] = "&#x0255F;"; // boxVr;
        escapesec[0x029C9] = "&#x029C9;"; // boxbox;
        escapesec[0x02555] = "&#x02555;"; // boxdL;
        escapesec[0x02552] = "&#x02552;"; // boxdR;
        escapesec[0x02510] = "&#x02510;"; // boxdl;
        escapesec[0x0250C] = "&#x0250C;"; // boxdr;
        escapesec[0x02500] = "&#x02500;"; // boxh;
        escapesec[0x02565] = "&#x02565;"; // boxhD;
        escapesec[0x02568] = "&#x02568;"; // boxhU;
        escapesec[0x0252C] = "&#x0252C;"; // boxhd;
        escapesec[0x02534] = "&#x02534;"; // boxhu;
        escapesec[0x0229F] = "&#x0229F;"; // boxminus;
        escapesec[0x0229E] = "&#x0229E;"; // boxplus;
        escapesec[0x022A0] = "&#x022A0;"; // boxtimes;
        escapesec[0x0255B] = "&#x0255B;"; // boxuL;
        escapesec[0x02558] = "&#x02558;"; // boxuR;
        escapesec[0x02518] = "&#x02518;"; // boxul;
        escapesec[0x02514] = "&#x02514;"; // boxur;
        escapesec[0x02502] = "&#x02502;"; // boxv;
        escapesec[0x0256A] = "&#x0256A;"; // boxvH;
        escapesec[0x02561] = "&#x02561;"; // boxvL;
        escapesec[0x0255E] = "&#x0255E;"; // boxvR;
        escapesec[0x0253C] = "&#x0253C;"; // boxvh;
        escapesec[0x02524] = "&#x02524;"; // boxvl;
        escapesec[0x0251C] = "&#x0251C;"; // boxvr;
        escapesec[0x02035] = "&#x02035;"; // bprime;
        escapesec[0x002D8] = "&#x002D8;"; // breve;
        escapesec[0x000A6] = "&#x000A6;"; // brvbar;
        escapesec[0x1D4B7] = "&#x1D4B7;"; // bscr;
        escapesec[0x0204F] = "&#x0204F;"; // bsemi;
        escapesec[0x0223D] = "&#x0223D;"; // bsim;
        escapesec[0x022CD] = "&#x022CD;"; // bsime;
        escapesec[0x0005C] = "&#x0005C;"; // bsol;
        escapesec[0x029C5] = "&#x029C5;"; // bsolb;
        escapesec[0x027C8] = "&#x027C8;"; // bsolhsub;
        escapesec[0x02022] = "&#x02022;"; // bull;
        escapesec[0x0224E] = "&#x0224E;"; // bump;
        escapesec[0x02AAE] = "&#x02AAE;"; // bumpE;
        escapesec[0x0224F] = "&#x0224F;"; // bumpe;
        escapesec[0x00107] = "&#x00107;"; // cacute;
        escapesec[0x02229] = "&#x02229;"; // cap;
        escapesec[0x02A44] = "&#x02A44;"; // capand;
        escapesec[0x02A49] = "&#x02A49;"; // capbrcup;
        escapesec[0x02A4B] = "&#x02A4B;"; // capcap;
        escapesec[0x02A47] = "&#x02A47;"; // capcup;
        escapesec[0x02A40] = "&#x02A40;"; // capdot;
        escapesec[0x02041] = "&#x02041;"; // caret;
        escapesec[0x002C7] = "&#x002C7;"; // caron;
        escapesec[0x02A4D] = "&#x02A4D;"; // ccaps;
        escapesec[0x0010D] = "&#x0010D;"; // ccaron;
        escapesec[0x000E7] = "&#x000E7;"; // ccedil;
        escapesec[0x00109] = "&#x00109;"; // ccirc;
        escapesec[0x02A4C] = "&#x02A4C;"; // ccups;
        escapesec[0x02A50] = "&#x02A50;"; // ccupssm;
        escapesec[0x0010B] = "&#x0010B;"; // cdot;
        escapesec[0x000B8] = "&#x000B8;"; // cedil;
        escapesec[0x029B2] = "&#x029B2;"; // cemptyv;
        escapesec[0x000A2] = "&#x000A2;"; // cent;
        escapesec[0x000B7] = "&#x000B7;"; // centerdot;
        escapesec[0x1D520] = "&#x1D520;"; // cfr;
        escapesec[0x00447] = "&#x00447;"; // chcy;
        escapesec[0x02713] = "&#x02713;"; // check;
        escapesec[0x003C7] = "&#x003C7;"; // chi;
        escapesec[0x025CB] = "&#x025CB;"; // cir;
        escapesec[0x029C3] = "&#x029C3;"; // cirE;
        escapesec[0x002C6] = "&#x002C6;"; // circ;
        escapesec[0x02257] = "&#x02257;"; // circeq;
        escapesec[0x021BA] = "&#x021BA;"; // circlearrowleft;
        escapesec[0x021BB] = "&#x021BB;"; // circlearrowright;
        escapesec[0x000AE] = "&#x000AE;"; // circledR;
        escapesec[0x024C8] = "&#x024C8;"; // circledS;
        escapesec[0x0229B] = "&#x0229B;"; // circledast;
        escapesec[0x0229A] = "&#x0229A;"; // circledcirc;
        escapesec[0x0229D] = "&#x0229D;"; // circleddash;
        escapesec[0x02257] = "&#x02257;"; // cire;
        escapesec[0x02A10] = "&#x02A10;"; // cirfnint;
        escapesec[0x02AEF] = "&#x02AEF;"; // cirmid;
        escapesec[0x029C2] = "&#x029C2;"; // cirscir;
        escapesec[0x02663] = "&#x02663;"; // clubs;
        escapesec[0x0003A] = "&#x0003A;"; // colon;
        escapesec[0x02254] = "&#x02254;"; // colone;
        escapesec[0x0002C] = "&#x0002C;"; // comma;
        escapesec[0x00040] = "&#x00040;"; // commat;
        escapesec[0x02201] = "&#x02201;"; // comp;
        escapesec[0x02218] = "&#x02218;"; // compfn;
        escapesec[0x02201] = "&#x02201;"; // complement;
        escapesec[0x02102] = "&#x02102;"; // complexes;
        escapesec[0x02245] = "&#x02245;"; // cong;
        escapesec[0x02A6D] = "&#x02A6D;"; // congdot;
        escapesec[0x0222E] = "&#x0222E;"; // conint;
        escapesec[0x1D554] = "&#x1D554;"; // copf;
        escapesec[0x02210] = "&#x02210;"; // coprod;
        escapesec[0x000A9] = "&#x000A9;"; // copy;
        escapesec[0x02117] = "&#x02117;"; // copysr;
        escapesec[0x021B5] = "&#x021B5;"; // crarr;
        escapesec[0x02717] = "&#x02717;"; // cross;
        escapesec[0x1D4B8] = "&#x1D4B8;"; // cscr;
        escapesec[0x02ACF] = "&#x02ACF;"; // csub;
        escapesec[0x02AD1] = "&#x02AD1;"; // csube;
        escapesec[0x02AD0] = "&#x02AD0;"; // csup;
        escapesec[0x02AD2] = "&#x02AD2;"; // csupe;
        escapesec[0x022EF] = "&#x022EF;"; // ctdot;
        escapesec[0x02938] = "&#x02938;"; // cudarrl;
        escapesec[0x02935] = "&#x02935;"; // cudarrr;
        escapesec[0x022DE] = "&#x022DE;"; // cuepr;
        escapesec[0x022DF] = "&#x022DF;"; // cuesc;
        escapesec[0x021B6] = "&#x021B6;"; // cularr;
        escapesec[0x0293D] = "&#x0293D;"; // cularrp;
        escapesec[0x0222A] = "&#x0222A;"; // cup;
        escapesec[0x02A48] = "&#x02A48;"; // cupbrcap;
        escapesec[0x02A46] = "&#x02A46;"; // cupcap;
        escapesec[0x02A4A] = "&#x02A4A;"; // cupcup;
        escapesec[0x0228D] = "&#x0228D;"; // cupdot;
        escapesec[0x02A45] = "&#x02A45;"; // cupor;
        escapesec[0x021B7] = "&#x021B7;"; // curarr;
        escapesec[0x0293C] = "&#x0293C;"; // curarrm;
        escapesec[0x022DE] = "&#x022DE;"; // curlyeqprec;
        escapesec[0x022DF] = "&#x022DF;"; // curlyeqsucc;
        escapesec[0x022CE] = "&#x022CE;"; // curlyvee;
        escapesec[0x022CF] = "&#x022CF;"; // curlywedge;
        escapesec[0x000A4] = "&#x000A4;"; // curren;
        escapesec[0x021B6] = "&#x021B6;"; // curvearrowleft;
        escapesec[0x021B7] = "&#x021B7;"; // curvearrowright;
        escapesec[0x022CE] = "&#x022CE;"; // cuvee;
        escapesec[0x022CF] = "&#x022CF;"; // cuwed;
        escapesec[0x02232] = "&#x02232;"; // cwconint;
        escapesec[0x02231] = "&#x02231;"; // cwint;
        escapesec[0x0232D] = "&#x0232D;"; // cylcty;
        escapesec[0x021D3] = "&#x021D3;"; // dArr;
        escapesec[0x02965] = "&#x02965;"; // dHar;
        escapesec[0x02020] = "&#x02020;"; // dagger;
        escapesec[0x02138] = "&#x02138;"; // daleth;
        escapesec[0x02193] = "&#x02193;"; // darr;
        escapesec[0x02010] = "&#x02010;"; // dash;
        escapesec[0x022A3] = "&#x022A3;"; // dashv;
        escapesec[0x0290F] = "&#x0290F;"; // dbkarow;
        escapesec[0x002DD] = "&#x002DD;"; // dblac;
        escapesec[0x0010F] = "&#x0010F;"; // dcaron;
        escapesec[0x00434] = "&#x00434;"; // dcy;
        escapesec[0x02146] = "&#x02146;"; // dd;
        escapesec[0x02021] = "&#x02021;"; // ddagger;
        escapesec[0x021CA] = "&#x021CA;"; // ddarr;
        escapesec[0x02A77] = "&#x02A77;"; // ddotseq;
        escapesec[0x000B0] = "&#x000B0;"; // deg;
        escapesec[0x003B4] = "&#x003B4;"; // delta;
        escapesec[0x029B1] = "&#x029B1;"; // demptyv;
        escapesec[0x0297F] = "&#x0297F;"; // dfisht;
        escapesec[0x1D521] = "&#x1D521;"; // dfr;
        escapesec[0x021C3] = "&#x021C3;"; // dharl;
        escapesec[0x021C2] = "&#x021C2;"; // dharr;
        escapesec[0x022C4] = "&#x022C4;"; // diam;
        escapesec[0x02666] = "&#x02666;"; // diamondsuit;
        escapesec[0x000A8] = "&#x000A8;"; // die;
        escapesec[0x003DD] = "&#x003DD;"; // digamma;
        escapesec[0x022F2] = "&#x022F2;"; // disin;
        escapesec[0x000F7] = "&#x000F7;"; // div;
        escapesec[0x000F7] = "&#x000F7;"; // divide
        escapesec[0x022C7] = "&#x022C7;"; // divideontimes;
        escapesec[0x00452] = "&#x00452;"; // djcy;
        escapesec[0x0231E] = "&#x0231E;"; // dlcorn;
        escapesec[0x0230D] = "&#x0230D;"; // dlcrop;
        escapesec[0x00024] = "&#x00024;"; // dollar;
        escapesec[0x1D555] = "&#x1D555;"; // dopf;
        escapesec[0x002D9] = "&#x002D9;"; // dot;
        escapesec[0x02250] = "&#x02250;"; // doteq;
        escapesec[0x02251] = "&#x02251;"; // doteqdot;
        escapesec[0x02238] = "&#x02238;"; // dotminus;
        escapesec[0x02214] = "&#x02214;"; // dotplus;
        escapesec[0x022A1] = "&#x022A1;"; // dotsquare;
        escapesec[0x02306] = "&#x02306;"; // doublebarwedge;
        escapesec[0x02193] = "&#x02193;"; // downarrow;
        escapesec[0x021CA] = "&#x021CA;"; // downdownarrows;
        escapesec[0x021C3] = "&#x021C3;"; // downharpoonleft;
        escapesec[0x021C2] = "&#x021C2;"; // downharpoonright;
        escapesec[0x02910] = "&#x02910;"; // drbkarow;
        escapesec[0x0231F] = "&#x0231F;"; // drcorn;
        escapesec[0x0230C] = "&#x0230C;"; // drcrop;
        escapesec[0x1D4B9] = "&#x1D4B9;"; // dscr;
        escapesec[0x00455] = "&#x00455;"; // dscy;
        escapesec[0x029F6] = "&#x029F6;"; // dsol;
        escapesec[0x00111] = "&#x00111;"; // dstrok;
        escapesec[0x022F1] = "&#x022F1;"; // dtdot;
        escapesec[0x025BF] = "&#x025BF;"; // dtri;
        escapesec[0x025BE] = "&#x025BE;"; // dtrif;
        escapesec[0x021F5] = "&#x021F5;"; // duarr;
        escapesec[0x0296F] = "&#x0296F;"; // duhar;
        escapesec[0x029A6] = "&#x029A6;"; // dwangle;
        escapesec[0x0045F] = "&#x0045F;"; // dzcy;
        escapesec[0x027FF] = "&#x027FF;"; // dzigrarr;
        escapesec[0x02A77] = "&#x02A77;"; // eDDot;
        escapesec[0x02251] = "&#x02251;"; // eDot;
        escapesec[0x000E9] = "&#x000E9;"; // eacute;
        escapesec[0x02A6E] = "&#x02A6E;"; // easter;
        escapesec[0x0011B] = "&#x0011B;"; // ecaron;
        escapesec[0x02256] = "&#x02256;"; // ecir;
        escapesec[0x000EA] = "&#x000EA;"; // ecirc;
        escapesec[0x02255] = "&#x02255;"; // ecolon;
        escapesec[0x0044D] = "&#x0044D;"; // ecy;
        escapesec[0x00117] = "&#x00117;"; // edot;
        escapesec[0x02147] = "&#x02147;"; // ee;
        escapesec[0x02252] = "&#x02252;"; // efDot;
        escapesec[0x1D522] = "&#x1D522;"; // efr;
        escapesec[0x02A9A] = "&#x02A9A;"; // eg;
        escapesec[0x000E8] = "&#x000E8;"; // egrave;
        escapesec[0x02A96] = "&#x02A96;"; // egs;
        escapesec[0x02A98] = "&#x02A98;"; // egsdot;
        escapesec[0x02A99] = "&#x02A99;"; // el;
        escapesec[0x023E7] = "&#x023E7;"; // elinters;
        escapesec[0x02113] = "&#x02113;"; // ell;
        escapesec[0x02A95] = "&#x02A95;"; // els;
        escapesec[0x02A97] = "&#x02A97;"; // elsdot;
        escapesec[0x00113] = "&#x00113;"; // emacr;
        escapesec[0x02205] = "&#x02205;"; // empty;
        escapesec[0x02205] = "&#x02205;"; // emptyv;
        escapesec[0x02004] = "&#x02004;"; // emsp13;
        escapesec[0x02005] = "&#x02005;"; // emsp14;
        escapesec[0x02003] = "&#x02003;"; // emsp;
        escapesec[0x0014B] = "&#x0014B;"; // eng;
        escapesec[0x02002] = "&#x02002;"; // ensp;
        escapesec[0x00119] = "&#x00119;"; // eogon;
        escapesec[0x1D556] = "&#x1D556;"; // eopf;
        escapesec[0x022D5] = "&#x022D5;"; // epar;
        escapesec[0x029E3] = "&#x029E3;"; // eparsl;
        escapesec[0x02A71] = "&#x02A71;"; // eplus;
        escapesec[0x003B5] = "&#x003B5;"; // epsi;
        escapesec[0x003F5] = "&#x003F5;"; // epsiv;
        escapesec[0x02256] = "&#x02256;"; // eqcirc;
        escapesec[0x02255] = "&#x02255;"; // eqcolon;
        escapesec[0x02242] = "&#x02242;"; // eqsim;
        escapesec[0x02A96] = "&#x02A96;"; // eqslantgtr;
        escapesec[0x02A95] = "&#x02A95;"; // eqslantless;
        escapesec[0x0003D] = "&#x0003D;"; // equals;
        escapesec[0x0225F] = "&#x0225F;"; // equest;
        escapesec[0x02261] = "&#x02261;"; // equiv;
        escapesec[0x02A78] = "&#x02A78;"; // equivDD;
        escapesec[0x029E5] = "&#x029E5;"; // eqvparsl;
        escapesec[0x02253] = "&#x02253;"; // erDot;
        escapesec[0x02971] = "&#x02971;"; // erarr;
        escapesec[0x0212F] = "&#x0212F;"; // escr;
        escapesec[0x02250] = "&#x02250;"; // esdot;
        escapesec[0x02242] = "&#x02242;"; // esim;
        escapesec[0x003B7] = "&#x003B7;"; // eta;
        escapesec[0x000F0] = "&#x000F0;"; // eth;
        escapesec[0x000EB] = "&#x000EB;"; // euml;
        escapesec[0x020AC] = "&#x020AC;"; // euro;
        escapesec[0x00021] = "&#x00021;"; // excl;
        escapesec[0x02203] = "&#x02203;"; // exist;
        escapesec[0x02130] = "&#x02130;"; // expectation;
        escapesec[0x02147] = "&#x02147;"; // exponentiale;
        escapesec[0x02252] = "&#x02252;"; // fallingdotseq;
        escapesec[0x00444] = "&#x00444;"; // fcy;
        escapesec[0x02640] = "&#x02640;"; // female;
        escapesec[0x0FB03] = "&#x0FB03;"; // ffilig;
        escapesec[0x0FB00] = "&#x0FB00;"; // fflig;
        escapesec[0x0FB04] = "&#x0FB04;"; // ffllig;
        escapesec[0x1D523] = "&#x1D523;"; // ffr;
        escapesec[0x0FB01] = "&#x0FB01;"; // filig;
        escapesec[0x0266D] = "&#x0266D;"; // flat;
        escapesec[0x0FB02] = "&#x0FB02;"; // fllig;
        escapesec[0x025B1] = "&#x025B1;"; // fltns;
        escapesec[0x00192] = "&#x00192;"; // fnof;
        escapesec[0x1D557] = "&#x1D557;"; // fopf;
        escapesec[0x02200] = "&#x02200;"; // forall;
        escapesec[0x022D4] = "&#x022D4;"; // fork;
        escapesec[0x02AD9] = "&#x02AD9;"; // forkv;
        escapesec[0x02A0D] = "&#x02A0D;"; // fpartint;
        escapesec[0x000BD] = "&#x000BD;"; // frac12;
        escapesec[0x02153] = "&#x02153;"; // frac13;
        escapesec[0x000BC] = "&#x000BC;"; // frac14;
        escapesec[0x02155] = "&#x02155;"; // frac15;
        escapesec[0x02159] = "&#x02159;"; // frac16;
        escapesec[0x0215B] = "&#x0215B;"; // frac18;
        escapesec[0x02154] = "&#x02154;"; // frac23;
        escapesec[0x02156] = "&#x02156;"; // frac25;
        escapesec[0x000BE] = "&#x000BE;"; // frac34;
        escapesec[0x02157] = "&#x02157;"; // frac35;
        escapesec[0x0215C] = "&#x0215C;"; // frac38;
        escapesec[0x02158] = "&#x02158;"; // frac45;
        escapesec[0x0215A] = "&#x0215A;"; // frac56;
        escapesec[0x0215D] = "&#x0215D;"; // frac58;
        escapesec[0x0215E] = "&#x0215E;"; // frac78;
        escapesec[0x02044] = "&#x02044;"; // frasl;
        escapesec[0x02322] = "&#x02322;"; // frown;
        escapesec[0x1D4BB] = "&#x1D4BB;"; // fscr;
        escapesec[0x02267] = "&#x02267;"; // gE;
        escapesec[0x02A8C] = "&#x02A8C;"; // gEl;
        escapesec[0x001F5] = "&#x001F5;"; // gacute;
        escapesec[0x003B3] = "&#x003B3;"; // gamma;
        escapesec[0x003DD] = "&#x003DD;"; // gammad;
        escapesec[0x02A86] = "&#x02A86;"; // gap;
        escapesec[0x0011F] = "&#x0011F;"; // gbreve;
        escapesec[0x0011D] = "&#x0011D;"; // gcirc;
        escapesec[0x00433] = "&#x00433;"; // gcy;
        escapesec[0x00121] = "&#x00121;"; // gdot;
        escapesec[0x02265] = "&#x02265;"; // ge;
        escapesec[0x022DB] = "&#x022DB;"; // gel;
        escapesec[0x02265] = "&#x02265;"; // geq;
        escapesec[0x02267] = "&#x02267;"; // geqq;
        escapesec[0x02A7E] = "&#x02A7E;"; // geqslant;
        escapesec[0x02AA9] = "&#x02AA9;"; // gescc;
        escapesec[0x02A80] = "&#x02A80;"; // gesdot;
        escapesec[0x02A82] = "&#x02A82;"; // gesdoto;
        escapesec[0x02A84] = "&#x02A84;"; // gesdotol;
        escapesec[0x02A94] = "&#x02A94;"; // gesles;
        escapesec[0x1D524] = "&#x1D524;"; // gfr;
        escapesec[0x0226B] = "&#x0226B;"; // gg;
        escapesec[0x022D9] = "&#x022D9;"; // ggg;
        escapesec[0x02137] = "&#x02137;"; // gimel;
        escapesec[0x00453] = "&#x00453;"; // gjcy;
        escapesec[0x02277] = "&#x02277;"; // gl;
        escapesec[0x02A92] = "&#x02A92;"; // glE;
        escapesec[0x02AA5] = "&#x02AA5;"; // gla;
        escapesec[0x02AA4] = "&#x02AA4;"; // glj;
        escapesec[0x02269] = "&#x02269;"; // gnE;
        escapesec[0x02A8A] = "&#x02A8A;"; // gnap;
        escapesec[0x02A88] = "&#x02A88;"; // gne;
        escapesec[0x02269] = "&#x02269;"; // gneqq;
        escapesec[0x022E7] = "&#x022E7;"; // gnsim;
        escapesec[0x1D558] = "&#x1D558;"; // gopf;
        escapesec[0x00060] = "&#x00060;"; // grave;
        escapesec[0x0210A] = "&#x0210A;"; // gscr;
        escapesec[0x02273] = "&#x02273;"; // gsim;
        escapesec[0x02A8E] = "&#x02A8E;"; // gsime;
        escapesec[0x02A90] = "&#x02A90;"; // gsiml;
        escapesec[0x0003E] = "&#x0003E;"; // gt;
        escapesec[0x02AA7] = "&#x02AA7;"; // gtcc;
        escapesec[0x02A7A] = "&#x02A7A;"; // gtcir;
        escapesec[0x022D7] = "&#x022D7;"; // gtdot;
        escapesec[0x02995] = "&#x02995;"; // gtlPar;
        escapesec[0x02A7C] = "&#x02A7C;"; // gtquest;
        escapesec[0x02A86] = "&#x02A86;"; // gtrapprox;
        escapesec[0x02978] = "&#x02978;"; // gtrarr;
        escapesec[0x022D7] = "&#x022D7;"; // gtrdot;
        escapesec[0x022DB] = "&#x022DB;"; // gtreqless;
        escapesec[0x02A8C] = "&#x02A8C;"; // gtreqqless;
        escapesec[0x02277] = "&#x02277;"; // gtrless;
        escapesec[0x02273] = "&#x02273;"; // gtrsim;
        escapesec[0x021D4] = "&#x021D4;"; // hArr;
        escapesec[0x0200A] = "&#x0200A;"; // hairsp;
        escapesec[0x000BD] = "&#x000BD;"; // half;
        escapesec[0x0210B] = "&#x0210B;"; // hamilt;
        escapesec[0x0044A] = "&#x0044A;"; // hardcy;
        escapesec[0x02194] = "&#x02194;"; // harr;
        escapesec[0x02948] = "&#x02948;"; // harrcir;
        escapesec[0x021AD] = "&#x021AD;"; // harrw;
        escapesec[0x0210F] = "&#x0210F;"; // hbar;
        escapesec[0x00125] = "&#x00125;"; // hcirc;
        escapesec[0x02665] = "&#x02665;"; // hearts;
        escapesec[0x02026] = "&#x02026;"; // hellip;
        escapesec[0x022B9] = "&#x022B9;"; // hercon;
        escapesec[0x1D525] = "&#x1D525;"; // hfr;
        escapesec[0x02925] = "&#x02925;"; // hksearow;
        escapesec[0x02926] = "&#x02926;"; // hkswarow;
        escapesec[0x021FF] = "&#x021FF;"; // hoarr;
        escapesec[0x0223B] = "&#x0223B;"; // homtht;
        escapesec[0x021A9] = "&#x021A9;"; // hookleftarrow;
        escapesec[0x021AA] = "&#x021AA;"; // hookrightarrow;
        escapesec[0x1D559] = "&#x1D559;"; // hopf;
        escapesec[0x02015] = "&#x02015;"; // horbar;
        escapesec[0x1D4BD] = "&#x1D4BD;"; // hscr;
        escapesec[0x0210F] = "&#x0210F;"; // hslash;
        escapesec[0x00127] = "&#x00127;"; // hstrok;
        escapesec[0x02043] = "&#x02043;"; // hybull;
        escapesec[0x02010] = "&#x02010;"; // hyphen;
        escapesec[0x000ED] = "&#x000ED;"; // iacute;
        escapesec[0x02063] = "&#x02063;"; // ic;
        escapesec[0x000EE] = "&#x000EE;"; // icirc;
        escapesec[0x00438] = "&#x00438;"; // icy;
        escapesec[0x00435] = "&#x00435;"; // iecy;
        escapesec[0x000A1] = "&#x000A1;"; // iexcl;
        escapesec[0x021D4] = "&#x021D4;"; // iff;
        escapesec[0x1D526] = "&#x1D526;"; // ifr;
        escapesec[0x000EC] = "&#x000EC;"; // igrave;
        escapesec[0x02148] = "&#x02148;"; // ii;
        escapesec[0x02A0C] = "&#x02A0C;"; // iiiint;
        escapesec[0x0222D] = "&#x0222D;"; // iiint;
        escapesec[0x029DC] = "&#x029DC;"; // iinfin;
        escapesec[0x02129] = "&#x02129;"; // iiota;
        escapesec[0x00133] = "&#x00133;"; // ijlig;
        escapesec[0x0012B] = "&#x0012B;"; // imacr;
        escapesec[0x02111] = "&#x02111;"; // image;
        escapesec[0x02110] = "&#x02110;"; // imagline;
        escapesec[0x02111] = "&#x02111;"; // imagpart;
        escapesec[0x00131] = "&#x00131;"; // imath;
        escapesec[0x022B7] = "&#x022B7;"; // imof;
        escapesec[0x001B5] = "&#x001B5;"; // imped;
        escapesec[0x02208] = "&#x02208;"; // in;
        escapesec[0x02105] = "&#x02105;"; // incare;
        escapesec[0x0221E] = "&#x0221E;"; // infin;
        escapesec[0x029DD] = "&#x029DD;"; // infintie;
        escapesec[0x00131] = "&#x00131;"; // inodot;
        escapesec[0x0222B] = "&#x0222B;"; // int;
        escapesec[0x022BA] = "&#x022BA;"; // intcal;
        escapesec[0x02124] = "&#x02124;"; // integers;
        escapesec[0x022BA] = "&#x022BA;"; // intercal;
        escapesec[0x02A17] = "&#x02A17;"; // intlarhk;
        escapesec[0x02A3C] = "&#x02A3C;"; // intprod;
        escapesec[0x00451] = "&#x00451;"; // iocy;
        escapesec[0x0012F] = "&#x0012F;"; // iogon;
        escapesec[0x1D55A] = "&#x1D55A;"; // iopf;
        escapesec[0x003B9] = "&#x003B9;"; // iota;
        escapesec[0x02A3C] = "&#x02A3C;"; // iprod;
        escapesec[0x000BF] = "&#x000BF;"; // iquest;
        escapesec[0x1D4BE] = "&#x1D4BE;"; // iscr;
        escapesec[0x02208] = "&#x02208;"; // isin;
        escapesec[0x022F9] = "&#x022F9;"; // isinE;
        escapesec[0x022F5] = "&#x022F5;"; // isindot;
        escapesec[0x022F4] = "&#x022F4;"; // isins;
        escapesec[0x022F3] = "&#x022F3;"; // isinsv;
        escapesec[0x02208] = "&#x02208;"; // isinv;
        escapesec[0x02062] = "&#x02062;"; // it;
        escapesec[0x00129] = "&#x00129;"; // itilde;
        escapesec[0x00456] = "&#x00456;"; // iukcy;
        escapesec[0x000EF] = "&#x000EF;"; // iuml;
        escapesec[0x00135] = "&#x00135;"; // jcirc;
        escapesec[0x00439] = "&#x00439;"; // jcy;
        escapesec[0x1D527] = "&#x1D527;"; // jfr;
        escapesec[0x00237] = "&#x00237;"; // jmath;
        escapesec[0x1D55B] = "&#x1D55B;"; // jopf;
        escapesec[0x1D4BF] = "&#x1D4BF;"; // jscr;
        escapesec[0x00458] = "&#x00458;"; // jsercy;
        escapesec[0x00454] = "&#x00454;"; // jukcy;
        escapesec[0x003BA] = "&#x003BA;"; // kappa;
        escapesec[0x003F0] = "&#x003F0;"; // kappav;
        escapesec[0x00137] = "&#x00137;"; // kcedil;
        escapesec[0x0043A] = "&#x0043A;"; // kcy;
        escapesec[0x1D528] = "&#x1D528;"; // kfr;
        escapesec[0x00138] = "&#x00138;"; // kgreen;
        escapesec[0x00445] = "&#x00445;"; // khcy;
        escapesec[0x0045C] = "&#x0045C;"; // kjcy;
        escapesec[0x1D55C] = "&#x1D55C;"; // kopf;
        escapesec[0x1D4C0] = "&#x1D4C0;"; // kscr;
        escapesec[0x021DA] = "&#x021DA;"; // lAarr;
        escapesec[0x021D0] = "&#x021D0;"; // lArr;
        escapesec[0x0291B] = "&#x0291B;"; // lAtail;
        escapesec[0x0290E] = "&#x0290E;"; // lBarr;
        escapesec[0x02266] = "&#x02266;"; // lE;
        escapesec[0x02A8B] = "&#x02A8B;"; // lEg;
        escapesec[0x02962] = "&#x02962;"; // lHar;
        escapesec[0x0013A] = "&#x0013A;"; // lacute;
        escapesec[0x029B4] = "&#x029B4;"; // laemptyv;
        escapesec[0x02112] = "&#x02112;"; // lagran;
        escapesec[0x003BB] = "&#x003BB;"; // lambda;
        escapesec[0x027E8] = "&#x027E8;"; // lang;
        escapesec[0x02991] = "&#x02991;"; // langd;
        escapesec[0x027E8] = "&#x027E8;"; // langle;
        escapesec[0x02A85] = "&#x02A85;"; // lap;
        escapesec[0x000AB] = "&#x000AB;"; // laquo;
        escapesec[0x02190] = "&#x02190;"; // larr;
        escapesec[0x021E4] = "&#x021E4;"; // larrb;
        escapesec[0x0291F] = "&#x0291F;"; // larrbfs;
        escapesec[0x0291D] = "&#x0291D;"; // larrfs;
        escapesec[0x021A9] = "&#x021A9;"; // larrhk;
        escapesec[0x021AB] = "&#x021AB;"; // larrlp;
        escapesec[0x02939] = "&#x02939;"; // larrpl;
        escapesec[0x02973] = "&#x02973;"; // larrsim;
        escapesec[0x021A2] = "&#x021A2;"; // larrtl;
        escapesec[0x02AAB] = "&#x02AAB;"; // lat;
        escapesec[0x02919] = "&#x02919;"; // latail;
        escapesec[0x02AAD] = "&#x02AAD;"; // late;
        escapesec[0x0290C] = "&#x0290C;"; // lbarr;
        escapesec[0x02772] = "&#x02772;"; // lbbrk;
        escapesec[0x0007B] = "&#x0007B;"; // lbrace;
        escapesec[0x0005B] = "&#x0005B;"; // lbrack;
        escapesec[0x0298B] = "&#x0298B;"; // lbrke;
        escapesec[0x0298F] = "&#x0298F;"; // lbrksld;
        escapesec[0x0298D] = "&#x0298D;"; // lbrkslu;
        escapesec[0x0013E] = "&#x0013E;"; // lcaron;
        escapesec[0x0013C] = "&#x0013C;"; // lcedil;
        escapesec[0x02308] = "&#x02308;"; // lceil;
        escapesec[0x0007B] = "&#x0007B;"; // lcub;
        escapesec[0x0043B] = "&#x0043B;"; // lcy;
        escapesec[0x02936] = "&#x02936;"; // ldca;
        escapesec[0x0201C] = "&#x0201C;"; // ldquo;
        escapesec[0x0201E] = "&#x0201E;"; // ldquor;
        escapesec[0x02967] = "&#x02967;"; // ldrdhar;
        escapesec[0x0294B] = "&#x0294B;"; // ldrushar;
        escapesec[0x021B2] = "&#x021B2;"; // ldsh;
        escapesec[0x02264] = "&#x02264;"; // le;
        escapesec[0x02190] = "&#x02190;"; // leftarrow;
        escapesec[0x021A2] = "&#x021A2;"; // leftarrowtail;
        escapesec[0x021BD] = "&#x021BD;"; // leftharpoondown;
        escapesec[0x021BC] = "&#x021BC;"; // leftharpoonup;
        escapesec[0x021C7] = "&#x021C7;"; // leftleftarrows;
        escapesec[0x02194] = "&#x02194;"; // leftrightarrow;
        escapesec[0x021C6] = "&#x021C6;"; // leftrightarrows;
        escapesec[0x021CB] = "&#x021CB;"; // leftrightharpoons;
        escapesec[0x021AD] = "&#x021AD;"; // leftrightsquigarrow;
        escapesec[0x022CB] = "&#x022CB;"; // leftthreetimes;
        escapesec[0x022DA] = "&#x022DA;"; // leg;
        escapesec[0x02264] = "&#x02264;"; // leq;
        escapesec[0x02266] = "&#x02266;"; // leqq;
        escapesec[0x02A7D] = "&#x02A7D;"; // leqslant;
        escapesec[0x02AA8] = "&#x02AA8;"; // lescc;
        escapesec[0x02A7F] = "&#x02A7F;"; // lesdot;
        escapesec[0x02A81] = "&#x02A81;"; // lesdoto;
        escapesec[0x02A83] = "&#x02A83;"; // lesdotor;
        escapesec[0x02A93] = "&#x02A93;"; // lesges;
        escapesec[0x02A85] = "&#x02A85;"; // lessapprox;
        escapesec[0x022D6] = "&#x022D6;"; // lessdot;
        escapesec[0x022DA] = "&#x022DA;"; // lesseqgtr;
        escapesec[0x02A8B] = "&#x02A8B;"; // lesseqqgtr;
        escapesec[0x02276] = "&#x02276;"; // lessgtr;
        escapesec[0x02272] = "&#x02272;"; // lesssim;
        escapesec[0x0297C] = "&#x0297C;"; // lfisht;
        escapesec[0x0230A] = "&#x0230A;"; // lfloor;
        escapesec[0x1D529] = "&#x1D529;"; // lfr;
        escapesec[0x02276] = "&#x02276;"; // lg;
        escapesec[0x02A91] = "&#x02A91;"; // lgE;
        escapesec[0x021BD] = "&#x021BD;"; // lhard;
        escapesec[0x021BC] = "&#x021BC;"; // lharu;
        escapesec[0x0296A] = "&#x0296A;"; // lharul;
        escapesec[0x02584] = "&#x02584;"; // lhblk;
        escapesec[0x00459] = "&#x00459;"; // ljcy;
        escapesec[0x0226A] = "&#x0226A;"; // ll;
        escapesec[0x021C7] = "&#x021C7;"; // llarr;
        escapesec[0x0231E] = "&#x0231E;"; // llcorner;
        escapesec[0x0296B] = "&#x0296B;"; // llhard;
        escapesec[0x025FA] = "&#x025FA;"; // lltri;
        escapesec[0x00140] = "&#x00140;"; // lmidot;
        escapesec[0x023B0] = "&#x023B0;"; // lmoust;
        escapesec[0x02268] = "&#x02268;"; // lnE;
        escapesec[0x02A89] = "&#x02A89;"; // lnap;
        escapesec[0x02A87] = "&#x02A87;"; // lne;
        escapesec[0x02268] = "&#x02268;"; // lneqq;
        escapesec[0x022E6] = "&#x022E6;"; // lnsim;
        escapesec[0x027EC] = "&#x027EC;"; // loang;
        escapesec[0x021FD] = "&#x021FD;"; // loarr;
        escapesec[0x027E6] = "&#x027E6;"; // lobrk;
        escapesec[0x027F5] = "&#x027F5;"; // longleftarrow;
        escapesec[0x027F7] = "&#x027F7;"; // longleftrightarrow;
        escapesec[0x027FC] = "&#x027FC;"; // longmapsto;
        escapesec[0x027F6] = "&#x027F6;"; // longrightarrow;
        escapesec[0x021AB] = "&#x021AB;"; // looparrowleft;
        escapesec[0x021AC] = "&#x021AC;"; // looparrowright;
        escapesec[0x02985] = "&#x02985;"; // lopar;
        escapesec[0x1D55D] = "&#x1D55D;"; // lopf;
        escapesec[0x02A2D] = "&#x02A2D;"; // loplus;
        escapesec[0x02A34] = "&#x02A34;"; // lotimes;
        escapesec[0x02217] = "&#x02217;"; // lowast;
        escapesec[0x0005F] = "&#x0005F;"; // lowbar;
        escapesec[0x025CA] = "&#x025CA;"; // loz;
        escapesec[0x029EB] = "&#x029EB;"; // lozf;
        escapesec[0x00028] = "&#x00028;"; // lpar;
        escapesec[0x02993] = "&#x02993;"; // lparlt;
        escapesec[0x021C6] = "&#x021C6;"; // lrarr;
        escapesec[0x0231F] = "&#x0231F;"; // lrcorner;
        escapesec[0x021CB] = "&#x021CB;"; // lrhar;
        escapesec[0x0296D] = "&#x0296D;"; // lrhard;
        escapesec[0x0200E] = "&#x0200E;"; // lrm;
        escapesec[0x022BF] = "&#x022BF;"; // lrtri;
        escapesec[0x02039] = "&#x02039;"; // lsaquo;
        escapesec[0x1D4C1] = "&#x1D4C1;"; // lscr;
        escapesec[0x021B0] = "&#x021B0;"; // lsh;
        escapesec[0x02272] = "&#x02272;"; // lsim;
        escapesec[0x02A8D] = "&#x02A8D;"; // lsime;
        escapesec[0x02A8F] = "&#x02A8F;"; // lsimg;
        escapesec[0x0005B] = "&#x0005B;"; // lsqb;
        escapesec[0x02018] = "&#x02018;"; // lsquo;
        escapesec[0x0201A] = "&#x0201A;"; // lsquor;
        escapesec[0x00142] = "&#x00142;"; // lstrok;
        escapesec[0x0003C] = "&#x0003C;"; // lt;
        escapesec[0x02AA6] = "&#x02AA6;"; // ltcc;
        escapesec[0x02A79] = "&#x02A79;"; // ltcir;
        escapesec[0x022D6] = "&#x022D6;"; // ltdot;
        escapesec[0x022CB] = "&#x022CB;"; // lthree;
        escapesec[0x022C9] = "&#x022C9;"; // ltimes;
        escapesec[0x02976] = "&#x02976;"; // ltlarr;
        escapesec[0x02A7B] = "&#x02A7B;"; // ltquest;
        escapesec[0x02996] = "&#x02996;"; // ltrPar;
        escapesec[0x025C3] = "&#x025C3;"; // ltri;
        escapesec[0x022B4] = "&#x022B4;"; // ltrie;
        escapesec[0x025C2] = "&#x025C2;"; // ltrif;
        escapesec[0x0294A] = "&#x0294A;"; // lurdshar;
        escapesec[0x02966] = "&#x02966;"; // luruhar;
        escapesec[0x0223A] = "&#x0223A;"; // mDDot;
        escapesec[0x000AF] = "&#x000AF;"; // macr;
        escapesec[0x02642] = "&#x02642;"; // male;
        escapesec[0x02720] = "&#x02720;"; // malt;
        escapesec[0x021A6] = "&#x021A6;"; // map;
        escapesec[0x021A7] = "&#x021A7;"; // mapstodown;
        escapesec[0x021A4] = "&#x021A4;"; // mapstoleft;
        escapesec[0x021A5] = "&#x021A5;"; // mapstoup;
        escapesec[0x025AE] = "&#x025AE;"; // marker;
        escapesec[0x02A29] = "&#x02A29;"; // mcomma;
        escapesec[0x0043C] = "&#x0043C;"; // mcy;
        escapesec[0x02014] = "&#x02014;"; // mdash;
        escapesec[0x02221] = "&#x02221;"; // measuredangle;
        escapesec[0x1D52A] = "&#x1D52A;"; // mfr;
        escapesec[0x02127] = "&#x02127;"; // mho;
        escapesec[0x000B5] = "&#x000B5;"; // micro;
        escapesec[0x02223] = "&#x02223;"; // mid;
        escapesec[0x0002A] = "&#x0002A;"; // midast;
        escapesec[0x02AF0] = "&#x02AF0;"; // midcir;
        escapesec[0x000B7] = "&#x000B7;"; // middot;
        escapesec[0x02212] = "&#x02212;"; // minus;
        escapesec[0x0229F] = "&#x0229F;"; // minusb;
        escapesec[0x02238] = "&#x02238;"; // minusd;
        escapesec[0x02A2A] = "&#x02A2A;"; // minusdu;
        escapesec[0x02ADB] = "&#x02ADB;"; // mlcp;
        escapesec[0x02026] = "&#x02026;"; // mldr;
        escapesec[0x02213] = "&#x02213;"; // mnplus;
        escapesec[0x022A7] = "&#x022A7;"; // models;
        escapesec[0x1D55E] = "&#x1D55E;"; // mopf;
        escapesec[0x02213] = "&#x02213;"; // mp;
        escapesec[0x1D4C2] = "&#x1D4C2;"; // mscr;
        escapesec[0x0223E] = "&#x0223E;"; // mstpos;
        escapesec[0x003BC] = "&#x003BC;"; // mu;
        escapesec[0x022B8] = "&#x022B8;"; // multimap;
        escapesec[0x021CD] = "&#x021CD;"; // nLeftarrow;
        escapesec[0x021CE] = "&#x021CE;"; // nLeftrightarrow;
        escapesec[0x021CF] = "&#x021CF;"; // nRightarrow;
        escapesec[0x022AF] = "&#x022AF;"; // nVDash;
        escapesec[0x022AE] = "&#x022AE;"; // nVdash;
        escapesec[0x02207] = "&#x02207;"; // nabla;
        escapesec[0x00144] = "&#x00144;"; // nacute;
        escapesec[0x02249] = "&#x02249;"; // nap;
        escapesec[0x00149] = "&#x00149;"; // napos;
        escapesec[0x02249] = "&#x02249;"; // napprox;
        escapesec[0x0266E] = "&#x0266E;"; // natur;
        escapesec[0x02115] = "&#x02115;"; // naturals;
        escapesec[0x000A0] = "&#x000A0;"; // nbsp;
        escapesec[0x02A43] = "&#x02A43;"; // ncap;
        escapesec[0x00148] = "&#x00148;"; // ncaron;
        escapesec[0x00146] = "&#x00146;"; // ncedil;
        escapesec[0x02247] = "&#x02247;"; // ncong;
        escapesec[0x02A42] = "&#x02A42;"; // ncup;
        escapesec[0x0043D] = "&#x0043D;"; // ncy;
        escapesec[0x02013] = "&#x02013;"; // ndash;
        escapesec[0x02260] = "&#x02260;"; // ne;
        escapesec[0x021D7] = "&#x021D7;"; // neArr;
        escapesec[0x02924] = "&#x02924;"; // nearhk;
        escapesec[0x02197] = "&#x02197;"; // nearr;
        escapesec[0x02262] = "&#x02262;"; // nequiv;
        escapesec[0x02928] = "&#x02928;"; // nesear;
        escapesec[0x02204] = "&#x02204;"; // nexist;
        escapesec[0x1D52B] = "&#x1D52B;"; // nfr;
        escapesec[0x02271] = "&#x02271;"; // nge;
        escapesec[0x02275] = "&#x02275;"; // ngsim;
        escapesec[0x0226F] = "&#x0226F;"; // ngt;
        escapesec[0x021CE] = "&#x021CE;"; // nhArr;
        escapesec[0x021AE] = "&#x021AE;"; // nharr;
        escapesec[0x02AF2] = "&#x02AF2;"; // nhpar;
        escapesec[0x0220B] = "&#x0220B;"; // ni;
        escapesec[0x022FC] = "&#x022FC;"; // nis;
        escapesec[0x022FA] = "&#x022FA;"; // nisd;
        escapesec[0x0220B] = "&#x0220B;"; // niv;
        escapesec[0x0045A] = "&#x0045A;"; // njcy;
        escapesec[0x021CD] = "&#x021CD;"; // nlArr;
        escapesec[0x0219A] = "&#x0219A;"; // nlarr;
        escapesec[0x02025] = "&#x02025;"; // nldr;
        escapesec[0x02270] = "&#x02270;"; // nle;
        escapesec[0x0219A] = "&#x0219A;"; // nleftarrow;
        escapesec[0x021AE] = "&#x021AE;"; // nleftrightarrow;
        escapesec[0x02270] = "&#x02270;"; // nleq;
        escapesec[0x0226E] = "&#x0226E;"; // nless;
        escapesec[0x02274] = "&#x02274;"; // nlsim;
        escapesec[0x0226E] = "&#x0226E;"; // nlt;
        escapesec[0x022EA] = "&#x022EA;"; // nltri;
        escapesec[0x022EC] = "&#x022EC;"; // nltrie;
        escapesec[0x02224] = "&#x02224;"; // nmid;
        escapesec[0x1D55F] = "&#x1D55F;"; // nopf;
        escapesec[0x000AC] = "&#x000AC;"; // not;
        escapesec[0x02209] = "&#x02209;"; // notin;
        escapesec[0x022F7] = "&#x022F7;"; // notinvb;
        escapesec[0x022F6] = "&#x022F6;"; // notinvc;
        escapesec[0x0220C] = "&#x0220C;"; // notni;
        escapesec[0x022FE] = "&#x022FE;"; // notnivb;
        escapesec[0x022FD] = "&#x022FD;"; // notnivc;
        escapesec[0x02226] = "&#x02226;"; // npar;
        escapesec[0x02A14] = "&#x02A14;"; // npolint;
        escapesec[0x02280] = "&#x02280;"; // npr;
        escapesec[0x022E0] = "&#x022E0;"; // nprcue;
        escapesec[0x02280] = "&#x02280;"; // nprec;
        escapesec[0x021CF] = "&#x021CF;"; // nrArr;
        escapesec[0x0219B] = "&#x0219B;"; // nrarr;
        escapesec[0x022EB] = "&#x022EB;"; // nrtri;
        escapesec[0x022ED] = "&#x022ED;"; // nrtrie;
        escapesec[0x02281] = "&#x02281;"; // nsc;
        escapesec[0x022E1] = "&#x022E1;"; // nsccue;
        escapesec[0x1D4C3] = "&#x1D4C3;"; // nscr;
        escapesec[0x02224] = "&#x02224;"; // nshortmid;
        escapesec[0x02226] = "&#x02226;"; // nshortparallel;
        escapesec[0x02241] = "&#x02241;"; // nsim;
        escapesec[0x02244] = "&#x02244;"; // nsime;
        escapesec[0x02224] = "&#x02224;"; // nsmid;
        escapesec[0x02226] = "&#x02226;"; // nspar;
        escapesec[0x022E2] = "&#x022E2;"; // nsqsube;
        escapesec[0x022E3] = "&#x022E3;"; // nsqsupe;
        escapesec[0x02284] = "&#x02284;"; // nsub;
        escapesec[0x02288] = "&#x02288;"; // nsube;
        escapesec[0x02281] = "&#x02281;"; // nsucc;
        escapesec[0x02285] = "&#x02285;"; // nsup;
        escapesec[0x02289] = "&#x02289;"; // nsupe;
        escapesec[0x02279] = "&#x02279;"; // ntgl;
        escapesec[0x000F1] = "&#x000F1;"; // ntilde;
        escapesec[0x02278] = "&#x02278;"; // ntlg;
        escapesec[0x022EA] = "&#x022EA;"; // ntriangleleft;
        escapesec[0x022EC] = "&#x022EC;"; // ntrianglelefteq;
        escapesec[0x022EB] = "&#x022EB;"; // ntriangleright;
        escapesec[0x022ED] = "&#x022ED;"; // ntrianglerighteq;
        escapesec[0x003BD] = "&#x003BD;"; // nu;
        escapesec[0x00023] = "&#x00023;"; // num;
        escapesec[0x02116] = "&#x02116;"; // numero;
        escapesec[0x02007] = "&#x02007;"; // numsp;
        escapesec[0x022AD] = "&#x022AD;"; // nvDash;
        escapesec[0x02904] = "&#x02904;"; // nvHarr;
        escapesec[0x022AC] = "&#x022AC;"; // nvdash;
        escapesec[0x029DE] = "&#x029DE;"; // nvinfin;
        escapesec[0x02902] = "&#x02902;"; // nvlArr;
        escapesec[0x02903] = "&#x02903;"; // nvrArr;
        escapesec[0x021D6] = "&#x021D6;"; // nwArr;
        escapesec[0x02923] = "&#x02923;"; // nwarhk;
        escapesec[0x02196] = "&#x02196;"; // nwarr;
        escapesec[0x02927] = "&#x02927;"; // nwnear;
        escapesec[0x024C8] = "&#x024C8;"; // oS;
        escapesec[0x000F3] = "&#x000F3;"; // oacute;
        escapesec[0x0229B] = "&#x0229B;"; // oast;
        escapesec[0x0229A] = "&#x0229A;"; // ocir;
        escapesec[0x000F4] = "&#x000F4;"; // ocirc;
        escapesec[0x0043E] = "&#x0043E;"; // ocy;
        escapesec[0x0229D] = "&#x0229D;"; // odash;
        escapesec[0x00151] = "&#x00151;"; // odblac;
        escapesec[0x02A38] = "&#x02A38;"; // odiv;
        escapesec[0x02299] = "&#x02299;"; // odot;
        escapesec[0x029BC] = "&#x029BC;"; // odsold;
        escapesec[0x00153] = "&#x00153;"; // oelig;
        escapesec[0x029BF] = "&#x029BF;"; // ofcir;
        escapesec[0x1D52C] = "&#x1D52C;"; // ofr;
        escapesec[0x002DB] = "&#x002DB;"; // ogon;
        escapesec[0x000F2] = "&#x000F2;"; // ograve;
        escapesec[0x029C1] = "&#x029C1;"; // ogt;
        escapesec[0x029B5] = "&#x029B5;"; // ohbar;
        escapesec[0x003A9] = "&#x003A9;"; // ohm;
        escapesec[0x0222E] = "&#x0222E;"; // oint;
        escapesec[0x021BA] = "&#x021BA;"; // olarr;
        escapesec[0x029BE] = "&#x029BE;"; // olcir;
        escapesec[0x029BB] = "&#x029BB;"; // olcross;
        escapesec[0x0203E] = "&#x0203E;"; // oline;
        escapesec[0x029C0] = "&#x029C0;"; // olt;
        escapesec[0x0014D] = "&#x0014D;"; // omacr;
        escapesec[0x003C9] = "&#x003C9;"; // omega;
        escapesec[0x003BF] = "&#x003BF;"; // omicron;
        escapesec[0x029B6] = "&#x029B6;"; // omid;
        escapesec[0x02296] = "&#x02296;"; // ominus;
        escapesec[0x1D560] = "&#x1D560;"; // oopf;
        escapesec[0x029B7] = "&#x029B7;"; // opar;
        escapesec[0x029B9] = "&#x029B9;"; // operp;
        escapesec[0x02295] = "&#x02295;"; // oplus;
        escapesec[0x02228] = "&#x02228;"; // or;
        escapesec[0x021BB] = "&#x021BB;"; // orarr;
        escapesec[0x02A5D] = "&#x02A5D;"; // ord;
        escapesec[0x02134] = "&#x02134;"; // order;
        escapesec[0x000AA] = "&#x000AA;"; // ordf;
        escapesec[0x000BA] = "&#x000BA;"; // ordm;
        escapesec[0x022B6] = "&#x022B6;"; // origof;
        escapesec[0x02A56] = "&#x02A56;"; // oror;
        escapesec[0x02A57] = "&#x02A57;"; // orslope;
        escapesec[0x02A5B] = "&#x02A5B;"; // orv;
        escapesec[0x02134] = "&#x02134;"; // oscr;
        escapesec[0x000F8] = "&#x000F8;"; // oslash;
        escapesec[0x02298] = "&#x02298;"; // osol;
        escapesec[0x000F5] = "&#x000F5;"; // otilde;
        escapesec[0x02297] = "&#x02297;"; // otimes;
        escapesec[0x02A36] = "&#x02A36;"; // otimesas;
        escapesec[0x000F6] = "&#x000F6;"; // ouml;
        escapesec[0x0233D] = "&#x0233D;"; // ovbar;
        escapesec[0x02225] = "&#x02225;"; // par;
        escapesec[0x000B6] = "&#x000B6;"; // para;
        escapesec[0x02225] = "&#x02225;"; // parallel;
        escapesec[0x02AF3] = "&#x02AF3;"; // parsim;
        escapesec[0x02AFD] = "&#x02AFD;"; // parsl;
        escapesec[0x02202] = "&#x02202;"; // part;
        escapesec[0x0043F] = "&#x0043F;"; // pcy;
        escapesec[0x00025] = "&#x00025;"; // percnt;
        escapesec[0x0002E] = "&#x0002E;"; // period;
        escapesec[0x02030] = "&#x02030;"; // permil;
        escapesec[0x022A5] = "&#x022A5;"; // perp;
        escapesec[0x02031] = "&#x02031;"; // pertenk;
        escapesec[0x1D52D] = "&#x1D52D;"; // pfr;
        escapesec[0x003C6] = "&#x003C6;"; // phi;
        escapesec[0x003D5] = "&#x003D5;"; // phiv;
        escapesec[0x02133] = "&#x02133;"; // phmmat;
        escapesec[0x0260E] = "&#x0260E;"; // phone;
        escapesec[0x003C0] = "&#x003C0;"; // pi;
        escapesec[0x022D4] = "&#x022D4;"; // pitchfork;
        escapesec[0x003D6] = "&#x003D6;"; // piv;
        escapesec[0x0210F] = "&#x0210F;"; // planck;
        escapesec[0x0210E] = "&#x0210E;"; // planckh;
        escapesec[0x0210F] = "&#x0210F;"; // plankv;
        escapesec[0x0002B] = "&#x0002B;"; // plus;
        escapesec[0x02A23] = "&#x02A23;"; // plusacir;
        escapesec[0x0229E] = "&#x0229E;"; // plusb;
        escapesec[0x02A22] = "&#x02A22;"; // pluscir;
        escapesec[0x02214] = "&#x02214;"; // plusdo;
        escapesec[0x02A25] = "&#x02A25;"; // plusdu;
        escapesec[0x02A72] = "&#x02A72;"; // pluse;
        escapesec[0x000B1] = "&#x000B1;"; // plusmn;
        escapesec[0x02A26] = "&#x02A26;"; // plussim;
        escapesec[0x02A27] = "&#x02A27;"; // plustwo;
        escapesec[0x000B1] = "&#x000B1;"; // pm;
        escapesec[0x02A15] = "&#x02A15;"; // pointint;
        escapesec[0x1D561] = "&#x1D561;"; // popf;
        escapesec[0x000A3] = "&#x000A3;"; // pound;
        escapesec[0x0227A] = "&#x0227A;"; // pr;
        escapesec[0x02AB3] = "&#x02AB3;"; // prE;
        escapesec[0x02AB7] = "&#x02AB7;"; // prap;
        escapesec[0x0227C] = "&#x0227C;"; // prcue;
        escapesec[0x02AAF] = "&#x02AAF;"; // pre;
        escapesec[0x0227A] = "&#x0227A;"; // prec;
        escapesec[0x02AB7] = "&#x02AB7;"; // precapprox;
        escapesec[0x0227C] = "&#x0227C;"; // preccurlyeq;
        escapesec[0x02AAF] = "&#x02AAF;"; // preceq;
        escapesec[0x02AB9] = "&#x02AB9;"; // precnapprox;
        escapesec[0x02AB5] = "&#x02AB5;"; // precneqq;
        escapesec[0x022E8] = "&#x022E8;"; // precnsim;
        escapesec[0x0227E] = "&#x0227E;"; // precsim;
        escapesec[0x02032] = "&#x02032;"; // prime;
        escapesec[0x02119] = "&#x02119;"; // primes;
        escapesec[0x02AB5] = "&#x02AB5;"; // prnE;
        escapesec[0x02AB9] = "&#x02AB9;"; // prnap;
        escapesec[0x022E8] = "&#x022E8;"; // prnsim;
        escapesec[0x0220F] = "&#x0220F;"; // prod;
        escapesec[0x0232E] = "&#x0232E;"; // profalar;
        escapesec[0x02312] = "&#x02312;"; // profline;
        escapesec[0x02313] = "&#x02313;"; // profsurf;
        escapesec[0x0221D] = "&#x0221D;"; // prop;
        escapesec[0x0227E] = "&#x0227E;"; // prsim;
        escapesec[0x022B0] = "&#x022B0;"; // prurel;
        escapesec[0x1D4C5] = "&#x1D4C5;"; // pscr;
        escapesec[0x003C8] = "&#x003C8;"; // psi;
        escapesec[0x02008] = "&#x02008;"; // puncsp;
        escapesec[0x1D52E] = "&#x1D52E;"; // qfr;
        escapesec[0x02A0C] = "&#x02A0C;"; // qint;
        escapesec[0x1D562] = "&#x1D562;"; // qopf;
        escapesec[0x02057] = "&#x02057;"; // qprime;
        escapesec[0x1D4C6] = "&#x1D4C6;"; // qscr;
        escapesec[0x0210D] = "&#x0210D;"; // quaternions;
        escapesec[0x02A16] = "&#x02A16;"; // quatint;
        escapesec[0x0003F] = "&#x0003F;"; // quest;
        escapesec[0x0225F] = "&#x0225F;"; // questeq;
        escapesec[0x00022] = "&#x00022;"; // quot;
        escapesec[0x021DB] = "&#x021DB;"; // rAarr;
        escapesec[0x021D2] = "&#x021D2;"; // rArr;
        escapesec[0x0291C] = "&#x0291C;"; // rAtail;
        escapesec[0x0290F] = "&#x0290F;"; // rBarr;
        escapesec[0x02964] = "&#x02964;"; // rHar;
        escapesec[0x00155] = "&#x00155;"; // racute;
        escapesec[0x0221A] = "&#x0221A;"; // radic;
        escapesec[0x029B3] = "&#x029B3;"; // raemptyv;
        escapesec[0x027E9] = "&#x027E9;"; // rang;
        escapesec[0x02992] = "&#x02992;"; // rangd;
        escapesec[0x029A5] = "&#x029A5;"; // range;
        escapesec[0x027E9] = "&#x027E9;"; // rangle;
        escapesec[0x000BB] = "&#x000BB;"; // raquo;
        escapesec[0x02192] = "&#x02192;"; // rarr;
        escapesec[0x02975] = "&#x02975;"; // rarrap;
        escapesec[0x021E5] = "&#x021E5;"; // rarrb;
        escapesec[0x02920] = "&#x02920;"; // rarrbfs;
        escapesec[0x02933] = "&#x02933;"; // rarrc;
        escapesec[0x0291E] = "&#x0291E;"; // rarrfs;
        escapesec[0x021AA] = "&#x021AA;"; // rarrhk;
        escapesec[0x021AC] = "&#x021AC;"; // rarrlp;
        escapesec[0x02945] = "&#x02945;"; // rarrpl;
        escapesec[0x02974] = "&#x02974;"; // rarrsim;
        escapesec[0x021A3] = "&#x021A3;"; // rarrtl;
        escapesec[0x0219D] = "&#x0219D;"; // rarrw;
        escapesec[0x0291A] = "&#x0291A;"; // ratail;
        escapesec[0x02236] = "&#x02236;"; // ratio;
        escapesec[0x0211A] = "&#x0211A;"; // rationals;
        escapesec[0x0290D] = "&#x0290D;"; // rbarr;
        escapesec[0x02773] = "&#x02773;"; // rbbrk;
        escapesec[0x0007D] = "&#x0007D;"; // rbrace;
        escapesec[0x0005D] = "&#x0005D;"; // rbrack;
        escapesec[0x0298C] = "&#x0298C;"; // rbrke;
        escapesec[0x0298E] = "&#x0298E;"; // rbrksld;
        escapesec[0x02990] = "&#x02990;"; // rbrkslu;
        escapesec[0x00159] = "&#x00159;"; // rcaron;
        escapesec[0x00157] = "&#x00157;"; // rcedil;
        escapesec[0x02309] = "&#x02309;"; // rceil;
        escapesec[0x0007D] = "&#x0007D;"; // rcub;
        escapesec[0x00440] = "&#x00440;"; // rcy;
        escapesec[0x02937] = "&#x02937;"; // rdca;
        escapesec[0x02969] = "&#x02969;"; // rdldhar;
        escapesec[0x0201D] = "&#x0201D;"; // rdquo;
        escapesec[0x021B3] = "&#x021B3;"; // rdsh;
        escapesec[0x0211C] = "&#x0211C;"; // real;
        escapesec[0x0211B] = "&#x0211B;"; // realine;
        escapesec[0x0211C] = "&#x0211C;"; // realpart;
        escapesec[0x0211D] = "&#x0211D;"; // reals;
        escapesec[0x025AD] = "&#x025AD;"; // rect;
        escapesec[0x000AE] = "&#x000AE;"; // reg;
        escapesec[0x0297D] = "&#x0297D;"; // rfisht;
        escapesec[0x0230B] = "&#x0230B;"; // rfloor;
        escapesec[0x1D52F] = "&#x1D52F;"; // rfr;
        escapesec[0x021C1] = "&#x021C1;"; // rhard;
        escapesec[0x021C0] = "&#x021C0;"; // rharu;
        escapesec[0x0296C] = "&#x0296C;"; // rharul;
        escapesec[0x003C1] = "&#x003C1;"; // rho;
        escapesec[0x003F1] = "&#x003F1;"; // rhov;
        escapesec[0x02192] = "&#x02192;"; // rightarrow;
        escapesec[0x021A3] = "&#x021A3;"; // rightarrowtail;
        escapesec[0x021C1] = "&#x021C1;"; // rightharpoondown;
        escapesec[0x021C0] = "&#x021C0;"; // rightharpoonup;
        escapesec[0x021C4] = "&#x021C4;"; // rightleftarrows;
        escapesec[0x021CC] = "&#x021CC;"; // rightleftharpoons;
        escapesec[0x021C9] = "&#x021C9;"; // rightrightarrows;
        escapesec[0x0219D] = "&#x0219D;"; // rightsquigarrow;
        escapesec[0x022CC] = "&#x022CC;"; // rightthreetimes;
        escapesec[0x002DA] = "&#x002DA;"; // ring;
        escapesec[0x02253] = "&#x02253;"; // risingdotseq;
        escapesec[0x021C4] = "&#x021C4;"; // rlarr;
        escapesec[0x021CC] = "&#x021CC;"; // rlhar;
        escapesec[0x0200F] = "&#x0200F;"; // rlm;
        escapesec[0x023B1] = "&#x023B1;"; // rmoust;
        escapesec[0x02AEE] = "&#x02AEE;"; // rnmid;
        escapesec[0x027ED] = "&#x027ED;"; // roang;
        escapesec[0x021FE] = "&#x021FE;"; // roarr;
        escapesec[0x027E7] = "&#x027E7;"; // robrk;
        escapesec[0x02986] = "&#x02986;"; // ropar;
        escapesec[0x1D563] = "&#x1D563;"; // ropf;
        escapesec[0x02A2E] = "&#x02A2E;"; // roplus;
        escapesec[0x02A35] = "&#x02A35;"; // rotimes;
        escapesec[0x00029] = "&#x00029;"; // rpar;
        escapesec[0x02994] = "&#x02994;"; // rpargt;
        escapesec[0x02A12] = "&#x02A12;"; // rppolint;
        escapesec[0x021C9] = "&#x021C9;"; // rrarr;
        escapesec[0x0203A] = "&#x0203A;"; // rsaquo;
        escapesec[0x1D4C7] = "&#x1D4C7;"; // rscr;
        escapesec[0x021B1] = "&#x021B1;"; // rsh;
        escapesec[0x0005D] = "&#x0005D;"; // rsqb;
        escapesec[0x02019] = "&#x02019;"; // rsquo;
        escapesec[0x022CC] = "&#x022CC;"; // rthree;
        escapesec[0x022CA] = "&#x022CA;"; // rtimes;
        escapesec[0x025B9] = "&#x025B9;"; // rtri;
        escapesec[0x022B5] = "&#x022B5;"; // rtrie;
        escapesec[0x025B8] = "&#x025B8;"; // rtrif;
        escapesec[0x029CE] = "&#x029CE;"; // rtriltri;
        escapesec[0x02968] = "&#x02968;"; // ruluhar;
        escapesec[0x0211E] = "&#x0211E;"; // rx;
        escapesec[0x0015B] = "&#x0015B;"; // sacute;
        escapesec[0x0201A] = "&#x0201A;"; // sbquo;
        escapesec[0x0227B] = "&#x0227B;"; // sc;
        escapesec[0x02AB4] = "&#x02AB4;"; // scE;
        escapesec[0x02AB8] = "&#x02AB8;"; // scap;
        escapesec[0x00161] = "&#x00161;"; // scaron;
        escapesec[0x0227D] = "&#x0227D;"; // sccue;
        escapesec[0x02AB0] = "&#x02AB0;"; // sce;
        escapesec[0x0015F] = "&#x0015F;"; // scedil;
        escapesec[0x0015D] = "&#x0015D;"; // scirc;
        escapesec[0x02AB6] = "&#x02AB6;"; // scnE;
        escapesec[0x02ABA] = "&#x02ABA;"; // scnap;
        escapesec[0x022E9] = "&#x022E9;"; // scnsim;
        escapesec[0x02A13] = "&#x02A13;"; // scpolint;
        escapesec[0x0227F] = "&#x0227F;"; // scsim;
        escapesec[0x00441] = "&#x00441;"; // scy;
        escapesec[0x022C5] = "&#x022C5;"; // sdot;
        escapesec[0x022A1] = "&#x022A1;"; // sdotb;
        escapesec[0x02A66] = "&#x02A66;"; // sdote;
        escapesec[0x021D8] = "&#x021D8;"; // seArr;
        escapesec[0x02925] = "&#x02925;"; // searhk;
        escapesec[0x02198] = "&#x02198;"; // searr;
        escapesec[0x000A7] = "&#x000A7;"; // sect;
        escapesec[0x0003B] = "&#x0003B;"; // semi;
        escapesec[0x02929] = "&#x02929;"; // seswar;
        escapesec[0x02216] = "&#x02216;"; // setminus;
        escapesec[0x02736] = "&#x02736;"; // sext;
        escapesec[0x1D530] = "&#x1D530;"; // sfr;
        escapesec[0x02322] = "&#x02322;"; // sfrown;
        escapesec[0x0266F] = "&#x0266F;"; // sharp;
        escapesec[0x00449] = "&#x00449;"; // shchcy;
        escapesec[0x00448] = "&#x00448;"; // shcy;
        escapesec[0x02223] = "&#x02223;"; // shortmid;
        escapesec[0x02225] = "&#x02225;"; // shortparallel;
        escapesec[0x000AD] = "&#x000AD;"; // shy;
        escapesec[0x003C3] = "&#x003C3;"; // sigma;
        escapesec[0x003C2] = "&#x003C2;"; // sigmaf;
        escapesec[0x0223C] = "&#x0223C;"; // sim;
        escapesec[0x02A6A] = "&#x02A6A;"; // simdot;
        escapesec[0x02243] = "&#x02243;"; // sime;
        escapesec[0x02A9E] = "&#x02A9E;"; // simg;
        escapesec[0x02AA0] = "&#x02AA0;"; // simgE;
        escapesec[0x02A9D] = "&#x02A9D;"; // siml;
        escapesec[0x02A9F] = "&#x02A9F;"; // simlE;
        escapesec[0x02246] = "&#x02246;"; // simne;
        escapesec[0x02A24] = "&#x02A24;"; // simplus;
        escapesec[0x02972] = "&#x02972;"; // simrarr;
        escapesec[0x02190] = "&#x02190;"; // slarr;
        escapesec[0x02216] = "&#x02216;"; // smallsetminus;
        escapesec[0x02A33] = "&#x02A33;"; // smashp;
        escapesec[0x029E4] = "&#x029E4;"; // smeparsl;
        escapesec[0x02223] = "&#x02223;"; // smid;
        escapesec[0x02323] = "&#x02323;"; // smile;
        escapesec[0x02AAA] = "&#x02AAA;"; // smt;
        escapesec[0x02AAC] = "&#x02AAC;"; // smte;
        escapesec[0x0044C] = "&#x0044C;"; // softcy;
        escapesec[0x0002F] = "&#x0002F;"; // sol;
        escapesec[0x029C4] = "&#x029C4;"; // solb;
        escapesec[0x0233F] = "&#x0233F;"; // solbar;
        escapesec[0x1D564] = "&#x1D564;"; // sopf;
        escapesec[0x02660] = "&#x02660;"; // spades;
        escapesec[0x02225] = "&#x02225;"; // spar;
        escapesec[0x02293] = "&#x02293;"; // sqcap;
        escapesec[0x02294] = "&#x02294;"; // sqcup;
        escapesec[0x0228F] = "&#x0228F;"; // sqsub;
        escapesec[0x02291] = "&#x02291;"; // sqsube;
        escapesec[0x0228F] = "&#x0228F;"; // sqsubset;
        escapesec[0x02291] = "&#x02291;"; // sqsubseteq;
        escapesec[0x02290] = "&#x02290;"; // sqsup;
        escapesec[0x02292] = "&#x02292;"; // sqsupe;
        escapesec[0x02290] = "&#x02290;"; // sqsupset;
        escapesec[0x02292] = "&#x02292;"; // sqsupseteq;
        escapesec[0x025A1] = "&#x025A1;"; // squ;
        escapesec[0x025AA] = "&#x025AA;"; // squarf;
        escapesec[0x02192] = "&#x02192;"; // srarr;
        escapesec[0x1D4C8] = "&#x1D4C8;"; // sscr;
        escapesec[0x02216] = "&#x02216;"; // ssetmn;
        escapesec[0x02323] = "&#x02323;"; // ssmile;
        escapesec[0x022C6] = "&#x022C6;"; // sstarf;
        escapesec[0x02606] = "&#x02606;"; // star;
        escapesec[0x02605] = "&#x02605;"; // starf;
        escapesec[0x003F5] = "&#x003F5;"; // straightepsilon;
        escapesec[0x003D5] = "&#x003D5;"; // straightphi;
        escapesec[0x000AF] = "&#x000AF;"; // strns;
        escapesec[0x02282] = "&#x02282;"; // sub;
        escapesec[0x02AC5] = "&#x02AC5;"; // subE;
        escapesec[0x02ABD] = "&#x02ABD;"; // subdot;
        escapesec[0x02286] = "&#x02286;"; // sube;
        escapesec[0x02AC3] = "&#x02AC3;"; // subedot;
        escapesec[0x02AC1] = "&#x02AC1;"; // submult;
        escapesec[0x02ACB] = "&#x02ACB;"; // subnE;
        escapesec[0x0228A] = "&#x0228A;"; // subne;
        escapesec[0x02ABF] = "&#x02ABF;"; // subplus;
        escapesec[0x02979] = "&#x02979;"; // subrarr;
        escapesec[0x02282] = "&#x02282;"; // subset;
        escapesec[0x02286] = "&#x02286;"; // subseteq;
        escapesec[0x02AC5] = "&#x02AC5;"; // subseteqq;
        escapesec[0x0228A] = "&#x0228A;"; // subsetneq;
        escapesec[0x02ACB] = "&#x02ACB;"; // subsetneqq;
        escapesec[0x02AC7] = "&#x02AC7;"; // subsim;
        escapesec[0x02AD5] = "&#x02AD5;"; // subsub;
        escapesec[0x02AD3] = "&#x02AD3;"; // subsup;
        escapesec[0x0227B] = "&#x0227B;"; // succ;
        escapesec[0x02AB8] = "&#x02AB8;"; // succapprox;
        escapesec[0x0227D] = "&#x0227D;"; // succcurlyeq;
        escapesec[0x02AB0] = "&#x02AB0;"; // succeq;
        escapesec[0x02ABA] = "&#x02ABA;"; // succnapprox;
        escapesec[0x02AB6] = "&#x02AB6;"; // succneqq;
        escapesec[0x022E9] = "&#x022E9;"; // succnsim;
        escapesec[0x0227F] = "&#x0227F;"; // succsim;
        escapesec[0x02211] = "&#x02211;"; // sum;
        escapesec[0x0266A] = "&#x0266A;"; // sung;
        escapesec[0x000B9] = "&#x000B9;"; // sup1;
        escapesec[0x000B2] = "&#x000B2;"; // sup2;
        escapesec[0x000B3] = "&#x000B3;"; // sup3;
        escapesec[0x02283] = "&#x02283;"; // sup;
        escapesec[0x02AC6] = "&#x02AC6;"; // supE;
        escapesec[0x02ABE] = "&#x02ABE;"; // supdot;
        escapesec[0x02AD8] = "&#x02AD8;"; // supdsub;
        escapesec[0x02287] = "&#x02287;"; // supe;
        escapesec[0x02AC4] = "&#x02AC4;"; // supedot;
        escapesec[0x027C9] = "&#x027C9;"; // suphsol;
        escapesec[0x02AD7] = "&#x02AD7;"; // suphsub;
        escapesec[0x0297B] = "&#x0297B;"; // suplarr;
        escapesec[0x02AC2] = "&#x02AC2;"; // supmult;
        escapesec[0x02ACC] = "&#x02ACC;"; // supnE;
        escapesec[0x0228B] = "&#x0228B;"; // supne;
        escapesec[0x02AC0] = "&#x02AC0;"; // supplus;
        escapesec[0x02283] = "&#x02283;"; // supset;
        escapesec[0x02287] = "&#x02287;"; // supseteq;
        escapesec[0x02AC6] = "&#x02AC6;"; // supseteqq;
        escapesec[0x0228B] = "&#x0228B;"; // supsetneq;
        escapesec[0x02ACC] = "&#x02ACC;"; // supsetneqq;
        escapesec[0x02AC8] = "&#x02AC8;"; // supsim;
        escapesec[0x02AD4] = "&#x02AD4;"; // supsub;
        escapesec[0x02AD6] = "&#x02AD6;"; // supsup;
        escapesec[0x021D9] = "&#x021D9;"; // swArr;
        escapesec[0x02926] = "&#x02926;"; // swarhk;
        escapesec[0x02199] = "&#x02199;"; // swarr;
        escapesec[0x0292A] = "&#x0292A;"; // swnwar;
        escapesec[0x000DF] = "&#x000DF;"; // szlig;
        escapesec[0x02316] = "&#x02316;"; // target;
        escapesec[0x003C4] = "&#x003C4;"; // tau;
        escapesec[0x023B4] = "&#x023B4;"; // tbrk;
        escapesec[0x00165] = "&#x00165;"; // tcaron;
        escapesec[0x00163] = "&#x00163;"; // tcedil;
        escapesec[0x00442] = "&#x00442;"; // tcy;
        escapesec[0x020DB] = "&#x020DB;"; // tdot;
        escapesec[0x02315] = "&#x02315;"; // telrec;
        escapesec[0x1D531] = "&#x1D531;"; // tfr;
        escapesec[0x02234] = "&#x02234;"; // there4;
        escapesec[0x003B8] = "&#x003B8;"; // theta;
        escapesec[0x003D1] = "&#x003D1;"; // thetasym;
        escapesec[0x02248] = "&#x02248;"; // thickapprox;
        escapesec[0x0223C] = "&#x0223C;"; // thicksim;
        escapesec[0x02009] = "&#x02009;"; // thinsp;
        escapesec[0x02248] = "&#x02248;"; // thkap;
        escapesec[0x0223C] = "&#x0223C;"; // thksim;
        escapesec[0x000FE] = "&#x000FE;"; // thorn;
        escapesec[0x002DC] = "&#x002DC;"; // tilde;
        escapesec[0x000D7] = "&#x000D7;"; // times;
        escapesec[0x022A0] = "&#x022A0;"; // timesb;
        escapesec[0x02A31] = "&#x02A31;"; // timesbar;
        escapesec[0x02A30] = "&#x02A30;"; // timesd;
        escapesec[0x0222D] = "&#x0222D;"; // tint;
        escapesec[0x02928] = "&#x02928;"; // toea;
        escapesec[0x022A4] = "&#x022A4;"; // top;
        escapesec[0x02336] = "&#x02336;"; // topbot;
        escapesec[0x02AF1] = "&#x02AF1;"; // topcir;
        escapesec[0x1D565] = "&#x1D565;"; // topf;
        escapesec[0x02ADA] = "&#x02ADA;"; // topfork;
        escapesec[0x02929] = "&#x02929;"; // tosa;
        escapesec[0x02034] = "&#x02034;"; // tprime;
        escapesec[0x02122] = "&#x02122;"; // trade;
        escapesec[0x025B5] = "&#x025B5;"; // triangle;
        escapesec[0x025BF] = "&#x025BF;"; // triangledown;
        escapesec[0x025C3] = "&#x025C3;"; // triangleleft;
        escapesec[0x022B4] = "&#x022B4;"; // trianglelefteq;
        escapesec[0x0225C] = "&#x0225C;"; // triangleq;
        escapesec[0x025B9] = "&#x025B9;"; // triangleright;
        escapesec[0x022B5] = "&#x022B5;"; // trianglerighteq;
        escapesec[0x025EC] = "&#x025EC;"; // tridot;
        escapesec[0x0225C] = "&#x0225C;"; // trie;
        escapesec[0x02A3A] = "&#x02A3A;"; // triminus;
        escapesec[0x02A39] = "&#x02A39;"; // triplus;
        escapesec[0x029CD] = "&#x029CD;"; // trisb;
        escapesec[0x02A3B] = "&#x02A3B;"; // tritime;
        escapesec[0x023E2] = "&#x023E2;"; // trpezium;
        escapesec[0x1D4C9] = "&#x1D4C9;"; // tscr;
        escapesec[0x00446] = "&#x00446;"; // tscy;
        escapesec[0x0045B] = "&#x0045B;"; // tshcy;
        escapesec[0x00167] = "&#x00167;"; // tstrok;
        escapesec[0x0226C] = "&#x0226C;"; // twixt;
        escapesec[0x0219E] = "&#x0219E;"; // twoheadleftarrow;
        escapesec[0x021A0] = "&#x021A0;"; // twoheadrightarrow;
        escapesec[0x021D1] = "&#x021D1;"; // uArr;
        escapesec[0x02963] = "&#x02963;"; // uHar;
        escapesec[0x000FA] = "&#x000FA;"; // uacute;
        escapesec[0x02191] = "&#x02191;"; // uarr;
        escapesec[0x0045E] = "&#x0045E;"; // ubrcy;
        escapesec[0x0016D] = "&#x0016D;"; // ubreve;
        escapesec[0x000FB] = "&#x000FB;"; // ucirc;
        escapesec[0x00443] = "&#x00443;"; // ucy;
        escapesec[0x021C5] = "&#x021C5;"; // udarr;
        escapesec[0x00171] = "&#x00171;"; // udblac;
        escapesec[0x0296E] = "&#x0296E;"; // udhar;
        escapesec[0x0297E] = "&#x0297E;"; // ufisht;
        escapesec[0x1D532] = "&#x1D532;"; // ufr;
        escapesec[0x000F9] = "&#x000F9;"; // ugrave;
        escapesec[0x021BF] = "&#x021BF;"; // uharl;
        escapesec[0x021BE] = "&#x021BE;"; // uharr;
        escapesec[0x02580] = "&#x02580;"; // uhblk;
        escapesec[0x0231C] = "&#x0231C;"; // ulcorn;
        escapesec[0x0230F] = "&#x0230F;"; // ulcrop;
        escapesec[0x025F8] = "&#x025F8;"; // ultri;
        escapesec[0x0016B] = "&#x0016B;"; // umacr;
        escapesec[0x000A8] = "&#x000A8;"; // uml;
        escapesec[0x00173] = "&#x00173;"; // uogon;
        escapesec[0x1D566] = "&#x1D566;"; // uopf;
        escapesec[0x02191] = "&#x02191;"; // uparrow;
        escapesec[0x02195] = "&#x02195;"; // updownarrow;
        escapesec[0x021BF] = "&#x021BF;"; // upharpoonleft;
        escapesec[0x021BE] = "&#x021BE;"; // upharpoonright;
        escapesec[0x0228E] = "&#x0228E;"; // uplus;
        escapesec[0x003C5] = "&#x003C5;"; // upsi;
        escapesec[0x003D2] = "&#x003D2;"; // upsih;
        escapesec[0x003C5] = "&#x003C5;"; // upsilon;
        escapesec[0x021C8] = "&#x021C8;"; // upuparrows;
        escapesec[0x0231D] = "&#x0231D;"; // urcorn;
        escapesec[0x0230E] = "&#x0230E;"; // urcrop;
        escapesec[0x0016F] = "&#x0016F;"; // uring;
        escapesec[0x025F9] = "&#x025F9;"; // urtri;
        escapesec[0x1D4CA] = "&#x1D4CA;"; // uscr;
        escapesec[0x022F0] = "&#x022F0;"; // utdot;
        escapesec[0x00169] = "&#x00169;"; // utilde;
        escapesec[0x025B5] = "&#x025B5;"; // utri;
        escapesec[0x025B4] = "&#x025B4;"; // utrif;
        escapesec[0x021C8] = "&#x021C8;"; // uuarr;
        escapesec[0x000FC] = "&#x000FC;"; // uuml;
        escapesec[0x029A7] = "&#x029A7;"; // uwangle;
        escapesec[0x021D5] = "&#x021D5;"; // vArr;
        escapesec[0x02AE8] = "&#x02AE8;"; // vBar;
        escapesec[0x02AE9] = "&#x02AE9;"; // vBarv;
        escapesec[0x022A8] = "&#x022A8;"; // vDash;
        escapesec[0x0299C] = "&#x0299C;"; // vangrt;
        escapesec[0x003F5] = "&#x003F5;"; // varepsilon;
        escapesec[0x003F0] = "&#x003F0;"; // varkappa;
        escapesec[0x02205] = "&#x02205;"; // varnothing;
        escapesec[0x003D5] = "&#x003D5;"; // varphi;
        escapesec[0x003D6] = "&#x003D6;"; // varpi;
        escapesec[0x0221D] = "&#x0221D;"; // varpropto;
        escapesec[0x02195] = "&#x02195;"; // varr;
        escapesec[0x003F1] = "&#x003F1;"; // varrho;
        escapesec[0x003C2] = "&#x003C2;"; // varsigma;
        escapesec[0x003D1] = "&#x003D1;"; // vartheta;
        escapesec[0x022B2] = "&#x022B2;"; // vartriangleleft;
        escapesec[0x022B3] = "&#x022B3;"; // vartriangleright;
        escapesec[0x00432] = "&#x00432;"; // vcy;
        escapesec[0x022A2] = "&#x022A2;"; // vdash;
        escapesec[0x02228] = "&#x02228;"; // vee;
        escapesec[0x022BB] = "&#x022BB;"; // veebar;
        escapesec[0x0225A] = "&#x0225A;"; // veeeq;
        escapesec[0x022EE] = "&#x022EE;"; // vellip;
        escapesec[0x0007C] = "&#x0007C;"; // verbar;
        escapesec[0x1D533] = "&#x1D533;"; // vfr;
        escapesec[0x022B2] = "&#x022B2;"; // vltri;
        escapesec[0x1D567] = "&#x1D567;"; // vopf;
        escapesec[0x0221D] = "&#x0221D;"; // vprop;
        escapesec[0x022B3] = "&#x022B3;"; // vrtri;
        escapesec[0x1D4CB] = "&#x1D4CB;"; // vscr;
        escapesec[0x0299A] = "&#x0299A;"; // vzigzag;
        escapesec[0x00175] = "&#x00175;"; // wcirc;
        escapesec[0x02A5F] = "&#x02A5F;"; // wedbar;
        escapesec[0x02227] = "&#x02227;"; // wedge;
        escapesec[0x02259] = "&#x02259;"; // wedgeq;
        escapesec[0x02118] = "&#x02118;"; // weierp;
        escapesec[0x1D534] = "&#x1D534;"; // wfr;
        escapesec[0x1D568] = "&#x1D568;"; // wopf;
        escapesec[0x02118] = "&#x02118;"; // wp;
        escapesec[0x02240] = "&#x02240;"; // wr;
        escapesec[0x1D4CC] = "&#x1D4CC;"; // wscr;
        escapesec[0x022C2] = "&#x022C2;"; // xcap;
        escapesec[0x025EF] = "&#x025EF;"; // xcirc;
        escapesec[0x022C3] = "&#x022C3;"; // xcup;
        escapesec[0x025BD] = "&#x025BD;"; // xdtri;
        escapesec[0x1D535] = "&#x1D535;"; // xfr;
        escapesec[0x027FA] = "&#x027FA;"; // xhArr;
        escapesec[0x027F7] = "&#x027F7;"; // xharr;
        escapesec[0x003BE] = "&#x003BE;"; // xi;
        escapesec[0x027F8] = "&#x027F8;"; // xlArr;
        escapesec[0x027F5] = "&#x027F5;"; // xlarr;
        escapesec[0x027FC] = "&#x027FC;"; // xmap;
        escapesec[0x022FB] = "&#x022FB;"; // xnis;
        escapesec[0x02A00] = "&#x02A00;"; // xodot;
        escapesec[0x1D569] = "&#x1D569;"; // xopf;
        escapesec[0x02A01] = "&#x02A01;"; // xoplus;
        escapesec[0x02A02] = "&#x02A02;"; // xotime;
        escapesec[0x027F9] = "&#x027F9;"; // xrArr;
        escapesec[0x027F6] = "&#x027F6;"; // xrarr;
        escapesec[0x1D4CD] = "&#x1D4CD;"; // xscr;
        escapesec[0x02A06] = "&#x02A06;"; // xsqcup;
        escapesec[0x02A04] = "&#x02A04;"; // xuplus;
        escapesec[0x025B3] = "&#x025B3;"; // xutri;
        escapesec[0x022C1] = "&#x022C1;"; // xvee;
        escapesec[0x022C0] = "&#x022C0;"; // xwedge;
        escapesec[0x000FD] = "&#x000FD;"; // yacute;
        escapesec[0x0044F] = "&#x0044F;"; // yacy;
        escapesec[0x00177] = "&#x00177;"; // ycirc;
        escapesec[0x0044B] = "&#x0044B;"; // ycy;
        escapesec[0x000A5] = "&#x000A5;"; // yen;
        escapesec[0x1D536] = "&#x1D536;"; // yfr;
        escapesec[0x00457] = "&#x00457;"; // yicy;
        escapesec[0x1D56A] = "&#x1D56A;"; // yopf;
        escapesec[0x1D4CE] = "&#x1D4CE;"; // yscr;
        escapesec[0x0044E] = "&#x0044E;"; // yucy;
        escapesec[0x000FF] = "&#x000FF;"; // yuml;
        escapesec[0x0017A] = "&#x0017A;"; // zacute;
        escapesec[0x0017E] = "&#x0017E;"; // zcaron;
        escapesec[0x00437] = "&#x00437;"; // zcy;
        escapesec[0x0017C] = "&#x0017C;"; // zdot;
        escapesec[0x02128] = "&#x02128;"; // zeetrf;
        escapesec[0x003B6] = "&#x003B6;"; // zeta;
        escapesec[0x1D537] = "&#x1D537;"; // zfr;
        escapesec[0x00436] = "&#x00436;"; // zhcy;
        escapesec[0x021DD] = "&#x021DD;"; // zigrarr;
        escapesec[0x1D56B] = "&#x1D56B;"; // zopf;
        escapesec[0x1D4CF] = "&#x1D4CF;"; // zscr;
        escapesec[0x0200D] = "&#x0200D;"; // zwj;
        escapesec[0x0200C] = "&#x0200C;"; // zwnj;

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

void NodeCounter::operator += (const NodeCounter& o)
{
    storage += o.storage;
    versionStorage += o.versionStorage;
    files += o.files;
    folders += o.folders;
    versions += o.versions;
}

void NodeCounter::operator -= (const NodeCounter& o)
{
    storage -= o.storage;
    versionStorage -= o.versionStorage;
    files -= o.files;
    folders -= o.folders;
    versions -= o.versions;
}


CacheableStatus::CacheableStatus(mega::CacheableStatus::Type type, int64_t value)
    : mType(type)
    , mValue(value)
{ }


// This should be a const-method but can't be due to the broken Cacheable interface.
// Do not mutate members in this function! Hence, we forward to a private const-method.
bool CacheableStatus::serialize(std::string* data)
{
    return const_cast<const CacheableStatus*>(this)->serialize(*data);
}

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

bool CacheableStatus::serialize(std::string& data) const
{
    CacheableWriter writer{data};
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

    std::unique_ptr<byte[]> buffer(new byte[SZ_1024K + SymmCipher::BLOCKSIZE]);
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
            f = std::move(mQueue.front().f);
            if (!f) return;   // nullptr is not popped, and causes all the threads to exit
            mQueue.pop_front();
        }
        f(cipher);
        mWaiter.notify();
    }
}

bool islchex(const int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

std::string getSafeUrl(const std::string &posturl)
{
    string safeurl = posturl;
    size_t sid = safeurl.find("sid=");
    if (sid != string::npos)
    {
        sid += 4;
        size_t end = safeurl.find("&", sid);
        if (end == string::npos)
        {
            end = safeurl.size();
        }
        memset((char *)safeurl.data() + sid, 'X', end - sid);
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
        memset((char *)safeurl.data() + authKey, 'X', end - authKey);
    }
    return safeurl;
}

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

} // namespace

