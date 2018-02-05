/**
 * @file cryptopp.h
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

#ifdef USE_CRYPTOPP
#ifndef CRYPTOCRYPTOPP_H
#define CRYPTOCRYPTOPP_H 1

#include <cryptopp/cryptlib.h>
#include <cryptopp/modes.h>
#include <cryptopp/ccm.h>
#include <cryptopp/gcm.h>
#include <cryptopp/integer.h>
#include <cryptopp/aes.h>
#include <cryptopp/osrng.h>
#include <cryptopp/sha.h>
#include <cryptopp/rsa.h>
#include <cryptopp/crc.h>
#include <cryptopp/nbtheory.h>
#include <cryptopp/algparam.h>
#include <cryptopp/hmac.h>
#include <cryptopp/pwdbased.h>

namespace mega {
using namespace std;

/**
 * @brief A generic pseudo-random number generator.
 */
class MEGA_API PrnGen
{
public:
    static CryptoPP::AutoSeededRandomPool rng;

    /**
     * @brief Generates a block of random bytes of length `len` into a buffer
     *        `buf`.
     *
     * @param buf The buffer that takes the generated random bytes. Ensure that
     *     the buffer is of sufficient size to take `len` bytes.
     * @param len The number of random bytes to generate.
     * @return Void.
     */
    static void genblock(byte* buf, int len);

    /**
     * @brief Generates a random integer between 0 ... max - 1.
     *
     * @param max The maximum of which the number is to generate under.
     * @return The random number generated.
     */
    static uint32_t genuint32(uint64_t max);
};

// symmetric cryptography: AES-128
class MEGA_API SymmCipher
{
public:

private:
    CryptoPP::ECB_Mode<CryptoPP::AES>::Encryption aesecb_e;
    CryptoPP::ECB_Mode<CryptoPP::AES>::Decryption aesecb_d;

    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption aescbc_e;
    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption aescbc_d;

    CryptoPP::CCM<CryptoPP::AES, 16>::Encryption aesccm16_e;
    CryptoPP::CCM<CryptoPP::AES, 16>::Decryption aesccm16_d;

    CryptoPP::CCM<CryptoPP::AES, 8>::Encryption aesccm8_e;
    CryptoPP::CCM<CryptoPP::AES, 8>::Decryption aesccm8_d;

    CryptoPP::GCM<CryptoPP::AES>::Encryption aesgcm_e;
    CryptoPP::GCM<CryptoPP::AES>::Decryption aesgcm_d;

public:
    static byte zeroiv[CryptoPP::AES::BLOCKSIZE];

    static const int BLOCKSIZE = CryptoPP::AES::BLOCKSIZE;
    static const int KEYLENGTH = CryptoPP::AES::BLOCKSIZE;

    byte key[KEYLENGTH];

    typedef uint64_t ctr_iv;

    void setkey(const byte*, int = 1);
    bool setkey(const string*);

    /**
     * @brief Encrypt symmetrically using AES in ECB mode.
     *
     * @param data Data to be encrypted.
     * @param dst Target buffer to encrypt to. If NULL, encrypt in-place (to `data`).
     * @param len Length of data to be encrypted in bytes. Defaults to
     *     SymCipher::BLOCKSIZE.
     * @return Void.
     */
    void ecb_encrypt(byte*, byte* = NULL, unsigned = BLOCKSIZE);

    /**
     * @brief Decrypt symmetrically using AES in ECB mode.
     *
     * @param data Data to be decrypted (in-place).
     * @param len Length of data to be decrypted in bytes. Defaults to
     *     SymCipher::BLOCKSIZE.
     * @return Void.
     */
    void ecb_decrypt(byte*, unsigned = BLOCKSIZE);

    /**
     * @brief Encrypt symmetrically using AES in CBC mode.
     *
     * The size of the IV is one block in AES-128 (16 bytes).
     *
     * @param data Data to be encrypted (encryption in-place).
     * @param len Length of data to be encrypted in bytes.
     * @param iv Initialisation vector to use. Choose randomly and never re-use.
     * @return Void.
     */
    void cbc_encrypt(byte* data, unsigned len, const byte* iv = NULL);

    /**
     * @brief Decrypt symmetrically using AES in CBC mode.
     *
     * The size of the IV is one block in AES-128 (16 bytes).
     *
     * @param data Data to be decrypted (encryption in-place).
     * @param len Length of cipher text to be decrypted in bytes.
     * @param iv Initialisation vector.
     * @return Void.
     */
    void cbc_decrypt(byte* data, unsigned len, const byte* iv = NULL);

    /**
     * @brief Encrypt symmetrically using AES in CBC mode and pkcs padding
     *
     * The size of the IV is one block in AES-128 (16 bytes).
     *
     * @param data Data to be encrypted
     * @param iv Initialisation vector.
     * @param result Encrypted message
     * @return Void.
     */
    void cbc_encrypt_pkcs_padding(const string *data, const byte* iv, string *result);

