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

const std::string EdDSA::TLV_KEY = "prEd255";

EdDSA::EdDSA(unsigned char *keySeed)
{
    this->privKey = NULL;
    this->pubKey = NULL;
    this->keySeed = NULL;

    if (sodium_init() == -1)
    {
        LOG_err << "Cannot initialize sodium library.";
        return;
    }

    this->keySeed = (unsigned char*) malloc(EdDSA::SEED_KEY_LENGTH);
    if (this->keySeed == NULL)
    {
        LOG_err << "Cannot allocate memory for Ed25519 key seed.";
        return;
    }

    if (keySeed)    // then use the value
    {
        memcpy(this->keySeed, keySeed, EdDSA::SEED_KEY_LENGTH);
    }
    else    // make the new key seed.
    {
        PrnGen::genblock(this->keySeed, EdDSA::SEED_KEY_LENGTH);
    }

    // derive public and private keys from the seed
    if (!genKeys())
    {
        LOG_err << "Error generating an Ed25519 key pair.";
    }
}

EdDSA::~EdDSA()
{
    free(pubKey);
    free(privKey);
    free(keySeed);
}

// Computes the signature of a message.
int EdDSA::sign(const unsigned char* msg, const unsigned long long msglen,
                unsigned char* sig, unsigned long long siglen)
{
    if (!sig || siglen != msglen + crypto_sign_BYTES)
    {
        // Wrong allocated space for signature
        return 0;
    }

    // Generate the key pair from the keySeed
    if (!privKey && !genKeys())
    {
        return 0;
    }

    // Sign the message 'm' with the private key and store it in 'sm'
    unsigned long long len = 0;
    int check = crypto_sign(sig, &len, msg, msglen, (const unsigned char*)privKey);

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


// Derives the Ed25519 private and public keys from the stored private key seed.
int EdDSA::genKeys()
{
    int check = 1;  // error code != 0

    if (!privKey)
    {
        privKey = (unsigned char*)malloc(PRIVATE_KEY_LENGTH);
        if (privKey == NULL)
        {
            // Something went wrong allocating the memory.
            return 0;
        }
    }

    if (!pubKey)
    {
        pubKey = (unsigned char*)malloc(PUBLIC_KEY_LENGTH);
        if (pubKey == NULL)
        {
            free(privKey);
            // Something went wrong allocating the memory.
            return 0;
        }
    }

    check = crypto_sign_seed_keypair(pubKey, privKey, (const unsigned char*) keySeed);

    // crypto_sign_seed_keypair() returns 0 on success
    return check ? 0 : 1;
}

const std::string ECDH::TLV_KEY= "prCu255";

ECDH::ECDH(unsigned char *privKey)
{
    if (sodium_init() == -1)
    {
        LOG_err << "Cannot initialize sodium library.";
        return;
    }

    this->privKey = NULL;
    this->pubKey = NULL;

    if (privKey)    // then use the value
    {
        this->privKey = (unsigned char*)malloc(PRIVATE_KEY_LENGTH);
        if (this->privKey == NULL)
        {
            LOG_err << "Cannot allocate memory for x25519 private key.";
            return;
        }

        memcpy(this->privKey, privKey, PRIVATE_KEY_LENGTH);

        // derive public key from privKey
        this->pubKey = publicKey();
    }
    else
    {
        // no private key specified: create a new key pair
        if (!genKeys())
        {
            LOG_err << "Error generating an x25519 key pair.";
        }
    }
}

ECDH::~ECDH()
{
    free(pubKey);
    free(privKey);
}

int ECDH::genKeys()
{
    pubKey = (unsigned char*)malloc(PUBLIC_KEY_LENGTH);
    privKey = (unsigned char*)malloc(PRIVATE_KEY_LENGTH);

    int check = crypto_box_keypair(pubKey, privKey);

    if (check != 0)
    {
        free (pubKey);
        free (privKey);
        pubKey = NULL;
        privKey = NULL;
        return 0;
    }

    return 1;
}

unsigned char * ECDH::publicKey()
{
    if (!pubKey)
    {
        if (privKey)
        {
            // derive pubKey from privKey
            pubKey = (unsigned char*)malloc(PRIVATE_KEY_LENGTH);
            if (crypto_scalarmult_base(pubKey, privKey))
            {
                free(pubKey);
                pubKey = NULL;
            }
        }
        else
        {
            LOG_warn << "Trying to get public key without private key.";
        }
    }

    return pubKey;
}

int ECDH::encrypt(unsigned char *encmsg, const unsigned char *msg,
                  const unsigned long long msglen, const unsigned char *nonce,
                  const unsigned char *pubKey, const unsigned char *privKey)
{
    int check = crypto_box(encmsg, msg, msglen, nonce, pubKey, privKey);

    return check ? 0 : 1;
}

int ECDH::decrypt(unsigned char *msg, const unsigned char *encmsg,
                  const unsigned long long encmsglen, const unsigned char *nonce,
                  const unsigned char *pubKey, const unsigned char *privKey)
{
    int check = crypto_box_open(msg, encmsg, encmsglen, nonce, pubKey, privKey);

    return check ? 0 : 1;
}

} // namespace
#endif
