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

namespace mega {

string toNodeHandle(handle nodeHandle)
{
    char base64Handle[14];
    Base64::btoa((byte*)&(nodeHandle), MegaClient::NODEHANDLE, base64Handle);
    return string(base64Handle);
}

Cachable::Cachable()
{
    dbid = 0;
    notified = 0;
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

    ll = userpriv ? userpriv->size() : 0;
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
    ll = title.size();
    d->append((char*)&ll, sizeof ll);
    d->append(title.data(), ll);

    d->append((char*)&ou, sizeof ou);
    d->append((char*)&ts, sizeof(ts));

    char hasAttachments = attachedNodes.size() != 0;
    d->append((char*)&hasAttachments, 1);
    d->append("\0\0\0\0\0\0\0\0", 9); // additional bytes for backwards compatibility

    if (hasAttachments)
    {
        ll = attachedNodes.size();  // number of nodes with granted access
        d->append((char*)&ll, sizeof ll);

        for (attachments_map::iterator it = attachedNodes.begin(); it != attachedNodes.end(); it++)
        {
            d->append((char*)&it->first, sizeof it->first); // nodehandle

            ll = it->second.size(); // number of users with granted access to the node
            d->append((char*)&ll, sizeof ll);
            for (set<handle>::iterator ituh = it->second.begin(); ituh != it->second.end(); ituh++)
            {
                d->append((char*)&(*ituh), sizeof *ituh);   // userhandle
            }
        }
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
    char hasAttachments;
    attachments_map attachedNodes;

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

    if (ptr + sizeof(handle) + sizeof(m_time_t) + 10 > end)
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

    for (int i = 9; i--;)
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
    chat->attachedNodes = attachedNodes;

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
void PaddedCBC::encrypt(string* data, SymmCipher* key, string* iv)
{
    if (iv)
    {
        // Make a new 8-byte IV, if the one passed is zero length.
        if (iv->size() == 0)
        {
            byte* buf = new byte[8];
            PrnGen::genblock(buf, 8);
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

PayCrypter::PayCrypter()
{
    PrnGen::genblock(keys, ENC_KEY_BYTES + MAC_KEY_BYTES);
    encKey = keys;
    hmacKey = keys+ENC_KEY_BYTES;

    PrnGen::genblock(iv, IV_BYTES);
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
    int keylen = keyString.size();

    //Resize to add padding
    keyString.resize(asym.key[AsymmCipher::PUB_PQ].ByteCount() - 2);

    //Add padding
    if(randompadding)
    {
        PrnGen::genblock((byte *)keyString.data() + keylen, keyString.size() - keylen);
    }

    //RSA encryption
    result->resize(pubkdatalen);
    result->resize(asym.rawencrypt((byte *)keyString.data(), keyString.size(), (byte *)result->data(), result->size()));

    //Complete the result (2-byte header + RSA result)
    int reslen = result->size();
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

string * TLVstore::tlvRecordsToContainer(SymmCipher *key, encryptionsetting_t encSetting)
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
    PrnGen::genblock(iv, ivlen);

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
    result->at(0) = encSetting;
    result->append((char*) iv, ivlen);
    result->append((char*) cipherText.data(), cipherText.length()); // includes auth. tag

    delete [] iv;
    delete container;

    return result;
}

string * TLVstore::tlvRecordsToContainer()
{
    TLV_map::iterator it;
    unsigned buflen = 0;

    for (it = tlv.begin(); it != tlv.end(); it++)
    {
        // add string length + null char + 2 bytes for length + value length
        buflen += it->first.length() + 1 + 2 + it->second.length();
    }

    string * result = new string;
    unsigned offset = 0;
    unsigned length;

    for (it = tlv.begin(); it != tlv.end(); it++)
    {
        // copy Type
        result->append(it->first);
        offset += it->first.length() + 1;   // keep the NULL-char for Type string

        // set Length of value
        length = it->second.length();
        result->resize(offset + 2);
        result->at(offset) = length >> 8;
        result->at(offset + 1) = length & 0xFF;
        offset += 2;

        // copy the Value
        result->append((char*)it->second.data(), it->second.length());
        offset += it->second.length();
    }

    return result;
}

string TLVstore::get(string type)
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

bool TLVstore::find(string type)
{
    return (tlv.find(type) != tlv.end());
}

void TLVstore::set(string type, string value)
{
    tlv[type] = value;
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

    unsigned offset = 0;

    string type;
    unsigned typelen;
    string value;
    unsigned valuelen;
    size_t pos;

    unsigned datalen = data->length();

    while (offset < datalen)
    {
        // get the length of the Type string
        pos = data->find('\0', offset);
        typelen = pos - offset;

        // if no valid TLV record in the container, but remaining bytes...
        if ( (pos == data->npos) || (offset + typelen + 3 > datalen) )
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

    if (encMode == AES_MODE_UNKNOWN || !ivlen || !taglen ||  data->size() <= offset+ivlen+taglen)
    {
        return NULL;
    }

    byte *iv = new byte[ivlen];
    memcpy(iv, &(data->data()[offset]), ivlen);
    offset += ivlen;

    unsigned cipherTextLen = data->length() - offset;
    string cipherText = data->substr(offset, cipherTextLen);

    unsigned clearTextLen = cipherTextLen - taglen;
    string clearText;

    if (encMode == AES_MODE_CCM)   // CCM or GCM_BROKEN (same than CCM)
    {
        key->ccm_decrypt(&cipherText, iv, ivlen, taglen, &clearText);
    }
    else if (encMode == AES_MODE_GCM)  // GCM
    {
        key->gcm_decrypt(&cipherText, iv, ivlen, taglen, &clearText);
    }

    delete [] iv;

    if (clearText.empty())  // the decryption has failed (probably due to authentication)
    {
        return NULL;
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

} // namespace
