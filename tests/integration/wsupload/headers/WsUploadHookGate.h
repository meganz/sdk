/**
 * @file tests/integration/wsupload/WsUploadHookGate.h
 * @brief Runtime gating macro for WS-upload integration tests that depend on
 *        MEGASDK_DEBUG_TEST_HOOKS_ENABLED-only symbols.
 *
 * Use WSUPLOAD_REQUIRE_TEST_HOOKS() at the entry of a TEST_F body when the
 * body relies on hook callbacks installed via globalMegaTestHooks. The macro
 * resolves to a no-op in hooks-ON builds and to GTEST_SKIP in hooks-OFF
 * builds — so the test body always compiles, but only runs when hooks are
 * available.
 *
 * The hook-ABI redesign migrated WS-upload TEST_F bodies that rely on
 * MegaTestHooks struct fields to wrap their hook-callback invocations in the
 * always-compile MegaTestHookCallContext pattern, allowing the bodies to compile
 * in both hooks-ON and hooks-OFF builds. Combined with this macro at the body
 * entry, the GTEST_SKIP path keeps the runtime behaviour correct in hooks-OFF.
 *
 * Current usage: ~14+ TEST_F sites across SdkWsUploadTest.cpp (the macro and
 * the field-access wrappers were rolled out in lockstep). Search the
 * tests/integration/wsupload/ tree for `WSUPLOAD_REQUIRE_TEST_HOOKS()` to
 * enumerate the live sites.
 */

#pragma once

#include <gtest/gtest.h>

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
#  define WSUPLOAD_REQUIRE_TEST_HOOKS() do {} while (0)
#else
#  define WSUPLOAD_REQUIRE_TEST_HOOKS() \
       GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED"
#endif
