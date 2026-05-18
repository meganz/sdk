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
 * Use is gated by a separate fu7-15+ refactor: today, most TEST_F bodies in
 * SdkWsUploadTest.cpp reference globalMegaTestHooks.<field> directly (the
 * MegaTestHooks struct itself is gated under MEGASDK_DEBUG_TEST_HOOKS_ENABLED
 * in include/mega/testhooks.h:43), so a runtime gate cannot stand alone for
 * those tests — they still need #ifdef gating at the field-access site.
 * See Goal2_deferred_items/g2c7_design_constraints.md for the audit.
 *
 * The single compile-safe site that uses this macro today is
 * OverquotaDuringTransfer (relies only on WsChunkSendOverquotaCapture, a
 * self-stubbing helper class).
 */

#pragma once

#include <gtest/gtest.h>

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
#  define WSUPLOAD_REQUIRE_TEST_HOOKS() do {} while (0)
#else
#  define WSUPLOAD_REQUIRE_TEST_HOOKS() \
       GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED"
#endif
