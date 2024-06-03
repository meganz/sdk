/**
 * @file cryptopp.cpp
 * @brief Crypto layer using Crypto++
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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

#include "mega.h"

namespace mega {
#ifndef htobe64
#define htobe64(x) (((uint64_t)htonl((uint32_t)((x) >> 32))) | (((uint64_t)htonl((uint32_t)x)) << 32))
#endif

using namespace CryptoPP;

// cryptographically strong random byte sequence
void PrnGen::genblock(byte* buf, size_t len)
{
    GenerateBlock(buf, len);
}

// random number from 0 ... max-1
uint32_t PrnGen::genuint32(uint64_t max)
{
    uint32_t t;

    genblock((byte*)&t, sizeof t);

    return (uint32_t)(((uint64_t)t) / ((((uint64_t)(~(uint32_t)0)) + 1) / max));
}

std::string PrnGen::genstring(const size_t len)
{
    std::string result(len, '\0');

    // Necessary as data() returns const until C++17.
    void *buffer = const_cast<char*>(result.data());

    genblock(reinterpret_cast<byte*>(buffer), len);

    return result;
}

SymmCipher::SymmCipher(const byte* key)
{
    setkey(key);
}

byte SymmCipher::zeroiv[BLOCKSIZE] = {};

void SymmCipher::setkey(const byte* newkey, int type)
{
    memcpy(key, newkey, KEYLENGTH);

    if (!type)
    {
        xorblock(newkey + KEYLENGTH, key);
    }

    aesecb_e.SetKey(key, KEYLENGTH);
    aesecb_d.SetKey(key, KEYLENGTH);

    aescbc_e.SetKeyWithIV(key, KEYLENGTH, zeroiv);
    aescbc_d.SetKeyWithIV(key, KEYLENGTH, zeroiv);

    aesccm8_e.SetKeyWithIV(key, KEYLENGTH, zeroiv);
    aesccm8_d.SetKeyWithIV(key, KEYLENGTH, zeroiv);

    aesccm16_e.SetKeyWithIV(key, KEYLENGTH, zeroiv);
    aesccm16_d.SetKeyWithIV(key, KEYLENGTH, zeroiv);

    aesgcm_e.SetKeyWithIV(key, KEYLENGTH, zeroiv);
    aesgcm_d.SetKeyWithIV(key, KEYLENGTH, zeroiv);
}

bool SymmCipher::setkey(const string* key)
{
    if (key->size() == FILENODEKEYLENGTH || key->size() == FOLDERNODEKEYLENGTH)
    {
        setkey((const byte*)key->data(), (key->size() == FOLDERNODEKEYLENGTH) ? FOLDERNODE : FILENODE);

        return true;
    }

    return false;
}

bool SymmCipher::cbc_encrypt_with_key(const std::string& plain, std::string& cipher, const byte* key, const size_t keylen, const byte* iv)
{
    try
    {
        aescbc_e.SetKeyWithIV(key, keylen, iv ? iv: zeroiv);
        StringSource ss(plain, true, new StreamTransformationFilter(aescbc_e, new StringSink(cipher)));
        return true;
    }
    catch (const CryptoPP::Exception& e)
    {
        LOG_err << "Failed AES-CBC encryption" << e.what();
        return false;
    }
}

bool SymmCipher::cbc_decrypt_with_key(const std::string& cipher, std::string& plain, const byte* key, const size_t keylen, const byte* iv)
{
    try
    {
        aescbc_d.SetKeyWithIV(key, keylen, iv ? iv: zeroiv);
        StringSource ss(cipher, true, new StreamTransformationFilter(aescbc_d, new StringSink(plain)));
        return true;
    }
    catch(const CryptoPP::Exception& e)
    {
        LOG_err << "Failed AES-CBC decryption" << e.what();
        return false;
    }
}

bool SymmCipher::cbc_encrypt(byte* data, size_t len, const byte* iv)
{
    try
    {
        aescbc_e.Resynchronize(iv ? iv : zeroiv);
        aescbc_e.ProcessData(data, data, len);
        return true;
    }
    catch (const CryptoPP::Exception& e)
    {
        LOG_err << "Failed AES-CBC encryption " << e.what();
        return false;
    }
}

bool SymmCipher::cbc_decrypt(byte* data, size_t len, const byte* iv)
{
    try
    {
        aescbc_d.Resynchronize(iv ? iv : zeroiv);
        aescbc_d.ProcessData(data, data, len);
        return true;
    }
    catch (const CryptoPP::Exception& e)
    {
        LOG_err << "Failed AES-CBC decryption " << e.what();
        return false;
    }
}

bool SymmCipher::cbc_encrypt_pkcs_padding(const string *data, const byte *iv, string *result)
{
    if (!data || !result)
    {
        assert(data && result);
        return false;
    }

    using Transformation = StreamTransformationFilter;

    try
    {
        // Update IV.
        aescbc_e.Resynchronize(iv ? iv : zeroiv);

        // Create sink.
        unique_ptr<StringSink> sink =
            std::make_unique<StringSink>(*result);

        // Create transform.
        unique_ptr<Transformation> xfrm =
            std::make_unique<Transformation>(aescbc_e,
                sink.get(),
                Transformation::PKCS_PADDING);

        // Transform now owns sink.
        static_cast<void>(sink.release());

        // Encrypt.
        StringSource ss(*data, true, xfrm.release());

        return true;
    }
    catch (const CryptoPP::Exception& e)
    {
        LOG_err << "Failed AES-CBC pkcs encryption " << e.what();
        return false;
    }
}

bool SymmCipher::cbc_decrypt_pkcs_padding(const std::string* data, const byte* iv, string* result)
{
    if (!data || !result)
    {
        assert(data && result);
        return false;
    }

    try
    {
        using Transformation = StreamTransformationFilter;

        // Update IV.
        aescbc_d.Resynchronize(iv ? iv : zeroiv);

        // Create sink.
        unique_ptr<StringSink> sink =
          std::make_unique<StringSink>(*result);
        
        // Create transform.
        unique_ptr<Transformation> xfrm =
          std::make_unique<Transformation>(aescbc_d,
                                            sink.get(),
                                            Transformation::PKCS_PADDING);

        // Transform now owns sink.
        static_cast<void>(sink.release());

        // Attempt decrypt.
        StringSource ss(*data, true, xfrm.release());

        // Decrypt had correct padding.
        return true;
    }
    catch (const CryptoPP::Exception& e)
    {
        LOG_err << "Failed AES-CBC pkcs decryption " << e.what();
        return false;
    }
}

bool SymmCipher::cbc_decrypt_pkcs_padding(const byte* data,
                                          const size_t dataLength,
                                          const byte* iv,
                                          std::string* result)
{
    if (!result)
    {
        assert(result);
        return false;
    }

    try
    {
        using Transformation = StreamTransformationFilter;

        // Update IV.
        aescbc_d.Resynchronize(iv ? iv : zeroiv);

        // Create sink.
        unique_ptr<StringSink> sink =
          std::make_unique<StringSink>(*result);
        
        // Create transform.
        unique_ptr<Transformation> xfrm =
          std::make_unique<Transformation>(aescbc_d,
                                            sink.get(),
                                            Transformation::PKCS_PADDING);

        // Transform now owns sink.
        sink.release();

        // Attempt decrypt.
        ArraySource(data, dataLength, true, xfrm.release());

        // Decrypt had correct padding.
        return true;
    }
    catch (const CryptoPP::Exception& e)
    {
        LOG_err << "Failed AES-CBC pkcs decryption " << e.what();
        return false;
    }
}

void SymmCipher::ecb_encrypt(byte* data, byte* dst, size_t len)
{
    aesecb_e.ProcessData(dst ? dst : data, data, len);
}

void SymmCipher::ecb_decrypt(byte* data, size_t len)
{
    aesecb_d.ProcessData(data, data, len);
}

bool SymmCipher::ccm_encrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    if (!data || !result)
    {
        assert(data && result);
        return false;
    }

    try
    {
        if (taglen == 16)
        {
            aesccm16_e.Resynchronize(iv, ivlen);
            aesccm16_e.SpecifyDataLengths(0, data->size(), 0);
            StringSource ss(*data, true, new AuthenticatedEncryptionFilter(aesccm16_e, new StringSink(*result)));
            return true;
        }
        else if (taglen == 8)
        {
            aesccm8_e.Resynchronize(iv, ivlen);
            aesccm8_e.SpecifyDataLengths(0, data->size(), 0);
            StringSource ss(*data, true, new AuthenticatedEncryptionFilter(aesccm8_e, new StringSink(*result)));
            return true;
        }
    }
    catch (CryptoPP::Exception const& e)
    {
        LOG_err << "Failed AES-CCM encryption: " << e.GetWhat();
    }
    return false;
}

bool SymmCipher::ccm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    if (!data || !result)
    {
        assert(data && result);
        return false;
    }

    try
    {
        if (taglen == 16)
        {
            aesccm16_d.Resynchronize(iv, ivlen);
            aesccm16_d.SpecifyDataLengths(0, data->size() - taglen, 0);
            StringSource ss(*data, true, new AuthenticatedDecryptionFilter(aesccm16_d, new StringSink(*result)));
            return true;
        }
        else if (taglen == 8)
        {
            aesccm8_d.Resynchronize(iv, ivlen);
            aesccm8_d.SpecifyDataLengths(0, data->size() - taglen, 0);
            StringSource ss(*data, true, new AuthenticatedDecryptionFilter(aesccm8_d, new StringSink(*result)));
            return true;
        }
    }
    catch (HashVerificationFilter::HashVerificationFailed const &e)
    {
        result->clear();
        LOG_err << "Failed AES-CCM decryption: " << e.GetWhat();
    }
    return false;
}

bool SymmCipher::gcm_encrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    if (!data || !result)
    {
        assert(data && result);
        return false;
    }

    try
    {
        aesgcm_e.Resynchronize(iv, ivlen);
        StringSource ss(*data, true, new AuthenticatedEncryptionFilter(aesgcm_e, new StringSink(*result), false, taglen));
    }
    catch (CryptoPP::Exception const &e)
    {
        LOG_err << "Failed AES-GCM encryption: " << e.GetWhat();
        return false;
    }

    return true;
}


bool SymmCipher::gcm_encrypt_add(const byte* data, const size_t datasize, const byte* additionalData,
                                 const size_t additionalDatalen, const byte* iv, const size_t ivlen,
                                 const size_t taglen, std::string& result, const size_t expectedSize)
{
    if (!additionalData || !additionalData)
    {
        LOG_err << "Failed AES-GCM encryption with additional authenticated data. Invalid additional data";
        return false;
    }

    return gcm_encrypt(data, datasize, nullptr /*key*/, 0 /*keylen*/, additionalData, additionalDatalen, iv, ivlen, taglen, result, expectedSize);
}

