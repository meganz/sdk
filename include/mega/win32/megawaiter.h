/**
 * @file mega/win32/megawaiter.h
 * @brief Win32 event/timeout handling
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

#ifndef WAIT_CLASS
#define WAIT_CLASS WinWaiter

typedef ULONGLONG (WINAPI * PGTC)();

extern PGTC pGTC;

namespace mega {
class MEGA_API WinWaiter : public Waiter
{
    vector<HANDLE> handles;
    vector<int> flags;

    size_t index = 0;

public:
    int wait();

    bool addhandle(HANDLE handle, int);
    void notify();

    WinWaiter();
    ~WinWaiter();

#ifdef MEGA_MEASURE_CODE
    struct PerformanceStats
    {
        uint64_t waitTimedoutNonzero = 0;
        uint64_t waitTimedoutZero = 0;
        uint64_t waitIOCompleted = 0;
        uint64_t waitSignalled= 0;
    } performanceStats;
#endif

protected:
    HANDLE externalEvent;
};
} // namespace

#endif
