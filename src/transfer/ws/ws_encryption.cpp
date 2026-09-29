#ifdef MEGA_USE_WSUPLOAD

#include "mega/transfer/ws/ws_encryption.h"

#include <memory>

namespace mega
{
namespace ws
{

chunkmac_map encryptChunk(const std::array<byte, SymmCipher::KEYLENGTH>& key,
                          std::int64_t ctrIv,
                          m_off_t chunkPos,
                          int chunkLen,
                          byte* buf)
{
    chunkmac_map macs;
    thread_local std::unique_ptr<SymmCipher> tlsCipher;
    if (!tlsCipher)
        tlsCipher.reset(new SymmCipher(key.data()));
    else
        tlsCipher->setkey(key.data());

    macs.ctr_encrypt(chunkPos,
                     tlsCipher.get(),
                     buf,
                     static_cast<unsigned>(chunkLen),
                     chunkPos,
                     ctrIv,
                     false);
    return macs;
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