bool SymmCipher::gcm_encrypt(const byte* data, const size_t datasize, const byte* key, const size_t keylen, const byte* additionalData,
                             const size_t additionalDatalen, const byte* iv, const size_t ivlen, const size_t taglen, std::string& result,
                             const size_t expectedSize)
{
    std::string err;
    if (!data || !datasize)                     {err = "Invalid plain text";}
    if (!iv || !ivlen)                          {err = "Invalid IV";}
    if (!err.empty())
    {
        LOG_err << "Failed AES-GCM encryption with additional authenticated data: " <<  err;
        return false;
    }

    try
    {
        if (!key || !keylen)
        {
            // resynchronizes with the provided IV
            aesgcm_e.Resynchronize(iv, static_cast<int>(ivlen));
        }
        else
        {
            // resynchronizes with the provided Key and IV
            aesgcm_e.SetKeyWithIV(key, keylen, iv, ivlen);
        }

        AuthenticatedEncryptionFilter ef (aesgcm_e, new StringSink(result), false, static_cast<int>(taglen));
        // add additionalData to channel for additional authenticated data
        if (additionalData && additionalDatalen)
        {
            ef.ChannelPut(AAD_CHANNEL, additionalData, additionalDatalen, true);
        }
        ef.ChannelMessageEnd(AAD_CHANNEL);

        // add plain text to DEFAULT_CHANNEL in order to be encrypted
        ef.ChannelPut(DEFAULT_CHANNEL, reinterpret_cast<const byte*>(data), datasize, true);
        ef.ChannelMessageEnd(DEFAULT_CHANNEL);

        if (expectedSize && expectedSize != result.size())
        {
            LOG_err << "Failed AES-GCM encryption with additional authenticated data, invalid encrypted data size";
            return false;
        }
    }
    catch (CryptoPP::Exception const &e)
    {
        LOG_err << "Failed AES-GCM encryption with additional authenticated data: " << e.GetWhat();
        return false;
    }
    return true;
}