    /**
     * @brief Decrypt symmetrically using AES in CBC mode and pkcs padding
     *
     * The size of the IV is one block in AES-128 (16 bytes).
     *
     * @param data Data to be decrypted
     * @param iv Initialisation vector.
     * @param result Decrypted message
     * @return Void.
     */
    void cbc_decrypt_pkcs_padding(const string *data, const byte* iv, string *result);

    /**
     * Authenticated symmetric encryption using AES in CCM mode (counter with CBC-MAC).
     *
     * The size of the IV limits the maximum length of data. A length of 12 bytes
     * allows for up to 16.7 MB data size. Smaller IVs lead to larger maximum data
     * sizes.
     *
     * @param data Data to be encrypted.
     * @param iv Initialisation vector or nonce to use for encryption. Choose randomly
     * and never re-use. See note on size above.
     * @param ivlen Length of IV. Allowed sizes are 7, 8, 9, 10, 11, 12, and 13 bytes.
     * @param taglen Length of expected authentication tag. Allowed sizes are 8 and 16 bytes.
     * @param result Encrypted data, including the authentication tag.
     */
    void ccm_encrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result);

    /**
     * @brief Authenticated symmetric decryption using AES in CCM mode (counter with CBC-MAC).
     *
     * The size of the IV limits the maximum length of data. A length of 12 bytes
     * allows for up to 16.7 MB data size. Smaller IVs lead to larger maximum data
     * sizes.
     *
     * @param data Data to be decrypted.
     * @param iv Initialisation vector or nonce.
     * @param ivlen Length of IV. Allowed sizes are 7, 8, 9, 10, 11, 12, and 13 bytes.
     * @param taglen Length of expected authentication tag. Allowed sizes are 8 and 16 bytes.
     * @param result Decrypted data, not including the authentication tag.
     */
    void ccm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result);

    /**
     * @brief Authenticated symmetric encryption using AES in GCM mode.
     *
     * The size of the IV limits the maximum length of data. A length of 12 bytes
     * allows for up to 16.7 MB data size. Smaller IVs lead to larger maximum data
     * sizes.
     *
     * @param data Data to be encrypted.
     * @param iv Initialisation vector or nonce to use for encryption. Choose randomly
     * and never re-use. See note on size above.
     * @param ivlen Length of IV. Allowed sizes are 7, 8, 9, 10, 11, 12, and 13 bytes.
     * @param taglen Length of expected authentication tag.
     * @param result Encrypted data, including the authentication tag.
     */
    void gcm_encrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result);

    /**
     * @brief Authenticated symmetric decryption using AES in GCM mode.
     *
     * The size of the IV limits the maximum length of data. A length of 12 bytes
     * allows for up to 16.7 MB data size. Smaller IVs lead to larger maximum data
     * sizes.
     *
     * @param data Data to be decrypted.
     * @param iv Initialisation vector or nonce.
     * @param ivlen Length of IV. Allowed sizes are 7, 8, 9, 10, 11, 12, and 13 bytes.
     * @param taglen Length of expected authentication tag. Allowed sizes are 8 and 16 bytes.
     * @param result Decrypted data, not including the authentication tag.
     */
    void gcm_decrypt(const string *data, const byte *iv, unsigned ivlen, unsigned taglen, string *result);

    /**
     * @brief Serialize key for compatibility with the webclient
     *
     * The key is serialized to a JSON array like this one:
     * "[669070598,-250738112,2059051645,-1942187558]"
     *
     * @param d string that receives the serialized key
     */
    void serializekeyforjs(string *);

    void ctr_crypt(byte *, unsigned, m_off_t, ctr_iv, byte *, bool, bool initmac = true);

    static void setint64(int64_t, byte*);

    static void xorblock(const byte*, byte*);
    static void xorblock(const byte*, byte*, int);

    static void incblock(byte*, unsigned = BLOCKSIZE);

    SymmCipher() { }
    SymmCipher(const SymmCipher& ref);
    SymmCipher& operator=(const SymmCipher& ref);
    SymmCipher(const byte*);
};

/**
 * @brief Asymmetric cryptography using RSA.
 */
class MEGA_API AsymmCipher
{
    int decodeintarray(CryptoPP::Integer*, int, const byte*, int);

public:
    enum { PRIV_P, PRIV_Q, PRIV_D, PRIV_U };
    enum { PUB_PQ, PUB_E };

    static const int PRIVKEY = 4;
    static const int PUBKEY = 2;

    CryptoPP::Integer key[PRIVKEY];
    unsigned int padding;

    static const int MAXKEYLENGTH = 1026;   // in bytes, allows for RSA keys up
                                            // to 8192 bits

