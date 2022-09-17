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

byte SymmCipher::zeroiv[BLOCKSIZE];

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

void SymmCipher::cbc_encrypt(byte* data, size_t len, const byte* iv)
{
    aescbc_e.Resynchronize(iv ? iv : zeroiv);
    aescbc_e.ProcessData(data, data, len);
}

void SymmCipher::cbc_decrypt(byte* data, size_t len, const byte* iv)
{
    aescbc_d.Resynchronize(iv ? iv : zeroiv);
    aescbc_d.ProcessData(data, data, len);
}

void SymmCipher::cbc_encrypt_pkcs_padding(const string *data, const byte *iv, string *result)
{
    using Transformation = StreamTransformationFilter;

    // Update IV.
    aescbc_e.Resynchronize(iv ? iv : zeroiv);

    // Create sink.
    unique_ptr<StringSink> sink =
      mega::make_unique<StringSink>(*result);

    // Create transform.
    unique_ptr<Transformation> xfrm =
      mega::make_unique<Transformation>(aescbc_e,
                                        sink.get(),
                                        Transformation::PKCS_PADDING);

    // Transform now owns sink.
    sink.release();

    // Encrypt.
    StringSource(*data, true, xfrm.release());
}

bool SymmCipher::cbc_decrypt_pkcs_padding(const std::string* data, const byte* iv, string* result)
{
    try 
    {
        using Transformation = StreamTransformationFilter;

        // Update IV.
        aescbc_d.Resynchronize(iv ? iv : zeroiv);

        // Create sink.
        unique_ptr<StringSink> sink =
          mega::make_unique<StringSink>(*result);
        
        // Create transform.
        unique_ptr<Transformation> xfrm =
          mega::make_unique<Transformation>(aescbc_d,
                                            sink.get(),
                                            Transformation::PKCS_PADDING);

        // Transform now owns sink.
        sink.release();

        // Attempt decrypt.
        StringSource(*data, true, xfrm.release());

        // Decrypt had correct padding.
        return true;
    }
    catch (...)
    {
        // Decrypt failed.
        return false;
    }
}

bool SymmCipher::cbc_decrypt_pkcs_padding(const byte* data,
                                          const size_t dataLength,
                                          const byte* iv,
                                          std::string* result)
{
    try 
    {
        using Transformation = StreamTransformationFilter;

        // Update IV.
        aescbc_d.Resynchronize(iv ? iv : zeroiv);

        // Create sink.
        unique_ptr<StringSink> sink =
          mega::make_unique<StringSink>(*result);
        
        // Create transform.
        unique_ptr<Transformation> xfrm =
          mega::make_unique<Transformation>(aescbc_d,
                                            sink.get(),
                                            Transformation::PKCS_PADDING);

        // Transform now owns sink.
        sink.release();

        // Attempt decrypt.
        ArraySource(data, dataLength, true, xfrm.release());

        // Decrypt had correct padding.
        return true;
    }
    catch (...)
    {
        // Decrypt failed.
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

void SymmCipher::ccm_encrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    if (taglen == 16)
    {
        aesccm16_e.Resynchronize(iv, ivlen);
        aesccm16_e.SpecifyDataLengths(0, data->size(), 0);
        StringSource(*data, true, new AuthenticatedEncryptionFilter(aesccm16_e, new StringSink(*result)));
    }
    else if (taglen == 8)
    {
        aesccm8_e.Resynchronize(iv, ivlen);
        aesccm8_e.SpecifyDataLengths(0, data->size(), 0);
        StringSource(*data, true, new AuthenticatedEncryptionFilter(aesccm8_e, new StringSink(*result)));
    }
}

bool SymmCipher::ccm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    try {
        if (taglen == 16)
        {
            aesccm16_d.Resynchronize(iv, ivlen);
            aesccm16_d.SpecifyDataLengths(0, data->size() - taglen, 0);
            StringSource(*data, true, new AuthenticatedDecryptionFilter(aesccm16_d, new StringSink(*result)));
        }
        else if (taglen == 8)
        {
            aesccm8_d.Resynchronize(iv, ivlen);
            aesccm8_d.SpecifyDataLengths(0, data->size() - taglen, 0);
            StringSource(*data, true, new AuthenticatedDecryptionFilter(aesccm8_d, new StringSink(*result)));
        }
    } catch (HashVerificationFilter::HashVerificationFailed const &e)
    {
        result->clear();
        LOG_err << "Failed AES-CCM decryption: " << e.GetWhat();
        return false;
    }
    return true;
}

