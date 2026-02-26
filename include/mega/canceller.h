/**
 * @file mega/canceller.h
 * @brief Mega atomic canceller to be used for MegaClient code that should stop in order for app's
 * requests to be reached. Meant to be used for login / locallogout app requests.
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace mega
{

// 64-bit epoch. Wrap period is ~5.8e5 years at 1 bump/microsecond.
using cancel_epoch_t = std::uint64_t;

// Use this as a starting point reference for a further cancel_epoch_bump()
cancel_epoch_t cancel_epoch_snapshot() noexcept;

// Bump the global epoch: invalidates all inflight snapshots.
void cancel_epoch_bump() noexcept;

struct ScopedCanceller
{
    ScopedCanceller(const ScopedCanceller&) = delete;
    ScopedCanceller& operator=(const ScopedCanceller&) = delete;

    explicit ScopedCanceller(const cancel_epoch_t snapshot):
        m_snapshot{snapshot}
    {}

    ScopedCanceller() noexcept;

    bool triggered() const noexcept;

private:
    cancel_epoch_t m_snapshot{};
};

} // namespace mega