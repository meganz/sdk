#include "mega.h"
#include "gtest/gtest.h"

bool debug;

TEST(JSON, storeobject) {
  string in_str("Test");
  JSON j;
  j.storeobject (&in_str);
}

int main (int argc, char *argv[])
{
    return RUN_ALL_TESTS();
}
