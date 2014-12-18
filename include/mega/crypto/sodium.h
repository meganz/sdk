/**
 * @file sodium.h
 * @brief Crypto layer using libsodium.
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

#ifdef USE_SODIUM
#ifndef SODIUM_H
#define SODIUM_H 1

#include <sodium.h>

namespace mega {
using namespace std;

/**
 * @brief Asymmetric cryptographic signature using EdDSA with Edwards 25519.
 */
class MEGA_API EdDSA
{
public:
    EdDSA() : keySeed(NULL) {}

    unsigned char* keySeed;

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
    static int verify(const unsigned char* msg, unsigned long long msglen,
                      const unsigned char* sig, const unsigned char* pubKey);

    /**
     * @brief Generates a new Ed25519 private key seed. The key seed is stored
     * in the object.
     *
     * @param privk Private key seed to return, unless NULL.
     * @return 1 on success, 0 on failure.
     */
    int genKeySeed(unsigned char* privKey = NULL);

    /**
     * @brief Derives the Ed25519 public key from the stored private key seed.
     *
     * @param pubKey Public key.
     * @return 1 on success, 0 on failure.
     */
    int publicKey(unsigned char* pubKey);
};


} // namespace

#endif
#endif
