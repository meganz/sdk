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
    EdDSA();
    ~EdDSA();

    unsigned char* keySeed;
    unsigned char* privKey;

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
     * @param siglen Size of the buffer to take the signature.
     * @return Number of bytes for signed message (msg length + signature),
     *     0 on failure.
     */
    int sign(const unsigned char* msg, const unsigned long long msglen,
             unsigned char* sig, unsigned long long siglen);

    /**
     * @brief Verifies the signature of a message.
     *
     * @param msg Text of the message.
     * @param msglen Length of message.
     * @param sig Signature of the message
     * @param pubKey Public key to check the signature.
     * @return 1 on a valid signature, 0 on a failed verification.
     */
    static int verify(const unsigned char* msg, unsigned long long msglen,
                      const unsigned char* sig, const unsigned char* pubKey);

    /**
     * @brief Generates a new Ed25519 private key seed. The key seed is stored
     * in the object and, optionally, in the pointer passed as parameter (if not NULL)
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


/**
 * @brief Asymmetric cryptographic for chat messages encryptiong using
 * ECDH approach with x25519 key pair.
 */
class MEGA_API ECDH
{
public:

    ECDH();
    ~ECDH();

    /**
     *  @brief Initialise libsodium crypto system. Should be called only once.
     */
    static void init();

    /**
     * @brief genKeys Generate a new key pair
     *
     * @return 1 on success, 0 on failure.
     */
    int genKeys();


    /**
     * @brief setKeys Establish the key pair passed by parameter.
     *
     * @param pubKey
     * @param privKey
     */
    void setKeys(unsigned char *pubKey, unsigned char *privKey);

    /**
     * @brief encrypt Encrypt a message using the public key of recipient, the
     * private key of the sender and a nounce (number used once)
     *
     * @param encmsg Encrypted text after encryption. This function ensures that the
     * first crypto_box_ZEROBYTES bytes of the encrypted text msg are all 0.
     * @param msg Message to be encrypted. Caller must ensure that the first
     * crypto_box_ZEROBYTES bytes of the message msg are all 0.
     * @param msglen Lenght of the message to be encrypted.
     * @param nounce Number used once. The same nonce must never be used to encrypt another
     * packet from the sender's private key to this receiver's public key or viceversa.
     * @param pubKey Public key of the receiver.
     * @param privKey Private key of the sender.
     *
     * @return 1 on success, 0 on failure.
     */
    int encrypt(unsigned char* encmsg, const unsigned char* msg,
               const unsigned long long msglen, const unsigned char* nounce,
               const unsigned char* pubKey, const unsigned char* privKey);

    /**
     * @brief decrypt Decrypt a message using the public key of recipient, the
     * private key of the sender and a nounce (number used once)
     *
     * @param msg Message in plain text after decryption. This function ensures that
     * the first crypto_box_ZEROBYTES bytes of the message msg are all 0.
     * @param encmsg Encrypted text to be decrypted. Caller must ensure that the first
     * crypto_box_ZEROBYTES bytes of the chipered text encmsg are all 0.
     * @param encmsglen Length of the encrypted text.
     * @param nounce Number used once. The same nonce must never be used to encrypt another
     * packet from the sender's private key to this receiver's public key or viceversa.
     * @param pubKey Public key of the sender.
     * @param privKey Private key of the receiver.
     *
     * @return 1 on success, 0 on failure.
     */
    int decrypt(unsigned char* msg, const unsigned char* encmsg,
                 const unsigned long long encmsglen, const unsigned char* nounce,
                 const unsigned char* pubKey, const unsigned char* privKey);

    unsigned char * publicKey()     { return pubKey; }
    unsigned char * privateKey()    { return privKey; }

private:
    unsigned char* pubKey;
    unsigned char* privKey;
    bool keypairset;
};

} // namespace

#endif
#endif