bool SymmCipher::gcm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    if (!data || !result)
    {
        assert(data && result);
        return false;
    }

    try
    {
        aesgcm_d.Resynchronize(iv, ivlen);
        StringSource ss(*data, true, new AuthenticatedDecryptionFilter(aesgcm_d, new StringSink(*result), taglen));
    }
    catch (CryptoPP::Exception const& e)
    {
        result->clear();
        LOG_err << "Failed AES-GCM decryption: " << e.GetWhat();
        return false;
    }
    return true;
}

bool SymmCipher::gcm_decrypt(const byte* data, const size_t datalen, const byte* additionalData, const size_t additionalDatalen,
                             const byte* key, const size_t keylength, const byte* tag, const size_t taglen, const byte* iv,
                             const size_t ivlen, byte* result, const size_t resultSize)
{
    std::string err;
    if (!data || !datalen)                      {err = "Invalid data";}
    if (!tag || !taglen)                        {err = "Invalid tag";}
    if (!iv || !ivlen)                          {err = "Invalid IV";}
    if (!err.empty())
    {
        LOG_err << "Failed AES-GCM decryption with additional authenticated data: " <<  err;
        return false;
    }
    try
    {
        if (!key || !keylength)
        {
            // resynchronizes with provided IV
            aesgcm_d.Resynchronize(iv, static_cast<int>(ivlen));
        }
        else
        {
            // resynchronizes with the provided Key and IV
            aesgcm_d.SetKeyWithIV(key, keylength, iv, ivlen);
        }

        unsigned int flags = AuthenticatedDecryptionFilter::MAC_AT_BEGIN | AuthenticatedDecryptionFilter::THROW_EXCEPTION;
        AuthenticatedDecryptionFilter df(aesgcm_d, nullptr, flags, static_cast<int>(taglen));

        // add tag (GCM authentication tag) to DEFAULT_CHANNEL to check message hash or MAC
        df.ChannelPut(DEFAULT_CHANNEL, tag, taglen);

        if (additionalData && additionalDatalen)
        {
            // add additionalData to AAD_CHANNEL for additional authenticated data
            df.ChannelPut(AAD_CHANNEL, additionalData, additionalDatalen);
        }

        // add encrypted data to DEFAULT_CHANNEL in order to be decrypted
        df.ChannelPut(DEFAULT_CHANNEL, data, datalen);
        df.ChannelMessageEnd(AAD_CHANNEL);
        df.ChannelMessageEnd(DEFAULT_CHANNEL);

        // check data's integrity
        assert(df.GetLastResult());
        if (!df.GetLastResult())
        {
            LOG_err << "Failed AES-GCM decryption with additional authenticated data: integrity check failure";
            return false;
        }
        // retrieve decrypted data from channel
        df.SetRetrievalChannel(DEFAULT_CHANNEL);
        uint64_t maxRetrievable = df.MaxRetrievable();
        std::string retrieved;
        if (maxRetrievable <= 0 || maxRetrievable > resultSize)
        {
            LOG_err << "Failed AES-GCM decryption with additional authenticated data: output size mismatch";
            return false;
        }
        df.Get((byte*)result, maxRetrievable);
    }
    catch (CryptoPP::Exception const &e)
    {
        LOG_err << "Failed AES-GCM decryption with additional authenticated data: " << e.GetWhat();
        return false;
    }
    return true;
}

