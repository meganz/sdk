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


#include "mega.h"

#ifdef USE_SODIUM
namespace mega
{

CryptoPP::AutoSeededRandomPool EdDSA::rng;

// Initialise libsodium crypto system.
void EdDSA::init()
{
    sodium_init();
}


// Sets a private key seed from a buffer.
void EdDSA::setKeySeed(const char* data)
{
    memcpy(this->keySeed, data, crypto_sign_SEEDBYTES);
}


// Computes the signature of a message.
int EdDSA::sign(const unsigned char* m, const unsigned long long mlen,
                unsigned char* sm, unsigned long long smlen)
{
    if (!sm || smlen != mlen + crypto_sign_BYTES)
    {
        // Wrong allocated space for signature
        return 0;
    }

    // Allocate memory for key pair
    unsigned char* pubKey = (unsigned char*)malloc(crypto_sign_PUBLICKEYBYTES);
    if (pubKey == NULL)
    {
        // Something went wrong allocating the memory.
        return 0;
    }
    unsigned char* privKey = (unsigned char*)malloc(crypto_sign_SECRETKEYBYTES);
    if (privKey == NULL) {
        // Something went wrong allocating the memory.
        free(pubKey);
        return 0;
    }

    // Generate the key pair from the keySeed
    int check = crypto_sign_seed_keypair(pubKey, privKey, (const unsigned char*)this->keySeed);
    if (check != 0)
    {
        // Something went wrong deriving keys.
        free(pubKey);
        free(privKey);
        return 0;
    }

    // Sign the message 'm' with the private key and store it in 'sm'
    unsigned long long len = 0;
    check = crypto_sign(sm, &len, m, mlen, (const unsigned char*)privKey);

    free(pubKey);
    free(privKey);

    // crypto_sign() returns 0 on success
    return check ? 0 : len;
}


// Verifies the signature of a message.
int EdDSA::verify(const unsigned char* msg, unsigned long long msglen,
                  const unsigned char* sig, const unsigned char* pubKey)
{
    unsigned char* msgbuffer = (unsigned char*)malloc(msglen + crypto_sign_BYTES);
    if (msgbuffer == NULL)
    {
        // Something went wrong allocating the memory.
        return 0;
    }
    unsigned char* signedmsg = (unsigned char*)malloc(msglen + crypto_sign_BYTES);
    if (signedmsg == NULL)
    {
        // Something went wrong allocating the memory.
        free(msgbuffer);
        return 0;
    }

    // Assemble signed message in one array.
    memcpy(signedmsg, msg, msglen);
    memcpy(signedmsg + msglen, sig, crypto_sign_BYTES);

    unsigned long long bufferlen = 0;
    int result = crypto_sign_open(msgbuffer, &bufferlen,
                                  signedmsg, msglen + crypto_sign_BYTES, pubKey);
    free(msgbuffer);
    free(signedmsg);

    // crypto_sign_open() returns 0 on success.
    return result ? 1 : 0;
}


// Generates a new Ed25519 private key seed. The key seed is stored in the object.
int EdDSA::genKeySeed(unsigned char* privKey)
{
    // Make space for a new key seed (if not present).
    if (!this->keySeed)
    {
        this->keySeed = (unsigned char*)malloc(crypto_sign_SEEDBYTES);
        if (this->keySeed == NULL)
        {
            // Something went wrong allocating the memory.
            return 0;
        }
    }

    // Now make the new key seed.
    this->rng.GenerateBlock(this->keySeed, crypto_sign_SEEDBYTES);

    // Copy it to privKey before returning.
    if (privKey)
    {
        memcpy(privKey, this->keySeed, crypto_sign_SEEDBYTES);
    }

    return 1;
}


// Derives the Ed25519 public key from the stored private key seed.
int EdDSA::publicKey(unsigned char* pubKey)
{
    unsigned char* privKey = (unsigned char*)malloc(crypto_sign_SECRETKEYBYTES);
    if (privKey == NULL)
    {
        // Something went wrong allocating the memory.
        return 0;
    }

    int check = crypto_sign_seed_keypair(pubKey, privKey,
                                         (const unsigned char*)this->keySeed);

    free(privKey);

    // crypto_sign_seed_keypair() returns 0 on success
    return check ? 0 : 1;
}

ECDH::ECDH()
{
    pubKey = NULL;
    privKey = NULL;
    keypairset = false;
}

void ECDH::init()
{
    sodium_init();
}

int ECDH::genKeys()
{
    if (keypairset)
    {
        LOG_warn << "Regenerating chat key pair";
    }
    else
    {
        pubKey = (unsigned char*)malloc(crypto_box_PUBLICKEYBYTES);
        privKey = (unsigned char*)malloc(crypto_box_SECRETKEYBYTES);
    }

    int check = crypto_box_keypair(pubKey,privKey);

    if (check == 0)
    {
        keypairset = true;
        return 1;
    }
    else
    {
        free (pubKey);
        free (privKey);
        keypairset = false;
        return 0;
    }
}

void ECDH::setKeys(unsigned char *pubKey, unsigned char *privKey)
{
    if (keypairset)
    {
        LOG_warn << "Setting a new chat key pair, but it already exists";
    }
    else
    {
        this->pubKey = (unsigned char*)malloc(crypto_box_PUBLICKEYBYTES);
        this->privKey = (unsigned char*)malloc(crypto_box_SECRETKEYBYTES);
    }

    memcpy(this->pubKey, pubKey, crypto_box_PUBLICKEYBYTES);
    memcpy(this->privKey, privKey, crypto_box_SECRETKEYBYTES);

    keypairset = true;
}

int ECDH::cipher(unsigned char *c, const unsigned char *m,
                 const unsigned long long mlen, const unsigned char *n,
                 const unsigned char *pubKey, const unsigned char *privKey)
{
    int check = crypto_box(c, m, mlen, n, pubKey, privKey);

    return check ? 0 : 1;
}

int ECDH::uncipher(unsigned char* m, const unsigned char* c,
                   const unsigned long long clen, const unsigned char* n,
                   const unsigned char* pubKey, const unsigned char* privKey)
{
    int check = crypto_box_open(m, c, clen, n, pubKey, privKey);

    return check ? 0 : 1;
}

} // namespace
#endif