bool SymmCipher::gcm_encrypt_aad(const unsigned char *data, size_t datasize, const byte *additionalData, unsigned additionalDatalen, const byte *iv, unsigned ivlen, unsigned taglen, byte *result, size_t resultSize)
{
    std::string err;
    if (!data || !datasize)                     {err = "Invalid plain text";}
    if (!additionalData || !additionalDatalen)  {err = "Invalid additional data";}
    if (!iv || !ivlen)                          {err = "Invalid IV";}

    if (!err.empty())
    {
        LOG_err << "Failed AES-GCM encryption with additional authenticated data: " <<  err;
        return false;
    }

    try
    {
        // resynchronizes with the provided IV
        aesgcm_e.Resynchronize(iv, static_cast<int>(ivlen));
        AuthenticatedEncryptionFilter ef (aesgcm_e, new ArraySink(result, resultSize), false, static_cast<int>(taglen));

        // add additionalData to channel for additional authenticated data
        ef.ChannelPut(AAD_CHANNEL, additionalData, additionalDatalen, true);
        ef.ChannelMessageEnd(AAD_CHANNEL);

        // add plain text to DEFAULT_CHANNEL in order to be encrypted
        ef.ChannelPut(DEFAULT_CHANNEL, reinterpret_cast<const byte*>(data), datasize, true);
        ef.ChannelMessageEnd(DEFAULT_CHANNEL);
    }
    catch (CryptoPP::Exception const &e)
    {
        LOG_err << "Failed AES-GCM encryption with additional authenticated data: " << e.GetWhat();
        return false;
    }
    return true;
}

void SymmCipher::gcm_encrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    aesgcm_e.Resynchronize(iv, ivlen);
    StringSource(*data, true, new AuthenticatedEncryptionFilter(aesgcm_e, new StringSink(*result), false, taglen));
}

bool SymmCipher::gcm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    aesgcm_d.Resynchronize(iv, ivlen);
    try {
        StringSource(*data, true, new AuthenticatedDecryptionFilter(aesgcm_d, new StringSink(*result), taglen));
    } catch (HashVerificationFilter::HashVerificationFailed const &e)
    {
        result->clear();
        LOG_err << "Failed AES-GCM decryption: " << e.GetWhat();
        return false;
    }
    return true;
}

