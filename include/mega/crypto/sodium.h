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

#ifndef SODIUM_H
#define SODIUM_H 1

#include <sodium.h>
#include <vector>

namespace mega {

class PrnGen;

/**
 * @brief Asymmetric cryptographic signature using EdDSA with Edwards 25519.
 */
class MEGA_API EdDSA
{
public:
    static const int SEED_KEY_LENGTH = crypto_sign_SEEDBYTES;
    static const int PUBLIC_KEY_LENGTH = crypto_sign_PUBLICKEYBYTES;

    // TLV key to access to the corresponding value in the TLV records
    static const std::string TLV_KEY;
    bool initializationOK = false;

    EdDSA(PrnGen &rng, unsigned char* keySeed = NULL);
    ~EdDSA();

    unsigned char keySeed[SEED_KEY_LENGTH];
    unsigned char pubKey[PUBLIC_KEY_LENGTH];

    /**
     * @brief Computes the signature of a message.
     *
     * @param msg The message to sign.
     * @param msglen Length of the message.
     * @param sig Buffer to take the signature.
     * @return Number of bytes for signed message (msg length + signature),
     *     0 on failure.
     */
    int sign(const unsigned char* msg, const unsigned long long msglen,
             unsigned char* sig);

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

    void signKey(const unsigned char* key, const unsigned long long keyLength, std::string *sigBuf, uint64_t ts = 0);
    static bool verifyKey(const unsigned char* pubk, const unsigned long long pubkLen,
                   const std::string *sig, const unsigned char* singingPubKey);

private:
    static const int PRIVATE_KEY_LENGTH = crypto_sign_SECRETKEYBYTES;
    unsigned char privKey[PRIVATE_KEY_LENGTH]; // don't use it externally, use keySeed instead
};


/**
 * @brief Asymmetric cryptographic for chat messages encryptiong using
 * ECDH approach with x25519 key pair.
 */
class MEGA_API ECDH
{
public:
    static const int PRIVATE_KEY_LENGTH = crypto_box_SECRETKEYBYTES;
    static const int PUBLIC_KEY_LENGTH = crypto_box_PUBLICKEYBYTES;
    static const int DERIVED_KEY_LENGTH = crypto_scalarmult_BYTES;

    // TLV key to access to the corresponding value in the TLV records
    static const std::string TLV_KEY;
    bool initializationOK = false;

    ECDH(); // constructs an instance of ECDH and generates a new x25519 key pair
    ECDH(const std::string &privKey); // initialize the private key (and derive public key)
    ECDH(const unsigned char *privk, const std::string &pubk); // initialize the private key and public key
    ECDH(const ECDH& aux);
    ECDH* copy() const { return new ECDH(*this); }
    ECDH& operator=(const ECDH& aux) = delete;
    ECDH(ECDH&& aux) = delete;
    ECDH& operator=(ECDH&& aux) = delete;
    ~ECDH();

    const unsigned char* getPrivKey() const { return mPrivKey; }
    const unsigned char* getPubKey()  const { return mPubKey;  }
    bool deriveSharedKeyWithSalt(const unsigned char* pubkey, const unsigned char* salt, size_t saltSize, std::string& output) const;

    /**
     * @brief Compute symetric key using the stored private and public keys
     *
     * @param output Generated symetric key
     *
     * @return 1 on success, 0 on failure.
     */
    int computeSymmetricKey(std::string& output) const { return !doComputeSymmetricKey(mPrivKey, mPubKey, output); }

    /**
     * @brief encrypt Encrypt a message using the public key of recipient, the
     * private key of the sender and a nonce (number used once)
     *
     * @param encmsg Encrypted text after encryption. This function ensures that the
     * first crypto_box_ZEROBYTES bytes of the encrypted text msg are all 0.
     * @param msg Message to be encrypted. Caller must ensure that the first
     * crypto_box_ZEROBYTES bytes of the message msg are all 0.
     * @param msglen Lenght of the message to be encrypted.
     * @param nonce Number used once. The same nonce must never be used to encrypt another
     * packet from the sender's private key to this receiver's public key or viceversa.
     * @param pubKey Public key of the receiver.
     * @param privKey Private key of the sender.
     *
     * @return 1 on success, 0 on failure.
     */
    int encrypt(unsigned char* encmsg, const unsigned char* msg,
               const unsigned long long msglen, const unsigned char* nonce,
               const unsigned char* pubKey, const unsigned char* privKey);

    /**
     * @brief decrypt Decrypt a message using the public key of recipient, the
     * private key of the sender and a nonce (number used once)
     *
     * @param msg Message in plain text after decryption. This function ensures that
     * the first crypto_box_ZEROBYTES bytes of the message msg are all 0.
     * @param encmsg Encrypted text to be decrypted. Caller must ensure that the first
     * crypto_box_ZEROBYTES bytes of the chipered text encmsg are all 0.
     * @param encmsglen Length of the encrypted text.
     * @param nonce Number used once. The same nonce must never be used to encrypt another
     * packet from the sender's private key to this receiver's public key or viceversa.
     * @param pubKey Public key of the sender.
     * @param privKey Private key of the receiver.
     *
     * @return 1 on success, 0 on failure.
     */
    int decrypt(unsigned char* msg, const unsigned char* encmsg,
                 const unsigned long long encmsglen, const unsigned char* nonce,
                 const unsigned char* pubKey, const unsigned char* privKey);

private:
    int doComputeSymmetricKey(const unsigned char* privk, const unsigned char* pubk, std::string& output) const;
    unsigned char mPrivKey[PRIVATE_KEY_LENGTH];
    unsigned char mPubKey[PUBLIC_KEY_LENGTH];
};
} // namespace

#endif
