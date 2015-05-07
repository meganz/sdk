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
#include <utility>

#include "mega/secureBuffer.h"

namespace mega {
using namespace std;

/**
 * @brief Asymmetric cryptographic signature using EdDSA with Edwards 25519.
 */
class MEGA_API EdDSA
{
public:

    static const int SODIUM_PK_BYTES;

    static CryptoPP::AutoSeededRandomPool rng;

    /**
     * @brief No-arg constructor for EdDSA;
     */
    EdDSA() {}

    /**
     * @brief Destructor for EdDSA.
     *
     * Deletes keySeed if it exists.
     *
     */
    virtual ~EdDSA() {
        if(keySeed) keySeed.free_buffer();
        if(publicKey) publicKey.free_buffer();
        if(privateKey) privateKey.free_buffer();
    }

    /**
     * @brief The keyseed for this key pair.
     */
    SecureBuffer keySeed;

    /**
     * @brief The public key.
     */
    SecureBuffer publicKey;

    /**
     * @brief The private key.
     */
    SecureBuffer privateKey;

    /**
     * @brief Check to see if the keySeed has been generated/set.
     *
     * @return true if the keySeed is set, false otherwise.
     */
    bool keySet();

    /**
     *  @brief Initialize libsodium crypto system. Only needs to be called once.
     */
    static void init();

    /**
     * @brief Sets the seed for the keypair from a buffer.
     *
     * @param kSeed Buffer containing the seed for this key pair.
     */
    void setKeySeed(SecureBuffer kSeed);

    /**
     * @brief Computes the signature of a message.
     *
     * @param msg The message to sign.
     * @param msglen Length of the message.
     * @param sig Buffer to take the signature.
     * @return Number of bytes for signed message (msg length + signature),
     *     0 on failure.
     */
    SecureBuffer sign(unsigned char* msg, unsigned long long msglen);

    /**
     * @brief Sign the given bytes without prepending the signature to the message.
     *
     * @param msg The message to sign.
     * @param msglen The length of the message.
     * @return A buffer with the signature.
     */
    SecureBuffer signDetatched(unsigned char *msg, unsigned long long msglen);

    /**
     * @brief Verify the signature of the given message with the signature not
     * prepended.
     *
     * @param sig The signature of the message.
     * @param msg The message to verify.
     * @param msglen The length of the message.
     * @param pKey The public signing key to use for the verification.
     * @return True if the signature is verified.
     */
    static bool verifyDetatched(unsigned char *sig, unsigned char *msg,
            unsigned long long msglen, unsigned char *pKey);

    /**
     * @brief Verifies the signature of a message.
     *
     * @param msg The message to verify.
     * @param msglen The length of the message.
     * @param sig The signature to verify against.
     * @param publicKey The public signing key to verify with.
     * @return 1 on a valid signature, 0 on a failed verification.
     */
    static int verify(const unsigned char* msg,
            unsigned long long msglen, const unsigned char* sig,
            SecureBuffer publicKey);

    /**
     * @brief Verify a message with message appended.
     *
     * @param signedMessage The message with appended signature.
     * @param msglen The length of the signed message.
     * @param publicKey The public key to verify against.
     *
     * @return 0 on success, 1 on failure.
     */
    static SecureBuffer verify(const unsigned char *signedMessage, unsigned long long msglen,
            SecureBuffer publicKey);

    /**
     * @brief Generates a new Ed25519 private key seed. The key seed is stored
     * in the object.
     *
     * @return A SecureBuffer containing the key seed on success, or
     * a pointer to null on failure.
     */
    SecureBuffer genKeySeed();

    /**
     * @brief Generates an Ed25519 private key seed from the provided secret key.
     *
     * @param secretKey The secret key to derive the seed from.
     * @return The derived seed.
     */
    SecureBuffer genKeySeed(SecureBuffer secretKey);

    /**
     * @brief Get the key pair for this key.
     *
     * @return The key pair for the contained key seed.
     */
    std::pair<SecureBuffer, SecureBuffer> getKeyPair();

    /**
     * @brief Set the key pair.
     *
     * @param pair The key pair to set.
     */
    void setKeyPair(std::pair<SecureBuffer, SecureBuffer> pair);
};


} // namespace

#endif
#endif
