/**
 * @file SecondTimer.h
 * @brief Coarse second-granularity stopwatch used by integration tests.
 *
 * Extracted from SdkTest_test.cpp anonymous-namespace helper. Used pervasively
 * across the test suite for elapsed-time guards (waitForX patterns, retry
 * windows). Resolution is 1 second since the underlying `m_time()` returns
 * `m_time_t` (POSIX time_t), which is sufficient for the >5s test budgets the
 * helper anchors.
 *
 * Header-only / fully inline so consumers may include without ODR concerns.
 */

#pragma once

#include "mega/types.h"

namespace mega::test::wsupload
{

struct second_timer
{
    ::mega::m_time_t t;
    ::mega::m_time_t pause_t;
    second_timer() { t = ::mega::m_time(); }
    void reset() { t = ::mega::m_time(); }
    void pause() { pause_t = ::mega::m_time(); }
    void resume() { t += ::mega::m_time() - pause_t; }
    size_t elapsed() { return static_cast<size_t>(::mega::m_time() - t); }
};

} // namespace mega::test::wsupload
