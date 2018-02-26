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

#ifdef ENABLE_CHAT

namespace mega
{

const std::string EdDSA::TLV_KEY = "prEd255";

EdDSA::EdDSA(unsigned char *keySeed)
{
    initializationOK = false;

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
        PrnGen::genblock(this->keySeed, EdDSA::SEED_KEY_LENGTH);
    }

    // derive public and private keys from the seed
    if (crypto_sign_seed_keypair(this->pubKey, this->privKey, this->keySeed) != 0)
    {
        LOG_err << "Error generating an Ed25519 key pair.";
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

    return (result == 0) ? (crypto_sign_BYTES + msglen) : 0;
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

byte *EdDSA::genFingerprint(bool hexFormat)
{
    HashSHA256 hash;
    string binaryhash;
    hash.add((byte*)&pubKey, PUBLIC_KEY_LENGTH);
    hash.get(&binaryhash);

    size_t size = hexFormat ? 40 : 20;

    byte *result = new byte[size];
    memcpy(result, binaryhash.substr(0, size).data(), size);

    return result;
}

char *EdDSA::genFingerprintHex()
{
    byte *fp = genFingerprint(true);

    static const char hexchars[] = "0123456789ABCDEF";
    ostringstream oss;
    string fpHex;
    for (size_t i = 0; i < 40; ++i)
    {
        oss.put(hexchars[(fp[i] >> 4) & 0x0F]);
        oss.put(hexchars[fp[i] & 0x0F]);
    }
    fpHex = oss.str();

    delete fp;

    char *result = new char[40 + 1];
    strncpy(result, fpHex.c_str(), 40);
    result[40] = '\0';

    return result;
}

void EdDSA::signKey(const unsigned char *key, const unsigned long long keyLength, string *result, uint64_t ts)
{
    if (!ts)
    {
        ts = (uint64_t) time(NULL);
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
    keyString.append((char*)key, keyLength);

    byte sigBuf[crypto_sign_BYTES];
    sign((unsigned char *)keyString.data(), keyString.size(), sigBuf);

    result->resize(crypto_sign_BYTES + sizeof(ts));  // 8 --> timestamp prefix
    result->assign(tsstr.data(), sizeof(ts));
    result->append((const char*)sigBuf, crypto_sign_BYTES);
}

bool EdDSA::verifyKey(const unsigned char *pubk, const unsigned long long pubkLen, string *sig, const unsigned char* signingPubKey)
{
    if (sig->size() < 72)
    {
        return false;
    }

    uint64_t ts;
    memcpy(&ts, sig->substr(0, 8).data(), sizeof(ts));

    string message = "keyauth";
    message.append(sig->data(), 8);
    message.append((char*)pubk, pubkLen);

    string signature = sig->substr(8);

    return verify((unsigned char*) message.data(), message.length(),
                  (unsigned char*) signature.data(),
                  signingPubKey ? signingPubKey : pubKey);
}

const std::string ECDH::TLV_KEY= "prCu255";

ECDH::ECDH(unsigned char *privKey)
{
    initializationOK = false;

    if (sodium_init() == -1)
    {
        LOG_err << "Cannot initialize sodium library.";
        return;
    }

    if (privKey)    // then use the value
    {
        memcpy(this->privKey, privKey, PRIVATE_KEY_LENGTH);

        // derive public key from privKey
        crypto_scalarmult_base(this->pubKey, this->privKey);
    }
    else
    {
        // no private key specified: create a new key pair
        crypto_box_keypair(this->pubKey, this->privKey);
    }

    initializationOK = true;
}

ECDH::~ECDH()
{
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

} // namespace

#endif  // ENABLE_CHAT