bool SymmCipher::gcm_decrypt_add(const byte* data, const size_t datalen, const byte* additionalData,
                                 const size_t additionalDatalen, const byte* tag, const size_t taglen,
                                 const byte* iv, const size_t ivlen, byte* result, const size_t resultSize)
{
    if (!additionalData || !additionalDatalen)
    {
        LOG_err << "Failed AES-GCM decryption with additional authenticated data. Invalid additional data";
        return false;
    }
    return gcm_decrypt(data, datalen, additionalData, additionalDatalen,
                       nullptr /*key*/, 0 /*keylength*/, tag, taglen,
                       iv, ivlen, result, resultSize);
}

bool SymmCipher::gcm_decrypt_with_key(const byte* data, const size_t datalen, const byte* key, const size_t keylength,
                                      const byte* tag, const size_t taglen, const byte* iv, const size_t ivlen,
                                      byte* result, const size_t resultSize)
{
    if (!key || !keylength)
    {
        LOG_err << "Failed AES-GCM decryption. Invalid decryption key";
        return false;
    }
    return gcm_decrypt(data, datalen, nullptr /*additionalData*/, 0 /*additionalDatalen*/,
                       key, keylength, tag, taglen, iv, ivlen, result, resultSize);
}

void SymmCipher::serializekeyforjs(string *d)
{
    char invertedkey[BLOCKSIZE];
    std::stringstream ss;

    ss << "[";
    for (int i=0; i<BLOCKSIZE; i++)
    {
        invertedkey[i] = key[BLOCKSIZE - i - 1];
    }

    int32_t *k = (int32_t *)invertedkey;
    for (int i = 3; i >= 0; i--)
    {
        ss << k[i];
        if (i)
        {
            ss << ",";
        }
    }
    ss << "]";
    *d = ss.str();
}

