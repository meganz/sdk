/**
 * @file tests/sdk_test.cpp
 * @brief Mega SDK test file
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

using namespace mega;
using ::testing::Test;

class MegaClientTest : public ::testing::Test {

protected:
  virtual void SetUp()
  {
        // do some initialization

        client = NULL;
        // instantiate app components: the callback processor (DemoApp),
        // the HTTP I/O engine (WinHttpIO) and the MegaClient itself
//        client = new MegaClient(new DemoApp, new CONSOLE_WAIT_CLASS,
//                                new HTTPIO_CLASS, new FSACCESS_CLASS,
//                        #ifdef DBACCESS_CLASS
//                                new DBACCESS_CLASS,
//                        #else
//                                NULL,
//                        #endif
//                        #ifdef GFX_CLASS
//                                new GFX_CLASS,
//                        #else
//                                NULL,
//                        #endif
//                                "SDKSAMPLE",
//                                "megatest/" TOSTRING(MEGA_MAJOR_VERSION)
//                                "." TOSTRING(MEGA_MINOR_VERSION)
//                                "." TOSTRING(MEGA_MICRO_VERSION));
  }

   virtual void TearDown()
   {
       // do some cleanup
   }

    MegaClient *client;

};

TEST_F(MegaClientTest, ClientNotNull)
{
    ASSERT_NE(client, NULL);
}