bool SymmCipher::gcm_decrypt_aad(const byte *data, unsigned datalen,
                     const byte *additionalData, unsigned additionalDatalen,
                     const byte *tag, unsigned taglen,
                     const byte *iv, unsigned ivlen,
                     byte *result,
                     size_t resultSize)
{
    std::string err;
    if (!data || !datalen)                      {err = "Invalid data";}
    if (!additionalData || !additionalDatalen)  {err = "Invalid additional data";}
    if (!tag || !taglen)                        {err = "Invalid tag";}
    if (!iv || !ivlen)                          {err = "Invalid IV";}
    if (!err.empty())
    {
        LOG_err << "Failed AES-GCM decryption with additional authenticated data: " <<  err;
        return false;
    }

    try
    {
        // resynchronizes with provided IV
        aesgcm_d.Resynchronize(iv, static_cast<int>(ivlen));
        unsigned int flags = AuthenticatedDecryptionFilter::MAC_AT_BEGIN | AuthenticatedDecryptionFilter::THROW_EXCEPTION;
        AuthenticatedDecryptionFilter df(aesgcm_d, nullptr, flags, static_cast<int>(taglen));

        // add tag (GCM authentication tag) to DEFAULT_CHANNEL to check message hash or MAC
        df.ChannelPut(DEFAULT_CHANNEL, tag, taglen);

        // add additionalData to AAD_CHANNEL for additional authenticated data
        df.ChannelPut(AAD_CHANNEL, additionalData, additionalDatalen);

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

static void rsaencrypt(Integer* key, Integer* m)
{
    *m = a_exp_b_mod_c(*m, key[AsymmCipher::PUB_E], key[AsymmCipher::PUB_PQ]);
}

unsigned AsymmCipher::rawencrypt(const byte* plain, size_t plainlen, byte* buf, size_t buflen)
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

int AsymmCipher::encrypt(PrnGen &rng, const byte* plain, size_t plainlen, byte* buf, size_t buflen)
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

    int i = t.BitCount();

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

static void rsadecrypt(Integer* key, Integer* m)
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

unsigned AsymmCipher::rawdecrypt(const byte* cipher, size_t cipherlen, byte* buf, size_t buflen)
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

int AsymmCipher::decrypt(const byte* cipher, size_t cipherlen, byte* out, size_t numbytes)
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

int AsymmCipher::setkey(int numints, const byte* data, int len)
{
    int ret = decodeintarray(key, numints, data, len);
    if (numints == PRIVKEY && ret && !isvalid(numints)) return 0;
    padding = (numints == PUBKEY && ret) ? (len - key[PUB_PQ].ByteCount() - key[PUB_E].ByteCount() - 4) : 0;
    return ret;
}

void AsymmCipher::resetkey()
{
    for (int i = 0; i < PRIVKEY; i++)
    {
        key[i] = Integer::Zero();
        padding = 0;
    }
}

void AsymmCipher::serializekeyforjs(string& d)
{
    unsigned sizePQ = key[PUB_PQ].ByteCount();
    unsigned sizeE = key[PUB_E].ByteCount();
    char c;

    d.clear();
    d.reserve(sizePQ + sizeE + padding);

    for (int j = key[PUB_PQ].ByteCount(); j--;)
    {
        c = key[PUB_PQ].GetByte(j);
        d.append(&c, sizeof c);
    }

    // accounts created by webclient use 4 bytes for serialization of exponent
    // --> add left-padding up to 4 bytes for compatibility reasons
    c = 0;
    for (unsigned j = 0; j < padding; j++)
    {
        d.append(&c, sizeof c);
    }

    for (int j = sizeE; j--;)
    {
        c = key[PUB_E].GetByte(j);  // returns 0 if out-of-range
        d.append(&c, sizeof c);
    }
}

void AsymmCipher::serializekey(string* d, int keytype)
{
    serializeintarray(key, keytype, d);
}

void AsymmCipher::serializeintarray(Integer* t, int numints, string* d, bool headers)
{
    unsigned size = 0;
    char c;

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
            c = static_cast<char>(t[i].BitCount() >> 8);
            d->append(&c, sizeof c);

            c = (char)t[i].BitCount();
            d->append(&c, sizeof c);
        }

        for (int j = t[i].ByteCount(); j--;)
        {
            c = t[i].GetByte(j);
            d->append(&c, sizeof c);
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

    return i == numints && len - p < 16;
}

int AsymmCipher::isvalid(int keytype)
{
    if (keytype == PUBKEY)
    {
        return key[PUB_PQ].BitCount() && key[PUB_E].BitCount();
    }

    if (keytype == PRIVKEY)
    {
        // detect private key blob corruption - prevent API-exploitable RSA oracle requiring 500+ logins
        return key[PRIV_P].BitCount() > 1000 &&
                key[PRIV_Q].BitCount() > 1000 &&
                key[PRIV_D].BitCount() > 2000 &&
                key[PRIV_U].BitCount() > 1000 &&
                key[PRIV_U] == key[PRIV_P].InverseMod(key[PRIV_Q]);
    }

    return 0;
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

void PBKDF2_HMAC_SHA512::deriveKey(byte* derivedkey,
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
}

} // namespace
