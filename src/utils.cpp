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

string toHandle(handle h)
{
    char base64Handle[14];
    Base64::btoa((byte*)&(h), sizeof h, base64Handle);
    return string(base64Handle);
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
    for (const_iterator it = begin(); it != end(); it++)
    {
        d.append((char*)&it->first, sizeof(it->first));
        d.append((char*)&it->second, sizeof(it->second));
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

        memcpy(&((*this)[pos]), ptr, sizeof(ChunkMAC));
        ptr += sizeof(ChunkMAC);
    }
    return true;
}

void chunkmac_map::calcprogress(m_off_t size, m_off_t& chunkpos, m_off_t& progresscompleted, m_off_t* lastblockprogress)
{
    chunkpos = 0;
    progresscompleted = 0;

    for (chunkmac_map::iterator it = begin(); it != end(); ++it)
    {
        m_off_t chunkceil = ChunkedHash::chunkceil(it->first, size);

        if (chunkpos == it->first && it->second.finished)
        {
            chunkpos = chunkceil;
            progresscompleted = chunkceil;
        }
        else if (it->second.finished)
        {
            m_off_t chunksize = chunkceil - ChunkedHash::chunkfloor(it->first);
            progresscompleted += chunksize;
        }
        else
        {
            progresscompleted += it->second.offset;
            if (lastblockprogress)
            {
                *lastblockprogress += it->second.offset;
            }
        }
    }
}

m_off_t chunkmac_map::nextUnprocessedPosFrom(m_off_t pos)
{
    for (const_iterator it = find(ChunkedHash::chunkfloor(pos));
        it != end();
        it = find(ChunkedHash::chunkfloor(pos)))
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
    for (iterator it = find(npos);
        npos < fileSize && (npos - pos) <= maxReqSize && (it == end() || (!it->second.finished && !it->second.offset));
        it = find(npos))
    {
        npos = ChunkedHash::chunkceil(npos, fileSize);
    }
    return npos;
}

void chunkmac_map::finishedUploadChunks(chunkmac_map& macs)
{
    for (auto& m : macs)
    {
        m.second.finished = true;
        (*this)[m.first] = m.second;
        LOG_verbose << "Upload chunk completed: " << m.first;
    }
}