void SymmCipher::setint64(int64_t value, byte* data)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    value = htobe64(value);
#else
#if __BYTE_ORDER != __BIG_ENDIAN
#error "Unknown or unsupported endianness"
#endif
#endif
    memcpy(data, (char*)&value, sizeof value);
}

void SymmCipher::xorblock(const byte* src, byte* dst)
{
    if (((ptrdiff_t)src & (sizeof(long)-1)) == 0 && ((ptrdiff_t)dst & (sizeof(long)-1)) == 0) 
    {
        // src and dst aligned to machine word
        long* lsrc = (long*)src;
        long* ldst = (long*)dst;
        for (int i = BLOCKSIZE / sizeof(long); i--;)
        {
            ldst[i] ^= lsrc[i];
        }
    }
    else 
    {
        xorblock(src, dst, BLOCKSIZE);
    }
}

void SymmCipher::xorblock(const byte* src, byte* dst, int len)
{
    while (len--)
    {
        dst[len] ^= src[len];
    }
}

void SymmCipher::incblock(byte* dst, unsigned len)
{
    while (len)
    {
        if (++dst[--len])
        {
            break;
        }
    }
}

bool SymmCipher::isZeroKey(const byte* key, size_t keySize)
{
    if (!key)
    {
        // Invalid key pointer, consider it non-zero
        LOG_warn << "[SymmCipher::isZeroKey] invalid key pointer";
        assert(false && "[SymmCipher::isZeroKey] invalid key pointer");
        return false;
    }

    if (keySize == FILENODEKEYLENGTH) // 32 (filekey, nodekey, etc)
    {
        static_assert(FILENODEKEYLENGTH == SymmCipher::BLOCKSIZE * 2);
        // Check if the lower 16 bytes (0-15) are equal to the higher 16 bytes (16-31)
        // This will be true either if the key is all zeros or it was generated with a 16-byte zero key (for example, the transferkey was a zerokey).
        return std::memcmp(key /*key[0-15]*/, key + SymmCipher::BLOCKSIZE /*key[16-31]*/, SymmCipher::BLOCKSIZE) == 0;
    }
    else if (keySize == SymmCipher::BLOCKSIZE) // 16 (transfer key, client master key, etc)
    {
        // Check if all bytes are zero (zerokey)
        static const byte zeroKey[SymmCipher::BLOCKSIZE] = {};
        return std::memcmp(key, zeroKey, SymmCipher::BLOCKSIZE) == 0;
    }

    // Invalid key size, consider it non-zero
    LOG_warn << "[SymmCipher::isZeroKey] used a keySize(" << keySize << ") different from 32 and 16 -> function will return false";
    assert(false && "SymmCipher::isZeroKey used a keySize different from 32 and 16");
    return false;
}

SymmCipher::SymmCipher(const SymmCipher &ref)
{
    setkey(ref.key);
}

SymmCipher& SymmCipher::operator=(const SymmCipher& ref)
{
    setkey(ref.key);
    return *this;
}

// encryption: data must be NUL-padded to BLOCKSIZE
// decryption: data must be padded to BLOCKSIZE
// len must be < 2^31
void SymmCipher::ctr_crypt(byte* data, unsigned len, m_off_t pos, ctr_iv ctriv, byte* mac, bool encrypt, bool initmac)
{
    assert(!(pos & (KEYLENGTH - 1)));

    byte ctr[BLOCKSIZE], tmp[BLOCKSIZE];

    MemAccess::set<int64_t>(ctr,ctriv);
    setint64(pos / BLOCKSIZE, ctr + sizeof ctriv);

    if (mac && initmac)
    {
        memcpy(mac, ctr, sizeof ctriv);
        memcpy(mac + sizeof ctriv, ctr, sizeof ctriv);
    }

    while ((int)len > 0)
    {
        if (encrypt)
        {
            if(mac)
            {
                xorblock(data, mac);
                ecb_encrypt(mac);
            }

            ecb_encrypt(ctr, tmp);
            xorblock(tmp, data);
        }
        else
        {
            ecb_encrypt(ctr, tmp);
            xorblock(tmp, data);

            if (mac)
            {
                if (len >= (unsigned)BLOCKSIZE)
                {
                    xorblock(data, mac);
                }
                else
                {
                    xorblock(data, mac, len);
                }

                ecb_encrypt(mac);
            }
        }

        len -= BLOCKSIZE;
        data += BLOCKSIZE;

        incblock(ctr);
    }
}

