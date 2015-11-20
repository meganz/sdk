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

namespace mega {
Cachable::Cachable()
{
    dbid = 0;
    notified = 0;
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
m_off_t ChunkedHash::chunkceil(m_off_t p)
{
    m_off_t cp, np;

    cp = 0;

    for (unsigned i = 1; i <= 8; i++)
    {
        np = cp + i * SEGSIZE;

        if ((p >= cp) && (p < np))
        {
            return np;
        }

        cp = np;
    }

    return ((p - cp) & - (8 * SEGSIZE)) + cp + 8 * SEGSIZE;
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

bool HashSignature::check(AsymmCipher* pubk, const byte* sig, unsigned len)
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

TLVcontainer * TLVstore::TLVrecordsToContainer(SymmCipher *key, unsigned mode)
{
    // serialize the TLV records
    TLVcontainer *tlv = TLVrecordsToContainer();

    // encrypt the result
    int ivlen, taglen;
    switch (mode)
    {
    case AES_CCM_12_16:
    case AES_CCM_12_16_buggy:
        ivlen = 12;
        taglen = 16;
        break;

    case AES_CCM_10_16:
        ivlen = 10;
        taglen = 16;
        break;

    case AES_CCM_10_08:
    case AES_CCM_10_08_buggy:
        ivlen = 12;
        taglen = 16;
        break;

    case AES_GCM_12_16:
        ivlen = 12;
        taglen = 16;
        break;

    case AES_GCM_10_08:
        ivlen = 10;
        taglen = 16;
        break;

    default:    // unknown block encryption mode
        return NULL;
    }

    byte *buf;
    unsigned buflen;

    // encrypt the bytes using the specified mode



    delete tlv;

    return new TLVcontainer(buf, buflen);
}

TLVcontainer * TLVstore::TLVrecordsToContainer()
{

}

TLVstore * TLVstore::containerToTLVrecords(const byte * data, unsigned datalen)
{
    if (!data || !datalen)
    {
        return NULL;
    }

    TLVstore *tlv = new TLVstore();

    char *ptr = (char*) data;
    char *end = ptr + datalen;

    unsigned int typelen;
    string type;
    unsigned int valuelen;
    byte * value;

    while (ptr < end)
    {
        // get the length of the Type string
        typelen = 0;
        while (ptr[typelen] != '\0')
        {
            typelen++;
        }

        if (ptr + typelen + 2 > end)
        {
            delete tlv;
            return NULL;
        }

        // get the Type string
        type.assign(ptr, 0, typelen);
        ptr += typelen + 1;             // +1: NULL character

        // get the Length of the value
        valuelen = ptr[0] << 8 | ptr[1];
        ptr += 2;

        if (ptr + valuelen > end)
        {
            delete tlv;
            return NULL;
        }

        // get the Value
        value = new byte[valuelen];
        memcpy(value, ptr, valuelen);
        ptr += valuelen;

        // add it to the map
        tlv->set(type, value, valuelen);
    }

    return tlv;
}


TLVstore * TLVstore::containerToTLVrecords(const byte *data, unsigned datalen, SymmCipher *key)
{
    if (!data || !datalen)
    {
        return NULL;
    }

    unsigned offset = 0;

    int mode = data[offset];
    offset++;

    int ivlen, taglen;
    switch (mode)
    {
    case AES_CCM_12_16:
    case AES_CCM_12_16_buggy:
        ivlen = 12;
        taglen = 16;
        break;

    case AES_CCM_10_16:
        ivlen = 10;
        taglen = 16;
        break;

    case AES_CCM_10_08:
    case AES_CCM_10_08_buggy:
        ivlen = 12;
        taglen = 16;
        break;

    case AES_GCM_12_16:
        ivlen = 12;
        taglen = 16;
        break;

    case AES_GCM_10_08:
        ivlen = 10;
        taglen = 16;
        break;

    default:    // unknown block encryption mode
        return NULL;
    }

    byte *iv = new byte[ivlen];
    memcpy(iv, &(data[offset]), ivlen);
    offset += ivlen;

    int buflen = datalen - offset;

    byte *buf = new byte[buflen];
    memcpy(buf, &(data[offset]), buflen);

    // BUG: the AES mode may be different, based on the mode. Now, it's always CCM_
    key->ccm_decrypt(buf, buflen, iv, ivlen);

    TLVstore *tlv = TLVstore::containerToTLVrecords(buf, buflen);

    delete [] buf;
    delete [] iv;

    return tlv;
}

TLVstore::~TLVstore()
{
    if (tlv.size())
    {
        TLV_map::iterator it;
        for (it = tlv.begin(); it != tlv.end(); it++)
        {
            delete [] it->second.first;
        }
    }
}

bool TLVstore::set(string type, byte *value, unsigned valuelen)
{
    tlv[type] = TLVvalue(value, valuelen);
}

bool TLVstore::set(string type, TLVvalue value)
{
    tlv[type] = value;
}

} // namespace