// coalesce block macs into file mac
int64_t chunkmac_map::macsmac(SymmCipher *cipher)
{
    byte mac[SymmCipher::BLOCKSIZE] = { 0 };

    for (chunkmac_map::iterator it = begin(); it != end(); it++)
    {
        assert(it->first == ChunkedHash::chunkfloor(it->first));
        // LOG_debug << "macsmac input: " << it->first << ": " << Base64Str<sizeof it->second.mac>(it->second.mac);
        SymmCipher::xorblock(it->second.mac, mac);
        cipher->ecb_encrypt(mac);
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

    d->append("\0\0\0\0\0\0", 6); // additional bytes for backwards compatibility

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

    for (int i = 6; i--;)
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

std::string TLVstore::get(string type) const
{
    return tlv.at(type);
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

bool TLVstore::find(string type) const
{
    return (tlv.find(type) != tlv.end());
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
                    res[rescount++] = unicodecp & 0xFF;
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

SyncConfig::SyncConfig(std::string localPath,
                       const handle remoteNode,
                       const fsfp_t localFingerprint,
                       std::vector<std::string> regExps,
                       const Type syncType,
                       const bool syncDeletions,
                       const bool forceOverwrite)
    : mLocalPath{std::move(localPath)}
    , mRemoteNode{remoteNode}
    , mLocalFingerprint{localFingerprint}
    , mRegExps{std::move(regExps)}
    , mSyncType{syncType}
    , mSyncDeletions{syncDeletions}
    , mForceOverwrite{forceOverwrite}
{}

bool SyncConfig::isResumable() const
{
    return mResumable;
}

void SyncConfig::setResumable(bool resumable)
{
    mResumable = resumable;
}

const std::string& SyncConfig::getLocalPath() const
{
    return mLocalPath;
}

handle SyncConfig::getRemoteNode() const
{
    return mRemoteNode;
}

handle SyncConfig::getLocalFingerprint() const
{
    return mLocalFingerprint;
}

void SyncConfig::setLocalFingerprint(fsfp_t fingerprint)
{
    mLocalFingerprint = fingerprint;
}

const std::vector<std::string>& SyncConfig::getRegExps() const
{
    return mRegExps;
}

SyncConfig::Type SyncConfig::getType() const
{
    return mSyncType;
}

bool SyncConfig::isUpSync() const
{
    return mSyncType & TYPE_UP;
}

bool SyncConfig::isDownSync() const
{
    return mSyncType & TYPE_DOWN;
}

bool SyncConfig::syncDeletions() const
{
    switch (mSyncType)
    {
        case TYPE_UP: return mSyncDeletions;
        case TYPE_DOWN: return mSyncDeletions;
        case TYPE_TWOWAY: return true;
    }
    assert(false);
    return true;
}

bool SyncConfig::forceOverwrite() const
{
    switch (mSyncType)
    {
        case TYPE_UP: return mForceOverwrite;
        case TYPE_DOWN: return mForceOverwrite;
        case TYPE_TWOWAY: return false;
    }
    assert(false);
    return false;
}

// This should be a const-method but can't be due to the broken Cacheable interface.
// Do not mutate members in this function! Hence, we forward to a private const-method.
bool SyncConfig::serialize(std::string* data)
{
    return const_cast<const SyncConfig*>(this)->serialize(*data);
}

std::unique_ptr<SyncConfig> SyncConfig::unserialize(const std::string& data)
{
    bool resumable;
    std::string localPath;
    handle remoteNode;
    fsfp_t fingerprint;
    uint32_t regExpCount;
    std::vector<std::string> regExps;
    uint32_t syncType;
    bool syncDeletions;
    bool forceOverwrite;

    CacheableReader reader{data};
    if (!reader.unserializebool(resumable))
    {
        return {};
    }
    if (!reader.unserializestring(localPath))
    {
        return {};
    }
    if (!reader.unserializehandle(remoteNode))
    {
        return {};
    }
    if (!reader.unserializefsfp(fingerprint))
    {
        return {};
    }
    if (!reader.unserializeu32(regExpCount))
    {
        return {};
    }
    for (uint32_t i = 0; i < regExpCount; ++i)
    {
        std::string regExp;
        if (!reader.unserializestring(regExp))
        {
            return {};
        }
        regExps.push_back(std::move(regExp));
    }
    if (!reader.unserializeu32(syncType))
    {
        return {};
    }
    if (!reader.unserializebool(syncDeletions))
    {
        return {};
    }
    if (!reader.unserializebool(forceOverwrite))
    {
        return {};
    }

    auto syncConfig = std::unique_ptr<SyncConfig>{new SyncConfig{std::move(localPath),
                    remoteNode, fingerprint, std::move(regExps),
                    static_cast<Type>(syncType), syncDeletions, forceOverwrite}};
    syncConfig->setResumable(resumable);
    return syncConfig;
}

bool SyncConfig::serialize(std::string& data) const
{
    CacheableWriter writer{data};
    writer.serializebool(mResumable);
    writer.serializestring(mLocalPath);
    writer.serializehandle(mRemoteNode);
    writer.serializefsfp(mLocalFingerprint);
    writer.serializeu32(static_cast<uint32_t>(mRegExps.size()));
    for (const auto& regExp : mRegExps)
    {
        writer.serializestring(regExp);
    }
    writer.serializeu32(static_cast<uint32_t>(mSyncType));
    writer.serializebool(mSyncDeletions);
    writer.serializebool(mForceOverwrite);
    writer.serializeexpansionflags();
    return true;
}

bool operator==(const SyncConfig& lhs, const SyncConfig& rhs)
{
    return lhs.tie() == rhs.tie();
}

std::pair<bool, int64_t> generateMetaMac(SymmCipher &cipher, FileAccess &ifAccess, const int64_t iv)
{
    FileInputStream isAccess(&ifAccess);

    return generateMetaMac(cipher, isAccess, iv);
}

std::pair<bool, int64_t> generateMetaMac(SymmCipher &cipher, InputStreamAccess &isAccess, const int64_t iv)
{
    static const m_off_t SZ_1024K = 1l << 20;
    static const m_off_t SZ_128K  = 128l << 10;

    std::unique_ptr<byte[]> buffer(new byte[SZ_1024K + SymmCipher::BLOCKSIZE]);
    chunkmac_map chunkMacs;
    m_off_t chunkLength = 0;
    m_off_t current = 0;
    m_off_t remaining = isAccess.size();

    while (remaining > 0)
    {
        chunkLength =
          std::min(chunkLength + SZ_128K,
                   std::min(remaining, SZ_1024K));

        if (!isAccess.read(&buffer[0], (unsigned int)chunkLength))
            return std::make_pair(false, 0l);

        memset(&buffer[chunkLength], 0, SymmCipher::BLOCKSIZE);

        cipher.ctr_crypt(&buffer[0],
                         (unsigned int)chunkLength,
                         current,
                         iv,
                         chunkMacs[current].mac,
                         1);

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

} // namespace

