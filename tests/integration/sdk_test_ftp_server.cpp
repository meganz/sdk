#include "SdkTest_test.h"

#include <gmock/gmock.h>

/**
 * Test for FTP server using port 0, which also consist of:
 * - start two FTP servers from a thread and no ports conflicting
 * - stop FTP servers from a different thread, to allow TSAN to report any data races
 */
TEST_F(SdkTest, FtpServerCanUsePort0)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2, false));

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    ASSERT_TRUE(megaApi[1]->ftpServerStart(true, 0));
    ASSERT_TRUE(megaApi[0]->ftpServerIsRunning());
    ASSERT_TRUE(megaApi[1]->ftpServerIsRunning());

    std::async(std::launch::async,
               [&api = megaApi]()
               {
                   api[0]->ftpServerStop();
                   api[1]->ftpServerStop();
               })
        .get();

    CASE_info << "finished";
}
