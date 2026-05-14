/**
 * @file include/mega/transfer/ws/ws_encryption.h
 * @brief WS-upload chunk encryption helper (extracted from wsupload.cpp).
 *
 * Provides a lock-free `encryptChunk()` that runs the per-chunk encrypt + MAC
 * pass using a thread-local SymmCipher. The caller manages all locking around
 * the call (typically WsPool::sendChunk releases impl.uploadMutex around this
 * call so multiple worker threads can encrypt concurrently).
 */

#pragma once

#ifdef MEGA_USE_WSUPLOAD

#include "mega/crypto/cryptopp.h"
#include "mega/types.h"
#include "mega/utils.h"

#include <array>
#include <cstdint>

namespace mega
{
namespace ws
{

// Encrypt a single WS upload chunk in place using a thread-local SymmCipher
// derived from `key`. Returns the chunk's MAC map.
//
// The function is intentionally lock-free; concurrent calls on different
// threads are safe because each thread keeps its own SymmCipher instance
// (`thread_local`). Synchronisation around the buffer / file state is the
// caller's responsibility.
chunkmac_map encryptChunk(const std::array<byte, SymmCipher::KEYLENGTH>& key,
                          std::int64_t ctrIv,
                          m_off_t chunkPos,
                          int chunkLen,
                          byte* buf);

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