    /**
     * @brief Sets a key from a buffer.
     *
     * @param numints Number of integers for key type (AsymmCipher::PRIVKEY
     *     or AsymmCipher::PUBKEY).
     * @param data Buffer containing the serialised key.
     * @param len Length of data in buffer.
     * @return Number of bytes encrypted, 0 on failure.
     */
    int setkey(int numints, const byte* data, int len);

    /**
     * @brief Reset the existing key
     */
    void resetkey();

    /**
     * @brief Simple check for validity of key pair.
     *
     * @param keytype Key type indication by number of integers for key type
     *     (AsymmCipher::PRIVKEY or AsymmCipher::PUBKEY).
     * @return 0 on an invalid key pair.
     */
    int isvalid(int keytype = PUBKEY);

    /**
     * @brief Encrypts a randomly padded plain text into a buffer.
     *
     * @param plain The plain text to encrypt.
     * @param plainlen Length of the plain text.
     * @param buf Buffer to take the cipher text..
     * @param buflen Length of the cipher text.
     * @return Number of bytes encrypted, 0 on failure.
     */
    int encrypt(const byte* plain, int plainlen, byte* buf, int buflen);

    /**
     * @brief Decrypts a cipher text into a buffer and strips random padding.
     *
     * @param cipher The cipher text to encrypt.
     * @param cipherlen Length of the cipher text.
     * @param buf Buffer to take the plain text..
     * @param buflen Length of the plain text.
     * @return Always returns 1.
     */
    int decrypt(const byte* cipher, int cipherlen, byte* buf, int buflen);

    /**
     * @brief Encrypts a plain text into a buffer.
     *
     * @param plain The plain text to encrypt.
     * @param plainlen Length of the plain text.
     * @param buf Buffer to take the cipher text..
     * @param buflen Length of the cipher text.
     * @return Number of bytes encrypted, 0 on failure.
     */
    unsigned rawencrypt(const byte* plain, int plainlen, byte* buf, int buflen);

    /**
     * @brief Decrypts a cipher text into a buffer.
     *
     * @param cipher The cipher text to encrypt.
     * @param cipherlen Length of the cipher text.
     * @param buf Buffer to take the plain text..
     * @param buflen Length of the plain text.
     * @return Always returns 1.
     */
    unsigned rawdecrypt(const byte* cipher, int cipherlen, byte* buf, int buflen);

    static void serializeintarray(CryptoPP::Integer*, int, string*, bool headers = true);

    /**
     * @brief Serialises a key to a string.
     *
     * @param d String to take the key.
     * @param keytype Key type indication by number of integers for key type
     *     (AsymmCipher::PRIVKEY or AsymmCipher::PUBKEY).
     * @return Void.
     */
    void serializekey(string* d, int keytype);

    /**
     * @brief Serialize public key for compatibility with the webclient.
     *
     * It also add padding (PUB_E size is forced to 4 bytes) in case the
     * of the key, at reception from server, indicates it has zero-padding.
     *
     * @param d String to take the serialized key without size-headers
     * @return Void.
     */
    void serializekeyforjs(string& d);

    /**
     * @brief Generates an RSA key pair of a given key size.
     *
     * @param privk Private key.
     * @param pubk Public key.
     * @param size Size of key to generate in bits (key strength).
     * @return Always returns 1.
     */
    void genkeypair(CryptoPP::Integer* privk, CryptoPP::Integer* pubk, int size);
};

class MEGA_API Hash
{
    CryptoPP::SHA512 hash;

public:
    void add(const byte*, unsigned);
    void get(string*);
};

class MEGA_API HashSHA256
{
    CryptoPP::SHA256 hash;

public:
    void add(const byte*, unsigned int);
    void get(string*);
};

class MEGA_API HashCRC32
{
    CryptoPP::CRC32 hash;

public:
    void add(const byte*, unsigned);
    void get(byte*);
};

/**
 * @brief HMAC-SHA256 generator
 */
class MEGA_API HMACSHA256
{
    CryptoPP::HMAC< CryptoPP::SHA256 > hmac;

public:
    /**
     * @brief Constructor
     * @param key HMAC key
     * @param length Key length
     */
    HMACSHA256(const byte *key, size_t length);

    /**
     * @brief Add data to the HMAC
     * @param data Data to add
     * @param len Data length
     */
    void add(const byte* data, unsigned len);

    /**
     * @brief Compute the HMAC for the current message
     * @param out The HMAC-SHA256 will be returned in the first 32 bytes of this buffer
     */
    void get(byte *out);
};

/**
 * @brief HMAC-SHA512 generator
 */
class MEGA_API PBKDF2_HMAC_SHA512
{
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;

public:
    PBKDF2_HMAC_SHA512();
    void deriveKey(byte* derivedkey, size_t derivedkeyLen,
                   byte* pwd, size_t pwdLen,
                   byte* salt, size_t saltLen, unsigned int iterations);
};

} // namespace

#endif
#endif
