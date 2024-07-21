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
#include <cryptopp/hkdf.h>

namespace mega
{

const std::string EdDSA::TLV_KEY = "prEd255";

EdDSA::EdDSA(PrnGen &rng, unsigned char *keySeed)
{
    if (sodium_init() == -1)
    {
        LOG_err << "Cannot initialize sodium library.";
        return;
    }

    if (keySeed)    // then use the value
    {
        memcpy(this->keySeed, keySeed, EdDSA::SEED_KEY_LENGTH);
    }
    else    // make the new key seed.
    {
        rng.genblock(this->keySeed, EdDSA::SEED_KEY_LENGTH);
    }

    // derive public and private keys from the seed
    if (crypto_sign_seed_keypair(this->pubKey, this->privKey, this->keySeed) != 0)
    {
        LOG_err << "Error generating an Ed25519 key pair.";
        return;
    }

    initializationOK = true;
}

EdDSA::~EdDSA()
{
}

// Computes the signature of a message.
int EdDSA::sign(const unsigned char* msg, const unsigned long long msglen,
                unsigned char* sig)
{
    if (!sig || !msg)
    {
        return 0;
    }

    int result = crypto_sign_detached(sig, NULL, msg, msglen, (const unsigned char*)privKey);

    return int( (result == 0) ? (crypto_sign_BYTES + msglen) : 0 );
}


// Verifies the signature of a message.
int EdDSA::verify(const unsigned char* msg, unsigned long long msglen,
                  const unsigned char* sig, const unsigned char* pubKey)
{
    if (!sig || !msg)
    {
        return 0;
    }

    return !crypto_sign_verify_detached(sig, msg, msglen, pubKey);
}

void EdDSA::signKey(const unsigned char *key, const unsigned long long keyLength, string *result, uint64_t ts)
{
    if (!ts)
    {
        ts = (uint64_t) m_time();
    }

    string tsstr;
    unsigned char digit;
    for (int i = 0; i < 8; i++)
    {
        digit = ts & 0xFF;
        tsstr.insert(0, 1, digit);
        ts >>= 8;
    }

    string keyString = "keyauth";
    keyString.append(tsstr);
    keyString.append((char*)key, size_t(keyLength));

    byte sigBuf[crypto_sign_BYTES];
    sign((unsigned char *)keyString.data(), keyString.size(), sigBuf);

    result->resize(crypto_sign_BYTES + sizeof(ts));  // 8 --> timestamp prefix
    result->assign(tsstr.data(), sizeof(ts));
    result->append((const char*)sigBuf, crypto_sign_BYTES);
}

bool EdDSA::verifyKey(const unsigned char *pubk, const unsigned long long pubkLen, const string *sig, const unsigned char* signingPubKey)
{
    if (sig->size() < 72)
    {
        return false;
    }

    uint64_t ts;
    memcpy(&ts, sig->substr(0, 8).data(), sizeof(ts));

    string message = "keyauth";
    message.append(sig->data(), 8);
    message.append((char*)pubk, size_t(pubkLen));

    string signature = sig->substr(8);

    return verify((unsigned char*)message.data(),
                  message.length(),
                  (unsigned char*)signature.data(),
                  signingPubKey) != 0;
}

const std::string ECDH::TLV_KEY= "prCu255";

ECDH::ECDH()
{
    if (sodium_init() == -1)
    {
        LOG_err << "Cannot initialize sodium library.";
        return;
    }

    // create a new key pair
    crypto_box_keypair(mPubKey, mPrivKey);

    initializationOK = true;
}


ECDH::ECDH (const ECDH& aux)
{
    std::copy(aux.getPrivKey(), aux.getPrivKey() + ECDH::PRIVATE_KEY_LENGTH, mPrivKey);
    std::copy(aux.getPubKey(), aux.getPubKey() + ECDH::PUBLIC_KEY_LENGTH, mPubKey);
}

ECDH::ECDH(const string& privKey)
{
    if (sodium_init() == -1)
    {
        LOG_err << "Cannot initialize sodium library.";
        return;
    }

    if (privKey.size() != PRIVATE_KEY_LENGTH)
    {
        LOG_err << "Invalid size of private Cu25519 key";
        return;
    }

    memcpy(mPrivKey, privKey.data(), PRIVATE_KEY_LENGTH);

    // derive public key from privKey
    crypto_scalarmult_base(mPubKey, mPrivKey);

    initializationOK = true;
}

ECDH::~ECDH()
{
}

ECDH::ECDH(const unsigned char *privk, const std::string &pubk)
{
    assert(privk);
    if (!privk) return;

    std::copy(privk, privk + PRIVATE_KEY_LENGTH, mPrivKey);
    std::copy(pubk.data(), pubk.data() + PUBLIC_KEY_LENGTH, mPubKey);
}

int ECDH::doComputeSymmetricKey(const unsigned char* privk, const unsigned char* pubk, std::string& output) const
{
    assert(privk && pubk);
    if (!privk || !pubk) return -1; // return some non-0 value

    output.resize(DERIVED_KEY_LENGTH);
    unsigned char* outputPtr = reinterpret_cast<unsigned char*>(const_cast<char*>(output.data()));
    int ret = crypto_scalarmult(outputPtr, privk, pubk); // 0: success

    return ret;
}

int ECDH::encrypt(unsigned char *encmsg, const unsigned char *msg,
                  const unsigned long long msglen, const unsigned char *nonce,
                  const unsigned char *pubKey, const unsigned char *privKey)
{
   return !crypto_box(encmsg, msg, msglen, nonce, pubKey, privKey);
}

int ECDH::decrypt(unsigned char *msg, const unsigned char *encmsg,
                  const unsigned long long encmsglen, const unsigned char *nonce,
                  const unsigned char *pubKey, const unsigned char *privKey)
{
    return !crypto_box_open(msg, encmsg, encmsglen, nonce, pubKey, privKey);
}

bool ECDH::deriveSharedKeyWithSalt(const unsigned char* pubkey, const unsigned char* salt, size_t saltSize, std::string& output) const
{
    if (!pubkey || !salt || ! saltSize)
    {
        LOG_err << "derivePrivKeyWithSalt: eargs check input params";
        return false;
    }

    if (!getPrivKey())
    {
        LOG_err << "derivePrivKeyWithSalt: invalid private key";
        return false;
    }

    std::string sharedSecret;
    int err = doComputeSymmetricKey(mPrivKey, pubkey, sharedSecret);
    if (err)
    {
        LOG_err << "derivePrivKeyWithSalt: crypto_scalarmult err: " << err;
        return false;
    }

    try
    {
        output.resize(::mega::ECDH::DERIVED_KEY_LENGTH);
        auto outPtr = reinterpret_cast<const unsigned char *>(output.data());
        CryptoPP::HKDF<CryptoPP::SHA256> hkdf;
        hkdf.DeriveKey(const_cast<unsigned char*>(outPtr), output.size(), reinterpret_cast<const unsigned char*>(sharedSecret.data()), sharedSecret.size(), salt, saltSize, nullptr, 0);
        return true;
    }
    catch (std::invalid_argument const& e)
    {
        LOG_err << "derivePrivKeyWithSalt: Invalid argument err: " << e.what();
        return false;
    }
}
} // namespace

