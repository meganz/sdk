#include "executable_dir.h"

#include "megaapi.h"

#include <gtest/gtest.h>

int main (int argc, char *argv[])
{
    // init ExecutableDir
    mega_test::ExecutableDir::init(argv[0]);

    // log
    ::mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    // test
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