static void rsaencrypt(const Integer* key, Integer* m)
{
    *m = a_exp_b_mod_c(*m, key[AsymmCipher::PUB_E], key[AsymmCipher::PUB_PQ]);
}

unsigned AsymmCipher::rawencrypt(const byte* plain, size_t plainlen, byte* buf, size_t buflen) const
{
    Integer t(plain, plainlen);

    rsaencrypt(key, &t);

    unsigned i = t.ByteCount();

    if (i > buflen)
    {
        return 0;
    }

    while (i--)
    {
        *buf++ = t.GetByte(i);
    }

    return t.ByteCount();
}

int AsymmCipher::encrypt(PrnGen &rng, const byte* plain, size_t plainlen, byte* buf, size_t buflen) const
{
    if (key[PUB_PQ].ByteCount() + 2 > buflen)
    {
        return 0;
    }

    if (buf != plain)
    {
        memcpy(buf, plain, plainlen);
    }

    // add random padding
    rng.genblock(buf + plainlen, key[PUB_PQ].ByteCount() - plainlen - 2);

    Integer t(buf, key[PUB_PQ].ByteCount() - 2);

    rsaencrypt(key, &t);

    unsigned int i = t.BitCount();

    byte* ptr = buf;

    *ptr++ = (byte)(i >> 8);
    *ptr++ = (byte)i;

    i = t.ByteCount();

    while (i--)
    {
        *ptr++ = t.GetByte(i);
    }

    return int(ptr - buf);
}

static void rsadecrypt(const Integer* key, Integer* m)
{
    Integer xp = a_exp_b_mod_c(*m % key[AsymmCipher::PRIV_P],
                               key[AsymmCipher::PRIV_D] % (key[AsymmCipher::PRIV_P] - Integer::One()),
                               key[AsymmCipher::PRIV_P]);
    Integer xq = a_exp_b_mod_c(*m % key[AsymmCipher::PRIV_Q],
                               key[AsymmCipher::PRIV_D] % (key[AsymmCipher::PRIV_Q] - Integer::One()),
                               key[AsymmCipher::PRIV_Q]);

    if (xp > xq)
    {
        *m = key[AsymmCipher::PRIV_Q] - (((xp - xq) * key[AsymmCipher::PRIV_U]) % key[AsymmCipher::PRIV_Q]);
    }
    else
    {
        *m = ((xq - xp) * key[AsymmCipher::PRIV_U]) % key[AsymmCipher::PRIV_Q];
    }

    *m = *m * key[AsymmCipher::PRIV_P] + xp;
}

unsigned AsymmCipher::rawdecrypt(const byte* cipher, size_t cipherlen, byte* buf, size_t buflen) const
{
    Integer m(cipher, cipherlen);

    rsadecrypt(key, &m);

    unsigned i = m.ByteCount();

    if (i > buflen)
    {
        return 0;
    }

    while (i--)
    {
        *buf++ = m.GetByte(i);
    }

    return m.ByteCount();
}

int AsymmCipher::decrypt(const byte* cipher, size_t cipherlen, byte* out, size_t numbytes) const
{
    Integer m;

    if (!decodeintarray(&m, 1, cipher, int(cipherlen)))
    {
        return 0;
    }

    rsadecrypt(key, &m);

    size_t l = key[AsymmCipher::PRIV_P].ByteCount() + key[AsymmCipher::PRIV_Q].ByteCount() - 2;

    if (m.ByteCount() > l)
    {
        l = m.ByteCount();
    }

    l -= numbytes;

    while (numbytes--)
    {
        out[numbytes] = m.GetByte(l++);
    }

    return 1;
}

