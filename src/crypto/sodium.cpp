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
#include "mega/secureBuffer.h"

#ifdef USE_SODIUM
namespace mega {

CryptoPP::AutoSeededRandomPool EdDSA::rng;

const int EdDSA::SODIUM_PK_BYTES = crypto_sign_ed25519_PUBLICKEYBYTES;

bool EdDSA::keySet()
{
    return (keySeed) ? true : false;
}

// Initialise libsodium crypto system.
void EdDSA::init()
{
    sodium_init();
}


// Sets a private key seed from a buffer.
void EdDSA::setKeySeed(SecureBuffer kSeed)
{
    this->keySeed = kSeed;
}


// Computes the signature of a message.
SecureBuffer EdDSA::sign(unsigned char* msg, unsigned long long msglen)
{
    std::pair<SecureBuffer, SecureBuffer> kPair = getKeyPair();

    unsigned long long bufferlen = 0;
    SecureBuffer sigBuffer(msglen + crypto_sign_ed25519_BYTES);
    if (sigBuffer.get() == NULL)
    {
        // Something went wrong allocating memory.

        return sigBuffer;
    }

    if(kPair.first.get() == nullptr || kPair.second.get() == nullptr)
    {
        kPair.first.free_buffer();
        kPair.second.free_buffer();
        sigBuffer.free_buffer();
        return 0;
    }

    int check = crypto_sign(sigBuffer.get(), &bufferlen, (const unsigned char*)msg, msglen,
                        (const unsigned char*)kPair.second.get());

    kPair.first.free_buffer();
    kPair.second.free_buffer();

    return sigBuffer;
}

SecureBuffer EdDSA::signDetatched(unsigned char *msg, unsigned long long msglen)
{
    SecureBuffer sig(crypto_sign_ed25519_BYTES);
    if(sig.get() == nullptr) {
        return sig;
    }

    std::pair<SecureBuffer, SecureBuffer> kPair = getKeyPair();
    if(kPair.first.get() == nullptr || kPair.second.get() == nullptr)
    {
        return sig;
    }

    unsigned long long int sigLen = 0;
    crypto_sign_ed25519_detached(sig.get(), &sigLen, msg, msglen,
            kPair.second.get());

    return sig;
}

bool EdDSA::verifyDetatched(unsigned char *sig, unsigned char *msg, unsigned long long mlen,
        unsigned char *pubKey)
{
    return (crypto_sign_ed25519_verify_detached(sig, msg, mlen, pubKey) == 0);
}

// Verifies the signature of a message.
int EdDSA::verify(const unsigned char* msg, unsigned long long msglen,
                  const unsigned char* sig, SecureBuffer publicKey)
{
    SecureBuffer signedmsg(msglen + crypto_sign_BYTES);
    if (signedmsg.get() == NULL)
    {
        // Something went wrong allocating the memory.
        return 0;
    }

    // Assemble signed message in one array.
    memcpy(signedmsg.get() + crypto_sign_ed25519_BYTES, msg, msglen);
    memcpy(signedmsg.get(), sig, crypto_sign_ed25519_BYTES);
    SecureBuffer result = verify(signedmsg.get(), signedmsg.size(), publicKey);
    signedmsg.free_buffer();

    return (result.get() != nullptr) ? 0 : 1;
}

SecureBuffer EdDSA::verify(const unsigned char *signedMsg, unsigned long long msglen,
        SecureBuffer publicKey)
{
    SecureBuffer msgbuffer(msglen - crypto_sign_ed25519_BYTES);
    if (msgbuffer.get() == NULL) {
       // Something went wrong allocating the memory.
       return SecureBuffer();
    }

    unsigned long long bufferlen = 0;
    int result = crypto_sign_open(msgbuffer.get(), &bufferlen, signedMsg,
            msglen, publicKey.get());

    return (result == 0) ? msgbuffer : SecureBuffer();

}

// Generates a new Ed25519 private key seed. The key seed is stored in the object.
SecureBuffer EdDSA::genKeySeed()
{
    // Make space for a new key seed (if not present).

    if (this->keySeed.get() == nullptr)
    {
        this->keySeed = SecureBuffer(crypto_sign_ed25519_SEEDBYTES);
        if (this->keySeed.get() == NULL)
        {
            LOG_err << "Could not allocate memory for keySeed";
            return SecureBuffer();
        }
    }
    // Now make the new key seed.
    this->rng.GenerateBlock(this->keySeed.get(), crypto_sign_ed25519_SEEDBYTES);

    return this->keySeed;
}

SecureBuffer EdDSA::genKeySeed(SecureBuffer secretKey)
{
    this->keySeed = SecureBuffer(crypto_sign_ed25519_SEEDBYTES);
    if(this->keySeed.get() == NULL)
    {
        LOG_err << "Error allocating memory for keySeed";
        return this->keySeed;
    }

    crypto_sign_ed25519_sk_to_seed(this->keySeed.get(), secretKey.get());
    return this->keySeed;
}

std::pair<SecureBuffer, SecureBuffer> EdDSA::getKeyPair()
{
    if(!keySeed)
    {
        LOG_err << "Key seed not set, cannot generate keys";
        return std::pair<SecureBuffer, SecureBuffer>(SecureBuffer(), SecureBuffer());
    }

    if(publicKey && privateKey) {
        return std::pair<SecureBuffer, SecureBuffer>(publicKey, privateKey);
    }

    publicKey = SecureBuffer(crypto_sign_ed25519_PUBLICKEYBYTES);
    if (publicKey.get() == NULL) {
        LOG_err << "Error allocating memory for public key bytes";
       // Something went wrong allocating the memory.
       return std::pair<SecureBuffer, SecureBuffer>(SecureBuffer(), SecureBuffer());
    }

    privateKey = SecureBuffer(crypto_sign_ed25519_SECRETKEYBYTES);
    if (privateKey.get() == NULL) {
       // Something went wrong allocating the memory.
        LOG_err << "Error allocating memory for private key bytes";
        publicKey.free_buffer();
        return std::pair<SecureBuffer, SecureBuffer>(SecureBuffer(), SecureBuffer());
    }
    int check = 0;
    check = crypto_sign_seed_keypair(publicKey.get(), privateKey.get(),
                                    (const unsigned char*)this->keySeed.get());
    if (check != 0) {
       // Something went wrong deriving keys.
        LOG_err << "Error deriving public key";
        publicKey.free_buffer();
        privateKey.free_buffer();

        return std::pair<SecureBuffer, SecureBuffer>(SecureBuffer(), SecureBuffer());
    }

    return std::pair<SecureBuffer, SecureBuffer>(publicKey, privateKey);
}

void EdDSA::setKeyPair(std::pair<SecureBuffer, SecureBuffer> pair)
{
	publicKey = pair.first;
	privateKey = pair.second;
}

} // namespace
#endif
