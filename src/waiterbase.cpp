/**
 * @file waiterbase.cpp
 * @brief Generic waiter interface
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

#include "mega/waiter.h"
#include "mega/utils.h"

namespace mega {

std::atomic<dstime> Waiter::ds{0};
std::mutex Waiter::dsMutex;

// update monotonously increasing timestamp in deciseconds
void Waiter::bumpds()
{
    std::lock_guard<std::mutex> lock(dsMutex);
    ds = m_clock_getmonotonictimeDS();
}

void Waiter::init(dstime ds)
{
    maxds = ds;
}

// add events to wakeup criteria
void Waiter::wakeupby(EventTrigger* et, int flags)
{
    et->addevents(this, flags);
}

} // namespace