const Integer& AsymmCipher::getKey(unsigned component) const
{
    assert(component < PRIVKEY);

    return key[component];
}

auto AsymmCipher::getKey() const -> const Key&
{
    return key;
}

int AsymmCipher::setkey(int numints, const byte* data, int len)
{
    // Assume key material is invalid.
    padding = 0;
    status  = S_UNKNOWN;

    // Deserialize key material.
    auto result = decodeintarray(key, numints, data, len);

    if (!result)
        return result;

    // We've been provided a private key.
    if (numints == PRIVKEY || numints == PRIVKEY_SHORT)
        return isvalid(numints) ? result : 0;

    // Convenience.
    auto e  =  key[PUB_E].ByteCount();
    auto pq = key[PUB_PQ].ByteCount();

    // Compute number of padding bytes.
    padding = static_cast<unsigned>(len) - pq - e - 4;

    // Return result to caller.
    return result;
}

void AsymmCipher::resetkey()
{
    std::fill(std::begin(key), std::end(key), Integer::Zero());

    padding = 0;
    status = S_INVALID;
}

void AsymmCipher::serializekeyforjs(string& d) const
{
    unsigned sizePQ = key[PUB_PQ].ByteCount();
    unsigned sizeE = key[PUB_E].ByteCount();
    char c;

    d.clear();
    d.reserve(sizePQ + sizeE + padding);

    for (unsigned int j = key[PUB_PQ].ByteCount(); j--;)
    {
        c = static_cast<char>(key[PUB_PQ].GetByte(j));
        d.append(&c, sizeof c);
    }

    // accounts created by webclient use 4 bytes for serialization of exponent
    // --> add left-padding up to 4 bytes for compatibility reasons
    c = 0;
    for (unsigned j = 0; j < padding; j++)
    {
        d.append(&c, sizeof c);
    }

    for (unsigned int j = sizeE; j--;)
    {
        c = static_cast<char>(key[PUB_E].GetByte(j));  // returns 0 if out-of-range
        d.append(&c, sizeof c);
    }
}

void AsymmCipher::serializekey(string* d, int keytype) const
{
    serializeintarray(key, keytype, d);
}

void AsymmCipher::serializeintarray(const Integer* t, int numints, string* d, bool headers)
{
    unsigned size = 0;
    unsigned char c;

    for (int i = numints; i--;)
    {
        size += t[i].ByteCount();

        if (headers)
        {
            size += 2;
        }
    }

    d->reserve(d->size() + size);

    for (int i = 0; i < numints; i++)
    {
        if (headers)
        {
            unsigned int bitCount = t[i].ByteCount() * 8;
            c = (bitCount & 0x0000FF00) >> 8;
            d->append((char*)(&c), sizeof c);

            c = bitCount & 0x000000FF;
            d->append((char*)&c, sizeof c);
        }

        for (int j = t[i].ByteCount(); j--;)
        {
            c = t[i].GetByte(j);
            d->append((char*)&c, sizeof c);
        }
    }
}

int AsymmCipher::decodeintarray(Integer* t, int numints, const byte* data, int len)
{
    int p, i, n;

    p = 0;

    for (i = 0; i < numints; i++)
    {
        if (p + 2 > len)
        {
            break;
        }

        n = ((data[p] << 8) + data[p + 1] + 7) >> 3;

        p += 2;
        if (p + n > len)
        {
            break;
        }

        t[i] = Integer(data + p, n);

        p += n;
    }

    // If u is not present, calculate it.
    if (numints == PRIVKEY_SHORT)
    {
        t[PRIV_U] = t[PRIV_P].InverseMod(t[PRIV_Q]);
    }

    return i == numints && len - p < 16;
}

bool AsymmCipher::isvalid(int type) const
{
    if (status == S_UNKNOWN)
        status = isvalid(key, type);

    return status == S_VALID;
}

auto AsymmCipher::isvalid(const Key& key, int type) const -> Status
{
    assert(type >= PUBKEY && type <= PRIVKEY);

    if (type == PUBKEY)
    {
        if (key[PUB_E].BitCount() && key[PUB_PQ].BitCount())
            return S_VALID;

        return S_INVALID;
    }

    // Convenience.
    auto& d = key[PRIV_D];
    auto& p = key[PRIV_P];
    auto& u = key[PRIV_U];
    auto& q = key[PRIV_Q];

    // detect private key blob corruption.
    // prevent API-exploitable RSA oracle requiring 500+ logins
    if (d.BitCount() <= 2000
        || p.BitCount() <= 1000
        || q.BitCount() <= 1000
        || u.BitCount() <= 1000
        || u != p.InverseMod(q))
        return S_INVALID;

    return S_VALID;
}

