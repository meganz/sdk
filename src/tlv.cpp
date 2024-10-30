#include "mega/tlv.h"

#include "mega/logging.h"
#include "mega/types.h"

using namespace std;

namespace mega
{

unique_ptr<string> TLVstore::recordsToContainer(TLV_map&& records,
                                                PrnGen& rng,
                                                SymmCipher& key)
{
    TLVstore tlv;
    tlv.set(std::move(records));
    return unique_ptr<string>{tlv.tlvRecordsToContainer(rng, &key)};
}


string* TLVstore::tlvRecordsToContainer(PrnGen& rng,
                                        SymmCipher* key,
                                        encryptionsetting_t encSetting)
{
    // decide nonce/IV and auth. tag lengths based on the `mode`
    unsigned ivlen = TLVstore::getIvlen(encSetting);
    unsigned taglen = TLVstore::getTaglen(encSetting);
    encryptionmode_t encMode = TLVstore::getMode(encSetting);

    if (!ivlen || !taglen || encMode == AES_MODE_UNKNOWN)
    {
        return NULL;
    }

    // serialize the TLV records
    std::unique_ptr<string> container(tlvRecordsToContainer());

    // generate IV array
    std::vector<byte> iv(ivlen);
    rng.genblock(iv.data(), ivlen);

    string cipherText;

    // encrypt the bytes using the specified mode

    if (encMode == AES_MODE_CCM) // CCM or GCM_BROKEN (same than CCM)
    {
        if (!key->ccm_encrypt(container.get(), iv.data(), ivlen, taglen, &cipherText))
        {
            return nullptr;
        }
    }
    else if (encMode == AES_MODE_GCM) // then use GCM
    {
        if (!key->gcm_encrypt(container.get(), iv.data(), ivlen, taglen, &cipherText))
        {
            return nullptr;
        }
    }

    string* result = new string;
    result->resize(1);
    result->at(0) = static_cast<char>(encSetting);
    result->append((char*)iv.data(), ivlen);
    result->append((char*)cipherText.data(), cipherText.length()); // includes auth. tag

    return result;
}

string* TLVstore::tlvRecordsToContainer()
{
    string* result = new string;
    size_t offset = 0;
    size_t length;

    for (TLV_map::iterator it = tlv.begin(); it != tlv.end(); it++)
    {
        // copy Type
        result->append(it->first);
        offset += it->first.length() + 1; // keep the NULL-char for Type string

        // set Length of value
        length = it->second.length();
        if (length > 0xFFFF)
        {
            assert(it->first == "" && tlv.size() == 1 &&
                   "Only records with a single empty key can be larger");
            LOG_warn << "Overflow of Length for TLV record: " << length;
            length = 0xFFFF;
        }
        result->resize(offset + 2);
        result->at(offset) = static_cast<char>(length >> 8);
        result->at(offset + 1) = static_cast<char>(length & 0xFF);
        offset += 2;

        // copy the Value
        result->append((char*)it->second.data(), it->second.length());
        offset += it->second.length();
    }

    return result;
}

bool TLVstore::get(const string& type, string& value) const
{
    auto it = tlv.find(type);
    if (it == tlv.cend())
        return false;

    value = it->second;
    return true;
}

const TLV_map* TLVstore::getMap() const
{
    return &tlv;
}

vector<string>* TLVstore::getKeys() const
{
    vector<string>* keys = new vector<string>;
    for (string_map::const_iterator it = tlv.begin(); it != tlv.end(); it++)
    {
        keys->push_back(it->first);
    }
    return keys;
}

void TLVstore::set(const string& type, const string& value)
{
    tlv[type] = value;
}

void TLVstore::set(TLV_map&& records)
{
    tlv.swap(records);
}

void TLVstore::reset(const string& type)
{
    tlv.erase(type);
}

size_t TLVstore::size() const
{
    return tlv.size();
}

unsigned TLVstore::getTaglen(int mode)
{
    switch (mode)
    {
        case AES_CCM_10_16:
        case AES_CCM_12_16:
        case AES_GCM_12_16_BROKEN:
        case AES_GCM_12_16:
            return 16;

        case AES_CCM_10_08:
        case AES_GCM_10_08_BROKEN:
        case AES_GCM_10_08:
            return 8;

        default: // unknown block encryption mode
            return 0;
    }
}

unsigned TLVstore::getIvlen(int mode)
{
    switch (mode)
    {
        case AES_CCM_12_16:
        case AES_GCM_12_16_BROKEN:
        case AES_GCM_12_16:
            return 12;

        case AES_CCM_10_08:
        case AES_GCM_10_08_BROKEN:
        case AES_CCM_10_16:
        case AES_GCM_10_08:
            return 10;

        default: // unknown block encryption mode
            return 0;
    }
}

encryptionmode_t TLVstore::getMode(int mode)
{
    switch (mode)
    {
        case AES_CCM_12_16:
        case AES_GCM_12_16_BROKEN:
        case AES_CCM_10_16:
        case AES_CCM_10_08:
        case AES_GCM_10_08_BROKEN:
            return AES_MODE_CCM;

        case AES_GCM_12_16:
        case AES_GCM_10_08:
            return AES_MODE_GCM;

        default: // unknown block encryption mode
            return AES_MODE_UNKNOWN;
    }
}

TLVstore* TLVstore::containerToTLVrecords(const string* data)
{
    if (data->empty())
    {
        return NULL;
    }

    TLVstore* tlv = new TLVstore();

    size_t offset = 0;

    string type;
    size_t typelen;
    string value;
    unsigned valuelen;
    size_t pos;

    size_t datalen = data->length();

    // T is a C-string
    // L is an unsigned integer encoded in 2 bytes (aka. uint16_t)
    // V is a binary buffer of "L" bytes

    // if the size of the container is greater than the 65538 (T.1, L.2, V.>65535),
    // then the container is probably an authring whose value is greater than the
    // maximum length supported by 2 bytes. Since the T is an empty string for this
    // type of attributes, we can directly assign the value to the record
    // note: starting from Nov2022, if the size of the record is too large,
    // the Length will be truncated to indicate the maximum size 65535 = 0xFFFF
    size_t maxSize = 1 + 2 + 0xFFFF;
    if (datalen >= maxSize && !(*data)[0])
    {
        tlv->set("", data->substr(3));
        return tlv;
    }

    while (offset < datalen)
    {
        // get the length of the Type string
        pos = data->find('\0', offset);
        typelen = pos - offset;

        // if no valid TLV record in the container, but remaining bytes...
        if (pos == string::npos || offset + typelen + 3 > datalen)
        {
            delete tlv;
            return NULL;
        }

        // get the Type string
        type.assign((char*)&(data->data()[offset]), typelen);
        offset += typelen + 1; // +1: NULL character

        // get the Length of the value
        valuelen = (unsigned char)data->at(offset) << 8 | (unsigned char)data->at(offset + 1);
        offset += 2;

        // if there's not enough data for value...
        if (offset + valuelen > datalen)
        {
            delete tlv;
            return NULL;
        }

        // get the Value
        value.assign((char*)&(data->data()[offset]),
                     valuelen); // value may include NULL characters, read as a buffer
        offset += valuelen;

        // add it to the map
        tlv->set(type, value);
    }

    return tlv;
}

TLVstore* TLVstore::containerToTLVrecords(const string* data, SymmCipher* key)
{
    if (data->empty())
    {
        return NULL;
    }

    unsigned offset = 0;
    encryptionsetting_t encSetting = (encryptionsetting_t)data->at(offset);
    offset++;

    unsigned ivlen = TLVstore::getIvlen(encSetting);
    unsigned taglen = TLVstore::getTaglen(encSetting);
    encryptionmode_t encMode = TLVstore::getMode(encSetting);

    if (encMode == AES_MODE_UNKNOWN || !ivlen || !taglen || data->size() < offset + ivlen + taglen)
    {
        return NULL;
    }

    byte* iv = new byte[ivlen];
    memcpy(iv, &(data->data()[offset]), ivlen);
    offset += ivlen;

    unsigned cipherTextLen = unsigned(data->length() - offset);
    string cipherText = data->substr(offset, cipherTextLen);

    unsigned clearTextLen = cipherTextLen - taglen;
    string clearText;

    bool decrypted = false;
    if (encMode == AES_MODE_CCM) // CCM or GCM_BROKEN (same than CCM)
    {
        decrypted = key->ccm_decrypt(&cipherText, iv, ivlen, taglen, &clearText);
    }
    else if (encMode == AES_MODE_GCM) // GCM
    {
        decrypted = key->gcm_decrypt(&cipherText, iv, ivlen, taglen, &clearText);
    }

    delete[] iv;

    if (!decrypted) // the decryption has failed (probably due to authentication)
    {
        return NULL;
    }
    else if (clearText
                 .empty()) // If decryption succeeded but attribute is empty, generate an empty TLV
    {
        return new TLVstore();
    }

    TLVstore* tlv = TLVstore::containerToTLVrecords(&clearText);
    if (!tlv) // 'data' might be affected by the legacy bug: strings encoded in UTF-8 instead of
              // Unicode
    {
        // retry TLV decoding after conversion from 'UTF-8 chars' to 'Unicode chars'
        LOG_warn << "Retrying TLV records decoding with UTF-8 patch";

        string clearTextUnicode;
        if (!Utils::utf8toUnicode((const byte*)clearText.data(), clearTextLen, &clearTextUnicode))
        {
            LOG_err << "Invalid UTF-8 encoding";
        }
        else
        {
            tlv = TLVstore::containerToTLVrecords(&clearTextUnicode);
        }
    }

    return tlv;
}

} // namespace mega
