/**
 * @file sodium.cpp
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

#include "mega.h"

namespace mega {

CryptoPP::AutoSeededRandomPool EdDSA::rng;

// Initialise libsodium crypto system.
void EdDSA::init() {
    sodium_init();
}


// Sets a private key seed from a buffer.
void EdDSA::setKeySeed(const char* data) {
    memcpy(this->keySeed, data, crypto_sign_SEEDBYTES);
}


// Computes the signature of a message.
int EdDSA::sign(unsigned char* msg, unsigned long long msglen, char* sig) {
    unsigned char* pubKey = (unsigned char*)malloc(crypto_sign_PUBLICKEYBYTES);
    if (pubKey == NULL) {
        // Something went wrong allocating the memory.
        return(0);
    }
    unsigned char* privKey = (unsigned char*)malloc(crypto_sign_SECRETKEYBYTES);
    if (privKey == NULL) {
        // Something went wrong allocating the memory.
        free(pubKey);
        return(0);
    }
    int check = 0;
    check = crypto_sign_seed_keypair(pubKey, privKey,
                                     (const unsigned char*)this->keySeed);
    if (check != 0) {
        // Something went wrong deriving keys.
        return(0);
    }
    unsigned long long bufferlen = 0;
    unsigned char* sigbuffer = (unsigned char*)malloc(msglen + crypto_sign_BYTES);
    if (sigbuffer == NULL) {
        // Something went wrong allocating the memory.
        free(pubKey);
        free(privKey);
        return(0);
    }
    check = crypto_sign(sigbuffer, &bufferlen, (const unsigned char*)msg, msglen,
                        (const unsigned char*)privKey);
    if (check != 0) {
        // Something went wrong signing the message.
        free(sigbuffer);
        free(pubKey);
        free(privKey);
        return(0);
    }
    free(sigbuffer);
    free(pubKey);
    free(privKey);
    return(bufferlen);
}


// Verifies the signature of a message.
int EdDSA::verify(const unsigned char* msg, unsigned long long msglen,
                  const unsigned char* sig, const unsigned char* pubKey) {
    unsigned char* msgbuffer = (unsigned char*)malloc(msglen + crypto_sign_BYTES);
    if (msgbuffer == NULL) {
        // Something went wrong allocating the memory.
        return(0);
    }
    unsigned char* signedmsg = (unsigned char*)malloc(msglen + crypto_sign_BYTES);
    if (signedmsg == NULL) {
        // Something went wrong allocating the memory.
        free(msgbuffer);
        return(0);
    }

    // Assemble signed message in one array.
    memcpy(signedmsg, msg, msglen);
    memcpy(signedmsg + msglen, sig, crypto_sign_BYTES);

    unsigned long long bufferlen = 0;
    int result = crypto_sign_open(msgbuffer, &bufferlen,
                                  signedmsg, msglen + crypto_sign_BYTES, pubKey);
    free(msgbuffer);
    free(signedmsg);
    if (result) {
        return(1);
    } else {
        // crypto_sign_open() returns -1 on failure.
        return(0);
    }
}


// Generates a new Ed25519 private key seed. The key seed is stored in the object.
int EdDSA::genKeySeed(unsigned char* privKey) {
    // Make space for a new key seed (if not present).
    if (!this->keySeed) {
        this->keySeed = (unsigned char*)malloc(crypto_sign_SEEDBYTES);
        if (this->keySeed == NULL) {
            // Something went wrong allocating the memory.
            return(0);
        }
    }
    // Now make the new key seed.
    this->rng.GenerateBlock(this->keySeed, crypto_sign_SEEDBYTES);
    // Copy it to privKey before returning.
    if (privKey)
    {
        memcpy(privKey, this->keySeed, crypto_sign_SEEDBYTES);
    }
    return(1);
}


// Derives the Ed25519 public key from the stored private key seed.
int EdDSA::publicKey(unsigned char* pubKey) {
    unsigned char* privKey = (unsigned char*)malloc(crypto_sign_SECRETKEYBYTES);
    if (privKey == NULL) {
        // Something went wrong allocating the memory.
        return(0);
    }
    int check = crypto_sign_seed_keypair(pubKey, privKey,
                                         (const unsigned char*)this->keySeed);
    if (check != 0) {
        // Something went wrong deriving keys.
        return(0);
    }
    free(privKey);
    return(1);
}

} // namespace
#endif
