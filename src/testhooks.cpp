/**
 * @file mega/testhooks.cpp
 * @brief helper classes for causing/simulating conditions for various tests of the mega SDK code
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

#include "mega/testhooks.h"


namespace mega
{
    // Always-compile MegaTestHooks struct. The struct definition and this
    // storage are unconditionally present so the hook-install ABI is
    // identical across NDEBUG and non-NDEBUG. Only the DEBUG_TEST_HOOK_*
    // macros in include/mega/testhooks.h remain gated on
    // MEGASDK_DEBUG_TEST_HOOKS_ENABLED, so Release builds emit zero
    // instructions at SDK-side hook call sites.
    MegaTestHooks globalMegaTestHooks;
}
