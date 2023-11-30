#include <gtest/gtest.h>
#include "megaapi.h"

int main (int argc, char *argv[])
{
    ::mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    testing::InitGoogleTest(&argc, argv);
    int rc = RUN_ALL_TESTS();
    return rc;
}
