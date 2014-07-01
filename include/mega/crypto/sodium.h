/**
 * @file sodium.h
 * @brief Crypto layer using libsodium.
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

#ifdef USE_SODIUM
#ifndef SODIUM_H
#define SODIUM_H 1

#include <sodium/core.h>
#include <sodium/crypto_sign.h>

namespace mega {
using namespace std;

/**
 * @brief Asymmetric cryptographic signature using EdDSA with Edwards 25519.
 */
class MEGA_API EdDSA
{
public:
    char* keySeed;

    /**
     *  @brief Initialise libsodium crypto system. Should be called only once.
     */
    static void init();

    /**
     * @brief Sets a private key seed from a buffer.
     *
     * @param data Buffer containing the key bytes.
     * @return Void.
     */
    void setKeySeed(const char* data);

    /**
     * @brief Computes the signature of a message.
     *
     * @param msg The message to sign.
     * @param msglen Length of the message.
     * @param sig Buffer to take the signature.
     * @return Number of bytes for signed message (msg length + signature),
     *     0 on failure.
     */
    int sign(unsigned char* msg, unsigned long long msglen, char* sig);

    /**
     * @brief Verifies the signature of a message.
     *
     * @param cipher The cipher text to encrypt.
     * @param cipherlen Length of the cipher text.
     * @param buf Buffer to take the plain text..
     * @param buflen Length of the plain text.
     * @return 1 on a valid signature, 0 on a failed verification.
     */
    int verify(const unsigned char* msg, unsigned long long msglen,
               const unsigned char* sig, const unsigned char* pubKey);

    /**
     * @brief Generates an Ed25519 key pair of a given key size.
     *
     * @param privk Private key seed.
     * @param pubk Public key.
     * @return Always returns 1.
     */
    void genKeyPair(char* privk, char* pubk);
};


} // namespace

#endif
#endif
