#include "executable_dir.h"

// headers from gfxworker
#include "logger.h"

// headers from sdk
#include "megaapi.h"

#include <gtest/gtest.h>

int main (int argc, char *argv[])
{
    // init ExecutableDir
    mega_test::ExecutableDir::init(argv[0]);

    // log
    ::mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    ::mega::gfx::MegaFileLogger::get().initialize(".", "gfxworker_test_integration.log", false);
    // test
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
