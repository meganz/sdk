/**
 * @brief Mega SDK test file for server implementations (TCP, HTTP)
 *
 * (c) 2025 by Mega Limited, New Zealand
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

#include "SdkTest_test.h"

#include <gmock/gmock.h>

namespace
{

#ifdef HAVE_LIBUV
/**
 * Test for HTTP server using port 0, which also consist of:
 * - start two HTTP servers from a thread and no ports conflicting
 * - stop HTTP servers from a different thread, to allow TSAN to report any data races
 */
TEST_F(SdkTest, HttpServerCanUsePort0)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2, false));

    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));
    ASSERT_TRUE(megaApi[1]->httpServerStart(true, 0));
    ASSERT_TRUE(megaApi[0]->httpServerIsRunning());
    ASSERT_TRUE(megaApi[1]->httpServerIsRunning());

    std::async(std::launch::async,
               [&api = megaApi]()
               {
                   api[0]->httpServerStop();
                   api[1]->httpServerStop();
               })
        .get();

    CASE_info << "finished";
}

#endif

}
