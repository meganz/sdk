#include "mega/canceller.h"

namespace mega
{

namespace
{
std::atomic<cancel_epoch_t> g_cancel_epoch{0};
}

cancel_epoch_t cancel_epoch_snapshot() noexcept
{
    return g_cancel_epoch.load(std::memory_order_relaxed);
}

void cancel_epoch_bump() noexcept
{
    g_cancel_epoch.fetch_add(1, std::memory_order_relaxed);
}

bool cancel_epoch_triggered(const cancel_epoch_t snapshot) noexcept
{
    return g_cancel_epoch.load(std::memory_order_relaxed) != snapshot;
}

ScopedCanceller::ScopedCanceller() noexcept
{
    m_snapshot = cancel_epoch_snapshot();
}

bool ScopedCanceller::triggered() const noexcept
{
    return cancel_epoch_triggered(m_snapshot);
}

} // namespace mega
