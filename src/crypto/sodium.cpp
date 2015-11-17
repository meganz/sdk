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

EdDSA::EdDSA()
{
    keySeed = NULL;
    privKey = NULL;
}

EdDSA::~EdDSA()
{
    free(privKey);
    free(keySeed);
}

// Initialise libsodium crypto system.
void EdDSA::init()
{
    sodium_init();
}


// Sets a private key seed from a buffer.
void EdDSA::setKeySeed(const char* data)
{
    // Make space for a key seed (if not present).
    if (!this->keySeed)
    {
        this->keySeed = (unsigned char*)malloc(crypto_sign_SEEDBYTES);
        if (this->keySeed == NULL)
        {
            // Something went wrong allocating the memory.
            return;
        }
    }

    memcpy(this->keySeed, data, crypto_sign_SEEDBYTES);
}


// Computes the signature of a message.
int EdDSA::sign(const unsigned char* msg, const unsigned long long msglen,
                unsigned char* sig, unsigned long long siglen)
{
    int check;

    if (!sig || siglen != msglen + crypto_sign_BYTES)
    {
        // Wrong allocated space for signature
        return 0;
    }

    // Allocate memory for public key
    unsigned char* pubKey = (unsigned char*)malloc(crypto_sign_PUBLICKEYBYTES);
    if (pubKey == NULL)
    {
        // Something went wrong allocating the memory.
        return 0;
    }

    // Generate the key pair from the keySeed
    if (!privKey)
    {
        privKey = (unsigned char*)malloc(crypto_sign_SECRETKEYBYTES);
        if (privKey == NULL)
        {
            // Something went wrong allocating the memory.
            free(pubKey);
            return 0;
        }

        check = crypto_sign_seed_keypair(pubKey, privKey, (const unsigned char*)this->keySeed);
        if (check != 0)
        {
            // Something went wrong deriving keys.
            free(pubKey);
            free(privKey);
            privKey = NULL;
            return 0;
        }
    }

    // Sign the message 'm' with the private key and store it in 'sm'
    unsigned long long len = 0;
    check = crypto_sign(sig, &len, msg, msglen, (const unsigned char*)privKey);

    free(pubKey);

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
int EdDSA::genKeySeed(unsigned char* keySeed)
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
    PrnGen::genblock(this->keySeed, crypto_sign_SEEDBYTES);

    // Copy it to privKey before returning.
    if (keySeed)
    {
        memcpy(keySeed, this->keySeed, crypto_sign_SEEDBYTES);
    }

    return 1;
}


// Derives the Ed25519 public key from the stored private key seed.
int EdDSA::publicKey(unsigned char* pubKey)
{
    int check = 1;  // error code != 0

    if (!privKey)
    {
        privKey = (unsigned char*)malloc(crypto_sign_SECRETKEYBYTES);
        if (privKey == NULL)
        {
            // Something went wrong allocating the memory.
            return 0;
        }

        check = crypto_sign_seed_keypair(pubKey, privKey,
                                             (const unsigned char*)this->keySeed);
    }
    else
    {
        check = crypto_sign_ed25519_sk_to_pk(pubKey, privKey);
    }

    // crypto_sign_seed_keypair() returns 0 on success
    return check ? 0 : 1;
}

ECDH::ECDH()
{
    pubKey = NULL;
    privKey = NULL;
    keypairset = false;
}

ECDH::~ECDH()
{
    if (keypairset)
    {
        free(pubKey);
        free(privKey);
    }
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

int ECDH::encrypt(unsigned char *encmsg, const unsigned char *msg,
                  const unsigned long long msglen, const unsigned char *nounce,
                  const unsigned char *pubKey, const unsigned char *privKey)
{
    int check = crypto_box(encmsg, msg, msglen, nounce, pubKey, privKey);

    return check ? 0 : 1;
}

int ECDH::decrypt(unsigned char *msg, const unsigned char *encmsg,
                  const unsigned long long encmsglen, const unsigned char *nounce,
                  const unsigned char *pubKey, const unsigned char *privKey)
{
    int check = crypto_box_open(msg, encmsg, encmsglen, nounce, pubKey, privKey);

    return check ? 0 : 1;
}

} // namespace
#endif