// adapted from CryptoPP, rsa.cpp
class RSAPrimeSelector : public PrimeSelector
{
    Integer m_e;

public:
    RSAPrimeSelector(const Integer &e) : m_e(e) { }

    bool IsAcceptable(const Integer &candidate) const
    {
        return RelativelyPrime(m_e, candidate - Integer::One());
    }
};

// generate RSA keypair
void AsymmCipher::genkeypair(PrnGen &rng, Integer* privk, Integer* pubk, int size)
{
    pubk[PUB_E] = 17;

    RSAPrimeSelector selector(pubk[PUB_E]);
    AlgorithmParameters primeParam
            = MakeParametersForTwoPrimesOfEqualSize(size)
                (Name::PointerToPrimeSelector(), selector.GetSelectorPointer());

    privk[PRIV_P].GenerateRandom(rng, primeParam);
    privk[PRIV_Q].GenerateRandom(rng, primeParam);

    privk[PRIV_D] = pubk[PUB_E].InverseMod(LCM(privk[PRIV_P] - Integer::One(), privk[PRIV_Q] - Integer::One()));
    pubk[PUB_PQ] = privk[PRIV_P] * privk[PRIV_Q];
    privk[PRIV_U] = privk[PRIV_P].InverseMod(privk[PRIV_Q]);
}

void AsymmCipher::genkeypair(PrnGen &rng, Integer* pubk, int size)
{
    assert(pubk);

    genkeypair(rng, key, pubk, size);

    // Consider the keys we generate as valid.
    status = S_VALID;
}

void Hash::add(const byte* data, unsigned len)
{
    hash.Update(data, len);
}

void Hash::get(string* out)
{
    out->resize(hash.DigestSize());
    hash.Final((byte*)out->data());
}

void HashSHA256::add(const byte *data, unsigned int len)
{
    hash.Update(data, len);
}

void HashSHA256::get(std::string *retStr)
{
    retStr->resize(hash.DigestSize());
    hash.Final((byte*)retStr->data());
}

void HashCRC32::add(const byte* data, unsigned len)
{
    hash.Update(data, len);
}

void HashCRC32::get(byte* out)
{
    hash.Final(out);
}

HMACSHA256::HMACSHA256(const byte *key, size_t length)
    : hmac(key, length)
{
}

HMACSHA256::HMACSHA256()
{
}

void HMACSHA256::add(const byte *data, size_t len)
{
    hmac.Update(data, len);
}

void HMACSHA256::get(byte *out)
{
    hmac.Final(out);
}

void HMACSHA256::setkey(const byte* key, const size_t length)
{
    assert(key || length == 0);

    hmac.SetKey(key, length);
}

PBKDF2_HMAC_SHA512::PBKDF2_HMAC_SHA512()
{
}

bool PBKDF2_HMAC_SHA512::deriveKey(byte* derivedkey,
                                   const size_t derivedkeyLen,
                                   const byte* pwd,
                                   const size_t pwdLen,
                                   const byte* salt,
                                   const size_t saltLen,
                                   const unsigned int iterations) const
{
    assert(derivedkey);
    assert(derivedkeyLen > 0);
    assert(pwd);
    assert(pwdLen > 0);
    assert(salt);
    assert(saltLen > 0);
    assert(iterations > 0);

    try
    {
        pbkdf2.DeriveKey(
            // buffer that holds the derived key
            derivedkey, derivedkeyLen,
            // purpose byte. unused by this PBKDF implementation.
            0x00,
            // password bytes. careful to be consistent with encoding...
            pwd, pwdLen,
            // salt bytes
            salt, saltLen,
            // iteration count. See SP 800-132 for details. You want this as large as you can tolerate.
            // make sure to use the same iteration count on both sides...
            iterations
        );
        return true;
    }
    catch (const CryptoPP::Exception& e)
    {
        // DeriveKey() should throw CryptoPP::InvalidDerivedLength, however that is not present in all
        // versions of the lib, i.e. Linux system lib
        LOG_err << "PKCS5_PBKDF2_HMAC<T>::DeriveKey() exception: " << e.what();
        return false;
    }
}

} // namespace
