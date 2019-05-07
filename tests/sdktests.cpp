/**
 * @file tests/sdktests.cpp
 * @brief Mega SDK main test file
 *
 * (c) 2015 by Mega Limited, Wellsford, New Zealand
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

#include "mega.h"
#include "gtest/gtest.h"
#include "sdk_test.h"
#include <stdio.h>

using namespace mega;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;



int main (int argc, char *argv[])
{
    
    std::vector<char*> myargv(argv, argv + argc);

    for (auto it = myargv.begin(); it != myargv.end(); ++it)
    {
        if (string(*it) == "--CI")
        {
            g_runningInCI = true;
            myargv.erase(it);
            argc -= 1;
            break;
        }
    }

    remove("SDK.log");

#if defined(WIN32) && defined(NO_READLINE)
    WinConsole* wc = new CONSOLE_CLASS;
    wc->setShellConsole();
#endif

    InitGoogleTest(&argc, myargv.data());
    return RUN_ALL_TESTS();
}
