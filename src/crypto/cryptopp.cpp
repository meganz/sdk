/**
 * @file cryptopp.cpp
 * @brief Crypto layer using Crypto++
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

#include "mega.h"

namespace mega {
#ifndef htobe64
#define htobe64(x) (((uint64_t)htonl((uint32_t)((x) >> 32))) | (((uint64_t)htonl((uint32_t)x)) << 32))
#endif

using namespace CryptoPP;

AutoSeededRandomPool PrnGen::rng;

// cryptographically strong random byte sequence
void PrnGen::genblock(byte* buf, int len)
{
    rng.GenerateBlock(buf, len);
}

// random number from 0 ... max-1
uint32_t PrnGen::genuint32(uint64_t max)
{
    uint32_t t;

    genblock((byte*)&t, sizeof t);

    return (uint32_t)(((uint64_t)t) / ((((uint64_t)(~(uint32_t)0)) + 1) / max));
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

void SymmCipher::cbc_encrypt(byte* data, unsigned len, const byte* iv)
{
    aescbc_e.Resynchronize(iv ? iv : zeroiv);
    aescbc_e.ProcessData(data, data, len);
}

void SymmCipher::cbc_decrypt(byte* data, unsigned len, const byte* iv)
{
    aescbc_d.Resynchronize(iv ? iv : zeroiv);
    aescbc_d.ProcessData(data, data, len);
}

void SymmCipher::cbc_encrypt_pkcs_padding(const string *data, const byte *iv, string *result)
{
    aescbc_e.Resynchronize(iv ? iv : zeroiv);
    StringSource(*data, true,
           new StreamTransformationFilter( aescbc_e, new StringSink( *result ),
                                                     StreamTransformationFilter::PKCS_PADDING));
}

void SymmCipher::cbc_decrypt_pkcs_padding(const std::string *data, const byte *iv, string *result)
{
    aescbc_d.Resynchronize(iv ? iv : zeroiv);
    StringSource(*data, true,
           new StreamTransformationFilter( aescbc_d, new StringSink( *result ),
                                                     StreamTransformationFilter::PKCS_PADDING));
}

void SymmCipher::ecb_encrypt(byte* data, byte* dst, unsigned len)
{
    aesecb_e.ProcessData(dst ? dst : data, data, len);
}

void SymmCipher::ecb_decrypt(byte* data, unsigned len)
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

void SymmCipher::ccm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
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
    } catch (HashVerificationFilter::HashVerificationFailed e)
    {
        result->clear();
        LOG_err << "Failed AES-CCM decryption: " << e.GetWhat();
    }
}

void SymmCipher::gcm_encrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    aesgcm_e.Resynchronize(iv, ivlen);
    StringSource(*data, true, new AuthenticatedEncryptionFilter(aesgcm_e, new StringSink(*result), false, taglen));
}

void SymmCipher::gcm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result)
{
    aesgcm_d.Resynchronize(iv, ivlen);
    try {
        StringSource(*data, true, new AuthenticatedDecryptionFilter(aesgcm_d, new StringSink(*result), taglen));
    } catch (HashVerificationFilter::HashVerificationFailed e)
    {
        result->clear();
        LOG_err << "Failed AES-GCM decryption: " << e.GetWhat();
    }
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
    long* lsrc = (long*)src;
    long* ldst = (long*)dst;

    for (int i = BLOCKSIZE / sizeof(long); i--;)
    {
        ldst[i] ^= lsrc[i];
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

unsigned AsymmCipher::rawencrypt(const byte* plain, int plainlen, byte* buf, int buflen)
{
    Integer t(plain, plainlen);

    rsaencrypt(key, &t);

    int i = t.ByteCount();

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

int AsymmCipher::encrypt(const byte* plain, int plainlen, byte* buf, int buflen)
{
    if ((int)key[PUB_PQ].ByteCount() + 2 > buflen)
    {
        return 0;
    }

    if (buf != plain)
    {
        memcpy(buf, plain, plainlen);
    }

    // add random padding
    PrnGen::genblock(buf + plainlen, key[PUB_PQ].ByteCount() - plainlen - 2);

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

    return ptr - buf;
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

unsigned AsymmCipher::rawdecrypt(const byte* cipher, int cipherlen, byte* buf, int buflen)
{
    Integer m(cipher, cipherlen);

    rsadecrypt(key, &m);

    int i = m.ByteCount();

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

int AsymmCipher::decrypt(const byte* cipher, int cipherlen, byte* out, int numbytes)
{
    Integer m;

    if (!decodeintarray(&m, 1, cipher, cipherlen))
    {
        return 0;
    }

    rsadecrypt(key, &m);

    unsigned l = key[AsymmCipher::PRIV_P].ByteCount() + key[AsymmCipher::PRIV_Q].ByteCount() - 2;

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
            c = t[i].BitCount() >> 8;
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
        return key[PRIV_P].BitCount() &&
                key[PRIV_Q].BitCount() &&
                key[PRIV_D].BitCount() &&
                key[PRIV_U].BitCount();
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
void AsymmCipher::genkeypair(Integer* privk, Integer* pubk, int size)
{
    pubk[PUB_E] = 17;

    RSAPrimeSelector selector(pubk[PUB_E]);
    AlgorithmParameters primeParam
            = MakeParametersForTwoPrimesOfEqualSize(size)
                (Name::PointerToPrimeSelector(), selector.GetSelectorPointer());

    privk[PRIV_P].GenerateRandom(PrnGen::rng, primeParam);
    privk[PRIV_Q].GenerateRandom(PrnGen::rng, primeParam);

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

void HMACSHA256::add(const byte *data, unsigned len)
{
    hmac.Update(data, len);
}

void HMACSHA256::get(byte *out)
{
    hmac.Final(out);
}

PBKDF2_HMAC_SHA512::PBKDF2_HMAC_SHA512()
{
}

void PBKDF2_HMAC_SHA512::deriveKey(byte* derivedkey, size_t derivedkeyLen,
                                   byte* pwd, size_t pwdLen,
                                   byte* salt, size_t saltLen, unsigned int iterations)
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
}

} // namespace
