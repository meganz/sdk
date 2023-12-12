#include "megaapi.h"

#include <gtest/gtest.h>

int main (int argc, char *argv[])
{
    ::mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
